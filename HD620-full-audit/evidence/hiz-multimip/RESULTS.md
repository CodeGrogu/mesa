# Gen9 multi-mip HiZ: scoped enablement (HD 620, KBL GT2 0x5916)

Nothing was committed. There were no upstream posts, system changes or agents. The approved ANV patches 1–8 and the Iris/mi_builder edits are preserved. The ANV diff outside `anv_image.c` still hashes to patch8's `6ebf1574…`.

## Change
There is one hunk in `src/intel/vulkan/anv_image.c` ([diff](hiz-candidate-anv_image.c.diff)). The Gen9 "disable aux for multi-LOD depth" gate is relaxed only when `anv_gfx9_multi_lod_hiz_allowed()` is true:

- format `VK_FORMAT_D32_SFLOAT`, 2D, `VK_IMAGE_TILING_OPTIMAL`, 1 sample, 1 layer, depth 1
- **every** LOD ≥ 1 has `minify(w) % 8 == 0` and `minify(h) % 4 == 0`. LOD0 can be any size because BLORP already grows its rectangle.

HiZ is then enabled for the whole image. There is no LOD-prefix enablement, no per-LOD state and no BLORP or assertion change. `blorp_hiz_op()`'s Gen8/9 upper-LOD ambiguate assertion (blorp.c ~373) holds by construction, and asserts are active in this debugoptimized build. Every other image keeps aux disabled. The perf-warn text now says "outside the aligned HiZ scope".

## Build
- Docker `hd620-mesa`, with a fresh builddir `/build/hiz-all` using the same meson options as `/build/base-all`.
- `ninja -j1` built libvulkan_intel.so with rc 0 ([build.log](build.log)). The single warning is upstream's `vasprintf` warning in `vk_acceleration_structure.c`.
- The source `src/intel` diff was `27fa38e6…` before the build and is the same after all tests ([candidate-src-intel.diff](candidate-src-intel.diff)).
- An Iris-only file (`iris_query.c`) changed in the shared tree while the build ran, from the concurrent Iris work. It is not part of this Vulkan-only build.
- Candidate: `~/.local/state/catchingup/hd620-hiz-multimip/icd/candidate/`, **sha256 `3f01be27…0ebbe0c1`**, read-only, with IDENTITY. It is identical to the build-tree library.
- Control: the approved patch8-all snapshot `a741f7be…`. Its hashes were re-verified after the tests ([immutability-after-tests.txt](immutability-after-tests.txt)).

## Test
- Harness: [vulkan-hiz-mips.c](../../hiz-multimip/vulkan-hiz-mips.c), [run-hiz.sh](../../hiz-multimip/run-hiz.sh), [bat-summary.py](../../hiz-multimip/bat-summary.py). Source and binary hashes are in [test-sources-sha256.txt](test-sources-sha256.txt).
- Each run prints driverName/driverInfo and the path of the loaded `libvulkan_intel.so`. The runner also checks that the loader log picked the intended ICD.
- Every check is exact per pixel (bitwise float compare) on every pixel of every mip against a CPU model:
  1. **Phase 1, forward and reverse LOD order:**
     - Buffer → image upload of a distinct pattern per mip.
     - TRANSFER_DST → attachment per mip (ambiguate).
     - `vkCmdClearAttachments` of an unaligned interior rect. The value is the HiZ clear value 1.0 (fast path) on alternating LODs and another value (slow path) on the rest.
     - Two depth-tested draws (LESS, scissored) at different depths.
     - Attachment → TRANSFER_SRC, then readback of every mip after all mips were touched.
  2. **Phase 2, for each target LOD:**
     - loadOp CLEAR (1.0 fast / 0.75 slow) plus a draw.
     - loadOp LOAD with three draws: one nearer that must win, one that must be rejected unless the clear was 1.0, and one at z=1.0 that must never pass.
     - Every LOD is sampled via `texelFetch` from one view of all mips in SHADER_READ_ONLY (Gen9 samples with HiZ) and copied. The target must match the model and **every other LOD must be unchanged**.
  3. **Phase 3, resolve cases (input-attachment usage):**
     - Draw, then attachment → GENERAL. This is AUX_INVALID on Gen9, so it is a **full resolve**.
     - The resolved LOD is copied from GENERAL.
     - GENERAL → attachment (**ambiguate of resolved data**), then a draw and a check of all LODs.
- Checker self-test: corrupting one expected interior pixel at LOD3 makes the run fail with rc 2 ([checker-selftest-candidate.log](checker-selftest-candidate.log)).

## Results
| Check | Candidate | Candidate `INTEL_DEBUG=nohiz` | Control patch8 | Control nohiz |
| --- | --- | --- | --- | --- |
| Pixel cases ([correct-*.log](correct-candidate-default.log)) | **16/16** | 16/16 | 16/16 | 16/16 |

- **Activation by allocation** ([sizes-verdict.txt](sizes-verdict.txt), PASS):
  - Every in-scope multi-mip image grows only on the candidate without nohiz. For example, 2048²×4 goes from 24,641,536 to 27,754,496 B, 704×288×4 from 1,216,512 to 1,380,352 B, and 17×9×2 from 4,096 to 8,192 B.
  - Every out-of-scope image is byte-identical to nohiz and to the control: 696×288 (LOD1 w=348), 704×280 (LOD2 h=70), 32²×5, 2049×1026 (LOD1 h=513), full chain 64×32×7, D16, X8_D24, D24S8, D32S8 and the 2-layer image.
  - The single-LOD control has HiZ on both drivers.
