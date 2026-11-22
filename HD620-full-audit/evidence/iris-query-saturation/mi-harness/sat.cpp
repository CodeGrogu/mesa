/* MI_MATH check of the Iris 32-bit query-buffer saturation
 * (iris_get_query_result_resource -> mi_umin_imm), on the local GPU.
 *
 * Reuses the iris-timer-conversion harness: run.cpp there includes the
 * unmodified src/intel/common/tests/mi_builder_test.cpp, the gtest stand-in
 * and main().  Build with -I<that harness dir>; see build.sh.
 */
#include "run.cpp"

#ifndef MI_PREDICATE_RESULT
#define MI_PREDICATE_RESULT 0x2418
#endif

/* BEGIN verbatim copy of mi_umin_imm() from iris_query.c (check-sync.sh) */
static struct mi_value
mi_umin_imm(struct mi_builder *b, struct mi_value val, uint64_t limit)
{
   if (val.type == MI_VALUE_TYPE_IMM)
      return mi_imm(MIN2(mi_value_to_u64(val), limit));

   /* Load memory/register sources once; val is used three times below. */
   assert(!val.invert);
   val = mi_value_to_gpr(b, val);

   struct mi_value mask = mi_ult(b, mi_imm(limit), mi_value_ref(b, val));
   struct mi_value diff = mi_ixor(b, mi_value_ref(b, val), mi_imm(limit));
   return mi_ixor(b, val, mi_iand(b, diff, mask));
}
/* END verbatim copy */

static const uint64_t sat_values[] = {
   0, 1, 0x7ffffffeull, 0x7fffffffull, 0x80000000ull, 0xb2d05e00ull /* 3e9 */,
   0xfffffffeull, 0xffffffffull, 0x100000000ull, 0x17fffffffull,
   0x24f1d4a8fc0ull /* ~2.5e12 ns, a HD 620 GL_TIMESTAMP */,
   0xffffffff7fffffffull, 0x8000000000000000ull, ~0ull,
};
static const uint64_t sat_limits[] = { 0x7fffffff /* I32 */, 0xffffffff /* U32 */ };

/* Source kinds seen by iris_get_query_result_resource: a GPR computed by
 * MI_MATH (every query but TIMESTAMP_RAW), raw memory (TIMESTAMP_RAW:
 * result = start_val), and the 0/1 boolean (mi_iand(mi_nz(), 1)). */
enum sat_src { SRC_GPR, SRC_MEM, SRC_BOOL };

TEST_F(mi_builder_test, iris_query_saturation)
{
   for (unsigned s = SRC_GPR; s <= SRC_BOOL; s++) {
      for (unsigned l = 0; l < ARRAY_SIZE(sat_limits); l++) {
         for (unsigned i = 0; i < ARRAY_SIZE(sat_values); i++) {
            const uint64_t v = sat_values[i], limit = sat_limits[l];
            batch_offset = 0;
            mi_builder_init(&b, &devinfo, this);
            const uint64_t in[3] = { v, 1 /* landed */, 0 /* not landed */ };
            memcpy(input, in, sizeof(in));

            struct mi_value r;
            uint64_t logical;
            if (s == SRC_MEM) {
               r = in_mem64(0);
               logical = v;
            } else if (s == SRC_BOOL) {
               r = mi_iand(&b, mi_nz(&b, in_mem64(0)), mi_imm(1));
               logical = v != 0;
            } else {
               r = mi_iadd(&b, in_mem64(0), mi_imm(0));
               logical = v;
            }
#ifdef NEGATIVE_CONTROL
            /* Pre-fix code: low 32 bits, no clamp. */
#else
            r = mi_umin_imm(&b, r, limit);
#endif
            /* Iris stores exactly once; this test stores the same value four
             * ways, so it takes three extra references. */
            r = mi_value_ref(&b, r);
            r = mi_value_ref(&b, r);
            r = mi_value_ref(&b, r);
            mi_store(&b, out_mem32(0), r);                       /* PIPE_QUERY_WAIT */
            mi_store(&b, mi_reg32(MI_PREDICATE_RESULT), in_mem64(8));
            mi_store_if(&b, out_mem32(8), r);                    /* NO_WAIT, landed */
            mi_store(&b, mi_reg32(MI_PREDICATE_RESULT), in_mem64(16));
            mi_store_if(&b, out_mem32(16), r);                   /* NO_WAIT, not landed */
            mi_store(&b, out_mem64(24), r);                      /* full 64-bit min */
            EXPECT_EQ(b.gprs, 0u) << "GPR leak, src " << s;
            submit_batch();

            const uint64_t e = MIN2(logical, limit);
            const char *src_name[] = { "gpr", "mem", "bool" };
            EXPECT_EQ(*(uint32_t *)(output + 0), (uint32_t)e)
               << src_name[s] << " limit " << limit << " v " << v;
            EXPECT_EQ(*(uint32_t *)(output + 8), (uint32_t)e)
               << src_name[s] << " predicated, limit " << limit << " v " << v;
            EXPECT_EQ(*(uint32_t *)(output + 16), (uint32_t)canary)
               << src_name[s] << " predicate false wrote, v " << v;
            EXPECT_EQ(*(uint64_t *)(output + 24), e)
               << src_name[s] << " 64-bit min, limit " << limit << " v " << v;
            for (unsigned off = 4; off <= 20; off += 8)
               EXPECT_EQ(*(uint32_t *)(output + off), (uint32_t)canary)
                  << "32-bit store wrote byte " << off;
         }
      }
   }
}

/* Peak GPR use of the clamp on top of a held GPR result (Iris pool: 15). */
TEST_F(mi_builder_test, iris_query_saturation_gprs)
{
   batch_offset = 0;
   mi_builder_init(&b, &devinfo, this);
   struct mi_value r = mi_iadd(&b, in_mem64(0), mi_imm(0));
   unsigned before = __builtin_popcount(b.gprs);
   r = mi_umin_imm(&b, r, 0x7fffffff);
   unsigned after = __builtin_popcount(b.gprs);
   mi_store(&b, out_mem32(0), r);
   EXPECT_EQ(before, 1u);
   EXPECT_EQ(after, 1u);
   EXPECT_EQ(b.gprs, 0u);
   mi_builder_flush_math(&b);
   printf("SAT clamp: %u batch dwords incl. setup and store\n", batch_offset / 4);
   submit_batch();
}
