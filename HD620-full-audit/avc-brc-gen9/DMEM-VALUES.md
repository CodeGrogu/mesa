# Gen9 KBL H.264 VDEnc HuC BRC: DMEM values written by iHD vs ANV

Read-only research. Nothing under `mesa/` or `linux/` was modified, and no GPU workload was run.

## Source key

- **iHD** = `/home/polarbear/.t3/projects/catchingup/media-driver`. Short paths below are relative to `media_driver/agnostic/` unless stated otherwise.
  - `KBL` = `gen9_kbl/codec/hal/codechal_vdenc_avc_g9_kbl.cpp`
  - `CMN` = `common/codec/hal/codechal_vdenc_avc.h` (templates `SetDmemHuCBrcInitResetImpl` at L1263, `SetDmemHuCBrcUpdateImpl` at L1468)
  - `CMN.cpp` = `common/codec/hal/codechal_vdenc_avc.cpp`
  - `DDI` = `media_driver/linux/common/codec/ddi/media_ddi_encode_avc.cpp`
  - `ENC` = `common/codec/hal/codechal_encoder_base.cpp`
  - `BASE` = `common/codec/hal/codechal_encode_avc_base.cpp`
- **ANV** = `/home/polarbear/.t3/projects/catchingup/mesa/src/intel/vulkan/`
  - `genX_huc_brc.h` (the setters)
  - `genX_cmd_video_enc.c` (the pass loop and PAK-stat staging)
  - `genX_cmd_video.c` (RC state population)

Units: BRC sizes are in bits, except the `UPD_TR_TargetSize` input and the `ProfileLevelMaxFrame` value, which are in bytes. Rates are in bits per second.

## 0. Corrections to ABI.md

ABI.md contains two statements about ANV that are wrong. Both are checked against the code below.

1. ABI.md section c says ANV never sets init byte 134 (`INIT_SliceSizeCtrlWA` on KBL). **ANV does set it.** `genX_huc_brc.h:520-523` runs `dmem->INIT_LookaheadDepth_U8 = 1;` under `#if GFX_VER == 9`. That is the same byte iHD sets at `KBL:524`. The ANV struct calls it `INIT_LookaheadDepth_U8`, but the byte is the same.
2. ABI.md says ANV leaves update bytes 369 and 371 at zero. **ANV writes byte 369.** `genX_huc_brc.h:593` runs `dmem->RSVD3 = 1;` under `#if GFX_VER == 9`. On KBL, byte 369 is `UPD_SklCabacWAEnable_U8` (see the KBL struct, `KBL` L201 in the struct listing, offset 369). The ANV `static_assert` at `genX_huc_brc.h:280-281` labels byte 369 as "KBL UPD_SLCSZ_ConsertativeThreshold". That label is wrong. On KBL the conservative-threshold byte is **371**. ANV leaves 371 at zero. iHD writes 371 from `KBL:574-581`.

Two smaller ABI.md corrections:
- The iHD comment at `CMN.cpp`/`CMN:1531` reads "I:1, P:2, B:0". The arithmetic (`(type+1)%3`, with I_TYPE=1, P_TYPE=2, B_TYPE=3) and the KBL struct comment (`KBL` struct update, `UPD_CurrFrameType_U8`, "I frame 2; P frame 0; B frame 1") both give **I=2, P=0, B=1**. The code comment is stale.
- ABI.md cites ANV line numbers that are often off by about 10 lines (for example MinRate is at `genX_huc_brc.h:453`, not 443).

## 1. iHD KBL setters, quoted

### 1a. `SetDmemHuCBrcInitReset` (`KBL:504-557`)

```
518  MOS_ZeroMemory(pHucVDEncBrcInitDmem, sizeof(CODECHAL_VDENC_AVC_BRC_INIT_DMEM_G9_KBL));
520  SetDmemHuCBrcInitResetImpl<CODECHAL_VDENC_AVC_BRC_INIT_DMEM_G9_KBL>(pHucVDEncBrcInitDmem);
523  pHucVDEncBrcInitDmem->INIT_FracQPEnable_U8 = (uint8_t)m_vdencInterface->IsRhoDomainStatsEnabled();
524  pHucVDEncBrcInitDmem->INIT_SliceSizeCtrlWA = 1;
526  pHucVDEncBrcInitDmem->INIT_SinglePassOnly = m_vdencSinglePassEnable ? true : false;
528-534 if (TargetUsage & 0x07) == TARGETUSAGE_BEST_SPEED && FrameWidth >= 3840 && FrameHeight >= 2160
      && FramesPer100Sec >= 6000  ->  INIT_SinglePassOnly = true
537-547 if (m_mbBrcEnabled) INIT_DistQPDelta_I8 = BRC_INIT_DistQPDelta_I8_LowDelay (if EXTREMELY_LOW) else BRC_INIT_DistQPDelta_I8
549-552 CODECHAL_DEBUG_TOOL only (PopulateBrcInitParam)
```

Constants: `BRC_INIT_DistQPDelta_I8_LowDelay = {-5,-2,2,5}` (`KBL:355-358`), `BRC_INIT_DistQPDelta_I8 = {-5,-2,2,5}` (`CMN.cpp:2791-2793`). The two tables are equal. `SINGLE_PASS_MIN_FRAME_WIDTH=3840`, `_HEIGHT=2160`, `_PER100S=6000` (`KBL` header L111-113). `TARGETUSAGE_BEST_SPEED = 7` (`media_common/agnostic/common/codec/shared/codec_def_common.h:385`).

### 1b. Common init template `SetDmemHuCBrcInitResetImpl` (`CMN` L1263-1465)

