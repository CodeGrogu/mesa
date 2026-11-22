# Gen9 (KBL GT2) source sweep: iris, isl, blorp, common, ds, perf

Read-only sweep of `mesa/` at commit `0e9e108` (checkout at `/home/polarbear/.t3/projects/catchingup/mesa`). No builds, no edits outside this file.

**Target:** Intel Kaby Lake GT2 (HD 620), `GFX_VER 9` / `devinfo->ver == 9`, `INTEL_PLATFORM_KBL`, `gt == 2`.

**Method:** `grep -n` and `sed` over the six directories; every row's line number and quote is checked against the file at build time, and the row is dropped with an error on mismatch. Quotes are the line with leading indentation removed and are cut at 120 characters.

**Key finding for Gen9:** `src/intel/dev/mesa_defs.json` lists 194 workarounds, and none has a SKL, KBL, GLK, BXT, BDW or CFL platform entry. `intel_needs_workaround()` is therefore false on KBL for every ID in the table. Gen9 behaviour comes from `GFX_VER == 9` / `ver <= 9`-style `#if`s and `devinfo->ver` checks, not from `Wa_` IDs. Rows in Section 2 marked "Not Gen9" cite this.

**Note prefixes:** `(Gen9)` = the code path runs on KBL GT2. `Not Gen9` = gated away from Gen9, or only on GT4 / Gen8 / Gen12+ paths.

## 1. Features disabled or limited on Gen9

| Location | Quote | Note |
|---|---|---|
| `src/gallium/drivers/iris/iris_screen.c:304` | `caps->sampler_reduction_minmax_arb = devinfo->ver > 8;` | Enabled on Gen9 (`ver > 8`), so min/max sampler reduction is exposed on KBL. |
| `src/gallium/drivers/iris/iris_screen.c:353` | `caps->compressed_surface_reinterpret_blocks_layered = devinfo->ver >= 9;` | Enabled on Gen9 (`ver >= 9`). |
| `src/gallium/drivers/iris/iris_screen.c:374` | `caps->fbfetch_coherent = devinfo->ver >= 9 && devinfo->ver < 20;` | Coherent framebuffer fetch is advertised on Gen9. |
| `src/gallium/drivers/iris/iris_screen.c:380` | `caps->atomic_float_minmax = devinfo->ver >= 9;` | Float atomic min/max is advertised on Gen9. |
| `src/gallium/drivers/iris/iris_screen.c:381` | `caps->depth_bounds_test = devinfo->ver >= 12;` | Depth bounds test cap is off on Gen9 (Gen12+ only). |
| `src/gallium/drivers/iris/iris_screen.c:454` | `caps->allow_dynamic_vao_fastpath = devinfo->ver >= 11;` | Dynamic VAO fast path is off on Gen9; the comment above calls it harmful on Gen8/9. |
| `src/gallium/drivers/iris/iris_screen.c:462` | `devinfo->ver == 9 && !intel_device_info_is_9lp(devinfo);` | Gen9 non-9LP (KBL GT2 qualifies) sets `astc_void_extents_need_denorm_flush`. |
| `src/gallium/drivers/iris/iris_screen.c:498` | `if (devinfo->ver >= 9) {` | Subgroup size 32 and vote/ballot caps are advertised from Gen9 up. |
| `src/gallium/drivers/iris/iris_formats.c:202` | `if (devinfo->ver == 9 && (format == ISL_FORMAT_ASTC_LDR_2D_5X5_FLT16 \|\|` | ASTC 5x5 is rejected on Gen9; st/mesa falls back to uncompressed emulation. |
| `src/gallium/drivers/iris/iris_resource.c:91` | `if (devinfo->ver <= 8 \|\| devinfo->ver >= 12)` | Y_TILED_CCS modifier is allowed on Gen9 (rejected only on Gen8 and Gen12+). |
| `src/gallium/drivers/iris/iris_resource.c:696` | `if (devinfo->ver <= 11 &&` | On Gen9, 32-bit float formats are excluded from CCS_E (comment cites a 62% FPS drop). |
| `src/gallium/drivers/iris/iris_resource.c:1060` | `res->aux.usage = intel_needs_workaround(devinfo, 1607794140) ?` | Wa_1607794140 (FCV_CCS_E) has no Gen9 platform entry, so Gen9 uses plain CCS_E on this branch. |
| `src/gallium/drivers/iris/iris_resource.c:1093` | `if (isl_aux_usage_has_ccs(res->aux.usage) && devinfo->ver <= 11)` | Gen9 zero-initializes CCS aux at BO setup so it starts in pass-through state. |
| `src/gallium/drivers/iris/iris_resource.c:1101` | `if (res->aux.usage == ISL_AUX_USAGE_HIZ && devinfo->ver <= 11)` | Gen9 zero-initializes HiZ aux to CLEAR state, avoiding ambiguates. |
| `src/gallium/drivers/iris/iris_resolve.c:1053` | `if (devinfo->ver < 12)` | Image render compression is off on Gen9; images get `ISL_AUX_USAGE_NONE`. |
| `src/gallium/drivers/iris/iris_resolve.c:1116` | `if (devinfo->ver <= 9 &&` | On Gen9, texture views with a format change disable clear-color sampling. |
| `src/gallium/drivers/iris/iris_clear.c:39` | `if (devinfo->ver < 9 &&` | Only pre-Gen9 is limited to 0.0/1.0 fast-clear values; Gen9 may fast-clear arbitrary colors. |
| `src/gallium/drivers/iris/iris_draw.c:315` | `if (iris_use_draw_indirect_generation(screen, indirect) && devinfo->ver <= 9)` | Gen9 draw-indirect generation reserves a binding-table entry for the generation shader. |
| `src/gallium/drivers/iris/iris_draw.c:286` | `if (devinfo->ver == 9)` | Gen9-only: mid-object preemption is re-evaluated on every draw. |
| `src/gallium/drivers/iris/iris_query.c:143` | `GFX_VER == 9 && devinfo->gt == 4 ?  PIPE_CONTROL_CS_STALL : 0;` | Optional CS stall on query writes is GT4-only; KBL GT2 does not get it. |
| `src/gallium/drivers/iris/iris_state.c:3204` | `f.use_clear_address = isl_dev->info->ver > 9;` | Gen9 cannot use the clear-address field, so the clear color must be patched into surface state. |
| `src/gallium/drivers/iris/iris_state.c:5703` | `#if GFX_VER == 9` | Gen9-only `surf_state_update_clear_value`: fast-clear color is written into each surface state. |
| `src/gallium/drivers/iris/iris_state.c:5758` | `#if GFX_VER == 9` | Gen9 branch of the clear-value update loop; skips the NONE aux surface state. |
| `src/gallium/drivers/iris/iris_state.c:2303` | `#if GFX_VER == 8` | PMA fix block is Gen8-only; Gen9 does not run it. |
| `src/gallium/drivers/iris/iris_state.c:1366` | `#if GFX_VER == 9` | Gen9-only CACHE_MODE_1 setup (float blend optimization, MSCRAW hazard, partial resolve disable). |
| `src/gallium/drivers/iris/iris_state.c:1376` | `if (devinfo->platform == INTEL_PLATFORM_GLK)` | GLK-only barrier-mode chicken bit; KBL does not run it. |
| `src/gallium/drivers/iris/iris_state.c:975` | `#if GFX_VER == 9` | Gen9-only object-preemption enable path. |
| `src/gallium/drivers/iris/iris_state.c:727` | `#if GFX_VER >= 8 && GFX_VER < 10` | Gen8/9 clears COLOR_CALC_STATE valid before switching to GPGPU pipeline select. |
| `src/gallium/drivers/iris/iris_state.c:8436` | `#if GFX_VER >= 9 && GFX_VER < 12` | Gen9-11 emit 3DSTATE_WM_DEPTH_STENCIL with stencil refs through a separate path; applies to Gen9. |
| `src/gallium/drivers/iris/iris_state.c:7371` | `(GFX_VER == 9 ? IRIS_STAGE_DIRTY_CONSTANTS_VS : 0))` | Gen9 adds a VS constants dirty flag on this path, so VS constants are re-uploaded more often. |
| `src/gallium/drivers/iris/iris_state.c:11072` | `#if GFX_VER == 9` | Gen9-only preemption management (`gfx9_toggle_preemption`). |
| `src/gallium/drivers/iris/iris_state.c:11184` | `#if GFX_VER == 9` | Gen9-only slice hashing table for multi-slice parts; KBL GT2 is single-slice, so likely unused. |
| `src/gallium/drivers/iris/iris_indirect_gen.c:304` | `ps.BindingTableEntryCount = GFX_VER == 9 ? 1 : 0;` | Gen9 generation shader reserves one binding-table entry for its PS. |
| `src/gallium/drivers/iris/iris_indirect_gen.c:493` | `#if GFX_VER <= 9` | Gen9 re-emits binding-table pointers after the generation shader (constant-commit workaround). |
| `src/intel/common/intel_l3_config.c:405` | `const unsigned max = (devinfo->ver == 9 ? 1008 : ~0);` | Gen9 caps usable URB at 1008 KB of L3 config. |
| `src/intel/isl/isl_format.c:862` | `if (devinfo->ver < 7 \|\| devinfo->ver > 11)` | Clear-only compression is permitted on Gen9 (range 7 to 11). |
| `src/intel/isl/isl_format.c:950` | `return devinfo->ver <= 8;` | On Gen9 HiZ is always single-sampled; multisampled HiZ is rejected. |
| `src/intel/isl/isl_aux_info.c:148` | `if (zeroed && devinfo->ver <= 11) {` | Gen9 treats zeroed HiZ/CCS as CLEAR state (pre-Gen12 behavior). |
| `src/intel/isl/isl_aux_info.c:219` | `} else if (devinfo->ver >= 9) {` | Gen9 takes the AUX_INVALID branch in this aux-state choice. |
| `src/intel/isl/isl_storage_image.c:239` | `if (devinfo->ver >= 9) {` | Gen9 matches typed storage-image formats directly, with no 32-bit fallback. |
| `src/intel/isl/isl_surface_state.c:950` | `if (GFX_VER >= 9 \|\| dev->info->platform == INTEL_PLATFORM_CHV) {` | Gen9 sets Sampler L2 Bypass Disable for BC formats. |
| `src/intel/isl/isl_surface_state.c:967` | `if (GFX_VER >= 9 && info->aux_usage == ISL_AUX_USAGE_HIZ)` | Gen9 sets Sampler L2 Bypass Disable for HiZ-aux textures. |
| `src/intel/blorp/blorp_clear.c:68` | `batch->blorp->isl_dev->info->ver >= 6 &&` | Replicated (fast) clears are enabled for 6 <= ver < 12, including Gen9. |
| `src/intel/blorp/blorp_clear.c:1481` | `} else if (batch->blorp->isl_dev->info->ver >= 9) {` | Gen9 allows FULL and PARTIAL resolve only; AMBIGUATE is Gen10+. |
| `src/intel/blorp/blorp_clear.c:1504` | `if (batch->blorp->isl_dev->info->ver <= 8) {` | Gen9 skips the Gen8-and-earlier CCS_D full-resolve pass-through fixup. |
| `src/intel/blorp/blorp.c:370` | `} else if (devinfo->ver >= 8 && devinfo->ver <= 9 &&` | Gen8-9 HiZ ambiguate LOD adjustment runs on Gen9. |
| `src/intel/blorp/blorp_genX_exec_brw.h:642` | `if (GFX_VER != 9 && batch->blorp->config.use_cached_dynamic_states) {` | Gen9 never reuses cached CC_VIEWPORT state (gitlab issue 11647). |
| `src/intel/blorp/blorp_blit.c:3125` | `/* On gfx9-12, avoid format re-interpretation in order to support` | Gen9 copies keep the surface format to preserve non-zero clear colors; no reinterpretation. |
| `src/intel/blorp/blorp_blit.c:3297` | `/* CCS_E on gfx9-11 requires the surface's bpc not change. */` | Gen9 CCS_E forbids a bpc change in blit copies; the code returns early for ver <= 11. |
| `src/gallium/drivers/iris/iris_blit.c:292` | `bool need_flush = devinfo->ver >= 11 ?` | On Gen9 any format re-interpretation sets need_flush (ASTC-only check is Gen11+). |
| `src/gallium/drivers/iris/iris_program.c:2576` | `.quads_workaround = devinfo->ver < 9 &&` | Quads workaround is pre-Gen9 only; not applied on Gen9. |

