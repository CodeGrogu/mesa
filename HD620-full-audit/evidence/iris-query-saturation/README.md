# Iris 32-bit query-buffer saturation (HD 620, Gen9 KBL GT2)

Status: **verified and independently approved for HD 620.** The pre-saturation driver is pinned at `/build/iris-presat`, and the candidate is at `/build/audit-iris`. See [the driver build log](build.log) for the baseline hash and rebuild output.

- The [candidate GL run](new-gl-query-saturation.log) passed all 10 categories and 44 samples. The [baseline](presat-gl-query-saturation.log) failed the four expected overflow categories. The mapped Gallium library and its hash are recorded in each log.
- The [Gen9 MI run](mi-sat-all-hw.log) passed 32 tests and 4,851 checks, including both bounds, predicate-false stores and adjacent-byte canaries. The [clamp-omitted control](mi-sat-negative.log) failed 84 assertions as expected.
- The candidate passed the [64-bit conversion](new-gl-timer-conversion.log) and [17 timing-order checks](new-gl-timer-ordering.log). GL resolve-path coverage is inferred from query completion and command sequencing. The MI harness directly exercises the GPU clamp. Direct-state-access callers share the reviewed store implementation by source inspection.

It goes on top of the [timer ordering](../iris-timer-fix/README.md) and [timer conversion](../iris-timer-conversion/README.md) candidates, which are unchanged. No ANV file was touched. The working-tree `git diff -- src/intel/vulkan` was sha256 `aa6a06ac…f99` before and after this task. That differs from the `6ebf1574…` recorded in the conversion README because the concurrent HiZ task has edited ANV since.

## Defect

`GL_QUERY_BUFFER` results requested as `GL_INT` or `GL_UNSIGNED_INT` reach Iris as `PIPE_QUERY_TYPE_I32` or `U32`. Iris stored the **low 32 bits** of the 64-bit result on both paths:

- **CPU path:** `store_data_imm32(q->result)`.
- **GPU path:** `mi_store(mi_mem32, result)`.

The spec requires a saturated value instead:

- **ARB_query_buffer_object** says a buffer result is written as `glGetQueryObject*` would return it. Only the destination changes.
- **ARB_timer_query** says a result too large for the requested type is clamped to the largest representable value.
- **The Mesa frontend** already does this for client memory (`queryobj.c:1181-1190`): `0x7fffffff` for `iv` and `0xffffffff` for `uiv`.

So for a `GL_TIMESTAMP` of about 2.5e12 ns:

- The buffer got the truncated low word, while `glGetQueryObjectuiv` returned `0xffffffff`.
- A `GL_INT` buffer got a value with bit 31 set, which reads back as a negative number.

The conversion README had recorded this as a known, out-of-scope limit.

## Fix

The change is [iris-query-saturation.diff](iris-query-saturation.diff) (sha256 `1abb059a…3b94`), one file, `iris_query.c`. It applies to the pre-task working tree, whose Iris and common diff is [pre-saturation-iris-common.diff](pre-saturation-iris-common.diff) (sha256 `3f06ac47…afd0`). [iris-query-saturation-full.diff](iris-query-saturation-full.diff) (sha256 `97052104…32e8`) is the complete Iris and common working-tree diff afterwards. I checked that HEAD + the pre-task diff + this diff reproduces the working-tree `iris_query.c` byte for byte.

- **`query_result_limit32(type)`** returns `INT32_MAX` for I32 and `UINT32_MAX` for U32. Every Iris result is a nonnegative unsigned 64-bit logical value:
  - counters are `end − start`;
  - timers are an unsigned scale;
  - booleans are 0 or 1.

  A plain unsigned min is therefore the whole saturation rule.
