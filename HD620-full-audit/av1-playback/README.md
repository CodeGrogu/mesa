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
