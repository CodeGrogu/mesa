# Gen9 (KBL) AVC VDEnc HuC BRC: firmware ABI as implemented by intel/media-driver

## Scope and sources

- Source: shallow clone of `https://github.com/intel/media-driver.git` at `/home/polarbear/.t3/projects/catchingup/media-driver`, commit `6c82293` (2026-11-18). Line numbers refer to that commit.
- `MD` = `/home/polarbear/.t3/projects/catchingup/media-driver/media_driver/agnostic`
- `MC` = `/home/polarbear/.t3/projects/catchingup/media-driver/media_common/agnostic`
- `MDL` = `/home/polarbear/.t3/projects/catchingup/media-driver/media_driver/linux`
- ANV = `/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/genX_huc_brc.h` (read only, not modified)
- Byte offsets and sizes were computed by a script from the verbatim struct declarations (no C compiler is installed in this environment). The script's offsets reproduce every ANV `static_assert` offset (4, 52, 76, 99, 136, 4, 44, 384, 440) and the sizes 192, 448 and 1728. The computation is therefore trustworthy for these structs.
- Nothing under `mesa/` or `linux/` was modified. No GPU workload was run.

## a. Source locations used

**Gen9 KBL (`MD/gen9_kbl/codec/hal/codechal_vdenc_avc_g9_kbl.cpp`)**

- L95-158: `CODECHAL_VDENC_AVC_BRC_INIT_DMEM_G9_KBL` (init DMEM struct). No `static_assert` and no size constant in the file.
- L160-226: `_CODECHAL_VDENC_AVC_BRC_UPDATE_DMEM_G9_KBL` (update DMEM struct). No `static_assert`.
- L360-372: constructor. L369-371 set `m_vdencBrcInitDmemBufferSize = sizeof(...)`, `m_vdencBrcUpdateDmemBufferSize = sizeof(...)`, `m_vdencBrcNumOfSliceOffset = CODECHAL_OFFSETOF(..., NumOfSlice)`.
- L378-388: `InitializeState()`. L386: `m_sliceSizeStreamoutSupported = true;`.
- L504-557: `SetDmemHuCBrcInitReset()`. L518 zeroes the mapped init DMEM. L520 calls the shared template. L524 `INIT_SliceSizeCtrlWA = 1` (unconditional).
- L559-599: `SetDmemHuCBrcUpdate()`. L572 calls the shared template. L576-581 `UPD_SLCSZ_ConsertativeThreshold_U8`. L585 `HmeDistAvailable_U8`. L587-588 width/height in MB. L590 `UPD_TR_TargetSize_U32 = TargetFrameSize * 8`.
- L697 `PopulateBrcInitParam`, L747 `PopulateBrcUpdateParam`, L781 `PopulateEncParam`. These are debug-tool-only readers. L801 comment: `// BRC case: VDENC IMG STATE is updated by HuC FW`.

**Gen9 SKL (for comparison) `MD/gen9_skl/codec/hal/codechal_vdenc_avc_g9_skl.cpp`**

- L94-152 init struct, L154-213 update struct, L289-290 sizes, L368-388 init reset, L393-434 update setter.

**Gen12 (`MD/gen12/codec/hal/codechal_vdenc_avc_g12.cpp`)**

- L312-376: `CodechalVdencAvcStateG12::BrcInitDmem`. L378-463: `CodechalVdencAvcStateG12::BrcUpdateDmem`.
- L624-626 sizes. L684 `m_sliceSizeStreamoutSupported = MEDIA_IS_WA(m_waTable, Wa_22010554215) ? false : true;`
- L1336-1352 `SetBufferToStorePakStatistics()`. L1356-1372 `AddMiStoreForHWOutputToHucDmem()`.
- L1634 `SetDmemHuCBrcInitReset()`. L1720-1765 `SetDmemHuCBrcUpdate()`. L1735-1736: `if (UPD_CurrFrameType_U8 == 1 && RefPicFlag == 1) UPD_CurrFrameType_U8 = 3;`

**Common HuC BRC code (`MD/common/codec/hal/`)**

- `codechal_vdenc_avc.cpp`:
  - L2889-2919 `AvcVdencStoreHuCStatus2Register()` (writes the IMEM-loaded mask and stores HUC_STATUS2).
  - L3959-3968 BRC enable: `CodecHalIsRateControlBrc` (L3959) then `MEDIA_IS_SKU(FtrEnableMediaKernels)` (L3966); error "Failed to load HuC firmware!" if the SKU flag is absent.
  - L4023 BRC disabled (CQP fallback) on invalid VBV/bitrate parameters.
  - L5077-5100 `StoreHucErrorStatus()`. L5092 stores `CODECHAL_VDENC_AVC_BRC_HUC_STATUS_MEMORY_ACCESS_ERROR_MASK`.
  - L5122 `HuCBrcInitReset()`. L5147 init IMEM descriptor. L5160 `SetDmemHuCBrcInitReset()`. L5165-5166 DMEM source and length. L5172-5173 region 0. L5181-5183 HUC_STATUS2 store. L5185 HUC_START.
  - L5235 `HuCBrcUpdate()`. L5263-5271 conditional batch-buffer end keyed to `m_resHucStatus2Buffer` when `m_brcInit || m_brcReset`. L5279 `AddMiStoreForHWOutputToHucDmem`. L5310 `AddVdencBrcImgBuffer` (input SLB). L5326 update IMEM descriptor. L5340 `SetDmemHuCBrcUpdate()`. L5344-5345 DMEM source and length. L5351 `SetConstDataHuCBrcUpdate()`. L5387-5428 virtual address regions (table in section e). L5462 REENCODE bit store.
  - L5569-5610 `SetConstDataHuCBrcUpdate()`. Fills all const-data buffers when `m_brcInit`. L5594 per-frame static-frame ROI adjustment.
  - L6111 `SetBufferToStorePakStatistics()`. L6116 / L6123 call `HuCBrcInitReset` / `HuCBrcUpdate`.
  - L7075-7087 const-data allocation (`GetBRCCostantDataSize()`, page aligned).
  - L7929 `FillHucConstData()`.
  - L8477-8492 `AddVdencBrcImgBuffer()` wrapper. L8651 base `SetBufferToStorePakStatistics()`. L8671-8674 `GetCurrConstDataBufIdx() { return m_avcPicParam->CodingType - 1; }`.
- `codechal_vdenc_avc.h`:
  - L38-45 HUC_STATUS bit masks (REENCODE `1<<31`, ..., MEMORY_ACCESS `1<<26`, DMEM_ERROR `1<<24`).
  - L47 min QP 10. L50-52 history and debug buffer sizes (0x1000) and MB budget size 104.
  - L207-247 `AVCVdencBRCCostantData`, the const-data struct (1728 bytes).
  - L943 `GetBRCCostantDataSize()`, L947 `FillHucConstData()`, L1026-1031 base `AddMiStoreForHWOutputToHucDmem()` (no-op).
  - L1135-1136 `m_vdboxHucVdencBrcInitKernelDescriptor = 4`, `m_vdboxHucVdencBrcUpdateKernelDescriptor = 5`.
  - L1263 `SetDmemHuCBrcInitResetImpl()`. L1280 `BRCFunc_U8 = m_brcInit ? 0 : 2`.
  - L1468 `SetDmemHuCBrcUpdateImpl()`. L1477 `BRCFunc_U8 = 1`. L1531 `UPD_CurrFrameType_U8 = (m_pictureCodingType + 1) % 3`. L1541-1542 PAK pass fields. L1602 `UPD_HMECostEnable_U8 = 1`. L1628 `UPD_SLBB_Size_U16 = m_hwInterface->m_vdencBrcImgStateBufferSize`.
- `codechal_encoder_base.cpp`:
  - L2840-2870 PAK MFC_IMAGE_STATUS_CONTROL MMIO stored into update DMEM (`dwOffset = 7 * sizeof(uint32_t)`, i.e. byte 28).
  - L2936-2952 MFC_BITSTREAM_BYTECOUNT_FRAME stored at `5 * sizeof(uint32_t)` (byte 20); MFC_AVC_NUM_SLICES stored at `m_vdencBrcNumOfSliceOffset` (byte 384 on KBL).
  - L4119-4140 encode-status path: HuC status checks. L4137 `!(encodeStatus->HuCStatus2Reg & GetHucStatus2ImemLoadedMask())` fails the frame.