Rows: 51

## 2. Gen9-specific workarounds (`Wa_`, SKL, KBL, gfx9)

| Location | Quote | Note |
|---|---|---|
| `src/gallium/drivers/iris/iris_state.c:10592` | `if (GFX_VER == 9 && (flags & PIPE_CONTROL_VF_CACHE_INVALIDATE)) {` | (Gen9) SKL/KBL/BXT rule: VF cache invalidate needs a preceding null PIPE_CONTROL; KBL runs it. |
| `src/gallium/drivers/iris/iris_state.c:10605` | `"workaround: recursive VF cache invalidate",` | (Gen9) Emits the null PIPE_CONTROL required by the SKL/KBL VF cache invalidate rule. |
| `src/gallium/drivers/iris/iris_state.c:10609` | `if (GFX_VER == 9 && IS_COMPUTE_PIPELINE(batch) && post_sync_flags) {` | (Gen9) SKL rule: CS stall before an LRI post-sync in GPGPU mode; KBL compute path runs it. |
| `src/gallium/drivers/iris/iris_state.c:10798` | `if (GFX_VER == 9 && devinfo->gt == 4) {` | Skylake GT4 post-sync workaround is an empty TODO block; GT4-only, does not run on GT2. |
| `src/gallium/drivers/iris/iris_state.c:9528` | `iris_emit_pipe_control_flush(batch, "before generation",` | (Gen9) Extra VF cache invalidate before generated (indirect) draws on Gen9. |
| `src/gallium/drivers/iris/iris_indirect_gen.c:493` | `#if GFX_VER <= 9` | (Gen9) Re-emits binding-table pointers to commit constants (SKL-era WA); TODO inside. |
| `src/gallium/drivers/iris/iris_indirect_gen.c:571` | `#if GFX_VER <= 9` | (Gen9) Re-reserves binding tables after the generated draw to restore app binding offsets. |
| `src/gallium/drivers/iris/iris_state.c:11076` | `* See these workarounds for preemption:` | (Gen9) Lists WaDisableMidObjectPreemptionFor* and WA#0798, applied by the Gen9 preemption toggle. |
| `src/gallium/drivers/iris/iris_state.c:1368` | `reg.FloatBlendOptimizationEnable = true;` | (Gen9) Enables float blend optimization in CACHE_MODE_1 (Gen9 chicken bit). |
| `src/gallium/drivers/iris/iris_state.c:819` | `/* Project: DevGLK` | GLK-only barrier chicken bit; KBL does not run `init_glk_barrier_mode`. |
| `src/gallium/drivers/iris/iris_state.c:1673` | `if (devinfo->platform == INTEL_PLATFORM_GLK)` | GLK-only GPGPU barrier mode; KBL skips it. |
| `src/gallium/drivers/iris/iris_state.c:2316` | `* The Gfx9 docs say to use a depth stall rather than a command streamer` | Not Gen9 at runtime: the comment sits in the Gen8-only PMA block (line 2303). |
| `src/intel/isl/isl.c:2119` | `if ((ISL_GFX_VER(dev) == 9 \|\|` | (Gen9) SKL WA: 3D Yf/Ys miptail is limited for CCS surfaces; Gen9 always takes this branch. |
| `src/intel/isl/isl.c:2123` | `/* From the workarounds section in the SKL PRM:` | (Gen9) Quotes the SKL PRM WA: RCC cacheline straddles LODs in the 3D TYF miptail when CCS is on. |
| `src/intel/isl/isl.c:2136` | `* Referred to as Wa_1207137018 on ICL+. Use a narrower workaround` | (Gen9) Notes the ICL+ variant is narrower (described in the HSD); Gen9 keeps the wide form. |
| `src/intel/isl/isl.c:2104` | `if (intel_needs_workaround(dev->info, 22015614752) &&` | Not Gen9: `22015614752` lists only ARL_H, ARL_U, ATSM_G10, ATSM_G11, DG2_G10, DG2_G11, DG2_G12, MTL_H, MTL_U; no SKL/KBL entry. |
| `src/intel/isl/isl_surface_state.c:458` | `} else if (INTEL_NEEDS_WA_1806565034) {` | Not Gen9: `1806565034` lists only ADL, DG1, RKL, RPL, TGL; no SKL/KBL entry. |
| `src/intel/isl/isl_surface_state.c:1171` | `assert(intel_needs_workaround(dev->info, 14019708328) == false);` | Not Gen9: asserts Wa_14019708328 is off; table lists LNL/BMG only. |
| `src/gallium/drivers/iris/iris_blorp.c:317` | `if (intel_needs_workaround(batch->screen->devinfo, 18019816803)) {` | Not Gen9: `18019816803` lists only ARL_U, ATSM_G10, ATSM_G11, DG2_G10, DG2_G11, DG2_G12; no SKL/KBL entry. |
| `src/gallium/drivers/iris/iris_resolve.c:515` | `if (intel_needs_workaround(batch->screen->devinfo, 1508744258)) {` | Not Gen9: `1508744258` lists only ADL, DG1, RKL, RPL, TGL; no SKL/KBL entry. |
| `src/gallium/drivers/iris/iris_resolve.c:1136` | `intel_needs_workaround(devinfo, 14013111325) &&` | Not Gen9: `14013111325` lists only ADL, DG1, RKL, RPL, TGL; no SKL/KBL entry. |
| `src/gallium/drivers/iris/iris_bufmgr.c:2605` | `if (intel_needs_workaround(devinfo, 14019708328)) {` | Not Gen9: `14019708328` lists only BMG, LNL; no SKL/KBL entry. |
| `src/gallium/drivers/iris/iris_state.c:2817` | `if (intel_needs_workaround(screen->devinfo, 14014414195))` | Not Gen9: `14014414195` lists only ARL_U, ATSM_G10, ATSM_G11, DG2_G10, DG2_G11, DG2_G12; no SKL/KBL entry. |
| `src/gallium/drivers/iris/iris_state.c:1687` | `iris_emit_pipe_control_flush(batch, "Wa_14015782607",` | Not Gen9: Wa_14015782607 is gated by intel_needs_workaround; table lists ATSM/DG2/ARL only. |
| `src/gallium/drivers/iris/iris_state.c:6731` | `/* Wa_16018063123 - emit fast color dummy blit before MI_FLUSH_DW. */` | Not Gen9: Wa_16018063123 lists ARL/ATSM/DG2/MTL only. |
| `src/gallium/drivers/iris/iris_state.c:7160` | `if (!intel_needs_workaround(batch->screen->devinfo, 16013994831))` | Not Gen9: Wa_16013994831 is also gated `GFX_VERx10 >= 120`; returns early on Gen9. |
| `src/gallium/drivers/iris/iris_state.c:5198` | `/* Wa_1606682166 */` | Not Gen9: Gen11 sampler prefetch workaround. |
| `src/gallium/drivers/iris/iris_formats.c:198` | `/* TODO: Support ASTC 5x5 on Gfx9 properly.  This means implementing` | (Gen9) Open TODO: no ASTC 5x5 sampler WA, so Gen9 uses st/mesa emulation (see line 202). |
| `src/intel/blorp/blorp_genX_exec_brw.h:1728` | `if (intel_needs_workaround(devinfo, 1408224581) \|\|` | Not Gen9: `1408224581` lists only ADL, DG1, RPL, TGL; no SKL/KBL entry. |
| `src/intel/blorp/blorp_genX_exec_brw.h:994` | `if (intel_needs_workaround(devinfo, 16030144090)) {` | Not Gen9: `16030144090` lists only NVL_P; no SKL/KBL entry. |
| `src/gallium/drivers/iris/iris_context.c:230` | `gfx9_##func(__VA_ARGS__);                   \` | (Gen9) Macro dispatch to the gfx9 vtable variant; Gen9 builds use these functions. |

Rows: 31

## 3. TODO / FIXME / XXX / HACK comments

| Location | Quote | Note |
|---|---|---|
| `src/gallium/drivers/iris/iris_formats.c:198` | `/* TODO: Support ASTC 5x5 on Gfx9 properly.  This means implementing` | (Gen9) Gen9 ASTC 5x5 support missing; code below disables it on Gen9. |
| `src/gallium/drivers/iris/iris_query.c:802` | `// XXX: most packets aren't paying attention to this yet, because it'd` | (Gen9) Packets ignore a state flag; generic query path that runs on Gen9. |
| `src/gallium/drivers/iris/iris_screen.c:257` | `caps->max_global_size = 1 << 30; /* TODO */` | (Gen9) Placeholder `max_global_size` cap reported on all gens. |
| `src/gallium/drivers/iris/iris_screen.c:259` | `caps->max_clock_frequency = 400; /* TODO */` | (Gen9) Placeholder `max_clock_frequency` cap reported on all gens. |
| `src/gallium/drivers/iris/iris_screen.c:400` | `caps->texture_buffer_offset_alignment = 16; // XXX: u_screen says 256 is the minimum value...` | (Gen9) Texture buffer offset alignment set to 16; comment questions u_screen's 256 minimum. |
| `src/gallium/drivers/iris/iris_resource.c:9` | `* XXX: explain resources` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_resource.c:168` | `/* TODO: Do we still face these restrictions on Xe2+? */` | Not Gen9-specific: asks about Xe2+ modifier restrictions. |
| `src/gallium/drivers/iris/iris_resource.c:1025` | `/* TODO: We should be able to drop this. */` | (Gen9) Code says a protected-BO-type restriction could be dropped; runs on Gen9 when set. |
| `src/gallium/drivers/iris/iris_resource.c:2004` | `/* TODO: TILE64 modifier support in the KMD */` | Not Gen9: TILE64 modifier support is Xe-era. |
| `src/gallium/drivers/iris/iris_resource.c:2735` | `* TODO: Teach isl_memcpy_linear_to_tiled about Tile64...` | Not Gen9: Tile64 memcpy only; Gen9 does not use Tile64. |
| `src/gallium/drivers/iris/iris_bufmgr.h:278` | `* XXX: this is not ideal now that we have more than one batch per context,` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_bufmgr.h:279` | `* XXX: as the index will flop back and forth between the render index and` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_bufmgr.h:280` | `* XXX: compute index...` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_resolve.c:251` | `/* XXX: Need to track which bindings to make dirty */` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_resolve.c:320` | `// XXX: front buffer drawing?` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_resolve.c:739` | `* TODO: Such as the spec says, this could be conditional.` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_resolve.c:956` | `/* XXX: Need to track which bindings to make dirty */` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_fence.c:381` | `/* XXX: We can't safely flush the other context, because it might be` | (Gen9) Cross-context fence wait cannot flush the other context safely; applies to Gen9 i915 fences. |
| `src/gallium/drivers/iris/iris_nir_analyze_ubo_ranges.c:139` | `/* TODO: should we count uses in loops as higher benefit? */` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_nir_analyze_ubo_ranges.c:248` | `/* TODO: Consider combining ranges.` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_batch.c:416` | `/* TODO: We probably could suballocate batches... */` | (Gen9) Each batch is its own BO; suballocation would reduce allocations on all gens. |
| `src/gallium/drivers/iris/iris_blit.c:157` | `// XXX: comments assume source clipping, which we don't do` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_blit.c:730` | `// XXX: what about one surface being a buffer and not the other?` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_program.c:2819` | `/* TODO: Could compare and avoid flagging this. */` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_program.c:3238` | `// XXX: only need to flag CLIP if barycentric has NONPERSPECTIVE` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_program.c:3895` | `// XXX: disallow more than 64KB of shared variables` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_program.c:4037` | `// XXX: make sure the linker fills this out from the TES...` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_program.c:4065` | `// XXX: not ideal, need TCS output/TES input unification` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_indirect_gen.c:495` | `* order to commit constants. TODO: Investigate "Disable Gather at Set` | (Gen9) Gen9 binding-table re-emit TODO: investigate 'Disable Gather at Set Shader'. |
| `src/gallium/drivers/iris/iris_bufmgr.c:9` | `* XXX: write better comments` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_bufmgr.c:448` | `.flags = DMA_BUF_SYNC_RW, /* TODO */` | (Gen9) dma-buf sync flags left as RW; runs on Gen9 i915 sync path. |
| `src/gallium/drivers/iris/iris_bufmgr.c:781` | `* TODO: move this to intel_device_info?` | (Gen9) Device-info helper placement question; generic. |
| `src/gallium/drivers/iris/iris_state.c:476` | `* XXX:  As far as we have been able to determine through` | (Gen9) Flush-after-SBA rationale not explained; the flush runs on every SBA change on Gen9. |
| `src/gallium/drivers/iris/iris_state.c:1161` | `* TODO: Try lazily allocating the HS/DS/GS sections as needed, and` | (Gen9) Push-constant URB split not tuned; runs on Gen9. |
| `src/gallium/drivers/iris/iris_state.c:1538` | `/* TODO: may need to set an offset for origin-UL framebuffers */` | (Gen9) Origin-UL framebuffer offset may be required; unhandled. |
| `src/gallium/drivers/iris/iris_state.c:2633` | `// XXX: explain this code ported from ilo...I don't get it at all...` | (Gen9) Code ported from ilo, no explanation; runs on Gen9 when reached. |
| `src/gallium/drivers/iris/iris_state.c:3444` | `* TODO: This only handles the case where we're rendering to a single` | (Gen9) Only handles a single render target case in this path. |
| `src/gallium/drivers/iris/iris_state.c:4204` | `/* TODO: Only do this if the buffer changes? */` | (Gen9) Constant buffer update may be needed only when buffer changes; runs on Gen9. |
| `src/gallium/drivers/iris/iris_state.c:4975` | `/* XXX: this should be generated when putting programs in place */` | (Gen9) Program-generation setup should happen when programs are installed; cost is per bind. |
| `src/gallium/drivers/iris/iris_state.c:5583` | `/* TODO: Check if we are missing workarounds and enable mid-thread` | (Gen9) Mid-thread preemption workaround check missing (TODO). |
| `src/gallium/drivers/iris/iris_state.c:5768` | `/* TODO: Could update rather than re-filling */` | Not Gen9: sits in the `#elif GFX_VER == 8` branch. |
| `src/gallium/drivers/iris/iris_state.c:6317` | `/* XXX: YUV surfaces not implemented yet */` | (Gen9) YUV surfaces not implemented for surface state. |
| `src/gallium/drivers/iris/iris_state.c:6935` | `* XXX - Use workaround infrastructure and final workaround` | (Gen9) Workaround infrastructure not used for an ad-hoc WA; generic. |
| `src/gallium/drivers/iris/iris_state.c:7101` | `/* XXX - Pessimistic, in some cases it might be helpful to neglect` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_state.c:7122` | `/* XXX - Pessimistic, in some cases it might be helpful to neglect` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_state.c:7367` | `* in order to commit constants.  TODO: Investigate "Disable Gather` | (Gen9) Binding-table re-emit TODO on Gen9 generated-draw path. |
| `src/gallium/drivers/iris/iris_state.c:7554` | `/* XXX: could avoid streaming for depth_clip [0,1] case. */` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_state.c:7870` | `// XXX: we may want to flag IRIS_DIRTY_MULTISAMPLE (or SAMPLE_MASK?)` | (Gen9) Multisample dirty-flag mask not flagged; possible missed update. |
| `src/gallium/drivers/iris/iris_state.c:7871` | `// XXX: see commit 979fc1bc9bcc64027ff2cfafd285676f31b930a6` | (Gen9) Comment on which commit introduced a related change; doc-only. |
| `src/gallium/drivers/iris/iris_state.c:7881` | `// XXX: does this need to happen at 3DSTATE_BTP_PS time?` | (Gen9) Question whether a flag is needed at BTP_PS time. |
| `src/gallium/drivers/iris/iris_state.c:7978` | `/* XXX - No major improvement observed from enabling` | (Gen9) Overlapping subspans for PS dispatch not used; no measured gain recorded. |
| `src/gallium/drivers/iris/iris_state.c:9187` | `* XXX - Provide hardware spec quotation when available.` | (Gen9) Missing hardware spec quote (XXX); doc-only. |
| `src/gallium/drivers/iris/iris_state.c:9393` | `* XXX - Provide hardware spec quotation when available.` | (Gen9) Missing hardware spec quote (XXX); doc-only. |
| `src/gallium/drivers/iris/iris_state.c:9492` | `* XXX - Provide hardware spec quotation when available.` | (Gen9) Missing hardware spec quote (XXX); doc-only. |
| `src/gallium/drivers/iris/iris_state.c:9665` | `// TODO: Swizzling X and Y workgroup sizes is not supported in execute indirect dispatch` | (Gen9) Swizzled workgroup sizes unsupported in indirect dispatch; compute path on KBL. |
| `src/gallium/drivers/iris/iris_state.c:9976` | `/* TODO: Combine subgroup-id with cbuf0 so we can push regular uniforms */` | (Gen9) Subgroup ID is not pushed with cbuf0; compute path, which KBL has. |
| `src/gallium/drivers/iris/iris_state.c:10555` | `/* TODO: This may not always be necessary */` | (Gen9) Pipe-control emitted here may be unnecessary; runs on Gen9. |
| `src/gallium/drivers/iris/iris_state.c:10666` | `* TODO: Implement end-of-pipe checking.` | (Gen9) End-of-pipe checking not implemented in raw PIPE_CONTROL emission; runs on Gen9. |
| `src/gallium/drivers/iris/iris_state.c:10799` | `/* TODO: The big Skylake GT4 post sync op workaround */` | Not GT2: body is `if (GFX_VER == 9 && devinfo->gt == 4)`, so KBL GT2 never runs it. |
| `src/gallium/drivers/iris/iris_clear.c:932` | `// XXX: actually just get_copy_format_for_bpb from BLORP` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/iris_clear.c:933` | `// XXX: don't cut and paste this` | (Gen9) Generic code, no Gen9-specific gating; runs on KBL when reached. |
| `src/gallium/drivers/iris/xe/iris_batch.c:130` | `/* TODO: handle "protected" context/exec_queue */` | Not Gen9: Xe KMD exec-queue protected context. |
| `src/gallium/drivers/iris/xe/iris_kmd_backend.c:56` | `/* TODO: we might need to consider scanout for shared buffers too as we` | Not Gen9: Xe KMD backend (KBL uses i915). |
| `src/intel/isl/isl_emit_depth_stencil.c:296` | `* TODO: Confirm documentation and remove setting of `Depth` if not` | (Gen9) Depth field set for SKL+ may be unnecessary; the emitter runs on Gen9. |
| `src/intel/isl/isl_gfx7.c:348` | `* TODO: Is this an issue for multisampled surfaces as well?` | Not Gen9: Gen7 file. |
| `src/intel/isl/isl_gfx12.c:217` | `* TODO: Cite docs for CPB.` | Not Gen9: Gen12 file. |
| `src/intel/isl/isl.c:119` | `/* TODO: Setting to uncached` | (Gen9) MOCS 'uncached' encoding left as a TODO in the Gen9 MOCS setup. |
| `src/intel/isl/isl.c:135` | `/* TODO: XY_BLOCK_COPY_BLT don't mention what should be the L4 cache` | (Gen9) L4 cache policy for XY_BLOCK_COPY_BLT left open. |
| `src/intel/isl/isl.c:2094` | `/* TODO: figure out why having YUV formats in the miptail on Gfx12 does not` | Not Gen9: YUV in the Gen12 miptail question. |
| `src/intel/isl/isl.c:2315` | `* XXX(chadv): Is the errata natural corollary of the physical` | (Gen9) Errata reasoning (chadv) unclear; reached on Gen9 for relevant layouts. |
| `src/intel/isl/isl_storage_image.c:31` | `/* XXX: Maybe we should put this in the CSV? */` | (Gen9) Format-table placement question; generic. |
| `src/intel/isl/isl_gfx20.c:217` | `* TODO: Cite docs for CPB.` | Not Gen9: Xe2 file. |
| `src/intel/isl/isl_gfx8.c:155` | `/* XXX(chadv): I believe the hardware requires each image to be` | Not Gen9: Gen8 file. |
| `src/intel/isl/isl.h:1153` | `/* TODO(chadv): Explain */` | (Gen9) Explanation missing for an ISL declaration; doc-only. |
| `src/intel/blorp/blorp_blit.c:348` | `* from an input.  TODO: This is a bit magic; we should probably make this` | (Gen9) Blit magic-value handling is unclear; runs on Gen9 blits. |
| `src/intel/blorp/blorp_blit.c:1997` | `* TODO: Are we sure we don't also need to fake it on gfx6?` | (Gen9) Question whether gfx6 needs the same fake; generic blit. |
| `src/intel/blorp/blorp_blit.c:2283` | `* TODO: what if this makes the coordinates too large?` | (Gen9) Coordinate overflow unchecked; blit path on Gen9. |
| `src/intel/blorp/blorp_blit.c:2347` | `* TODO: what if this makes the coordinates (or the texture size) too` | (Gen9) Coordinate/texture size overflow unchecked; blit path on Gen9. |
| `src/intel/blorp/blorp_blit.c:2383` | `* TODO: what if this makes the texture size too large?` | (Gen9) Texture size overflow unchecked; blit path on Gen9. |
| `src/intel/blorp/blorp_blit.c:3123` | `surf_format != ISL_FORMAT_R16G16B16A16_UNORM /* TODO */ &&` | (Gen9) R16G16B16A16_UNORM exception left TODO in the copy-format choice (Gen9 path). |
| `src/intel/blorp/blorp_genX_exec_brw.h:1828` | `/* TODO - If we ever start using 3DSTATE_WM_HZ_OP::StencilBufferResolveEnable` | (Gen9) Stencil-resolve HZ op not used; TODO for future use. |
| `src/intel/blorp/blorp_genX_exec_elk.h:1099` | `wm.PixelShaderKillsPixel = true; /* TODO: temporarily smash on */` | Not Gen9: ELK (Gen8) blorp path; Gen9 uses the BRW blorp file. |
| `src/intel/blorp/blorp_genX_exec_elk.h:1768` | `/* TODO - If we ever start using 3DSTATE_WM_HZ_OP::StencilBufferResolveEnable` | Not Gen9: ELK (Gen8) blorp path. |
| `src/intel/blorp/blorp.c:367` | `/* TODO: What about MSAA? */` | (Gen9) MSAA not handled in the HiZ LOD adjustment on the Gen8-9 path. |
| `src/intel/common/intel_hang_dump.h:37` | `/* TODO: Consider compression? ZSTD_error_dstSize_tooSmall */` | Generic: hang-dump compression idea; not Gen9 behavior. |
| `src/intel/common/intel_urb_config.c:359` | `/* TODO(mesh): Take push constant size as parameter instead of considering always` | Not Gen9: mesh URB sizing. |
| `src/intel/common/intel_urb_config.c:367` | `* TODO(mesh): Re-evaluate if this is a good default once there are more` | Not Gen9: mesh URB default. |
| `src/intel/common/intel_urb_config.c:420` | `/* TODO(mesh): Could we avoid allocating URB for Mesh if rasterization is` | Not Gen9: mesh URB allocation. |
| `src/intel/common/intel_l3_config.c:194` | `/* XXX - Some MTL configs may need special-casing here, but` | Not Gen9: MTL L3 config. |
| `src/intel/common/mi_builder.h:497` | `/* TODO: We could handle src.invert by emitting a bit of math if we really` | (Gen9) mi_builder src.invert emulation not optimized; shared by Gen9 perf/ds code. |
| `src/intel/common/intel_tiled_render.h:52` | `* XXX - Note that this assumes TBIMR in pixel hashing mode is in use.` | Not Gen9: TBIMR pixel hashing (Gen12.5+). |
| `src/intel/common/intel_tiled_render.h:128` | `/* XXX - If the tile_surf calculated above is smaller than the` | Not Gen9: TBIMR tile surface (Gen12.5+). |
| `src/intel/common/xe/intel_gem.c:154` | `/* FIXME: Get a proper kernel api to detect this */` | Not Gen9: Xe KMD detection. |
| `src/intel/common/xe/intel_gem.c:200` | `/* XXX: Kernel didn't give us a way to check for support */` | Not Gen9: Xe KMD support check. |
| `src/intel/common/xe/intel_gem.c:220` | `mesa_logd("FIXME: Received page fault with unknown SRCID 0x%02x\n",` | Not Gen9: Xe KMD page-fault logging. |
| `src/intel/perf/intel_perf_query.c:724` | `/* XXX: We have to consider that the command parser unit that parses batch` | (Gen9) Perf command parser ordering question; perf on Gen9 via i915. |
| `src/intel/perf/intel_perf.c:757` | `/* TODO: Support performance metrics */` | (Gen9) Performance metrics support not implemented here; affects Gen9 perf queries. |
| `src/intel/perf/intel_perf.c:769` | `/* TODO: We should query this from i915?` | (Gen9) Max-freq query from i915 not implemented; Gen9 perf uses a fixed value. |
| `src/intel/perf/xe/intel_perf.c:129` | `* TODO: this approach does not takes into account applications running` | Not Gen9: Xe perf path. |
| `src/intel/perf/xe/intel_perf.c:472` | `/* TODO: also append REPORT_LOST and BUFFER_LOST */` | Not Gen9: Xe perf path (REPORT_LOST / BUFFER_LOST). |
| `src/intel/perf/intel_perf_mdapi.c:163` | `/* TODO: Fields not being set: MidQueryEvents, OverrunOccured, MarkerUser,` | (Gen9) MDAPI fields left unset; affects Gen9 MDAPI reports. |
| `src/intel/perf/intel_perf_mdapi.c:185` | `/* TODO: not clear from where the remaining VisaCounters should be read from */` | (Gen9) VisaCounters source unclear; affects Gen9 MDAPI reports. |
| `src/intel/perf/intel_perf_mdapi.c:204` | `/* TODO: Fields not being set: MidQueryEvents, OverrunOccured, MarkerUser,` | (Gen9) MDAPI fields left unset (second report type). |