```
1280  hucVDEncBrcInitDmem->BRCFunc_U8 = m_brcInit ? 0 : 2;
1282-1283 INIT_FrameWidth_U16 = m_frameWidth; INIT_FrameHeight_U16 = m_frameHeight;
1285  INIT_TargetBitrate_U32 = avcSeqParams->TargetBitRate;
1286  INIT_MinRate_U32 = avcSeqParams->MinBitRate;
1287  INIT_MaxRate_U32 = avcSeqParams->MaxBitRate;
1288  INIT_BufSize_U32 = avcSeqParams->VBVBufferSizeInBit;
1289  INIT_InitBufFull_U32 = avcSeqParams->InitVBVBufferFullnessInBit;
1291-1292 if (INIT_InitBufFull_U32 > VBVBufferSizeInBit) INIT_InitBufFull_U32 = VBVBufferSizeInBit;
1294-1318 switch (RateControlMethod): CBR |=0x10, VBR |=0x20, QVBR |=0x20, AVBR |=0x80, ICQ |=0x00, VCM |=0x40
1320-1324 if (FrameSizeTolerance == EXTREMELY_LOW) { INIT_BRCFlag_U16 = 0x80; INIT_LowDelayGoldenFrameBoost_U8 = 0; }
1326  INIT_FrameRateM_U32 = avcSeqParams->FramesPer100Sec;
1327  INIT_FrameRateD_U32 = 100;
1330-1333 CodecHalAvcEncode_GetProfileLevelMaxFrameSize(...); INIT_ProfileLevelMaxFrame_U32 = profileLevelMaxFrame;
1335-1345 GopRefDist >= GopPicSize && GopPicSize>1 -> GopP=1, GopB=GopPicSize-2
          else GopP=(GopPicSize-1)/GopRefDist, GopB=(GopRefDist-1)*GopP
1347-1357 if (m_minMaxQpControlEnabled) MinQP=max(m_iMinQp,10), MaxQP=max(m_iMaxQp,10)
          else INIT_MinQP_U16 = CODECHAL_VDENC_AVC_BRC_MIN_QP (10); INIT_MaxQP_U16 = CODECHAL_ENCODE_AVC_MAX_SLICE_QP (51)
1360-1363 bps_ratio = (MaxBitRate*100/FPS100) / (VBV*100/FPS100), clamped to [0.1, 3.5]
1365-1391 EXTREMELY_LOW -> BRC_LowDelay_DevThresh*_S8 tables (CMN.cpp:2779-2789); else
          DevThreshPB0/I0/VBR0 = (int8)(MULT * pow(BRC_DevThresh*_FP_{NEG,POS}[i], bps_ratio))
          with NEG_MULT_PB=-50, POS_MULT_PB=50, NEG_MULT_I=-50, POS_MULT_I=50, NEG_MULT_VBR=-50, POS_MULT_VBR=100 (CMN L73-82)
1393-1396 ComputeBRCInitQP(avcSeqParams, &initQP); INIT_InitQPIP_U8 = (uint8_t)initQP;
1399-1403 if (IsMBBRCControlEnabled()) { INIT_MbQpCtrl_U8 = 1; INIT_DistQPDelta_I8 = {-5,-2,2,5}; }
1405  INIT_SliceSizeCtrlEn_U8 = avcSeqParams->EnableSliceLevelRateCtrl;
1407-1408 INIT_OscillationQpDelta_U8 = (VCM || QVBR) ? 16 : 0;
1409-1410 INIT_HRDConformanceCheckDisable_U8 = (VCM || AVBR) ? 1 : 0;
1413-1436 >=4K (w*h MBs >= (3840*2160)>>8): Top/BotQPDelta = 5/5, TopFrmSz/BotFrmSz = 80/80
          AVBR: 2/2, 48/32   else: TopQPDelta=2 (CMN L54), BotQPDelta=1 (L55), TopFrmSz=32 (L56), BotFrmSz=24 (L57)
1438  INIT_QPSelectForFirstPass_U8 = 1;
1439  INIT_MBHeaderCompensation_U8 = 1;
1440  INIT_DeltaQP_Adaptation_U8 = 1;
1441  INIT_MaxCRFQualityFactor_U8 = CODECHAL_ENCODE_AVC_MAX_ICQ_QUALITYFACTOR + 1;   // 52
1443-1447 if (QVBR || ICQ) { INIT_CRFQualityFactor_U8 = ICQQualityFactor; INIT_ScenarioInfo_U8 = (QVBR) ? 1 : 0; }
1449-1452 if (NumDirtyROI) INIT_ScenarioInfo_U8 = 1;
1454-1459 if (FrameSizeTolerance == LOW) { SlidingWidowRCEnable=1; SlidingWindowSize=FramesPer100Sec/100; SlidingWindowMaxRateRatio=120; }
1461-1462 memcpy INIT_EstRateThreshP0_U8 = BRC_EstRateThreshP0_U8 {4,8,12,16,20,24,28};
          memcpy INIT_EstRateThreshI0_U8 = BRC_EstRateThreshI0_U8 {4,8,12,16,20,24,28};
          (INIT_EstRateThreshB0_U8 is NOT written anywhere in the iHD KBL/common path)
```

The `ComputeBRCInitQP` body (`CMN.cpp:2860-2887`):

```
2873  frameSize = ((m_frameWidth * m_frameHeight * 3) >> 1);
2874  QP = (int32_t)(1. / 1.2 * pow(10.0, (log10(frameSize * 2. / 3. * ((float)FramesPer100Sec) / ((float)(TargetBitRate) * 100)) - x0) * (y1 - y0) / (x1 - x0) + y0) + 0.5);
2875  QP += 2;
2877  deltaQ = (int32_t)(9 - (VBVBufferSizeInBit * ((float)FramesPer100Sec) / ((float)(TargetBitRate) * 100)));
2878  QP += deltaQ < 0 ? 0 : deltaQ;
2879  QP = CodecHal_Clip3(CODECHAL_ENCODE_AVC_BRC_MIN_QP /*1*/, CODECHAL_ENCODE_AVC_MAX_SLICE_QP /*51*/, QP);
2880  QP--;   if (QP < 0) QP = 1;
```
(`x0=0, y0=1.19, x1=1.75, y1=1.75`, `CMN.cpp:2864`.)

### 1c. Common update template `SetDmemHuCBrcUpdateImpl` (`CMN` L1468-1635)

```
1477  BRCFunc_U8 = 1;
1479-1485 if (!m_brcInit && m_currPass == 0) {
            m_brcInitPreviousTargetBufFullInBits = (uint32)(m_dBrcInitCurrentTargetBufFullInBits + m_dBrcInitResetInputBitsPerFrame * m_numSkipFrames);
            m_dBrcInitCurrentTargetBufFullInBits += m_dBrcInitResetInputBitsPerFrame * (1 + m_numSkipFrames);
            m_dBrcTargetSize += m_dBrcInitResetInputBitsPerFrame * (1 + m_numSkipFrames); }
1487-1490 if (m_dBrcTargetSize > VBVBufferSizeInBit) m_dBrcTargetSize -= VBVBufferSizeInBit;
1492  UPD_FRAMENUM_U32 = m_avcSliceParams->frame_num;
1493  UPD_TARGETSIZE_U32 = (uint32)(m_dBrcTargetSize);
1494  UPD_PeakTxBitsPerFrame_U32 = (uint32)(m_dBrcInitCurrentTargetBufFullInBits - m_brcInitPreviousTargetBufFullInBits);
1497-1520 if (EnableSliceLevelRateCtrl) { UPD_SLCSZ_TARGETSLCSZ_U16 = SliceSizeInBytes; UPD_TargetSliceSize_U16 = SliceSizeInBytes;
              UPD_MaxNumSliceAllowed_U16 = m_maxNumSlicesAllowed;
              UPD_SLCSZ_UPD_THRDELTAI/P_U16[k] = MIN(SliceSizeInBytes-150, m_vdencSSCThrsTblI/P[k+10]) for k<42 }
          else all zero
1522-1529 gRateRatioThreshold = (FrameSizeTolerance==LOW) ? BRC_UPD_slwin_global_rate_ratio_threshold {80,90,95,101,105,110,120}
                                                       : BRC_UPD_global_rate_ratio_threshold {80,90,95,101,105,115,130}
1531  UPD_CurrFrameType_U8 = (m_pictureCodingType + 1) % 3;
1533-1538 UPD_startGAdjFrame_U16 = {10,50,100,150}; UPD_startGAdjMult_U8 = {1,1,3,2,1}; UPD_startGAdjDiv_U8 = {40,5,5,3,1};
1539  UPD_gRateRatioThresholdQP_U8 = {-3,-2,-1,0,1,1,2,3}
1541-1542 UPD_PAKPassNum_U8 = m_currPass; UPD_MaxNumPass_U8 = m_numPasses + 1;
1544-1553 numP = (GopPicSize-1)/GopRefDist (if GopRefDist && GopPicSize>0); UPD_SceneChgWidth_U8[0..1] = MIN((numP+1)/5, 6)
1555-1557 UPD_SceneChgDetectEn_U8 = 1; UPD_SceneChgPrevIntraPctThreshold_U8 = 0x60; UPD_SceneChgCurIntraPctThreshold_U8 = 0xc0;
1559-1560 UPD_IPAverageCoeff_U8 = (EXTREMELY_LOW) ? 0 : 0x80;
1562  UPD_CQP_FracQp_U8 = 0;
1564-1571 UPD_CQP_QpValue_U8 = (ICQ) ? 18 : 0;
1573-1580 UPD_HMEDetectionEnable_U8 = m_staticFrameDetectionInUse ? 1 : 0;
1583-1599 skip-frame fields (0 by default)
1602  UPD_HMECostEnable_U8 = 1;
1606-1626 UPD_RoiQpViaForceQp_U8 = 0; UPD_StaticRegionPct_U16 = 0; UPD_ROISource_U8 = 0;
          if NumROI: UPD_RoiQpViaForceQp_U8 = bNativeROI ? 0 : 1; UPD_ROIQpDelta_I8[i+1] = ROIDistinctDeltaQp[i];
          else if NumDirtyROI: UPD_StaticRegionPct_U16 = m_vdencStaticRegionPct; if (m_mbBrcEnabled) UPD_ROISource_U8 = 2;
1628  UPD_SLBB_Size_U16 = m_hwInterface->m_vdencBrcImgStateBufferSize;   // 228 on KBL
```

