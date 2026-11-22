# HD 620 driver implementation plan

## First change

Correct ANV's Vulkan Video queue capability reporting. The installed ANV build showed two failures on the HD 620 under process-local video debug flags.

- The queue advertises H.265 encode while device extension enumeration omits `VK_KHR_video_encode_h265`.
- A caller-supplied sentinel survives on graphics queues and in the encode-only codec mask.

Initialize each returned codec mask and gate its seven possible operation bits by both queue direction and the corresponding supported device extension. This reuses the existing generation, build, and firmware checks. The patch scope is the queue property query. See [ANV device extension policy](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:495), [queue property code](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:3482), and [Vulkan queue property documentation](https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyVideoPropertiesKHR.html).

## Verification

1. Select the built Intel ICD using the absolute path to the build-tree `intel_devenv_icd.<cpu>.json`. Keep driver installation outside this task.
2. Run fresh processes with video flags unset or empty, decode-only, encode-only, and both. Verify unchanged extension enumeration across baseline and patch in each matching build configuration.
3. Chain a video-properties structure prefilled with a sentinel onto each queue query. Verify a zero mask on each non-video queue whenever the video queue extension is exposed. On video queues, compare the mask to the matching codec extension list and the queue direction. Verify the Gen9 queue lacks H.265 encode and the encode-only result clears the sentinel.
4. Use `-Dvideo-codecs=all` for the primary comparison so H.264 and H.265 are compiled in. Use the default `all_free` configuration for negative coverage when the toolchain is available. Check build and debug flag provenance before drawing conclusions from a run.
5. Run `git diff --check`. Record exactly which builds and hardware checks ran.

The installed-driver baseline is reproducible with the temporary [queue probe](/tmp/hd620-probe.yWsooZ/queue-mask-probe.py). Mesa's [build-tree ICD target](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/meson.build:73) writes an absolute library path to the generated manifest. The host lacks the standard Mesa compiler/build tools on the current PATH, so provision and verify an isolated build environment before treating runtime testing as complete.

## Follow-up investigations

- Audit queue creation separately. The source uses raw debug flags to set queue direction, but the encode extension also imposes a Gen12.5 upper bound. Check newer devices and builds with every codec for one direction disabled.
- Review video profile and format validation separately. An exploratory query using unadvertised AV1 decode and HEVC encode profiles returned success on Gen9. These profiles are unavailable to applications under the current extension contract, so investigate this as defensive consistency work.
- Prototype Gen9 HiZ for multi-mip depth images with BLORP validation, then measure bandwidth and frame time on a workload that uses depth mip chains.
- Measure which Vulkan Video codecs pass workload tests on Kaby Lake, then assess default enablement and the separate Gen9 HEVC encode implementation.
- Verify Intel's legacy OpenCL and Level Zero runtime in an isolated environment before deciding whether to deploy it on the host.

The adversarial reviewer approved the narrow change with the verification conditions above. Opus 5.5 medium independently reproduced both failures and confirmed readiness. Implementation was delegated after both gates were met. Verification uses an isolated build and selects its ICD for each probe process.

## First patch results

