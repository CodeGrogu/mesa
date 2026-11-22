# Iris timer ordering fix (HD 620, Gen9 KBL GT2)

Status: local candidate. It is not committed and has not been sent upstream. No system configuration was changed. The pre-existing ANV diff is unchanged: `git diff -- src/intel/vulkan` has sha256 `6ebf1574…eaf22df` both before and after this work.

## Defect and cause

Saved logs showed the defect first: [gl-compute-timing-controls.txt](../gl-compute-timing-controls.txt), [gl-compute-current-iris.txt](../gl-compute-current-iris.txt) and [independent-review/iris-current.log](../independent-review/iris-current.log). For a dispatch of about 11 ms, a timestamp pair returned 250–333 ns and `GL_TIME_ELAPSED` returned 0 ns.

Both query boundaries fail for the same reason. In `write_value()`, `iris_query.c` writes every timer snapshot (`TIME_ELAPSED` begin and end, `TIMESTAMP`, `TIMESTAMP_DISJOINT`, `TIMESTAMP_RAW`) with a pipelined `PIPE_CONTROL` into `IRIS_BATCH_RENDER`. `iris_launch_grid()` records dispatches into `IRIS_BATCH_COMPUTE`, which is a separate i915 context. The two batches have no dependency on each other: the query BO is used only by the render batch. In the GL sequence *snapshot A → dispatch → snapshot B*, A and B therefore go into one render batch, and the GPU can run that batch before or after the compute batch. A and B end up adjacent, so the interval is about 0.

The ARB_timer_query spec requires each snapshot to happen after all earlier commands have completed. It must also come before later work, or a later dispatch would be counted in the interval.

## Fix (48 added lines, Iris only)

[iris-timer-fix.diff](iris-timer-fix.diff) changes Iris in three places:

- `iris_batch_wait_for_batch(batch, other)` in `iris_fence.c` flushes `other` if it has queued commands. If `other->last_fence` has not signalled, it adds that fence's syncobj as an `IRIS_BATCH_FENCE_WAIT` on `batch`, without adding the same syncobj twice. Batches on one context run in order, so waiting for the last submission also covers all earlier ones. Every wait refers to a batch that has already been submitted, so the waits cannot form a cycle.
- `write_value()` handles the earlier-work boundary. Before writing a timer snapshot, the render batch waits for the compute batch. It also sets `ice->state.compute_after_timer_snapshot`.
- `iris_launch_grid()` handles the later-work boundary. If that flag is set, the compute batch waits for the render batch, which contains the snapshot, and the flag is cleared. The check runs lazily, at the first dispatch after a snapshot.

Synchronization is added only where compute and timers meet:

- **Contexts that never dispatch compute:** a snapshot only tests an empty compute batch and a NULL fence. Nothing is flushed and no wait is added.
- **Where the cost is paid:** one compute flush at a snapshot that follows compute work, and one render flush at the first dispatch after a snapshot.
- **Unchanged:** pipelined timestamp writes inside the render batch, occlusion/statistics queries, and query result resolution.

## Results (hardware, HD 620, same session, serial runs)

The original probe [gl-compute-timing.py](../../gl-compute-timing.py) used 2^20 items and 256 rounds. Values are medians of 9 runs.

| Build | Timestamp pair | `GL_TIME_ELAPSED` | Explicit-drain control | Output checks |
| --- | ---: | ---: | ---: | --- |
| Installed 26.0.8 ([log](installed-gl-compute-timing.log)) | 0.000250 ms | 0 ms | 11.60 ms | pass |
| Build tree before patch ([log](baseline-candidate-pre-patch.log)) | 0.000250 ms | 0 ms | 11.62 ms | pass |
| Candidate ([log](candidate-gl-compute-timing.log)) | **11.27 ms** | **11.29 ms** | 11.63 ms | pass |

The new focused probe is [gl-timer-ordering.py](../../gl-timer-ordering.py). It runs 17 timing checks against glFinish-drained references. The checks cover compute, graphics, and both orders of mixed work. Each boundary is tested for including the timed work and for excluding earlier and later work. A four-timestamp chain (compute → draw → compute) is also tested, along with a query-buffer resolution path. Compute buffer words and rendered pixels are checked against CPU results.

