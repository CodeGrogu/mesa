# VP9 8-bit encode on KBL through iHD: researched, blocked (2026-11-22)

Test 15 found that no driver exposes VP9 encode on this HD 620, although Intel
marketed VP9 8-bit encode for Kaby Lake. This note explains why, from the
checked-out media-driver source (`media-driver/media_driver`).

## What the open driver has for Gen9/KBL

- **Hardware commands: yes.** `agnostic/gen9_kbl/hw/vdbox/mhw_vdbox_hcp_g9_kbl.cpp`
  implements `AddHcpVp9PicStateEncCmd` (around line 1375), and
  `VP9_PIC_STATE` is shared by decode and encode. The KBL HCP (PAK) can
  write VP9 bitstreams.
- **Kernel media states: listed only.** `agnostic/gen9_kbl/codec/hal/codechal_hw_g9_kbl.cpp`
  lines 67-80 list `CODECHAL_MEDIA_STATE_VP9_ENC_I_32x32`, `..._ENC_P`,
  `..._ENC_TX` and the `..._PAK_*` recon/deblock states.
- **Encoder: no.** `agnostic/gen9*/codec/hal/` contains encoders for AVC, HEVC,
  MPEG-2 and VP8 (`codechal_encode_*_g9*.cpp`). There is no VP9 encoder. The
  only VP9 encoder (`agnostic/common/codec/hal/codechal_vdenc_vp9_base.*`)
  is built on VDENC, which VP9 gets only from Gen11 on.
- **Kernels: no.** The Gen9 and KBL kernel tables
  (`agnostic/gen9*/codec/kernel/igcodeckrn_g9*.h`) have no VP9 entries. So
  the motion-search and mode-decision kernels that VP9 ENC would need on the
  EUs were never published.
- **Capability: no.** `MediaLibvaCaps::LoadVp9EncProfileEntrypoints()`
  (`linux/common/ddi/media_libva_caps.cpp:1745`) is empty in the base class
  that Gen9 uses, so no `VAProfileVP9Profile0` encode entrypoint is ever
  added.

## Conclusion

The marketed feature presumably depended on Gen9 VP9 ENC kernels that were
not released in the open-source driver. Exposing it would mean writing the
ENC kernels (motion search, mode decision and transform selection for the
Gen9 EU ISA), a Gen9 VP9 encoder on top of the HCP PAK commands, and rate
control. That is a new encoder, not a capability fix.

A possible lighter design, not attempted: do the mode decision on the CPU and
use the HCP only as the PAK. It would need the undocumented Gen9 VP9 PAK
input format, and would lose most of the benefit of hardware encode.

Status: blocked. Nothing can be enabled by a patch. For VP9 encode on this
laptop, use the CPU (libvpx-vp9 realtime, as in `../av1-playback/`).
