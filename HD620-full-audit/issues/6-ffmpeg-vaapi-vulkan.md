## Summary (FFmpeg bug, not Mesa)

`hwmap=derive_device=vulkan` from VA-API frames fails with `EINVAL` (`Failed to map frame: -22`), so VA-API → Vulkan zero-copy doesn't work. `av_hwframe_map()` tries the source's `map_from` first and only falls through on `ENOSYS`. `vaapi_map_from()` handles only `DRM_PRIME` and sends every other destination, including `AV_PIX_FMT_VULKAN`, to `vaapi_map_to_memory()`, which returns `EINVAL` under `AV_HWFRAME_MAP_DIRECT`. So `vulkan_map_from_vaapi()` is never reached. FFmpeg 8.0.1 and master are both affected.

## Evidence ([`zero-copy/`](https://github.com/CodeGrogu/mesa/tree/hd620-audit/HD620-full-audit/zero-copy))

`va-vk-map.c` maps a seeded NV12/P010 VA surface into Vulkan and compares every byte. On iHD 26.1.2 with ANV:

- One-step map (what `hwmap` does): `EINVAL`.
- Two-step map through `DRM_PRIME`: 0 bytes differ, so the drivers are fine.

With [the patch](https://github.com/CodeGrogu/mesa/tree/hd620-audit/HD620-full-audit/zero-copy/ffmpeg-vaapi-map-from-enosys.patch), which makes `vaapi_map_from()` return `ENOSYS` for other hardware destination formats, the one-step map works byte-exactly and the `ffmpeg` CLI works too. 1080p H.264 decode to Vulkan takes 1.49 s for 960 frames, versus 4.58 s with `hwdownload` (about 3×).

This belongs on FFmpeg's tracker; it's recorded here because the HD 620 audit found it.
