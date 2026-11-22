# Vulkan HEVC decode on HD 620 with HuC (TODO items 6 and 11)

The boot parameter `i915.enable_guc=2` (`/etc/default/grub.d/99-i915-huc.cfg`) makes i915 load `kbl_huc_4.0.0.bin`: "HuC: authenticated for all workloads". GuC submission and SLPC stay disabled. With HuC loaded and `ANV_DEBUG=video-decode`, ANV exposes `VK_KHR_video_decode_h265` alongside H.264 and VP9 decode and H.264 encode. Before the reboot, HEVC decode was hidden because HuC was disabled.

`check.sh` decodes each clip through FFmpeg's Vulkan hwaccel on the round-2 ANV and compares every frame's MD5 against software decode (`result.txt`):

| Clip | Profile | Frames | Result |
| --- | --- | --- | --- |
| `hevc_vaapi-nv12.hevc` (iHD-encoded) | Main | 60 | 60/60 identical |
| `hevc_vaapi-p010le.hevc` (iHD-encoded) | Main10 | 60 | 60/60 identical |
| `main.hevc` (x265: 2 I, 9 P, 25 B, multiple GOPs) | Main | 36 | 36/36 identical |
| `main10.hevc` (x265, same structure) | Main10 | 36 | 36/36 identical |

The logs confirm the device (HD Graphics 620 KBL GT2), the driver (`~/.local/mesa-round2/.../libvulkan_intel.so`) and that `VK_KHR_video_decode_queue` was used.

No driver change was needed. ANV's existing HuC-based HEVC path works once HuC is loaded. Turning Vulkan Video on without the debug flag is item 14 and depends on the CTS video results.
