# Intel HD Graphics 620 Linux driver audit

This audit covers the Intel HD Graphics 620 Kaby Lake GT2 at PCI ID `8086:5916`. It compares what the installed stack exposes with what the cloned upstream source implements. The host runs Linux `7.0.0-42-generic`, Mesa `26.0.8-1ubuntu0.3`, and Intel iHD `26.1.2`. The source snapshots examined are Linux `20eef29b7c84` and Mesa `0e9e1086bb86`. The clones were inspected but were neither built nor installed. API enumeration confirms what the installed driver advertises. Only the running renderer identification, driver initialization, and capability queries were exercised. The findings cover API enumeration and targeted source review. Workload testing, benchmarking, and full conformance testing remain unverified.

## Already implemented

| Area | Evidence |
| --- | --- |
| Kernel graphics driver | `i915` binds to the GPU and exposes a render node. A read-only engine query enumerated one render, one copy, one video, and one video-enhancement engine. The video engine reported HEVC and SFC. The internal eDP display is connected. HDMI was disconnected during the audit. |
| Kernel memory and submission | The Kaby Lake profile inherits 48-bit per-process graphics address spaces, logical ring contexts and Gen9 execution support. See [platform features](/home/polarbear/.t3/projects/catchingup/linux/drivers/gpu/drm/i915/i915_pci.c:414) and [Execlists submission](/home/polarbear/.t3/projects/catchingup/linux/drivers/gpu/drm/i915/gt/intel_execlists_submission.c:3526). |
| Kernel display and power | KMS and eDP/HDMI handling, runtime power management, RPS frequency control, RC6 idle states, framebuffer compression and panel self-refresh have driver implementations. Actual FBC and PSR use depends on display state and the panel. See [platform features](/home/polarbear/.t3/projects/catchingup/linux/drivers/gpu/drm/i915/i915_pci.c:414), [FBC](/home/polarbear/.t3/projects/catchingup/linux/drivers/gpu/drm/i915/display/intel_fbc.c:2268) and [PSR](/home/polarbear/.t3/projects/catchingup/linux/drivers/gpu/drm/i915/display/intel_psr.c:199). |
| OpenGL, GLES and EGL | Hardware-accelerated Iris reports OpenGL 4.6 core and compatibility, OpenGL ES 3.2 and EGL 1.5. EGL reported the HD 620 renderer on GBM, Wayland, X11 and surfaceless platforms. |
| Vulkan | ANV reports Vulkan 1.4.335 with a reported conformance version of 1.4.0.0, 183 default device extensions and one graphics, compute, transfer and present queue. The driver reports descriptor indexing and buffers, dynamic rendering and local read, synchronization2, timeline semaphores, buffer device addresses, pipeline binaries, shader objects, graphics pipeline libraries, calibrated timestamps and present wait. |
| Newer upstream Vulkan work | The cloned ANV source advertises `VK_EXT_descriptor_heap` unconditionally. It is absent from the installed extension list. Maintenance 7, 8 and 9 are already advertised on the host. See [ANV extension table](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:424). |
| Hardware media through VA-API | iHD initialized on the GPU and advertised H.264, HEVC Main/Main10, MPEG-2, VP8 and baseline JPEG decode and encode, VC-1 and VP9 profiles 0/2 decode, and video processing. Exact profiles and entry points are in the appendix. See the [Intel media driver](https://github.com/intel/media-driver). |

## Partially implemented or conditional

| Area | Current coverage and limitation |
| --- | --- |
| Vulkan Video | The installed ANV does not expose video queues by default. With process-local video debug flags, it exposed H.264, HEVC and VP9 decode, H.264 encode, and video maintenance 1/2 on one video queue. The cloned source has a HuC gate for HEVC decode and a Gen12 floor for HEVC encode. This audit enumerated the extensions and queue without running video workloads. See [codec gates](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:495) and [queue selection](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:2961). |
| Vulkan cooperative matrices | Absent from the default device inventory. With process-local `INTEL_LOWER_DPAS=1`, installed ANV advertised `VK_KHR_cooperative_matrix` and reported `cooperativeMatrix = true`. The cloned source has an emulated property path and compiler lowering. No matrix workload was exercised, and the GPU has no native systolic acceleration. See [reported configurations](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:3750) and [compiler lowering](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/compiler/brw/brw_lower_dpas.cpp:302). |
| Device-generated commands | The cloned ANV gates `VK_EXT_device_generated_commands` on Gen12.5 or a source-level experimental flag. The installed Mesa did not report the extension on this GPU in either the default run or a run with `ANV_DEBUG=experimental`. See [upstream extension gate](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:551). |
| Vulkan line rasterization | Bresenham and smooth modes are advertised. Rectangular mode and stippled rectangular/smooth modes are unavailable on this GPU. See [ANV feature declarations](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:907). |
| Shader floating-point atomics | Both float-atomic extensions are advertised, with capability bits for individual operations. FP32 atomic add was false for buffers, shared memory and images. See [ANV feature declarations](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:983). |
| GuC and HuC | Kaby Lake firmware paths exist. Upstream defaults disable GuC/HuC through the `enable_guc` policy on pre-Gen12, and the GuC submission path excludes Gen9. Firmware status and application behavior can differ from those defaults on the installed distribution kernel. See [firmware selection](/home/polarbear/.t3/projects/catchingup/linux/drivers/gpu/drm/i915/gt/uc/intel_uc_fw.c:110), [defaults](/home/polarbear/.t3/projects/catchingup/linux/drivers/gpu/drm/i915/gt/uc/intel_uc.c:27) and [submission gate](/home/polarbear/.t3/projects/catchingup/linux/drivers/gpu/drm/i915/gt/uc/intel_guc_submission.c:4818). |
| Depth compression | ANV explicitly disables auxiliary compression on Gen9 depth images with multiple mip levels. Single-level images can still use it where otherwise eligible. See [ANV image allocation](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_image.c:2035). |
| OpenCL and Level Zero | The OpenCL loader is present, and `clGetPlatformIDs` returned `CL_PLATFORM_NOT_FOUND_KHR` with zero platforms. No Level Zero loader was found in the dynamic linker cache. Intel documents OpenCL 3.0 and Level Zero 1.5 for this GPU via its legacy branch, with critical fixes and a frozen feature set. See [Intel legacy compute support](https://github.com/intel/compute-runtime/blob/master/documentation/LEGACY_PLATFORMS.md). |

## Missing software work

| Work item | What can be delivered and verified |
| --- | --- |
| Restore local OpenCL 3.0 and Level Zero 1.5 | Use the matching Intel legacy runtime, register its ICD or loader components, and test enumeration plus a compute kernel. The upstream implementation already exists. Mesa Rusticl over Iris is an alternative for OpenCL, subject to package and device validation. See [Rusticl](https://docs.mesa3d.org/rusticl.html). |
| HEVC encode through Vulkan Video | This GPU and the iHD driver already expose HEVC Main/Main10 encoding through VA-API. A Gen9 ANV implementation could target that hardware, including profile validation, reference pictures, rate control, synchronization and required bitstream behavior. The VA-API capability establishes a hardware path. Passing Vulkan Video CTS and stream validation is still required. See [ANV encode gate](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:503). |
| Default Vulkan Video for supported Gen9 codecs | The opt-in queue and codecs already enumerate. Finishing default support requires profile-specific conformance, known-good decode/encode samples, multi-frame reference tests, firmware handling and stable exposure. Enablement can be evaluated per codec. |
| Optional FP32 shader atomic add | Mesa already has [NIR compare-exchange lowering](/home/polarbear/.t3/projects/catchingup/mesa/src/compiler/nir/nir_lower_atomics.c:29). A bounded first target is FP32 SSBO or shared-memory add. Check the integer atomic prerequisites, NaNs, rounding, memory ordering, contention and CTS before enabling a capability bit. Image atomics need a separate format and addressing assessment. |
| Production support for emulated cooperative matrices | The opt-in software path already enumerates on this GPU. Measure correctness and throughput for each advertised type and matrix shape against a scalar reference and a tuned compute shader before considering default exposure. |
| AV1 playback fallback | A software decoder can feed an existing GPU presentation path. This is a complete playback feature at the application or media-framework layer. It carries a CPU cost and does not add a hardware codec profile. |

Native Vulkan sparse residency is also absent, but Gen9 `i915` lacks the backend ANV uses for it. Mesa has an application-specific fake-sparse option. A conformant implementation would require a demonstrated GPU page-table or shader/resource implementation that preserves Vulkan's residency and aliasing semantics. Ray-tracing pipelines, native mesh shading, native cooperative-matrix acceleration, fragment shading rate on ANV, protected Vulkan memory and fixed-function AV1 are outside the hardware capabilities or generation paths exposed here. CPU or shader substitutes have different API coverage and performance. See [ANV feature gates](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:361) and [sparse backend selection](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:3191).

## Incorrect or inconsistent behavior

**Reproduced in the installed Mesa with Vulkan Video debug flags:** the video queue reports `VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR`, but the device does not advertise `VK_KHR_video_encode_h265`. The cloned source contains the same inconsistent gating. It adds the queue operation when video encode is enabled, while the device extension requires Gen12 or newer. A focused fix should derive each reported queue operation from the corresponding exposed codec capability and test extension/operation consistency. The reproduced mismatch concerns capability reporting. Encode workload behavior and Vulkan conformance remain untested. See [extension gate](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:503), [queue property code](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_physical_device.c:3483), and [Vulkan queue property documentation](https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyVideoPropertiesKHR.html).

Mesa also documents an [i915 engine-capability query issue](https://gitlab.freedesktop.org/drm/intel/-/issues/8832) and caps Gen9 Vulkan Video at one queue. This machine actually exposes one video engine and the kernel query returned HEVC/SFC on it, so the known workaround did not hide a second engine on this GPU. No additional current defects were established by these queries and the targeted source review.

## Optimization and reimplementation candidates

1. **Enable Gen9 HiZ for multi-mip depth images.** Extend BLORP's auxiliary handling, then test clears, resolves and sampling across mip levels. Compare bandwidth and frame time on workloads that actually use depth mip chains. This is the strongest explicit Gen9 optimization TODO found in the audit. See [ANV image allocation](/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/anv_image.c:2035).
2. **Reduce media-to-display copies.** Measure existing VA-API DMA-BUF export and compositor or renderer import. Improve format and modifier negotiation where the working path copies frames. Compare H.264 and Main10 playback CPU time, bandwidth and dropped frames.
3. **Improve display power where FBC or PSR is eligible.** Measure residency, panel compatibility, idle package power and flicker before changing policy. These target display bandwidth and battery draw. See [FBC policy](/home/polarbear/.t3/projects/catchingup/linux/drivers/gpu/drm/i915/display/intel_fbc.c:2268) and [PSR eligibility](/home/polarbear/.t3/projects/catchingup/linux/drivers/gpu/drm/i915/display/intel_psr.c:199).
4. **Reduce GPU submission, synchronization and state emission overhead where traces show stalls.** Profile ANV and Iris batch submissions, wait time, CPU overhead and p95/p99 frame time. Gen9 has a specific VF-cache reason for avoiding Iris's existing dynamic VAO fast path, so a replacement needs a Gen9-aware design. See [Iris capability policy](/home/polarbear/.t3/projects/catchingup/mesa/src/gallium/drivers/iris/iris_screen.c:450).
5. **Prototype legacy two-sided color using SBE attribute swizzling.** Iris documents this alternative to shader lowering and describes the expected gain as minor. This is a low-priority optimization for workloads using the feature. See [Iris TODO](/home/polarbear/.t3/projects/catchingup/mesa/src/gallium/drivers/iris/iris_screen.c:488).

DXVK, VKD3D-Proton and browser WebGPU are separate application-facing layers. The measured Vulkan version, descriptor indexing and 256-byte push-constant limit are relevant inputs, but exact working profiles require checking the full application requirements and running that application. See [DXVK requirements](https://github.com/doitsujin/dxvk/wiki/Driver-support), [VKD3D-Proton requirements](https://github.com/HansKristian-Work/vkd3d-proton/blob/master/README.md) and [Dawn platform support](https://github.com/google/dawn/blob/main/docs/support.md).

## Capability appendix

The inventories below come from the installed drivers on the HD 620. Vulkan was restricted to the ANV ICD. The displayed capability values are those the driver reported at query time.

### Default Vulkan device extensions (183)

```text
VK_AMD_buffer_marker                          : extension revision 1
  VK_EXT_4444_formats                           : extension revision 1
  VK_EXT_attachment_feedback_loop_dynamic_state : extension revision 1
  VK_EXT_attachment_feedback_loop_layout        : extension revision 2
  VK_EXT_border_color_swizzle                   : extension revision 1
  VK_EXT_buffer_device_address                  : extension revision 2
  VK_EXT_calibrated_timestamps                  : extension revision 2
  VK_EXT_color_write_enable                     : extension revision 1
  VK_EXT_conditional_rendering                  : extension revision 2
  VK_EXT_conservative_rasterization             : extension revision 1
  VK_EXT_custom_border_color                    : extension revision 12
  VK_EXT_depth_bias_control                     : extension revision 1
  VK_EXT_depth_clamp_control                    : extension revision 1
  VK_EXT_depth_clamp_zero_one                   : extension revision 1
  VK_EXT_depth_clip_control                     : extension revision 1
  VK_EXT_depth_clip_enable                      : extension revision 1
  VK_EXT_descriptor_buffer                      : extension revision 1
  VK_EXT_descriptor_indexing                    : extension revision 2
  VK_EXT_device_memory_report                   : extension revision 2
  VK_EXT_display_control                        : extension revision 1
  VK_EXT_dynamic_rendering_unused_attachments   : extension revision 1
  VK_EXT_extended_dynamic_state                 : extension revision 1
  VK_EXT_extended_dynamic_state2                : extension revision 1
  VK_EXT_extended_dynamic_state3                : extension revision 2
  VK_EXT_external_memory_acquire_unmodified     : extension revision 1
  VK_EXT_external_memory_dma_buf                : extension revision 1
  VK_EXT_external_memory_host                   : extension revision 1
  VK_EXT_fragment_shader_interlock              : extension revision 1
  VK_EXT_global_priority                        : extension revision 2
  VK_EXT_global_priority_query                  : extension revision 1
  VK_EXT_graphics_pipeline_library              : extension revision 1
  VK_EXT_hdr_metadata                           : extension revision 3
  VK_EXT_host_image_copy                        : extension revision 1
  VK_EXT_host_query_reset                       : extension revision 1
  VK_EXT_image_2d_view_of_3d                    : extension revision 1
  VK_EXT_image_drm_format_modifier              : extension revision 2
  VK_EXT_image_robustness                       : extension revision 1
  VK_EXT_image_sliced_view_of_3d                : extension revision 1
  VK_EXT_image_view_min_lod                     : extension revision 1
  VK_EXT_index_type_uint8                       : extension revision 1
  VK_EXT_inline_uniform_block                   : extension revision 1
  VK_EXT_legacy_dithering                       : extension revision 2
  VK_EXT_legacy_vertex_attributes               : extension revision 1
  VK_EXT_line_rasterization                     : extension revision 1
  VK_EXT_load_store_op_none                     : extension revision 1
  VK_EXT_map_memory_placed                      : extension revision 1
  VK_EXT_memory_budget                          : extension revision 1
  VK_EXT_multi_draw                             : extension revision 1
  VK_EXT_mutable_descriptor_type                : extension revision 1
  VK_EXT_nested_command_buffer                  : extension revision 1
  VK_EXT_non_seamless_cube_map                  : extension revision 1
  VK_EXT_pci_bus_info                           : extension revision 2
  VK_EXT_physical_device_drm                    : extension revision 1
  VK_EXT_pipeline_creation_cache_control        : extension revision 3
  VK_EXT_pipeline_creation_feedback             : extension revision 1
  VK_EXT_pipeline_robustness                    : extension revision 1
  VK_EXT_post_depth_coverage                    : extension revision 1
  VK_EXT_primitive_topology_list_restart        : extension revision 1
  VK_EXT_primitives_generated_query             : extension revision 1
  VK_EXT_private_data                           : extension revision 1
  VK_EXT_provoking_vertex                       : extension revision 1
  VK_EXT_queue_family_foreign                   : extension revision 1
  VK_EXT_robustness2                            : extension revision 1
  VK_EXT_sample_locations                       : extension revision 1
  VK_EXT_sampler_filter_minmax                  : extension revision 2
  VK_EXT_scalar_block_layout                    : extension revision 1
  VK_EXT_separate_stencil_usage                 : extension revision 1
  VK_EXT_shader_atomic_float                    : extension revision 1
  VK_EXT_shader_atomic_float2                   : extension revision 1
  VK_EXT_shader_demote_to_helper_invocation     : extension revision 1
  VK_EXT_shader_image_atomic_int64              : extension revision 1
  VK_EXT_shader_module_identifier               : extension revision 1
  VK_EXT_shader_object                          : extension revision 1
  VK_EXT_shader_replicated_composites           : extension revision 1
  VK_EXT_shader_stencil_export                  : extension revision 1
  VK_EXT_shader_subgroup_ballot                 : extension revision 1
  VK_EXT_shader_subgroup_vote                   : extension revision 1
  VK_EXT_shader_uniform_buffer_unsized_array    : extension revision 1
  VK_EXT_shader_viewport_index_layer            : extension revision 1
  VK_EXT_subgroup_size_control                  : extension revision 2
  VK_EXT_swapchain_maintenance1                 : extension revision 1
  VK_EXT_texel_buffer_alignment                 : extension revision 1
  VK_EXT_tooling_info                           : extension revision 1
  VK_EXT_transform_feedback                     : extension revision 1
  VK_EXT_vertex_attribute_divisor               : extension revision 3
  VK_EXT_vertex_input_dynamic_state             : extension revision 2
  VK_EXT_ycbcr_2plane_444_formats               : extension revision 1
  VK_EXT_ycbcr_image_arrays                     : extension revision 1
  VK_GOOGLE_decorate_string                     : extension revision 1
  VK_GOOGLE_hlsl_functionality1                 : extension revision 1
  VK_GOOGLE_user_type                           : extension revision 1
  VK_INTEL_shader_integer_functions2            : extension revision 1
  VK_KHR_16bit_storage                          : extension revision 1
  VK_KHR_8bit_storage                           : extension revision 1
  VK_KHR_bind_memory2                           : extension revision 1
  VK_KHR_buffer_device_address                  : extension revision 1
  VK_KHR_calibrated_timestamps                  : extension revision 1
  VK_KHR_compute_shader_derivatives             : extension revision 1
  VK_KHR_copy_commands2                         : extension revision 1
  VK_KHR_create_renderpass2                     : extension revision 1
  VK_KHR_dedicated_allocation                   : extension revision 3
  VK_KHR_deferred_host_operations               : extension revision 4
  VK_KHR_depth_clamp_zero_one                   : extension revision 1
  VK_KHR_depth_stencil_resolve                  : extension revision 1
  VK_KHR_descriptor_update_template             : extension revision 1
  VK_KHR_device_group                           : extension revision 4
  VK_KHR_draw_indirect_count                    : extension revision 1
  VK_KHR_driver_properties                      : extension revision 1
  VK_KHR_dynamic_rendering                      : extension revision 1
  VK_KHR_dynamic_rendering_local_read           : extension revision 1
  VK_KHR_external_fence                         : extension revision 1
  VK_KHR_external_fence_fd                      : extension revision 1
  VK_KHR_external_memory                        : extension revision 1
  VK_KHR_external_memory_fd                     : extension revision 1
  VK_KHR_external_semaphore                     : extension revision 1
  VK_KHR_external_semaphore_fd                  : extension revision 1
  VK_KHR_format_feature_flags2                  : extension revision 2
  VK_KHR_get_memory_requirements2               : extension revision 1
  VK_KHR_global_priority                        : extension revision 1
  VK_KHR_image_format_list                      : extension revision 1
  VK_KHR_imageless_framebuffer                  : extension revision 1
  VK_KHR_incremental_present                    : extension revision 2
  VK_KHR_index_type_uint8                       : extension revision 1
  VK_KHR_line_rasterization                     : extension revision 1
  VK_KHR_load_store_op_none                     : extension revision 1
  VK_KHR_maintenance1                           : extension revision 2
  VK_KHR_maintenance10                          : extension revision 1
  VK_KHR_maintenance2                           : extension revision 1
  VK_KHR_maintenance3                           : extension revision 1
  VK_KHR_maintenance4                           : extension revision 2
  VK_KHR_maintenance5                           : extension revision 1
  VK_KHR_maintenance6                           : extension revision 1
  VK_KHR_maintenance7                           : extension revision 1
  VK_KHR_maintenance8                           : extension revision 1
  VK_KHR_maintenance9                           : extension revision 1
  VK_KHR_map_memory2                            : extension revision 1
  VK_KHR_multiview                              : extension revision 1
  VK_KHR_pipeline_binary                        : extension revision 1
  VK_KHR_pipeline_executable_properties         : extension revision 1
  VK_KHR_pipeline_library                       : extension revision 1
  VK_KHR_present_id                             : extension revision 1
  VK_KHR_present_id2                            : extension revision 1
  VK_KHR_present_wait                           : extension revision 1
  VK_KHR_present_wait2                          : extension revision 1
  VK_KHR_push_descriptor                        : extension revision 2
  VK_KHR_relaxed_block_layout                   : extension revision 1
  VK_KHR_robustness2                            : extension revision 1
  VK_KHR_sampler_mirror_clamp_to_edge           : extension revision 3
  VK_KHR_sampler_ycbcr_conversion               : extension revision 14
  VK_KHR_separate_depth_stencil_layouts         : extension revision 1
  VK_KHR_shader_atomic_int64                    : extension revision 1
  VK_KHR_shader_clock                           : extension revision 1
  VK_KHR_shader_draw_parameters                 : extension revision 1
  VK_KHR_shader_expect_assume                   : extension revision 1
  VK_KHR_shader_float16_int8                    : extension revision 1
  VK_KHR_shader_float_controls                  : extension revision 4
  VK_KHR_shader_float_controls2                 : extension revision 1
  VK_KHR_shader_integer_dot_product             : extension revision 1
  VK_KHR_shader_maximal_reconvergence           : extension revision 1
  VK_KHR_shader_non_semantic_info               : extension revision 1
  VK_KHR_shader_quad_control                    : extension revision 1
  VK_KHR_shader_relaxed_extended_instruction    : extension revision 1
  VK_KHR_shader_subgroup_extended_types         : extension revision 1
  VK_KHR_shader_subgroup_rotate                 : extension revision 2
  VK_KHR_shader_subgroup_uniform_control_flow   : extension revision 1
  VK_KHR_shader_terminate_invocation            : extension revision 1
  VK_KHR_shader_untyped_pointers                : extension revision 1
  VK_KHR_spirv_1_4                              : extension revision 1
  VK_KHR_storage_buffer_storage_class           : extension revision 1
  VK_KHR_swapchain                              : extension revision 70
  VK_KHR_swapchain_maintenance1                 : extension revision 1
  VK_KHR_swapchain_mutable_format               : extension revision 1
  VK_KHR_synchronization2                       : extension revision 1
  VK_KHR_timeline_semaphore                     : extension revision 2
  VK_KHR_uniform_buffer_standard_layout         : extension revision 1
  VK_KHR_variable_pointers                      : extension revision 1
  VK_KHR_vertex_attribute_divisor               : extension revision 1
  VK_KHR_vulkan_memory_model                    : extension revision 3
  VK_KHR_workgroup_memory_explicit_layout       : extension revision 1
  VK_KHR_zero_initialize_workgroup_memory       : extension revision 1
  VK_MESA_image_alignment_control               : extension revision 1
  VK_NV_compute_shader_derivatives              : extension revision 1
  VK_VALVE_mutable_descriptor_type              : extension revision 1
```

### Vulkan device feature bits

```text
VkPhysicalDeviceFeatures:
=========================
  robustBufferAccess                      = true
  fullDrawIndexUint32                     = true
  imageCubeArray                          = true
  independentBlend                        = true
  geometryShader                          = true
  tessellationShader                      = true
  sampleRateShading                       = true
  dualSrcBlend                            = true
  logicOp                                 = true
  multiDrawIndirect                       = true
  drawIndirectFirstInstance               = true
  depthClamp                              = true
  depthBiasClamp                          = true
  fillModeNonSolid                        = true
  depthBounds                             = false
  wideLines                               = true
  largePoints                             = true
  alphaToOne                              = true
  multiViewport                           = true
  samplerAnisotropy                       = true
  textureCompressionETC2                  = true
  textureCompressionASTC_LDR              = true
  textureCompressionBC                    = true
  occlusionQueryPrecise                   = true
  pipelineStatisticsQuery                 = true
  vertexPipelineStoresAndAtomics          = true
  fragmentStoresAndAtomics                = true
  shaderTessellationAndGeometryPointSize  = true
  shaderImageGatherExtended               = true
  shaderStorageImageExtendedFormats       = true
  shaderStorageImageMultisample           = false
  shaderStorageImageReadWithoutFormat     = false
  shaderStorageImageWriteWithoutFormat    = true
  shaderUniformBufferArrayDynamicIndexing = true
  shaderSampledImageArrayDynamicIndexing  = true
  shaderStorageBufferArrayDynamicIndexing = true
  shaderStorageImageArrayDynamicIndexing  = true
  shaderClipDistance                      = true
  shaderCullDistance                      = true
  shaderFloat64                           = true
  shaderInt64                             = true
  shaderInt16                             = true
  shaderResourceResidency                 = false
  shaderResourceMinLod                    = true
  sparseBinding                           = false
  sparseResidencyBuffer                   = false
  sparseResidencyImage2D                  = false
  sparseResidencyImage3D                  = false
  sparseResidency2Samples                 = false
  sparseResidency4Samples                 = false
  sparseResidency8Samples                 = false
  sparseResidency16Samples                = false
  sparseResidencyAliased                  = false
  variableMultisampleRate                 = true
  inheritedQueries                        = true

VkPhysicalDevice4444FormatsFeaturesEXT:
---------------------------------------
  formatA4R4G4B4 = true
  formatA4B4G4R4 = false

VkPhysicalDeviceAttachmentFeedbackLoopDynamicStateFeaturesEXT:
--------------------------------------------------------------
  attachmentFeedbackLoopDynamicState = true

VkPhysicalDeviceAttachmentFeedbackLoopLayoutFeaturesEXT:
--------------------------------------------------------
  attachmentFeedbackLoopLayout = true

VkPhysicalDeviceBorderColorSwizzleFeaturesEXT:
----------------------------------------------
  borderColorSwizzle          = true
  borderColorSwizzleFromImage = true

VkPhysicalDeviceBufferDeviceAddressFeaturesEXT:
-----------------------------------------------
  bufferDeviceAddress              = true
  bufferDeviceAddressCaptureReplay = false
  bufferDeviceAddressMultiDevice   = false

VkPhysicalDeviceColorWriteEnableFeaturesEXT:
--------------------------------------------
  colorWriteEnable = true

VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR:
----------------------------------------------------
  computeDerivativeGroupQuads  = true
  computeDerivativeGroupLinear = true

VkPhysicalDeviceConditionalRenderingFeaturesEXT:
------------------------------------------------
  conditionalRendering          = true
  inheritedConditionalRendering = true

VkPhysicalDeviceCustomBorderColorFeaturesEXT:
---------------------------------------------
  customBorderColors             = true
  customBorderColorWithoutFormat = true

VkPhysicalDeviceDepthBiasControlFeaturesEXT:
--------------------------------------------
  depthBiasControl                                = true
  leastRepresentableValueForceUnormRepresentation = false
  floatRepresentation                             = true
  depthBiasExact                                  = true

VkPhysicalDeviceDepthClampControlFeaturesEXT:
---------------------------------------------
  depthClampControl = true

VkPhysicalDeviceDepthClampZeroOneFeaturesKHR:
---------------------------------------------
  depthClampZeroOne = true

VkPhysicalDeviceDepthClipControlFeaturesEXT:
--------------------------------------------
  depthClipControl = true

VkPhysicalDeviceDepthClipEnableFeaturesEXT:
-------------------------------------------
  depthClipEnable = true

VkPhysicalDeviceDescriptorBufferFeaturesEXT:
--------------------------------------------
  descriptorBuffer                   = true
  descriptorBufferCaptureReplay      = true
  descriptorBufferImageLayoutIgnored = false
  descriptorBufferPushDescriptors    = true

VkPhysicalDeviceDeviceMemoryReportFeaturesEXT:
----------------------------------------------
  deviceMemoryReport = true

VkPhysicalDeviceDynamicRenderingUnusedAttachmentsFeaturesEXT:
-------------------------------------------------------------
  dynamicRenderingUnusedAttachments = true

VkPhysicalDeviceExtendedDynamicState2FeaturesEXT:
-------------------------------------------------
  extendedDynamicState2                   = true
  extendedDynamicState2LogicOp            = true
  extendedDynamicState2PatchControlPoints = true

VkPhysicalDeviceExtendedDynamicState3FeaturesEXT:
-------------------------------------------------
  extendedDynamicState3TessellationDomainOrigin         = true
  extendedDynamicState3DepthClampEnable                 = true
  extendedDynamicState3PolygonMode                      = true
  extendedDynamicState3RasterizationSamples             = true
  extendedDynamicState3SampleMask                       = true
  extendedDynamicState3AlphaToCoverageEnable            = true
  extendedDynamicState3AlphaToOneEnable                 = true
  extendedDynamicState3LogicOpEnable                    = true
  extendedDynamicState3ColorBlendEnable                 = true
  extendedDynamicState3ColorBlendEquation               = true
  extendedDynamicState3ColorWriteMask                   = true
  extendedDynamicState3RasterizationStream              = true
  extendedDynamicState3ConservativeRasterizationMode    = true
  extendedDynamicState3ExtraPrimitiveOverestimationSize = false
  extendedDynamicState3DepthClipEnable                  = true
  extendedDynamicState3SampleLocationsEnable            = true
  extendedDynamicState3ColorBlendAdvanced               = false
  extendedDynamicState3ProvokingVertexMode              = true
  extendedDynamicState3LineRasterizationMode            = true
  extendedDynamicState3LineStippleEnable                = true
  extendedDynamicState3DepthClipNegativeOneToOne        = true
  extendedDynamicState3ViewportWScalingEnable           = false
  extendedDynamicState3ViewportSwizzle                  = false
  extendedDynamicState3CoverageToColorEnable            = false
  extendedDynamicState3CoverageToColorLocation          = false
  extendedDynamicState3CoverageModulationMode           = false
  extendedDynamicState3CoverageModulationTableEnable    = false
  extendedDynamicState3CoverageModulationTable          = false
  extendedDynamicState3CoverageReductionMode            = false
  extendedDynamicState3RepresentativeFragmentTestEnable = false
  extendedDynamicState3ShadingRateImageEnable           = false

VkPhysicalDeviceExtendedDynamicStateFeaturesEXT:
------------------------------------------------
  extendedDynamicState = true

VkPhysicalDeviceFragmentShaderInterlockFeaturesEXT:
---------------------------------------------------
  fragmentShaderSampleInterlock      = true
  fragmentShaderPixelInterlock       = true
  fragmentShaderShadingRateInterlock = false

VkPhysicalDeviceGraphicsPipelineLibraryFeaturesEXT:
---------------------------------------------------
  graphicsPipelineLibrary = true

VkPhysicalDeviceImage2DViewOf3DFeaturesEXT:
-------------------------------------------
  image2DViewOf3D   = true
  sampler2DViewOf3D = true

VkPhysicalDeviceImageSlicedViewOf3DFeaturesEXT:
-----------------------------------------------
  imageSlicedViewOf3D = true

VkPhysicalDeviceImageViewMinLodFeaturesEXT:
-------------------------------------------
  minLod = true

VkPhysicalDeviceLegacyDitheringFeaturesEXT:
-------------------------------------------
  legacyDithering = true

VkPhysicalDeviceLegacyVertexAttributesFeaturesEXT:
--------------------------------------------------
  legacyVertexAttributes = true

VkPhysicalDeviceMaintenance10FeaturesKHR:
-----------------------------------------
  maintenance10 = true

VkPhysicalDeviceMaintenance7FeaturesKHR:
----------------------------------------
  maintenance7 = true

VkPhysicalDeviceMaintenance8FeaturesKHR:
----------------------------------------
  maintenance8 = true

VkPhysicalDeviceMaintenance9FeaturesKHR:
----------------------------------------
  maintenance9 = true

VkPhysicalDeviceMapMemoryPlacedFeaturesEXT:
-------------------------------------------
  memoryMapPlaced      = true
  memoryMapRangePlaced = false
  memoryUnmapReserve   = true

VkPhysicalDeviceMultiDrawFeaturesEXT:
-------------------------------------
  multiDraw = true

VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT:
-------------------------------------------------
  mutableDescriptorType = true

VkPhysicalDeviceNestedCommandBufferFeaturesEXT:
-----------------------------------------------
  nestedCommandBuffer                = true
  nestedCommandBufferRendering       = true
  nestedCommandBufferSimultaneousUse = false

VkPhysicalDeviceNonSeamlessCubeMapFeaturesEXT:
----------------------------------------------
  nonSeamlessCubeMap = true

VkPhysicalDevicePipelineBinaryFeaturesKHR:
------------------------------------------
  pipelineBinaries = true

VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR:
--------------------------------------------------------
  pipelineExecutableInfo = true

VkPhysicalDevicePresentId2FeaturesKHR:
--------------------------------------
  presentId2 = true

VkPhysicalDevicePresentIdFeaturesKHR:
-------------------------------------
  presentId = true

VkPhysicalDevicePresentWait2FeaturesKHR:
----------------------------------------
  presentWait2 = true

VkPhysicalDevicePresentWaitFeaturesKHR:
---------------------------------------
  presentWait = true

VkPhysicalDevicePrimitiveTopologyListRestartFeaturesEXT:
--------------------------------------------------------
  primitiveTopologyListRestart      = true
  primitiveTopologyPatchListRestart = true

VkPhysicalDevicePrimitivesGeneratedQueryFeaturesEXT:
----------------------------------------------------
  primitivesGeneratedQuery                      = true
  primitivesGeneratedQueryWithRasterizerDiscard = false
  primitivesGeneratedQueryWithNonZeroStreams    = false

VkPhysicalDeviceProvokingVertexFeaturesEXT:
-------------------------------------------
  provokingVertexLast                       = true
  transformFeedbackPreservesProvokingVertex = true

VkPhysicalDeviceRobustness2FeaturesKHR:
---------------------------------------
  robustBufferAccess2 = true
  robustImageAccess2  = true
  nullDescriptor      = true

VkPhysicalDeviceShaderAtomicFloat2FeaturesEXT:
----------------------------------------------
  shaderBufferFloat16Atomics      = false
  shaderBufferFloat16AtomicAdd    = false
  shaderBufferFloat16AtomicMinMax = false
  shaderBufferFloat32AtomicMinMax = true
  shaderBufferFloat64AtomicMinMax = false
  shaderSharedFloat16Atomics      = false
  shaderSharedFloat16AtomicAdd    = false
  shaderSharedFloat16AtomicMinMax = false
  shaderSharedFloat32AtomicMinMax = true
  shaderSharedFloat64AtomicMinMax = false
  shaderImageFloat32AtomicMinMax  = false
  sparseImageFloat32AtomicMinMax  = false

VkPhysicalDeviceShaderAtomicFloatFeaturesEXT:
---------------------------------------------
  shaderBufferFloat32Atomics   = true
  shaderBufferFloat32AtomicAdd = false
  shaderBufferFloat64Atomics   = false
  shaderBufferFloat64AtomicAdd = false
  shaderSharedFloat32Atomics   = true
  shaderSharedFloat32AtomicAdd = false
  shaderSharedFloat64Atomics   = false
  shaderSharedFloat64AtomicAdd = false
  shaderImageFloat32Atomics    = true
  shaderImageFloat32AtomicAdd  = false
  sparseImageFloat32Atomics    = false
  sparseImageFloat32AtomicAdd  = false

VkPhysicalDeviceShaderClockFeaturesKHR:
---------------------------------------
  shaderSubgroupClock = true
  shaderDeviceClock   = false

VkPhysicalDeviceShaderImageAtomicInt64FeaturesEXT:
--------------------------------------------------
  shaderImageInt64Atomics = true
  sparseImageInt64Atomics = false

VkPhysicalDeviceShaderMaximalReconvergenceFeaturesKHR:
------------------------------------------------------
  shaderMaximalReconvergence = true

VkPhysicalDeviceShaderModuleIdentifierFeaturesEXT:
--------------------------------------------------
  shaderModuleIdentifier = true

VkPhysicalDeviceShaderObjectFeaturesEXT:
----------------------------------------
  shaderObject = true

VkPhysicalDeviceShaderQuadControlFeaturesKHR:
---------------------------------------------
  shaderQuadControl = true

VkPhysicalDeviceShaderRelaxedExtendedInstructionFeaturesKHR:
------------------------------------------------------------
  shaderRelaxedExtendedInstruction = true

VkPhysicalDeviceShaderReplicatedCompositesFeaturesEXT:
------------------------------------------------------
  shaderReplicatedComposites = true

VkPhysicalDeviceShaderSubgroupUniformControlFlowFeaturesKHR:
------------------------------------------------------------
  shaderSubgroupUniformControlFlow = true

VkPhysicalDeviceShaderUniformBufferUnsizedArrayFeaturesEXT:
-----------------------------------------------------------
  shaderUniformBufferUnsizedArray = true

VkPhysicalDeviceShaderUntypedPointersFeaturesKHR:
-------------------------------------------------
  shaderUntypedPointers = true

VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR:
-------------------------------------------------
  swapchainMaintenance1 = true

VkPhysicalDeviceTexelBufferAlignmentFeaturesEXT:
------------------------------------------------
  texelBufferAlignment = true

VkPhysicalDeviceTransformFeedbackFeaturesEXT:
---------------------------------------------
  transformFeedback = true
  geometryStreams   = true

VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT:
---------------------------------------------------
  vertexInputDynamicState = true

VkPhysicalDeviceVulkan11Features:
---------------------------------
  storageBuffer16BitAccess           = true
  uniformAndStorageBuffer16BitAccess = true
  storagePushConstant16              = true
  storageInputOutput16               = false
  multiview                          = true
  multiviewGeometryShader            = true
  multiviewTessellationShader        = true
  variablePointersStorageBuffer      = true
  variablePointers                   = true
  protectedMemory                    = false
  samplerYcbcrConversion             = true
  shaderDrawParameters               = true

VkPhysicalDeviceVulkan12Features:
---------------------------------
  samplerMirrorClampToEdge                           = true
  drawIndirectCount                                  = true
  storageBuffer8BitAccess                            = true
  uniformAndStorageBuffer8BitAccess                  = true
  storagePushConstant8                               = true
  shaderBufferInt64Atomics                           = true
  shaderSharedInt64Atomics                           = false
  shaderFloat16                                      = true
  shaderInt8                                         = true
  descriptorIndexing                                 = true
  shaderInputAttachmentArrayDynamicIndexing          = false
  shaderUniformTexelBufferArrayDynamicIndexing       = true
  shaderStorageTexelBufferArrayDynamicIndexing       = true
  shaderUniformBufferArrayNonUniformIndexing         = true
  shaderSampledImageArrayNonUniformIndexing          = true
  shaderStorageBufferArrayNonUniformIndexing         = true
  shaderStorageImageArrayNonUniformIndexing          = true
  shaderInputAttachmentArrayNonUniformIndexing       = false
  shaderUniformTexelBufferArrayNonUniformIndexing    = true
  shaderStorageTexelBufferArrayNonUniformIndexing    = true
  descriptorBindingUniformBufferUpdateAfterBind      = true
  descriptorBindingSampledImageUpdateAfterBind       = true
  descriptorBindingStorageImageUpdateAfterBind       = true
  descriptorBindingStorageBufferUpdateAfterBind      = true
  descriptorBindingUniformTexelBufferUpdateAfterBind = true
  descriptorBindingStorageTexelBufferUpdateAfterBind = true
  descriptorBindingUpdateUnusedWhilePending          = true
  descriptorBindingPartiallyBound                    = true
  descriptorBindingVariableDescriptorCount           = true
  runtimeDescriptorArray                             = true
  samplerFilterMinmax                                = true
  scalarBlockLayout                                  = true
  imagelessFramebuffer                               = true
  uniformBufferStandardLayout                        = true
  shaderSubgroupExtendedTypes                        = true
  separateDepthStencilLayouts                        = true
  hostQueryReset                                     = true
  timelineSemaphore                                  = true
  bufferDeviceAddress                                = true
  bufferDeviceAddressCaptureReplay                   = true
  bufferDeviceAddressMultiDevice                     = false
  vulkanMemoryModel                                  = true
  vulkanMemoryModelDeviceScope                       = true
  vulkanMemoryModelAvailabilityVisibilityChains      = true
  shaderOutputViewportIndex                          = true
  shaderOutputLayer                                  = true
  subgroupBroadcastDynamicId                         = true

VkPhysicalDeviceVulkan13Features:
---------------------------------
  robustImageAccess                                  = true
  inlineUniformBlock                                 = true
  descriptorBindingInlineUniformBlockUpdateAfterBind = true
  pipelineCreationCacheControl                       = true
  privateData                                        = true
  shaderDemoteToHelperInvocation                     = true
  shaderTerminateInvocation                          = true
  subgroupSizeControl                                = true
  computeFullSubgroups                               = true
  synchronization2                                   = true
  textureCompressionASTC_HDR                         = false
  shaderZeroInitializeWorkgroupMemory                = true
  dynamicRendering                                   = true
  shaderIntegerDotProduct                            = true
  maintenance4                                       = true

VkPhysicalDeviceVulkan14Features:
---------------------------------
  globalPriorityQuery                    = true
  shaderSubgroupRotate                   = true
  shaderSubgroupRotateClustered          = true
  shaderFloatControls2                   = true
  shaderExpectAssume                     = true
  rectangularLines                       = false
  bresenhamLines                         = true
  smoothLines                            = true
  stippledRectangularLines               = false
  stippledBresenhamLines                 = true
  stippledSmoothLines                    = false
  vertexAttributeInstanceRateDivisor     = true
  vertexAttributeInstanceRateZeroDivisor = true
  indexTypeUint8                         = true
  dynamicRenderingLocalRead              = true
  maintenance5                           = true
  maintenance6                           = true
  pipelineProtectedAccess                = false
  pipelineRobustness                     = true
  hostImageCopy                          = true
  pushDescriptor                         = true

VkPhysicalDeviceWorkgroupMemoryExplicitLayoutFeaturesKHR:
---------------------------------------------------------
  workgroupMemoryExplicitLayout                  = true
  workgroupMemoryExplicitLayoutScalarBlockLayout = true
  workgroupMemoryExplicitLayout8BitAccess        = true
  workgroupMemoryExplicitLayout16BitAccess       = true

VkPhysicalDeviceYcbcr2Plane444FormatsFeaturesEXT:
-------------------------------------------------
  ycbcr2plane444Formats = true

VkPhysicalDeviceYcbcrImageArraysFeaturesEXT:
--------------------------------------------
  ycbcrImageArrays = true
```

### GLX extensions (31)

```text
GLX_ARB_context_flush_control
GLX_ARB_create_context
GLX_ARB_create_context_no_error
GLX_ARB_create_context_profile
GLX_ARB_create_context_robustness
GLX_ARB_fbconfig_float
GLX_ARB_framebuffer_sRGB
GLX_ARB_get_proc_address
GLX_ARB_multisample
GLX_EXT_buffer_age
GLX_EXT_create_context_es2_profile
GLX_EXT_create_context_es_profile
GLX_EXT_fbconfig_packed_float
GLX_EXT_framebuffer_sRGB
GLX_EXT_no_config_context
GLX_EXT_swap_control
GLX_EXT_swap_control_tear
GLX_EXT_texture_from_pixmap
GLX_EXT_visual_info
GLX_EXT_visual_rating
GLX_MESA_copy_sub_buffer
GLX_MESA_gl_interop
GLX_MESA_query_renderer
GLX_MESA_swap_control
GLX_OML_sync_control
GLX_SGIS_multisample
GLX_SGIX_fbconfig
GLX_SGIX_pbuffer
GLX_SGIX_visual_select_group
GLX_SGI_make_current_read
GLX_SGI_video_sync
```

### OpenGL core profile extensions (234)

```text
GL_3DFX_texture_compression_FXT1
GL_AMD_conservative_depth
GL_AMD_depth_clamp_separate
GL_AMD_draw_buffers_blend
GL_AMD_gpu_shader_int64
GL_AMD_multi_draw_indirect
GL_AMD_performance_monitor
GL_AMD_pinned_memory
GL_AMD_query_buffer_object
GL_AMD_seamless_cubemap_per_texture
GL_AMD_shader_stencil_export
GL_AMD_shader_trinary_minmax
GL_AMD_texture_texture4
GL_AMD_vertex_shader_layer
GL_AMD_vertex_shader_viewport_index
GL_ANGLE_texture_compression_dxt3
GL_ANGLE_texture_compression_dxt5
GL_ARB_ES2_compatibility
GL_ARB_ES3_1_compatibility
GL_ARB_ES3_2_compatibility
GL_ARB_ES3_compatibility
GL_ARB_arrays_of_arrays
GL_ARB_base_instance
GL_ARB_blend_func_extended
GL_ARB_buffer_storage
GL_ARB_clear_buffer_object
GL_ARB_clear_texture
GL_ARB_clip_control
GL_ARB_compressed_texture_pixel_storage
GL_ARB_compute_shader
GL_ARB_compute_variable_group_size
GL_ARB_conditional_render_inverted
GL_ARB_conservative_depth
GL_ARB_copy_buffer
GL_ARB_copy_image
GL_ARB_cull_distance
GL_ARB_debug_output
GL_ARB_depth_buffer_float
GL_ARB_depth_clamp
GL_ARB_derivative_control
GL_ARB_direct_state_access
GL_ARB_draw_buffers
GL_ARB_draw_buffers_blend
GL_ARB_draw_elements_base_vertex
GL_ARB_draw_indirect
GL_ARB_draw_instanced
GL_ARB_enhanced_layouts
GL_ARB_explicit_attrib_location
GL_ARB_explicit_uniform_location
GL_ARB_fragment_coord_conventions
GL_ARB_fragment_layer_viewport
GL_ARB_fragment_shader
GL_ARB_fragment_shader_interlock
GL_ARB_framebuffer_no_attachments
GL_ARB_framebuffer_object
GL_ARB_framebuffer_sRGB
GL_ARB_get_program_binary
GL_ARB_get_texture_sub_image
GL_ARB_gl_spirv
GL_ARB_gpu_shader5
GL_ARB_gpu_shader_fp64
GL_ARB_gpu_shader_int64
GL_ARB_half_float_pixel
GL_ARB_half_float_vertex
GL_ARB_indirect_parameters
GL_ARB_instanced_arrays
GL_ARB_internalformat_query
GL_ARB_internalformat_query2
GL_ARB_invalidate_subdata
GL_ARB_map_buffer_alignment
GL_ARB_map_buffer_range
GL_ARB_multi_bind
GL_ARB_multi_draw_indirect
GL_ARB_occlusion_query2
GL_ARB_parallel_shader_compile
GL_ARB_pipeline_statistics_query
GL_ARB_pixel_buffer_object
GL_ARB_point_sprite
GL_ARB_polygon_offset_clamp
GL_ARB_post_depth_coverage
GL_ARB_program_interface_query
GL_ARB_provoking_vertex
GL_ARB_query_buffer_object
GL_ARB_robust_buffer_access_behavior
GL_ARB_robustness
GL_ARB_sample_shading
GL_ARB_sampler_objects
GL_ARB_seamless_cube_map
GL_ARB_seamless_cubemap_per_texture
GL_ARB_separate_shader_objects
GL_ARB_shader_atomic_counter_ops
GL_ARB_shader_atomic_counters
GL_ARB_shader_ballot
GL_ARB_shader_bit_encoding
GL_ARB_shader_clock
GL_ARB_shader_draw_parameters
GL_ARB_shader_group_vote
GL_ARB_shader_image_load_store
GL_ARB_shader_image_size
GL_ARB_shader_objects
GL_ARB_shader_precision
GL_ARB_shader_stencil_export
GL_ARB_shader_storage_buffer_object
GL_ARB_shader_subroutine
GL_ARB_shader_texture_image_samples
GL_ARB_shader_texture_lod
GL_ARB_shader_viewport_layer_array
GL_ARB_shading_language_420pack
GL_ARB_shading_language_include
GL_ARB_shading_language_packing
GL_ARB_spirv_extensions
GL_ARB_stencil_texturing
GL_ARB_sync
GL_ARB_tessellation_shader
GL_ARB_texture_barrier
GL_ARB_texture_border_clamp
GL_ARB_texture_buffer_object
GL_ARB_texture_buffer_object_rgb32
GL_ARB_texture_buffer_range
GL_ARB_texture_compression_bptc
GL_ARB_texture_compression_rgtc
GL_ARB_texture_cube_map_array
GL_ARB_texture_filter_anisotropic
GL_ARB_texture_filter_minmax
GL_ARB_texture_float
GL_ARB_texture_gather
GL_ARB_texture_mirror_clamp_to_edge
GL_ARB_texture_multisample
GL_ARB_texture_non_power_of_two
GL_ARB_texture_query_levels
GL_ARB_texture_query_lod
GL_ARB_texture_rectangle
GL_ARB_texture_rg
GL_ARB_texture_rgb10_a2ui
GL_ARB_texture_stencil8
GL_ARB_texture_storage
GL_ARB_texture_storage_multisample
GL_ARB_texture_swizzle
GL_ARB_texture_view
GL_ARB_timer_query
GL_ARB_transform_feedback2
GL_ARB_transform_feedback3
GL_ARB_transform_feedback_instanced
GL_ARB_transform_feedback_overflow_query
GL_ARB_uniform_buffer_object
GL_ARB_vertex_array_bgra
GL_ARB_vertex_array_object
GL_ARB_vertex_attrib_64bit
GL_ARB_vertex_attrib_binding
GL_ARB_vertex_buffer_object
GL_ARB_vertex_shader
GL_ARB_vertex_type_10f_11f_11f_rev
GL_ARB_vertex_type_2_10_10_10_rev
GL_ARB_viewport_array
GL_ATI_blend_equation_separate
GL_ATI_texture_float
GL_EXT_EGL_image_storage
GL_EXT_EGL_image_storage_compression
GL_EXT_EGL_sync
GL_EXT_abgr
GL_EXT_blend_equation_separate
GL_EXT_debug_label
GL_EXT_demote_to_helper_invocation
GL_EXT_draw_buffers2
GL_EXT_draw_instanced
GL_EXT_framebuffer_blit
GL_EXT_framebuffer_multisample
GL_EXT_framebuffer_multisample_blit_scaled
GL_EXT_framebuffer_object
GL_EXT_framebuffer_sRGB
GL_EXT_memory_object
GL_EXT_memory_object_fd
GL_EXT_packed_depth_stencil
GL_EXT_packed_float
GL_EXT_pixel_buffer_object
GL_EXT_polygon_offset_clamp
GL_EXT_provoking_vertex
GL_EXT_semaphore
GL_EXT_semaphore_fd
GL_EXT_shader_clock
GL_EXT_shader_framebuffer_fetch
GL_EXT_shader_framebuffer_fetch_non_coherent
GL_EXT_shader_integer_mix
GL_EXT_shader_samples_identical
GL_EXT_texture_array
GL_EXT_texture_compression_dxt1
GL_EXT_texture_compression_rgtc
GL_EXT_texture_compression_s3tc
GL_EXT_texture_filter_anisotropic
GL_EXT_texture_integer
GL_EXT_texture_sRGB
GL_EXT_texture_sRGB_R8
GL_EXT_texture_sRGB_decode
GL_EXT_texture_shadow_lod
GL_EXT_texture_shared_exponent
GL_EXT_texture_snorm
GL_EXT_texture_storage
GL_EXT_texture_swizzle
GL_EXT_timer_query
GL_EXT_transform_feedback
GL_EXT_vertex_array_bgra
GL_EXT_vertex_attrib_64bit
GL_IBM_multimode_draw_arrays
GL_INTEL_blackhole_render
GL_INTEL_conservative_rasterization
GL_INTEL_performance_query
GL_INTEL_shader_atomic_float_minmax
GL_INTEL_shader_integer_functions2
GL_KHR_blend_equation_advanced
GL_KHR_blend_equation_advanced_coherent
GL_KHR_context_flush_control
GL_KHR_debug
GL_KHR_no_error
GL_KHR_parallel_shader_compile
GL_KHR_robust_buffer_access_behavior
GL_KHR_robustness
GL_KHR_shader_subgroup
GL_KHR_texture_compression_astc_ldr
GL_KHR_texture_compression_astc_sliced_3d
GL_MESA_framebuffer_flip_y
GL_MESA_pack_invert
GL_MESA_shader_integer_functions
GL_MESA_texture_const_bandwidth
GL_MESA_texture_signed_rgba
GL_NV_alpha_to_coverage_dither_control
GL_NV_compute_shader_derivatives
GL_NV_conditional_render
GL_NV_copy_image
GL_NV_depth_clamp
GL_NV_fragment_shader_interlock
GL_NV_packed_depth_stencil
GL_NV_texture_barrier
GL_OES_EGL_image
GL_S3_s3tc
```

### OpenGL compatibility profile extensions (314)

```text
GL_3DFX_texture_compression_FXT1
GL_AMD_conservative_depth
GL_AMD_depth_clamp_separate
GL_AMD_draw_buffers_blend
GL_AMD_multi_draw_indirect
GL_AMD_performance_monitor
GL_AMD_pinned_memory
GL_AMD_query_buffer_object
GL_AMD_seamless_cubemap_per_texture
GL_AMD_shader_stencil_export
GL_AMD_shader_trinary_minmax
GL_AMD_texture_texture4
GL_AMD_vertex_shader_layer
GL_AMD_vertex_shader_viewport_index
GL_ANGLE_texture_compression_dxt3
GL_ANGLE_texture_compression_dxt5
GL_APPLE_packed_pixels
GL_ARB_ES2_compatibility
GL_ARB_ES3_1_compatibility
GL_ARB_ES3_2_compatibility
GL_ARB_ES3_compatibility
GL_ARB_arrays_of_arrays
GL_ARB_base_instance
GL_ARB_blend_func_extended
GL_ARB_buffer_storage
GL_ARB_clear_buffer_object
GL_ARB_clear_texture
GL_ARB_clip_control
GL_ARB_color_buffer_float
GL_ARB_compatibility
GL_ARB_compressed_texture_pixel_storage
GL_ARB_compute_shader
GL_ARB_compute_variable_group_size
GL_ARB_conditional_render_inverted
GL_ARB_conservative_depth
GL_ARB_copy_buffer
GL_ARB_copy_image
GL_ARB_cull_distance
GL_ARB_debug_output
GL_ARB_depth_buffer_float
GL_ARB_depth_clamp
GL_ARB_depth_texture
GL_ARB_derivative_control
GL_ARB_direct_state_access
GL_ARB_draw_buffers
GL_ARB_draw_buffers_blend
GL_ARB_draw_elements_base_vertex
GL_ARB_draw_indirect
GL_ARB_draw_instanced
GL_ARB_enhanced_layouts
GL_ARB_explicit_attrib_location
GL_ARB_explicit_uniform_location
GL_ARB_fragment_coord_conventions
GL_ARB_fragment_layer_viewport
GL_ARB_fragment_program
GL_ARB_fragment_program_shadow
GL_ARB_fragment_shader
GL_ARB_fragment_shader_interlock
GL_ARB_framebuffer_no_attachments
GL_ARB_framebuffer_object
GL_ARB_framebuffer_sRGB
GL_ARB_get_program_binary
GL_ARB_get_texture_sub_image
GL_ARB_gl_spirv
GL_ARB_gpu_shader5
GL_ARB_gpu_shader_fp64
GL_ARB_gpu_shader_int64
GL_ARB_half_float_pixel
GL_ARB_half_float_vertex
GL_ARB_indirect_parameters
GL_ARB_instanced_arrays
GL_ARB_internalformat_query
GL_ARB_internalformat_query2
GL_ARB_invalidate_subdata
GL_ARB_map_buffer_alignment
GL_ARB_map_buffer_range
GL_ARB_multi_bind
GL_ARB_multi_draw_indirect
GL_ARB_multisample
GL_ARB_multitexture
GL_ARB_occlusion_query
GL_ARB_occlusion_query2
GL_ARB_parallel_shader_compile
GL_ARB_pipeline_statistics_query
GL_ARB_pixel_buffer_object
GL_ARB_point_parameters
GL_ARB_point_sprite
GL_ARB_polygon_offset_clamp
GL_ARB_post_depth_coverage
GL_ARB_program_interface_query
GL_ARB_provoking_vertex
GL_ARB_query_buffer_object
GL_ARB_robust_buffer_access_behavior
GL_ARB_robustness
GL_ARB_sample_shading
GL_ARB_sampler_objects
GL_ARB_seamless_cube_map
GL_ARB_seamless_cubemap_per_texture
GL_ARB_separate_shader_objects
GL_ARB_shader_atomic_counter_ops
GL_ARB_shader_atomic_counters
GL_ARB_shader_ballot
GL_ARB_shader_bit_encoding
GL_ARB_shader_clock
GL_ARB_shader_draw_parameters
GL_ARB_shader_group_vote
GL_ARB_shader_image_load_store
GL_ARB_shader_image_size
GL_ARB_shader_objects
GL_ARB_shader_precision
GL_ARB_shader_stencil_export
GL_ARB_shader_storage_buffer_object
GL_ARB_shader_subroutine
GL_ARB_shader_texture_image_samples
GL_ARB_shader_texture_lod
GL_ARB_shader_viewport_layer_array
GL_ARB_shading_language_100
GL_ARB_shading_language_420pack
GL_ARB_shading_language_include
GL_ARB_shading_language_packing
GL_ARB_shadow
GL_ARB_spirv_extensions
GL_ARB_stencil_texturing
GL_ARB_sync
GL_ARB_tessellation_shader
GL_ARB_texture_barrier
GL_ARB_texture_border_clamp
GL_ARB_texture_buffer_object
GL_ARB_texture_buffer_object_rgb32
GL_ARB_texture_buffer_range
GL_ARB_texture_compression
GL_ARB_texture_compression_bptc
GL_ARB_texture_compression_rgtc
GL_ARB_texture_cube_map
GL_ARB_texture_cube_map_array
GL_ARB_texture_env_add
GL_ARB_texture_env_combine
GL_ARB_texture_env_crossbar
GL_ARB_texture_env_dot3
GL_ARB_texture_filter_anisotropic
GL_ARB_texture_filter_minmax
GL_ARB_texture_float
GL_ARB_texture_gather
GL_ARB_texture_mirror_clamp_to_edge
GL_ARB_texture_mirrored_repeat
GL_ARB_texture_multisample
GL_ARB_texture_non_power_of_two
GL_ARB_texture_query_levels
GL_ARB_texture_query_lod
GL_ARB_texture_rectangle
GL_ARB_texture_rg
GL_ARB_texture_rgb10_a2ui
GL_ARB_texture_stencil8
GL_ARB_texture_storage
GL_ARB_texture_storage_multisample
GL_ARB_texture_swizzle
GL_ARB_texture_view
GL_ARB_timer_query
GL_ARB_transform_feedback2
GL_ARB_transform_feedback3
GL_ARB_transform_feedback_instanced
GL_ARB_transform_feedback_overflow_query
GL_ARB_transpose_matrix
GL_ARB_uniform_buffer_object
GL_ARB_vertex_array_bgra
GL_ARB_vertex_array_object
GL_ARB_vertex_attrib_64bit
GL_ARB_vertex_attrib_binding
GL_ARB_vertex_buffer_object
GL_ARB_vertex_program
GL_ARB_vertex_shader
GL_ARB_vertex_type_10f_11f_11f_rev
GL_ARB_vertex_type_2_10_10_10_rev
GL_ARB_viewport_array
GL_ARB_window_pos
GL_ATI_blend_equation_separate
GL_ATI_draw_buffers
GL_ATI_fragment_shader
GL_ATI_separate_stencil
GL_ATI_texture_compression_3dc
GL_ATI_texture_env_combine3
GL_ATI_texture_float
GL_EXT_EGL_image_storage
GL_EXT_EGL_image_storage_compression
GL_EXT_EGL_sync
GL_EXT_abgr
GL_EXT_bgra
GL_EXT_blend_color
GL_EXT_blend_equation_separate
GL_EXT_blend_func_separate
GL_EXT_blend_minmax
GL_EXT_blend_subtract
GL_EXT_compiled_vertex_array
GL_EXT_copy_texture
GL_EXT_debug_label
GL_EXT_demote_to_helper_invocation
GL_EXT_direct_state_access
GL_EXT_draw_buffers2
GL_EXT_draw_instanced
GL_EXT_draw_range_elements
GL_EXT_fog_coord
GL_EXT_framebuffer_blit
GL_EXT_framebuffer_multisample
GL_EXT_framebuffer_multisample_blit_scaled
GL_EXT_framebuffer_object
GL_EXT_framebuffer_sRGB
GL_EXT_gpu_program_parameters
GL_EXT_gpu_shader4
GL_EXT_memory_object
GL_EXT_memory_object_fd
GL_EXT_multi_draw_arrays
GL_EXT_packed_depth_stencil
GL_EXT_packed_float
GL_EXT_packed_pixels
GL_EXT_pixel_buffer_object
GL_EXT_point_parameters
GL_EXT_polygon_offset_clamp
GL_EXT_provoking_vertex
GL_EXT_rescale_normal
GL_EXT_secondary_color
GL_EXT_semaphore
GL_EXT_semaphore_fd
GL_EXT_separate_specular_color
GL_EXT_shader_clock
GL_EXT_shader_framebuffer_fetch
GL_EXT_shader_framebuffer_fetch_non_coherent
GL_EXT_shader_integer_mix
GL_EXT_shader_samples_identical
GL_EXT_shadow_funcs
GL_EXT_stencil_two_side
GL_EXT_stencil_wrap
GL_EXT_subtexture
GL_EXT_texture
GL_EXT_texture3D
GL_EXT_texture_array
GL_EXT_texture_buffer_object
GL_EXT_texture_compression_dxt1
GL_EXT_texture_compression_latc
GL_EXT_texture_compression_rgtc
GL_EXT_texture_compression_s3tc
GL_EXT_texture_cube_map
GL_EXT_texture_edge_clamp
GL_EXT_texture_env_add
GL_EXT_texture_env_combine
GL_EXT_texture_env_dot3
GL_EXT_texture_filter_anisotropic
GL_EXT_texture_integer
GL_EXT_texture_lod_bias
GL_EXT_texture_object
GL_EXT_texture_rectangle
GL_EXT_texture_sRGB
GL_EXT_texture_sRGB_R8
GL_EXT_texture_sRGB_decode
GL_EXT_texture_shadow_lod
GL_EXT_texture_shared_exponent
GL_EXT_texture_snorm
GL_EXT_texture_storage
GL_EXT_texture_swizzle
GL_EXT_timer_query
GL_EXT_transform_feedback
GL_EXT_vertex_array
GL_EXT_vertex_array_bgra
GL_EXT_vertex_attrib_64bit
GL_IBM_multimode_draw_arrays
GL_IBM_rasterpos_clip
GL_IBM_texture_mirrored_repeat
GL_INGR_blend_func_separate
GL_INTEL_blackhole_render
GL_INTEL_performance_query
GL_INTEL_shader_atomic_float_minmax
GL_INTEL_shader_integer_functions2
GL_KHR_blend_equation_advanced
GL_KHR_blend_equation_advanced_coherent
GL_KHR_context_flush_control
GL_KHR_debug
GL_KHR_no_error
GL_KHR_parallel_shader_compile
GL_KHR_robust_buffer_access_behavior
GL_KHR_robustness
GL_KHR_shader_subgroup
GL_KHR_texture_compression_astc_ldr
GL_KHR_texture_compression_astc_sliced_3d
GL_MESA_framebuffer_flip_y
GL_MESA_pack_invert
GL_MESA_shader_integer_functions
GL_MESA_texture_const_bandwidth
GL_MESA_texture_signed_rgba
GL_MESA_window_pos
GL_NV_ES1_1_compatibility
GL_NV_alpha_to_coverage_dither_control
GL_NV_blend_square
GL_NV_compute_shader_derivatives
GL_NV_conditional_render
GL_NV_copy_depth_to_color
GL_NV_copy_image
GL_NV_depth_clamp
GL_NV_fog_distance
GL_NV_fragment_shader_interlock
GL_NV_half_float
GL_NV_light_max_exponent
GL_NV_packed_depth_stencil
GL_NV_primitive_restart
GL_NV_texgen_reflection
GL_NV_texture_barrier
GL_NV_texture_env_combine4
GL_NV_texture_rectangle
GL_OES_EGL_image
GL_OES_read_format
GL_S3_s3tc
GL_SGIS_generate_mipmap
GL_SGIS_texture_border_clamp
GL_SGIS_texture_edge_clamp
GL_SGIS_texture_lod
GL_SUN_multi_draw_arrays
```

### OpenGL ES extensions (167)

```text
GL_AMD_performance_monitor
GL_ANDROID_extension_pack_es31a
GL_ANGLE_pack_reverse_row_order
GL_ANGLE_texture_compression_dxt3
GL_ANGLE_texture_compression_dxt5
GL_APPLE_sync
GL_APPLE_texture_max_level
GL_EXT_EGL_image_storage
GL_EXT_EGL_image_storage_compression
GL_EXT_base_instance
GL_EXT_blend_func_extended
GL_EXT_blend_minmax
GL_EXT_buffer_storage
GL_EXT_clear_texture
GL_EXT_clip_control
GL_EXT_clip_cull_distance
GL_EXT_color_buffer_float
GL_EXT_color_buffer_half_float
GL_EXT_compressed_ETC1_RGB8_sub_texture
GL_EXT_conservative_depth
GL_EXT_copy_image
GL_EXT_debug_label
GL_EXT_demote_to_helper_invocation
GL_EXT_depth_clamp
GL_EXT_discard_framebuffer
GL_EXT_disjoint_timer_query
GL_EXT_draw_buffers
GL_EXT_draw_buffers_indexed
GL_EXT_draw_elements_base_vertex
GL_EXT_draw_instanced
GL_EXT_float_blend
GL_EXT_frag_depth
GL_EXT_geometry_point_size
GL_EXT_geometry_shader
GL_EXT_gpu_shader5
GL_EXT_instanced_arrays
GL_EXT_map_buffer_range
GL_EXT_memory_object
GL_EXT_memory_object_fd
GL_EXT_multi_draw_arrays
GL_EXT_multi_draw_indirect
GL_EXT_occlusion_query_boolean
GL_EXT_polygon_offset_clamp
GL_EXT_primitive_bounding_box
GL_EXT_read_format_bgra
GL_EXT_render_snorm
GL_EXT_robustness
GL_EXT_sRGB
GL_EXT_sRGB_write_control
GL_EXT_semaphore
GL_EXT_semaphore_fd
GL_EXT_separate_shader_objects
GL_EXT_shader_clock
GL_EXT_shader_framebuffer_fetch
GL_EXT_shader_framebuffer_fetch_non_coherent
GL_EXT_shader_group_vote
GL_EXT_shader_implicit_conversions
GL_EXT_shader_integer_mix
GL_EXT_shader_io_blocks
GL_EXT_shader_samples_identical
GL_EXT_shadow_samplers
GL_EXT_tessellation_point_size
GL_EXT_tessellation_shader
GL_EXT_texture_border_clamp
GL_EXT_texture_buffer
GL_EXT_texture_compression_bptc
GL_EXT_texture_compression_dxt1
GL_EXT_texture_compression_rgtc
GL_EXT_texture_compression_s3tc
GL_EXT_texture_compression_s3tc_srgb
GL_EXT_texture_cube_map_array
GL_EXT_texture_filter_anisotropic
GL_EXT_texture_format_BGRA8888
GL_EXT_texture_mirror_clamp_to_edge
GL_EXT_texture_norm16
GL_EXT_texture_query_lod
GL_EXT_texture_rg
GL_EXT_texture_sRGB_R8
GL_EXT_texture_sRGB_decode
GL_EXT_texture_shadow_lod
GL_EXT_texture_storage
GL_EXT_texture_storage_compression
GL_EXT_texture_type_2_10_10_10_REV
GL_EXT_texture_view
GL_EXT_unpack_subimage
GL_INTEL_blackhole_render
GL_INTEL_conservative_rasterization
GL_INTEL_performance_query
GL_KHR_blend_equation_advanced
GL_KHR_blend_equation_advanced_coherent
GL_KHR_context_flush_control
GL_KHR_debug
GL_KHR_no_error
GL_KHR_parallel_shader_compile
GL_KHR_robust_buffer_access_behavior
GL_KHR_robustness
GL_KHR_shader_subgroup
GL_KHR_texture_compression_astc_ldr
GL_KHR_texture_compression_astc_sliced_3d
GL_MESA_bgra
GL_MESA_framebuffer_flip_y
GL_MESA_sampler_objects
GL_MESA_shader_integer_functions
GL_MESA_texture_const_bandwidth
GL_NV_alpha_to_coverage_dither_control
GL_NV_compute_shader_derivatives
GL_NV_conditional_render
GL_NV_draw_buffers
GL_NV_fbo_color_attachments
GL_NV_fragment_shader_interlock
GL_NV_generate_mipmap_sRGB
GL_NV_image_formats
GL_NV_pack_subimage
GL_NV_pixel_buffer_object
GL_NV_read_buffer
GL_NV_read_depth
GL_NV_read_depth_stencil
GL_NV_read_stencil
GL_NV_shader_noperspective_interpolation
GL_NV_texture_barrier
GL_OES_EGL_image
GL_OES_EGL_image_external
GL_OES_EGL_image_external_essl3
GL_OES_EGL_sync
GL_OES_compressed_ETC1_RGB8_texture
GL_OES_copy_image
GL_OES_depth24
GL_OES_depth_texture
GL_OES_depth_texture_cube_map
GL_OES_draw_buffers_indexed
GL_OES_draw_elements_base_vertex
GL_OES_element_index_uint
GL_OES_fbo_render_mipmap
GL_OES_geometry_point_size
GL_OES_geometry_shader
GL_OES_get_program_binary
GL_OES_gpu_shader5
GL_OES_mapbuffer
GL_OES_packed_depth_stencil
GL_OES_primitive_bounding_box
GL_OES_required_internalformat
GL_OES_rgb8_rgba8
GL_OES_sample_shading
GL_OES_sample_variables
GL_OES_shader_image_atomic
GL_OES_shader_io_blocks
GL_OES_shader_multisample_interpolation
GL_OES_standard_derivatives
GL_OES_stencil8
GL_OES_surfaceless_context
GL_OES_tessellation_point_size
GL_OES_tessellation_shader
GL_OES_texture_3D
GL_OES_texture_border_clamp
GL_OES_texture_buffer
GL_OES_texture_cube_map_array
GL_OES_texture_float
GL_OES_texture_float_linear
GL_OES_texture_half_float
GL_OES_texture_half_float_linear
GL_OES_texture_npot
GL_OES_texture_stencil8
GL_OES_texture_storage_multisample_2d_array
GL_OES_texture_view
GL_OES_vertex_array_object
GL_OES_vertex_half_float
GL_OES_viewport_array
```

### VA-API profiles and entry points

```text
VAProfileNone                   :	VAEntrypointVideoProc
VAProfileNone                   :	VAEntrypointStats
VAProfileMPEG2Simple            :	VAEntrypointVLD
VAProfileMPEG2Simple            :	VAEntrypointEncSlice
VAProfileMPEG2Main              :	VAEntrypointVLD
VAProfileMPEG2Main              :	VAEntrypointEncSlice
VAProfileH264Main               :	VAEntrypointVLD
VAProfileH264Main               :	VAEntrypointEncSlice
VAProfileH264Main               :	VAEntrypointFEI
VAProfileH264Main               :	VAEntrypointEncSliceLP
VAProfileH264High               :	VAEntrypointVLD
VAProfileH264High               :	VAEntrypointEncSlice
VAProfileH264High               :	VAEntrypointFEI
VAProfileH264High               :	VAEntrypointEncSliceLP
VAProfileVC1Simple              :	VAEntrypointVLD
VAProfileVC1Main                :	VAEntrypointVLD
VAProfileVC1Advanced            :	VAEntrypointVLD
VAProfileJPEGBaseline           :	VAEntrypointVLD
VAProfileJPEGBaseline           :	VAEntrypointEncPicture
VAProfileH264ConstrainedBaseline:	VAEntrypointVLD
VAProfileH264ConstrainedBaseline:	VAEntrypointEncSlice
VAProfileH264ConstrainedBaseline:	VAEntrypointFEI
VAProfileH264ConstrainedBaseline:	VAEntrypointEncSliceLP
VAProfileVP8Version0_3          :	VAEntrypointVLD
VAProfileVP8Version0_3          :	VAEntrypointEncSlice
VAProfileHEVCMain               :	VAEntrypointVLD
VAProfileHEVCMain               :	VAEntrypointEncSlice
VAProfileHEVCMain               :	VAEntrypointFEI
VAProfileHEVCMain10             :	VAEntrypointVLD
VAProfileHEVCMain10             :	VAEntrypointEncSlice
VAProfileVP9Profile0            :	VAEntrypointVLD
VAProfileVP9Profile2            :	VAEntrypointVLD
```