| Build | Failed checks | Failing cases |
| --- | --- | --- |
| Candidate ([log](candidate-gl-timer-ordering.log)) | **0 of 17** | none |
| Build tree before patch ([log](baseline-gl-timer-ordering.log)) | 7 of 17 | every case where compute should be included |
| Installed ([log](installed-gl-timer-ordering.log)) | 7 of 17 | the same 7 |

Selected candidate values:

| Case | Candidate | Reference |
| --- | ---: | ---: |
| `TIME_ELAPSED`, compute then draw | 22.36 ms | 22.71 ms drained |
| `TIME_ELAPSED`, draw then compute | 22.39 ms | 22.71 ms drained |
| Timestamp chain interval 1 (compute) | 11.26 ms | |
| Timestamp chain interval 2 (draw) | 11.10 ms | |
| Timestamp chain interval 3 (compute) | 11.30 ms | |
| Every exclusion case | 0.000 ms | |

With the patch applied, graphics-only timing is unchanged: draw intervals were 11.10 ms on the candidate and 11.07 ms on the pre-patch build.

### Overhead

Wall time per iteration for 200 small operations (4,096-item dispatch or 1-round full-screen draw). Values are medians of 5 runs.

| Loop body | Build tree before patch | Candidate |
| --- | ---: | ---: |
| Dispatch, no query | 11.5 µs | 11.1 µs |
| Dispatch inside `TIME_ELAPSED` | 14.0 µs | **75.1 µs** |
| Draw, no query | 182.1 µs | 182.7 µs |
| Draw inside `TIME_ELAPSED` | 185.9 µs | 187.3 µs |
| Draw + dispatch inside `TIME_ELAPSED` | 201.7 µs | 256.1 µs |

Wrapping each tiny dispatch in its own timer costs about 60 µs per interval. That is two extra submissions, each with a cross-context wait. This cost is the price of correct ordering across two hardware contexts. It matters only for code that wraps many very small dispatches in separate timers. Graphics-only timer use did not change measurably.

## Rusticl raw timestamps

Rusticl itself was **not tested on the candidate**, for a concrete reason. The candidate build has `gallium-rusticl=false`, and the `hd620-mesa` container has no `rustc`, `bindgen` or `cargo`. Building it would mean installing a toolchain and reconfiguring, which is outside this narrow fix. The staged Rusticl uses the distro 26.0.8 `libgallium` and cannot load the candidate.

Source analysis of the checked-out tree:

- **How Rusticl takes timestamps.** `GPUProfiler` (`frontends/rusticl/core/event.rs`) uses `PIPE_QUERY_TIMESTAMP_RAW`, because Iris implements `convert_timestamp`. It creates the start query (`end_query` → `iris_begin_query` → `write_value`) and calls `get_query_result_resource(PIPE_QUERY_WAIT)`. It then runs the kernel (`launch_grid`, compute batch) and repeats the query sequence for the end timestamp. `CPUProfiler` uses `PIPE_QUERY_TIMESTAMP` with the same `write_value` path.
- **The patch covers the defect.** Both Rusticl profilers take snapshots through the same `write_value` path that the patch orders. Rusticl's 250 ns event against about 11.1 ms of i915 render time ([opencl-smoke.txt](../opencl-smoke.txt)) has the same mechanism as the GL defect. The patch should fix it, but this is not verified on hardware.
- **The query-buffer problem below does not affect Rusticl.** For `TIMESTAMP_RAW`, both the CPU path (`map->start`) and the GPU path (`start_val`) return the raw tick, and Rusticl converts on the CPU. The GL probe exercises the same gallium sequence with `GL_TIMESTAMP` written to a `GL_QUERY_BUFFER`, using `get_query_result_resource` with `PIPE_QUERY_WAIT`. Its ordering checks pass on the candidate.
- **Cost for Rusticl.** Each profiled enqueue pays the extra render submission described above.

