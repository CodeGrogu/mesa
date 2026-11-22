# BLORP CC_VIEWPORT re-emit on Gfx9 (TODO item 22)

`blorp_emit_cc_viewport()` writes a fresh 32-byte `CC_VIEWPORT` on every Gfx9 BLORP operation instead of reusing the cached dynamic state. Issue mesa#11647 (closed) traced a roughly 10% flake in `dEQP-VK.multiview.dynamic_rendering.depth.no_queries.3_6_12_9_6_12_9_3_6_12_9_3` on CML to the reuse. The flake rate dropped below 1% after an unrelated device-init change, and the reporter suspected the bug was hidden rather than fixed.

## Test

A measurement-only toggle (`BLORP_GFX9_CACHED_CC_VIEWPORT`, never added to the project tree) re-enabled reuse on the round-2 release ANV. The flaky test ran 300 times in each mode (`flake.txt`):

| Mode | Pass | Fail |
| --- | --- | --- |
| Default (re-emit) | 300 | 0 |
| Cached (reuse) | 300 | 0 |

## Decision: keep the re-emit

300 clean runs cannot rule out a failure rate below 1%; with a 1% rate there's about a 5% chance of seeing no failures. The saving is one 32-byte dynamic-state allocation per BLORP operation; the `3DSTATE_VIEWPORT_STATE_POINTERS_CC` packet is emitted either way. That is not enough to justify the risk.
