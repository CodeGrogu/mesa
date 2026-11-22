# Item 23: barrier flush precision (2026-11-22)

**Question:** do ANV's flushes on Gfx9 include redundant ones worth removing with dirty-cache tracking?

## Trace

vkmark 2025.01 ran headless at 1280×720 with `INTEL_DEBUG=pc` (about 50,000 frames, 130 MB of log; the
log is not kept). Every frame shows the same pattern of about ten PIPE_CONTROLs:

| Per frame | Reason | Required by |
| --- | --- | --- |
| depth + RT + HDC + UTDP flush, all four invalidates, CS stall | `flush/invalidate PIPELINE_SELECT 3D`, the first select in each command buffer | SKL PRM, PIPELINE_SELECT: "all the write caches flushed through a stalling PIPE_CONTROL … followed by … invalidate read only caches" |
| depth/RT and HDC/UTDP flushes | `end render command buffer L/L flush` | making writes available at the end of a command buffer |
| RT flush + CS stall | `aux color change (non fast-clear)` | aux state transition |
| PB stall + CS stall | `change pixel hash mode` | Gfx9 hashing change |
| flushes on `pipe barrier` | the application's barriers (src writes → dst) | Vulkan availability |

Each access category already maps to its specific cache (see `anv_pipe_flush_bits_for_access_flags`).
No flush in the trace flushes a cache that the preceding work could not have written. The only
candidate is the first-select flush, when the command buffer has done nothing yet.

## Measurement

A temporary build (not committed) skipped that flush when `current_pipeline == UINT32_MAX`
([vkmark-first-ps.txt](vkmark-first-ps.txt), interleaved):

| Round | Flush kept | Flush skipped |
| --- | --- | --- |
| 1 | 2903 | 2950 (+1.6%) |
| 2 | 3182 | 3213 (+1.0%) |
| 3 | 3092 | 3389 (+9.6%) |

The baseline alone varies by 9% between rounds. At about 3,000 fps a few µs per command buffer is
visible; at 60 fps it is under 0.1%.

## Decision: no change

The flush cannot be proven unnecessary. ANV chains command buffers into one batch, and at record time a
primary command buffer does not know which pipeline mode (3D or GPGPU) the previously submitted one ended
in. The PRM requirement applies exactly when the mode changes. Removing the flush safely would need
submit-time patching of the first `PIPELINE_SELECT`, which is out of proportion to a gain that only
shows at thousands of frames per second. The other per-frame flushes are required. Finer dirty
tracking has nothing to remove in this trace.
