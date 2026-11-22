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

## 15. VP9 encode on Kaby Lake

| Route | Result |
| --- | --- |
| VA-API (iHD 26.1.2) | `vainfo` lists VP9 profiles 0 and 2 with `VLD` (decode) only. `vp9_vaapi`: "No usable encoding entrypoint found for VAProfileVP9Profile0". |
| Legacy i965 (inventory taken before removal) | Decode only. Removing it lost no capability. |
| QSV | Cannot create an MFX session (`-9`): no Media SDK/oneVPL runtime for Gen9 is installed, and it would use iHD's capabilities anyway. |
| Vulkan Video | The registry defines no VP9 encode operation. |
| Software, libvpx-vp9 (`-deadline realtime -cpu-used 8`) | Works: 720p at 21 fps (0.7× real time). |

Intel marketed VP9 8-bit encode for Kaby Lake, but no shipping driver here exposes it. Enabling it would mean patching Intel's iHD (media-driver), which is outside the Mesa tree. That is recorded as a "might" item. Evidence: `evidence/vp9-encode/`.
