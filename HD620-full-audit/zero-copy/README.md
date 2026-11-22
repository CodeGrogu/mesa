# VA-API → Vulkan zero-copy (TODO item 4)

## Root cause: FFmpeg, not the drivers

`hwmap=derive_device=vulkan` calls `av_hwframe_map(vulkan ← vaapi)`. `av_hwframe_map()` (libavutil/hwcontext.c) tries the source context's `map_from` first, and only moves on to the destination's `map_to` if that returns `ENOSYS`. `vaapi_map_from()` handles `DRM_PRIME` and sends every other destination to `vaapi_map_to_memory()`, including hardware formats such as `AV_PIX_FMT_VULKAN`. With `AV_HWFRAME_MAP_DIRECT`, `vaapi_map_frame()` then returns `EINVAL` because the destination format differs from `sw_format`. Since that is not `ENOSYS`, the working path is never tried: `vulkan_map_to()` → `vulkan_map_from_vaapi()`, which goes VA → DRM PRIME → `VK_EXT_image_drm_format_modifier`. FFmpeg 8.0.1 and master (2026-11-19) have the same code.

The earlier "via DRM" control failed for a different reason. The CLI cannot derive a DRM device from VA-API (`ENOSYS`).

## Evidence

`va-vk-map.c` uploads a seeded pattern to a VA surface, maps it to Vulkan, downloads it and compares every byte. It uses the same frames-context derivation as `hwmap`.

| libavutil | Route | NV12 | P010 |
| --- | --- | --- | --- |
| Ubuntu 8.0.1 | A: `av_hwframe_map(vulkan ← vaapi)`, as `hwmap` does | `EINVAL` | `EINVAL` |
| Ubuntu 8.0.1 | B: vaapi → drm_prime, then drm_prime → vulkan | mapped, 0 bytes differ | mapped, 0 bytes differ |
| patched 8.0.1 (`f60aea17…`) | A | **mapped, 0 bytes differ** | **mapped, 0 bytes differ** |

iHD export and ANV import were correct throughout (`baseline.txt`, `patched.txt`). With the patched library, `ffmpeg … -vf hwmap=derive_device=vulkan:mode=read+direct` decodes all frames of the NV12 and P010 samples (`cli-*-patched.log`).

## Throughput

Test: 1080p H.264, 960 frames, decode only, discard (`throughput.txt`). Five alternating rounds, round-2 ANV, iHD 26.1.2.

| Path | rtime (s) per round | Median |
| --- | --- | --- |
| VA → Vulkan zero-copy (patched libavutil) | 1.498, 1.474, 2.937, 1.494, 1.483 | **1.49 s, ≈645 fps** |
| VA → `hwdownload` to NV12 in system memory | 4.584, 4.643, 5.154, 4.123, 4.233 | 4.58 s, ≈210 fps |

Frames arrive in Vulkan about 3× faster than with a CPU download, and the CPU no longer touches the frame data.

## Fix

The fix is [`ffmpeg-vaapi-map-from-enosys.patch`](ffmpeg-vaapi-map-from-enosys.patch). `vaapi_map_from()` returns `ENOSYS` for hardware destination formats other than DRM PRIME, so `av_hwframe_map()` falls through to the destination's mapper. Mappings to memory are unchanged.

The tested library is in `~/.local/state/catchingup/round2/ffmpeg-vaapi-fix/`. It has the same version and symbol versions as Ubuntu's `libavutil.so.60`, and was built with only the vaapi, vulkan and libdrm hwcontexts. Use it per command with `LD_LIBRARY_PATH`. It is not installed system-wide, because it replaces a library that every FFmpeg user on the system loads.

## Related CLI behaviour (not fixed)

With `-stream_loop`, the decoder re-initialises and the CLI rebuilds the filter graph ("hwaccel changed"). With `-noauto_conversion_filters`, the rebuilt graph fails with `EINVAL` because no format conversion is allowed. This is CLI filter-graph behaviour, separate from the mapping bug. Single-pass decoding is unaffected.
