# Item 20: Gfx9 `MEDIA_VFE_STATE` re-emit after GPGPU → 3D (2026-11-22)

ANV emits a dummy `MEDIA_VFE_STATE` on every GPGPU → 3D switch on Gfx9. The original commit
(`bc612536eb2`, 2019) gives two reasons:
- an object-level preemption workaround;
- an unexplained geometry flicker "when 3D and compute are combined in the same batch" (fdo#109630,
  fdo#111267; the bug pages could not be fetched from here).

It costs about 0.5 µs per switch ([gen9-stalls](../gen9-stalls/README.md)).

## Looking for the flicker

New `stall-bench` scenarios ([`../gen9-stalls/stall-bench.c`](../gen9-stalls/stall-bench.c)) use
`blend2`'s per-cell check: 1,024 draws per command buffer, each into its own 8×8 cell, every cell
verified, 9 submissions per run.

- `vfe2`: a compute dispatch before every draw.
- `vfe3`: the dispatch writes the vertices the next draw reads ([`geom.comp`](geom.comp)), with a
  compute → vertex-input barrier in between.

The measurement driver's `ANV_GEN9_STALL_EXP=4` removes the re-emit.

| Scenario | Re-emit removed | Control |
| --- | --- | --- |
| vfe2 | 200 runs, 1,843,200 switches, 0 failing ([vfe2-stress.txt](vfe2-stress.txt)) | 50 runs, 0 failing |
| vfe3 | 200 runs, 1,843,200 switches, 0 failing ([vfe3-stress.txt](vfe3-stress.txt)) | 50 runs, 0 failing |
| vfe3 with `VFE3_NO_BARRIER=1` (negative control) | — | **413 of 1,024 cells wrong**, every run |

No flicker was reproduced, and the negative control shows the check would catch stale or misplaced
geometry. Real applications produced the 2019 bugs, however, and the preemption reason still stands.
The re-emit therefore stays on by default, and the commit `anv: add anv_gfx9_skip_vfe_reemit drirc
option` makes skipping it an opt-in, like item 19's option. With the option set, `vfe3` runs about
0.6 µs per iteration faster and stays pixel-correct.
