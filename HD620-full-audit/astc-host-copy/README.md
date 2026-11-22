# ANV ASTC host image copy and Gfx9 ASTC 5x5

`astc-host-copy.c` uploads the same ASTC data three ways and samples every texel with `sample.comp`:

- **cmd-copy:** `vkCmdCopyBufferToImage`, the known-good path, which runs ANV's GPU emulation pass.
- **host-copy:** `vkCopyMemoryToImageEXT`.
- **host-i2i:** `vkCopyMemoryToImageEXT` into a staging image, then `vkCopyImageToImageEXT`.

The default data is LDR void-extent blocks with colours that are multiples of 257. Every seventh block has components of 1–3, which the Gfx9 denorm flush zeroes. Each texel is checked exactly, within 1.5/255. With `ASTC_DUMP=dir`, random blocks are used instead and the sampled images are written out for comparison between drivers.

```bash
../tools/root/usr/bin/glslangValidator --target-env vulkan1.3 -V sample.comp -o sample.spv
docker exec -i hd620-mesa sh -c 'set -e; bin=$(mktemp); trap "rm -f $bin" EXIT; gcc -O2 -Wall -Wextra -I/src/include -x c -o "$bin" - -l:libvulkan.so.1 -lm; cat "$bin"' < astc-host-copy.c > astc-host-copy
VK_DRIVER_FILES=<icd.json> ./astc-host-copy sample.spv
```

## Results on HD 620

| Driver | cmd-copy | host-copy, host-i2i (UNORM) | sRGB | Log |
| --- | --- | --- | --- | --- |
| Installed Mesa 26.0.8 | exact | 99% of channels wrong (ASTC error colour) | exact | `installed.txt` |
| Checkout + eight patches | exact | same failure | exact | `patch8.txt` |
| Round-2 build `b614763e…`, `ANV_NATIVE_ASTC_5X5=true` | exact | **exact** | exact | `fixed-native.txt` |
| Round-2 build `b614763e…`, 5x5 emulated (default) | exact | **exact** | exact | `fixed-emu.txt` |

On random blocks, every format, path and mode of the round-2 build matched the installed driver's native hardware decode from `cmd-copy`. The worst difference was 0.0038, which is RGBA8 rounding of emulated 5x5.

## Causes

1. On Gfx9 non-LP, the emulation plane holds the image's own ASTC blocks with void-extent denorms flushed (`flush_astc_ldr_void_extent_denorms`). The host-copy code always decoded to RGBA8 and wrote those bytes into that ASTC plane.
2. The decode path passed an extent in blocks to `_mesa_unpack_astc_2d_ldr`, which takes texels. Its tile stepping also mixed block and RGBA8 strides. This path is used on GPUs that emulate ASTC entirely, and on Gfx9 5x5 after this change.
3. `vkCopyImageToImage` never filled the emulation plane, because the API only addresses plane 0. The unreachable branch also passed a `VkFormat *` as a `util_format_description *`.

The fix adds `write_emu_plane()`, which flushes or decodes strips of block rows and writes each strip in the plane's own units. Image-to-image now fills the plane after copying the raw blocks. The bug came in with upstream MR 37691 (2025-11-07), and today's main has the same code.

## ASTC 5x5 on Gfx9

Gfx9 hangs when ASTC 5x5 and CCS data are in the sampler cache together (mesa#1892). Iris emulates 5x5 on Gfx9 to avoid this (MR 3251). ANV exposed native 5x5. The patch decodes the two 5x5 formats to RGBA8 on Gfx9 and leaves the other block sizes native. The hang itself was not reproduced, because doing so would reset the GPU under the running desktop. The cost is RGBA8 memory for 5x5 textures and a compute decode on upload.

Patch: [`../patches/anv-astc-host-copy-and-gfx9-5x5.patch`](../patches/anv-astc-host-copy-and-gfx9-5x5.patch)
