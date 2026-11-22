# ANV Gen9 (Kaby Lake GT2 / HD 620) source sweep

- Source: `/home/polarbear/.t3/projects/catchingup/mesa`, checkout at commit `0e9e108`. All paths below are relative to `mesa/`.
- Target: KBL GT2 (`INTEL_PLATFORM_KBL`, `GFX_VER` 9, `devinfo->ver == 9`).
- Read-only sweep. Nothing was built and no source file was modified.

Method and limits:
- Section 1 is version and `has_*` gates in `anv_physical_device.c` and `anv_video.c`, read in context. KBL devinfo values come from `src/intel/dev/intel_device_info.c`: `has_64bit_float` is inherited from `GFX8_FEATURES` (line 448); `has_lsc`, `has_ray_tracing`, `has_mesh_shading`, `has_bfloat16`, `has_aux_map` and `has_systolic` are not set in the GFX9 chain, so they default to false. Kernel-dependent flags (`has_userptr_uapi`, `has_mmap_offset`, `has_context_isolation`) are runtime values and are marked as such.
- Section 2 is pattern grep for `Wa_`, `WA`, `workaround`, `SKL`, `KBL`, `GFX_VER == 9`, `gfx9`, followed by reading the blocks. `intel_needs_workaround(..., N)` gates are not verified for KBL; the workaround table is outside `src/intel/vulkan`.
- Section 3 lists every TODO/FIXME/XXX/HACK in `.c`, `.h`, `.build`, `.py` files under `src/intel/vulkan` (tests excluded). A line is marked `(Gen9)` when it is not inside a preprocessor block that is false for `GFX_VER=9` (evaluated line by line; unknown conditions are kept), and it is not in `xe/`, RT, mesh, DGC, video-encode, HuC or AV1 files. Runtime `if` conditions are not evaluated, except where the note says so. `unverified` lines (no `(Gen9)` tag) are noted in the text.
- Section 4 is hand-picked comment lines that mention performance, stalls, flushes or optimization.

## 1. Features disabled or limited on Gen9