- **CPU path:** `store_data_imm32(MIN2(q->result, limit))`.
- **GPU path:** if the type is 32-bit, `result = mi_umin_imm(&b, result, limit)`, then the existing `mi_mem32` store, plain or `mi_store_if`. `mi_umin_imm` computes `mask = ult(limit, v)` and returns `v ^ ((v ^ limit) & mask)`. It uses only the existing `mi_ult`, `mi_ixor` and `mi_iand`, all exact over 64 bits on gfx8+.
  - **Constant input:** an immediate input folds on the CPU.
  - **Memory input:** a memory or register input (`TIMESTAMP_RAW` returns `start_val` as a MEM64) is loaded into a GPR once, by `mi_value_to_gpr`.
  - **References:** the value is used three times, so the function takes two extra references with `mi_value_ref` and consumes the caller's one. It returns a fresh GPR with one reference, which `mi_store` or `mi_store_if` drops. Those two stores accept a GPR directly.
  - **GPR use:** by hand count, peak is the held result + 3 temporaries (`ult` destination, immediate load, `ixor` destination) = 4 of Iris's 15. Once the calculation returns, the timer conversion holds only that one GPR.
- **Unchanged:**
  - 64-bit destinations;
  - availability (`index == −1` copies `snapshots_landed`, 4 or 8 bytes);
  - the `NO_WAIT` predicate setup (`mi_store_if` gated on `snapshots_landed`);
  - store width, so a 32-bit store still writes exactly 4 bytes and the adjacent bytes are untouched;
  - client-memory `get_query_result`;
  - `set_predicate_for_result`, which has its own `mi_nz` and `mi_z`.

### Other Gallium callers reviewed

- **GL `queryobj.c:482`** is the only caller that asks Iris for I32 or U32.
- **Rusticl** (`rusticl/mesa/pipe/query.rs:114`) always asks for U64, so it is unaffected.
- **Lavapipe** (`lvp_execute.c:3685-3704`) asks for U32 or U64, but drives llvmpipe, never Iris.
- **`u_threaded_context`, `driver_trace` and `driver_ddebug`** pass the call through unchanged.
- **Other drivers for comparison:**
  - zink (`zink_query.c:754-761`) already applies the same `MIN2(INT_MAX or UINT_MAX)` rule;
  - nvc0 and d3d12 handle signedness explicitly;
  - llvmpipe (`lp_query.c:372-380`) masks with `& INT32_MAX` and `& UINT32_MAX`, which truncates rather than saturates. That is a separate driver and was not changed.

## Verification done here (no build, no GPU)

- **Bit model check.** A Python model of `v ^ ((v ^ L) & (L < v ? ~0 : 0))` equals `min(v, L)` for 400 022 cases: 11 edge values and 200 000 random values of 8 to 64 bits, × 2 limits. There were 0 mismatches.
- **Sync check.** `mi-harness/check-sync.sh` reports `mi_umin_imm in sync (14 lines)` against `iris_query.c`. A deliberately changed copy reports `OUT OF SYNC`, so the check works.
- **Probe syntax.** `python3 -m py_compile gl-query-saturation.py` passes.

The initial source-only checks preceded the successful driver and harness builds. The commands below verify GPU behavior.

## Regression probes

### 1. MI harness: [mi-harness/sat.cpp](mi-harness/sat.cpp)

It includes the conversion harness `run.cpp`, which brings in the unmodified `mi_builder_test.cpp`, the gtest stand-in and `main()`. It also carries a verbatim copy of `mi_umin_imm`, kept honest by `check-sync.sh`.

- **`iris_query_saturation`:** 14 values × 2 limits × 3 source kinds:
  - **Values:** 0, 1, 2^31−2, 2^31−1, 2^31, 3e9, 2^32−2, 2^32−1, 2^32, 0x17fffffff, about 2.5e12, 0xffffffff7fffffff, 2^63 and 2^64−1.
  - **Source kinds:** an MI_MATH GPR, a raw MEM64 (as `TIMESTAMP_RAW` gives), and the 0/1 boolean.
  - **Each case checks:**
    - the plain 32-bit store (`WAIT`);
    - `mi_store_if` with the predicate loaded from a landed flag of 1, exactly as Iris does it;
    - `mi_store_if` with the flag at 0, which must leave the canary;
    - the full 64-bit min;
    - the canary in every adjacent dword;
    - `b.gprs == 0` after the stores, so no GPR leaks.
- **`iris_query_saturation_gprs`:** the clamp on a held GPR leaves exactly 1 GPR allocated and frees it after the store. It also prints the batch dwords.
- **`build-neg.sh`** builds `-DNEGATIVE_CONTROL`, which skips the clamp. That build should fail the cases where the value is above the limit.

### 2. GL: [gl-query-saturation.py](../../gl-query-saturation.py)