### 1d. KBL update setter `SetDmemHuCBrcUpdate` (`KBL:559-600`)

```
566-570 lockFlags.WriteOnly = 1; pfnLockResource(&m_resVdencBrcUpdateDmemBuffer[m_currRecycledBufIdx][m_currPass])
          // NOTE: the update DMEM buffer itself is NOT zeroed here (only lockFlags is zeroed at 567)
572   SetDmemHuCBrcUpdateImpl<_CODECHAL_VDENC_AVC_BRC_UPDATE_DMEM_G9_KBL>(pHucVDEncBrcDmem);
574-581 if (avcSeqParams->EnableSliceLevelRateCtrl)
            UPD_SLCSZ_ConsertativeThreshold_U8 = (uint8_t)(RateControlMethod != RATECONTROL_VCM);   // byte 371
        else UPD_SLCSZ_ConsertativeThreshold_U8 = 0;
583-586 if (m_16xMeSupported && m_pictureCodingType == P_TYPE) HmeDistAvailable_U8 = 1;             // byte 390
587-588 UPD_WidthInMB_U16 = m_picWidthInMb; UPD_HeightInMB_U16 = m_picHeightInMb;                    // bytes 404, 406
590   UPD_TR_TargetSize_U32 = m_avcPicParam->TargetFrameSize * 8;   // byte 416, "convert bytes to bits"
592-595 CODECHAL_DEBUG_TOOL only
```

The KBL struct byte offsets are confirmed by counting the declarations at `KBL` L160-226 (`UPD_SklCabacWAEnable_U8` = 369, `UPD_ROISource_U8` = 370, `UPD_SLCSZ_ConsertativeThreshold_U8` = 371, `UPD_TR_TargetSize_U32` = 416). Init struct: `INIT_SliceSizeCtrlWA` = 134, `INIT_SinglePassOnly` = 135.

## 2. ANV setters, with line references

### 2a. `anv_brc_estimate_init_qp` (`genX_huc_brc.h:260-267`)

```
double x = log10(frame_size * 2.0 / 3.0 * frame_rate / bitrate);
return (int32_t)(1.0 / 1.2 * pow(10.0, (x - x0) * (y1 - y0) / (x1 - x0) + y0) + 0.5) + 2;
```
Called at `genX_huc_brc.h:496-498` with `frame_size = width*height*3/2`, `frame_rate = frames_per_100s`, `bitrate = target_bps*100.0`. Then `delta_q = (int32_t)(9 - buf_size*frames_per_100s/(target_bps*100.0))`, `qp += max(delta_q,0)`, `clamp(qp,1,51)`, `qp--`, `if (qp<0) qp=1` (L499-505). The formula matches iHD's `ComputeBRCInitQP` term for term.

### 2b. `anv_h264_brc_fill_init_dmem` (`genX_huc_brc.h:423-524`), zeroed by memset at L814

```
444  BRCFunc_U8 = rc->frame_counter ? 2 : 0;
445-446 INIT_BRCFlag_U16 = is_cbr ? 0x0010 : 0x0020;                 (no ICQ, VCM or LOWDELAY case)
447-448 INIT_FrameWidth_U16 = width; INIT_FrameHeight_U16 = height;  (width = MBs*16)
449  INIT_TargetBitrate_U32 = target_bps = DIV_ROUND_UP(average_bitrate,1000)*1000;
450  INIT_MaxRate_U32 = is_cbr ? target_bps : DIV_ROUND_UP(max_bitrate,1000)*1000;
453  INIT_MinRate_U32 = target_bps;
454  INIT_BufSize_U32 = vbv_size_bits;
455  INIT_InitBufFull_U32 = MIN2(vbv_initial_fullness_bits, buf_size);
458-460 INIT_ProfileLevelMaxFrame_U32 = MBs_w * MBs_h * 96;     (no level, no fps, no user max)
461-462 INIT_FrameRateM_U32 = frames_per_100s (default 30 fps); INIT_FrameRateD_U32 = 100;
464-467 gop_p = gop>1 ? (gop-1)/gop_ref_dist : 0; gop_b = gop_ref_dist>1 ? (gop_ref_dist-1)*gop_p : 0;
468-469 INIT_MinQP_U16 = 10; INIT_MaxQP_U16 = 51;
471-486 bps_ratio = max_bps/buf_size clamped [0.1,3.5]; DevThresh PB0/I0/VBR0 with same tables and multipliers as iHD
488-494 INIT_EstRateThreshP0/B0/I0_U8 = {4,8,12,16,20,24,28}     (B0 IS written by ANV; NOT by iHD)
511  INIT_FracQPEnable_U8 = 1;
512  INIT_QPSelectForFirstPass_U8 = 1;
513  INIT_MBHeaderCompensation_U8 = 1;
514  INIT_DeltaQP_Adaptation_U8 = 1;
515  INIT_MaxCRFQualityFactor_U8 = 52;
516-519 INIT_TopQPDeltaThrForAdapt2Pass_U8 = 2; INIT_BotQPDeltaThrForAdapt2Pass_U8 = 1;
      INIT_TopFrmSzThrForAdapt2Pass_U8 = 32;  INIT_BotFrmSzThrForAdapt2Pass_U8 = 24;
520-523 #if GFX_VER == 9: INIT_LookaheadDepth_U8 (byte 134, = KBL INIT_SliceSizeCtrlWA) = 1;
```

Not written by ANV's init: `INIT_OscillationQpDelta_U8` (0), `INIT_HRDConformanceCheckDisable_U8` (0), `INIT_MbQpCtrl_U8` (0), `INIT_SliceSizeCtrlEn_U8` (0), `INIT_DistQPDelta_I8` (0), `INIT_SinglePassOnly` (0), `INIT_CRFQualityFactor_U8` (0), `INIT_ScenarioInfo_U8` (0), sliding-window fields (0), `INIT_InitQPB` (0). `INIT_InitQPIP` is written at L506.