## Pre-existing defect: `GL_TIMESTAMP` in query buffers (fixed separately)

Fixed on top of this patch by [../iris-timer-conversion/](../iris-timer-conversion/README.md). That fix makes the GPU path exact with a 64-bit long division on the CS ALU. The analysis below is kept as found.

`calculate_result_on_gpu()` resolves `PIPE_QUERY_TIMESTAMP` as `(raw * 83) & (2^36 - 1)`. That truncates the 83.33 ns tick and masks the value *after* scaling. `calculate_result_on_cpu()` returns `raw * 83.33` with no mask. Values written to a query buffer therefore differ from `glGetQueryObjectui64v`. This happened in 9/9 runs on every build, the installed driver included (`KNOWN-DEFECT` line in each log).

The pre-patch build hid this, because both snapshots went through the GPU path. On the candidate, the start snapshot often lands before the buffer resolve and takes the CPU shortcut, while the end snapshot uses the GPU path. A timestamp pair written to a query buffer can therefore give a delta of about −1.6e12 ns ([candidate log](candidate-gl-timer-ordering.log)). The same mixing could already happen before the patch whenever the start batch had been flushed.

**Blocker for fixing it here:** an exact GPU conversion needs a 64-bit multiply followed by a divide by 3. `mi_builder` provides only `mi_udiv32_imm`. Options are to compute it on the GPU with a shader, or to give query-buffer resolution consistent precision. Either is a separate change that needs its own tests.

## Limits and risks

- **Prior render work waits for compute.** Render work recorded before a snapshot is in the same render batch, so it now also waits for outstanding compute. This is extra serialization, but GL ordering allows it.
- **Gen12+ blitter batch.** Gen12+ has a third batch (`IRIS_BATCH_BLITTER`) that this patch does not order. It is untested, because the HD 620 has no such batch in `iris_foreach_batch`.
- **Xe KMD.** The Xe kernel driver path is untested. The helper uses the KMD-agnostic `last_fence->syncobj`.
- **Not run.** The ordinary GL regression via glmark2 could not use the candidate: the build is surfaceless-only (`platforms=[]`, `glx=disabled`) and glmark2 needs X11 or Wayland. The probe's graphics pixel checks and graphics-only timer cases stand in for it. Piglit, the GL CTS and the OpenCL CTS were not run.
- **Overhead.** About 60 µs for each timer interval that contains compute (see above). A possible follow-up is to write the snapshot into the compute batch when an interval contains only compute. That would need its own correctness argument.

## Files

| File | Contents |
| --- | --- |
| [iris-timer-fix.diff](iris-timer-fix.diff) | Iris diff (sha256 `26bbc03e…`) |
| [build.log](build.log) | Serial build: `ninja -j1` in `/build/audit-iris`, exit 0, no warnings |
| [candidate-sha256.txt](candidate-sha256.txt) | Candidate library hashes. `libgallium` `d3d98884…`; `libEGL` unchanged |
| [baseline-sha256.txt](baseline-sha256.txt) | Hashes of the pre-patch build-tree libraries, preserved in the container at `/build/iris-timer-baseline` with the same rpath layout. `libgallium` `e2f1e508…` matches [iris-build-sha256.txt](../iris-build-sha256.txt) |
| `*-gl-compute-timing.log`, `*-gl-timer-ordering.log` | Hardware runs. Each build-tree log records the mapped driver libraries |

Commands, from `HD620-full-audit/`:

```sh
docker exec hd620-mesa sh -c 'cd /build/audit-iris && ninja -j1'
docker exec -i -e EGL_TEST_LIBRARY=/build/audit-iris/src/egl/libEGL.so.1.0.0 hd620-mesa python3 - < gl-timer-ordering.py
docker exec -i -e EGL_TEST_LIBRARY=/build/iris-timer-baseline/src/egl/libEGL.so.1.0.0 hd620-mesa python3 - < gl-timer-ordering.py
EGL_TEST_LIBRARY=libEGL.so.1 python3 gl-timer-ordering.py   # installed Iris
```
