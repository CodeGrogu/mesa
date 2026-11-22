# Iris timer snapshot placement (item 18, 2026-11-22)

Commit `iris: place timer snapshots in whichever batch has pending work` on `hd620-round2`.

Before this change, every timer snapshot was written to the render batch. Snapshots were ordered
against the compute batch, which runs on its own hardware context, by flushing the compute batch at
each snapshot and the render batch at the next dispatch. A compute-only timed interval therefore
cost two forced submissions.

Now `write_value()` puts the snapshot in the compute batch when only that batch has unsubmitted
work, and in the render batch otherwise. It still waits for the other batch's outstanding work, and
it sets `other->order_after`. `iris_get_command_space()`, through which every command emission
passes, makes that batch wait for the snapshot before it records its next command. This covers
draws, dispatches, clears, blits and copies, and replaces the dispatch-only flag.

`gl-timer-ordering.py` overhead, median µs per iteration:

| Case | Original (unordered, wrong results) | Forced submissions | This change |
| --- | --- | --- | --- |
| small dispatch, no query | 10.8 | 11.1 | 12.2 |
| small dispatch inside `GL_TIME_ELAPSED` | 12.8 | 73.7 | **15.1** |
| small draw inside `GL_TIME_ELAPSED` | 188.4 | 185.9 | 191.4 |
| draw + dispatch inside `GL_TIME_ELAPSED` | 217.8 | 252.4 | 260.8 |

The mixed draw+dispatch case still needs one cross-batch wait per interval, because its work is
in both batches. All ordering checks pass, as do `gl-timer-conversion.py` (0 failures),
`gl-query-saturation.py` (0 failures) and the live rollover straddle
(`../iris-timer-conversion/straddle.log`, run on this build: sha256 `120d52c1…`).

Rusticl (`opencl-smoke.py --require-valid-profiling`, `opencl-{old,new}-{1,2}.log`) passes on both
builds. Enqueue-and-wait latency is the same within noise (about 1.4–1.8 ms per small kernel, 11.8 ms
for the 256-round kernel), so Rusticl's per-event cost is dominated by something other than these
flushes.