| Location | Quote | Note |
|---|---|---|
| src/intel/vulkan/anv_physical_device.c:369 | `if (VIDEO_CODEC_H265DEC && device->has_huc)` | H.265 decode advertised only when HuC is present. |
| src/intel/vulkan/anv_physical_device.c:371 | `if (VIDEO_CODEC_AV1DEC && device->info.ver >= 12)` | AV1 decode off on Gen9 (ver 12+ only). |
| src/intel/vulkan/anv_physical_device.c:380 | `if (VIDEO_CODEC_H265ENC && device->info.ver >= 12)` | HEVC encode off on Gen9 (ver 12+ only); encode block is also ANV_DEBUG-gated. |
| src/intel/vulkan/anv_physical_device.c:382 | `if (VIDEO_CODEC_AV1ENC && device->info.verx10 == 125)` | AV1 encode off on Gen9 (12.5 only). |
| src/intel/vulkan/anv_physical_device.c:402 | `const bool rt_enabled = ANV_SUPPORT_RT && device->info.has_ray_tracing;` | Ray tracing off on KBL: has_ray_tracing is unset in the GFX9 devinfo chain. |
| src/intel/vulkan/anv_physical_device.c:463 | `.KHR_fragment_shader_barycentric       = device->info.ver >= 20,` | Off on Gen9 (ver 20+). |
| src/intel/vulkan/anv_physical_device.c:464 | `.KHR_fragment_shading_rate             = device->info.ver >= 11,` | Off on Gen9 (needs ver 11+). |
| src/intel/vulkan/anv_physical_device.c:581 | `.EXT_conservative_rasterization        = device->info.platform != INTEL_PLATFORM_SKL,` | Exposed on KBL; only SKL is excluded, so Gen9 GT2 gets it. |
| src/intel/vulkan/anv_physical_device.c:604 | `.EXT_device_generated_commands         = device->info.verx10 >= 125 \|\| ANV_DEBUG(EXPERIMENTAL),` | Off on Gen9 unless ANV_DEBUG(EXPERIMENTAL); Gen9 DGC code exists in anv_dgc_set.c. |
| src/intel/vulkan/anv_physical_device.c:616 | `.EXT_external_memory_host              = device->info.has_userptr_uapi,` | Runtime KMD check (userptr UAPI), not version-gated. |
| src/intel/vulkan/anv_physical_device.c:638 | `.EXT_map_memory_placed                 = device->info.has_mmap_offset,` | Runtime KMD check (mmap offset), not version-gated. |
| src/intel/vulkan/anv_physical_device.c:642 | `.EXT_memory_budget                     = (!device->info.has_local_mem \|\|` | First clause is true on a Gen9 iGPU (no local memory); the rest of the condition was not checked. |
| src/intel/vulkan/anv_physical_device.c:645 | `.EXT_mesh_shader                       = device->info.has_mesh_shading,` | Off on KBL (has_mesh_shading unset in GFX9 chain). |
| src/intel/vulkan/anv_physical_device.c:712 | `.KHR_shader_bfloat16                   = device->info.has_bfloat16,` | Off on KBL (has_bfloat16 unset). |
| src/intel/vulkan/anv_physical_device.c:698 | `.AMD_texture_gather_bias_lod           = device->info.ver >= 20,` | Off on Gen9 (ver 20+). |
| src/intel/vulkan/anv_physical_device.c:752 | `.depthBounds                              = pdevice->info.ver >= 12,` | Off on Gen9 (ver 12+ only). |
| src/intel/vulkan/anv_physical_device.c:965 | `.rectangularLines = pdevice->info.ver >= 10,` | Off on Gen9; the comment says rectangular lines need the strict algorithm, unsupported before ICL. |
| src/intel/vulkan/anv_physical_device.c:975 | `.smoothLines = pdevice->info.ver < 10,` | On for Gen9 (ver < 10). |
| src/intel/vulkan/anv_physical_device.c:759 | `.textureCompressionASTC_LDR               = pdevice->has_astc_ldr \|\|` | Depends on native ASTC LDR or emulation (emu_astc_ldr); Gen9 is the emulation case. |
| src/intel/vulkan/anv_physical_device.c:782 | `.shaderFloat64                            = pdevice->info.has_64bit_float \|\|` | On for KBL (has_64bit_float inherited from GFX8 devinfo). |
| src/intel/vulkan/anv_physical_device.c:1377 | `.shaderFmaFloat64 = pdevice->info.has_64bit_float &&` | On on Gen9 unless INTEL_DEBUG soft64 is set. |
| src/intel/vulkan/anv_physical_device.c:1038 | `.shaderBufferFloat32AtomicAdd =  pdevice->info.has_lsc,` | Off on KBL: needs has_lsc, which is unset for GFX9. |
| src/intel/vulkan/anv_physical_device.c:1040 | `pdevice->info.has_64bit_float && pdevice->info.has_lsc,` | Off on KBL: needs has_lsc (shaderBufferFloat64AtomicAdd). |
| src/intel/vulkan/anv_physical_device.c:1057 | `pdevice->info.has_64bit_float && pdevice->info.has_lsc &&` | Off on KBL: needs has_lsc and ver < 20 (fp64 min/max atomics). |
| src/intel/vulkan/anv_physical_device.c:1052 | `.shaderBufferFloat16Atomics      = pdevice->info.has_lsc,` | Off on KBL: needs LSC. |
| src/intel/vulkan/anv_physical_device.c:1059 | `.shaderSharedFloat16Atomics      = pdevice->info.has_lsc,` | Off on KBL: needs LSC. |
| src/intel/vulkan/anv_physical_device.c:1289 | `.shaderBFloat16Type = pdevice->info.has_bfloat16,` | Off on KBL (bfloat16 unset). |
| src/intel/vulkan/anv_physical_device.c:1326 | `.indirectMemoryToImageCopy = pdevice->info.verx10 >= 125,` | Off on Gen9 (12.5+ only). |
| src/intel/vulkan/anv_physical_device.c:1586 | `p->integerDotProduct4x8BitPackedUnsignedAccelerated = pdevice->info.ver >= 12;` | Off on Gen9 (ver 12+ hardware path). |
| src/intel/vulkan/anv_physical_device.c:1601 | `p->integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated = pdevice->info.ver >= 12;` | Off on Gen9 (ver 12+ only). |
| src/intel/vulkan/anv_physical_device.c:795 | `.sparseResidency8Samples                  = has_sparse_or_fake &&` | Set only if sparse is supported (sparse_type); not version-gated except 12.5. |
| src/intel/vulkan/anv_physical_device.c:1814 | `.sparseResidencyStandard2DMultisampleBlockShape = has_sparse_or_fake &&` | Reported on Gen9 when sparse is supported (ver < 20). |
| src/intel/vulkan/anv_physical_device.c:1852 | `props->meshAndTaskShaderDerivatives = pdevice->info.has_mesh_shading;` | Off on KBL (mesh unset). |
| src/intel/vulkan/anv_physical_device.c:1881 | `props->fragmentShadingRateWithShaderSampleMask = devinfo->verx10 >= 200;` | Off on Gen9 (ver 20+). |
| src/intel/vulkan/anv_physical_device.c:3030 | `.queueCount = pdevice->info.ver == 9 ? MIN2(1, v_count) : v_count,` | Gen9 video family capped at one queue. |
| src/intel/vulkan/anv_physical_device.c:3120 | `if (devinfo.ver < 9) {` | Gen8 and older rejected here; Gen9 passes this check. |
| src/intel/vulkan/anv_physical_device.c:3198 | `device->has_protected_contexts = device->info.ver >= 12 &&` | Off on Gen9 (ver 12+ required). |
| src/intel/vulkan/anv_physical_device.c:3209 | `if (devinfo.ver == 9 && !intel_device_info_is_9lp(&devinfo)) {` | KBL (non-9LP Gen9) enables the ASTC void-extent denorm flush when has_astc_ldr && !emu. |
| src/intel/vulkan/anv_physical_device.c:3263 | `device->has_cooperative_matrix =` | Off on Gen9 (needs systolic hardware). |
| src/intel/vulkan/anv_physical_device.c:3270 | `device->has_compression_control = device->info.ver < 20;` | On for Gen9 (ver < 20). |
| src/intel/vulkan/anv_physical_device.c:3285 | `device->uses_efficient_64bit =` | Off on Gen9 (ver 35+ only). |
| src/intel/vulkan/anv_physical_device.c:3289 | `device->alloc_aux_tt_mem =` | Off on Gen9 (aux map and 12.5+). |
| src/intel/vulkan/anv_physical_device.c:3311 | `device->has_scratch_page =` | Scratch page kept on for Gen9 (ver < 20). |
| src/intel/vulkan/anv_physical_device.c:2968 | `if (pdevice->info.verx10 >= 125 && can_use_non_render_engines) {` | Non-render engine queues gated to 12.5+; Gen9 does not get them. |
| src/intel/vulkan/anv_physical_device.c:3142 | `if (!devinfo.has_context_isolation) {` | Context isolation is required; device init fails if the kernel does not report it (runtime). |
| src/intel/vulkan/anv_physical_device.c:3469 | `.timestampValidBits = device->info.ver >= 20 ? 64 : 36,` | Gen9 reports 36 timestamp valid bits. |
| src/intel/vulkan/anv_video.c:252 | `pdevice->info.platform <= INTEL_PLATFORM_SKL)` | Range-extension H.265 check covers SKL and older only; KBL is later in the enum, so it is not caught. |
| src/intel/vulkan/anv_video.c:259 | `pdevice->info.ver <= 10)` | Range-extension H.265 profiles rejected on ver <= 10, so on Gen9. |

## 2. Gen9-specific workarounds