Rows: 103

## 4. Performance notes

| Location | Quote | Note |
|---|---|---|
| `src/gallium/drivers/iris/iris_resource.c:689` | `/* Prior to TGL, CCS_E seems to significantly hurt performance with 32-bit` | (Gen9) Source of the Gen9 32-bit float CCS_E exclusion; any change needs fresh benchmark data. |
| `src/gallium/drivers/iris/iris_resource.c:2468` | `/* We want to avoid uncached reads because they are slow. */` | (Gen9) Staging-map heuristic: CPU reads of non-WB maps bypass the direct path, costing a copy. |
| `src/gallium/drivers/iris/iris_resource.c:2472` | `/* We want to avoid stalling.  We can't avoid stalling for reads, though,` | (Gen9) Buffer-map stall heuristic; stalls for reads are accepted rather than avoided. |
| `src/gallium/drivers/iris/iris_clear.c:143` | `/* Wa_18020603990 - slow clear surfaces up to 256x256, 32bpp. */` | Not Gen9: table lists TGL/DG1/ADL/RKL/RPL only; the slow-clear case is not applied on KBL. |
| `src/gallium/drivers/iris/iris_clear.c:456` | `*    Stall set at the end of the sequence suffices.` | (Gen9) Spec note saying one trailing stall suffices, while fast clears also run pre and post EOP syncs. |
| `src/gallium/drivers/iris/iris_clear.c:633` | `* Set CS stall bit to guarantee that the fast clear starts the execution` | (Gen9) Depth fast clear adds a CS stall for ordering; an extra serialization point per clear. |
| `src/gallium/drivers/iris/iris_resolve.c:725` | `/* For gfx8-11, the following stalls and flushes are only documented to be` | (Gen9) Resolve path emits stalls/flushes documented only for gen8-11; candidate for re-review. |
| `src/gallium/drivers/iris/iris_resolve.c:748` | `if (devinfo->verx10 < 120) {` | (Gen9) Every HiZ op on Gen9 adds a depth cache flush and depth stall; the nearby comment says HW flushes on HZ_OP. |
| `src/gallium/drivers/iris/iris_state.c:418` | `* Normally, in GL, we would trust the kernel to do sufficient stalls` | (Gen9) Forced end-of-pipe sync before each STATE_BASE_ADDRESS change; the reason is documented here. |
| `src/gallium/drivers/iris/iris_state.c:1165` | `*       stalls and may not actually help; we don't know yet.` | (Gen9) Unmeasured idea to lazily allocate HS/DS/GS URB; the comment admits it adds stalls. |
| `src/gallium/drivers/iris/iris_state.c:1276` | `* write merging has a large impact on rendering performance.` | (Gen9) L3 partial-write merge is force-enabled at init for performance; Gen9 runs it. |
| `src/gallium/drivers/iris/iris_state.c:2316` | `* The Gfx9 docs say to use a depth stall rather than a command streamer` | Not Gen9 at runtime: Gen8-only PMA block; the CS stall there is documented as needed by hardware. |
| `src/gallium/drivers/iris/iris_state.c:7101` | `/* XXX - Pessimistic, in some cases it might be helpful to neglect` | (Gen9) Tile-footprint estimate counts aux traffic pessimistically; used only on the TBIMR-gated path. |
| `src/gallium/drivers/iris/iris_state.c:7168` | `/* Emit CS_STALL and 250 noops. */` | Not Gen9: Wa_16013994831 path returns early unless GFX_VERx10 >= 120. |
| `src/gallium/drivers/iris/iris_state.c:8566` | `* performance by avoiding HiZ planes for draw calls which compute depth` | Depth-only optimization from a Bspec/HSD; Gen9 gating not verified in this sweep. |
| `src/gallium/drivers/iris/iris_state.c:10848` | `* It sounds like we could avoid CS stalls in some cases, but we` | (Gen9) Stall-workaround list emits more CS stalls than needed; the comment admits it ('we don't currently bother'). |
| `src/gallium/drivers/iris/iris_state.c:7978` | `/* XXX - No major improvement observed from enabling` | (Gen9) Overlapping-subspan PS dispatch left unused; no measured gain recorded. |
| `src/gallium/drivers/iris/iris_blit.c:292` | `bool need_flush = devinfo->ver >= 11 ?` | (Gen9) Blits whose formats differ set need_flush on Gen9; the ASTC-only check starts at Gen11. |
| `src/intel/isl/isl_aux_info.c:205` | `* can have negative performance implications. Even though some` | (Gen9) Stencil CCS aux-state choice avoids CLEAR states for performance reasons; documented trade-off. |
| `src/intel/isl/isl_format.c:1333` | `* the isl_format.  This function is very slow for a format conversion` | Generic per-pixel packing helper; comment says it is slow, so keep off hot paths on Gen9. |
| `src/intel/isl/isl_surface_state.c:463` | `* Since this is a performance workaround, we only enable it when robust` | Not Gen9 by table: gated by INTEL_NEEDS_WA_1806565034; SurfaceArray trimming stays off on KBL. |
| `src/intel/blorp/blorp_genX_exec_brw.h:642` | `if (GFX_VER != 9 && batch->blorp->config.use_cached_dynamic_states) {` | (Gen9) Forces CC_VIEWPORT re-emit on every blorp op on Gen9 (issue 11647); a perf cost per blit or clear. |
| `src/intel/blorp/blorp_genX_exec_brw.h:1728` | `if (intel_needs_workaround(devinfo, 1408224581) \|\|` | Not Gen9 by table: Wa_1408224581 family adds extra blorp flushes on TGL+ only. |
| `src/gallium/drivers/iris/iris_bufmgr.h:152` | `* required, as snooping is expensive there.` | Doc on cache coherency: snooping is costly on non-LLC parts; KBL is LLC, so this rarely applies. |
| `src/gallium/drivers/iris/iris_screen.c:425` | `* cogniscience is likely to be detrimental to performance and require` | Caps comment on snooped buffers; no Gen9 runtime cost. |

Rows: 25

## 5. Forced batch flushes / stalls

Scope: `iris_batch_flush(` call sites, `iris_emit_end_of_pipe_sync(` call sites, `PIPE_CONTROL_CS_STALL` uses, and `iris_batch_sync_region_start/end` and `iris_batch_sync_boundary` call sites, all under `src/gallium/drivers/iris`. Declarations, definitions and the flag-table entries in `iris_context.h` and `iris_utrace.c` are excluded. Sync-region calls are bookkeeping only (sequence numbers), not flushes; they are listed for completeness.

### 5a. `iris_batch_flush(` call sites

| Location | Quote | Note |
|---|---|---|
| `src/gallium/drivers/iris/iris_query.c:649` | `iris_batch_flush(batch);` | Trigger: CPU-side query result read; flushes if the pending signal sits in this batch. |
| `src/gallium/drivers/iris/iris_query.c:736` | `iris_batch_flush(batch);` | Trigger: Query availability check into a resource; flushes if the signal sits in this batch. |
| `src/gallium/drivers/iris/iris_resource.c:1778` | `iris_batch_flush(batch);` | Trigger: Modifier or aux-usage change: flush batches that reference the BO before reallocating. |
| `src/gallium/drivers/iris/iris_resource.c:2271` | `iris_batch_flush(map->batch);` | Trigger: Staging-copy map: flush the batch if it references the staging BO before CPU access. |
| `src/gallium/drivers/iris/iris_resource.c:2637` | `iris_batch_flush(batch);` | Trigger: Non-unsynchronized CPU map of a BO referenced by any batch. |
| `src/gallium/drivers/iris/iris_resource.c:2752` | `iris_batch_flush(batch);` | Trigger: CPU texture upload into a BO referenced by a batch. |
| `src/gallium/drivers/iris/iris_fence.c:123` | `iris_batch_flush(other);` | Trigger: Cross-batch wait: submit the other batch so there is a submission to wait on. |
| `src/gallium/drivers/iris/iris_fence.c:313` | `iris_batch_flush(batch);` | Trigger: Non-deferred fence flush: submit every batch of the context. |
| `src/gallium/drivers/iris/iris_fence.c:418` | `iris_batch_flush(batch);` | Trigger: Fence await: flush each batch so queued work precedes the fence wait. |
| `src/gallium/drivers/iris/iris_fence.c:481` | `iris_batch_flush(batch);` | Trigger: Fence finish from the creating context: flush batches whose signal syncobj matches. |
| `src/gallium/drivers/iris/iris_fence.c:676` | `iris_batch_flush(batch);` | Trigger: Fence signal recorded in this batch: submit the batch. |
| `src/gallium/drivers/iris/iris_batch.c:364` | `iris_batch_flush(other_batch);` | Trigger: Cross-batch BO hazard (write/read or write/write) between render, compute and blitter batches. |
| `src/gallium/drivers/iris/iris_batch.c:567` | `iris_batch_flush(batch);` | Trigger: Batch chained to a secondary BO or near BATCH_SZ: flush between draws. |
| `src/gallium/drivers/iris/iris_batch.c:1025` | `iris_batch_flush(batch);` | Trigger: Noop-mode toggle: flush before switching noop state. |
| `src/gallium/drivers/iris/iris_perf.c:38` | `_iris_batch_flush(&ice->batches[IRIS_BATCH_RENDER], __FILE__, __LINE__);` | Trigger: Perf-query support: force a render batch flush. |

Rows: 15

### 5b. `iris_emit_end_of_pipe_sync(` call sites

| Location | Quote | Note |
|---|---|---|
| `src/gallium/drivers/iris/iris_resolve.c:512` | `iris_emit_end_of_pipe_sync(batch, "color resolve: pre-flush",` | Trigger: Color resolve: EOP sync before and after every resolve. |
| `src/gallium/drivers/iris/iris_resolve.c:537` | `iris_emit_end_of_pipe_sync(batch, "color resolve: post-flush",` | Trigger: Color resolve: EOP sync before and after every resolve. |
| `src/gallium/drivers/iris/iris_pipe_control.c:56` | `iris_emit_end_of_pipe_sync(batch, reason,` | Trigger: Flush plus invalidate in one PIPE_CONTROL: split with an EOP sync first. |
| `src/gallium/drivers/iris/iris_pipe_control.c:315` | `iris_emit_end_of_pipe_sync(batch, "cache tracker: flush",` | Trigger: Cache tracker flush for a buffer barrier. |
| `src/gallium/drivers/iris/iris_perf.c:18` | `iris_emit_end_of_pipe_sync(&ice->batches[IRIS_BATCH_RENDER],` | Trigger: Perf stall-at-pixel-scoreboard request. |
| `src/gallium/drivers/iris/iris_state.c:433` | `iris_emit_end_of_pipe_sync(batch,` | Trigger: Every STATE_BASE_ADDRESS change: EOP sync before (includes Wa_18039438632 RT flush). |
| `src/gallium/drivers/iris/iris_state.c:488` | `iris_emit_end_of_pipe_sync(batch,` | Trigger: Every STATE_BASE_ADDRESS change: EOP sync with invalidates after. |
| `src/gallium/drivers/iris/iris_state.c:980` | `iris_emit_end_of_pipe_sync(batch, enable ? "enable preemption"` | Trigger: Gen9-only: object preemption toggled (called only on change). |
| `src/gallium/drivers/iris/iris_state.c:6697` | `iris_emit_end_of_pipe_sync(batch, "Invalidate aux map table",` | Trigger: Aux-map table invalidation per engine. |
| `src/gallium/drivers/iris/iris_state.c:6719` | `iris_emit_end_of_pipe_sync(batch, "Invalidate aux map table",` | Trigger: Aux-map table invalidation per engine. |
| `src/gallium/drivers/iris/iris_clear.c:377` | `iris_emit_end_of_pipe_sync(batch, "fast clear: pre-flush",` | Trigger: Color fast clear: EOP sync before and after every clear. |
| `src/gallium/drivers/iris/iris_clear.c:477` | `iris_emit_end_of_pipe_sync(batch, "fast clear: post flush",` | Trigger: Color fast clear: EOP sync before and after every clear. |

Rows: 12

### 5c. `PIPE_CONTROL_CS_STALL` uses

| Location | Quote | Note |
|---|---|---|
| `src/gallium/drivers/iris/iris_blorp.c:253` | `PIPE_CONTROL_CS_STALL);` | Trigger: 48-bit vertex buffer address transition: VF invalidate with CS stall. |
| `src/gallium/drivers/iris/iris_query.c:143` | `GFX_VER == 9 && devinfo->gt == 4 ?  PIPE_CONTROL_CS_STALL : 0;` | Trigger: Query snapshot write: CS stall to order the write (Gen9 only on GT4 per flag). |
| `src/gallium/drivers/iris/iris_query.c:158` | `enum pipe_control_flags flags = PIPE_CONTROL_CS_STALL \|` | Trigger: Query value write: CS stall for ordering. |
| `src/gallium/drivers/iris/iris_query.c:257` | `PIPE_CONTROL_CS_STALL \|` | Trigger: Stream-output overflow query write: CS stall for ordering. |
| `src/gallium/drivers/iris/iris_resource.c:2265` | `PIPE_CONTROL_CS_STALL);` | Trigger: in `iris_map_copy_region` |
| `src/gallium/drivers/iris/iris_resolve.c:425` | `PIPE_CONTROL_CS_STALL);` | Trigger: Aux-usage mismatch for a BO already tracked in this batch: RT and tile flush with CS stall. |
| `src/gallium/drivers/iris/iris_resolve.c:707` | `PIPE_CONTROL_CS_STALL);` | Trigger: HiZ/depth resolve operation: stall around the HIZ op. |
| `src/gallium/drivers/iris/iris_resolve.c:769` | `PIPE_CONTROL_CS_STALL);` | Trigger: HiZ/depth resolve operation: stall around the HIZ op. |
| `src/gallium/drivers/iris/iris_batch.c:660` | `PIPE_CONTROL_CS_STALL);` | Trigger: End of batch: CS stall before finishing. |
| `src/gallium/drivers/iris/iris_pipe_control.c:58` | `flags &= ~(PIPE_CONTROL_CACHE_FLUSH_BITS \| PIPE_CONTROL_CS_STALL);` | Trigger: Generic flush helper: flag manipulation (CS stall removed when flush+invalidate are split). |
| `src/gallium/drivers/iris/iris_pipe_control.c:130` | `flags \| PIPE_CONTROL_CS_STALL \|` | Trigger: Built into EOP sync: every EOP sync includes a CS stall. |
| `src/gallium/drivers/iris/iris_pipe_control.c:333` | `PIPE_CONTROL_CS_STALL \|` | Trigger: Flush-all-caches helper. |
| `src/gallium/drivers/iris/iris_pipe_control.c:358` | `PIPE_CONTROL_CS_STALL);` | Trigger: Texture barrier: CS stall. |
| `src/gallium/drivers/iris/iris_pipe_control.c:368` | `PIPE_CONTROL_CS_STALL);` | Trigger: Texture barrier: CS stall. |
| `src/gallium/drivers/iris/iris_pipe_control.c:379` | `unsigned bits = PIPE_CONTROL_DATA_CACHE_FLUSH \| PIPE_CONTROL_CS_STALL;` | Trigger: Memory barrier: data cache flush plus CS stall. |
| `src/gallium/drivers/iris/iris_measure.c:181` | `PIPE_CONTROL_CS_STALL,` | Trigger: Measurement snapshot start. |
| `src/gallium/drivers/iris/iris_measure.c:217` | `PIPE_CONTROL_CS_STALL,` | Trigger: Measurement snapshot end. |
| `src/gallium/drivers/iris/iris_blit.c:301` | `iris_emit_pipe_control_flush(batch, reason, PIPE_CONTROL_CS_STALL);` | Trigger: Blit whose source and view formats differ (and BO used earlier in batch): CS stall plus texture cache invalidate. |
| `src/gallium/drivers/iris/iris_state.c:402` | `PIPE_CONTROL_CS_STALL \|` | Trigger: Before a STATE_BASE_ADDRESS change (CS stall component). |
| `src/gallium/drivers/iris/iris_state.c:756` | `enum pipe_control_flags flags = PIPE_CONTROL_CS_STALL \|` | Trigger: Pipeline select switch (3D or GPGPU). |
| `src/gallium/drivers/iris/iris_state.c:793` | `PIPE_CONTROL_CS_STALL);` | Trigger: Pipeline select switch (3D or GPGPU). |
| `src/gallium/drivers/iris/iris_state.c:1688` | `PIPE_CONTROL_CS_STALL \|` | Trigger: Compute context init. |
| `src/gallium/drivers/iris/iris_state.c:1697` | `PIPE_CONTROL_CS_STALL \|` | Trigger: Compute context init. |
| `src/gallium/drivers/iris/iris_state.c:2321` | `PIPE_CONTROL_CS_STALL \|` | Trigger: Gen8-only PMA fix block (`#if GFX_VER == 8`); not emitted on Gen9. |
| `src/gallium/drivers/iris/iris_state.c:6589` | `PIPE_CONTROL_CS_STALL);` | Trigger: Binder realloc: CS stall before 3DSTATE_BINDING_TABLE_POOL_ALLOC. |
| `src/gallium/drivers/iris/iris_state.c:6700` | `PIPE_CONTROL_CS_STALL \|` | Trigger: Aux-map table invalidation per engine. |
| `src/gallium/drivers/iris/iris_state.c:6722` | `PIPE_CONTROL_CS_STALL \|` | Trigger: Aux-map table invalidation per engine. |
| `src/gallium/drivers/iris/iris_state.c:6737` | `* PIPE_CONTROL_CS_STALL. The PIPE_CONTROL::L3 Fabric Flush` | Trigger: Aux-map table invalidation per engine. |
| `src/gallium/drivers/iris/iris_state.c:7170` | `PIPE_CONTROL_CS_STALL);` | Trigger: Wa_16013994831: Gen12+ only; returns early on Gen9. |
| `src/gallium/drivers/iris/iris_state.c:7314` | `PIPE_CONTROL_CS_STALL);` | Trigger: Wa_14024997852 autostrip toggle: CS stall on change. |
| `src/gallium/drivers/iris/iris_state.c:7906` | `(GFX_VERx10 >= 125 ? 0 : PIPE_CONTROL_CS_STALL));` | Trigger: Dirty render state upload: stall for a state or workaround change (see call line). |
| `src/gallium/drivers/iris/iris_state.c:8150` | `PIPE_CONTROL_CS_STALL);` | Trigger: Dirty render state upload: stall for a state or workaround change (see call line). |
| `src/gallium/drivers/iris/iris_state.c:8187` | `PIPE_CONTROL_CS_STALL);` | Trigger: Dirty render state upload: stall for a state or workaround change (see call line). |
| `src/gallium/drivers/iris/iris_state.c:8216` | `PIPE_CONTROL_CS_STALL);` | Trigger: Dirty render state upload: stall for a state or workaround change (see call line). |
| `src/gallium/drivers/iris/iris_state.c:8612` | `PIPE_CONTROL_CS_STALL);` | Trigger: Dirty render state upload: stall for a state or workaround change (see call line). |
| `src/gallium/drivers/iris/iris_state.c:8713` | `PIPE_CONTROL_CS_STALL;` | Trigger: Dirty render state upload: stall for a state or workaround change (see call line). |
| `src/gallium/drivers/iris/iris_state.c:9150` | `PIPE_CONTROL_CS_STALL);` | Trigger: Index buffer change: VF cache invalidate with CS stall. |
| `src/gallium/drivers/iris/iris_state.c:9543` | `PIPE_CONTROL_CS_STALL);` | Trigger: Generated (indirect) draw path: stall around the generation pass. |
| `src/gallium/drivers/iris/iris_state.c:9589` | `PIPE_CONTROL_CS_STALL);` | Trigger: Generated (indirect) draw path: stall around the generation pass. |
| `src/gallium/drivers/iris/iris_state.c:9608` | `PIPE_CONTROL_CS_STALL \|` | Trigger: Generated (indirect) draw path: stall around the generation pass. |
| `src/gallium/drivers/iris/iris_state.c:9632` | `PIPE_CONTROL_CS_STALL);` | Trigger: Generated (indirect) draw path: stall around the generation pass. |
| `src/gallium/drivers/iris/iris_state.c:9947` | `PIPE_CONTROL_CS_STALL);` | Trigger: GPGPU walker dispatch: stall before MEDIA_VFE_STATE/walker. |
| `src/gallium/drivers/iris/iris_state.c:10387` | `if ((flags & PIPE_CONTROL_CS_STALL)) {` | Trigger: Sync-seqno bookkeeping: checks whether CS stall is set. |
| `src/gallium/drivers/iris/iris_state.c:10571` | `flags \|= PIPE_CONTROL_CS_STALL;` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:10621` | `PIPE_CONTROL_CS_STALL, bo, offset, imm);` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:10700` | `flags \|= PIPE_CONTROL_CS_STALL;` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:10755` | `flags \|= PIPE_CONTROL_CS_STALL;` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:10795` | `flags \|= PIPE_CONTROL_CS_STALL;` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:10812` | `flags \|= PIPE_CONTROL_CS_STALL;` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:10834` | `flags \|= PIPE_CONTROL_CS_STALL;` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:10860` | `if (GFX_VER < 9 && (flags & PIPE_CONTROL_CS_STALL)) {` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:10933` | `flags &= (PIPE_CONTROL_CS_STALL \|` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:10961` | `(flags & PIPE_CONTROL_CS_STALL) ? "CS " : "",` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:11028` | `pc.CommandStreamerStallEnable = flags & PIPE_CONTROL_CS_STALL;` | Trigger: Raw PIPE_CONTROL: stall forced by a PRM rule for this flag combination. |
| `src/gallium/drivers/iris/iris_state.c:11237` | `PIPE_CONTROL_CS_STALL,` | Trigger: Gen8-only PMA fix block (`#if GFX_VER == 8`); not emitted on Gen9. |
| `src/gallium/drivers/iris/iris_clear.c:638` | `PIPE_CONTROL_CS_STALL \|` | Trigger: Depth fast clear: CS stall so the clear starts in order. |

Rows: 56

### 5d. `iris_batch_sync_region_start/end` and `iris_batch_sync_boundary` call sites

| Location | Quote | Note |
|---|---|---|
| `src/gallium/drivers/iris/iris_query.c:772` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_get_query_result_resource`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_query.c:791` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_get_query_result_resource`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_query.c:832` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `set_predicate_for_result`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_query.c:889` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `set_predicate_for_result`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_resolve.c:529` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_resolve_color`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_resolve.c:544` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_resolve_color`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_resolve.c:572` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_mcs_exec`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_resolve.c:597` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_mcs_exec`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_resolve.c:709` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_hiz_exec`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_resolve.c:772` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_hiz_exec`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_batch.c:476` | `iris_batch_sync_boundary(batch);` | Sequence-number region marker in `iris_batch_reset`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_blit.c:557` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_blit`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_blit.c:568` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_blit`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_blit.c:726` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_copy_region`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_blit.c:728` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_copy_region`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_blit.c:750` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_copy_region`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_blit.c:756` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_copy_region`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:540` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_load_register_mem32`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:545` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_load_register_mem32`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:556` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_load_register_mem64`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:561` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_load_register_mem64`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:569` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_store_register_mem32`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:578` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_store_register_mem32`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:586` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_store_register_mem64`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:595` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_store_register_mem64`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:603` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_store_data_imm32`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:609` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_store_data_imm32`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:617` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_store_data_imm64`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:623` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_store_data_imm64`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:636` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_copy_mem_mem`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:646` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_copy_mem_mem`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:1342` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_init_render_context`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:1572` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_init_render_context`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:1648` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_init_compute_context`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:1718` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_init_compute_context`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:1724` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_init_copy_context`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:1732` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_init_copy_context`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:6573` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_update_binder_address`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:6635` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_update_binder_address`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:9173` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_upload_render_state`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:9353` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_upload_render_state`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:9380` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_upload_indirect_render_state`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:9453` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_upload_indirect_render_state`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:9478` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_upload_indirect_shader_render_state`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:9637` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_upload_indirect_shader_render_state`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:10104` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_upload_compute_state`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:10149` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_upload_compute_state`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:10385` | `iris_batch_sync_boundary(batch);` | Sequence-number region marker in `batch_mark_sync_for_pipe_control`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:10537` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_emit_raw_pipe_control`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:10559` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_emit_raw_pipe_control`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:10991` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_emit_raw_pipe_control`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:11069` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_emit_raw_pipe_control`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:11154` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `iris_emit_mi_report_perf_count`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_state.c:11160` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `iris_emit_mi_report_perf_count`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_clear.c:414` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `fast_clear_color`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_clear.c:481` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `fast_clear_color`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_clear.c:536` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `clear_color`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_clear.c:551` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `clear_color`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_clear.c:806` | `iris_batch_sync_region_start(batch);` | Sequence-number region marker in `clear_depth_stencil`; no flush by itself (bookkeeping). |
| `src/gallium/drivers/iris/iris_clear.c:820` | `iris_batch_sync_region_end(batch);` | Sequence-number region marker in `clear_depth_stencil`; no flush by itself (bookkeeping). |

Rows: 60

## Top 15 candidates

Most worth improving for Gen9, in rough priority order:

1. `iris_state.c:5703`, `5758`, `3204`: Gen9 rewrites the clear color into each surface state on every fast clear (no clear-address field); batching or caching these writes is the largest Gen9-only per-clear cost.
2. `iris_clear.c:377`, `477`: every color fast clear emits EOP syncs before and after; skipping the pre-flush when no prior draw targets the surface would cut a stall per clear.
3. `iris_resolve.c:512`, `537`: every color resolve emits a pre- and post-EOP sync; the same redundancy as fast clears, on the resolve path.
4. `iris_state.c:433`, `488`: every STATE_BASE_ADDRESS change emits two EOP syncs; fewer SBA re-emits (or no-op detection) helps any binder-churning workload.
5. `iris_blit.c:292`, `301`: on Gen9 a blit whose formats differ gets a CS stall plus texture-cache invalidate (`tex_cache_flush_hack`); Gen11+ narrows the trigger to ASTC.
6. `iris_resolve.c:748`: Gen9 adds a depth cache flush and depth stall after every HiZ op; the nearby comment says HW flushes on HZ_OP, so this needs Gen9 validation before removal.
7. `blorp_genX_exec_brw.h:642`: Gen9 re-emits CC_VIEWPORT on every blorp op because of issue 11647; re-validating that issue would let Gen9 use the cached state.
8. `iris_formats.c:198`, `202`: ASTC 5x5 is disabled on Gen9 (TODO for the sampler WA), so these textures go through st/mesa uncompressed emulation, which costs memory and upload time.
9. `isl.c:2119`-`2123`: Gen9 disables 3D Yf/Ys miptails for CCS surfaces (wide SKL WA); the comment says ICL+ uses a narrower HSD-based form, so the Gen9 limit may be over-broad.
10. `iris_indirect_gen.c:493`, `571` and `iris_draw.c:315`: Gen9 re-emits binding tables and re-reserves binder slots around generated draws; checking whether both are needed per draw could cut redundant state.
11. `iris_state.c:9525`, `9528`: Gen9 adds a VF cache invalidate before generated draws; if the generation pass cannot read stale VF data, this flush is removable on Gen9.
12. `iris_state.c:10592`, `10609`: Gen9 emits a recursive null PIPE_CONTROL before each VF cache invalidate and a CS stall before LRI post-sync in GPGPU mode; both are per-occurrence costs.
13. `iris_state.c:10848`: the stall-workaround list emits more CS stalls than required, and the comment admits it; a precise per-case list would cut stalls on every render path.
14. `iris_screen.c:454`: the dynamic VAO fast path is off for Gen9 with a comment calling it harmful; measuring this on KBL is the way to confirm or retire that choice.
15. `iris_state.c:727`: switching to GPGPU on Gen8/9 clears COLOR_CALC_STATE valid before every pipeline select; compute-heavy mixed workloads pay this each switch.

## Counts

| Section | Rows |
|---|---|
| 1. Features disabled or limited | 51 |
| 2. Gen9-specific workarounds | 31 |
| 3. TODO / FIXME / XXX / HACK | 103 |
| 4. Performance notes | 25 |
| 5a. iris_batch_flush call sites | 15 |
| 5b. iris_emit_end_of_pipe_sync call sites | 12 |
| 5c. PIPE_CONTROL_CS_STALL uses | 56 |
| 5d. sync_region / sync_boundary call sites | 60 |