### 2c. `anv_h264_brc_fill_update_dmem` (`genX_huc_brc.h:527-595`), zeroed by memset at L866

```
548  BRCFunc_U8 = 1;
549-553 target_acc = MIN2(vbv_initial_fullness_bits, vbv_size_bits) + (frame_counter - brc_init_frame) * input_bits_per_frame;
      UPD_TARGETSIZE_U32 = vbv_size_bits ? target_acc % vbv_size_bits : target_acc;     (input_bits uses target_bps, not MaxBitRate)
555  UPD_FRAMENUM_U32 = rc->frame_counter;
556  UPD_PeakTxBitsPerFrame_U32 = input_bits_per_frame;     (= target_bps*100/frames_per_100s)
558-561 UPD_CurrFrameType_U8 = I/IDR ? 2 : P ? 0 : 1;
563-564 UPD_WidthInMB_U16, UPD_HeightInMB_U16 from SPS
565  UPD_SLBB_Size_U16 = ANV_H264_BRC_SLB_SIZE (228, L290-292)
566  UPD_AvcImgStateOffset_U16 = 0
567  UPD_PAKPassNum_U8 = pass;   568  UPD_MaxNumPass_U8 = 2;
570-579 start_gadj_frame / mult / div / rate_ratio_threshold / rate_ratio_threshold_qp: identical values to iHD
581-582 UPD_SceneChgWidth_U8[0..1] = MIN2((num_p+1)/5, 6), num_p = (gop-1)/gop_ref_dist, gop default 30
583  UPD_SceneChgDetectEn_U8 = 1;
584  UPD_SceneChgPrevIntraPctThreshold_U8 = 96;
585  UPD_SceneChgCurIntraPctThreshold_U8 = 192;
586  UPD_IPAverageCoeff_U8 = 128;
587  UPD_HMECostEnable_U8 = 1;
588-593 #if GFX_VER == 9: RSVD3 (byte 369) = 1;      // byte 369 = UPD_SklCabacWAEnable_U8 on KBL
```

Not written by ANV's update: `UPD_CQP_*` (0), `UPD_HMEDetectionEnable_U8` (0), `HmeDistAvailable_U8` (0), `UPD_SLCSZ_*` and `UPD_TargetSliceSize_U16` and `UPD_MaxNumSliceAllowed_U16` (0), `UPD_TR_TargetSize_U32` at byte 416 (0), `UPD_SLCSZ_ConsertativeThreshold_U8` at byte 371 (0), `UPD_ROISource_U8` (0), `UPD_RoiQpViaForceQp_U8` (0), `UPD_ROIQpDelta_I8` (0).

### 2d. Pass loop and PAK-stat staging (ANV)

- `genX_cmd_video_enc.c:1978-2093` (`anv_h264_encode_video`): on GFX<12, the update for pass 0 is emitted before the frame's PAK (`anv_h264_brc_init_update`, L1905-1925, which calls `anv_h264_brc_emit_huc_update(cmd, frame_info, 0, ...)` at L1919). Then the PAK is emitted **once** (no loop over passes in L1990-2091). `anv_h264_brc_store_pak_stats` (L1927-1973) stores the three PAK MMIO registers (L1949-1962), flushes, and emits the **pass-1** update at L1972. Then `vid->rc.frame_counter++` (L2090).
- `genX_huc_brc.h:878-884`: the update DMEM is patched from `ANV_VID_MEM_BRC_PAK_INFO`: `mi_memcpy(dmem+20, pak_info+0)` (FrameByteCount), `mi_memcpy(dmem+28, pak_info+4)` (ImgStatusCtrl), `mi_memcpy(dmem+384, pak_info+8)` (NumOfSlice).
- `genX_huc_brc.h:726-737`: ANV writes the REENCODE mask (`1<<31`, L728) and stores HUC_STATUS, the same pattern as iHD. Nothing in ANV reads that mask to trigger a second PAK.

## 3. Field-by-field table

Legend: **SAME** = same value and formula. **DIFF** = different. **COND** = same only under the stated condition.

Byte offsets are from the KBL structs in `KBL` L95-226.

### 3a. BRC init DMEM (192 bytes)