| Location | Quote | Note |
|---|---|---|
| src/intel/vulkan/genX_cmd_buffer.c:2257 | `if (GFX_VER == 9 && current_pipeline == GPGPU)` | SKL LRI post-sync rule: adds a CS stall before post-sync ops in GPGPU mode. |
| src/intel/vulkan/genX_cmd_buffer.c:2330 | `if (GFX_VER == 9 && (bits & ANV_PIPE_VF_CACHE_INVALIDATE_BIT)) {` | Gen9 VF-invalidate uses WriteImmediateData to the workaround BO for the post-sync write. |
| src/intel/vulkan/genX_cmd_buffer.c:2413 | `if (GFX_VER == 9 &&` | Gen9: a VF invalidate plus CS stall resets the software VF cache tracking. |
| src/intel/vulkan/genX_cmd_buffer.c:3169 | `if (GFX_VER == 9 && (bits & ANV_PIPE_VF_CACHE_INVALIDATE_BIT))` | Gen9 emits a null PIPE_CONTROL before each VF cache invalidate (SKL PRM rule). |
| src/intel/vulkan/genX_cmd_buffer.c:3167 | `* This appears to hang Broadwell, so we restrict it to just gfx9.` | Reason the null PIPE_CONTROL is Gen9-only. |
| src/intel/vulkan/genX_cmd_buffer.c:4613 | `if (GFX_VER == 9) {` | Secondary command buffers force a full VF-cache invalidate on Gen9 because they are not tracked. |
| src/intel/vulkan/genX_cmd_buffer.c:4833 | `/* Indirect draw commands on Gfx9 also set gl_BaseVertex &` | Gen9 indirect draws invalidate the VF cache because base vertex/index come through a VB. |
| src/intel/vulkan/genX_cmd_buffer.c:5842 | `*   Software must clear the COLOR_CALC_STATE Valid field in` | Gen9 emits 3DSTATE_CC_STATE_POINTERS before selecting GPGPU (BDW/SKL doc rule). |
| src/intel/vulkan/genX_cmd_buffer.c:5957 | `/* There is a mid-object preemption workaround which requires you to` | Gen9 re-emits MEDIA_VFE_STATE when switching GPGPU to 3D; the comment says the root cause is unknown. |
| src/intel/vulkan/genX_cmd_buffer.c:5990 | `if (devinfo->platform == INTEL_PLATFORM_GLK) {` | GLK-only barrier chicken bit; KBL does not take this branch. |
| src/intel/vulkan/genX_cmd_buffer.c:6007 | `/* Undocumented workaround, we need to reemit MEDIA_CURBE_LOAD on Gfx9 when` | Gen9 re-emits MEDIA_CURBE_LOAD on 3D-to-GPGPU switch to avoid corrupted push constants. |
| src/intel/vulkan/genX_cmd_buffer.c:6106 | `if (anv_gfx8_9_vb_cache_range_needs_workaround(bound, dirty,` | Gen9 VB-cache workaround: stall and VF invalidate when a bound VB range was written. |
| src/intel/vulkan/genX_cmd_buffer.c:6172 | `/* Because all Gfx9 platforms with more than one slice require` | Gen9 slice-hashing table for multi-slice (GT3/GT4) parts. |
| src/intel/vulkan/genX_cmd_buffer.c:5166 | `/* Using this bit on Gfx9 triggers a GPU hang.` | Gen9 hang caution: this bit stays behind GFX_VER >= 12. |
| src/intel/vulkan/genX_cmd_buffer.c:1402 | `genX(cmd_buffer_update_color_aux_op)(cmd_buffer, GFX_VER == 9 ?` | Gen9 uses SW_AMBIGUATE as the aux op for CCS transitions to no aux. |
| src/intel/vulkan/genX_cmd_draw.c:809 | `genX(cmd_buffer_set_binding_for_gfx8_vb_flush)(cmd_buffer, vb,` | Gen9 records each vertex-buffer range for the VB-cache workaround on every VB emit. |
| src/intel/vulkan/genX_cmd_draw.c:1175 | `if (intel_needs_workaround(cmd_buffer->device->info, 16011107343) &&` | Wa_16011107343 gate; Gen9 hits it only if the device workaround table enables it (not checked here). |
| src/intel/vulkan/genX_cmd_draw.c:1250 | `if (intel_needs_workaround(device->info, 22014412737) &&` | Wa_22014412737 gate; Gen9 applicability depends on the workaround table (not checked here). |
| src/intel/vulkan/genX_gpu_memcpy.c:413 | `if (GFX_VER == 9 &&` | Gen9 memcpy path: CS stall plus VF invalidate before copying from VB-bound memory. |
| src/intel/vulkan/genX_simple_shader.c:206 | `ps.BindingTableEntryCount = GFX_VER == 9 ? 1 : 0;` | Gen9 simple-shader PS uses a one-entry binding table. |
| src/intel/vulkan/genX_cmd_draw_generated_indirect.h:281 | `#if GFX_VER == 9` | Gen9-only indirect-draw branch (DGC is not exposed on Gen9). |
| src/intel/vulkan/genX_cmd_dgc.c:970 | `#if GFX_VER == 9` | Gen9-only DGC branch; EXT_device_generated_commands is off on Gen9. |
| src/intel/vulkan/anv_dgc_set.c:89 | `if (device->info->ver == 9) {` | Gen9 DGC draw-parameter flags (DGC is off on Gen9 by default). |
| src/intel/vulkan/anv_internal_kernels.c:510 | `.send_count =  (device->info->ver == 9 ?` | Gen9 internal indirect-draw kernel uses a different send count. |
| src/intel/vulkan/anv_types.h:80 | `/* 3DSTATE_VERTEX_BUFFER supports 33 VBs, but these limits are applied on Gen9` | Gen9 reserves two VB slots for SGVs, so the usable VB count is lower than 33. |
| src/intel/vulkan/anv_nir_lower_descriptor_heap.c:308 | `/* SKL PRMs, Volume 2d: Command Reference: Structures, RENDER_SURFACE_STATE` | Descriptor-heap lowering follows the SKL RENDER_SURFACE_STATE layout (Gen9 is the reference). |
| src/intel/vulkan/anv_allocator.c:407 | `* in the GTT.  There are two documented workarounds for state base address` | BO placement workarounds for state base address; applies to all gens including Gen9. |

## 3. TODO / FIXME / XXX / HACK comments

