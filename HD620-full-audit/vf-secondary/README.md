# Gfx9 VF cache: stale vertices and secondary command buffers (TODO item 24)

The Gfx9 VF cache tags lines by the low 32 bits of the vertex buffer address. ANV therefore tracks bound and dirty vertex buffer ranges and invalidates the VF cache (CS stall plus VF invalidate) when the tracked range would exceed 4 GiB (`genX(cmd_buffer_set_binding_for_gfx8_vb_flush)`). Secondary command buffers weren't covered by that tracking, so every `vkCmdExecuteCommands` ended with an unconditional CS stall and VF invalidate.

## Test: two vertex buffers exactly 4 GiB apart

`vf-alias.c` uses capture-replay addresses to place buffer B at A + 4 GiB, so both have the same low 32 bits. Both hold a full-screen triangle, A red and B green. Each case draws A and then B and checks that the centre pixel is green:

| Case | What runs | Driver without the fixes | Fixed |
| --- | --- | --- | --- |
| entry | A in the primary, then B in a secondary | 0/50 stale | 0/50 |
| exit | A in a secondary, then B in the primary | 0/50 stale (only because of the unconditional post-secondary invalidate) | 0/50 |
| inline | A and B in the primary, with a layout transition in the same batch | 0/50 stale | 0/50 |
| **clean** | **A then B in one scope; transition done in an earlier submission** | **50/50 stale** (also on installed Mesa 26.0.8) | **0/50** |

## Upstream bug: the invalidate was queued after the draw that needed it

`cmd_buffer_flush_gfx_state()` applies pending pipe flushes before it emits `3DSTATE_VERTEX_BUFFERS`. The VF range check ran while packing those buffers, so its invalidate sat pending until the *next* draw. Drawing from A and then from a buffer 4 GiB away fetched A's cached vertex data.

The inline case passed only because an earlier, unrelated invalidate landed late at the right moment: the BLORP layout transition binds a low-address VB, which widens the range on A's bind. Removing the post-secondary invalidate first exposed the bug: the exit case went from 0/20 to 20/20 stale. That led to the clean case, which fails without any secondary involved.

Fix (`genX_cmd_draw.c`, `cmd_buffer_flush_vertex_buffers`): on Gfx9, run the range check for every binding being emitted, apply pipe flushes, then emit the vertex buffer state. This puts the invalidate before binding, as the SKL PRM requires ("invalidated before binding and then using").

## Secondary command buffer tracking

`anv_cmd_graphics_state` gains `vf_cache_invalidated` plus `vb_entry_ranges`/`ib_entry_range`: the ranges a command buffer used before its first VF invalidate.

- **Before a secondary:** invalidate only if the container's dirty ranges merged with the secondary's entry ranges exceed 4 GiB. This also covers the previously untracked entry direction.
- **After a secondary:** the container's dirty ranges become the secondary's (if it invalidated) or the union (if it didn't). The unconditional invalidate is removed.

Benchmark: 1,000 executions of a one-draw secondary in one scope (`bench.txt`). VF invalidates dropped from 2,000 to 2 per run. GPU time stayed at about 13.5 µs per execution, because other per-`vkCmdExecuteCommands` work dominates on this workload.

## Index buffers

`3DSTATE_INDEX_BUFFER` on Gfx9 is not reported to the VF range tracker (`ib_bound_range` is never set). The same 4 GiB test with two index buffers (`IDX=1`) showed 0/20 stale on both installed Mesa and the fixed build. No aliasing was observed, so nothing was changed there.

## CTS

See `../TESTING.md` (VF section) for the command-buffer, draw, multiview and round-2 case lists, run before and after.