- **Activation by decoded batches** ([bat-*.txt](bat-candidate-aligned-704x288x4.txt), `INTEL_DEBUG=bat`):
  - On the candidate, `3DSTATE_DEPTH_BUFFER` has HiZ Enable true at LODs 0–3 (704×288) and 0–1 (17×9).
  - `3DSTATE_WM_HZ_OP` ambiguates and full resolves at every LOD, each with a rectangle exactly the LOD extent: 352×144, 176×72, 88×36, 8×4. LOD0 of 17×9 grows to 24×12.
  - Fast clears at upper LODs are both full-surface and partial unaligned. For example, LOD3 (22,9)–(66,27) and LOD1 of 17×9 (2,1)–(6,3) all produce exact pixels.
  - Control and out-of-scope images: HiZ Enable false at every LOD, and no HZ ops.
- **Negative misalignment:** the gate excludes these images, so no HiZ op is issued and the BLORP assertion is never reached. They still pass the same pixel tests through the non-HiZ path.

## Timing (only after correctness passed)
These are GPU timestamps, 64 ops × 6 runs after a warm-up, best/mean in µs ([timing-*.log](timing-candidate-default.log)). The "frame" is a synthetic depth-only workload: clear + 8 full-screen draws back-to-front + 8 occluded draws. It is not an application trace.

| Image / LOD | Candidate clear | nohiz clear | Control clear | Candidate frame | nohiz frame | Control frame |
| --- | --- | --- | --- | --- | --- | --- |
| 2048² LOD0 | 70.7 | 628.7 | 631.3 | 1335 | 18310 | 18331 |
| 2048² LOD1 1024² | 17.7 | 151.4 | 149.9 | 290 | 3867 | 3990 |
| 2048² LOD2 512² | 5.4 | 35.8 | 36.8 | 74.3 | 629 | 629 |
| 2048² LOD3 256² | 2.1 | 10.9 | 10.9 | 20.1 | 147 | 147 |
| 704×288 LOD1 352×144 | 1.9 | 9.1 | 9.0 | 16.0 | 116 | 116 |
| 704×288 LOD3 88×36 | 0.86 | 2.5 | 2.7 | 3.6 | 8.1 | 8.0 |

The candidate under nohiz matches the control, so the change has no cost when HiZ is not in use.

## Not covered / limits of this candidate
- **Formats other than D32:** D16, X8_D24, D24S8 and D32S8 are deliberately out of scope and are size-checked only. Stencil preservation with multi-mip HiZ is untested because it is not enabled.
- **Array layers:** layered images are out of scope and are size-checked only.
- MSAA, 3D and DRM-modifier images are excluded.
- No CTS run.
- No real application or game frame times. The timing is synthetic.
- Queue-family ownership transfers and secondary command buffers were not exercised.
- **Full mip chains can never qualify:** any LOD narrower than 8 or shorter than 4 fails the gate. A full chain down to 1×1 is therefore always excluded, so the enablement helps only truncated chains (for example a 4-mip 2048² image).

## Unaligned legal mips: correctness-first route (investigated, not implemented)
- **Hardware limit:**
  - The SKL PRM HiZ resolve rules require the rectangle to cover the full render target and to be 8×4 aligned.
  - `3DSTATE_WM_HZ_OP` requires Xmax ≤ Surface Width » LOD and Ymax ≤ Surface Height » LOD (quoted in blorp.c).
  - So a LOD > 0 whose minified extent is not 8×4 aligned cannot be ambiguated or resolved at all. The same rectangle rule applies to full resolves, which BLORP currently aligns up without an assertion.
  - Padding LOD0 is not a substitute. The Gen9 mip layout and the sampler's `textureSize`/edge behaviour derive from the programmed LOD0 width, so a padded surface would change the miptree geometry the application sees.
- **Required route:** real per-LOD HiZ state.
  - HiZ off in `3DSTATE_DEPTH_BUFFER` when rendering an unaligned LOD.
  - `transition_depth_buffer()` skips HZ ops on those LODs. It already loops per level.
  - `anv_can_hiz_clear_image()` refuses those LODs. It already takes a level.
  - Because the Gen9 sampler is understood to need HiZ on every LOD of a view (recalled from i965's `intel_miptree_sample_with_hiz`; not verifiable in this single-commit checkout), the sampled and transfer layouts of such images must map to no aux. That means a full resolve of all aligned LODs on every attachment → sampled/transfer transition and an ambiguate back.
- **Maintenance limit:** `anv_layout_to_aux_usage()` and `anv_layout_to_aux_state()` have no level parameter and 26 call sites (anv_blorp.c 5, anv_descriptor_set.c 1, anv_image.c 9, anv_image_view.c 3, genX_cmd_buffer.c 8). All would need auditing or a level-aware variant.
- **Performance risk:** the resolve traffic on every sampling transition could make such images slower than no HiZ for sample-heavy use. It would need its own pixel and timing validation.