| Location | Quote | Note |
|---|---|---|
| src/intel/vulkan/anv_allocator.c:1436 | `return NULL; /* TODO */` | (Gen9) BO allocation error is turned into NULL instead of a VkResult, so the reason is lost. |
| src/intel/vulkan/anv_astc_emu.c:29 | `/* XXX we only need 2D but the shader expects 2D_ARRAY */` | (Gen9) ASTC emulation shader is built as 2D_ARRAY though only 2D is needed; Gen9 uses emulation. |
| src/intel/vulkan/anv_batch_chain.c:1164 | `return; /* FIXME */` | (Gen9) Batch clone failure returns with the error set and a FIXME; cleanup path is unfinished. |
| src/intel/vulkan/anv_blorp.c:664 | `/* TODO: HSD 14017185931 recommends decompressing the MCS before` | Gen9 applicability unverified. Mentions HSD 14017185931 MCS decompress before a copy; Gen9 applicability unverified. |
| src/intel/vulkan/anv_descriptor_set.c:1831 | `* TODO: we could optimizer further, try to keep a copy of the old data on` | (Gen9) Descriptor update could keep old data to optimize copies; perf item. |
| src/intel/vulkan/anv_device.c:932 | `/* XXX(chadv): Can we dup() physicalDevice->fd here? */` | (Gen9) Question whether the physical device fd can be dup()ed; no change needed unless used. |
| src/intel/vulkan/anv_device.c:1214 | `/* TODO(RT): Do we want some sort of data structure for this? */` | Data structure for RT tracking is undecided; ray-tracing only. |
| src/intel/vulkan/anv_device.c:1892 | `/* TODO: Android, ChromeOS and other applications may need another way to` | (Gen9) Android/ChromeOS apps may need another device-selection path; not a Gen9 code change. |
| src/intel/vulkan/anv_dgc_layout.c:210 | `(1 /* TODO: _3DSTATE_VERTEX_BUFFERS_length(devinfo) */ +` | VB length hardcoded to 1 in DGC layout; DGC is not exposed on Gen9. |
| src/intel/vulkan/anv_dgc_layout.c:224 | `(1 /* TODO: _3DSTATE_VERTEX_BUFFERS_length(devinfo) */ +` | Same hardcoded VB length in DGC layout; DGC not exposed on Gen9. |
| src/intel/vulkan/anv_dgc_layout.c:523 | `(1 /* TODO: _3DSTATE_VERTEX_BUFFERS_length(devinfo) */ +` | Same hardcoded VB length in DGC layout; DGC not exposed on Gen9. |
| src/intel/vulkan/anv_dgc_layout.c:544 | `4 /* TODO: _3DSTATE_VERTEX_BUFFERS_length(devinfo) */);` | Same hardcoded VB length in DGC layout; DGC not exposed on Gen9. |
| src/intel/vulkan/anv_formats.c:437 | `/* TODO: it is possible to enable the following 2 formats, but that` | (Gen9) Two 4:2:2 multiplanar formats could be enabled but need a multiplanar refactor. |
| src/intel/vulkan/anv_formats.c:643 | `/* TODO: implement support for this in the copy shader. */` | (Gen9) Copy shader lacks support for this case. |
| src/intel/vulkan/anv_formats.c:647 | `/* TODO: we use compute for indirect copies, and compute cannot write HiZ,` | (Gen9) Compute copies cannot write HiZ; indirect-copy limitation. |
| src/intel/vulkan/anv_formats.c:902 | `* TODO: check if we could enable HOST copies in that non emulated case.` | (Gen9) Host copies may be possible in the non-emulated case. |
| src/intel/vulkan/anv_formats.c:994 | `/* XXX: We handle 3-channel formats by switching them out for RGBX or` | (Gen9) Three-channel formats are swapped to 4-channel forms internally; common path. |
| src/intel/vulkan/anv_formats.c:1060 | `/* TODO: We've disabled linear implicit reconstruction with the` | (Gen9) Linear implicit chroma reconstruction disabled because sampled values go slightly out of range. |
| src/intel/vulkan/anv_formats.c:2277 | `* TODO: Determine if the loss of Yf/Ys/Tile64 translates to a` | (Gen9) Unclear whether losing Yf/Ys/Tile64 affects modifier support. |
| src/intel/vulkan/anv_image.c:718 | `* TODO: This is a heuristic trade-off; we haven't tuned it at all.` | (Gen9) Image-size heuristic trade-off was never tuned. |
| src/intel/vulkan/anv_image.c:1101 | `/* TODO: Should return` | (Gen9) Modifier plane-layout error should return a specific VkResult, not OOM. |
| src/intel/vulkan/anv_image.c:3491 | `/* TODO(chadv): Drop this workaround when WSI gets fixed. */` | (Gen9) WSI workaround for an unfixed WSI issue; common path. |
| src/intel/vulkan/anv_image.c:3836 | `* TODO: Should we be disabling this in more cases?` | (Gen9) Open question whether this feature should be disabled in more cases. |
| src/intel/vulkan/anv_image.c:4437 | `* XXX: Does the framework allow us to view UNDEFINED layouts which will` | (Gen9) Open question whether UNDEFINED layout views are allowed; common image-layout code. |
| src/intel/vulkan/anv_image.c:4443 | `* TODO: We could set the` | (Gen9) Layout-to-compression setting unfinished; common path. |
| src/intel/vulkan/anv_image_host_copy.c:226 | `/* TODO: Get rid of this.` | (Gen9) Host image copy helper marked for removal; common code. |
| src/intel/vulkan/anv_nir_compute_push_layout.c:752 | `* XXX - Use workaround infrastructure and final workaround when provided` | Gen9 applicability unverified. Null PS push payload is hardcoded for TBIMR; should use the workaround framework (platform gating unverified). |
| src/intel/vulkan/anv_nir_lower_multiview.c:313 | `/* TODO: Figure out why we are not getting same benefits for larger than` | Gen9 applicability unverified. Primitive replication helps only 2 views; reason for the gap unknown. |
| src/intel/vulkan/anv_nir_lower_multiview.c:325 | `/* TODO: We should be able to support replication at 'geometry' stages` | Gen9 applicability unverified. Replication at geometry stages not supported. |
| src/intel/vulkan/anv_nir_push_constants_analysis.c:155 | `/* TODO: should we count uses in loops as higher benefit? */` | (Gen9) Push-constant benefit does not weight uses inside loops; affects Gen9 shader push layout. |
| src/intel/vulkan/anv_nir_push_constants_analysis.c:209 | `* TODO: consider swizzle` | (Gen9) Push analysis ignores swizzles, so some promotable constants may be missed. |
| src/intel/vulkan/anv_nir_push_constants_analysis.c:396 | `/* TODO: Consider combining ranges.` | (Gen9) Adjacent push ranges are not merged; minor push inefficiency. |
| src/intel/vulkan/anv_physical_device.c:892 | `.accelerationStructureIndirectBuild = false, /* TODO */` | accelerationStructureIndirectBuild hardcoded false; ray-tracing only. |
| src/intel/vulkan/anv_physical_device.c:1970 | `/* TODO */` | Ray-tracing shader group properties are placeholders; RT only. |
| src/intel/vulkan/anv_physical_device.c:2154 | `/* TODO: fixup Wa_18019110168 */` | Task/mesh stage exposure ignores Wa_18019110168; mesh only. |
| src/intel/vulkan/anv_physical_device.c:3002 | `/* TODO: CCS don't support MI_SET_APPID instruction, that might be` | CCS engine lacks MI_SET_APPID; CCS is Gen12+ only. |
| src/intel/vulkan/anv_physical_device.c:3027 | `/* TODO: enable protected content on video queue */` | Protected content on the video queue not enabled; Gen9 has no protected contexts. |
| src/intel/vulkan/anv_physical_device.c:3885 | `/* TODO: When we enable flexible dimensions, fill this properly. */` | Cooperative matrix flexible dimensions unfilled; systolic only. |
| src/intel/vulkan/anv_rmv.c:89 | `/* TODO: */` | (Gen9) Empty TODO in RMV tracing; no behaviour change. |
| src/intel/vulkan/anv_rmv.c:399 | `false /* internal */, true /* TODO: vram */,` | (Gen9) RMV heap domain hardcoded as VRAM for internal allocs; Only active with RMV tracing. |
| src/intel/vulkan/anv_rmv.c:481 | `false /* internal */, true /* TODO: vram */,` | (Gen9) Same hardcoded VRAM domain in RMV; Only active with RMV tracing. |
| src/intel/vulkan/anv_rmv.c:584 | `.preferred_domain                 = VK_RMV_KERNEL_MEMORY_DOMAIN_GTT /* TODO */,` | (Gen9) Preferred RMV domain fixed to GTT; Only active with RMV tracing. |
| src/intel/vulkan/anv_shader.c:683 | `/* TODO: deal with protected scratch surfaces in efficient 64bit mode` | Protected scratch in efficient-64-bit mode; that mode is ver 35+ only. |
| src/intel/vulkan/anv_shader_compile.c:347 | `/* TODO: Consider changing this to an address format that has the NULL` | (Gen9) NULL pointer address format is an open choice; affects shader address lowering on all gens. |
| src/intel/vulkan/anv_shader_compile.c:661 | `/* TODO: We should make this dynamic */` | (Gen9) Sample-mask OpenGL behaviour is a compile-time key; should be dynamic. |
| src/intel/vulkan/anv_shader_compile.c:2037 | `/* TODO: Once jay_compile_params has all we need, stop using` | Gen9 applicability unverified. Jay compile parameters are incomplete; matters only where Jay is used on Gen9. |
| src/intel/vulkan/anv_shader_compile.c:2567 | `/* TODO: Write the feedback index into the pipeline */` | Dead code under #if 0: pipeline feedback index is never written. |
| src/intel/vulkan/anv_sparse.c:662 | `* TODO: be fully spec-compliant here. Maybe have a device-internal queue` | Gen9 applicability unverified. Sparse binding is not fully spec-compliant; Gen9 sparse support not established here. |
| src/intel/vulkan/anv_sparse.c:668 | `fprintf(stderr, "FIXME: application has created a sparse resource "` | Gen9 applicability unverified. Sparse resource creation message; Gen9 sparse support not established here. |
| src/intel/vulkan/anv_sparse.c:1202 | `/* TODO: dEQP seems to care about the block shapes being standard even for` | Gen9 applicability unverified. Standard block-shape matching for dEQP; sparse path. |
| src/intel/vulkan/anv_sparse.c:1686 | `* TODO: add support for these formats.` | Gen9 applicability unverified. Some sparse formats unsupported; sparse path. |
| src/intel/vulkan/anv_utrace.c:195 | `* TODO: add INTEL_ENGINE_CLASS_COPY support (should be trivial ;)` | (Gen9) Copy-engine class not handled in utrace timestamping; applies to Gen9 copy queues when traced. |
| src/intel/vulkan/anv_video.c:845 | `/* TODO. these sizes can be determined dynamically depending on ctb sizes of each slice. */` | Video buffer sizing should be dynamic per slice; video path. |
| src/intel/vulkan/anv_video.c:1248 | `uint32_t max_tile_cols = 16; /* TODO. get the profile to work this out */` | Max tile columns hardcoded to 16 for the AV1 path; video only. |
| src/intel/vulkan/anv_video.c:1300 | `buffer_size = max_tile_cols * 8; /* TODO. take from profile */` | Tile buffer size should come from the profile; video only. |
| src/intel/vulkan/anv_video.c:1665 | `/* TODO: tune preferred constant QIndex for AV1 */` | AV1 constant QIndex not tuned; video encode only. |
| src/intel/vulkan/genX_acceleration_structure.c:591 | `/* TODO: Current encode.comp spilling a lot. Once we flip the swtich to Jay,` | AS encode shader spills heavily; ray-tracing only. |
| src/intel/vulkan/genX_acceleration_structure.c:676 | `/* TODO: ANV does not yet have support for AS updates without doing a full` | AS update without a full rebuild is unsupported; ray-tracing only. |
| src/intel/vulkan/genX_cmd_buffer.c:394 | `* XXX:  As far as we have been able to determine through` | (Gen9) Texture-cache invalidation as the binding-table fix is an unconfirmed theory. |
| src/intel/vulkan/genX_cmd_buffer.c:2023 | `* XXX - use WA framework with Wa_14026570320.` | Ad-hoc RT workaround outside the WA framework; ray-tracing only. |
| src/intel/vulkan/genX_cmd_buffer.c:2141 | `* XXX: According to spec this should not be a concern for a regular` | Inside a Gfx12-only block: VF cache invalidation is ignored in GPGPU mode on RCS, so invalidations can be missed. |
| src/intel/vulkan/genX_cmd_buffer.c:3058 | `/* XXX - insert all workarounds and GFX specific things below. */` | (Gen9) Authors say workarounds are still missing from this pipe-control emission path. |
| src/intel/vulkan/genX_cmd_buffer.c:3093 | `* TODO: Check if it's the same case with signal on event completion. (i.e` | (Gen9) Unverified whether the timestamp ordering rule also applies to event signal. |
| src/intel/vulkan/genX_cmd_buffer.c:4602 | `* TODO: do an ultimate version of this by diffing secondary/container` | (Gen9) Secondary-command-buffer state emission could diff against the container instead of re-emitting. |
| src/intel/vulkan/genX_cmd_buffer.c:5168 | `* TODO: check Gfx11` | Bit is Gfx12+ only because it hangs Gen9; Gfx11 status unknown. |
| src/intel/vulkan/genX_cmd_buffer.c:5357 | `/* XXX: Right now, we're really dumb and just flush whatever categories` | (Gen9) Barrier translation flushes every requested category; coarse, likely over-flushes. |
| src/intel/vulkan/genX_cmd_compute.c:184 | `* TODO: optimize this on Gfx12.5+ where the shader is not using per` | (Gen9) Comment limits the push-constant reallocation optimization to Gfx12.5+; Gen9 forces reallocation on every pipeline change. |
| src/intel/vulkan/genX_cmd_compute.c:1448 | `/* TODO: We can tune this value specific to apps. */` | Ray-query tuning for Gfx20; not Gen9. |
| src/intel/vulkan/genX_cmd_compute.c:1467 | `* TODO(RT): This is a bit of a hack` | RT hack in BTD setup; ray-tracing only. |
| src/intel/vulkan/genX_cmd_draw.c:1955 | `/* TODO: We need to stomp base vertex to 0 somehow */` | (Gen9) Base-vertex handling for indirect draws is unfinished; this is in the pre-gfx11 draw path. |
| src/intel/vulkan/genX_cmd_draw.c:2564 | `/* TODO(mesh): Check if this is not emitting more packets than we need. */` | Mesh packet count not checked; mesh is not on KBL. |
| src/intel/vulkan/genX_cmd_draw_generated_indirect.h:345 | `* TODO: we might have to split that if the amount of space is to large (at` | DGC indirect buffer may need splitting when large; DGC is not exposed on Gen9. |
| src/intel/vulkan/genX_cmd_video.c:2223 | `pic.SegmentMapisZeroFlag = 0; /* TODO */` | AV1 segment-map flag hardcoded to 0; video only. |
| src/intel/vulkan/genX_cmd_video.c:2224 | `pic.SegmentIDBufferStreamInEnableFlag = 0; /* TODO */` | AV1 segment-ID stream-in flag hardcoded to 0; video only. |
| src/intel/vulkan/genX_cmd_video.c:2225 | `pic.SegmentIDBufferStreamOutEnableFlag = 0; /* TODO */` | AV1 segment-ID stream-out flag hardcoded to 0; video only. |
| src/intel/vulkan/genX_cmd_video.c:2437 | `/* TODO. handling negative values?` | Negative-value handling undecided in the video path. |
| src/intel/vulkan/genX_cmd_video_enc.c:159 | `/* TODO. LoadMvCost/LoadHmeMvCost equivalents (AVC_MV_Cost tables) are not` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:646 | `/* TODO: Needs to read a dedicated all-intra colocated buffer when L1[0] is an I picture. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:1173 | `* TODO: a P-frame in a B GOP uses type 1 instead of 2, which needs the GOP's B-frame count` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:1271 | `* TODO: the rest, intra-refresh / A-stepping / Wa_18011246551 / stream-in` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:1422 | `/* TODO. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:1451 | `/* TODO. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:1511 | `/* TODO. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:1536 | `/* TODO. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:1667 | `/* TODO. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:1674 | `/* TODO. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:1749 | `/* TODO. Available only when RateControlCounterEnable is true. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:1758 | `/* TODO. Needs to get a different value of rounding inter under various conditions. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:2556 | `/* FIXME */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:2591 | `/* FIXME */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:2728 | `/* TODO. add DSFWDREF and FWDREF */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:2824 | `/* TODO: Currently ANV emits only I or low delay B,` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:3086 | `* TODO: Target usage from the quality level;` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:3106 | `/* TODO: palette, current picture reference, and RDOQ are not handled. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:3118 | `/* TODO Hierarchy-dependent rounding offsets. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:3166 | `/* TODO: target usage is fixed to 4 ("Normal mode"). */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:3169 | `/* TODO Hierarchy-dependent rounding offsets. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:3661 | `/* TODO: Different setting according to the target usage;` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:3845 | `/* TODO: weighted prediction is not included into the BRC SLB yet */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:4326 | `vdenc_pipe_mode.StandardSelect = 3; /* TODO: SS_AV1 */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:4985 | `* TODO: per-ref dims/scale factors for references of other sizes. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:5094 | `* TODO: SavedOrderHints + ActiveReferenceBitmask for use_ref_frame_mvs. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:5118 | `/* TODO: per-segment features when segmentation_enabled */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:5214 | `/* TODO: super-res and loop-restoration unit size when those tools are enabled */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:5540 | `* TODO: per-segment QP from pSegmentation. */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:5614 | `* TODO: adaptiveRounding derives param_inter from the previous frame` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:5768 | `/* TODO: AV1 VDEnc encoding is only validated on DG2 (gfx 12.5) for now */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_cmd_video_enc.c:5835 | `/* TODO: confirm AV1/VDEnc power well control bits */` | Video encode/AV1/HuC-BRC TODO; this path runs on Gen9 only under ANV_DEBUG(VIDEO_ENCODE) or not at all. |
| src/intel/vulkan/genX_gfx_state.c:1115 | `/* TODO: We should only require this when the last geometry shader uses a` | Coarse-pixel flush requirement; Gfx12.5+ only. |
| src/intel/vulkan/genX_gfx_state.c:4093 | `/* TODO: we can optimize this flush in the following cases:` | Flush optimization for geometry-shader rate changes; Gfx12+ only. |
| src/intel/vulkan/genX_huc_brc.h:446 | `/* TODO: proper profile/level based max frame size` | Max frame size by profile/level not derived; video encode only. |
| src/intel/vulkan/genX_huc_brc.h:1325 | `/* TODO: derive the profile and level based maximum frame size */` | Max frame size by profile/level not derived; video encode only. |
| src/intel/vulkan/genX_huc_brc.h:1461 | `/* TODO: derive from the level */` | Level-derived value not implemented; video encode only. |
| src/intel/vulkan/genX_huc_brc.h:1481 | `/* TODO: verify the softlet only fields against a DMEM dump */` | Soft-let fields not verified against a DMEM dump; video encode only. |
| src/intel/vulkan/genX_huc_brc.h:2654 | `/* TODO: drop once the tile group obu_size position is computed on the GPU */` | AV1 tile-group OBU size computed on the CPU; video only. |
| src/intel/vulkan/genX_huc_brc.h:2721 | `/* TODO: patch the tile group obu_size at a GPU computed position instead */` | AV1 tile-group OBU size patched at a CPU-computed position; video only. |
| src/intel/vulkan/genX_query.c:1532 | `/* TODO: This might only be necessary for certain stats */` | (Gen9) Pipeline-statistics query flush may only be needed for some statistics; Gen9 runs it for all. |
| src/intel/vulkan/genX_query.c:1790 | `/* TODO: This might only be necessary for certain stats */` | (Gen9) Same over-broad flush for pipeline-statistics queries as the earlier site. |
| src/intel/vulkan/genX_shader.c:352 | `* TODO: Compact vertex elements so we never end up with holes.` | (Gen9) Vertex elements not compacted, leaving holes in the VS input layout. |
| src/intel/vulkan/genX_shader.c:1052 | `/* XXX - No major improvement observed from enabling` | Overlapping subspans gave no gain on the Gfx12 path; Gfx12 only. |
| src/intel/vulkan/genX_shader.c:1304 | `/* TODO: Check if we are missing workarounds and enable mid-thread` | Mid-thread preemption and missing-workaround check; Gfx12+ path. |
| src/intel/vulkan/genX_simple_shader.c:801 | `/* TODO: Check if we are missing workarounds and enable mid-thread` | Mid-thread preemption and missing-workaround check; Gfx12+ path. |
| src/intel/vulkan/genX_simple_shader.c:843 | `/* TODO: switch to use INTEL_NEEDS_WA_14025112257 */` | Gen9 applicability unverified. Wa_14025112257 is still a compile-time define; applicability to Gen9 unverified. |
| src/intel/vulkan/i915/anv_gem.c:38 | `/* FIXME: On discrete platforms we don't have DRM_IOCTL_I915_GEM_GET_TILING` | (Gen9) GET_TILING ioctl missing on discrete GPUs; only Android uses it. Gen9 is iGPU. |
| src/intel/vulkan/xe/anv_device.c:141 | `* TODO: But with current UAPI we can't change the mmap mode in Xe, so` | Xe UAPI cannot change the mmap mode; Xe KMD only, not Gen9. |

