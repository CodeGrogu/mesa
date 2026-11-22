# Item 31: int8 DPAS emulation on Gfx9 (2026-11-22)

`brw_lower_dpas.cpp` lowers DPAS on Gfx9 (exposed with `INTEL_LOWER_DPAS=1`)
with MULs, after widening each src2 row's packed bytes to words. A FINISHME
said Gfx9 should not need that widening. It is right that the widening is not
needed for correctness. It is wrong that removing it helps.

`coopmat-bench` runs an 8x32 by 32x8 int8 cooperative-matrix multiply-add
1024 times per subgroup over 2688 tiles. It checks every result exactly and
times the dispatch with timestamps. The builds are the same debugoptimized
tree, run interleaved, 3 rounds of 11 runs ([bench-1.txt](bench-1.txt),
[bench-2.txt](bench-2.txt)):

| Lowering | GMAC/s (signed / unsigned) | Exact |
| --- | --- | --- |
| Upstream: widen src2 to words | **18.4–18.7 / 18.2–18.6** | yes |
| Read src2 as scalar bytes (the FINISHME) | 16.4–16.6 / 16.5–16.6 (−11%) | yes |
| Also widen src1 to words once per DPAS | 12.8–12.9 / 12.6–12.8 (−31%) | yes |

The scalar-byte form drops 8 instructions per DPAS (628 → 620) but is slower
on hardware. Both of the first two forms pass the 108 supported int8
`matrixmuladd` CTS cases (0 failures). The code is unchanged; the comment now
records the measurement (commit `brw: record why Gfx9 int8 DPAS lowering
widens src2 to words`).

Build: `coopmat.comp` → `sint8.spv` / `uint8.spv` (glslangValidator, `-DUNSIGNED`),
`coopmat-bench.c` built like `fp16-simd32/lanemask.c`.
Run: `INTEL_LOWER_DPAS=1 ./coopmat-bench sint8.spv signed 1024 11`.
