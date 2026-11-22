# brw compiler on Gfx9: items 28, 29, 30 and 32 (2026-11-22)

HD 620 (KBL GT2), round-2 tree. Item 31 (int8 DPAS) is in [`../dpas-int8/`](../dpas-int8/README.md).

## 28. Spill single GRFs instead of whole VGRFs: already done upstream

`brw_reg_alloc::spill_reg()` fills and spills only the register range each instruction accesses
(`calculate_unspill_ranges()`, added with upstream `229268911af` "brw: Refactor selection of spill
ranges" and its follow-ups). The comment above the loop still said the whole VGRF was spilled; it is
corrected in `brw: fix stale comment about spilling whole VGRFs`. No code change was needed.

## 29. SIMD16 for 32-bit quad swizzles and mixed-mode float: no change possible

- On Gfx9 a 32-bit `QUAD_SWIZZLE` is only emitted for vertical and diagonal quad swaps
  (`brw_lower_quad_swap`). The SIMD8 limit splits that one instruction into two Align16 SIMD8 MOVs;
  it does not drop the shader to SIMD8. The PRM forbids SIMD16 Align16 DW operations, and every
  Align1 form needs as many or more MOVs (the generic path needs four).
- The mixed-mode float SIMD8 caps are PRM restrictions ("No SIMD16 in mixed mode when destination
  is f32"). Converting first and then running a SIMD16 f32 operation is also two instructions, so
  there is nothing to gain.

## 30. MAD → MAC fusion on Gfx9: already done for every legal case

Gfx9 three-source instructions are Align16 only, and their operand encoding
(`encode_operand_src_reg_three_src`) has no register-file field, so a MAD source can only be a GRF:
MAD can never read the accumulator. The only fusion the hardware can express is MAD → MAC, and
`brw_opt_mac` already does it on Gfx9. [`fma.comp`](fma.comp) (two `dot()`s) compiles to
`mul acc0` followed by seven `mac`s ([`fma-cs.asm`](fma-cs.asm)).

## 32. Gfx9 cost model calibration

### Latency

[`run-lat.sh`](run-lat.sh) times one thread running dependent chains of 16 and 48 operations in a loop
([`lat8/`](lat8), [`lat16/`](lat16)). The difference cancels the loop overhead. The GPU clock was pinned
at 1000 MHz (`gt_min_freq_mhz`), so GPU timestamps are cycles. Without pinning, RPS moved the clock
between 300 and 983 MHz and the results were unusable. The in-shader `clock2x32ARB` counter was also
unusable: about one sample in ten wrapped or jumped.

Measured versus the model's prediction for the same straight-line code, in cycles per operation
(two sweeps averaged, [`model-vs-hw.txt`](model-vs-hw.txt), raw data in `lat-ns-*.txt`):

| Chain | SIMD8 measured / model | SIMD16 measured / model |
| --- | --- | --- |
| fadd, fmul | 6.1 / 6 | 8.2–9.2 / 8 |
| fmad | 11.1 / 10 | 13.2 / 12 |
| dadd, dmul, dmad | 20.7–21.9 / 20 | 20.1–21.3 / 20 |
| iadd | 10.6 / 10 | 12.6 / 12 |
| imul | 18.0 / 16 | 21.2 / 20 |
| rcp, sqrt, rsq | 18.1–19.4 / 18 | 22.1–23.3 / 22 |
| sin, cos | 18.1–18.2 / 18 | 22.1–23.2 / 22 |
| **exp2(x)·z** | **32.2 / 28** | **37.2 / 34** |
| **log2(x)+y** | **31.6 / 28** | **36.2 / 34** |
| idiv | 31.8 / 30 | 44.4 / 52 |

The model is within about 2 cycles everywhere except EXP2/LOG2, which take about 4 cycles longer than
the other extended-math functions. Commit `brw: give Gfx9 EXP2 and LOG2 their measured latency`
gives them their own Gfx9 entry. The model now predicts 32 / 38 for both. Integer division at SIMD16
is over-estimated by 8 cycles. It was left alone, because integer division is rare in fragment
shaders and the error is in the conservative direction.

### The SIMD32 decision

On Gfx9 the model's only consumer that changes code generation is the fragment-shader choice between
SIMD16 and SIMD32. glmark2 at 1920×1080, 14 scenes, two interleaved rounds each
([`simd32/`](simd32)):

- The recalibrated model makes exactly the same choices as before: 42 SIMD32 variants rejected
  either way.
- Default and SIMD16-only are indistinguishable. Forcing SIMD32 (`INTEL_SIMD_DEBUG=fs8,fs16,fs32`):
  effect2d +7.5%, desktop +4.5%, cel +4.3%, phong +2%, ideas −5%, jellyfish −1.3%, bump −1%, the
  rest within noise. The average is about +0.8%.
- A temporary print of the model's throughput ([`simd32/tput.txt`](simd32/tput.txt)) shows why.
  Almost every SIMD16/SIMD32 pair scores exactly 0.1244, a tie, and ties keep SIMD16. 0.1244 is
  16 × 7 threads / 900 cycles, which is the render-target write entry (450 busy cycles per 8 pixels,
  marked `XXX`). So the model treats nearly every Gfx9 fragment shader as bound by framebuffer
  writes, and the choice comes down to the tie-break.
- Halving or quartering the sampler busy cost changed none of the 28 decisions in a subset run.

Because the shaders that gain and the ones that lose all score as identical ties, no tie-break or
recalibration built on the model can separate them. Preferring SIMD32 on ties would trade a 7.5% gain
for a 5% loss for an average of under 1%. The decision logic is left unchanged.
