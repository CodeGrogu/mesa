# Intel HD Graphics 620 Vulkan Video benchmark

The unmodified Mesa ANV build and the local eight-fix build ran on the same Kaby Lake HD 620 (PCI ID `8086:5916`), Linux `7.0.0-43-generic`, and FFmpeg `8.0.1-3ubuntu2`. Both libraries came from Mesa revision `0e9e1086bb86` with the same build options. The original is the saved pre-patch upstream snapshot, separate from the distribution driver. The timed tests used the full-codec builds. Capability probes covered both full and `all_free` builds inside the same Docker container. The extension counts are for that environment.

## Throughput and output

| Vulkan Video workload | Original median fps | Patched median fps | Median paired change | Observed paired range |
| --- | ---: | ---: | ---: | ---: |
| H.264 decode · 1080p | 495.1 | 495.1 | -0.14% | -1.85% to +7.08% |
| VP9 Profile 0 decode · 720p | 547.3 | 548.7 | +0.08% | -5.76% to +13.99% |
| VP9 Profile 2 decode · 720p | 487.5 | 487.8 | +0.20% | -3.17% to +0.73% |
| H.264 encode · 1080p | 256.3 | 258.2 | +0.03% | -0.99% to +3.86% |
| H.264 transcode · 1080p | 215.9 | 216.3 | +0.34% | -0.52% to +2.52% |

Every paired median change was below 0.4% in magnitude. Every workload had runs in both directions, so the timings do not show a clear throughput improvement or regression. All five 48-frame output samples matched byte for byte, including the H.264 elementary streams and raw decoded frames.

Each driver ran eight timed repetitions per workload after warm-up, with the first driver alternating within the pair. Throughput is completed frames divided by end-to-end wall time, including FFmpeg process startup. Decode tests used 960 frames and downloaded the output. Encode and transcode used 480 frames, H.264 constant QP 24, and a null muxer. Encode includes generated input and upload. Transcode transferred Vulkan frames directly between the decoder and encoder. The measured inputs were preassembled by stream copy, so the timed decoder did not reopen the short source clip between segments. An exploratory stream-loop configuration failed while negotiating the output format. It contributed no measured samples. These results measure FFmpeg pipelines on one host running its normal background processes. The paired range shows observed variation, and provides no confidence interval.

## Support and reporting

| Full build or condition | Unmodified driver | Eight-fix driver |
| --- | --- | --- |
| H.264 decode, H.264 encode and transcode | Verified in hardware | Verified in hardware |
| VP9 Profile 0 (8-bit) and Profile 2 (10-bit) decode | Verified in hardware | Verified in hardware |
| HEVC decode and encode on this configuration | No codec extensions. The queue mask falsely reports both operations | No codec extensions or queue mask bits |
| AV1 decode and encode | Unavailable on the tested HD 620 ANV builds | Unavailable |
| Video flags disabled | No Vulkan Video extensions or queue | Same |
| Video queue hidden with `anv_queue_override=v=0` | Video extensions and NV12 video flags remain despite zero video queues | Video extensions and flags removed |
| `all_free`, video flags enabled | VP9 decode. False encode queue and NV12 encode flags. Queue mask also lists H.264 and HEVC codes without codec extensions | VP9 decode only. No H.264 decode or encode, no encode queue or flags |
| Non-video Vulkan device extensions | 199 | Same 199 |

For the full build with both debug flags, the original and patched video extension lists match. The video queue codec mask changes from `0x3000b` to `0x10009`, which represents H.264 decode, VP9 decode, and H.264 encode. The encode-only original mask also retained caller-supplied bits. This comparison covers the tested ANV configurations. It does not measure OpenGL, 3D rendering, power consumption, VA-API, other GPUs, or Vulkan certification. No driver installation, commit, or upstream submission was made.

## Evidence

- [All 80 timed runs](/home/polarbear/.t3/projects/catchingup/HD620-benchmark/timings.csv)
- [Measured medians and paired spread](/home/polarbear/.t3/projects/catchingup/HD620-benchmark/summary.json)
- [Output SHA-256 comparisons](/home/polarbear/.t3/projects/catchingup/HD620-benchmark/output-check.log)
- [Capability matrix](/home/polarbear/.t3/projects/catchingup/HD620-benchmark/capabilities.log)
- [Complete extension, queue, and format captures](/home/polarbear/.t3/projects/catchingup/HD620-benchmark/capabilities.json)
- [Measured libraries and SHA-256 hashes](/home/polarbear/.t3/projects/catchingup/HD620-benchmark/provenance.json)
- [Benchmark runner](/home/polarbear/.t3/projects/catchingup/HD620-benchmark/benchmark.py)
- [Input SHA-256 hashes](/home/polarbear/.t3/projects/catchingup/HD620-benchmark/input-sha256.txt)
