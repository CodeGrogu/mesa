## Summary

ANV's `VK_EXT_host_image_copy` paths write the wrong data into the ASTC emulation plane. On Gfx9 non-LP (for example the HD 620), UNORM ASTC uploaded with `vkCopyMemoryToImage` or `vkCopyImageToImage` samples as the ASTC error colour (magenta). GPU copies (`vkCmdCopyBufferToImage`) and sRGB ASTC are correct. Checked against Mesa main on 2026-11-19 (`anv_image_host_copy.c` unchanged).

## Hardware and software

- Intel HD 620 (Kaby Lake GT2, `8086:5916`), i915, Linux 7.0
- Mesa 26.0.8 (Ubuntu) and Mesa main `0e9e1086bb86`

## Causes (`src/intel/vulkan/anv_image_host_copy.c`)

1. On Gfx9 non-LP, `flush_astc_ldr_void_extent_denorms` keeps the emulation plane in the image's own ASTC format, with only void-extent denorms flushed. The host-copy code always decodes to RGBA8 and writes those bytes into that ASTC plane.
2. The decode path, which runs on GPUs with full ASTC emulation, passes `copy_intermediate()` an extent in ASTC blocks, but `_mesa_unpack_astc_2d_ldr()` expects texels. Its tile stepping also mixes block and RGBA8 strides.
3. `vkCopyImageToImage` never fills the emulation plane. The API only addresses plane 0, so `dst_plane == n_planes` is never true. The unreachable branch also passes a `VkFormat *` where a `util_format_description *` is expected.

Introduced by "anv: Implement host image copy for emulated astc images" (MR 37691).

## Reproducer

[`astc-host-copy/`](https://github.com/CodeGrogu/mesa/tree/hd620-audit/HD620-full-audit/astc-host-copy) uploads void-extent blocks with exactly representable colours through cmd-copy, host-copy and host image-to-image, then samples every texel.

| Driver | cmd-copy | host-copy / host-i2i (UNORM) | sRGB |
| --- | --- | --- | --- |
| Mesa 26.0.8 | exact | ~99% of channels wrong | exact |
| main `0e9e108` | exact | same | exact |
| `hd620-round2` | exact | **exact** | exact |

With random blocks, every format and path on `hd620-round2` matches native hardware decode; the worst difference is 0.0038, which is RGBA8 rounding.

## Fix

Commit "anv: fix host image copies into emulated ASTC images; emulate ASTC 5x5 on Gfx9" on branch `hd620-round2` adds `write_emu_plane()`. It flushes or decodes strips of block rows and writes each strip in the plane's own units, and image-to-image now fills the plane after copying the raw blocks.