- `codechal_hw.cpp`: L703-745 `CodechalHwInterface::AddVdencBrcImgBuffer()` (builds the input SLB).
- `codechal_encode_avc_base.h`: L1723 `m_sliceSizeStreamoutSupported = false` (default).
- `MC/codec/shared/codec_def_common.h`: L43-45 `I_TYPE 1`, `P_TYPE 2`, `B_TYPE 3`. L85 `HUC_DMEM_OFFSET_RTOS_GEMS 0x2000`.
- `MC/codec/shared/codec_def_common_avc.h`: L44 `CODECHAL_ENCODE_VDENC_BRC_CONST_BUFFER_NUM (NUM_PIC_TYPES + 1)` (4 buffers).
- `MC/codec/shared/codec_def_common_encode.h`: L550-558 `CodecHalIsRateControlBrc(rc, std)`.
- `MC/common/hw/vdbox/mhw_vdbox.h`: L108 `MHW_VDBOX_HUC_GENERAL_STATE_SHIFT = 6`.
- `MD/common/hw/vdbox/mhw_vdbox_huc_generic.h`: L353 HUC DMEM length in 64-byte units. L386 `bIsWritable` passed through. L330 region shift.
- `MD/common/hw/vdbox/mhw_vdbox_huc_interface.h`: L90 `m_hucStatus2ImemLoadedMask = 0x40`, L167-170 accessor.
- `MD/common/hw/vdbox/mhw_vdbox_vdenc_generic.h`: L303-311 `GetVdencCmd3Size()` and `GetVdencAvcCostStateSize()` both return 0 (not emitted on Gen9).
- `MD/gen9/codec/hal/codechal_hw_g9_X.h`: L60 `m_vdencBrcImgStateBufferSize = VDENC_IMG_STATE(140) + MFX_AVC_IMG_STATE(84) + MI_BATCH_BUFFER_END(4)`. KBL's `CodechalHwInterfaceG9Kbl` (`MD/gen9_kbl/codec/hal/codechal_hw_g9_kbl.h:35`) derives from `CodechalHwInterfaceG9`.
- `MD/gen9_kbl/hw/vdbox/mhw_vdbox_vdenc_hwcmd_g9_kbl.h`: `VDENC_IMG_STATE_CMD` at L1442, `byteSize = 140` at L2299. `MD/gen9_kbl/hw/vdbox/mhw_vdbox_mfx_hwcmd_g9_kbl.h`: `MFX_AVC_IMG_STATE_CMD` at L1764, `byteSize = 84` at L2505. `MD/gen9/hw/mhw_mi_hwcmd_g9_X.h:88`: `MI_BATCH_BUFFER_END` `byteSize = 4`.
- `MDL/gen9/ddi/media_sku_wa_g9.cpp`: `InitSklMediaSku` L120 (`FtrEnableMediaKernels = drvInfo->hasHuc` at L152), `InitBxtMediaSku` L245 (L278), `InitKblMediaSku` L348 (L388), `InitGlkMediaSku` L484 (L522). KBL SKU table entry L609.
- `ANV` (`genX_huc_brc.h`): kernel descriptors L38-39, flags L40-44, init struct L46-108, update struct L110-189, `static_assert`s L191-198, const-data struct L209-251 (`static_assert` 1728 at L256), `ANV_H264_BRC_SLB_SIZE` L280-288, const fill L309-391, init fill L413-511, update fill L513-578, status masks L637-638, DMEM emit L654-689, status L691-738, init L765-806, update L809-880. Region assignment L863-878. PAK fields copied with `mi_memcpy` L845-851.

## b. Gen9 (KBL) DMEM structs, verbatim

Both structs are defined inline in `codechal_vdenc_avc_g9_kbl.cpp`. The sizes are not asserted anywhere in the repository. They are taken from `sizeof` at L369-370. The init DMEM is zeroed before filling (L518). The update DMEM is not zeroed by the setter (see section c). Layout computed as in the Scope section; offsets are relative to the start of each struct.

**BRC init DMEM (CODECHAL_VDENC_AVC_BRC_INIT_DMEM_G9_KBL), sizeof = 192 bytes**

| Off | Size | Declaration (verbatim, from source) |
|---:|---:|---|
| 0 | 1 | `uint8_t     BRCFunc_U8;  // 0: Init; 2: Reset` |
| 1 | 1 | `uint8_t     OpenSourceEnable_U8;  // 0: disable opensource, 1: enable opensource` |
| 2 | 2 | `uint8_t     RVSD[2];` |
| 4 | 2 | `uint16_t    INIT_BRCFlag_U16;  // ICQ or CQP with slice size control: 0x00 CBR: 0x10; VBR: 0x20; VCM: 0x40; LOWDELAY: 0x80.` |
| 6 | 2 | `uint16_t    Reserved;` |
| 8 | 2 | `uint16_t    INIT_FrameWidth_U16;  // Luma width in bytes` |
| 10 | 2 | `uint16_t    INIT_FrameHeight_U16;  // Luma height in bytes` |
| 12 | 4 | `uint32_t    INIT_TargetBitrate_U32;  // target bitrate, set by application` |
| 16 | 4 | `uint32_t    INIT_MinRate_U32;  // 0` |
| 20 | 4 | `uint32_t    INIT_MaxRate_U32;  // Maximum bit rate in bits per second (bps).` |
| 24 | 4 | `uint32_t    INIT_BufSize_U32;  // buffer size` |
| 28 | 4 | `uint32_t    INIT_InitBufFull_U32;  // initial buffer fullness` |
| 32 | 4 | `uint32_t    INIT_ProfileLevelMaxFrame_U32;  // user defined. refer to AVC BRC for conformance check and correction` |
| 36 | 4 | `uint32_t    INIT_FrameRateM_U32;  // FrameRateM is the number of frames in FrameRateD` |
| 40 | 4 | `uint32_t    INIT_FrameRateD_U32;  // If driver gets this FrameRateD from VUI, it is the num_units_in_tick field (32 bits unsigned integer).` |
| 44 | 2 | `uint16_t    INIT_GopP_U16;  // number of P frames in a GOP` |
| 46 | 2 | `uint16_t    INIT_GopB_U16;  // number of B frames in a GOP` |
| 48 | 2 | `uint16_t    INIT_MinQP_U16;  // 10` |
| 50 | 2 | `uint16_t    INIT_MaxQP_U16;  // 51` |
| 52 | 8 | `int8_t      INIT_DevThreshPB0_S8[8];  // lowdelay ? (-45, -33, -23, -15, -8, 0, 15, 25) : (-46, -38, -30, -23, 23, 30, 40, 46)` |
| 60 | 8 | `int8_t      INIT_DevThreshVBR0_S8[8];  // lowdelay ? (-45, -35, -25, -15, -8, 0, 20, 40) : (-46, -40, -32, -23, 56, 64, 83, 93)` |
| 68 | 8 | `int8_t      INIT_DevThreshI0_S8[8];  // lowdelay ? (-40, -30, -17, -10, -5, 0, 10, 20) : (-43, -36, -25, -18, 18, 28, 38, 46)` |
| 76 | 1 | `uint8_t     INIT_InitQPIP;  // Initial QP for I and P` |
| 77 | 1 | `uint8_t     INIT_NotUseRhoDm_U8;  // Reserved` |
| 78 | 1 | `uint8_t     INIT_InitQPB;  // Initial QP for B` |
| 79 | 1 | `uint8_t     INIT_MbQpCtrl_U8;  // Enable MB level QP control (global)` |
| 80 | 1 | `uint8_t     INIT_SliceSizeCtrlEn_U8;  // Enable slice size control` |
| 81 | 3 | `int8_t      INIT_IntraQPDelta_I8[3];  // set to zero for all by default` |
| 84 | 1 | `int8_t      INIT_SkipQPDelta_I8;  // Reserved` |
| 85 | 4 | `int8_t      INIT_DistQPDelta_I8[4];  // lowdelay ? (-5, -2, 2, 5) : (0, 0, 0, 0)` |
| 89 | 1 | `uint8_t     INIT_OscillationQpDelta_U8;  // BRCFLAG_ISVCM ? 16 : 0` |
| 90 | 1 | `uint8_t     INIT_HRDConformanceCheckDisable_U8;  // BRCFLAG_ISAVBR ? 1 : 0` |
| 91 | 1 | `uint8_t     INIT_SkipFrameEnableFlag;` |
| 92 | 1 | `uint8_t     INIT_TopQPDeltaThrForAdapt2Pass_U8;  // =1. QP Delta threshold for second pass.` |
| 93 | 1 | `uint8_t     INIT_TopFrmSzThrForAdapt2Pass_U8;  // lowdelay ? 10 : 50. Top frame size threshold for second pass` |
| 94 | 1 | `uint8_t     INIT_BotFrmSzThrForAdapt2Pass_U8;  // lowdelay ? 10 : 200. Bottom frame size threshold for second pass` |
| 95 | 1 | `uint8_t     INIT_QPSelectForFirstPass_U8;  // lowdelay ? 0 : 1. =0 to use previous frame final QP; or =1 to use (targetQP + previousQP) / 2.` |
| 96 | 1 | `uint8_t     INIT_MBHeaderCompensation_U8;  // Reserved` |
| 97 | 1 | `uint8_t     INIT_OverShootCarryFlag_U8;  // set to zero by default` |
| 98 | 1 | `uint8_t     INIT_OverShootSkipFramePct_U8;  // set to zero by default` |
| 99 | 7 | `uint8_t     INIT_EstRateThreshP0_U8[7];  // 4, 8, 12, 16, 20, 24, 28` |
| 106 | 7 | `uint8_t     INIT_EstRateThreshB0_U8[7];  // 4, 8, 12, 16, 20, 24, 28` |
| 113 | 7 | `uint8_t     INIT_EstRateThreshI0_U8[7];  // 4, 8, 12, 16, 20, 24, 28` |
| 120 | 1 | `uint8_t     INIT_FracQPEnable_U8;  // ExtendedRhoDomainEn from par file` |
| 121 | 1 | `uint8_t     INIT_ScenarioInfo_U8;  // 0: UNKNOWN, 1: DISPLAYREMOTING, 2: VIDEOCONFERENCE, 3: ARCHIVE, 4: LIVESTREAMING.` |
| 122 | 1 | `uint8_t     INIT_StaticRegionStreamIn_U8;  // should be programmed from par file` |
| 123 | 1 | `uint8_t     INIT_DeltaQP_Adaptation_U8;  // =1, should be programmed from par file` |
| 124 | 1 | `uint8_t     INIT_MaxCRFQualityFactor_U8;  // =52, should be programmed from par file` |
| 125 | 1 | `uint8_t     INIT_CRFQualityFactor_U8;  // =25, should be programmed from par file` |
| 126 | 1 | `uint8_t     INIT_BotQPDeltaThrForAdapt2Pass_U8;  // =1. QP Delta threshold for second pass.` |
| 127 | 1 | `uint8_t     INIT_SlidingWindowSize_U8;  // =30, the window size (in frames) used to compute bit rate` |
| 128 | 1 | `uint8_t     INIT_SlidingWidowRCEnable_U8;  // =0, sliding window based rate control (SWRC) disabled, 1: enabled` |
| 129 | 1 | `uint8_t     INIT_SlidingWindowMaxRateRatio_U8;  // =120, ratio between the max rate within the window and average target bitrate` |
| 130 | 1 | `uint8_t     INIT_LowDelayGoldenFrameBoost_U8;  // only for lowdelay mode, 0 (default): no boost for I and scene change frames, 1: boost` |
| 131 | 1 | `uint8_t     INIT_AdaptiveCostEnable_U8;  // 0: disabled, 1: enabled` |
| 132 | 1 | `uint8_t     INIT_AdaptiveHMEExtensionEnable_U8;  // 0: disabled, 1: enabled` |
| 133 | 1 | `uint8_t     INIT_ICQReEncode_U8;  // 0: disabled, 1: enabled` |
| 134 | 1 | `uint8_t     INIT_SliceSizeCtrlWA;  // 0: disabled, 1: enabled` |
| 135 | 1 | `uint8_t     INIT_SinglePassOnly;  // 0: disabled, 1: enabled` |
| 136 | 56 | `uint8_t     RSVD2[56];  // must be zero` |