Opus implemented the seven codec checks and unconditional output assignment in [ANV's queue query](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:3482). Both isolated builds passed on the HD 620 using their build-tree ICDs. All six enabled-mode probe cases failed before the patch and passed after it across `all` and `all_free`. The unset and empty modes continued to expose no video queue extension. The extension lists matched between baseline and patch. The parent independently reran the patched matrix and checked the diff.

The `all_free` encode-only case still creates an encode queue with zero codec operations. The upstream development builds also did not expose HEVC decode on this machine, although the installed driver exposes it. The probe verified that both results agree with the extensions each build advertises. Full Vulkan Video conformance, codec workloads, and newer GPU generations remain untested. The local patch is uncommitted, and Docker container `hd620-mesa` and volume `hd620-build` retain the build environment.

## Queue availability baseline

The rebuilt `all_free` driver with `ANV_DEBUG=video-encode` exposes an encode queue and the base and encode queue extensions, while reporting a zero codec mask. The `all` build in the same mode exposes H.264 encode and reports a nonzero codec mask, providing a positive control.

With `anv_queue_override=v=0` and both video flags set, the `all` driver exposes the video queue and codec extensions while enumerating only a graphics, compute, and transfer queue. Use this read-only override in the next patch's regression matrix to cover the no-engine path on this device. The previous mask probe can establish this baseline but reports success because it only checks masks for families that exist. Extend it to check that advertised video directions have an enumerated queue and that each video queue direction has a codec.

The extended [queue and NV12 format probe](/tmp/hd620-availability.EkRYNn/queue-availability.py) passed the `all` positive control. It failed both targeted baselines. The `all_free` encode-only case reported NV12 encode features without a codec. Disabling the exposed video queue via `anv_queue_override=v=0` left both NV12 video directions and their extensions advertised. Use these cases to check the second patch with the build-tree ICD.

### Approved second patch

Derive candidate codec operations from the existing build, debug, generation, and HuC checks. Keep H.264 encode eligible on Gen9. HEVC encode requires Gen12 or newer within the existing encode ceiling, and AV1 encode requires Gen12.5. Derive video queue directions from the candidate operations and the effective engine count. Derive extension exposure from the finalized queues and use the direction extensions to set video format properties.

The adversarial reviewer confirmed the initialization order and format query call paths. Removing a video direction with zero codecs is ANV consistency policy. The Vulkan queue properties specification does not explicitly require a nonzero codec mask. Test the valid Gen9 H.264 encode case as well as the empty and no-queue cases. Opus has started implementation.

### Second patch results

The [physical-device change](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:373) derives video queue and extension availability from usable codecs and the effective video-engine count. The [format change](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_formats.c:864) uses the exposed direction extensions to set video format bits. Both patches remain local and uncommitted.

The 14-case hardware matrix passed against both build-tree ICDs and the parent reran it successfully. The `all` build retains H.264 encode on the HD 620. The `all_free` build exposes no encode queue, extension, or NV12 encode flags. The zero-queue override removes video exposure in both builds. The checks cover NV12 linear and optimal tilings and the available DRM modifiers, and compare non-video exposure against the first patch's saved drivers.

Codec workloads, Vulkan Video CTS, newer hardware, and the missing engine-info fallback remain untested at runtime. The development build's HEVC decode extension was absent. HuC status was not independently measured.

### Vulkan Video workload evidence

FFmpeg 8.0.1 ran against isolated copies of the two patched ANV libraries. Their SHA-256 hashes matched the Docker build outputs, and the Vulkan loader logged the isolated ICD and HD 620. The tests used generated 320×240 clips at 24 fps with 24 frames each. FFmpeg selected the Vulkan decode or encode queue and used Vulkan frames. The installed driver and firmware stayed in place.

- H.264 decode in the `all` build produced the same frame hashes as software decode. [Decode log](/tmp/hd620-video-smoke.QGeT6I/h264.mp4.all.log)
- VP9 Profile 0 and 10-bit Profile 2 decode in both builds produced the same frame hashes as software decode. [Profile 0 log](/tmp/hd620-video-smoke.QGeT6I/vp9.webm.free.log) · [Profile 2 log](/tmp/hd620-video-smoke.QGeT6I/vp9-10bit.free.log)
- H.264 Main encode produced a 24-frame stream. Software and Vulkan decode of that output produced identical frame hashes. [Encode log](/tmp/hd620-video-smoke.QGeT6I/h264-vulkan.log) · [Vulkan decode log](/tmp/hd620-video-smoke.QGeT6I/h264-roundtrip.log)
- The `all` build also transcoded H.264 with both Vulkan directions enabled in one process. The Vulkan encoder consumed the decoder's Vulkan frames and software decoded the resulting 24-frame stream without errors. [Transcode log](/tmp/hd620-video-smoke.QGeT6I/h264-transcode.log)

These runs cover the codecs exposed by the tested HD 620 builds. Full Vulkan Video CTS remains unrun. The development build continues to omit HEVC decode on this machine.

Opus ran an expanded 18-check matrix with the patched drivers and the saved unpatched `all` driver. It covered H.264 Constrained Baseline and High profiles, B-frames, a 1080p sample with four slices and four reference frames, VP9 8-bit and 10-bit, H.264 720p and 1080p encode, and cases where video exposure is disabled. Patched and unpatched 720p encode output matched byte for byte. The independent adversarial review will rerun the matrix and review whether the existing profile and format-query behavior affects the two capability patches. [Expanded workload script](/tmp/claude-1000/-home-polarbear--t3-projects-catchingup/86d66a2a-7fa2-44da-8a40-b19c3b4c0854/scratchpad/workload-matrix.sh)

### Independent sign-off

The adversarial reviewer approved both capability patches after independently passing the 14 capability cases and 18 hardware workload checks. Direct Vulkan queries also succeeded for supported H.264 Main and VP9 Profiles 0 and 2, including session creation and H.264 encode quality level 0. The reviewer confirmed that queries using enum values or structures from unadvertised codec extensions fall outside Vulkan valid usage. [Capability log](/tmp/hd620-final-review.FWn2Tk/p2check.log) · [Workload log](/tmp/hd620-final-review.FWn2Tk/workload.log) · [Profile and session probe](/tmp/hd620-final-review.FWn2Tk/profile-probe.log)

The reviewer reproduced three independent defects on both the saved unpatched driver and the patched driver. These are follow-ups to the two approved capability patches.

- A video format query with an output pointer and capacity zero returns a profile-format error where the spec requires `VK_INCOMPLETE`. [ANV format return path](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_video.c:731)
- A video format query for encode usage with a decode-only profile succeeds where the spec requires `VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR`. [ANV usage handling](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_video.c:635)
- The H.264 encode quality query leaves the caller's `preferredRateControlLayerCount` value intact. [ANV quality query](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_video.c:1533)

Full Vulkan Video CTS, newer GPU generations, the missing-engine-info fallback, and HEVC decode remain untested at runtime. Both patches are local and uncommitted.

### Format and encode quality follow-up baseline

The next work covers the three query defects from sign-off. A sentinel test also found three unwritten H.264 codec-specific quality outputs on both the saved baseline and the patched driver: `preferredRateControlFlags`, `preferredConsecutiveBFrameCount`, and `preferredTemporalLayerCount`. Those should be reviewed alongside the shared layer count, with analogous H.265 fields checked in source. The regression probe uses a supported H.264 Main profile and the required codec-specific output structure. [Baseline log](/tmp/hd620-query-baseline.s72myE/snapbase.log) · [Patched capability build log](/tmp/hd620-query-baseline.s72myE/all.log)

### Query follow-up proposal

Opus reproduced the capacity-zero error across every advertised HD 620 codec and confirmed that a partially filled output array already returns `VK_INCOMPLETE`. The proposed fix checks the number of eligible results before returning the error, so output capacity does not determine format support.

Opus also found that the H.264 encode capability query leaves `maxBitrate` unwritten and reports zero temporal layers even though the Vulkan minimum is one. The reviewer is evaluating the capability limits alongside the preferred output fields.

The proposed blanket rejection of encode-source usage with a decode profile is withdrawn. FFmpeg uses that combination for profile-independent images and stopped decoding H.264 and VP9 when the rejection was simulated. The reviewer is checking whether narrower usage guards are justified. [Probe baseline](/tmp/claude-1000/-home-polarbear--t3-projects-catchingup/f4919bb0-0b88-437a-b40a-672e67fb879d/scratchpad/probe/all.log) · [Vulkan profile compatibility rules](https://docs.vulkan.org/spec/latest/chapters/videocoding.html)

### Approved query fixes

The adversarial reviewer approved three changes in `anv_video.c`: distinguish a zero-capacity output array from an empty result, fill common and codec-specific encode quality preferences, and initialize the advertised encode limits. H.264 reports one temporal layer. Non-BRC bitrate values use the Vulkan minima for H.264, HEVC, and AV1. The existing 160 Mb/s BRC ceiling stays tied to the existing hardware and firmware checks.

The review also approved aligning H.265's preferred L1 reference count with its advertised maximum of one. The format usage policy stays outside this patch because of the profile-independent image compatibility rules and FFmpeg regression. The new regression matrix will compare the two capability-patched baseline builds with the new builds, check enumeration and sentinels, and rerun capability probes and actual codec workloads. [Vulkan Video requirements](https://docs.vulkan.org/spec/latest/chapters/videocoding.html#encode-h264-requirements)

### Query fixes implemented

The new `anv_video.c` changes passed 634 direct query assertions on the two HD 620 builds. Running the old capability-patched driver as the candidate failed 30 targeted assertions. All 14 capability cases and 18 workload cases passed. Both 48-frame H.264 Vulkan transcodes matched the outputs from the prior driver byte for byte, and logs showed the encoder receiving Vulkan frames directly. The review is independently rerunning the verification. The existing 160 Mb/s BRC ceiling applies to AV1 as well as H.264 and HEVC, and the code preserves that gate. [Implementation evidence](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/RESULTS.md)

### Video format component mapping baseline

A valid format query returned all four caller-supplied sentinel values in `componentMapping` for every tested row on both snapshots. Vulkan requires identity values for formats that need no swizzle. This affects H.264 decode and encode and VP9 Profiles 0 and 2, including the enumerated DRM modifier rows. Opus is planning the correction while final review of the earlier query fixes runs. [Sentinel probe](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/swizzle-probe.py) · [Candidate log](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/swizzle-patch3-all.log) · [Vulkan format properties](/home/polarbear/.t3/projects/catchingup/mesa/include/vulkan/vulkan_core.h)

The component-mapping correction is in adversarial planning review. An independent query also confirmed the separate multi-profile problem. VP9 Profile 0 decode plus H.264 encode returns each NV12 row twice. VP9 Profile 2 decode plus H.264 encode returns both P010 and NV12, in profile-list order, although the tested decode and encode profiles require different depths. Further review must establish the shared-format and empty-result behavior before changing this enumeration. [Mixed-profile results](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/mixed-profile.log)

The adversarial reviewer approved the query fixes after an independent 634-assertion query run, 30 targeted negative-control failures, 14 capability cases, 18 workloads, and both 48-frame transcodes. An FFmpeg request for 2 Mb/s produced the same 1.46 Mb/s bitstream on the baseline and candidate under automatic rate-control selection. HEVC encode, AV1 encode and the HuC rate-control path had source review only. [Independent query results](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/independent-query-check.log) · [Independent workload results](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/independent-workload.log)

The reviewer separately approved the component-mapping plan. Opus is implementing the two output assignments against the now-reviewed baseline. The multiple-profile investigation is running in parallel with no edits to the shared checkout. RADV's format query has a precedent for returning an image-usage error when the profiles do not share a supported format. [RADV shared-format selection](/home/polarbear/.t3/projects/catchingup/mesa/src/amd/vulkan/radv_video.c:986)

### Proposed shared-format query fix

Validate every supplied profile before writing output, select the one format common to their bit depths, and enumerate it once per supported query tiling. The proposal returns `VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR` for individually supported profiles that cannot share a format. A valid test list has at most one decode profile and can have more than one encode profile. The reviewer is checking error precedence and the exact return behavior before implementation. [Vulkan profile lists](/home/polarbear/.t3/projects/catchingup/mesa/src/vulkan/registry/vk.xml) · [Khronos profile-list requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkVideoProfileListInfoKHR.html)

The reviewer approved the shared-format plan after the component-mapping patch. A baseline probe confirmed that the HD 620 accepts H.264 Main and High encode as distinct profiles. One VP9 decode profile plus those two encode profiles currently returns six rows for two tilings. A later unsupported 10-bit H.264 High 4:4:4 Predictive profile produces the expected profile error but only after four earlier rows and their chained outputs have been written. The next patch's regression test will require the profile error before any output changes. [Multi-profile precedence baseline](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/multi-profile-precedence-baseline.log)

### Component mappings implemented

Opus added identity component mapping to the NV12 and P010 output rows. Both driver builds passed 1,210 query assertions, the 14 capability cases, the 18 hardware workloads, and a 48-frame Vulkan H.264 transcode. The negative control produced 256 expected mapping-only failures. Saved patch-4 drivers and the source diff are available for independent review while the approved shared-format implementation proceeds. [Component mapping results](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch4/RESULTS.md)

### Profile validation in the image format query

The separate Vulkan image-format query currently ignores supplied video profiles. On both saved patch-4 builds it returned success and a 16,384 × 16,384 extent for a VP9 Profile 0 query with 10-bit components, even though the video capabilities query rejects that profile combination. Accepted profiles were also reported as compatible with the opposite bit depth's format. Review is needed to establish the narrow validation change and preserve profile-independent paths. [Image format query requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceImageFormatProperties2.html) · [Hardware query log](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/video-image-profile-patch4-all.log)

The adversarial reviewer approved the component-mapping patch after independently passing 1,382 query assertions and confirming all 276 written rows had identity mappings. The redundant empty-list case was omitted because the spec requires at least one profile. The 14 capability cases, 18 workloads, and both 48-frame H.264 transcodes passed against pinned snapshot libraries. Transcode output matched patch 3 byte for byte. Runtime coverage was HD 620 H.264 and VP9 profiles 0 and 2. [Independent query log](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch4/independent/query-check.log) · [Independent row audit](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch4/independent/audit-query.log)

Opus proposed sharing ANV's existing video profile check with the image-format query. The proposed change rejects unsupported profiles in nonempty lists and preserves ordinary format and usage checks. The adversarial reviewer is evaluating error order and caller impact. Additional baseline probes confirmed that the unsupported 10-bit VP9 Profile 0 combination also succeeds with sampled-only usage on both builds and with profile-independent decode usage where maintenance1 is advertised. [Image query options baseline](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/video-image-profile-options-patch4-all.log)

The adversarial reviewer approved reuse of the existing validator through `anv_video_profile_supported`. The image-format query must validate the nonempty profile list immediately after parsing its input chain, before the format gate or any output-chain writes. Return the first failing profile error and zero the core image properties while preserving output headers and chained payloads. Baseline and candidate must be separately pinned. [Khronos image-format query requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceImageFormatProperties2.html)

### Shared formats implemented

Opus implemented the reviewed shared-format algorithm. Both builds passed 1,815 query assertions, 14 capability cases, 18 workloads, and two byte-identical 48-frame transcodes against the component-mapping baseline. The negative control failed 390 targeted assertions. Compatible lists emit one row per eligible tiling. Different supported depths return an image-usage error and leave outputs intact. Unsupported profiles now leave the caller's count unchanged even for single-profile queries. This follows from validating before initializing the output array. Independent review is using the saved patch-5 diff and libraries. [Shared-format results](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch5/RESULTS.md)

The approved image-profile validation patch is in implementation against the pinned patch-5 baseline. The pre-fix format-precedence probe confirmed that the supported profile with `VK_FORMAT_R64_SFLOAT` returns the existing format error, and that adding an unsupported profile currently returns that same format error rather than the profile-specific result. [Format-precedence baseline](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/video-image-profile-precedence-patch4-all.log)

The adversarial reviewer approved the shared-format implementation. From four pinned snapshots, the independent run passed 1,815 query assertions, all 14 capability cases, all 18 workloads, and two 48-frame H.264 transcodes. The negative control failed the same 390 intended assertions. Direct probes confirmed first-error precedence, output preservation, and order-independent shared formats for valid mixed lists. Shared P010 with 10-bit encode, newer GPUs, and Vulkan Video CTS remain outside the runtime coverage. [Independent query log](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch5/independent/query.log) · [Independent transcodes](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch5/independent/transcode.log)

### Image-profile validation implemented

Opus reused the existing ANV validator in the generic image-format query. Both builds passed 294 new image-format assertions and the 1,815 existing video-format assertions, 14 capability cases, 18 workloads, and both Vulkan transcodes. The negative control failed the 46 intended checks. Image-format errors now match the profile capability error before the format check and preserve output headers and chained payloads. Sparse coverage compared the zero-count result, since sparse allocations are unavailable on the HD 620. The saved builds and source are in independent review. [Implementation results](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch6/RESULTS.md)

### Reference-picture usage baseline

Both Vulkan format queries currently accept an encode DPB request with only a decode profile and a standalone decode DPB request with only an encode profile. The narrower follow-up is evaluating those two roles against profile-independent image rules. A separate probe found that NV12 decode plus depth/stencil attachment usage also succeeds in both queries. Acceptance alone does not establish the correct fix, so the reference-picture plan will preserve FFmpeg's known working mixed-usage queries and examine the attachment case separately. [Reference-picture probe](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/video-dpb-usage-patch6-all.log) · [Attachment usage probe](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/video-format-usage-patch6-all.log)

The adversarial reviewer approved image-profile validation after independently comparing the saved source and all four snapshot libraries. The run passed 294 image-format assertions, 1,793 valid retained video-format assertions, 14 capability cases, 18 workloads, and four transcodes. The negative control reproduced 46 expected failures. The reviewer excluded 22 retained assertions for empty or missing lists from the video-format query gate under VUID 06812. Empty and missing lists remain covered where valid in the image-format query. [Image-format rerun](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch6/independent/positive.log) · [Retained valid queries](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch6/independent/retained.log)

### Proposed reference-picture usage fix

Opus proposed a shared check for a supplied nonempty profile list. Reject encode DPB without an encode profile, and decode DPB without decode destination or a decode profile. The plan preserves decode DPB coincident with output and the mixed encode-source request FFmpeg uses with profile-independent images. Effective usage2 overrides legacy usage, and profile errors take precedence. Four synthetic assertions in the existing video query suite expect the erroneous success today and will need precise new expectations if approved. The reviewer is checking the specification and test validity. [Creation rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageCreateInfo.html) · [FFmpeg decoder queries](https://github.com/FFmpeg/FFmpeg/blob/n8.0.1/libavcodec/vulkan_decode.c#L907-L921)

The reviewer approved the reference-picture plan. Opus is implementing both checks against the signed-off patch-6 snapshots and will test FFmpeg's mixed-usage pattern and the four intentionally corrected expectations.

### Depth/stencil compatibility investigation

On the HD 620, R16, NV12 and P010 report no optimal depth/stencil attachment feature. Their image-format queries nevertheless succeed for depth/stencil usage with a mutable, extended-usage image and an implicit view-format list. Explicitly restricting the view list to the base format makes all three fail. D16 succeeds with either list. The first probe had a Python structure-lifetime bug. The corrected run keeps the chain objects alive and produced the recorded results. Opus is checking the same-size format classification in ANV and how to coordinate the generic image-format fix with the video query, which also advertises the incompatible attachment usage. [Corrected probe](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/extended-usage-depth-probe.py) · [Hardware results](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/extended-usage-depth-patch6-all.log)

### Proposed depth/stencil correction

Opus proposed excluding distinct depth/stencil formats from the implicit compatible-view scan and rejecting depth/stencil usage in the video-format query, which only emits NV12 and P010. The full and free saved builds produced byte-identical bug results. The reviewer will verify the two-path change, legal explicit lists, and the placement of the video guard. The guard must run before output-array initialization, since the initializer writes zero to the caller's count. [Free-build baseline](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/extended-usage-depth-patch6-free.log) · [Outarray initialization](/home/polarbear/.t3/projects/catchingup/mesa/src/vulkan/util/vk_util.h:201)

The reviewer approved both depth/stencil guards. The generic compatibility guard runs after the identical-format case and before any block-size matching. The video guard runs after profile, shared-format, and DPB checks but before output initialization. Explicit multi-planar tests use the legal `R8_UNORM`/`R8G8_UNORM` and `R10X6_UNORM_PACK16`/`R10X6G10X6_UNORM_2PACK16` plane formats. Earlier whole-image singleton lists are excluded from regression evidence. [Legal plane-format probe](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/extended-usage-depth-legal-planes-probe.py)

The first reference-picture verification run was killed for running out of memory. Its source diff and probes survived, and one Docker build was still linking when checked. The recovery run will reuse completed outputs and run any remaining builds serially with one compiler job, then resume verification. The depth/stencil patch is still gated on the saved reference-picture baseline.

### Reference-picture validation implemented

The serial recovery build completed both configurations. One previous linker run left a zero-byte output and a concurrent driver copy was truncated. The recovery rebuilt the library and verified the hashes of the pinned binaries before testing. The implementation passed 844 DPB assertions, 1,793 valid retained format assertions with the four intentional expectation changes, 294 image checks, 14 capability cases, 18 hardware workloads, and both byte-identical H.264 transcodes. The negative control reproduced 235 targeted failures. The final reviewer is rerunning checks serially before the next build. [Recovery and verification record](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch7/RESULTS.md)

The adversarial reviewer approved the reference-picture patch after matching all four snapshot hashes and independently passing 844 DPB assertions, 1,793 video-format and 294 image-format assertions, 14 capability cases, 18 workloads, and both 48-frame transcodes. The negative control failed 235 intended assertions. The FFmpeg mixed usage and profile-independent cases matched the baseline. The depth/stencil implementation has started with serial builds. [Independent DPB checks](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch7/independent/dpb.log) · [Independent transcode evidence](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch7/independent/transcode.log)

### Depth/stencil compatibility implemented

Both serial builds passed 809 new assertions, with 245 targeted failures from the negative control. All retained video, image and DPB probes passed, along with 14 capability cases, 18 hardware codec workloads, and both byte-identical H.264 transcodes. The first checker run had a capacity-one expectation error and treated D16/S8 color and storage requests as expected changes. Those requests already fail on the baseline and now serve as unchanged controls. Independent review is underway. [Implementation record](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch8/RESULTS.md)

The replay check also found that existing P010 Y-tiled DRM rows advertised storage, but image-format queries rejected the returned tuple with legal plane views. Optimal rows succeeded. Opus is checking whether another legal view list or modifier can satisfy the request before proposing a targeted correction.

The adversarial reviewer approved the depth/stencil implementation. Independent serial verification passed 809 new assertions, 1,793 retained video, 294 image and 844 DPB assertions, 14 capability cases, and 18 workloads. The negative control reproduced 245 targeted failures. Both H.264 transcodes matched the prior build byte for byte. A discrepancy in the earlier 720p PSNR log was resolved by rechecking the three byte-identical encoded files, each of which measured 48.153798 dB. The three P010 Y-tiled storage replays failed identically before and after the two guards. [Independent query evidence](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch8/independent/query-review.log) · [Independent media evidence](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch8/independent/media-review.log)

### P010 DRM storage replay

The targeted replay passed on the four pinned `all` and `free` drivers before and after the depth/stencil patch. For each of the three affected query forms, Y-tiled P010 accepts storage when the explicit view list contains `R16_UNORM` and `R16G16_UNORM` and the returned mutable and extended-usage flags are supplied. The original R10X6 view list, the mutable-only case, and the linear and X-tiled cases still fail. The optimal P010 and Y-tiled NV12 controls passed. This establishes a working format query with the alternate list and does not exercise image or view creation. Independent review is checking the Vulkan view rules. [Pinned hardware replay](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/p010-drm-storage/patch8-all.log)

The adversarial reviewer approved closing the Y-tiled P010 format-query issue without a driver change. The alternate R16 view list is legal for the format query and passed on all four independently pinned drivers. That list omits a full P010 view, so this does not establish that it can serve as the decode destination view for a P010 session. Image and view creation remain outside this evidence. [Independent replay](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/p010-drm-review.59tjBZyo/patch8-all.log) · [View usage rules](https://docs.vulkan.org/spec/latest/chapters/resources.html#VUID-VkImageViewCreateInfo-pNext-01585)

A separate optimal-tiling question remains under assessment. The P010 video row reports storage usage even though the base format does not advertise the storage feature bit. Extended usage allows compatible view features in the generic image query. The video format capability text also imposes a direct optimal-tiling requirement. Assess whether a source correction can respect that requirement while preserving supported workloads. [Video format capability text](https://docs.vulkan.org/spec/latest/chapters/videocoding.html#video-format-capabilities)

Opus recommended keeping current optimal-tiling behavior pending clarification of the Vulkan Video feature requirement and extended-usage rules. FFmpeg 8.0.1 requests optimal decode images with storage and sampled usage. Its frame setup adds mutable and extended-usage flags for multi-planar sampled or storage images and can select profile-independent images. Removing the optimal row or its requested bits may affect these supported paths. The adversarial review is checking the profile-bound and profile-independent requirements separately. The workspace source diff still matches the approved patch-8 snapshot. [FFmpeg decode-output usage](https://github.com/FFmpeg/FFmpeg/blob/n8.0.1/libavcodec/vulkan_decode.c#L1042-L1056) · [FFmpeg image flags](https://github.com/FFmpeg/FFmpeg/blob/n8.0.1/libavutil/hwcontext_vulkan.c#L2688-L2724)

### ANV Video series sign-off

All eight changes received independent adversarial approval. The workspace matches the saved patch-8 diff and passes `git diff --check`. The series corrects queue codec reporting and queue availability, format enumeration and encode query outputs, component mappings, shared video formats, image-profile validation, DPB usage checks, and depth/stencil format compatibility. [Verified Mesa diff](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch8/patch8-full.diff) · [Final hardware regression evidence](/home/polarbear/.local/state/catchingup/hd620-anv-query-verification/patch8/independent/query-review.log)

The final reviewer approved preserving the OPTIMAL NV12/P010 behavior pending specification clarification. The literal video-format feature rule and the extended-usage compatible-view rule do not yield a minimal correction that preserves the exercised workflows. FFmpeg 8.0.1's decode output can keep both the profile list and extended storage usage. Its default use of `ALIAS`, which the reviewed ANV rows omit, is a separate flag-matching caveat. The working FFmpeg workloads therefore do not establish complete `06811` conformance. The query replay also does not exercise creation and storage operations for the specific P010 DRM plane-view tuples. [Vulkan video format capabilities](https://docs.vulkan.org/spec/latest/chapters/videocoding.html#video-format-capabilities) · [FFmpeg decode setup](https://github.com/FFmpeg/FFmpeg/blob/n8.0.1/libavcodec/vulkan_decode.c#L979-L1147)

The Mesa series remains local and uncommitted. The hardware evidence covers the HD 620 and its advertised H.264 and VP9 paths. Vulkan Video CTS, newer GPUs, and unadvertised HEVC and AV1 paths remain outside that coverage.
