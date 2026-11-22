## Summary

ANV exposes native ASTC 5x5 on Gfx9. Sampling it in the same dispatch as a HiZ-compressed depth texture hangs the HD 620 on the first submission. Iris avoids this hang (mesa#1892, MR 3251) by faking ASTC 5x5 with uncompressed textures on Gfx9. ANV had no equivalent.

Hardware: HD 620 (KBL GT2), i915, Linux 7.0. Driver: Mesa main `0e9e1086bb86` plus local patches.

## Results ([`hang-proofs/astc-ccs.c`](https://github.com/CodeGrogu/mesa/tree/hd620-audit/HD620-full-audit/hang-proofs/astc-ccs.c), real ASTC data from astcenc 5.7.1)

| Aux-compressed surface | ASTC | Result |
| --- | --- | --- |
| HiZ depth (`AUX_HIZ` confirmed in batch decode) | 5x5 native | **`VK_ERROR_DEVICE_LOST` on submission 0**, `GPU HANG: ecode 9:1:8ed8fff3`, every attempt |
| HiZ depth | 4x4 native | 200 submissions completed |
| depth with `INTEL_DEBUG=nohiz` | 5x5 native | 200 completed |
| CCS_E colour (compute and 3D, one draw or alternating draws, random and real blocks) | 5x5 native | completed, no hang |
| HiZ depth | 5x5 emulated (fix) | 600 completed |

The same binary hangs with `ANV_NATIVE_ASTC_5X5=true` and runs cleanly by default, which isolates the emulation as what prevents the hang. On this KBL part the trigger was HiZ; no CCS_E combination tested hung.

## Fix

On branch `hd620-round2`, ASTC 5x5 is decoded into the RGBA8 emulation plane on Gfx9, and native ASTC is kept for other block sizes. `ANV_NATIVE_ASTC_5X5=true` restores the old behaviour.