**BRC update DMEM (_CODECHAL_VDENC_AVC_BRC_UPDATE_DMEM_G9_KBL), sizeof = 448 bytes**

| Off | Size | Declaration (verbatim, from source) |
|---:|---:|---|
| 0 | 1 | `uint8_t     BRCFunc_U8;  // =1 for Update, other values are reserved for future use` |
| 1 | 3 | `uint8_t     RSVD[3];` |
| 4 | 4 | `uint32_t    UPD_TARGETSIZE_U32;  // refer to AVC BRC for calculation` |
| 8 | 4 | `uint32_t    UPD_FRAMENUM_U32;  // frame number` |
| 12 | 4 | `uint32_t    UPD_PeakTxBitsPerFrame_U32;  // current global target bits - previous global target bits (global target bits += input bits per frame)` |
| 16 | 4 | `uint32_t    UPD_FrameBudget_U32;  // target time counter` |
| 20 | 4 | `uint32_t    FrameByteCount;  // PAK output via MMIO` |
| 24 | 4 | `uint32_t    TimingBudgetOverflow;  // PAK output via MMIO` |
| 28 | 4 | `uint32_t    ImgStatusCtrl;  // PAK output via MMIO` |
| 32 | 4 | `uint32_t    IPCMNonConformant;  // PAK output via MMIO` |
| 36 | 8 | `uint16_t    UPD_startGAdjFrame_U16[4];  // 10, 50, 100, 150` |
| 44 | 104 | `uint16_t    UPD_MBBudget_U16[52];  // MB bugdet for QP 0 ï¿½ 51.` |
| 148 | 2 | `uint16_t    UPD_SLCSZ_TARGETSLCSZ_U16;  // target slice size` |
| 150 | 84 | `uint16_t    UPD_SLCSZ_UPD_THRDELTAI_U16[42];  // slice size threshold delta for I frame` |
| 234 | 84 | `uint16_t    UPD_SLCSZ_UPD_THRDELTAP_U16[42];  // slice size threshold delta for P frame` |
| 318 | 2 | `uint16_t    UPD_NumOfFramesSkipped_U16;  // Recording how many frames have been skipped.` |
| 320 | 2 | `uint16_t    UPD_SkipFrameSize_U16;  // Recording the skip frame size for one frame. =NumMBs * 1, assuming one bit per mb for skip frame.` |
| 322 | 2 | `uint16_t    UPD_StaticRegionPct_U16;  // One entry, recording the percentage of static region` |
| 324 | 7 | `uint8_t     UPD_gRateRatioThreshold_U8[7];  // 80,95,99,101,105,125,160` |
| 331 | 1 | `uint8_t     UPD_CurrFrameType_U8;  // I frame: 2; P frame: 0; B frame: 1.` |
| 332 | 5 | `uint8_t     UPD_startGAdjMult_U8[5];  // 1, 1, 3, 2, 1` |
| 337 | 5 | `uint8_t     UPD_startGAdjDiv_U8[5];  // 40, 5, 5, 3, 1` |
| 342 | 8 | `uint8_t     UPD_gRateRatioThresholdQP_U8[8];  // 253,254,255,0,1,1,2,3` |
| 350 | 1 | `uint8_t     UPD_PAKPassNum_U8;  // current pak pass number` |
| 351 | 1 | `uint8_t     UPD_MaxNumPass_U8;  // 2` |
| 352 | 2 | `uint8_t     UPD_SceneChgWidth_U8[2];  // set both to MIN((NumP + 1) / 5, 6)` |
| 354 | 1 | `uint8_t     UPD_SceneChgDetectEn_U8;  // Enable scene change detection` |
| 355 | 1 | `uint8_t     UPD_SceneChgPrevIntraPctThreshold_U8;  // =96. scene change previous intra percentage threshold` |
| 356 | 1 | `uint8_t     UPD_SceneChgCurIntraPctThreshold_U8;  // =192. scene change current intra percentage threshold` |
| 357 | 1 | `uint8_t     UPD_IPAverageCoeff_U8;  // lowdelay ? 0 : 128` |
| 358 | 1 | `uint8_t     UPD_MinQpAdjustment_U8;  // Minimum QP increase step` |
| 359 | 1 | `uint8_t     UPD_TimingBudgetCheck_U8;  // Flag indicating if kernel will check timing budget.` |
| 360 | 4 | `int8_t      reserved_I8[4];  // must be zero` |
| 364 | 1 | `uint8_t     UPD_CQP_QpValue_U8;  // Application specified target QP in BRC_ICQ mode` |
| 365 | 1 | `uint8_t     UPD_CQP_FracQp_U8;  // Application specified fine position in BRC_ICQ mode` |
| 366 | 1 | `uint8_t     UPD_HMEDetectionEnable_U8;  // 0: default, 1: HuC BRC kernel requires information from HME detection kernel output` |
| 367 | 1 | `uint8_t     UPD_HMECostEnable_U8;  // 0: default, 1: driver provides HME cost table` |
| 368 | 1 | `uint8_t     UPD_DisablePFrame8x8Transform_U8;  // 0: enable, 1: disable` |
| 369 | 1 | `uint8_t     UPD_SklCabacWAEnable_U8;  // 0: disable, 1: enable` |
| 370 | 1 | `uint8_t     UPD_ROISource_U8;  // =0: disable, 1: ROIMap from HME Static Region or from App dirty rectangle, 2: ROIMap from App` |
| 371 | 1 | `uint8_t     UPD_SLCSZ_ConsertativeThreshold_U8;  // =0, 0: do not set conservative threshold (suggested for video conference) 1: set conservative threshold for non-video conference` |
| 372 | 2 | `uint16_t    UPD_TargetSliceSize_U16;  // default: 1498, max target slice size from app DDI` |
| 374 | 2 | `uint16_t    UPD_MaxNumSliceAllowed_U16;  // computed by driver based on level idc` |
| 376 | 2 | `uint16_t    UPD_SLBB_Size_U16;  // second level batch buffer (SLBB) size in bytes, the input buffer will contain two SLBBs A and B, A followed by B, A and B have the same structure.` |
| 378 | 2 | `uint16_t    UPD_SLBB_B_Offset_U16;  // offset in bytes from the beginning of the input buffer, it points to the start of SLBB B, set by driver for skip frame support` |
| 380 | 2 | `uint16_t    UPD_AvcImgStateOffset_U16;  // offset in bytes from the beginning of SLBB A` |
| 382 | 2 | `uint16_t    reserved_u16;` |
| 384 | 4 | `uint32_t    NumOfSlice;  // PAK output via MMIO` |
| 388 | 2 | `uint16_t    AveHmeDist_U16;  // default: 0, in HME detection kernel output` |
| 390 | 1 | `uint8_t     HmeDistAvailable_U8;  // 0: disabled, 1: enabled` |
| 391 | 1 | `uint8_t     reserved_u8;  // must be zero` |
| 392 | 2 | `uint16_t    AdditionalFrameSize_U16;  // for slice size control improvement` |
| 394 | 1 | `uint8_t     AddNALHeaderSizeInternally_U8;` |
| 395 | 1 | `uint8_t     UPD_RoiQpViaForceQp_U8;  // HuC does not update StreamIn Buffer, 1: HuC updates StreamIn Buffer` |
| 396 | 4 | `uint32_t    CABACZeroInsertionSize_U32;  // PAK output via MMIO` |
| 400 | 4 | `uint32_t    MiniFramePaddingSize_U32;  // PAK output via MMIO` |
| 404 | 2 | `uint16_t    UPD_WidthInMB_U16;  // width in MB` |
| 406 | 2 | `uint16_t    UPD_HeightInMB_U16;  // height in MB` |
| 408 | 8 | `int8_t      UPD_ROIQpDelta_I8[8];  // Application specified ROI QP Adjustment for Zone0, Zone1, Zone2 and Zone3, Zone4, Zone5, Zone6 and Zone7.` |
| 416 | 4 | `uint32_t    UPD_TR_TargetSize_U32;  // target frame size in TCBRC mode. If zero, TCBRC is disabled.` |
| 420 | 28 | `uint8_t     RSVD2[28];` |