It is a focused copy of `gl-timer-conversion.py`: the same EGL, GL and probe setup, with new cases. Each case runs on both resolve paths. The GPU path resolves before the end snapshot lands; the CPU path calls `glFinish` first.

- `GL_TIMESTAMP`, above `UINT32_MAX`: both widths saturate.
- `GL_TIME_ELAPSED` across a 3 s idle gap (begin, `glFlush`, sleep 3 s, end): about 3e9 ns, strictly between `INT32_MAX` and `UINT32_MAX`. `GL_INT` must be `0x7fffffff` while `GL_UNSIGNED_INT` is exact. This case separates the two limits on real hardware without a long GPU workload.
- In-range values, where all widths must be exact:
  - `TIME_ELAPSED` of a small draw;
  - `SAMPLES_PASSED` of a 1024² draw, which must be 1 048 576;
  - `ANY_SAMPLES_PASSED`, which must be 1.

Each run also checks:

- the frontend client `uiv` and `iv` values;
- the 64-bit `RESULT` and `NO_WAIT` values;
- 32-bit `NO_WAIT` for both types, which must be the saturated value or untouched;
- `GL_INT` `AVAILABLE` == 1;
- the upper 4 bytes after every 32-bit store, which must still hold the sentinel.

A run whose value falls outside the intended range is retried, for example a timestamp within 4.3 s after a 36-bit rollover.

### Predictions for the hardware run

| Probe | presat build (expected) | new build (expected) |
| --- | --- | --- |
| `run-sat iris_query_saturation` | — | all pass |
| `run-sat-negative iris_query_saturation` | fails for every value above the limit (GPR and MEM sources); booleans pass | — |
| `run-sat` (all tests) | — | the existing 30 tests still pass, plus 2 new |
| gl-query-saturation | FAIL: timestamp and 3 s cases on both paths (low word; `GL_INT` 3e9 has bit 31 set) | 0 failed checks |
| gl-timer-conversion | 0 failures with explicit legacy truncation reference | 0 failures with `glGetQueryObjectuiv` as the 32-bit reference |

## Commands

These need the HD 620 and the `hd620-mesa` container with nothing else on the GPU. Run them in order from `HD620-full-audit/`.

```sh
# 0. Source checks (no GPU)
sh evidence/iris-query-saturation/mi-harness/check-sync.sh
python3 -m py_compile gl-query-saturation.py

# 1. Keep the pre-saturation build as the baseline, then rebuild
# Initial run only, before rebuilding the driver with saturation:
docker exec hd620-mesa sh -eu -c 'test ! -e /build/iris-presat; cp -a /build/audit-iris /build/iris-presat'
docker exec hd620-mesa ninja -j1 -C /build/audit-iris src/gallium/targets/dri/libgallium-27.0.0-devel.so > evidence/iris-query-saturation/build.log 2>&1

# 2. MI harness (needs /build/mi-test from the conversion harness; refresh it, then add sat.cpp)
docker cp evidence/iris-timer-conversion/mi-harness/. hd620-mesa:/build/mi-test/
docker exec hd620-mesa mkdir -p /build/mi-sat
docker cp evidence/iris-query-saturation/mi-harness/. hd620-mesa:/build/mi-sat/
docker exec hd620-mesa sh -c 'cd /build/mi-sat && BASE=/build/mi-test sh build.sh && ./run-sat iris_query_saturation' > evidence/iris-query-saturation/mi-sat-hw.log 2>&1
docker exec hd620-mesa sh -c 'cd /build/mi-sat && ./run-sat' > evidence/iris-query-saturation/mi-sat-all-hw.log 2>&1
status=0
docker exec hd620-mesa sh -c 'cd /build/mi-sat && BASE=/build/mi-test sh build-neg.sh && ./run-sat-negative iris_query_saturation' > evidence/iris-query-saturation/mi-sat-negative.log 2>&1 || status=$?
test "$status" -eq 1

# 3. GL probes, serial, presat then new
./evidence/iris-query-saturation/run-serial.sh
```

The `hd620-mesa` container mounts the workspace `mesa/` tree read-only at `/src`. The serial runner checks the expected exit status, failure count, and mapped Gallium library for each GL run.
