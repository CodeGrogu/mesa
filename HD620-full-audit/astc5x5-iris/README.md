# Item 46: native ASTC 5x5 in Iris on Gfx9 (2026-11-22)

Commit `iris: support ASTC 5x5 natively on Gfx9 with the sampler workaround` on `hd620-round2`.

Iris used to refuse ASTC 5x5 on Gfx9, so st/mesa decoded it to RGBA8: 32 bits per texel instead of
1.28. The hardware hangs when ASTC 5x5 shares the sampler cache with CCS or HiZ data (#1892). The
change ports i965's workaround:

- When a draw or dispatch samples ASTC 5x5, every other texture it samples is resolved and sampled
  with aux disabled. This is done in `iris_gfx9_astc5x5_prepare()` and
  `iris_resource_texture_aux_usage()`.
- When the sampler cache may hold one kind of data and the next draw, dispatch or BLORP source
  brings the other, a CS stall and a texture-cache invalidate separate them. The tracking is per
  batch and is applied right before the command is emitted.
- `IRIS_GFX9_EMULATE_ASTC_5X5=true` restores the emulation.

## Test

[`../gl-astc5x5.py`](../gl-astc5x5.py) uses real astcenc 5.7.1 blocks (`../hang-proofs/test-5x5.astc`),
a 128² `DEPTH_COMPONENT32F` texture (HiZ) and an RGBA8 texture (CCS_E). Each iteration rewrites both
aux textures and then samples them with ASTC in four ways: ASTC only, all three in one draw,
alternating ASTC-only and aux-only draws, and a compute dispatch. A batch decode confirmed
`ASTC_LDR_2D_5X5_FLT16` (format 585) together with `AUX_HIZ` and `AUX_CCS_E` surface states. Each
iteration emits 8 workaround stall/invalidate pairs (`INTEL_DEBUG=pc`).

| Build | Result |
| --- | --- |
| Workaround compiled out (test switch, never committed) | **GPU hang on iteration 1**: `GPU HANG: ecode 9:1:85dffffb` and `9:1:8ed9fff2`, rcs0 reset ([control-noWA-dmesg.txt](control-noWA-dmesg.txt)) |
| Same binary with the switch off, and the committed build | 3 × 200 iterations each, no hang, GL error 0, no reset ([fixed.txt](fixed.txt)) |
| Images versus st/mesa's software decode (`IRIS_GFX9_EMULATE_ASTC_5X5=true`) | All four cases within 1/255 ([`../astc5x5-compare.py`](../astc5x5-compare.py)) |

A glmark2 subset (texture, shading, refract, desktop, ideas, terrain) ran normally on the installed
build. No GL CTS is built here, so the GL conformance ASTC cases were not run.