## c. Field-by-field comparison with the ANV / Gen12 layout

- ANV `anv_huc_avc_brc_init_dmem` (192 bytes) and `anv_huc_avc_brc_update_dmem` (448 bytes) are identical to media's Gen12 `BrcInitDmem` / `BrcUpdateDmem` field for field (same name, offset and size). This was checked by script: all 61 init fields and all 78 update fields match.
- Gen9 KBL has the same total sizes (192 and 448) but a different field set in a few places. The table below lists every field. "renamed" means the same offset and size with a different name in Gen12. "moved/resized" means the same name at a different offset or size. "removed" means the Gen9 field has no Gen12 counterpart. "added" means a Gen12 field with no Gen9 counterpart.

**BRC init DMEM: KBL (left) vs Gen12 `BrcInitDmem` (right)**

| KBL field | KBL off | KBL size | Gen12 field | Gen12 off | Gen12 size | Status |
|---|---:|---:|---|---:|---:|---|
| BRCFunc_U8 | 0 | 1 | BRCFunc_U8 | 0 | 1 | identical |
| OpenSourceEnable_U8 | 1 | 1 | OpenSourceEnable_U8 | 1 | 1 | identical |
| RVSD | 2 | 2 | RVSD | 2 | 2 | identical |
| INIT_BRCFlag_U16 | 4 | 2 | INIT_BRCFlag_U16 | 4 | 2 | identical |
| Reserved | 6 | 2 | Reserved | 6 | 2 | identical |
| INIT_FrameWidth_U16 | 8 | 2 | INIT_FrameWidth_U16 | 8 | 2 | identical |
| INIT_FrameHeight_U16 | 10 | 2 | INIT_FrameHeight_U16 | 10 | 2 | identical |
| INIT_TargetBitrate_U32 | 12 | 4 | INIT_TargetBitrate_U32 | 12 | 4 | identical |
| INIT_MinRate_U32 | 16 | 4 | INIT_MinRate_U32 | 16 | 4 | identical |
| INIT_MaxRate_U32 | 20 | 4 | INIT_MaxRate_U32 | 20 | 4 | identical |
| INIT_BufSize_U32 | 24 | 4 | INIT_BufSize_U32 | 24 | 4 | identical |
| INIT_InitBufFull_U32 | 28 | 4 | INIT_InitBufFull_U32 | 28 | 4 | identical |
| INIT_ProfileLevelMaxFrame_U32 | 32 | 4 | INIT_ProfileLevelMaxFrame_U32 | 32 | 4 | identical |
| INIT_FrameRateM_U32 | 36 | 4 | INIT_FrameRateM_U32 | 36 | 4 | identical |
| INIT_FrameRateD_U32 | 40 | 4 | INIT_FrameRateD_U32 | 40 | 4 | identical |
| INIT_GopP_U16 | 44 | 2 | INIT_GopP_U16 | 44 | 2 | identical |
| INIT_GopB_U16 | 46 | 2 | INIT_GopB_U16 | 46 | 2 | identical |
| INIT_MinQP_U16 | 48 | 2 | INIT_MinQP_U16 | 48 | 2 | identical |
| INIT_MaxQP_U16 | 50 | 2 | INIT_MaxQP_U16 | 50 | 2 | identical |
| INIT_DevThreshPB0_S8 | 52 | 8 | INIT_DevThreshPB0_S8 | 52 | 8 | identical |
| INIT_DevThreshVBR0_S8 | 60 | 8 | INIT_DevThreshVBR0_S8 | 60 | 8 | identical |
| INIT_DevThreshI0_S8 | 68 | 8 | INIT_DevThreshI0_S8 | 68 | 8 | identical |
| INIT_InitQPIP | 76 | 1 | INIT_InitQPIP | 76 | 1 | identical |
| INIT_NotUseRhoDm_U8 | 77 | 1 | INIT_NotUseRhoDm_U8 | 77 | 1 | identical |
| INIT_InitQPB | 78 | 1 | INIT_InitQPB | 78 | 1 | identical |
| INIT_MbQpCtrl_U8 | 79 | 1 | INIT_MbQpCtrl_U8 | 79 | 1 | identical |
| INIT_SliceSizeCtrlEn_U8 | 80 | 1 | INIT_SliceSizeCtrlEn_U8 | 80 | 1 | identical |
| INIT_IntraQPDelta_I8 | 81 | 3 | INIT_IntraQPDelta_I8 | 81 | 3 | identical |
| INIT_SkipQPDelta_I8 | 84 | 1 | INIT_SkipQPDelta_I8 | 84 | 1 | identical |
| INIT_DistQPDelta_I8 | 85 | 4 | INIT_DistQPDelta_I8 | 85 | 4 | identical |
| INIT_OscillationQpDelta_U8 | 89 | 1 | INIT_OscillationQpDelta_U8 | 89 | 1 | identical |
| INIT_HRDConformanceCheckDisable_U8 | 90 | 1 | INIT_HRDConformanceCheckDisable_U8 | 90 | 1 | identical |
| INIT_SkipFrameEnableFlag | 91 | 1 | INIT_SkipFrameEnableFlag | 91 | 1 | identical |
| INIT_TopQPDeltaThrForAdapt2Pass_U8 | 92 | 1 | INIT_TopQPDeltaThrForAdapt2Pass_U8 | 92 | 1 | identical |
| INIT_TopFrmSzThrForAdapt2Pass_U8 | 93 | 1 | INIT_TopFrmSzThrForAdapt2Pass_U8 | 93 | 1 | identical |
| INIT_BotFrmSzThrForAdapt2Pass_U8 | 94 | 1 | INIT_BotFrmSzThrForAdapt2Pass_U8 | 94 | 1 | identical |
| INIT_QPSelectForFirstPass_U8 | 95 | 1 | INIT_QPSelectForFirstPass_U8 | 95 | 1 | identical |
| INIT_MBHeaderCompensation_U8 | 96 | 1 | INIT_MBHeaderCompensation_U8 | 96 | 1 | identical |
| INIT_OverShootCarryFlag_U8 | 97 | 1 | INIT_OverShootCarryFlag_U8 | 97 | 1 | identical |
| INIT_OverShootSkipFramePct_U8 | 98 | 1 | INIT_OverShootSkipFramePct_U8 | 98 | 1 | identical |
| INIT_EstRateThreshP0_U8 | 99 | 7 | INIT_EstRateThreshP0_U8 | 99 | 7 | identical |
| INIT_EstRateThreshB0_U8 | 106 | 7 | INIT_EstRateThreshB0_U8 | 106 | 7 | identical |
| INIT_EstRateThreshI0_U8 | 113 | 7 | INIT_EstRateThreshI0_U8 | 113 | 7 | identical |
| INIT_FracQPEnable_U8 | 120 | 1 | INIT_FracQPEnable_U8 | 120 | 1 | identical |
| INIT_ScenarioInfo_U8 | 121 | 1 | INIT_ScenarioInfo_U8 | 121 | 1 | identical |
| INIT_StaticRegionStreamIn_U8 | 122 | 1 | INIT_StaticRegionStreamIn_U8 | 122 | 1 | identical |
| INIT_DeltaQP_Adaptation_U8 | 123 | 1 | INIT_DeltaQP_Adaptation_U8 | 123 | 1 | identical |
| INIT_MaxCRFQualityFactor_U8 | 124 | 1 | INIT_MaxCRFQualityFactor_U8 | 124 | 1 | identical |
| INIT_CRFQualityFactor_U8 | 125 | 1 | INIT_CRFQualityFactor_U8 | 125 | 1 | identical |
| INIT_BotQPDeltaThrForAdapt2Pass_U8 | 126 | 1 | INIT_BotQPDeltaThrForAdapt2Pass_U8 | 126 | 1 | identical |
| INIT_SlidingWindowSize_U8 | 127 | 1 | INIT_SlidingWindowSize_U8 | 127 | 1 | identical |
| INIT_SlidingWidowRCEnable_U8 | 128 | 1 | INIT_SlidingWidowRCEnable_U8 | 128 | 1 | identical |
| INIT_SlidingWindowMaxRateRatio_U8 | 129 | 1 | INIT_SlidingWindowMaxRateRatio_U8 | 129 | 1 | identical |
| INIT_LowDelayGoldenFrameBoost_U8 | 130 | 1 | INIT_LowDelayGoldenFrameBoost_U8 | 130 | 1 | identical |
| INIT_AdaptiveCostEnable_U8 | 131 | 1 | INIT_AdaptiveCostEnable_U8 | 131 | 1 | identical |
| INIT_AdaptiveHMEExtensionEnable_U8 | 132 | 1 | INIT_AdaptiveHMEExtensionEnable_U8 | 132 | 1 | identical |
| INIT_ICQReEncode_U8 | 133 | 1 | INIT_ICQReEncode_U8 | 133 | 1 | identical |
| INIT_SliceSizeCtrlWA | 134 | 1 | INIT_LookaheadDepth_U8 | 134 | 1 | renamed |
| INIT_SinglePassOnly | 135 | 1 | INIT_SinglePassOnly | 135 | 1 | identical |
| RSVD2 | 136 | 56 | RSVD2 | 137 | 55 | moved/resized in Gen12 (Gen12 off 137, size 55) |
| (none) | - | - | INIT_New_DeltaQP_Adaptation_U8 | 136 | 1 | added (overlaps KBL RSVD2) |

