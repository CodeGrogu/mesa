# Gfx9 hang workarounds proven on HD 620

Both workarounds from round 2 were tested against real GPU hangs on this HD 620 (KBL GT2). The desktop stayed up throughout. This Claude session runs inside the gnome-shell session, so stopping the desktop was not possible. i915 recovered each hang with a render-engine reset of the guilty context ("Resetting rcs0 for preemption time out"). The default 640 ms preemption timeout and 2.5 s heartbeat applied.

Drivers:

- **control:** the eight-patch checkout build, which has neither fix.
- **mr45047:** the checkout with only upstream MR 45047.
- **fixed:** the round-2 build `31ced66e…`, which also contains the ASTC 5x5 emulation and the scratch fix.

## SIMD32 FP16 `mad` with a conditional modifier (Mesa #16526)

`cmod-v1.comp` is an FP16 multiply-add followed by a leaky ReLU, the ncnn pattern. At required subgroup size 32, the control compiler emits the instruction the Skylake PRM forbids: `mad (32) (lt)f0.0 … :hf {Align16}` (`asm-cmod-v1-control.txt`). The fixed compiler splits it into `mad (16)` and `mad (16|M16)` (`asm-cmod-v1-fixed.txt`). `cmod-v2` and `cmod-v3` produce 8 and 1 such instructions on the control.

| Driver | Run | Result |
| --- | --- | --- |
| control | cmod-v1, SIMD32 | **`VK_ERROR_DEVICE_LOST` on submission 0.** `GPU HANG: ecode 9:1:8ed8fff3` (`cmod-control-*.txt`). |
| control | cmod-v1, SIMD16 | 20/20 completed |
| fixed | cmod-v1/v2/v3, SIMD32, 3 × 50 each | 450/450 completed, no hang |
| mr45047 | cmod-v1/v2/v3, SIMD32, 50 each | 150/150 completed, no hang |

## ASTC 5x5 alongside aux-compressed data in the sampler (Mesa #1892)

`astc-ccs.c` samples an aux-compressed surface and an ASTC texture in one compute dispatch. `astc-ccs-gfx.c` does the same from fragment shaders, either in one draw (`both`) or in alternating draws (`alt`). Batch decodes confirmed the surface states: `AUX_CCS_E` on the colour image (`bat-astc.txt`), `AUX_HIZ` on the depth view (`bat-hiz.txt`), and native `ASTC_LDR_2D_5X5_FLT16`. Real blocks came from Arm's astcenc 5.7.1 (`test-5x5.astc`, `test-4x4.astc`); runs with random blocks are labelled.

| Driver | Aux surface | ASTC | Result |
| --- | --- | --- | --- |
| control | CCS_E colour, compute | 5x5 native (random blocks, then 2048² stress, then real blocks) | completed, no hang (50, 300, 10 and 300 submissions) |
| control | CCS_E colour, 3D `both` and `alt` | 5x5 native (random and real blocks) | completed, no hang (200 + 300 each) |
| control | **HiZ depth**, compute | **5x5 native, real blocks** | **`VK_ERROR_DEVICE_LOST` on submission 0.** `GPU HANG: ecode 9:1:8ed8fff3`, reproduced on every attempt |
| fixed, `ANV_NATIVE_ASTC_5X5=true` | HiZ depth, compute | 5x5 native | **`VK_ERROR_DEVICE_LOST` on submission 0** |
| fixed (default: 5x5 emulated) | HiZ depth, compute | 5x5 | 3 × 200 completed, no hang |
| control | HiZ depth, compute | 4x4 native | 200 completed |
| control, `INTEL_DEBUG=nohiz` | depth without HiZ | 5x5 native | 200 completed |

On this KBL part, the hang needs HiZ data and native ASTC 5x5 in the sampler at the same time. The CCS_E combination never hung across the cases above. The same fixed binary hangs with emulation turned off and runs cleanly with it on, which isolates the ASTC 5x5 emulation as what prevents the hang. Iris already avoids this hang through the same mechanism. Fragment-shader HiZ sampling was not tested separately.

### Note on long 3D batches

The first `astc-ccs-gfx` build used 1024² targets with 64 full-screen draws. It was reset even with CCS-only sampling and no ASTC. i915 disables object-level 3D preemption on Gen9 (`intel_workarounds.c` in the kernel sweep), so a multi-second render batch cannot yield to the compositor within 640 ms. Those resets were artefacts of the test and are excluded above. All 3D results use 256² batches that finish inside the timeout. Compute is preempted mid-thread, which is why the 1.2 s compute stress dispatches never triggered a reset.