## 4. Performance notes

| Location | Quote | Note |
|---|---|---|
| src/intel/vulkan/genX_cmd_draw.c:501 | `* Always reallocate on gfx9, gfx11 to fix push constant related flaky tests.` | Forces push-constant reallocation on every gfx9 draw; a per-draw cost kept for flaky tests (issue 11064). |
| src/intel/vulkan/genX_cmd_buffer.c:5959 | `* even without preemption, we have issues with geometry flickering when` | Unexplained MEDIA_VFE_STATE re-emit on every GPGPU-to-3D switch on Gen9; a stall candidate. |
| src/intel/vulkan/genX_cmd_buffer.c:3167 | `* This appears to hang Broadwell, so we restrict it to just gfx9.` | Extra null PIPE_CONTROL before every VF cache invalidate on Gen9; a per-barrier cost. |
| src/intel/vulkan/genX_cmd_buffer.c:5357 | `/* XXX: Right now, we're really dumb and just flush whatever categories` | Barrier translation flushes every requested category; coarse, so it over-flushes. |
| src/intel/vulkan/genX_cmd_buffer.c:947 | `* In testing, SKL doesn't actually seem to need this, but HSW does.` | Comment says SKL does not need the state-cache invalidate after clear-color surface updates; check if still emitted on Gen9. |
| src/intel/vulkan/genX_cmd_buffer.c:1565 | `* concurrent rendering before the stall completes.  This intentionally` | Extra stalling flush accepted by design on RO invalidation; known cost. |
| src/intel/vulkan/genX_cmd_buffer.c:1580 | `/* Now send a third stalling flush to make sure that invalidation is` | Third stalling flush on the RO invalidation path; adds cost to that barrier. |
| src/intel/vulkan/genX_cmd_buffer.c:1550 | `* which involves a first PIPE_CONTROL flush which stalls the pipeline...` | Documents a stalling flush sequence used for RO invalidation. |
| src/intel/vulkan/genX_cmd_buffer.c:6987 | `* may not be true and cause excessive flushing.  We can trivially skip it` | Comment calls the BeginRendering flush excessive; this block is Gfx11+ only. |
| src/intel/vulkan/genX_cmd_buffer.c:3915 | `* better performance, don't replace the current fast clear operation` | Keeps fast-clear state instead of ambiguating, for performance (comment); deliberate choice. |
| src/intel/vulkan/genX_cmd_buffer.c:3871 | `* We perform a flush of the write cache before and after the clear and` | Fast-clear path flushes before and after the clear; cost on every clear. |
| src/intel/vulkan/genX_cmd_buffer.c:1388 | `* image.  Doing so in a performant way requires more detailed aux state` | Aux ambiguate is coarse; finer aux-state tracking would avoid work. |
| src/intel/vulkan/genX_cmd_buffer.c:6380 | `* avoid stalling for better performance.` | Deliberate choice to avoid a stall for performance; reference for pipeline-switch cost. |
| src/intel/vulkan/genX_cmd_buffer.c:7171 | `* bit inefficient but, since HiZ resolves aren't destructive, going` | HiZ resolve handled with a conservative and somewhat inefficient flush. |
| src/intel/vulkan/genX_cmd_draw.c:1210 | `* performance by avoiding HiZ planes for draw calls which compute depth` | Inside a Gfx12+ block (#if GFX_VER >= 12); the i915 branch disables extra HiZ planes for depth-computing shaders. |
| src/intel/vulkan/genX_cmd_compute.c:1549 | `* dispatch will be a little slower than it has to be.` | Compute dispatch acknowledged as slower than necessary; applicability to Gen9 depends on the gfx path. |
| src/intel/vulkan/genX_gfx_state.c:4093 | `/* TODO: we can optimize this flush in the following cases:` | Flush optimization for coarse-pixel path; Gfx12+ only. |
| src/intel/vulkan/genX_shader.c:1052 | `/* XXX - No major improvement observed from enabling` | Overlapping subspans gave no gain on the Gfx12 dual-SIMD8 path; Gfx12 only. |
| src/intel/vulkan/anv_allocator.c:495 | `* On MTL(Xe KMD only)/LNL platforms, there is a large perf penalty from` | The 64K-page memory trade-off is off for Gen9 (verx10 < 110). |
| src/intel/vulkan/anv_shader_compile.c:1448 | `/* nir_lower_idiv generates expensive code */` | Comment records that nir_lower_idiv is expensive; a compile and runtime cost note for all gens. |
| src/intel/vulkan/anv_nir_apply_pipeline_layout.c:2844 | `*  1. 48-bit address calculations are potentially expensive and using` | A64 descriptor model pays for 48-bit address math; bindless vs A64 choice matters on Gen9. |
| src/intel/vulkan/anv_batch_chain.c:454 | `* additional bunch of flushes/stalls.` | Batch chaining adds flushes and stalls; overhead on every gen, Gen9 included. |
| src/intel/vulkan/anv_batch_chain.c:666 | `*     STATE_BASE_ADDRESS as needed which requires a full pipeline stall.  In` | STATE_BASE_ADDRESS changes need a full pipeline stall; costly if frequent. |
| src/intel/vulkan/anv_private.h:4178 | `* we would have to CS stall on every flush which could be bad.` | Explains why RO invalidation avoids a CS stall on every flush. |
| src/intel/vulkan/anv_blorp.c:2148 | `* supposedly unnecessary, we choose to perform the flush unconditionally` | Flush is unconditional though the authors think it may be unnecessary; removal candidate. |
| src/intel/vulkan/anv_image.c:718 | `* TODO: This is a heuristic trade-off; we haven't tuned it at all.` | Image-size heuristic was never tuned; possible perf win. |
| src/intel/vulkan/anv_nir_lower_resource_intel.c:130 | `* etc...) so it's worth optimizing that case.` | Resource lowering case flagged as worth optimizing. |
| src/intel/vulkan/anv_va.c:137 | `* unnecessary 64bit address computations (see lower_load_constant() in` | Constant-load lowering avoids unnecessary 64-bit address math; reference for the NIR path. |
| src/intel/vulkan/anv_physical_device.c:2957 | `* obvious win in terms of performance.` | Non-render engines kept off on i915 for perf reasons; does not change Gen9 (ver < 12.5). |
| src/intel/vulkan/anv_nir_lower_multiview.c:313 | `/* TODO: Figure out why we are not getting same benefits for larger than` | Primitive replication used only for 2 views; larger view counts are not beneficial yet. |
| src/intel/vulkan/genX_cmd_compute.c:184 | `* TODO: optimize this on Gfx12.5+ where the shader is not using per` | Comment limits the optimization to Gfx12.5+; Gen9 keeps the forced reallocation on every pipeline change. |

## Top 15 candidates

1. `src/intel/vulkan/genX_cmd_buffer.c:947`: Check whether the state-cache invalidate after clear-color surface updates can be skipped on Gen9 (comment says SKL did not need it in testing); verify against the PRM first.
2. `src/intel/vulkan/genX_cmd_buffer.c:5957`: Investigate whether the MEDIA_VFE_STATE re-emit on GPGPU-to-3D switch can be dropped; the comment says the root cause is unknown, so verify on Gen9 hardware first.
3. `src/intel/vulkan/genX_cmd_buffer.c:3169`: The Gen9-only null PIPE_CONTROL before each VF invalidate is a fixed per-barrier cost; check whether it can be skipped when the VF cache was unused since the last invalidate.
4. `src/intel/vulkan/genX_cmd_draw.c:501`: Remove the forced push-constant reallocation on gfx9 once issue 11064 is root-caused.
5. `src/intel/vulkan/genX_cmd_draw.c:1955`: Finish the base-vertex zeroing TODO in the pre-gfx11 indirect-draw path, which is live on Gen9; confirm intended behaviour first.
6. `src/intel/vulkan/genX_cmd_buffer.c:4613`: Track VF-cache state across secondary command buffers instead of always invalidating the whole cache.
7. `src/intel/vulkan/genX_cmd_buffer.c:5357`: Map barrier stage/access to precise flush bits instead of flushing every requested category.
8. `src/intel/vulkan/genX_query.c:1532`: Narrow the pipeline-statistics flush to the statistics that need it.
9. `src/intel/vulkan/anv_allocator.c:1436`: Propagate the BO allocation error as a VkResult instead of returning NULL (correctness).
10. `src/intel/vulkan/anv_batch_chain.c:1164`: Finish cleanup on batch clone failure after the error is set (verify for a leak first).
11. `src/intel/vulkan/anv_formats.c:437`: Enable G16B16G16R16_422 and B16G16R16G16_422 after the multiplanar refactor (feature gap).
12. `src/intel/vulkan/anv_formats.c:1060`: Fix the out-of-range sampling so linear implicit chroma reconstruction can be enabled for linear YUV (feature).
13. `src/intel/vulkan/genX_cmd_compute.c:184`: Extend the push-constant reallocation limit (forced on every pipeline change) to Gen9; the comment restricts it to Gfx12.5+.
14. `src/intel/vulkan/anv_astc_emu.c:29`: Build the ASTC emulation for 2D only so Gen9 ASTC emulation avoids unneeded array addressing.
15. `src/intel/vulkan/anv_image.c:1101`: Return the specific modifier plane-layout VkResult instead of OUT_OF_DEVICE_MEMORY (correctness).

Counts: Section 1 = 48 rows, Section 2 = 27 rows, Section 3 = 125 rows (41 tagged (Gen9)), Section 4 = 31 rows, Top 15 = 15 items.