| Off | Field | iHD KBL (value / formula, location) | ANV (value / formula, location) | Status |
|---:|---|---|---|---|
| 0 | BRCFunc_U8 | `m_brcInit ? 0 : 2` (CMN:1280) | `frame_counter ? 2 : 0` (genX_huc_brc.h:444) | SAME in normal flow |
| 4 | INIT_BRCFlag_U16 | CBR 0x10, VBR 0x20, QVBR 0x20, AVBR 0x80, ICQ 0, VCM 0x40; LOWDELAY 0x80 overrides (CMN:1294-1324) | CBR 0x10, VBR 0x20 (L445-446) | COND: SAME for CBR/VBR; ANV has no ICQ/VCM/LOWDELAY |
| 8/10 | INIT_FrameWidth/Height | m_frameWidth/Height (CMN:1282-1283) | MBs*16 (L430-431, 447-448) | SAME (assuming no cropping in iHD) |
| 12 | INIT_TargetBitrate_U32 | bps. CBR: `bps` (DDI:236). VBR: `bps*target_percentage/100` (DDI:273). (CMN:1285) | `DIV_ROUND_UP(average_bitrate,1000)*1000` (L437, 449) | **DIFF**: ANV rounds up to a 1000 bps multiple; VBR iHD is scaled by target_percentage |
| 16 | INIT_MinRate_U32 | MinBitRate (CBR: bps DDI:248; VBR: scaled, DDI:274) (CMN:1286) | = target_bps (L453) | SAME for CBR; VBR SAME if target_percentage=100 |
| 20 | INIT_MaxRate_U32 | MaxBitRate: CBR bps (DDI:247), VBR bps (DDI:272) (CMN:1287) | CBR = target_bps; VBR = max_bitrate rounded (L438-439, 450) | SAME when VA/Vulkan max=bps |
| 24 | INIT_BufSize_U32 | VBVBufferSizeInBit = buffer_size (DDI:184), default 2*bps (DDI:1317) (CMN:1288) | vbv_size_bits = ms*avg/1000, default 2*avg (genX_cmd_video.c:121-123), (L454) | SAME units (bits); defaults equal (2 s) |
| 28 | INIT_InitBufFull_U32 | app initial_buffer_fullness (DDI:185), default bps (DDI:1316); clamped to VBV (CMN:1289-1292). **Zero disables BRC** (CMN.cpp:4006-4024) | MIN(vbv_initial_fullness_bits, vbv) (L455); default avg (genX_cmd_video.c:124-126) | **DIFF in edge case**: iHD gives up BRC if app passes 0. Defaults equal (1 s) |
| 32 | INIT_ProfileLevelMaxFrame_U32 | `GetProfileLevelMaxFrameSize` = min(numMB*96, MaxMBPS(level)*100/FPS100*96, userMax if set) and min with h*h (BASE:496-572, GetMaxMBPS BASE:435-490) (CMN:1329-1333) | numMB*96 (L458-460) | **DIFF** at high fps or small level MaxMBPS, or with UserMaxFrameSize. SAME at 1080p 30 fps level 4 (both 783360 B) |
| 36 | INIT_FrameRateM_U32 | FramesPer100Sec (DDI:200-208; default 3000) (CMN:1326) | frames_per_100s (default 30 fps) (L432-434, 461) | SAME |
| 40 | INIT_FrameRateD_U32 | 100 (CMN:1327) | 100 (L462) | SAME |
| 44 | INIT_GopP_U16 | CMN:1335-1343 | L464 | SAME for IPPP |
| 46 | INIT_GopB_U16 | CMN:1338/1344 | L465 | SAME for IPPP |
| 48 | INIT_MinQP_U16 | 10 or app min (min 10) (CMN:1347-1357) | 10 (L468) | SAME by default |
| 50 | INIT_MaxQP_U16 | 51 or app max (CMN:1356) | 51 (L469) | SAME by default |
| 52 | INIT_DevThreshPB0_S8[8] | pow tables, ×(-50/+50) (CMN:1374-1379, tables CMN.cpp:2760-2767) | same tables, same multipliers (L471-481) | SAME (low-delay table only in iHD, COND on EXTREMELY_LOW) |
| 60 | INIT_DevThreshVBR0_S8[8] | ×(-50/+100) (CMN:1386-1389) | same (L484-485) | SAME |
| 68 | INIT_DevThreshI0_S8[8] | ×(-50/+50) (CMN:1381-1384) | same (L482-483) | SAME |
| 76 | INIT_InitQPIP | `ComputeBRCInitQP` (CMN:1394-1396; formula CMN.cpp:2860-2887) | `anv_brc_estimate_init_qp` + delta + clamp + `--` (L496-506) | **SAME formula**. Equal inputs give equal QP. Input difference only for VBR target_percentage or kbps rounding |
| 79 | INIT_MbQpCtrl_U8 | 1 if IsMBBRCControlEnabled (CMN:1399-1401; CMN.cpp:8046) | 0 | COND: DIFF when MBBRC on (VA_RC_MB or internal MBBRC for ICQ/QVBR) |
| 80 | INIT_SliceSizeCtrlEn_U8 | seqParams->EnableSliceLevelRateCtrl (CMN:1405) | 0 | **DIFF** when app sends VAEncMiscParameterMaxSliceSize (DDI:716-721 sets true) |
| 85-88 | INIT_DistQPDelta_I8[4] | {-5,-2,2,5} if MBBRC (CMN:1402; KBL:537-547); else 0 | 0 | COND: DIFF when MBBRC |
| 89 | INIT_OscillationQpDelta_U8 | 16 if VCM or QVBR else 0 (CMN:1407-1408) | 0 | COND: SAME for CBR/VBR |
| 90 | INIT_HRDConformanceCheckDisable_U8 | 1 if VCM or AVBR (CMN:1409-1410) | 0 | COND: SAME for CBR/VBR |
| 92 | INIT_TopQPDeltaThrForAdapt2Pass_U8 | 2 (non-4K) (CMN:1431), 5 (≥4K) | 2 (L516) | SAME at ≤1080p |
| 93 | INIT_TopFrmSzThrForAdapt2Pass_U8 | 32 (CMN:1433) | 32 (L518) | SAME at ≤1080p |
| 94 | INIT_BotFrmSzThrForAdapt2Pass_U8 | 24 (CMN:1434) | 24 (L519) | SAME at ≤1080p |
| 95 | INIT_QPSelectForFirstPass_U8 | 1 (CMN:1438) | 1 (L512) | SAME |
| 96 | INIT_MBHeaderCompensation_U8 | 1 (CMN:1439) | 1 (L513) | SAME |
| 99 | INIT_EstRateThreshP0_U8[7] | {4..28} (CMN:1461) | {4..28} (L489-490) | SAME |
| 106 | INIT_EstRateThreshB0_U8[7] | **not written** (0) | {4..28} (L491-492) | **DIFF** (only affects B frames) |
| 113 | INIT_EstRateThreshI0_U8[7] | {4..28} (CMN:1462) | {4..28} (L493-494) | SAME |
| 120 | INIT_FracQPEnable_U8 | `IsRhoDomainStatsEnabled()` (KBL:523). Default false in `mhw_vdbox_vdenc_interface.h:195`. Only gen12 sets it true (`mhw_vdbox_vdenc_g12_X.h:372`). **Result on KBL: 0** | **1** (L511) | **DIFF** |
| 121 | INIT_ScenarioInfo_U8 | 1 if QVBR or NumDirtyROI, else 0 (CMN:1443-1452) | 0 | SAME for CBR/VBR with no ROI |
| 123 | INIT_DeltaQP_Adaptation_U8 | 1 (CMN:1440) | 1 (L514) | SAME |
| 124 | INIT_MaxCRFQualityFactor_U8 | 52 (CMN:1441) | 52 (L515) | SAME |
| 125 | INIT_CRFQualityFactor_U8 | ICQQualityFactor if QVBR/ICQ (CMN:1445) | 0 | SAME for CBR/VBR |
| 126 | INIT_BotQPDeltaThrForAdapt2Pass_U8 | 1 (CMN:1432) | 1 (L517) | SAME |
| 127-129 | Sliding window size / enable / ratio | only if FrameSizeTolerance==LOW: size=FPS100/100, enable=1, ratio=120 (CMN:1454-1459) | 0 | COND |
| 134 | INIT_SliceSizeCtrlWA (ANV name INIT_LookaheadDepth_U8) | 1 unconditional (KBL:524) | 1 under GFX_VER==9 (L520-523) | **SAME** (ABI.md was wrong) |
| 135 | INIT_SinglePassOnly | `m_vdencSinglePassEnable` (default false, CMN.cpp:3268-3273; user feature default "0", mos_utilities.cpp:675-683) OR (TU==7 && W≥3840 && H≥2160 && FPS100≥6000) (KBL:526-534). BRCPrecision==1 also sets m_vdencSinglePassEnable (CMN.cpp:4185-4187) | 0 | COND: DIFF only for TU=7 at ≥4K or BRCPrecision=1 |
| 36-175 | All other init bytes | Not written (zeroed by KBL:518) | Not written (memset L814) | SAME |

### 3b. BRC update DMEM (448 bytes)

