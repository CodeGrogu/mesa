# Item 13: H.264 CBR/VBR rate control in Vulkan encode on Gen9 (2026-11-22)

Commit `anv: H.264 CBR/VBR rate control on Kaby Lake class Gfx9` on `hd620-round2`.

ANV already drove the HuC BRC firmware for H.264 on Gfx12. The port to KBL rests on two documents that
Haiku extracted from Intel's media driver:
- [ABI.md](ABI.md): struct layouts, regions, SLB and PAK feedback.
- [DMEM-VALUES.md](DMEM-VALUES.md): field values compared with ANV. Its corrections supersede ABI.md
  on bytes 134, 369 and 371.

The layouts match Gfx12. What differs on Gen9:

| Issue | Finding | Fix |
| --- | --- | --- |
| KBL DMEM | `SliceSizeCtrlWA` (134) = 1; fractional QP off; byte 371 is the conservative threshold, byte 369 is `SklCabacWAEnable` | Programmed as iHD does |
| MMIO | No `AddCSMMIOStartOffset` before Gfx11 | VCS0 base 0x12000 for the MFC registers; absolute HuC status offsets |
| Ordering | HuC BRC passes issued after the MFX/VDEnc state hung the video engine: `IPEHR` = `MI_FLUSH_DW` after `HUC_START` ([hang-vcs0.txt](hang-vcs0.txt), [excerpt](hang-vcs0-excerpt.txt)) | Run the BRC passes before the frame selects the MFX pipeline, as iHD does |
| Re-encode | KBL corrects overshoot with a second PAK pass that iHD runs only when the HuC asks. [`cbbe-probe`](cbbe-probe/cbbe-probe.c) shows that on Gen9 VCS, `MI_CONDITIONAL_BATCH_BUFFER_END` ends the whole batch, even from a second-level batch: the first-level marker after the return was never written. Always re-encoding gave 750 KB I-frames at QP 26. | Forced second pass only for the first frame of each picture type after a BRC init or reset |

## Results

FFmpeg `h264_vulkan` versus iHD `h264_vaapi`, 1080p30, 30 s (25 s for the static clip). VBV is
simulated from packet sizes with [rate-check.py](rate-check.py) by [run-matrix.sh](run-matrix.sh).
Raw logs: [rate-matrix.txt](rate-matrix.txt) (first ANV version and iHD) and
[rate-matrix-anv-v2.txt](rate-matrix-anv-v2.txt) (final ANV).

| Source / mode | ANV final | iHD |
| --- | --- | --- |
| moving, CBR 2 Mbit/s, GOP 30 | +37.3%, 779 underflows (needs > QP 51) | +2.2%, 0 |
| moving, CBR 8 Mbit/s, GOP 120 | +0.2%, 0 | −0.5%, 0 |
| moving, CBR 4 Mbit/s, GOP 60, 2 B-frames | +1.3%, 0 (was +14% with a 17 Mbit first B-frame) | +1.1%, 0 |
| moving, VBR 6/9 Mbit/s | 6.12 Mbit/s, 0 | 5.95 Mbit/s, 0 |
| static, CBR 2 Mbit/s | +1.1%, 0 | +0.6%, 0 |
| static, CBR 8 Mbit/s | +0.7%, 0 | +1.9%, 0 |
| static, CBR 4 Mbit/s, B-frames | +0.3%, 0 | +1.4%, 0 |
| static, VBR 6/9 Mbit/s | 6.13 Mbit/s, 0 | 5.96 Mbit/s, 0 |

On the moving clip at 2 Mbit/s both drivers sit at QP 51. iHD's conditional re-encode then produces
mostly-skip frames to meet the budget; Gen9 cannot express that re-encode inside an application command
buffer. Mid-stream rate changes were not exercised, because FFmpeg cannot change Vulkan rate control
mid-stream. The reset path re-runs the BRC init and clears the first-of-type state.

CTS ([enc264.txt](enc264.txt), 204 `video.encode.h264` and `video.capabilities` cases, with
`ANV_DEBUG=video-encode`): the four `rc_cbr`/`rc_vbr` cases go from NotSupported to Pass. There are no
other changes, 0 failures and no GPU hangs ([base](enc264-base.status), [new](enc264-new.status)).
