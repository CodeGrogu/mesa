# Gen9-only flushes measured on HD 620

A measurement-only driver (`../patches/MEASUREMENT-ONLY-gen9-stall-toggles.patch`, `ccc60d97…`) adds an `ANV_GEN9_STALL_EXP` bitmask that removes four Gen9-only flushes or state re-emits. The toggles were confirmed to act. With bit 1, the batch decode had exactly 20,000 fewer `PIPE_CONTROL`s (60,250 → 40,250). Bits 4 and 8 removed the expected logged flushes. Bit 2 changes push-constant allocation, which does not show up as a flush. The toggles exist only in that driver. The project tree and the round-2 driver leave all four sites unchanged.

`stall-bench.c` records 1,000 iterations per command buffer, takes the median of 9 submissions, and reads back pixels to check output. `run-timing.sh` interleaves control and toggled runs over 8 rounds and alternates their order.

| Bit | Site (`genX_cmd_buffer.c` / `genX_cmd_draw.c`) | Scenario | GPU ns/iter, control → toggled | Paired GPU delta per iteration | Verdict |
| --- | --- | --- | --- | --- | --- |
| 1 | Null `PIPE_CONTROL` before VF cache invalidate (SKL PRM requirement) | `vf` | 8218.7 → 8178.5 (−0.5%) | median −50 ns, range −189 to +50 | No measurable cost. Keep. |
| 2 | Forced push-constant reallocation, gfx9 and gfx11 (#11064) | `push` | 541.6 → 575.3 | median +34 ns, range −472 to +331 | No measurable cost (GPU or CPU). Keep. |
| 4 | `MEDIA_VFE_STATE` re-emit after GPGPU → 3D | `vfe` | 9156.6 → 8692.2 (−5.1%) | median −507 ns, all 8 rounds negative | About 0.5 µs per compute → 3D switch. The comment ties it to geometry flicker with an unknown cause, so removal would need a flicker reproducer first. |
| 8 | State-cache invalidate after `load_clear_color` | `load` | 3228.8 → 2397.7 (−25.7%) | median −659 ns, 7 of 8 rounds negative | About 0.66 µs per render pass that loads a fast-cleared attachment. The largest measured cost. |

Bit 8 correctness checks, all with the invalidate removed:

- `blend`: each of 300 iterations fast-clears to a new colour and additively blends over it.
- `blend2`: 1,000 barrier-free `LOAD` scopes, each blending onto its own untouched cell. The clear colour changes between submissions, so surface-state memory is reused after each pool reset.

Every pixel was correct in every run (`ANV_GEN9_STALL_EXP=8`, 3 and 4 runs). The flush log confirmed 3,000 fast clears and clear-colour reloads in `blend`, and no per-pass invalidates in the toggled `blend2`.

**Decision: no source change.** Bits 1 and 2 cost nothing measurable. Bit 4 has a real cost but guards against an unexplained hardware symptom. Bit 8 is the only plausible removal. These tests are consistent with the source comment ("In testing, SKL doesn't actually seem to need this"), but the SKL PRM requires the invalidate when surface state in memory changes. A pixel test cannot prove an L1 state-cache race absent, so keeping the invalidate is the safe default.
