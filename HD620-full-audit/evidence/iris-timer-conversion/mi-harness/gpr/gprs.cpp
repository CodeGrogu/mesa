#include "gpr/mi_builder_test_gprs.cpp"
TEST_F(mi_builder_test, gpr_budget)
{
   const uint32_t freqs[] = { 12000000, 12500000, 19200000, 38400000, 12333333, 0x7fffffff };
   for (unsigned j = 0; j < ARRAY_SIZE(freqs); j++) {
      batch_offset = 0;
      mi_builder_init(&b, &devinfo, this);
      const uint64_t in[2] = { 0xffffffffbull, 0x123456789ull };
      memcpy(input, in, 16);
      struct mi_value start_val = in_mem64(0), end_val = in_mem64(8);
      struct mi_value wrap = mi_iand(&b, mi_ult(&b, end_val, start_val), mi_imm(1ull << 36));
      struct mi_value r = mi_iadd(&b, mi_isub(&b, end_val, start_val), wrap);
      r = mi_umul_udiv64_imm(&b, r, 1000000000, freqs[j]);
      mi_store(&b, out_mem64(0), r);
      mi_store(&b, out_mem64(8), mi_umul_udiv64_imm(&b, in_mem64(0), 1000000000, freqs[j]));
      EXPECT_EQ(b.gprs, 0u) << "leaked GPRs f=" << freqs[j];
      submit_batch();
      EXPECT_EQ(*(uint64_t *)output, cpu_timebase_scale(freqs[j], in[0] > in[1] ? (1ull << 36) + in[1] - in[0] : in[1] - in[0]));
      EXPECT_EQ(*(uint64_t *)(output + 8), cpu_timebase_scale(freqs[j], in[0]));
   }
}
int main() { int f = 0; for (auto &r : ::testing::registry()) { if (!strstr(r.name, "gpr_budget")) continue; auto *t = r.make(); try { t->SetUp(); t->TestBody(); } catch (...) {} t->TearDown(); delete t; } printf("failures %d\n", ::testing::failures()); return ::testing::failures() != 0; }