**BRC update DMEM: KBL (left) vs Gen12 `BrcUpdateDmem` (right)**

| KBL field | KBL off | KBL size | Gen12 field | Gen12 off | Gen12 size | Status |
|---|---:|---:|---|---:|---:|---|
| BRCFunc_U8 | 0 | 1 | BRCFunc_U8 | 0 | 1 | identical |
| RSVD | 1 | 3 | RSVD | 1 | 3 | identical |
| UPD_TARGETSIZE_U32 | 4 | 4 | UPD_TARGETSIZE_U32 | 4 | 4 | identical |
| UPD_FRAMENUM_U32 | 8 | 4 | UPD_FRAMENUM_U32 | 8 | 4 | identical |
| UPD_PeakTxBitsPerFrame_U32 | 12 | 4 | UPD_PeakTxBitsPerFrame_U32 | 12 | 4 | identical |
| UPD_FrameBudget_U32 | 16 | 4 | UPD_FrameBudget_U32 | 16 | 4 | identical |
| FrameByteCount | 20 | 4 | FrameByteCount | 20 | 4 | identical |
| TimingBudgetOverflow | 24 | 4 | TimingBudgetOverflow | 24 | 4 | identical |
| ImgStatusCtrl | 28 | 4 | ImgStatusCtrl | 28 | 4 | identical |
| IPCMNonConformant | 32 | 4 | IPCMNonConformant | 32 | 4 | identical |
| UPD_startGAdjFrame_U16 | 36 | 8 | UPD_startGAdjFrame_U16 | 36 | 8 | identical |
| UPD_MBBudget_U16 | 44 | 104 | UPD_MBBudget_U16 | 44 | 104 | identical |
| UPD_SLCSZ_TARGETSLCSZ_U16 | 148 | 2 | UPD_SLCSZ_TARGETSLCSZ_U16 | 148 | 2 | identical |
| UPD_SLCSZ_UPD_THRDELTAI_U16 | 150 | 84 | UPD_SLCSZ_UPD_THRDELTAI_U16 | 150 | 84 | identical |
| UPD_SLCSZ_UPD_THRDELTAP_U16 | 234 | 84 | UPD_SLCSZ_UPD_THRDELTAP_U16 | 234 | 84 | identical |
| UPD_NumOfFramesSkipped_U16 | 318 | 2 | UPD_NumOfFramesSkipped_U16 | 318 | 2 | identical |
| UPD_SkipFrameSize_U16 | 320 | 2 | UPD_SkipFrameSize_U16 | 320 | 2 | identical |
| UPD_StaticRegionPct_U16 | 322 | 2 | UPD_StaticRegionPct_U16 | 322 | 2 | identical |
| UPD_gRateRatioThreshold_U8 | 324 | 7 | UPD_gRateRatioThreshold_U8 | 324 | 7 | identical |
| UPD_CurrFrameType_U8 | 331 | 1 | UPD_CurrFrameType_U8 | 331 | 1 | identical |
| UPD_startGAdjMult_U8 | 332 | 5 | UPD_startGAdjMult_U8 | 332 | 5 | identical |
| UPD_startGAdjDiv_U8 | 337 | 5 | UPD_startGAdjDiv_U8 | 337 | 5 | identical |
| UPD_gRateRatioThresholdQP_U8 | 342 | 8 | UPD_gRateRatioThresholdQP_U8 | 342 | 8 | identical |
| UPD_PAKPassNum_U8 | 350 | 1 | UPD_PAKPassNum_U8 | 350 | 1 | identical |
| UPD_MaxNumPass_U8 | 351 | 1 | UPD_MaxNumPass_U8 | 351 | 1 | identical |
| UPD_SceneChgWidth_U8 | 352 | 2 | UPD_SceneChgWidth_U8 | 352 | 2 | identical |
| UPD_SceneChgDetectEn_U8 | 354 | 1 | UPD_SceneChgDetectEn_U8 | 354 | 1 | identical |
| UPD_SceneChgPrevIntraPctThreshold_U8 | 355 | 1 | UPD_SceneChgPrevIntraPctThreshold_U8 | 355 | 1 | identical |
| UPD_SceneChgCurIntraPctThreshold_U8 | 356 | 1 | UPD_SceneChgCurIntraPctThreshold_U8 | 356 | 1 | identical |
| UPD_IPAverageCoeff_U8 | 357 | 1 | UPD_IPAverageCoeff_U8 | 357 | 1 | identical |
| UPD_MinQpAdjustment_U8 | 358 | 1 | UPD_MinQpAdjustment_U8 | 358 | 1 | identical |
| UPD_TimingBudgetCheck_U8 | 359 | 1 | UPD_TimingBudgetCheck_U8 | 359 | 1 | identical |
| reserved_I8 | 360 | 4 | reserved_I8 | 360 | 4 | identical |
| UPD_CQP_QpValue_U8 | 364 | 1 | UPD_CQP_QpValue_U8 | 364 | 1 | identical |
| UPD_CQP_FracQp_U8 | 365 | 1 | UPD_CQP_FracQp_U8 | 365 | 1 | identical |
| UPD_HMEDetectionEnable_U8 | 366 | 1 | UPD_HMEDetectionEnable_U8 | 366 | 1 | identical |
| UPD_HMECostEnable_U8 | 367 | 1 | UPD_HMECostEnable_U8 | 367 | 1 | identical |
| UPD_DisablePFrame8x8Transform_U8 | 368 | 1 | UPD_DisablePFrame8x8Transform_U8 | 368 | 1 | identical |
| UPD_SklCabacWAEnable_U8 | 369 | 1 | RSVD3 | 369 | 1 | renamed |
| UPD_ROISource_U8 | 370 | 1 | UPD_ROISource_U8 | 370 | 1 | identical |
| UPD_SLCSZ_ConsertativeThreshold_U8 | 371 | 1 | RSVD4 | 371 | 1 | renamed |
| UPD_TargetSliceSize_U16 | 372 | 2 | UPD_TargetSliceSize_U16 | 372 | 2 | identical |
| UPD_MaxNumSliceAllowed_U16 | 374 | 2 | UPD_MaxNumSliceAllowed_U16 | 374 | 2 | identical |
| UPD_SLBB_Size_U16 | 376 | 2 | UPD_SLBB_Size_U16 | 376 | 2 | identical |
| UPD_SLBB_B_Offset_U16 | 378 | 2 | UPD_SLBB_B_Offset_U16 | 378 | 2 | identical |
| UPD_AvcImgStateOffset_U16 | 380 | 2 | UPD_AvcImgStateOffset_U16 | 380 | 2 | identical |
| reserved_u16 | 382 | 2 | reserved_u16 | 382 | 2 | identical |
| NumOfSlice | 384 | 4 | NumOfSlice | 384 | 4 | identical |
| AveHmeDist_U16 | 388 | 2 | AveHmeDist_U16 | 388 | 2 | identical |
| HmeDistAvailable_U8 | 390 | 1 | HmeDistAvailable_U8 | 390 | 1 | identical |
| reserved_u8 | 391 | 1 | DisableDMA | 391 | 1 | renamed |
| AdditionalFrameSize_U16 | 392 | 2 | AdditionalFrameSize_U16 | 392 | 2 | identical |
| AddNALHeaderSizeInternally_U8 | 394 | 1 | AddNALHeaderSizeInternally_U8 | 394 | 1 | identical |
| UPD_RoiQpViaForceQp_U8 | 395 | 1 | UPD_RoiQpViaForceQp_U8 | 395 | 1 | identical |
| CABACZeroInsertionSize_U32 | 396 | 4 | CABACZeroInsertionSize_U32 | 396 | 4 | identical |
| MiniFramePaddingSize_U32 | 400 | 4 | MiniFramePaddingSize_U32 | 400 | 4 | identical |
| UPD_WidthInMB_U16 | 404 | 2 | UPD_WidthInMB_U16 | 404 | 2 | identical |
| UPD_HeightInMB_U16 | 406 | 2 | UPD_HeightInMB_U16 | 406 | 2 | identical |
| UPD_ROIQpDelta_I8 | 408 | 8 | UPD_ROIQpDelta_I8 | 408 | 8 | identical |
| UPD_TR_TargetSize_U32 | 416 | 4 | (none) | - | - | removed (bytes reused: HME0XOffset_I8@416/1, HME0YOffset_I8@417/1, HME1XOffset_I8@418/1, HME1YOffset_I8@419/1) |
| RSVD2 | 420 | 28 | RSVD2 | 444 | 4 | moved/resized in Gen12 (Gen12 off 444, size 4) |
| (none) | - | - | MOTION_ADAPTIVE_G4 | 420 | 1 | added (overlaps KBL RSVD2) |
| (none) | - | - | EnableLookAhead | 421 | 1 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_LA_Data_Offset_U8 | 422 | 1 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_CQMEnabled_U8 | 423 | 1 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_LA_TargetSize_U32 | 424 | 4 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_LA_TargetFulness_U32 | 428 | 4 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_Delta_U8 | 432 | 1 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_ROM_CURRENT_U8 | 433 | 1 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_ROM_ZERO_U8 | 434 | 1 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_TCBRC_SCENARIO_U8 | 435 | 1 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_EnableFineGrainLA | 436 | 1 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_DeltaQpDcOffset | 437 | 1 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_NumSlicesForRounding | 438 | 2 | added (overlaps KBL RSVD2) |
| (none) | - | - | UPD_UserMaxFramePB | 440 | 4 | added (overlaps KBL RSVD2) |