| Off | Field | iHD KBL (value / formula, location) | ANV (value / formula, location) | Status |
|---:|---|---|---|---|
| 0 | BRCFunc_U8 | 1 (CMN:1477) | 1 (L548) | SAME |
| 4 | UPD_TARGETSIZE_U32 | `m_dBrcTargetSize`: starts at InitVBV (CMN:1278); +MaxBitRate*100/FPS100 per new frame (pass 0, !m_brcInit) (CMN:1275-1276, 1483-1484); −VBV once if >VBV (CMN:1487-1490) | `(min(init,vbv) + (frame_counter-brc_init_frame)*input) % vbv` (L549-553); input uses target_bps | **DIFF (mechanics)**: input rate MaxBitRate vs target (same for CBR); wrap is modulo in ANV vs single subtract in iHD. Frame 0 and 1 values equal |
| 8 | UPD_FRAMENUM_U32 | `m_avcSliceParams->frame_num` (H.264 frame_num: 0 for IDR, increments on ref pics, wraps) (CMN:1492) | `rc->frame_counter` (counts every frame) (L555) | **DIFF** (semantics). Equal for early IPPP |
| 12 | UPD_PeakTxBitsPerFrame_U32 | cur − prev = per-frame input (CMN:1494); equals MaxBitRate*100/FPS100 | `input_bits_per_frame` = target_bps*100/FPS (L556) | SAME for CBR |
| 16 | UPD_FrameBudget_U32 | not written (0) | not written (0) | SAME |
| 20 | FrameByteCount | PAK MMIO MFC_BITSTREAM_BYTECOUNT_FRAME stored at dword 5 of the next update DMEM (ENC:2935-2945) | copied from PAK_INFO+0 (L879-880) | SAME source, different timing (see §4) |
| 24 | TimingBudgetOverflow | not written | not written | SAME |
| 28 | ImgStatusCtrl | PAK MMIO MFC_IMAGE_STATUS_CONTROL stored at byte 7*4=28 (ENC:2852-2862) | copied from PAK_INFO+4 (L881-882) | SAME |
| 32 | IPCMNonConformant | not written | not written | SAME |
| 36 | UPD_startGAdjFrame_U16[4] | {10,50,100,150} (CMN:1533; table CMN.cpp:2803-2805) | same (L575, 570) | SAME |
| 44-147 | UPD_MBBudget_U16[52] | not written (uninitialised unless the buffer is zeroed) | not written (zeroed by memset L866) | **DIFF in zeroing**: see §5 |
| 148 | UPD_SLCSZ_TARGETSLCSZ_U16 | SliceSizeInBytes if slice RC (CMN:1499); else 0 | 0 | COND |
| 150 / 234 | UPD_SLCSZ_UPD_THRDELTAI/P_U16[42] | MIN(SliceSizeInBytes−150, SSC table[k+10]) if slice RC (CMN:1505-1506); else 0 | 0 | COND |
| 318-323 | NumOfFramesSkipped, SkipFrameSize, StaticRegionPct | 0 unless skip frames / dirty ROI (CMN:1583-1607) | 0 | SAME by default |
| 324 | UPD_gRateRatioThreshold_U8[7] | {80,90,95,101,105,115,130}; slwin {80,90,95,101,105,110,120} if LOW (CMN:1522-1529) | {80,90,95,101,105,115,130} (L573, 578) | SAME unless LOW tolerance (ANV has no slwin table) |
| 331 | UPD_CurrFrameType_U8 | (type+1)%3 → I=2, P=0, B=1 (CMN:1531) | I/IDR=2, P=0, B=1 (L558-561) | SAME |
| 332 | UPD_startGAdjMult_U8[5] | {1,1,3,2,1} (CMN:1535) | same (L571) | SAME |
| 337 | UPD_startGAdjDiv_U8[5] | {40,5,5,3,1} (CMN:1537) | same (L572) | SAME |
| 342 | UPD_gRateRatioThresholdQP_U8[8] | {-3,-2,-1,0,1,1,2,3} (CMN:1539) | same (L574, 579) | SAME |
| 350 | UPD_PAKPassNum_U8 | m_currPass (CMN:1541) | pass 0 or 1 (L567) | SAME |
| 351 | UPD_MaxNumPass_U8 | m_numPasses+1 = 2 (CMN:1542; m_numPasses set at CMN.cpp... see §4) | 2 always (L568) | SAME by default; DIFF only if BRCPrecision==1 (iHD then gives 1) |
| 352 | UPD_SceneChgWidth_U8[2] | MIN((numP+1)/5, 6), numP=(GopPicSize−1)/GopRefDist (CMN:1544-1553) | MIN((num_p+1)/5, 6), gop default 30 (L545-546, 581-582) | SAME when GOP is set |
| 354 | UPD_SceneChgDetectEn_U8 | 1 (CMN:1555) | 1 (L583) | SAME |
| 355 | UPD_SceneChgPrevIntraPctThreshold_U8 | 0x60 = 96 (CMN:1556) | 96 (L584) | SAME |
| 356 | UPD_SceneChgCurIntraPctThreshold_U8 | 0xc0 = 192 (CMN:1557) | 192 (L585) | SAME |
| 357 | UPD_IPAverageCoeff_U8 | 0x80 = 128, or 0 if EXTREMELY_LOW (CMN:1559-1560) | 128 (L586) | SAME unless low-delay |
| 364 | UPD_CQP_QpValue_U8 | 18 if ICQ else 0 (CMN:1564-1571) | 0 | SAME for CBR/VBR |
| 365 | UPD_CQP_FracQp_U8 | 0 (CMN:1562) | 0 | SAME |
| 366 | UPD_HMEDetectionEnable_U8 | 1 iff m_staticFrameDetectionInUse (CMN:1573-1580) | 0 | Default not traced; likely SAME |
| 367 | UPD_HMECostEnable_U8 | 1 (CMN:1602) | 1 (L587) | SAME |
| 369 | UPD_SklCabacWAEnable_U8 (KBL name) | **not written → 0** | **RSVD3 = 1** (L593) | **DIFF** (ANV enables the field on KBL) |
| 370 | UPD_ROISource_U8 | 0; 2 if NumDirtyROI and MBBRC (CMN:1608, 1624) | 0 | COND |
| 371 | UPD_SLCSZ_ConsertativeThreshold_U8 (KBL name) | `RateControlMethod != VCM` if EnableSliceLevelRateCtrl, else 0 (KBL:574-581) | **not written → 0** | COND: **DIFF** when slice RC is on |
| 372 | UPD_TargetSliceSize_U16 | SliceSizeInBytes if slice RC (CMN:1500), else 0 | 0 | COND |
| 374 | UPD_MaxNumSliceAllowed_U16 | m_maxNumSlicesAllowed if slice RC (CMN:1501), else 0 | 0 | COND |
| 376 | UPD_SLBB_Size_U16 | m_vdencBrcImgStateBufferSize = 140+84+4 = 228 (CMN:1628; ENC codechal_hw_g9_X.h:60) | 228 (ANV_H264_BRC_SLB_SIZE, L565) | SAME |
| 380 | UPD_AvcImgStateOffset_U16 | not written | 0 (L566) | SAME |
| 384 | NumOfSlice | PAK MMIO MFC_AVC_NUM_SLICES stored at m_vdencBrcNumOfSliceOffset=384 (ENC:2947-2953) | copied from PAK_INFO+8 (L883-884) | SAME |
| 388 | AveHmeDist_U16 | not written | 0 | SAME |
| 390 | HmeDistAvailable_U8 | 1 if m_16xMeSupported && P_TYPE (KBL:583-586). m_16xMeSupported is from user feature AVC_ENCODE_16xME (CMN.cpp:3200-3209), cleared by HMEDisable (CMN.cpp:5657-5665) | 0 | COND: DIFF when 16x ME on for P frames |
| 395 | UPD_RoiQpViaForceQp_U8 | 0; 1 if NumROI && !bNativeROI (CMN:1606, 1613) | 0 | COND |
| 404 / 406 | UPD_WidthInMB / HeightInMB | m_picWidthInMb / m_picHeightInMb (KBL:587-588) | from SPS (L563-564) | SAME |
| 408 | UPD_ROIQpDelta_I8[8] | ROIDistinctDeltaQp (CMN:1614-1617) if NumROI | 0 | COND |
| 416 | UPD_TR_TargetSize_U32 (KBL name) | **TargetFrameSize * 8** (KBL:590). TargetFrameSize = VA target_frame_size, bytes (DDI:312). Non-zero enables TCBRC | **not written → 0** (TCBRC off; comment L589-592) | **DIFF** when the app sets target_frame_size |
| 420-447 | RSVD2 | not written | memset 0 | SAME |

