# HD 620 driver TODO

Open work after round 2. Numbers match the master list from 2026-11-19. Effort: (S) small, (M) medium, (L) large. Evidence for each item is in [FINDINGS.md](FINDINGS.md), [IMPLEMENTATION.md](IMPLEMENTATION.md), [ROUND2.md](ROUND2.md) and the `sweep/` inventories.

## Implementation: fixes

- [x] **4. VA-API → Vulkan zero-copy (M). Done (2026-11-19).** The `EINVAL` came from FFmpeg, not the drivers. `vaapi_map_from()` routed Vulkan destinations to the memory mapper instead of returning `ENOSYS`. The fix is a patch to libavutil. NV12 and P010 now map byte-exactly through `hwmap`, and 1080p H.264 reaches Vulkan about 3× faster than with a CPU download (1.49 s vs 4.58 s for 960 frames). See [zero-copy/README.md](zero-copy/README.md).
- [ ] **18. Iris timers, remaining work (M).** Test a live counter rollover; measure the overhead of the two forced submissions; try the compute-batch timestamp placement for draws, clears, blits and copies; verify on Xe and the blitter.
- [x] **26. ANV error handling (S). Done.**
  - An explicit modifier layout that isl rejects now returns `VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT` (it returned `OUT_OF_DEVICE_MEMORY`). Verified with `small-fixes/modifier-layout.c`: a 1000-byte Y-tiled pitch now gets the specific error, and a 1024-byte pitch still succeeds.
  - Batch clone: a clone failure already frees its partial copy, and the FIXME now says so. The real leak was a `seen_bbos` allocation failure after a successful clone; the copies are now handed to the primary command buffer. Compile-checked only, since that out-of-memory path can't be triggered from a normal test.
- [x] **33. Rusticl clock telemetry (S). Done.** Iris reads the maximum configured frequency from sysfs: `gt_max_freq_mhz` on i915, `tile0/gt0/freq0/max_freq` on Xe. If that fails, it falls back to 400. clinfo now reports 1000 MHz (it reported 400), and the OpenCL profiling test still passes.

## Implementation: new features

- [ ] **6 → 11. Vulkan HEVC Main/Main10 decode on Gen9 (L).** *Item 6 result:* `kbl_huc_4.0.0` firmware is installed, but i915 reports "HuC disabled". Gen9 loads HuC only with boot parameter `i915.enable_guc=2`. That needs a reboot; reloading i915 live would kill the desktop session. **Blocked on the user rebooting with that parameter.** First check whether HuC can load (i915 has it off on Gen9). If it can, use the existing HuC path; otherwise write a queue-ordered slice parser. Verify DPB and references, slices, tiles and stream changes.
- [ ] **12. Vulkan HEVC encode on Gen9 (L).** Use iHD as the reference. Start with fixed-QP Main and Main10.
- [ ] **13. H.264/HEVC bitrate control in Vulkan encode (M–L).** Validate VBV behaviour on long streams across rate changes and GOPs.
- [ ] **14. Default-on Vulkan H.264 and VP9 (M).** Gate on per-codec conformance: profiles, DPB, stream changes, and concurrent graphics.
- [ ] **17. Broaden multi-mip HiZ (M–L).** Full mip chains, a nonzero base mip in sampled views, array layers, more depth formats, then application frame times.
- [ ] **27. `G16B16G16R16_422` / `B16G16R16G16_422` and linear YUV chroma reconstruction (M).**
- [ ] **36. Vulkan OA performance counters (M).** Separate pipeline statistics from OA sampling and test under authorised profiling access.
- [ ] **46. Native ASTC 5x5 in Iris (M–L).** Resolve HiZ (and, to be safe, CCS) instead of emulating. Round 2 found that HiZ triggers the hang on KBL.
- [ ] **47. Software fallbacks for ray tracing, mesh shaders, VRS and sparse textures (L, low value).**
- [ ] **48. Software AV1 decode into presentation (L).**

