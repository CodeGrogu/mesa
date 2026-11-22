# Gen9 (HD 620 / Kaby Lake GT2) Intel compiler sweep

Read-only sweep of `src/intel/compiler/` in the Mesa checkout. Locations are relative to `mesa/`. Gen9 (devinfo->ver == 9) compiles through the `brw` backend (section 1).

## 1. Backend selection for Gen9

Answer: **brw**. `elk` is the pre-Gen9 backend. Iris uses it only in the `else` of `ver >= 9`, and hasvk and crocus use it for Gen8 and earlier. There is no `ver < 9` check in iris; the split is `ver >= 9`. `src/intel/vulkan/anv_pipeline.c` does not exist in this checkout; the hasvk compile call is in `src/intel/vulkan_hasvk/anv_pipeline.c`.

| Location | Quote | Note |
|---|---|---|
| `src/intel/compiler/brw/brw_compiler.c:87` | `assert(devinfo->ver >= 9);` | brw backend floor: brw_compiler_create asserts ver >= 9. |
| `src/gallium/drivers/iris/iris_program.c:4577` | `if (screen->devinfo->ver >= 9) {` | Iris picks brw for ver >= 9. The split is this >= 9 test; there is no ver < 9 check in iris. Gen9 takes this branch. |
| `src/gallium/drivers/iris/iris_program.c:4580` | `screen->brw = brw_compiler_create(screen, screen->devinfo);` | Gen9 compiler object in iris is brw. |
| `src/gallium/drivers/iris/iris_program.c:4590` | `screen->elk = elk_compiler_create(screen, screen->devinfo);` | elk is created only in the else branch, i.e. ver < 9. |
| `src/gallium/drivers/iris/iris_program.c:4597` | `UNREACHABLE("no elk support");` | Inside iris_compiler_init: builds without INTEL_USE_ELK hit this for ver < 9. |
| `src/intel/compiler/meson.build:58` | `subdir('brw')` | brw is built unconditionally. |
| `src/intel/compiler/meson.build:61` | `if with_intel_elk` | elk is built only when with_intel_elk is set. |
| `src/intel/meson.build:7` | `with_intel_elk = get_option('intel-elk')` | Option is declared in meson.options:737 with value : true (default on). |
| `src/intel/vulkan/anv_physical_device.c:3320` | `device->compiler = brw_compiler_create(NULL, &device->info);` | Main anv always uses brw at this call; no version split here. |
| `src/intel/vulkan/anv_physical_device.c:3120` | `if (devinfo.ver < 9) {` | Main anv rejects ver < 9 and defers to hasvk. Gen9 passes and stays on brw. |
| `src/intel/vulkan_hasvk/anv_device.c:1797` | `device->compiler = elk_compiler_create(NULL, &device->info);` | hasvk (Gen8-and-earlier Vulkan) uses elk. |
| `src/intel/vulkan_hasvk/anv_pipeline.c:889` | `fs_stage->code = elk_compile_fs(compiler, &params);` | hasvk compiles through elk, consistent with elk being the pre-Gen9 path. |
| `src/gallium/drivers/crocus/crocus_screen.c:621` | `screen->compiler = elk_compiler_create(screen, &screen->devinfo);` | crocus (Gen4-8 GL driver) uses elk. |
| `src/intel/dev/intel_device_info.c:553` | `.ver = 9,                                        \` | GFX9_FEATURES defines ver 9 (first match, line 553); KBL resolves to ver 9 and therefore brw. |

## 2. Gen9-specific code in the brw backend

| Location | Quote | Note |
|---|---|---|
| `src/intel/compiler/brw/brw_compiler.c:156` | `if (devinfo->ver > 9)` | Gen9 does not enable nir_lower_imul_2x32_64, so 64-bit imul takes the generic int64 lowering. |
| `src/intel/compiler/brw/brw_compiler.c:178` | `nir_options->lower_flrp32 = devinfo->ver >= 11;` | LRP lowering only on Gen11+; Gen9 keeps native LRP. |
| `src/intel/compiler/brw/brw_compiler.c:183` | `nir_options->has_rotate32 = devinfo->ver >= 11;` | No native rotate on Gen9; rotates are lowered in NIR. |
| `src/intel/compiler/brw/brw_compiler.c:186` | `nir_options->has_sdot_4x8 = devinfo->ver >= 12;` | No native dot4 in NIR on Gen9; dot products go through lowering (DP4A/MUL-ADD emulation path). |
| `src/intel/compiler/brw/brw_compiler.c:196` | `nir_options->avoid_ternary_with_two_constants = devinfo->ver < 20;` | Applies to every brw gen below Xe2, Gen9 included. |
| `src/intel/compiler/brw/brw_compiler.c:210` | `if (devinfo->ver < 12)` | Pre-Gen12 FS divergence analysis assumes one primitive per FS subgroup (Gen9 included). |
| `src/intel/compiler/brw/brw_compiler.c:257` | `devinfo->verx10 == 90);` | Gen9 (verx10 90) is named in the lowered-storage-format assert; Gen9 needs no lowered storage formats. |
| `src/intel/compiler/brw/brw_nir.c:2680` | `!(devinfo->ver >= 10 \|\| devinfo->platform == INTEL_PLATFORM_KBL))` | Precise-trig workaround runs on Gen9 except KBL. HD 620 is KBL, so it skips this. Confirm the exemption is intended. |
| `src/intel/compiler/brw/brw_nir.c:326` | `if (devinfo->ver <= 11 && !nir_def_is_const(offset)) {` | Pre-Gen12 URB: base folded into dynamic offsets (extra ALU per dynamic URB access on Gen9). |
| `src/intel/compiler/brw/brw_nir.c:1005` | `if (devinfo->ver < 12) {` | Legacy tess-level patch layout for pre-Gen12, including Gen9. |
| `src/intel/compiler/brw/brw_nir.c:2031` | `if (devinfo->ver >= 11)` | nir_lower_interpolation is Gen11+ only; Gen9 skips it. |
| `src/intel/compiler/brw/brw_nir.c:2830` | `OPT(nir_lower_indirect_derefs_to_if_else_trees, nir_var_function_temp, 16);` | Indirect function temporaries under 16 elements become if/else trees on all gens, Gen9 included. |
| `src/intel/compiler/brw/brw_nir.c:2861` | `.lower_txs_lod = true, /* Wa_14012320009 */` | Wa_14012320009 applied with no version gate in this struct, so Gen9 included. |
| `src/intel/compiler/brw/brw_nir.c:4297` | `case nir_atomic_op_fadd: return LSC_OP_ATOMIC_FADD;` | Float atomic add maps to an LSC opcode with no version gate here. Gen9 has no LSC; confirm Gen9 never reaches this for fadd. |
| `src/intel/compiler/brw/brw_nir_lower_texture.c:42` | `if (devinfo->ver == 9) {` | Gen9-only Wa_1940217: resinfo MIPCount forced to 0 for SURFTYPE_NULL. |
| `src/intel/compiler/brw/brw_from_nir.cpp:2809` | `brw_imm_ud(devinfo->ver == 9 ? 0x8f000000u : 0x7f000000u);` | Gen9-specific compute barrier ID mask (0x8f000000 on Gen9, 0x7f000000 otherwise below Gfx12.5). |
| `src/intel/compiler/brw/brw_from_nir.cpp:5156` | `devinfo->ver == 9;` | Gen9 forces commit_enable on every fence, citing simulator complaints. Extra cost on all Gen9 fences. |
| `src/intel/compiler/brw/brw_from_nir.cpp:5173` | `bool force_stall = devinfo->ver >= 11;` | Forced stall is Gen11+ only; Gen9 does not get it. |
| `src/intel/compiler/brw/brw_lower_simd_width.cpp:102` | `if (inst->conditional_mod && inst->is_3src(compiler) && devinfo->ver < 12)` | Gen9 (ver < 12): 3-source instructions with conditional mod capped at SIMD16. |
| `src/intel/compiler/brw/brw_lower_simd_width.cpp:421` | `devinfo->ver < 11 && brw_type_size_bytes(inst->src[0].type) == 4 ? 8 :` | Pre-Gen11: QUAD_SWIZZLE on 32-bit non-uniform sources forced to SIMD8. |
| `src/intel/compiler/brw/brw_lower_simd_width.cpp:125` | `if (is_mixed_float_with_fp32_dst(inst) && devinfo->ver < 20)` | Mixed-mode float with f32 destination capped at SIMD8 on Gen9 (SKL PRM restriction). |
| `src/intel/compiler/brw/brw_lower_simd_width.cpp:134` | `if (is_mixed_float_with_packed_fp16_dst(inst) && devinfo->ver < 20)` | Mixed-mode float with packed f16 destination capped at SIMD8 on Gen9 (half-float restriction). |
| `src/intel/compiler/brw/brw_lower_simd_width.cpp:113` | `if (has_bfloat_operands(inst))` | Bfloat operands capped at SIMD8 below Gen20. Gen9 has no bfloat16, so this is for completeness. |
| `src/intel/compiler/brw/brw_lower.cpp:136` | `supported = devinfo->ver >= 11;` | Native CSEL for HF/W/D only on Gen11+; Gen9 lowers these select forms. |
| `src/intel/compiler/brw/brw_lower_logical_sends.cpp:550` | `assert(devinfo->ver >= 9 && devinfo->ver < 20);` | Explicit Gen9 floor for this sends lowering path. |
| `src/intel/compiler/brw/brw_lower_logical_sends.cpp:816` | `assert(payload_type_bit_size != 16 \|\| devinfo->ver >= 11);` | 16-bit (half-float) message payloads are Gen11+ only; on Gen9 they must be widened to 32-bit. |
| `src/intel/compiler/brw/brw_lower_logical_sends.cpp:904` | `} else if (devinfo->ver < 11 &&` | Pre-Gen11 sampler payload layout selected here; affects texture sends on Gen9. |
| `src/intel/compiler/brw/brw_lower_integer_multiplication.cpp:243` | `const bool source_mods_unsupported = (devinfo->ver >= 12);` | DW multiply src1 source modifiers are unsupported only on Gen12+; Gen9 does not need the pre-lowering. |
| `src/intel/compiler/brw/brw_opt_mac.cpp:655` | `/* Gfx9 cannot have accumulator as any source of MAD. If this` | Gen9-only: MAD cannot use accumulator sources, so MAD to MAC fusion is blocked for every source. |
| `src/intel/compiler/brw/brw_opt_mac.cpp:503` | `} else if (s.devinfo->verx10 == 90 && inst->dst.type == BRW_TYPE_DF) {` | Gen9 double-precision MAC special case, based on experimental determination (no PRM citation). |
| `src/intel/compiler/brw/brw_compile_fs.cpp:1095` | `if (devinfo->ver == 9)` | Gen9-only gfx9_ps_header_only_workaround (SKL PRM Workarounds 0877): adds a varying when PS is header-only. |
| `src/intel/compiler/brw/brw_compile_fs.cpp:730` | `devinfo->ver < 11) {` | Coarse pixel dispatch disabled on pre-Gen11 (Gen9 included). |
| `src/intel/compiler/brw/brw_compile_fs.cpp:1204` | `devinfo->ver >= 11 && devinfo->ver <= 12)` | Wa_14017468336 (dual-source SIMD32 hang) gated to Gen11-12 only; Gen9 gets no SIMD8 limit here. |
| `src/intel/compiler/brw/brw_eu.h:1040` | `return devinfo->ver >= 9 && devinfo->ver < 20;` | Coherent framebuffer fetch supported on Gen9 through Gen19. |
| `src/intel/compiler/brw/brw_reg_allocate.cpp:840` | `if (devinfo->ver < 10)` | Gen9-only grf127 send hack: reserves an extra RA node so r127 is not used as a send return address. |
| `src/intel/compiler/brw/brw_opt_bank_conflicts.cpp:527` | `constrained[p.atom_of_reg(127)] = v->devinfo->ver < 10;` | Gen9-only: bank-conflict pass constrains the r127 atom (paired with the grf127 send hack). |
| `src/intel/compiler/brw/brw_shader.cpp:134` | `if (devinfo->ver < 11)` | Pre-Gen11 "do not dereference URB" descriptor bit set on Gen9 for URB-handle threads. |
| `src/intel/compiler/brw/brw_lower_dpas.cpp:317` | `if (v.devinfo->ver >= 12) {` | DPAS int8 path: Gen12+ uses DP4A; Gen9 falls back to int8_using_mul_add. |
| `src/intel/compiler/brw/brw_lower_dpas.cpp:241` | `* FINISHME: Gfx9 should not need this work around. Gfx11` | Gen9-specific FINISHME: the packed-byte to word expansion for DPAS emulation is said to be unnecessary on Gfx9. |
| `src/intel/compiler/brw/brw_simd_selection.cpp:159` | `if (width == 32 && state.devinfo->ver < 20 &&` | SIMD32 rule for ver < 20 (includes Gen9). Hand-written heuristic; see TODO in section 3. |
| `src/intel/compiler/brw/brw_reg_allocate.cpp:483` | `/* FINISHME - On Gfx7+ it should be possible to avoid this limit` | Spill-temporary limit on the non-LSC path (Gen9 has no LSC), so this limit applies. |
| `src/intel/compiler/brw/brw_nir.c:3319` | `if (devinfo->ver < 35 && align < 4 && bytes > align && is_load) {` | Misaligned loads split for ver < 35. Comment describes an LSC slow path; Gen9 applicability not verified. |
| `src/intel/compiler/brw/brw_workaround.cpp:310` | `* predicate any NoMask SEND instructions we find under control` | Conservative predication of NoMask SENDs under control flow; Gen9 gating not verified. |
| `src/intel/compiler/brw/brw_workaround.cpp:163` | `* TODO/FINISHME: According to Curro we could avoid the fence in some cases.` | Dummy UGM fence before EOT is added with a weak criterion; applies to Gen9 shaders that write UGM (gating not verified). |

## 3. TODO / FIXME / XXX / HACK in the brw backend

Grep: `grep -rn -E "TODO|FIXME|XXX|HACK" src/intel/compiler/brw`. `XXXX` swizzle names (for example `BRW_SWIZZLE_XXXX`) are not markers and are excluded. The `XXX` cost-table lines in `brw_analysis_performance.cpp` are collapsed into one row. "(Gen9)" marks only items where the comment or code path touches pre-Gen11 hardware.

| Location | Quote | Note |
|---|---|---|
| `src/intel/compiler/brw/brw_builder.h:443` | `/* FIXME: We use a vector chan_index and dst to allow constant and` | General builder note. |
| `src/intel/compiler/brw/brw_builder.h:899` | `* components starting from that.  TODO: Support loading fewer than 4.` | General backend item; Gen9 applicability not checked. |
| `src/intel/compiler/brw/brw_compile_bs.cpp:31` | `/* TODO(RT): Perhaps rename this? */` | RT; not Gen9. |
| `src/intel/compiler/brw/brw_compile_bs.cpp:133` | `/* TODO: Figure out shader stats etc. for resume shaders */` | RT resume shaders; not Gen9. |
| `src/intel/compiler/brw/brw_compile_fs.cpp:1195` | `* XXX - Emit an extra single-source NULL RT-write marked LastRT in` | (Gen9) Dual-source SIMD32 workaround gated to Gen11-12 (brw_compile_fs.cpp:1204), so Gen9 gets no such limit. |
| `src/intel/compiler/brw/brw_compile_mesh.cpp:23` | `/* TODO(mesh): Make this a common function. */` | Mesh; not Gen9. |
| `src/intel/compiler/brw/brw_compile_mesh.cpp:579` | `vue_layout, 1 /* pos_slots, TODO: multiview */);` | Multiview / position-slot item; general, not Gen9-specific. |
| `src/intel/compiler/brw/brw_compile_mesh.cpp:630` | `/* TODO(mesh): can we write in bigger batches, generating fewer SENDs? */` | Mesh; not Gen9. |
| `src/intel/compiler/brw/brw_compile_mesh.cpp:787` | `* and could actually decrease performance. TODO: reevaluate this` | Mesh; not Gen9. |
| `src/intel/compiler/brw/brw_compile_mesh.cpp:884` | `* TODO: improve this` | Mesh; not Gen9. |
| `src/intel/compiler/brw/brw_eu.h:158` | `/* TODO: optimization that can be enabled later */` | General EU encoding note. |
| `src/intel/compiler/brw/brw_eu.h:164` | `/* TODO: support feedback message */` | General EU encoding note. |
| `src/intel/compiler/brw/brw_from_nir.cpp:2572` | `* TODO: If vertex_count is an immediate, we could do some of this math` | General geometry-output math; not Gen9-specific. |
| `src/intel/compiler/brw/brw_from_nir.cpp:3445` | `* TODO: These payload bits exist on Gfx7 too, but they appear to always` | (Gen9) Payload bits exist on Gfx7+ (Gen9 included); comment says they always-set. Check Gen9 header content. |
| `src/intel/compiler/brw/brw_from_nir.cpp:3842` | `/* TODO(mesh): Multiview. Verify and handle these special cases for Mesh. */` | Mesh; not Gen9. |
| `src/intel/compiler/brw/brw_from_nir.cpp:5028` | `* TODO: Check if applies for many HW threads sharing same Data Port.` | Data-port sharing question; general (Gen9 uses HDC data port). |
| `src/intel/compiler/brw/brw_from_nir.cpp:5088` | `* XXX - Use an alternative workaround on IVB and ICL, since` | (Gen9) Pre-Gen12 path: comment says the alternative needs Gfx12+, so Gen9 keeps the older path. |
| `src/intel/compiler/brw/brw_from_nir.cpp:5170` | `* TODO: Use scope and visibility information for the barriers from NIR` | General barrier/fence scope item; all gens with barriers, Gen9 included. |
| `src/intel/compiler/brw/brw_from_nir.cpp:6294` | `/* TODO: make sure sources have consistent bit sizes */` | General assertion-level TODO. |
| `src/intel/compiler/brw/brw_lower.cpp:858` | `/* XXX - The equation above is strictly speaking not correct on` | General lowering note. |
| `src/intel/compiler/brw/brw_lower_logical_sends.cpp:508` | `/* XXX - Bit 13 Per-sample PS enable */` | (Gen9) PS send header on the pre-Gen11 path (brw_lower_logical_sends.cpp:335). |
| `src/intel/compiler/brw/brw_lower_logical_sends.cpp:845` | `* TODO: implement feedback surface support if needed` | General sends lowering; feedback surface not implemented. |
| `src/intel/compiler/brw/brw_lower_logical_sends.cpp:2039` | `/* XXX - There is a Registers Per Thread field in the BTD spawn` | BTD spawn; not Gen9. |
| `src/intel/compiler/brw/brw_lower_logical_sends.cpp:2267` | `send->src[SENDG_SRC_IND_1_DESC] = brw_reg(); /* TODO: fish out the PostSyncId, coming from the payload? */` | BTD spawn; not Gen9. |
| `src/intel/compiler/brw/brw_lower_regioning.cpp:668` | `/* FIXME: We assume the opcodes not explicitly mentioned before just` | General lower_regioning note; Gen9 runs this pass. |
| `src/intel/compiler/brw/brw_lower_scoreboard.cpp:1428` | `/* XXX - Use bin-packing algorithm to assign hardware SBIDs optimally in` | (Gen9) SBID assignment on the SWSB scoreboard, which Gen9 uses. |
| `src/intel/compiler/brw/brw_lower_scoreboard.cpp:1431` | `* XXX - Use 32 SBIDs on Xe2 while in large GRF mode.` | Xe2 large-GRF only; not Gen9. |
| `src/intel/compiler/brw/brw_lower_subgroup_ops.cpp:449` | `* TODO: Check if we still need this for newer platforms.` | (Gen9) Asks whether the workaround is still needed on newer platforms; Gen9 is on the older side. |
| `src/intel/compiler/brw/brw_nir.c:67` | `unsigned stride = 4 /* TODO */;` | General backend item; Gen9 applicability not checked. |
| `src/intel/compiler/brw/brw_nir.c:2649` | `/* TODO: This is part of the "pre-processing" before the shader is fed to` | General NIR pre-processing note. |
| `src/intel/compiler/brw/brw_nir.c:2806` | `/* TODO: jay supports quad broadcast and should(?) do swaphorizontal */` | Jay (Xe2+) comment in brw NIR preprocess; not a Gen9 item. |
| `src/intel/compiler/brw/brw_nir.c:3038` | `/* TODO: This is part of the "pre-processing" before the shader is fed to` | General NIR pre-processing note. |
| `src/intel/compiler/brw/brw_nir.c:3224` | `* TODO: While HW has only ACQUIRE\|RELEASE fences, we could improve the` | General fence-merge note; applies to all gens including Gen9. |
| `src/intel/compiler/brw/brw_nir.c:3336` | `* XXX - Use W/A framework for decision when W/A number is` | General workaround-framework note; Gen9 applies. |
| `src/intel/compiler/brw/brw_nir.c:3922` | `/* TODO: Unify this with the logic in jay_prog_data somehow */` | General; jay/brw program data unification. |
| `src/intel/compiler/brw/brw_nir_lower_cooperative_matrix.c:1037` | `* TODO: Cooperative matrix inside struct?` | RT/mesh/Xe-only file; not Gen9. |
| `src/intel/compiler/brw/brw_nir_lower_cs_intrinsics.c:340` | `/* TODO: more heuristics about 1D/SLM access vs. 2D access */` | (Gen9) Compute intrinsic lowering heuristic applies to Gen9 compute shaders. |
| `src/intel/compiler/brw/brw_nir_lower_immediate_offsets.c:126` | `* TODO: better predict what types are going to be used?` | General immediate-offset heuristic; Gen9 runs this pass. |
| `src/intel/compiler/brw/brw_nir_opt_systolic_vectorize.c:119` | `* XXX - Allow memory load intrinsics other than SSBOs.` | RT/mesh/Xe-only file; not Gen9. |
| `src/intel/compiler/brw/brw_nir_opt_systolic_vectorize.c:296` | `* XXX - This implements the simplest possible check that is effective` | RT/mesh/Xe-only file; not Gen9. |
| `src/intel/compiler/brw/brw_nir_opt_systolic_vectorize.c:555` | `* XXX - Check if dpas.8x1 could be beneficial in cases where we` | RT/mesh/Xe-only file; not Gen9. |
| `src/intel/compiler/brw/brw_nir_opt_systolic_vectorize.c:708` | `* XXX - Allow other dot product intrinsics.` | RT/mesh/Xe-only file; not Gen9. |
| `src/intel/compiler/brw/brw_nir_opt_systolic_vectorize.c:853` | `/* XXX - Enable pre-Xe2. */` | RT/mesh/Xe-only file; not Gen9. |
| `src/intel/compiler/brw/brw_nir_opt_systolic_vectorize.c:869` | `/* XXX - The matrix multiplication arithmetic emitted by this` | RT/mesh/Xe-only file; not Gen9. |
| `src/intel/compiler/brw/brw_nir_rt_builder.h:84` | `/* TODO: This could be handled by nir_opt_algebraic (its not atm) */` | RT builder; not Gen9. |
| `src/intel/compiler/brw/brw_nir_rt_builder.h:118` | `/* TODO: This could be handled by nir_opt_algebraic (its not atm) */` | RT builder; not Gen9. |
| `src/intel/compiler/brw/brw_opt.cpp:759` | `* TODO: Pass LSC address length or infer it so valid splits can work.` | LSC-only (Gen12+); not Gen9. |
| `src/intel/compiler/brw/brw_opt_bank_conflicts.cpp:868` | `/* TODO: Re-work this pass for Gfx20+. */` | Pass bails only for ver >= 20; runs on Gen9. |
| `src/intel/compiler/brw/brw_reg.h:190` | `/* TODO: Remove this constructor to make this type a POD.  Need` | General brw_reg note; not Gen9-specific. |
| `src/intel/compiler/brw/brw_reg.h:1360` | `* XXX - Handle vector immediates correctly.` | General brw_reg note. |
| `src/intel/compiler/brw/brw_reg_allocate.cpp:251` | `* XXX - Update for xe3p when 512 GRF mode is enabled.` | General backend item; Gen9 applicability not checked. |
| `src/intel/compiler/brw/brw_schedule_instructions.cpp:255` | `/* TODO: 64bit */` | (Gen9) Scheduler does not model 64-bit specifics; Gen9 has native 64-bit int and float. |
| `src/intel/compiler/brw/brw_schedule_instructions.cpp:357` | `/* TODO: find the correct value, just got largest latency from` | (Gen9) Scheduler latency for one op is guessed; affects Gen9 scheduling. |
| `src/intel/compiler/brw/brw_schedule_instructions.cpp:499` | `latency = 50; /* TODO */` | (Gen9) Scheduler latency placeholder used by Gen9 scheduling. |
| `src/intel/compiler/brw/brw_schedule_instructions.cpp:551` | `/* TODO.` | General backend item; Gen9 applicability not checked. |
| `src/intel/compiler/brw/brw_shader.cpp:486` | `/* TODO(mesh): Review usage of this in the context of Mesh, we may want to` | Mesh; not Gen9. |
| `src/intel/compiler/brw/brw_shader.cpp:594` | `/* TODO: Add replacement for INTEL_SHADER_OPTIMIZER_PATH. */` | General debug/tooling item. |
| `src/intel/compiler/brw/brw_shader.cpp:906` | `* TODO: Change the passes above, then move this lowering to be part of` | General lowering-order note; Gen9 applies. |
| `src/intel/compiler/brw/brw_simd_selection.cpp:157` | `* TODO: Use performance_analysis and drop this rule.` | (Gen9) SIMD32 rule (ver < 20) is a fixed heuristic; a cost model could replace it. |
| `src/intel/compiler/brw/brw_thread_payload.cpp:329` | `/* TODO: Fill out uses_btd_stack_ids automatically */` | BTD/RT only; not Gen9. |
| `src/intel/compiler/brw/brw_to_binary.cpp:2083` | `/* TODO: Consider syncing with any SBIDs that were added above. */` | SWSB scheduling note; Gen9 applicability not checked. |
| `src/intel/compiler/brw/brw_to_binary.cpp:2098` | `/* TODO: See if we can move those normalizations elsewhere. */` | General brw_to_binary note. |
| `src/intel/compiler/brw/brw_to_binary.cpp:2412` | `* XXX: SIMD8 is not the smallest on Xe2. This logic should be adjusted.` | General backend item; Gen9 applicability not checked. |
| `src/intel/compiler/brw/brw_to_binary.cpp:2471` | `/* TODO: Check if we really need this padding at the end. */` | General program padding; Gen9 included. |
| `src/intel/compiler/brw/brw_vue_map.c:333` | `/* TODO: consider only using a single layout (just allowing position` | General backend item; Gen9 applicability not checked. |
| `src/intel/compiler/brw/brw_workaround.cpp:163` | `* TODO/FINISHME: According to Curro we could avoid the fence in some cases.` | (Gen9) Dummy UGM fence before EOT; criterion admitted to be weak. Gen9 applicability not verified. |
| `src/intel/compiler/brw/brw_workaround.cpp:211` | `/* TODO: remove this break if we ever have shader with multiple EOT. */` | General backend item; Gen9 applicability not checked. |
| `src/intel/compiler/brw/test_helpers.h:104` | `/* TODO: Once brw_shader is ralloc-able, we can simply get rid of this. */` | General backend item; Gen9 applicability not checked. |
| `src/intel/compiler/brw/brw_analysis_performance.cpp:287` | `* Parameters marked XXX shall be considered low-quality, they're possibly` | (Gen9) Comment flags the XXX cost constants as low quality. 108 XXX lines in this file (first at line 287) feed the perf model that drives SIMD and scheduling choices. |

## 4. Performance notes

| Location | Quote | Note |
|---|---|---|
| `src/intel/compiler/brw/brw_nir.c:2820` | `* The threshold of 16 was chosen semi-arbitrarily.  The idea is that an` | Indirect-temp threshold of 16 is a heuristic; the comment says it was chosen semi-arbitrarily. |
| `src/intel/compiler/brw/brw_nir.c:3120` | `/* nir_lower_idiv generates expensive code */` | Workgroup-ID shortcut only for verx10 >= 125; Gen9 pays for expensive idiv in compute workgroup-ID math. |
| `src/intel/compiler/brw/brw_nir.c:3045` | `/* Code motion is expensive and linking runs the optimization loop up to` | Link-time optimization loop is said to be expensive; code motion is skipped there to save time. |
| `src/intel/compiler/brw/brw_nir.c:2327` | `* low. Therefore there shouldn't be a performance benefit to avoid it.` | Uniform indirect loads: peephole-select limit of 8 chosen on the assumption that the load is cheap. |
| `src/intel/compiler/brw/brw_nir.c:4440` | `* This should be replaced by global value numbering someday.` | CSE is a stand-in for global value numbering (noted as future work). |
| `src/intel/compiler/brw/brw_nir.c:2686` | `/* This workaround existing for performance reasons. Since it requires not` | Image array-size workaround trades robustness for performance; skipped when robust image access is on. |
| `src/intel/compiler/brw/brw_nir.c:2836` | `* optimizer to combine UBO and SSBO load operations and save us some send` | Vec4 UBO/SSBO load combining is intended to cut send messages; depends on full-load alignment. |
| `src/intel/compiler/brw/brw_nir.c:2817` | `* it can still be expensive.  Lower indirects on small arrays to` | Indirect array access is expensive; small arrays are turned into conditional loads and stores. |
| `src/intel/compiler/brw/brw_workaround.cpp:308` | `* where this could easily lead to GPU hangs).  Unfortunately we` | NoMask SEND predication is conservative; comment says a whitelist could remove it. |
| `src/intel/compiler/brw/brw_workaround.cpp:313` | `* If this proves to have a measurable performance impact it can` | Predication of NoMask sends may cost performance; a whitelist is proposed. |
| `src/intel/compiler/brw/brw_workaround.cpp:393` | `* To avoid emitting MOVs when it's not needed, check if each block  reads all` | Dummy MOV insertion for a workaround is done conservatively per block. |
| `src/intel/compiler/brw/brw_schedule_instructions.cpp:22` | `* The heuristic is the important part.  We're trying to be cheap,` | Scheduler is intentionally cheap; the heuristic choice drives performance. |
| `src/intel/compiler/brw/brw_schedule_instructions.cpp:1733` | `/* Before register allocation, we don't care about the latencies of` | Pre-RA scheduling ignores latencies and only shortens live ranges; can hurt latency hiding. |
| `src/intel/compiler/brw/brw_schedule_instructions.cpp:994` | `* we could end up with scheduling deadlocks.` | Dependency choices are constrained to avoid scheduling deadlocks. |
| `src/intel/compiler/brw/brw_shader.cpp:713` | `/* Try each scheduling heuristic to choose the one one with the` | Multiple scheduler modes tried per shader; compile-time vs quality tradeoff. |
| `src/intel/compiler/brw/brw_shader.cpp:784` | `/* The latency-sensitive heuristic is unlikely to be helpful` | Latency-sensitive scheduler mode skipped in some low-performance cases. |
| `src/intel/compiler/brw/brw_shader.cpp:813` | `/* We should only spill registers on the last scheduling. */` | Spills only allowed in the final scheduling pass. |
| `src/intel/compiler/brw/brw_shader.cpp:730` | `/* This one doesn't appear to provide much benefit on xe3+.` | Comment says this mode has little benefit on Xe3+; the PRE-mode gating (ver < 30) applies to Gen9. |
| `src/intel/compiler/brw/brw_reg_allocate.cpp:1363` | `/* Divide the cost (in number of spills/fills) by the log of the length` | Spill cost = cost / log(live length); a heuristic, not measured. |
| `src/intel/compiler/brw/brw_reg_allocate.cpp:1541` | `* could just spill/unspill the GRF being accessed.` | Spills and fills the whole VGRF rather than the accessed GRF; extra scratch traffic. |
| `src/intel/compiler/brw/brw_reg_allocate.cpp:238` | `/* g0 is needed to construct scratch headers for spilling.  While we could` | g0 kept live for spill headers; extending its live range per spill is noted as an option. |
| `src/intel/compiler/brw/brw_opt_fill_spill.cpp:39` | `/* It would be great if these could be checked for equality, but` | Spill/fill optimization compares locations, not values; conservative. |
| `src/intel/compiler/brw/brw_opt_fill_spill.cpp:308` | `/* This limitation is necessary because (currently) a spill may` | Spill/fill optimization limited to avoid touching lowered spill sequences. |
| `src/intel/compiler/brw/brw_simd_selection.cpp:155` | `/* The SIMD32 is only enabled for cases it is needed unless forced.` | SIMD32 enabled only when needed; a fixed rule instead of a cost model. |
| `src/intel/compiler/brw/brw_simd_selection.cpp:186` | `/* If a SIMD spilled, all the larger ones would spill too. */` | If a SIMD width spills, larger widths are marked spilled without trying. |
| `src/intel/compiler/brw/brw_lower_dpas.cpp:40` | `* there are two accumulators. It should be possible to do this in` | DPAS emulation could use two accumulators; the comment says this should be possible. |
| `src/intel/compiler/brw/brw_lower_simd_width.cpp:123` | `* Testing indicates that this restriction does not apply to MOVs.` | Mixed-float SIMD8 cap is not applied to MOVs (tested); relevant when reviewing the cap. |
| `src/intel/compiler/brw/brw_opt_cmod_propagation.cpp:309` | `* supported. Perhaps this could be enabled later.` | Conditional-mod propagation skipped for an unsupported case; may be enabled later. |
| `src/intel/compiler/brw/brw_to_binary.cpp:844` | `* pair.  But the ideal approximation may impose a huge performance cost on` | DDX/DDY approximation: the ideal mapping would hurt sample_d; the chosen layout is a compromise. |
| `src/intel/compiler/brw/brw_to_binary.cpp:520` | `* makes the base offset almost useless.  We could try and do something` | Base-offset handling could be improved (comment). |

## Top 15 candidates (Gen9 shader performance or correctness)

1. `src/intel/compiler/brw/brw_nir.c:2680`: Correctness: precise-trig workaround (when precise_trig is set) is skipped on KBL (HD 620) but applied on other Gen9 parts; confirm the exemption is intended.
2. `src/intel/compiler/brw/brw_compile_fs.cpp:1204`: Correctness: dual-source SIMD32 hang workaround (Wa_14017468336) is gated to Gen11-12; check whether Gen9 needs it.
3. `src/intel/compiler/brw/brw_nir.c:4297`: Correctness check: float atomic-add mapping to an LSC opcode has no version gate; confirm Gen9 (no LSC) never takes it for fadd.
4. `src/intel/compiler/brw/brw_reg_allocate.cpp:1541`: Perf: spills and fills the whole VGRF instead of the accessed GRF; per-access spilling could cut scratch traffic on Gen9.
5. `src/intel/compiler/brw/brw_reg_allocate.cpp:483`: Perf: spill-temporary limit on the non-LSC path (Gen9) is marked avoidable by the FINISHME.
6. `src/intel/compiler/brw/brw_lower_simd_width.cpp:421`: Perf: 32-bit non-uniform QUAD_SWIZZLE forced to SIMD8 before Gen11; a lowering that keeps SIMD16 would help Gen9 fragment shaders.
7. `src/intel/compiler/brw/brw_lower_simd_width.cpp:125`: Perf: mixed-mode float with f32 destination capped at SIMD8 on Gen9; restructuring conversions could keep SIMD16.
8. `src/intel/compiler/brw/brw_lower.cpp:136`: Perf: Gen9 lowers HF/W/D select; check whether a predicated MOV sequence is cheaper.
9. `src/intel/compiler/brw/brw_opt_mac.cpp:655`: Perf: MAD to MAC fusion is blocked entirely on Gen9; any safe subset would cut ALU instruction count.
10. `src/intel/compiler/brw/brw_lower_dpas.cpp:241`: Perf: int8 DPAS emulation on Gen9 expands packed bytes to words; the FINISHME says Gen9 does not need it.
11. `src/intel/compiler/brw/brw_analysis_performance.cpp:287`: Perf: XXX cost constants drive SIMD and scheduling choices; calibrating Gen9 entries from measurements could improve them.
12. `src/intel/compiler/brw/brw_simd_selection.cpp:157`: Perf: fixed SIMD32 rule for ver < 20 (applies to Gen9) should be replaced by the cost model the TODO asks for.
13. `src/intel/compiler/brw/brw_shader.cpp:727`: Perf: on Gen9 the PRE scheduler is only tried in the first pass; more scheduler trials per shader could help at compile-time cost.
14. `src/intel/compiler/brw/brw_from_nir.cpp:5156`: Perf: Gen9 forces commit_enable on every fence based on a simulator complaint; confirm it is still needed.
15. `src/intel/compiler/brw/brw_workaround.cpp:163`: Perf: a dummy UGM fence before EOT uses a weak criterion; a better criterion would drop it for many Gen9 shaders.