Semantic differences in the bytes that the layouts share (these matter for a port, not for the layout):

- **Init byte 134**: KBL writes `INIT_SliceSizeCtrlWA = 1` unconditionally (KBL L524). ANV and Gen12 call this byte `INIT_LookaheadDepth_U8` and never set it (ANV genX_huc_brc.h L104 is the only mention; not written). A Gen9 port must write 1 here or the firmware sees a different value.
- **Update bytes 369 and 371**: KBL writes `UPD_SklCabacWAEnable_U8` (not written by KBL code, so zero) and `UPD_SLCSZ_ConsertativeThreshold_U8` (KBL L576-581: `RateControlMethod != RATECONTROL_VCM` when `EnableSliceLevelRateCtrl`, else 0). Gen12/ANV call these RSVD3 and RSVD4 (must be zero). A port that copies the Gen12 zero-fill does not reproduce KBL's slice-size conservative threshold.
- **Update byte 391**: KBL `reserved_u8` (must be zero). Gen12 `DisableDMA` (0 = use DMA; ANV leaves it 0).
- **Update bytes 416-419**: KBL writes `UPD_TR_TargetSize_U32 = TargetFrameSize * 8` (KBL L590). The field's comment says a zero value disables TCBRC. Gen12 uses these bytes for `HME0XOffset_I8`, `HME0YOffset_I8`, `HME1XOffset_I8`, `HME1YOffset_I8`, and sets them only when `INIT_AdaptiveHMEExtensionEnable_U8` is set (Gen12 L1745-1748). ANV does not write byte 416 and must not write TR target on Gen9 unless TCBRC is wanted.
- **Update bytes 420-447**: KBL reserved (`RSVD2[28]`). Gen12 uses them for `MOTION_ADAPTIVE_G4`, `EnableLookAhead`, the LA target fields, `UPD_NumSlicesForRounding`, and `UPD_UserMaxFramePB` (Gen12 L1779-1780). KBL leaves them zero.
- **Update byte 420/444 and related**: Gen12 `UPD_CurrFrameType_U8` is rewritten to 3 for reference B frames (Gen12 L1735-1736). KBL has no such override, so B frames are always 1 on Gen9.
- **Update buffer not zeroed**: neither the KBL nor the Gen12 update setter zeroes the mapped DMEM (only the lock-flags struct is zeroed, KBL L567). Fields not written keep whatever the recycled buffer held. ANV zero-fills (`memset`, genX_huc_brc.h L832). Gen9 KBL sets fewer fields, so the stale-data risk is larger there.
- **Value-level choices present in both media paths (not layout)**: `INIT_MinRate_U32` is `avcSeqParams->MinBitRate` in the common setter (`codechal_vdenc_avc.h` L1286). ANV sets it to the target bitrate (genX_huc_brc.h L443). `INIT_FracQPEnable_U8` is `IsRhoDomainStatsEnabled()` on KBL (KBL L523), always 1 in ANV.
- **Gen9 vs SKL**: SKL's update struct is 452 bytes (not 448), has `UPD_ROIQpDelta_I8[4]` and `RSVD3[59]`, and has no `NumOfSlice`. SKL's init struct is 192 bytes.

