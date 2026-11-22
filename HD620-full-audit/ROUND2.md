# HD 620 audit, round 2 (2026-11-19)

This round swept ANV, Iris, ISL, BLORP, the brw compiler, i915 and the upstream trackers for Gen9 issues, then verified the strongest leads on this HD 620 (KBL GT2, `8086:5916`). Haiku agents produced the raw inventories in [`sweep/`](sweep/); every item below was checked against source or hardware before being listed.

## Verified on this GPU

| Status | Area | Finding | Evidence |
| --- | --- | --- | --- |
| **Fixed (verified)** | Rusticl / Iris timers | The Iris timer-ordering candidate also fixes OpenCL event profiling. A 256-round kernel reported 250 ns on the baseline and 11.56 ms on the candidate against 11.18 ms of i915 render-busy time. The container needed the staged libclc SPIR-V files. | [Rusticl results](evidence/rusticl-candidate/README.md#result-2026-11-19) |
| **Bug reproduced** | brw compiler (Gfx9) | SIMD32 FP16 three-source instructions under divergent control flow give wrong results on KBL as well as the Comet/Whiskey Lake parts in upstream issue #16528. Lanes 16–31 follow lane L−8's execution mask. Installed Mesa 26.0.8 and the checkout both fail; SIMD8 and SIMD16 pass. | [Reproducer](fp16-simd32/) |
| **Upstream fix verified on KBL** | brw compiler (Gfx9) | Upstream MR 45047 (open, tested only on Comet/Whiskey Lake by its author) fixes it here too. Applied to a copy of the checkout: 0 mismatches in 12 runs, repeated 3 times, against 38,349 on the unpatched checkout. The dump shows each `mad (32) … :hf {Align16}` split into `mad (16)` and `mad (16\|M16)`. All 410 brw and 150 gen unit tests pass, including the MR's new Gfx9 SIMD32 tests. Its first commit fixes the SIMD32 cmod hang in #16526. That hang was later reproduced here and shown to be fixed (see the hang-proof row). | [Results and assembly](fp16-simd32/README.md) |
| **Bug found and fixed locally** | ANV host image copy (Gen9) | `vkCopyMemoryToImageEXT` and `vkCopyImageToImageEXT` decoded UNORM ASTC to RGBA8 and wrote it into the emulation plane. On Gen9 non-LP that plane is an ASTC surface (void-extent denorm flush mode), so sampling returned the ASTC error colour for ~99% of texels. sRGB ASTC and GPU copies were correct. Not fixed upstream as of today's main. | [Test](astc-host-copy/) |
| **Fixed (verified)** | ANV host image copy (all gens with ASTC emulation) | Two more defects in the same code. The decode path passed block counts where texels were expected, and `vkCopyImageToImage` never filled the sampled plane. `write_emu_plane()` replaces both paths. Every case now matches: 4x4, 8x8 and 5x5, UNORM and sRGB, through cmd-copy, host-copy and host-i2i. Exact colours pass, and random blocks match native hardware decode. | [Results](astc-host-copy/README.md) · [Patch](patches/anv-astc-host-copy-and-gfx9-5x5.patch) |
| **Hang avoidance implemented and proven** | ANV ASTC 5x5 (Gfx9) | ASTC 5x5 is now decoded to RGBA8 on Gfx9, as Iris does, because ASTC 5x5 next to aux-compressed data in the sampler hangs Gfx9 (#1892). Other block sizes stay native. `ANV_NATIVE_ASTC_5X5=true` restores native 5x5. Emulated output matches native decode within RGBA8 rounding. On this KBL part the hang needs **HiZ**: native 5x5 with a HiZ depth texture hangs on the first submission, while every CCS_E combination tested ran clean. | Same · [Hang proofs](hang-proofs/README.md) |
| **Both hangs reproduced and prevented** | brw #16526, ANV #1892 | Unfixed drivers hung the GPU on submission 0, each time with `GPU HANG: ecode 9:1:8ed8fff3`. With the fixes: 450 + 150 SIMD32 cmod submissions, and 600 ASTC 5x5 + HiZ submissions, all completed. For ASTC, the same binary hangs with emulation turned off. i915 reset only the render engine each time, and the desktop stayed up. | [Hang proofs](hang-proofs/README.md) |
| **Fixed (verified)** | ANV scratch allocation | `anv_scratch_pool_alloc()` failure was ignored, and Gfx9 programmed scratch at address 0. With an `LD_PRELOAD` shim failing the 42 MiB scratch BO, pipeline creation returned `VK_SUCCESS` before the fix and now returns `VK_ERROR_OUT_OF_DEVICE_MEMORY`. Success-path output is unchanged. | [Results](scratch-oom/README.md) · [Patch](patches/anv-scratch-alloc-errors.patch) |
| **Measured, unchanged** | Four Gen9-only flushes | Null PIPE_CONTROL before VF invalidate: about 50 ns, no measurable cost. Forced push-constant reallocation: no measurable cost. `MEDIA_VFE_STATE` re-emit: about 0.5 µs per compute → 3D switch. State-cache invalidate after clear-colour reload: about 0.66 µs per render pass. Removing that last one passed stricter blend checks, but the PRM requires it, so nothing was changed. | [Measurements](gen9-stalls/README.md) |
| **No regressions found** | Round-2 build | The round-2 build carries the eight ANV patches, multi-mip HiZ, MR 45047 and the ASTC patch (`b614763e…`). The buffer compute probe gave 1,048,576 exact values. FP16 lane-mask: 0 mismatches. All four HiZ pixel-correctness runs and the size verdict passed. | `evidence/compute-round2.txt` · `evidence/round2-hiz/` |
| **Fixed (verified)** | ANV Gfx9 VF cache | An upstream bug queued the VF cache invalidate after the draw that needed it. Two vertex buffers 4 GiB apart rendered stale vertices 50/50 times (also on Mesa 26.0.8). Fixed by invalidating before binding. Secondary command buffers are now tracked: 2000 → 2 invalidates per 1000 executions. | [Results](vf-secondary/README.md) |
| **Works (HuC)** | Vulkan HEVC decode | With HuC loaded, all four HEVC clips (192 frames, Main and Main10) decode bit-identically to software. No code change needed. | [Results](hevc-huc/README.md) |
| **Enabled by default** | Vulkan Video decode (Gfx9) | H.264, VP9 and H.265 (with HuC) decode is exposed without `ANV_DEBUG`. CTS decode: 98/98 supported cases pass, 0 failures. Opt out with drirc `anv_gfx9_video_decode=false`. Encode still needs `ANV_DEBUG`. | [TESTING.md](TESTING.md) |

## Found in source, not hardware-tested

| Area | Finding | Why not tested |
| --- | --- | --- |
| ANV host image copy on Gfx12.5+ | The decode-path fixes above also apply to GPUs that emulate all ASTC formats (`emu_astc_ldr`). The same code was exercised here through Gfx9 5x5. | No such GPU here. |
| ANV scratch on Gfx12.5+ | The surface-based scratch paths now set a batch error instead of dereferencing a NULL BO. | No such GPU here. |

## Leads checked and rejected

| Lead (from sweeps) | Verdict |
| --- | --- |
| Iris fast-clear and resolve end-of-pipe syncs are redundant | Required by the Sky Lake PRM ("Any transition … requires end of pipe synchronization"). |
| brw skips the precise-trig workaround on KBL | Intentional exemption. |
| Dual-source SIMD32 limit excludes Gen9 | The hang it avoids is ICL-specific (#2183). |
| fadd maps to an LSC opcode without a version gate | That mapping is only reached on LSC hardware. |
| New i915 display regressions on this laptop | This boot logged only the known single atomic-update miss; PSR is disabled on the command line. |

## Local Mesa tree state

`mesa/` is on branch `hd620-round2`. On top of `0e9e108` it contains the eight ANV video patches, the Iris timer and saturation fixes, multi-mip HiZ, upstream MR 45047, the ASTC patch and the scratch fix. The tested binaries are in `~/.local/state/catchingup/round2/`:

- `fp16-mr45047`: MR 45047 only.
- `scratch` (`31ced66e…`): the complete tree.
- `stall-exp`: the complete tree plus the measurement toggles. Not for use.

Nothing has been sent upstream.

The work is committed as 12 commits on branch hd620-round2 and pushed to https://github.com/CodeGrogu/mesa, with tests and evidence on branch hd620-audit and issues #1-#7.

## Next candidates

1. Decide whether the clear-colour state-cache invalidate (about 0.66 µs per render pass) is worth an opt-in drirc option, given the PRM requirement.
2. Find a flicker reproducer for the Gen9 `MEDIA_VFE_STATE` re-emit before considering it again.
3. Run real Vulkan applications (DXVK, VKD3D-Proton) on the round-2 driver for frame times and stability.