Other iHD update bytes written by the common template and not listed above: `UPD_SLCSZ_ConsertativeThreshold` (see 371), `UPD_SLBB_Size` (see 376). Bytes 44-147 (MBBudget) and 16-19, 24-35 are not written by iHD.

## 4. PAK feedback and pass structure

### 4a. iHD per-frame order (`CMN.cpp:6101-6125`, inside the per-pass loop)

`ENC:5078-5083`: `for (m_currPass = 0; m_currPass <= m_numPasses; m_currPass++)`. For each pass the code runs `SetBufferToStorePakStatistics()` (CMN.cpp:6111), then `HuCBrcInitReset()` if `m_brcInit || m_brcReset` (CMN.cpp:6114-6117), then `HuCBrcUpdate()` (CMN.cpp:6123), then `m_brcInit = m_brcReset = false` (CMN.cpp:6124), then the PAK for that pass (CMN.cpp:6127 onward).

Pass count (`CMN.cpp` is `codechal_vdenc_avc.cpp`):
- `codechal_vdenc_avc.cpp:4180-4196` (in `SetPictureStructs`, L4132):
  ```
  4181 if (m_vdencBrcEnabled) {
  4183     m_numPasses = CODECHAL_VDENC_BRC_NUM_OF_PASSES - 1;   // 2-pass VDEnc BRC  (=1; CODECHAL_VDENC_BRC_NUM_OF_PASSES=2, codechal_encoder_base.h:147)
  4185     if (picParams->BRCPrecision == 1) { m_vdencSinglePassEnable = true; m_numPasses = 0; }
  4191 } else m_numPasses = CODECHAL_VDENC_AVC_CQP_NUM_OF_PASSES - 1;   // 0 (CQP)
  ```
- `BRCPrecision` is never assigned from the VA-API DDI (a search of `media_ddi_encode_avc.cpp` and the HAL finds no assignment). It stays 0. So on KBL with VA-API, **m_numPasses = 1 and the frame runs two passes: update(0), PAK(0), update(1), PAK(1)**. The second PAK is conditional, see 4c.
- `m_vdencBrcEnabled` on KBL: `codechal_vdenc_avc.cpp:3966` (`MEDIA_IS_SKU(FtrEnableMediaKernels)`), disabled at `CMN.cpp:4006-4024` if InitVBVBufferFullnessInBit, VBV, MaxBitRate, TargetBitRate or FramesPer100Sec is zero (AVBR exempt, ICQ exempt).
- The user feature `VDENC_SINGLE_PASS_ENABLE` (default "0", `mos_utilities.cpp:675-683`) is read at `CMN.cpp:3268-3273` and only sets `m_vdencSinglePassEnable`. It does **not** change `m_numPasses`. It only changes `INIT_SinglePassOnly` (KBL:526).
- **Answer: single-pass BRC is not used on KBL by default.** It needs BRCPrecision==1, which VA-API does not set.

### 4b. Which PAK statistics each update sees

- `SetBufferToStorePakStatistics()` (`CMN.cpp:8651-8669`):
  ```
  nextRecycledBufIdx = (m_currRecycledBufIdx + 1) % CODECHAL_ENCODE_RECYCLED_BUFFER_NUM;
  nextPass = (m_currPass + 1) % CODECHAL_VDENC_BRC_NUM_OF_PASSES;
  m_resVdencBrcUpdateDmemBufferPtr[0] = &m_resVdencBrcUpdateDmemBuffer[nextRecycledBufIdx][0];   // next frame, pass 0
  m_resVdencBrcUpdateDmemBufferPtr[1] = m_lastTaskInPhase ? nullptr : &m_resVdencBrcUpdateDmemBuffer[m_currRecycledBufIdx][nextPass];
  ```
  The comment at L8656 says: "as the 2nd VDEnc+PAK pass may not be triggered."
- The PAK epilogue (`ENC:2935-2955`) stores MFC_BITSTREAM_BYTECOUNT_FRAME (dword 5) and MFC_AVC_NUM_SLICES (`m_vdencBrcNumOfSliceOffset`) into **both** `m_resVdencBrcUpdateDmemBufferPtr[0]` and `[1]`. The image status control store (dword 7, ENC:2852-2862) is the same. So:
  - `update(N, pass 0)` sees the PAK stats of the **last PAK of frame N-1**.
  - `update(N, pass 1)` sees the PAK stats of **PAK(N, pass 0)**.
  - `update(0, pass 0)` sees **no MI store at all**. Buffer `[0][0]` is written by nobody before it is used. Its contents are whatever the buffer held (see §5).
- ANV: the pass-0 update of frame N copies PAK_INFO, which holds frame N-1's PAK output (staged at `genX_cmd_video_enc.c:1944-1961`). The pass-1 update copies PAK_INFO **after** PAK(N) (`genX_cmd_video_enc.c:1972`, `genX_huc_brc.h:878-884`). So the data each update sees matches iHD. For frame 0, PAK_INFO has never been written. Whether the allocation is zeroed was not traced (`anv_video.c:973-978` shows size only).

### 4c. Second PAK: iHD conditional, ANV never

- iHD writes a REENCODE mask (`1<<31`, `CMN.cpp:5457-5463`) into `m_resPakMmioBuffer[1]`, then stores HUC_STATUS into `[0]` after HuC runs (`CMN.cpp:5466-5471`).
- The consumer is `CodechalEncoderState::UpdateEncodeStatus` (`ENC:3344`). It does `MHW_MI_ATOMIC_AND` (ENC:3605-3613) and copies the mask and status into GPRs. Per `CMN.cpp:6183-6189`, the pass≥1 path calls `UpdateEncodeStatus` when `m_inlineEncodeStatusUpdate && m_vdencBrcEnabled`. The exact batch-level gate that skips PAK pass 1 was **not traced** to its MI_CONDITIONAL instruction. What is confirmed: the second PAK is gated by the HuC-reported REENCODE state.
- ANV emits the same mask and HUC_STATUS store (`genX_huc_brc.h:726-737`) but has no consumer. `anv_h264_encode_video` emits **one** PAK per frame (`genX_cmd_video_enc.c:1990-2091`), and the pass-1 update only feeds the next frame.

## 5. Init fullness default, and other items the audit flagged

### 5a. `INIT_InitBufFull` default (question 4)

- VA-API `ParseSeqParams` (`DDI:1265-1320`): `InitVBVBufferFullnessInBit = bits_per_second` and `VBVBufferSizeInBit = bits_per_second << 1` (DDI:1316-1317, "set default same as MSDK, can be overwritten by HRD params"). Also `FramesPer100Sec = 3000` if unset (DDI:1309-1312).
- VA-API `ParseMiscParamHRD` (`DDI:172-189`) overwrites both from the app's `VAEncMiscParameterHRD`: `InitVBVBufferFullnessInBit = initial_buffer_fullness` (DDI:185), `VBVBufferSizeInBit = buffer_size` (DDI:184). **There is no fallback for zero.** An app that sends `initial_buffer_fullness = 0` gets `InitVBV = 0`.
- With InitVBV = 0 and a non-AVBR rate control, `CMN.cpp:4006-4024` disables VDEnc BRC and logs "Fatal error in AVC Encoding BRC parameters". The stream then runs CQP, not BRC.
- ANV's default is the same: `vbv_initial_fullness_bits = average_bitrate` (1 s) when the Vulkan info is absent, and `vbv_size_bits = 2*average_bitrate` (`genX_cmd_video.c:121-126`).
- **Check for the test:** if the iHD run used `initial_buffer_fullness = 0`, iHD was not running BRC at all. Confirm that BRC was on for the iHD run.

