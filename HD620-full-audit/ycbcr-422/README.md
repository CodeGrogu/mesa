# Item 27: packed 4:2:2 YCbCr in ANV on Gfx9 (2026-11-22)

Commit `anv: support 16-bit packed 4:2:2 and linear chroma for packed 4:2:2` on `hd620-round2`.

## What changed

- **`G16B16G16R16_422_UNORM` and `B16G16R16G16_422_UNORM`**, which were unsupported. Gfx9 has no 16-bit
  packed 4:2:2 sampler format. The bytes of a pixel pair (G0 B G1 R) are exactly two `R16G16_UNORM`
  pixels, (G0, B) and (G1, R), so the image is stored as `R16G16_UNORM` at full width. Hardware filtering
  of the luma channel is then correct. The new `anv_nir_lower_packed_422()` runs after the shared YCbCr
  lowering and rebuilds chroma from texel fetches on the half-width chroma grid. It supports midpoint and
  cosited-even chroma and nearest or linear chroma filters (implicit reconstruction). The runtime's
  VkFormat-to-pipe-format table gains both formats (mapped to the 2×1, 16-bit `R16G16_R16B16_422_UNORM`)
  for block size and channel widths.
- **Linear chroma filtering for `G8B8G8R8_422_UNORM` / `B8G8R8G8_422_UNORM`**, which was disabled
  because the sampler's own YCRCB filtering is slightly off. Enabling the feature with hardware filtering
  failed 20 CTS cases ([yuv8-hwlinear.status](yuv8-hwlinear.status)). With a linear chroma filter, the
  same pass now keeps the hardware luma and rebuilds chroma from point samples of each pixel pair. Nearest
  chroma still uses the hardware path unchanged.

## CTS (VK-GL-CTS `5bb4c7e`, debug ANV build)

| List | Before | After |
| --- | --- | --- |
| [`y422.txt`](y422.txt): every `dEQP-VK.ycbcr.*` case for the two 16-bit formats (1,004) | 0 pass, all not supported | **776 pass, 0 fail**, 228 not supported ([status](y422-new.status)) |
| [`yuv8.txt`](yuv8.txt): every `dEQP-VK.ycbcr.*` case for the two 8-bit packed formats (1,004) | 424 pass, 0 fail ([status](yuv8-before.status)) | **444 pass, 0 fail** ([status](yuv8-new.status)) |

The 16-bit cases that remain not supported need disjoint planes (112; these are single-plane formats),
explicit chroma reconstruction (96), storage (12) or mesh shaders (8). Advertising explicit
reconstruction was tried and is wrong for this implementation, which samples chroma at the filtered
position: 352 cases then failed, so it is not advertised.
