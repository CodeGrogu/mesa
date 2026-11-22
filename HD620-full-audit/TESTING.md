# Tests 5, 8 and 15 (2026-11-19)

All tests ran on the HD 620 (KBL GT2), i915 on Linux 7.0.

## 5. Copy engine (BCS) fixture: first hardware run

Correctness gates (`evidence/bcs-copy/verify-20261119/`): 42 blitter cases and 21 render-queue cases pass. They cover both packets, 4K–24M+12K, latency, queued and batch modes. Every case had zero bad words and correct fdinfo engine accounting. The timing run (`evidence/bcs-copy/20261119T213345Z/summary.md`) used `run-serial.sh` with the round-2 ANV and reported 0 failures.

| Route, 64 MiB copy | GPU throughput | 4 KiB latency (host p50) |
| --- | --- | --- |
| BCS `XY_FAST_COPY_BLT` | **10.5 GB/s** | 13.6 µs |
| BCS `XY_SRC_COPY_BLT` | 5.6 GB/s | 12.4 µs |
| Render, ANV `vkCmdCopyBuffer` | 6.3 GB/s | 92.2 µs |
| CPU `memcpy`, single thread | 11 GB/s | 0.1 µs |

`XY_FAST_COPY_BLT` on the blitter is about 1.65× faster than the render-engine copy ANV uses today, and it leaves the render engine free. Every BCS execbuf pays the Gen9 i915 command-parser cost, which is included above. ANV has no Gen9 blitter path, so this is now a new-feature TODO, not a switch to flip. Concurrency, cross-engine visibility and ownership tests are still needed before a queue could be exposed.

## 8. Vulkan CTS subsets

VK-GL-CTS main `5bb4c7e` (2026-11-13), `deqp-vk` built in the container and run on the host. The case list (`~/.local/state/catchingup/cts/round2.txt`, 8,956 cases from the must-pass list) covers the changed areas:

| Area | Cases |
| --- | --- |
| Host image copy: all ASTC cases plus a fixed random 3,000 others | 4,704 |
| ASTC textures | 672 |
| FP16 compute, subgroup size control, f16 subgroup arithmetic | 561 |
| Depth/stencil clears and depth/stencil image copies (HiZ) | 945 |
| DRM format modifiers | 1,572 |
| Memory allocation, basic compute, GLSL arrays (scratch) | 446 |
| Video capabilities | 56 |

| Driver | Passed | Failed | Not supported | Time |
| --- | --- | --- | --- | --- |
| Unpatched checkout `0e9e108` | 4,766 | 0 | 4,190 | 134 s |
| Round-2 release build | **4,772** | **0** | 4,184 | 75 s |

No regressions. The only differences were 6 tests that moved from not supported to Pass: `image.host_image_copy.simple.astc_5x5_srgb_block.linear.*`. Linear ASTC 5x5 images gained host-transfer support through the new Gfx9 5x5 emulation, and they pass. Per-test results are in `control.status` and `round2.status`. Video tests are mostly not supported because Vulkan Video is behind debug flags by default.

## 8b. Vulkan Video CTS (2026-11-22)

### Setup

After rebooting with `i915.enable_guc=2`, i915 logged `HuC firmware i915/kbl_huc_4.0.0.bin version 4.0.0` and `HuC: authenticated for all workloads`. GuC submission stays disabled.

CTS video sample clips were fetched with `fetch_video_decode_samples.py` (45 files) and `fetch_video_encode_samples.py` (6 YUV files), 148 MB in total, and copied into the host CTS data dir.

### Full run

Round-2 driver, `ANV_DEBUG=video-decode,video-encode`, 400 cases (`video.decode` h264/h265/vp9, `video.encode.h264`, `video.capabilities`).

| Result | Cases |
| --- | --- |
| Passed | **146** |
| Failed | **0** |
| Not supported | 254 |
| GPU hangs | 0 |

| Group | Passed / Not supported |
| --- | --- |
| capabilities.extended_flags | 13 / 15 |
| capabilities.none | 13 / 15 |
| decode.h264 | 30 / 30 |
| decode.h265 | 28 / 28 |
| decode.vp9 | 40 / 40 |
| encode.h264 | 22 / 126 |

Not-supported reasons:

| Missing extension or limit | Cases |
| --- | --- |
| VK_KHR_unified_image_layouts | 124 |
| VK_KHR_video_encode_intra_refresh | 46 |
| VK_KHR_video_encode_feedback2 | 30 |
| VK_KHR_video_encode_quantization_map | 24 |
| VK_KHR_video_encode_av1 | 10 |
| VK_KHR_video_encode_h265 | 10 |
| VK_KHR_video_decode_av1 | 6 |
| "Implementation does not support constant bitrate control" | 4 |

An earlier attempt stopped after 68 of 400 tests. It ran as a background job of a helper agent and was cut off when that agent ended. The logs show no crash and no GPU hang. The test it stopped on passes when run alone. The complete rerun is the result above.

### Decode without ANV_DEBUG

After the change "anv: expose Vulkan Video decode by default on Gfx9" (commit `04d82daeed8` on `hd620-round2`), the 196 decode cases were rerun with no `ANV_DEBUG`: 98 passed, 0 failed, 98 not supported (`VK_KHR_unified_image_layouts` variants).

| Check | Result |
| --- | --- |
| `vulkaninfo`, no debug flag | Lists `VK_KHR_video_decode_h264`, `_h265`, `_vp9` and `_queue` |
| `vulkaninfo`, `anv_gfx9_video_decode=false` | Lists none |
| HEVC frame checks (`hevc-huc/check.sh`) | Match software 60/60, 60/60, 36/36, 36/36 |

## 15. VP9 encode on Kaby Lake

| Route | Result |
| --- | --- |
| VA-API (iHD 26.1.2) | `vainfo` lists VP9 profiles 0 and 2 with `VLD` (decode) only. `vp9_vaapi`: "No usable encoding entrypoint found for VAProfileVP9Profile0". |
| Legacy i965 (inventory taken before removal) | Decode only. Removing it lost no capability. |
| QSV | Cannot create an MFX session (`-9`): no Media SDK/oneVPL runtime for Gen9 is installed, and it would use iHD's capabilities anyway. |
| Vulkan Video | The registry defines no VP9 encode operation. |
| Software, libvpx-vp9 (`-deadline realtime -cpu-used 8`) | Works: 720p at 21 fps (0.7× real time). |

Intel marketed VP9 8-bit encode for Kaby Lake, but no shipping driver here exposes it. Enabling it would mean patching Intel's iHD (media-driver), which is outside the Mesa tree. That is recorded as a "might" item. Evidence: `evidence/vp9-encode/`.
