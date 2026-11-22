## Summary

On Gfx9, drawing from vertex buffer A and then from a vertex buffer 4 GiB away renders with A's vertex data. ANV's VF cache range check queues its CS stall and VF invalidate while packing `3DSTATE_VERTEX_BUFFERS`, but `cmd_buffer_flush_gfx_state()` has already applied pending pipe flushes by then, so the invalidate lands after the draw that needed it.

Hardware: HD 620 (KBL GT2), i915, Linux 7.0. Reproduced on Mesa 26.0.8 (Ubuntu) and main `0e9e1086bb86`.

## Reproducer

[`vf-secondary/vf-alias.c`](https://github.com/CodeGrogu/mesa/tree/hd620-audit/HD620-full-audit/vf-secondary). Two buffers placed exactly 4 GiB apart with capture-replay addresses hold the same full-screen triangle, A red and B green. The image transition is done in a separate submission, then one rendering scope draws A and then B:

| Driver | Centre pixel |
| --- | --- |
| Mesa 26.0.8, main | **red (stale) in 50/50 runs** |
| `hd620-round2` | green in 50/50 runs |

Without the separate submission, the bug is masked: the BLORP layout transition binds a low-address VB, and the invalidate that triggers lands late at the right moment by coincidence.

## Fix

Commit "anv: invalidate the Gfx9 VF cache before binding, track it across secondaries" on [`hd620-round2`](https://github.com/CodeGrogu/mesa/commits/hd620-round2). It runs the range check and applies flushes before emitting the vertex buffers, which matches the SKL PRM's "invalidated before binding and then using". The same commit replaces the unconditional post-`vkCmdExecuteCommands` invalidate with entry/exit range tracking (2,000 → 2 invalidates per 1,000 executions). CTS command buffer, draw, multiview and vertex-input groups: 11,650 cases, 0 failures before and after.
