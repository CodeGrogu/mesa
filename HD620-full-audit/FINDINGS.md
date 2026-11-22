# Intel HD 620 findings

Kaby Lake GT2, PCI `8086:5916`, 24 EUs. Hardware tests used i915 on Linux `7.0.0-43-generic`. The installed drivers were Mesa 26.0.8 and Intel iHD 26.1.2. The unmodified and eight-fix ANV builds share Mesa revision `0e9e1086bb86`. The independent reviewer approved the audit within its documented test scope. [Audit and methods](REPORT.md) · [Driver hashes](../HD620-benchmark/provenance.json) · [Snapshot inventories](../HD620-driver-audit.md#capability-appendix)

**Good** means working in the stated exercise. **Bad** marks a reproduced defect, measured cost, or confirmed local limitation. **Fixed locally** marks a correction in the tested eight-patch build. **Conditional** means the result depends on a build, flag, firmware, API or test setup. **Open** marks something that still needs evidence.

## Graphs

| Question | Graph |
| --- | --- |
| Did the eight ANV patches change video throughput? What does each API support? | [Paired benchmark ranges and codec coverage](performance-and-coverage.html) |
| Which engine handled video processing? Where did Vulkan submission time go? Are Iris timers trustworthy? Does RC6 respond to load? | [Timing, submissions, engine use and RC6](measurement-charts.html) |
| How much did the multi-mip HiZ limitation cost in the clear probe? | [Depth clear timings](depth-clear-timing.html) |
| How much throughput was lost to frame download? | [GPU-resident versus downloaded frames](../HD620-benchmark/decode-profile/frame-download.html) |

See [Round 2 (2026-11-19)](ROUND2.md) for the compiler, ASTC and OpenCL results from the second sweep.

See [Hardware paths missing from an API or disabled on the HD 620](HARDWARE-PATHS.md) for the newer exposure inventory and implementation decisions.

## Graphics, compute and measured performance

| Indication | Finding |
| --- | --- |
| **Good** | Iris identified the HD 620 renderer and exposed OpenGL 4.6, GLES 3.2 and EGL 1.5. OpenGL and GLES glmark2 each had 27 passing validations and six cases with unknown validation status. GBM, Wayland, X11 and surfaceless EGL enumerated hardware. Installed ANV presented through Wayland and XCB. [Coverage and logs](REPORT.md#coverage-by-subsystem) |
| **Good** | Installed, unmodified and patched ANV ran headless vertex, Phong and blur workloads. A SPIR-V test and the buffer-fill tests verified all 1,048,576 output words on all three builds. [Graphics timings](evidence/graphics/timings.csv) · [Compute probe results](evidence/compute-patched.txt) |
| **Bad — profiling bug** | Fresh Iris reported 250 ns for a compute dispatch that took 11.48 ms to complete, and median `GL_TIME_ELAPSED` was zero. Checked results passed. Explicit completion at both timing boundaries brought the timer to 11.55 ms. Installed Iris reproduced the issue. Zink on the same GPU reported about 10.8 ms for the equivalent GL workload. Rusticl reported 250 ns while i915 accounted for about 11.1 ms of render-engine activity. Cross-batch ordering is the leading explanation. A submission trace or verified fix is still needed to establish the cause. [Independent Iris rerun](evidence/independent-review/iris-current.log) · [Zink control](evidence/gl-compute-zink.txt) · [Rusticl checks](evidence/opencl-smoke.txt) |
| **Bad — depth compression gap** | ANV disables HiZ for Gen9 depth images with multiple allocated mips. The same mip-0 clear group on patched ANV took 2.40 ms with one mip and 22.39 ms with four. The independent repeat was 2.44 ms and 27.66 ms. With HiZ disabled, the one-mip case also took over 20 ms. The probe checked the last clear at the center of mip 0. Draws, other mips and game frame times remain open. [Clear results and scope](REPORT.md#why-the-timer-and-depth-findings-are-different) |
| **Good when batched — costly when serialized** | The same 64 Vulkan buffer fills took 5.12 ms when submitted and waited on individually and 1.05 ms in one submission with one wait on patched ANV. Every driver showed the same direction of effect. The measurement includes command recording, barriers, submissions and waits, so it does not isolate one slow code path. [Submission samples](evidence/compute-patched.txt) |
| **Good — application rendering path** | The installed Iris geometry scene ran at 7,677.5 median fps with vertex buffers and 4,462.5 without. All four pairs favored the VBO path. Those figures apply to the short glmark2 scene. [Graphics timings](evidence/graphics/timings.csv) |
| **Neutral — local patches were correctness work** | The eight local changes yielded no consistent speedup in four light Vulkan graphics scenes or five video workloads. Across 80 paired video benchmark runs, each workload's median paired change was below 0.4% in magnitude, and every observed range crossed zero. All five checked output samples matched byte for byte. [Video benchmark](../HD620-benchmark/REPORT.md) · [Graphics results](REPORT.md#graphics-measurements) |
| **Conditional — usable OpenCL** | The host loader found zero platforms. A staged Rusticl OpenCL 3.0 ICD ran hardware kernels and checked the results without system installation. Level Zero execution was not tested. The separately documented Intel legacy compute runtime is another untested path on this host. [Default loader](evidence/opencl-installed.txt) · [Staged OpenCL run](evidence/opencl-smoke.txt) |
| **Conditional — telemetry** | Rusticl displays a hard-coded 400 MHz clock while i915 reports a 300–1,000 MHz configured range and the graphics run sampled 1,000 MHz. The constant is weak telemetry. OpenCL allows implementation-defined frequency reporting on variable-clock devices. [Rusticl snapshot](evidence/rusticl-container.txt) · [Clock inventory](evidence/host.txt) |

## Media, codec support and sharing

| Indication | Finding |
| --- | --- |
| **Good — VA-API codecs** | iHD hardware encode and decode worked for H.264, HEVC Main, HEVC Main10 and VP8. Hardware decode checksums matched software for the 60-frame samples. MPEG-2 and JPEG worked with sample differences of at most three and one 8-bit levels, respectively. iHD advertised VC-1 and VP9 profiles 0 and 2 decode. Those VA-API paths were enumerated rather than workload-tested here. [VA-API workloads](evidence/media-smoke.txt) · [Pixel deltas](evidence/media-pixel-delta.txt) · [Advertised profiles](evidence/va-iHD.txt) |
| **Good — video processing** | iHD scaled NV12 and P010. In i915 accounting for 960 H.264 frames, decode used the video engine, scaling also used render, and denoising used video enhancement. The copy engine recorded zero for these particular pipelines. [Processing results](evidence/media-interop.txt) · [Engine accounting](evidence/media-engine-accounting.jsonl) |
| **Conditional — Vulkan Video** | Debug opt-in enabled H.264 decode, encode and transcode and VP9 8-bit and 10-bit decode in the full build. The `all_free` candidate exposed VP9 decode only. The tested VA-API and Vulkan configurations provide different codec coverage. [Vulkan Video capability and workload evidence](../HD620-benchmark/REPORT.md#support-and-reporting) |
| **Bad — HEVC Vulkan gap in the checkout** | The installed Mesa Vulkan ICD decoded Main and Main10 with opt-in flags. All 60 frames of each test stream matched software checksums. Both checkout builds hide HEVC decode under the same configuration and their decoder requires HuC, which i915 reported disabled. Vulkan HEVC encode in the checkout requires Gen12 or newer. VA-API proves Gen9 HEVC encode works through iHD. [Installed Vulkan parity](evidence/hardware-paths/hevc-parity.txt) · [ANV codec gates](../mesa/src/intel/vulkan/anv_physical_device.c#L371) · [What to restore](HARDWARE-PATHS.md#implement-or-restore) |
| **Good and bad — sharing** | GEM PRIME export/import worked across DRM files. Direct VA-API export to EGL import worked for allocated NV12 and P010 Y-tiled surfaces. In FFmpeg, direct VA-API → Vulkan mapping returned `EINVAL` on both tested formats. The download-and-upload fallback processed all 60 frames per clip. The failing layer and compositor presentation remain open. [EGL import](evidence/va-egl-import.txt) · [FFmpeg mapping controls](evidence/media-interop-controls.txt) |
| **Good when GPU-resident — copy cost measured** | Skipping the CPU download improved median paired throughput by 30% on H.264 1080p and 38% on VP9 10-bit 720p. Each test used the same patched driver on both paths and discarded output after wrapping. Separate H.264 transcodes verified direct Vulkan frame flow from decode into encode. [Download benchmark](../HD620-benchmark/decode-profile/summary.json) · [Transcode evidence](../HD620-implementation-plan.md#vulkan-video-workload-evidence) |
| **Outdated** | The legacy VA-API i965 driver still initializes and reports a different profile set. Intel archived i965 and points users to iHD for continued updates. [i965 profile list](evidence/va-i965.txt) · [Archived driver](https://github.com/intel/intel-vaapi-driver) |

## i915, display, power and unused capabilities

| Indication | Finding |
| --- | --- |
| **Good** | i915 reported one slice, three subslices, 24 EUs and separate render, copy, video and video-enhancement engines. GEM creation, import, softpin reporting, context isolation reporting and timeline fences were exercised or queried as indicated in the logs. A minimal copy-engine batch completed. [Hardware query](evidence/i915-uapi.txt) · [Kernel smoke test](evidence/i915-smoke.txt) |
| **Bad — dedicated copy queue missing** | ANV exposes graphics, compute and transfer on the same family, with no dedicated transfer family on this setup. i915 exposes the copy engine. A no-op submission verifies access. Copy correctness, concurrent rendering and transfer throughput have yet to be benchmarked on BCS. [ANV queue setup](../mesa/src/intel/vulkan/anv_physical_device.c) · [Kernel smoke test](evidence/i915-smoke.txt) |
| **Good — display and idle state** | DRM inventoried three CRTCs and nine planes, an active 1366×768 eDP panel, and disconnected HDMI. DMC v1.4 loaded. RC6 covered about 89% of both sampled desktop windows and 0% during render load. RPS reported a configured 300–1,000 MHz range. [Display state](evidence/drm-info.json) · [Power sample](evidence/rc6-sample.txt) |
| **Conditional — firmware and panel policy** | The active eDP EDID matched the local override. The boot configuration disables PSR and battery initialization. Kaby Lake firmware paths exist, and the checked-out GuC submission path excludes Gen9. FBC activity and power savings remain unmeasured. [Display and firmware evidence](evidence/display-firmware.txt) · [Initial firmware analysis](../HD620-driver-audit.md#partially-implemented-or-conditional) |
| **Bad observation — cause open** | The boot journal recorded one missed Pipe A atomic-update deadline. The audit did not reproduce it. IGT probes stopped in debugfs permission checks, which leaves their assertions unverified. [Display journal](evidence/display-firmware.txt) · [IGT setup log](evidence/igt-basic.txt) |
| **Conditional — newer software** | The installed Vulkan ICD advertised **183** device extensions. Both checked-out builds advertised **199**, including descriptor heap, indirect memory copy, internally synchronized queues, and Maintenance 11/12. This is a reporting delta. The checkout's additions were not workload-tested individually. [All 16 new exposures](evidence/vulkan-extension-delta.txt) |
| **Conditional — shader and raster features** | Cooperative matrix support enumerated with `INTEL_LOWER_DPAS=1` through emulation. No kernel tested it. Float-atomic extensions were advertised, while FP32 atomic add was false for buffers, shared memory and images in the original inventory. Bresenham and smooth lines were exposed. Rectangular and some stippled modes were absent. Device-generated commands were not exposed on the installed Gen9 build. [Feature inventory](../HD620-driver-audit.md#partially-implemented-or-conditional) |
| **Unavailable or constrained** | The audited ANV configuration lacks native sparse residency, ray-tracing pipelines, mesh shading, fragment shading rate and protected memory. Gen9 lacks native systolic acceleration and the tested stack exposes no fixed-function AV1. Source-only fallback ideas need capability and workload evidence. [Architecture and backend inventory](../HD620-driver-audit.md#missing-software-work) |

## Eight ANV defects fixed locally

| Indication | Original behavior | Tested correction |
| --- | --- | --- |
| **Fixed locally** | Queue codec masks included unsupported operations and could retain caller output bits. | Derive codec operations from advertised extensions and overwrite the result. |
| **Fixed locally** | Queue flags, extensions and NV12 format features could describe an empty or hidden video queue. | Expose video directions only when a usable codec and effective engine count exist. |
| **Fixed locally** | Zero-capacity enumeration returned the wrong result. Encode quality and capability fields could stay uninitialized or report inconsistent limits. | Correct enumeration and initialize the applicable H.264, H.265 and AV1 query fields. Hardware encode validation here covered H.264. |
| **Fixed locally** | Format results left component swizzles unwritten. | Set identity for NV12 and P010 rows. |
| **Fixed locally** | Multi-profile queries duplicated rows or returned incompatible bit depths. | Emit the shared format and validate profiles before writing results. |
| **Fixed locally** | Generic image-format queries ignored unsupported supplied video profiles. | Return the matching profile error and clear core image properties. |
| **Fixed locally** | Reference-picture usage could succeed with a profile list missing the required decode or encode direction. | Reject impossible DPB requests and preserve legal mixed usage. |
| **Fixed locally** | Extended-usage color and video format paths incorrectly allowed depth/stencil combinations. | Exclude distinct depth/stencil view matches and reject depth/stencil attachment usage on the affected video rows. |

Independent patch verification included 809 new depth/stencil assertions, retained video/image/DPB checks, 14 capability cases, 18 hardware workloads and two byte-identical H.264 transcodes. The Mesa patches are local and uncommitted. [Implementation details and sign-off](../HD620-implementation-plan.md#anv-video-series-sign-off)

### What changed in the Vulkan Video probes

| Indication | Probe result |
| --- | --- |
| **Fixed locally — full build** | With both video flags enabled, the queue mask changed from `0x3000b` to `0x10009`. The remaining operations are H.264 decode, VP9 decode and H.264 encode. The extension set stayed the same in this case. An encode-only query now clears the caller sentinel and reports `0x10000`. Non-video families report a zero mask wherever the video property is queried with the extension available. |
| **Fixed locally — free build** | `all_free` plus encode-only originally exposed an encode queue, NV12 encode features and an unrelated codec mask. It now exposes no video capability in that mode. With decode enabled, the only codec exposed is VP9 decode. |
| **Fixed locally — hidden engine** | With `anv_queue_override=v=0`, both builds now suppress video extensions and NV12 video format flags as well as the video queue. Both baseline and patched builds reported no Vulkan Video extensions in the default no-debug runs. |
| **Investigated — no defect established for valid callers** | Some capability queries returned success for codecs whose extensions were unadvertised. The spec review determined that those calls used codec inputs unavailable to a valid caller. Other observed format cases, including the extended-usage P010 query, are detailed below. |

[Complete baseline and patched capability matrix](../HD620-benchmark/capabilities.log) · [Probe and review history](../HD620-implementation-plan.md)

## Follow-ups and limits

| Priority or status | Remaining work |
| --- | --- |
| **Fix correctness first** | Trace and repair Iris timer ordering. Test render-only, compute-only, mixed workloads, query buffers, and overhead around timer boundaries. |
| **Best measured driver speed target** | Develop Gen9 multi-mip HiZ support. Validate clears, loads, sampling and resolves across levels before testing games and frame-time percentiles. |
| **Measure before adding a path** | Compare real copy-engine transfers with render-engine and CPU copies, including contention and overlap. Trace the FFmpeg direct mapping failure and confirm end-to-end presentation. |
| **Feature work** | Pursue Gen9 Vulkan HEVC encode with iHD as a working reference. Test HuC separately before drawing conclusions about Vulkan HEVC decode. Broader default Vulkan Video support requires codec-specific conformance. |
| **Query limitation and open spec question** | P010 Y-tiled storage queries pass with a legal R16/R16G16 view list. The test did not create views or use the surface as a P010 decode destination. OPTIMAL NV12/P010 storage and color usage remains a specification interpretation question around extended usage. [P010 replay and decision](../HD620-implementation-plan.md#p010-drm-storage-replay) |
| **Source candidates** | Review the Gen9 dynamic VAO fast path, legacy two-sided color lowering, FP32 atomic-add emulation, cooperative matrix tuning and software AV1 presentation. The initial audit lists them as candidates and supplies no measured gains. [Candidate list](../HD620-driver-audit.md#optimization-and-reimplementation-candidates) |
| **Coverage still open** | Full CTS and cross-generation regression checks, real games and frame pacing, cache stutters, scheduler fairness and memory pressure, HDR/VRR and HDMI hotplug, FBC, suspend/resume, energy use, and DXVK, VKD3D-Proton, WebGPU and oneVPL workloads. [Coverage by subsystem](REPORT.md#coverage-by-subsystem) |