### 5b. `UPD_TR_TargetSize` and slice-level RC

- `UPD_TR_TargetSize_U32 = TargetFrameSize * 8` (`KBL:590`). `TargetFrameSize` is taken directly from VA `target_frame_size` (`DDI:312`). If the app passes a non-zero value, iHD enables target-size BRC for every frame, and ANV does not. This is the byte that is zero in ANV.
- `EnableSliceLevelRateCtrl` is set only by `ParseMiscParamMaxSliceSize` (`DDI:716-721`). The field has no default initialiser that I could find (`media_libva_encoder.h:167`). Treat it as false unless the app sends `VAEncMiscParameterMaxSliceSize`.

### 5c. Profile-level max frame (INIT byte 32)

`GetProfileLevelMaxFrameSize` (`BASE:496-572`):
```
bitsPerMB = 96 (levels 3.1-4) or 192 (other levels; minCR=2)     // byte units despite the name
maxBytePerPic        = max(numMB, MaxMBPS/172) * bitsPerMB
maxBytePerPicNot0    = (MaxMBPS * 100 / FramesPer100Sec) * bitsPerMB
userMaxFrameSize     = UserMaxFrameSize (VA max_frame_size>>3, DDI:358) or UserMaxPBFrameSize for P (DDI:1062)
profileLevelMaxFrame = min(maxBytePerPicNot0, userMax? min(userMax, maxBytePerPic) : maxBytePerPic)
VDEnc:  min(FrameHeight*FrameHeight, profileLevelMaxFrame)     // BASE:562-565, a height-squared quirk
```
ANV always writes `numMB*96`, which equals iHD's `maxBytePerPic`. The two agree at 1080p 30 fps level 4 (iHD: min(8160*96, 245760*100/3000*96) = min(783360, 786432) = 783360). They differ when `MaxMBPS*100/FPS100 < numMB`, for example 1080p at 60 fps (iHD 393216, ANV 783360), or when the app sends a max frame size.

## 6. Not found / not verified

- Whether the iHD second PAK pass is actually triggered for the first I-frame. The REENCODE status is produced by the firmware, and the gate was traced only to `UpdateEncodeStatus`, not to the instruction that skips the PAK.
- The FW's per-frame decision logic, for both drivers. The "adapt 2nd pass" thresholds (`TopFrmSzThr=32`, `BotFrmSzThr=24`, `TopQPDelta=2`, `BotQPDelta=1`) are identical in both drivers, so the difference is whether the second PAK runs.
- Whether DMEM and PAK_INFO allocations are zeroed (iHD `KBL:566-597` does not zero the update buffer; ANV uses memset for its DMEM copy but not for PAK_INFO). If iHD's buffer is not zeroed, `UPD_MBBudget_U16[]` (bytes 44-147) is never written by iHD and is uninitialised on first use.
- `m_16xMeSupported` default and `m_staticFrameDetectionInUse` default, both in the iHD common code. Not traced.
- `FrameSizeTolerance` default in VA-API (from `rc_flags.bits.frame_tolerance_mode`, `DDI:305`). Not traced.
- The MMIO register offsets used by iHD (`mmioRegisters->mfcBitstreamBytecountFrameRegOffset` etc.). ANV uses 0x08A0 / 0x08B8 / 0x0954 (`genX_huc_brc.h:665-667`). iHD values were not checked.
- Const-data buffer (`AVCVdencBRCCostantData`, 1728 bytes). Not compared in this pass, because the task asked for DMEM only. The structures are the same size. iHD fills it in `SetConstDataHuCBrcUpdate` (`CMN.cpp:5569-5610`) and ANV in `anv_h264_brc_fill_const_data` (`genX_huc_brc.h:319-391`).
- Units of `INIT_TargetBitrate`: iHD and ANV both use bits per second (no kbps conversion for AVC). Confirmed.

## 7. Ranked list of differences most likely to explain an oversized first I-frame and first P-frame

1. **Second PAK pass (re-encode) is missing in ANV.** iHD runs update(0), PAK(0), update(1), PAK(1), with PAK(1) gated by the HuC REENCODE state (CMN.cpp:6111-6125; 4180-4196; 5457-5471). ANV runs update(0), one PAK, and update(1) that only feeds the next frame (genX_cmd_video_enc.c:1990-2091). This is the only structural difference, and it would make the first frames unable to correct an overshoot. Confirm by checking whether iHD's PAK(1) runs for frame 0 and what HUC_STATUS bit 31 is.
2. **`INIT_FracQPEnable_U8` = 0 on iHD KBL, = 1 in ANV** (KBL:523 with default `m_rhoDomainStatsEnabled=false`; genX_huc_brc.h:511). Changes the firmware rate model for the first frame. Easy to test by flipping this one byte in ANV.
3. **`UPD_SklCabacWAEnable_U8` (byte 369) = 1 in ANV, 0 in iHD** (genX_huc_brc.h:593 vs. no write in KBL). A semantic flag, not a size one. Easy to test.
4. **`UPD_TR_TargetSize_U32` (byte 416) = 8 × target_frame_size in iHD, 0 in ANV** (KBL:590). If the test app sets `target_frame_size`, iHD runs target-size BRC and ANV does not. Check the app's value.
5. **`INIT_SliceSizeCtrlEn_U8` (80) and the slice-size fields (148-150, 234, 372, 374), iHD only**, when the app sends `VAEncMiscParameterMaxSliceSize` (DDI:716-721).
6. **MBBRC: `INIT_MbQpCtrl_U8` (79) and `INIT_DistQPDelta_I8` (85-88), iHD only**, when `VA_RC_MB` is set (CMN.cpp:3966-4001; CMN:1399-1403).
7. **`HmeDistAvailable_U8` (390) = 1 for P frames in iHD when 16x ME is on** (KBL:583-586). ANV writes 0.
8. **`INIT_InitBufFull` = 0 disables BRC in iHD** (CMN.cpp:4006-4024). If the iHD test run had initial fullness 0, it ran CQP. Verify BRC was active.
9. **`INIT_EstRateThreshB0_U8` (106-112) = 0 in iHD, {4..28} in ANV.** Only affects B frames, so probably irrelevant to a P-frame test.
10. **`INIT_ProfileLevelMaxFrame` (32)**: identical at 1080p 30 fps level 4. Diverges at high fps, a lower level, or with `UserMaxFrameSize`. Not the cause for the 30 fps case.
11. **Initial QP (76)**: formula and inputs identical except VBR `target_percentage` scaling and ANV's kbps rounding. The first I-frame QP should match between the two drivers unless those inputs differ.
12. **`UPD_FRAMENUM` (8)** uses H.264 `frame_num` in iHD and a plain frame counter in ANV. No difference for the first I or P frame.