## d. BRC constant-data buffer (region 5)

- Structure: `AVCVdencBRCCostantData` (`codechal_vdenc_avc.h:207-247`), 1728 bytes. This is the same as ANV's `anv_huc_avc_brc_const_data` (the `GFX_VERx10 < 125` branch, 1728 bytes). Gen9 KBL and Gen12 both use the common `GetBRCCostantDataSize()` (`sizeof(AVCVdencBRCCostantData)`, L943) and the common `FillHucConstData()` (L7929). Neither gen9 nor gen12 overrides either function (only `Xe_HPM` does).
- Buffers: 4 buffers (`CODECHAL_ENCODE_VDENC_BRC_CONST_BUFFER_NUM = NUM_PIC_TYPES + 1`, `codec_def_common_avc.h:44`), one per picture type plus reference B. All four are filled when `m_brcInit` is set (L5581-5587). Region 5 is bound to `m_resVdencBrcConstDataBuffer[GetCurrConstDataBufIdx()]` (L5394), and `GetCurrConstDataBufIdx() = CodingType - 1` (L8671-8674).
- Layout (verbatim field declarations, offsets within the buffer):

**AVCVdencBRCCostantData (CD), sizeof = 1728 bytes; ANV `anv_huc_avc_brc_const_data` is identical field-for-field (1728 bytes, GFX_VERx10 < 125 branch)**

| Off | Size | Field (verbatim declaration) |
|---:|---:|---|
| 0 | 64 | `uint8_t     UPD_GlobalRateQPAdjTabI_U8[64];` |
| 64 | 64 | `uint8_t     UPD_GlobalRateQPAdjTabP_U8[64];` |
| 128 | 64 | `uint8_t     UPD_GlobalRateQPAdjTabB_U8[64];` |
| 192 | 10 | `uint8_t     UPD_DistThreshldI_U8[10];` |
| 202 | 10 | `uint8_t     UPD_DistThreshldP_U8[10];` |
| 212 | 10 | `uint8_t     UPD_DistThreshldB_U8[10];` |
| 222 | 81 | `uint8_t     UPD_DistQPAdjTabI_U8[81];` |
| 303 | 81 | `uint8_t     UPD_DistQPAdjTabP_U8[81];` |
| 384 | 81 | `uint8_t     UPD_DistQPAdjTabB_U8[81];` |
| 465 | 72 | `int8_t      UPD_BufRateAdjTabI_S8[72];` |
| 537 | 72 | `int8_t      UPD_BufRateAdjTabP_S8[72];` |
| 609 | 72 | `int8_t      UPD_BufRateAdjTabB_S8[72];` |
| 681 | 9 | `uint8_t     UPD_FrmSzMinTabP_U8[9];` |
| 690 | 9 | `uint8_t     UPD_FrmSzMinTabB_U8[9];` |
| 699 | 9 | `uint8_t     UPD_FrmSzMinTabI_U8[9];` |
| 708 | 9 | `uint8_t     UPD_FrmSzMaxTabP_U8[9];` |
| 717 | 9 | `uint8_t     UPD_FrmSzMaxTabB_U8[9];` |
| 726 | 9 | `uint8_t     UPD_FrmSzMaxTabI_U8[9];` |
| 735 | 9 | `uint8_t     UPD_FrmSzSCGTabP_U8[9];` |
| 744 | 9 | `uint8_t     UPD_FrmSzSCGTabB_U8[9];` |
| 753 | 9 | `uint8_t     UPD_FrmSzSCGTabI_U8[9];` |
| 762 | 42 | `uint8_t     UPD_I_IntraNonPred[42];` |
| 804 | 42 | `uint8_t     UPD_I_Intra16x16[42];` |
| 846 | 42 | `uint8_t     UPD_I_Intra8x8[42];` |
| 888 | 42 | `uint8_t     UPD_I_Intra4x4[42];` |
| 930 | 42 | `uint8_t     UPD_I_IntraChroma[42];` |
| 972 | 42 | `uint8_t     UPD_P_IntraNonPred[42];` |
| 1014 | 42 | `uint8_t     UPD_P_Intra16x16[42];` |
| 1056 | 42 | `uint8_t     UPD_P_Intra8x8[42];` |
| 1098 | 42 | `uint8_t     UPD_P_Intra4x4[42];` |
| 1140 | 42 | `uint8_t     UPD_P_IntraChroma[42];` |
| 1182 | 42 | `uint8_t     UPD_P_Inter16x8[42];` |
| 1224 | 42 | `uint8_t     UPD_P_Inter8x8[42];` |
| 1266 | 42 | `uint8_t     UPD_P_Inter16x16[42];` |
| 1308 | 42 | `uint8_t     UPD_P_RefId[42];` |
| 1350 | 336 | `uint8_t     UPD_HMEMVCost[8][42];` |
| 1686 | 42 | `uint8_t     RSVD[42];` |

- ANV fills the same layout per frame (`anv_h264_brc_fill_const_data`, genX_huc_brc.h L309-391) using `anv_video_brc_tables.h`. Table values were not compared here.

## e. HuC virtual-address regions and kernel descriptors

**Kernel descriptors**: init = 4 and update = 5 (`codechal_vdenc_avc.h:1135-1136`, used at `codechal_vdenc_avc.cpp:5147` and `:5326`). No Gen9 or Gen12 override exists. ANV uses the same values (`genX_huc_brc.h:38-39`). IMEM is loaded by `AddHucImemStateCmd(descriptor)`. DMEM is loaded to HuC offset `HUC_DMEM_OFFSET_RTOS_GEMS = 0x2000` (`codec_def_common.h:85`) with length in 64-byte units (`mhw_vdbox_huc_generic.h:353`, shift 6). ANV uses the same destination (`ANV_HUC_DMEM_DEST_OFFSET 0x2000`, `genX_huc.h:40`) and `align(size, 64) / 64` (genX_huc_brc.h L686).

**BRC init** (`HuCBrcInitReset`, `codechal_vdenc_avc.cpp:5122`). Only region 0 is bound:

| Region | Media buffer | Access | Source | ANV (`genX_huc_brc.h` L789-791) |
|---:|---|---|---|---|
| 0 | `m_resVdencBrcHistoryBuffer` (BRC history, 0x1000 bytes) | write (`isWritable = true`) | L5172-5173 | `ANV_VID_MEM_BRC_HISTORY`, same |

**BRC update** (`HuCBrcUpdate`, `codechal_vdenc_avc.cpp:5235`):

| Region | Media buffer | Access | Bound when | Source | ANV (`genX_huc_brc.h` L866-872) |
|---:|---|---|---|---|---|
| 0 | `m_resVdencBrcHistoryBuffer` | write | always | L5172-5173 and L5408-5409 | `BRC_HISTORY`, yes |
| 1 | `m_vdencStatsBuffer` (VDEnc stats) | read | always | L5387 | `BRC_VDENC_STATS`, yes |
| 2 | `m_pakStatsBuffer` (PAK stats) | read | always | L5388 | `BRC_PAK_STATS`, yes |
| 3 | `m_resVdencBrcImageStatesReadBuffer[cur]` (input SLB) | read | always | L5389 | input SLB, yes |
| 4 | `m_resSfdOutputBuffer[cur]` (static frame detection) | read | `m_staticFrameDetectionInUse` (L5972) | L5391-5392 | no |
| 5 | `m_resVdencBrcConstDataBuffer[CodingType-1]` | read | always | L5394 | const data, yes |
| 6 | `m_batchBufferForVdencImgStat[0]` (output SLB) | write (`isWritable = true`) | always | L5410-5411 | `BRC_EXEC_SLB`, yes |
| 7 | `m_pakSliceSizeStreamoutBuffer` | (not marked writable) | `m_sliceSizeStreamoutSupported` (KBL L386 = true; Gen12 L684 unless Wa_22010554215; default false) | L5427-5428 | no |
| 8 | `m_resVdencBrcRoiBuffer[cur]` | read | non-native BRC ROI only | L5403 | no |
| 9 | `m_resVdencStreamInBuffer[cur]` | read | non-native BRC ROI only | L5404 | no |
| 10 | `m_resVdencStreamInBuffer[cur]` | write | non-native BRC ROI only | L5419-5420 | no |
| 15 | `m_resVdencBrcDbgBuffer` (debug, 0x1000 bytes) | not marked writable (no `isWritable`) | always | L5424 | `BRC_DEBUG`, yes |

