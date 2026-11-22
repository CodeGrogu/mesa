# Item 48: software AV1 decode into GPU presentation (2026-11-22)

The HD 620 has no AV1 decode hardware, and no software change can add it. A
compute-shader AV1 decoder would also lose to the CPU here: AV1 entropy
decoding is serial, and the GPU has only 24 EUs. So the route is the one the
audit names: decode on the CPU with dav1d, present on the GPU.

## Setup

[`mpv.conf`](mpv.conf), installed as `~/.config/mpv/mpv.conf`:

```
hwdec=auto-safe
vo=gpu-next
```

mpv's default is `hwdec=no`, i.e. software decode for every codec.
`auto-safe` uses the GPU decoder whenever one exists and falls back to dav1d
for AV1. The stock Ubuntu packages are enough: mpv 0.41.0 and libdav1d 1.5.3.

## Results

Test clips: 10 s of 1080p30 `testsrc2`. They were encoded on this laptop with
libsvtav1 (preset 11, CRF 35), libx264 (veryfast) and libvpx-vp9 (realtime).
Playback used a 640×360 window, and the counters were read on unload by
[`stats.lua`](stats.lua).

| Clip | Decode | Output | Frames | Display drops | Decoder drops |
| --- | --- | --- | --- | --- | --- |
| AV1 Main 8-bit | dav1d (CPU), `hwdec=no` | gpu-next, Vulkan on Wayland | 299 | 5 (window start) | 0 |
| H.264 | `hwdec=vulkan` (ANV Vulkan Video, item 14) | gpu-next, Vulkan on Wayland | 299 | 0 | 0 |
| VP9 | `hwdec=vulkan` | gpu-next, Vulkan on Wayland | 299 | 0 | 0 |

CPU headroom for AV1 (`ffmpeg -c:v libdav1d -f null`, decode only): 6.2× real
time with 1 thread, 9.0× with 2, and 8.0× with 4. Synthetic content
compresses well, so real films decode more slowly, but 1080p30 has a wide
margin.

## Not done

Browsers already ship their own dav1d paths. 4K AV1 and 10-bit AV1 were not
measured.

## AV1 on the GPU: what can be offloaded (2026-11-22)

- **Full hardware decode: impossible.** Neither driver offers AV1 decode on
  KBL: VA-API (iHD 26.1.2) lists HEVC and VP9 VLD only, and Vulkan exposes
  `VK_KHR_video_decode_h264/h265/vp9`. Forcing AV1 commands onto the video
  engine was not tried, because a GPU hang on this machine freezes the
  desktop (see the 22:30 freeze in the memory notes).
- **Film grain synthesis on the GPU: possible.** It is a real AV1 decode
  stage: mpv `--vd-lavc-film-grain=gpu` makes dav1d export the grain
  parameters and libplacebo applies them in shaders. Test clip: 1080p30, 10 s,
  SVT-AV1 preset 11, CRF 32, `film-grain=20`, made noisy with `noise=alls=12`.

| Run (300 frames) | Wall | CPU (user) | Drops |
| --- | --- | --- | --- |
| ffmpeg dav1d decode only, grain applied, 2 threads | 3.43× real time | | |
| ffmpeg dav1d decode only, grain skipped (`-export_side_data film_grain`) | 3.95× real time | | |
| mpv `--untimed`, grain on CPU | 4.48 s (67 fps) | 7.93 s | 0 |
| mpv `--untimed`, grain on GPU | 4.72 s (64 fps) | 7.44 s | 0 |

Moving grain to the GPU saves about 6% CPU time but is about 5% slower end to
end. The 24-EU GPU, which also scales and presents, becomes the limit. A
full compute-shader AV1 decoder would do worse still, since entropy decoding
is serial. On this laptop the CPU (dav1d) is the faster AV1 decoder. Grainy
1080p30 still plays at 2.2× real time.