- [ ] **New (from test 5). Gen9 transfer queue on the copy engine (L).** `XY_FAST_COPY_BLT` ran at 10.5 GB/s versus 6.3 GB/s for render-engine copies. ANV has no Gen9 BCS command emission, and every BCS execbuf goes through i915's command parser, which limits batch chaining. Needs a design, plus concurrency, visibility and ownership tests.
- [ ] **New (from test 15). VP9 8-bit encode on KBL through iHD (L, out of Mesa).** No driver exposes it, even though Intel marketed the hardware feature.

## Performance optimization

Driver flushes and state:
- [x] **19. Opt-in to skip the clear-colour state-cache invalidate (S). Done.** Set the drirc option `anv_gfx9_skip_clear_color_state_invalidate` (off by default) in drirc or the environment. On the release build, the `load` scenario drops from 2.3–2.6 µs to 1.4–1.5 µs per render pass, and every `load`/`blend2` pixel check passes (`gen9-stalls/drirc-option.txt`).
- [ ] **20. `MEDIA_VFE_STATE` re-emit after GPGPU → 3D (M).** About 0.5 µs per switch. Find the geometry-flicker reproducer before touching it.
- [x] **21. Iris dynamic VAO fast path on KBL (S). Measured; no change.** Measured with glmark2 over 5 rounds. It is correct, but no scene got a measurable gain and `build:use-vbo=true` was 8% slower, which confirms the "harmful" comment. See [vao-fastpath/README.md](vao-fastpath/README.md).
- [x] **22. Gen9 `CC_VIEWPORT` re-emit in BLORP (#11647) (S). Measured; no change.** With reuse re-enabled, the formerly flaky multiview test passed 300 of 300, but that cannot rule out a sub-1% flake, and the saving is one 32-byte allocation per BLORP operation. The workaround stays. See [blorp-ccvp/README.md](blorp-ccvp/README.md).
- [ ] **23. Barrier flush precision (M). Investigated; deferred.** Each access category already maps to its specific cache flush; the TODO itself says the hardware allows no finer control. The remaining idea, tracking which caches are actually dirty so redundant flushes can be skipped, needs application traces to show a benefit before the design is worth it.
- [x] **24. VF cache across secondary command buffers (M). Done, and it found an upstream bug.** A Gfx9 VF cache invalidate was queued after the draw that needed it, so a draw from a buffer 4 GiB away from the previous one read stale vertices: 50/50 failures, including on installed Mesa 26.0.8. Fixed by invalidating before the vertex buffers are emitted. Secondaries are now tracked on entry and exit instead of always invalidating: 2,000 → 2 invalidates per 1,000 executions, with no GPU time change in that benchmark. See [vf-secondary/README.md](vf-secondary/README.md).
- [x] **25. Pipeline-statistics query flush (S). Investigated; no change.** Every statistics counter is incremented by in-flight work and read with `MI_STORE_REGISTER_MEM`, so the CS stall at begin and end is what makes the result exact. None of the stats can skip it.

Compiler (brw on Gfx9):
- [ ] **28. Spill single GRFs instead of whole VGRFs (M).**
- [ ] **29. Keep SIMD16 for 32-bit quad swizzles and mixed-mode float (M).**
- [ ] **30. Allow the safe subset of MAD → MAC fusion on Gfx9 (M).**
- [ ] **31. Remove the unneeded byte → word expansion in int8 DPAS emulation (S).**
- [ ] **32. Calibrate the Gfx9 cost model from HD 620 measurements (M).**

Power:
- [ ] **40. RPS down-clock timeout and RC6 media power-gate hysteresis (M).**

## Done in this session (for reference)

- **Commits:** the round-2 work is committed on branch `hd620-round2` and pushed to `github.com/CodeGrogu/mesa`.
- **Drivers for daily use:** round-2 Vulkan and Rusticl OpenCL; OpenGL is available through `mesa-round2 <app>`.
- **VA-API:** iHD only; i965 removed.
- **Tests:** 5 (copy-engine fixture), 8 (CTS subsets) and 15 (VP9 encode) are done. Results are in [TESTING.md](TESTING.md): 8,956 CTS cases with 0 failures and 0 regressions; the copy-engine fast blit runs at 10.5 GB/s versus 6.3 GB/s for render copies; VP9 encode is not exposed by any driver on KBL.
