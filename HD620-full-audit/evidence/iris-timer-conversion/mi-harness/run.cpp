/* Runs mi_builder_test.cpp (from the read-only source tree) plus a check of
 * the Iris TIME_ELAPSED GPU expression, on the local GPU. */
#include "src/intel/common/tests/mi_builder_test.cpp"

static uint64_t
iris_cpu_time_elapsed(uint32_t f, uint64_t t0, uint64_t t1)
{
   /* iris_raw_timestamp_delta() + calculate_result_on_cpu() */
   uint64_t d = t0 > t1 ? (1ull << 36) + t1 - t0 : t1 - t0;
   return cpu_timebase_scale(f, d);
}

TEST_F(mi_builder_test, iris_time_elapsed_expression)
{
   const uint64_t pairs[][2] = {
      { 5, 10 }, { 10, 10 }, { 0, 0xfffffffffull },
      { 0xffffffffbull, 3 },            /* 36-bit wrap */
      { 0xfffffffffull, 0 },            /* wrap by one tick */
      { 0x123456789ull, 0x123456789ull + 12000000ull * 60 },
      { 0x800000000ull, 0x7ffffffffull }, /* wrap of almost 2^36 */
      { 0xffffffffffffull, 1 },         /* out-of-range values: match CPU */
   };
   const uint32_t freqs[] = { 12000000, 12500000, 19200000, 24000000, 38400000, 12333333 };
   for (unsigned j = 0; j < ARRAY_SIZE(freqs); j++) {
      for (unsigned i = 0; i < ARRAY_SIZE(pairs); i++) {
         batch_offset = 0;
         mi_builder_init(&b, &devinfo, this);
         memcpy(input, pairs[i], 16);
         struct mi_value start_val = in_mem64(0), end_val = in_mem64(8);
         /* Verbatim from calculate_result_on_gpu() PIPE_QUERY_TIME_ELAPSED */
         struct mi_value wrap =
            mi_iand(&b, mi_ult(&b, end_val, start_val), mi_imm(1ull << 36));
         struct mi_value result = mi_iadd(&b, mi_isub(&b, end_val, start_val), wrap);
#ifdef NEGATIVE_CONTROL
         /* Pre-fix code: truncated scale, no wrap handling. */
         mi_value_unref(&b, result);
         result = mi_imul_imm(&b, mi_isub(&b, end_val, start_val), 1000000000ull / freqs[j]);
#else
         result = mi_umul_udiv64_imm(&b, result, 1000000000, freqs[j]);
#endif
         mi_store(&b, out_mem64(0), mi_value_ref(&b, result));
         mi_store(&b, out_mem32(8), result);
         submit_batch();
         uint64_t e = iris_cpu_time_elapsed(freqs[j], pairs[i][0], pairs[i][1]);
         EXPECT_EQ(*(uint64_t *)output, e) << "f=" << freqs[j] << " pair " << i;
         EXPECT_EQ(*(uint32_t *)(output + 8), (uint32_t)e) << "f=" << freqs[j] << " pair " << i;
      }
   }
}

#include <algorithm>
/* Batch dwords and GPU time (CS TIMESTAMP ticks) of one conversion. */
TEST_F(mi_builder_test, zz_cost)
{
   const uint32_t freqs[] = { 12000000, 12500000, 19200000, 38400000, 12333333, 0x7fffffff };
   for (unsigned j = 0; j < ARRAY_SIZE(freqs); j++) {
      for (int variant = 0; variant < 4; variant++) {
         std::vector<uint64_t> ticks;
         unsigned dwords = 0;
         for (int rep = 0; rep < 21; rep++) {
            batch_offset = 0;
            mi_builder_init(&b, &devinfo, this);
            const uint64_t in[2] = { 0xffffffffbull, 0x123456789ull };
            memcpy(input, in, 16);
            mi_store(&b, out_mem64(16), mi_reg64(0x2358));
            mi_builder_flush_math(&b);
            unsigned start = batch_offset;
            struct mi_value start_val = in_mem64(0), end_val = in_mem64(8), r;
            uint32_t scale = 1000000000ull / freqs[j];
            switch (variant) {
            case 0: /* old TIMESTAMP */
               r = mi_iand(&b, mi_imm((1ull << 36) - 1), mi_imul_imm(&b, start_val, scale));
               break;
            case 1: /* new TIMESTAMP */
               r = mi_umul_udiv64_imm(&b, start_val, 1000000000, freqs[j]);
               break;
            case 2: /* old TIME_ELAPSED */
               r = mi_imul_imm(&b, mi_isub(&b, end_val, start_val), scale);
               break;
            default: { /* new TIME_ELAPSED */
               struct mi_value wrap = mi_iand(&b, mi_ult(&b, end_val, start_val), mi_imm(1ull << 36));
               r = mi_iadd(&b, mi_isub(&b, end_val, start_val), wrap);
               r = mi_umul_udiv64_imm(&b, r, 1000000000, freqs[j]);
            } }
            mi_store(&b, out_mem64(0), r);
            mi_builder_flush_math(&b);
            dwords = (batch_offset - start) / 4;
            mi_store(&b, out_mem64(24), mi_reg64(0x2358));
            submit_batch();
            ticks.push_back(*(uint64_t *)(output + 24) - *(uint64_t *)(output + 16));
         }
         std::sort(ticks.begin(), ticks.end());
         static const char *names[] = { "old TIMESTAMP", "new TIMESTAMP", "old TIME_ELAPSED", "new TIME_ELAPSED" };
         printf("COST f=%u %-17s %6u dwords, median %llu ticks (%.2f us), min %llu max %llu\n",
                freqs[j], names[variant], dwords, (unsigned long long)ticks[10],
                ticks[10] * 1e6 / devinfo.timestamp_frequency,
                (unsigned long long)ticks[0], (unsigned long long)ticks[20]);
      }
   }
   printf("COST device timestamp_frequency %llu\n", (unsigned long long)devinfo.timestamp_frequency);
}

int main(int argc, char **argv)
{
   const char *filter = argc > 1 ? argv[1] : NULL;
   int ran = 0, failed_tests = 0;
   for (auto &r : ::testing::registry()) {
      if (filter && !strstr(r.name, filter))
         continue;
      int before = ::testing::failures();
      ::testing::Test *t = r.make();
      try { t->SetUp(); t->TestBody(); } catch (::testing::Fatal &) {}
      try { t->TearDown(); } catch (::testing::Fatal &) {}
      delete t;
      bool ok = ::testing::failures() == before;
      printf("[%s] %s\n", ok ? "  OK  " : "FAILED", r.name);
      ran++; failed_tests += !ok;
   }
   printf("%d tests, %d failed, %ld checks, %d failed checks\n", ran, failed_tests,
          ::testing::checks(), ::testing::failures());
   return failed_tests != 0;
}