KBL-specific: region 7 is bound on KBL (`m_sliceSizeStreamoutSupported = true`, KBL L386). ANV does not bind it. Gen12 binds it too unless `Wa_22010554215` applies, so ANV also differs from media Gen12 here. Region 4 and regions 8-10 depend on runtime features (SFD and non-native ROI) that ANV does not implement.

The host copies `m_resVdencBrcUpdateDmemBuffer` for PAK feedback (section g) into the update DMEM; that is not a region.

## f. Gen9 / KBL conditions and HuC status handling

1. **Platform gate**: `FtrEnableMediaKernels = drvInfo->hasHuc` is set in `InitKblMediaSku` (`MDL/gen9/ddi/media_sku_wa_g9.cpp:388`). The same rule applies to SKL (L152), BXT (L278) and GLK (L522). **KBL is not special**: iHD uses HuC BRC on SKL, BXT, GLK and KBL whenever `hasHuc` is set. SKL has its own class (`CodechalVdencAvcStateG9Skl`) with its own DMEM structs (section c).
2. **Encoder gate** (`codechal_vdenc_avc.cpp:3963-3968`): if `CodecHalIsRateControlBrc(RateControlMethod, CODECHAL_AVC)` is true (`codec_def_common_encode.h:550-558`: CBR, VBR, AVBR and CQL always, plus VCM, ICQ, QVBR and IWD_VBR for AVC), then, with the SKU flag absent, the encoder fails with "Failed to load HuC firmware!" (`MOS_STATUS_UNKNOWN`). Otherwise `m_vdencBrcEnabled = FtrEnableMediaKernels`.
3. **Parameter fallback** (L4023): if VBV size, initial fullness, max bitrate, target bitrate or FramesPer100Sec are zero (exceptions for AVBR and ICQ), BRC is disabled and encoding continues in CQP.
4. **KBL-only state**: `m_sliceSizeStreamoutSupported = true` (KBL L386). Default is `false` (`codechal_encode_avc_base.h:1723`).
5. **KBL-only DMEM choices**: `INIT_SliceSizeCtrlWA = 1` always (L524); `UPD_TR_TargetSize_U32` from `TargetFrameSize * 8` (L590); `UPD_SLCSZ_ConsertativeThreshold_U8` (L576-581); `HmeDistAvailable_U8 = 1` for P frames when 16x ME is supported (L585).
6. **HuC status and authentication as observed by the driver**:
   - The driver reads and checks the HUC_STATUS2 "VALID IMEM LOADED" bit, `GetHucStatus2ImemLoadedMask() = 0x40` (`mhw_vdbox_huc_interface.h:90`). Before HUC_START, `AvcVdencStoreHuCStatus2Register` writes the mask to `m_resHucStatus2Buffer` and stores the HUC_STATUS2 MMIO register there (`codechal_vdenc_avc.cpp:2889-2919`, called at L5181 and L5438).
   - For BRC init and reset, a `MI_CONDITIONAL_BATCH_BUFFER_END` keyed to `m_resHucStatus2Buffer` is inserted before the update DMEM (L5263-5271).
   - After the frame, the encoder status path reads the stored HuCStatus2Reg and fails the frame if the IMEM-loaded bit is clear (`codechal_encoder_base.cpp:4137`). The FtrEnableMediaKernels-absent branch returns `MOS_STATUS_HUC_KERNEL_FAILED` a few lines above (L4119-4136).
   - Per-frame HuC errors: `StoreHucErrorStatus` writes the memory-access error mask `1<<26` (L5092). REENCODE (`1<<31`) is written at L5462. The mask names are at `codechal_vdenc_avc.h:38-45`.
   - **Not in this repository**: the HuC firmware image itself and its authentication. Media only observes the HUC_STATUS2 IMEM-loaded bit. ANV stores HUC_STATUS2 into a temporary (`anv_h264_brc_emit_huc_tail`, genX_huc_brc.h L740-763) and HUC_STATUS error flags (L691-738), but never checks the IMEM-loaded bit or gates on it.

## g. Output SLB and PAK feedback on Gen9

**Input SLB (region 3)**, built by `CodechalHwInterface::AddVdencBrcImgBuffer` (`codechal_hw.cpp:703-745`):

| Offset | Size | Command | Source |
|---:|---:|---|---|
| 0 | 84 | `MFX_AVC_IMG_STATE` | `AddMfxAvcImgCmd` (codechal_hw.cpp L736) |
| 84 | 0 | `VDENC_COST_STATE` (not emitted on Gen9) | `GetVdencAvcCostStateSize()` returns 0 (`mhw_vdbox_vdenc_generic.h:308`) |
| 84 | 0 | `VDENC_CMD3` (not emitted on Gen9) | `GetVdencCmd3Size()` returns 0 (L303) |
| 84 | 140 | `VDENC_IMG_STATE` | `AddVdencImgStateCmd` |
| 224 | 4 | `MI_BATCH_BUFFER_END` | `AddBatchBufferEndInsertionFlag` |

Total **228 bytes** (`m_vdencBrcImgStateBufferSize`, `codechal_hw_g9_X.h:60`). `UPD_SLBB_Size_U16` is set to the same value (`codechal_vdenc_avc.h:1628`). `UPD_SLBB_B_Offset_U16` and `UPD_AvcImgStateOffset_U16` are not written by media (they stay zero). ANV uses the same 228-byte size (`ANV_H264_BRC_SLB_SIZE`, genX_huc_brc.h L280-288, asserted at L634).

**Output SLB (region 6)**: `m_batchBufferForVdencImgStat[0]`, written by the HuC (L5410-5411). The same 228-byte layout is expected by the host readers:

- KBL `PopulateEncParam` (debug-tool only, L781-805) reads the region-3 copy at `+84`, which is `VDENC_IMG_STATE`, and uses `DW1.VdencPerfmode` and `DW4.BlockBasedSkipEnabled`.
- Source comment (KBL L801): `// BRC case: VDENC IMG STATE is updated by HuC FW`.
- **Which fields the HuC patches is not stated in the repository.** The HuC firmware is a binary. The media source does not enumerate the patched fields. ANV's comment says the HuC writes `QPPRIMEY` into `VDENC_IMG_STATE`; that is ANV's claim and is not confirmed by the media source.

**PAK feedback into the update DMEM** (the mechanism the HuC uses for the previous frame's size; done by hardware on the media Gen9 path, and by `mi_memcpy` on ANV):

| DMEM byte | Field | Source (media) | ANV |
|---:|---|---|---|
| 20 | `FrameByteCount` | `MFC_BITSTREAM_BYTECOUNT_FRAME` via `MI_STORE_REGISTER_MEM` (`codechal_encoder_base.cpp:2940-2947`, `dwOffset = 5*4`) | `mi_memcpy` L845 |
| 28 | `ImgStatusCtrl` | `MFC_IMAGE_STATUS_CONTROL` (`codechal_encoder_base.cpp:2855-2863`, `dwOffset = 7*4`) | `mi_memcpy` L847 |
| 384 | `NumOfSlice` | `MFC_AVC_NUM_SLICES` at `m_vdencBrcNumOfSliceOffset` (`codechal_encoder_base.cpp:2947-2952`; KBL L371 sets it to `offsetof(NumOfSlice)` = 384) | `mi_memcpy` L849 |

The base `AddMiStoreForHWOutputToHucDmem()` is a no-op (`codechal_vdenc_avc.h:1026-1031`), so on Gen9 the writes are done by MI_STORE_REGISTER_MEM rather than by a copy command. Gen12 uses an extra `MI_COPY_MEM_MEM` path when `!m_swBrcMode && m_singleTaskPhaseSupported` (Gen12 L1336-1372).

## Open items / not found

- HuC firmware image and authentication logic: not in this repository.
- Exact fields patched by the HuC in the output SLB: not documented in the repository.
- KBL-specific HuC FW version or descriptor overrides: none found; kernel descriptors 4 and 5 are common.
- Value-level comparisons of the const-data tables and of the rate-control formulas were not performed (layout only, as requested).
