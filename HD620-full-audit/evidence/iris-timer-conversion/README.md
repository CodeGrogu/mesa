# Iris query-buffer timer conversion fix (HD 620, Gen9 KBL GT2)

Status: local candidate. It is not committed and has not been sent upstream. No system configuration was changed. It goes on top of the [timer ordering fix](../iris-timer-fix/README.md), which is unchanged. The ANV diff is also unchanged: `git diff -- src/intel/vulkan` still has sha256 `6ebf1574…eaf22df`.

## Defect

When a timer query is written to a `GL_QUERY_BUFFER`, Iris resolves it one of two ways (`iris_get_query_result_resource`):

- **CPU path.** If the snapshots have already landed, Iris writes `calculate_result_on_cpu()` as an immediate. That is `intel_device_info_timebase_scale()`, which equals `floor(ticks × 10^9 / f) mod 2^64` for every 64-bit value. `TIME_ELAPSED` first applies `iris_raw_timestamp_delta()`, which adds 2^36 when the counter has wrapped.
- **GPU path.** Otherwise Iris emits MI_MATH (`calculate_result_on_gpu()`), which did something different:
  - `TIMESTAMP` was `(ticks × floor(10^9 / f)) & (2^36 − 1)`. The scale was truncated (83 instead of 83.33 at 12 MHz) and the mask was applied *after* scaling, to nanoseconds.
  - `TIME_ELAPSED` was `(end − start) × 83`, with the same truncation and no wrap handling.

The ordering fix made the two paths mix more often. A timestamp pair resolved CPU-then-GPU into one buffer gave deltas of about −2.55e12 ns.

## Fix

The full diff is [iris-timer-conversion.diff](iris-timer-conversion.diff), sha256 `18f04776…`. It applies on top of `iris-timer-fix.diff`. [iris-timer-conversion-full.diff](iris-timer-conversion-full.diff) is both diffs combined. Applying HEAD + ordering diff + conversion diff reproduces the working tree byte-for-byte.

- **`mi_builder.h`: `mi_udiv64_imm(b, N, D, bits, &rem)`.** Exact 64-bit ÷ immediate, giving quotient and remainder. It is schoolbook long division in base 2^k, where k is the largest of 16, 8, 4, 2, 1 with `D·2^k ≤ 2^32`. Every partial dividend `rem·2^k + digit < D·2^k` therefore fits a 32-bit numerator, and the existing `mi_udiv32_imm()` divides it exactly.
- **`mi_builder.h`: `mi_umul_udiv64_imm(b, N, num, den)`.** Returns `floor(N·num/den) mod 2^64`, exact for every 64-bit N:
  - Reduce `num/den` by their gcd.
  - Split `N = q·den + r`, then compute `q·num + floor(r·num/den)`. Because `r·num < den·num < 2^63`, nothing overflows.
  - If `den` reduces to 1, it is a single multiply.
  - This is the same function as `intel_device_info_timebase_scale()`. The CPU version is exact too: its 32-bit split cannot overflow for `f < 2^31`, because `(rem << 32) + lo·10^9 < 2^63 + 2^62`.
- **`iris_query.c`, `calculate_result_on_gpu()`:**
  - `TIMESTAMP`: `mi_umul_udiv64_imm(start, 10^9, f)`, with no mask, as on the CPU.
  - `TIME_ELAPSED`: `delta = (end − start) + (ult(end, start) & 2^36)`, which matches `iris_raw_timestamp_delta()` mod 2^64 for all inputs, then the same scale.
  - Not changed: `TIMESTAMP_RAW` (still copies the raw tick), `TIMESTAMP_DISJOINT` (still the default `end − start`), all non-timer queries, the CPU path, availability (`index == −1`), and `mi_store_if` predication for `NO_WAIT`.
- **`mi_builder_test.cpp`:** new `udiv64_imm` and `umul_udiv64_imm_timebase_scale` tests.

The frequency comes from `devinfo->timestamp_frequency`, which is read from the kernel at run time. Nothing assumes 12 MHz.

### Frequencies covered

These are all the frequencies the drivers can report (`linux/drivers/gpu/drm/i915/gt/intel_gt_clock_utils.c`, and Xe `reference_clock`):

| Source | Frequencies | Reduced 10^9/f | Digit size |
| --- | --- | --- | --- |
| Gen8 | 12.5 MHz | 80/1 | single multiply |
| Gen9 big core (incl. KBL HD 620), 24 MHz crystal >> 0..3 | 24 / 12 / 6 / 3 MHz | 125/3, 250/3, 500/3, 1000/3 | 16-bit |
| Gen9 LP, 19.2 MHz >> 0..3 | 19.2 / 9.6 / 4.8 / 2.4 MHz | 625/12, 625/6, 625/3, 1250/3 | 16-bit |
| Gen11+ crystals >> 0..3 | 24, 19.2, 38.4, 25 MHz and their shifts | e.g. 625/24, 40/1, 80/1, 160/1, 320/1 | 16-bit or multiply |
| Divide logic (`TIMESTAMP_OVERRIDE`) | `n MHz + 10^6/(d+1)`, e.g. 12 333 333 Hz | 10^9/12333333 (gcd 1) | 8-bit |
| Out-of-contract extremes (tests only) | 1 Hz, 10^9 Hz, 2^31−1 Hz | | 1-bit at 2^31−1 |

The divisor must reduce to at most 2^31, which every 32-bit frequency below 2^31 Hz satisfies. A larger reduced divisor is a contract violation. It asserts in debug builds and cannot divide by zero in release builds (k stops at 1).

## Verification

### 1. MI sequence on the GPU against the CPU formula ([mi-harness/](mi-harness/))

gtest is not installed in the build container, so I did not add it. [gtest/gtest.h](mi-harness/gtest/gtest.h) is a 50-line stand-in. With it, the unmodified `src/intel/common/tests/mi_builder_test.cpp` from the read-only source tree builds for `GFX_VERx10=90` with assertions on and runs on the HD 620 ([run.cpp](mi-harness/run.cpp), [build.sh](mi-harness/build.sh)).

- **[mi-builder-hw.log](mi-harness/mi-builder-hw.log): 30/30 tests, 3,966 checks, 0 failures.** This covers all 26 existing mi_builder tests, the 2 new ones, the Iris `TIME_ELAPSED` expression copied verbatim, and the cost test.
  - `udiv64_imm`: 8 values × 11 divisors (1, 2, 3, 12, 24, 65521, 65536, 65537, 16777259, 2^31−1, 2^31). This covers every digit size and the boundaries 0, 1, 2^36−1, 2^36, 2^63, 2^64−2 and 2^64−1. Quotient and remainder are both exact.
  - `umul_udiv64_imm_timebase_scale`: the same 8 values × 17 frequencies from the table, compared with `intel_device_info_timebase_scale()`. The 64-bit store is exact. The 32-bit store equals the low 32 bits and does not touch the next dword.
  - `iris_time_elapsed_expression`: 8 (start, end) pairs × 6 frequencies, compared with `iris_raw_timestamp_delta()` + scale. Pairs include:
    - a 36-bit wrap of 8 ticks,
    - a wrap of one tick,
    - a wrap of almost 2^36,
    - equal values,
    - a 60 s interval,
    - an out-of-range `start = 2^48−1`, where the GPU also matches the CPU.
- **Negative control ([negative-control.log](mi-harness/negative-control.log)).** The same harness built with the old `TIME_ELAPSED` formula fails 60 of 212 checks, for example 415 vs 416 ns for 5 ticks at 12 MHz. The harness does catch errors.
- **Register use ([gpr/](mi-harness/gpr/)).** Peak use is 7 GPRs, for both `TIMESTAMP` and `TIME_ELAPSED` at every listed frequency. A 6-GPR pool trips `mi_new_gpr`'s assert ([out6.log](mi-harness/gpr/out6.log)). 7 passes, and so does Iris's real pool of 15. `b.gprs == 0` after the stores, so nothing leaks.

### 2. GL on hardware, serial runs, pinned builds

Logs are in this directory, written by [run-serial.sh](run-serial.sh). Each log starts with the sha256 of the mapped `libgallium`. `libEGL` (`8c78c881…`) is identical in all three builds.

| Build | libgallium | Location |
| --- | --- | --- |
| original (pre-ordering) | `e2f1e508…` | `/build/iris-timer-baseline` |
| order-only | `d3d98884…` | `/build/iris-timer-order-only` (copied from `/build/audit-iris` before rebuilding) |
| new (ordering + conversion) | `58d2d687…` | `/build/audit-iris` |

[gl-timer-conversion.py](../../gl-timer-conversion.py) forces each resolve path and compares the buffer with `glGetQueryObject*` client results. It forces the GPU path by resolving right after an 11 ms draw or dispatch, and the CPU path by calling `glFinish` before resolving. Each case runs 9 times. In every run it checks:

- the 64-bit buffer value,
- the 32-bit buffer value (`GL_UNSIGNED_INT`),
- `GL_QUERY_RESULT_NO_WAIT` (predicated store),
- `GL_QUERY_RESULT_AVAILABLE`.

| Probe | original | order-only | new |
| --- | --- | --- | --- |
| gl-timer-conversion: failed checks | 14 | 16 | **0** |
| — `TIMESTAMP`, GPU path, buffer − client | −2 552 847 465 959 ns | −2 552 855 272 933 ns | **0, 9/9 exact** |
| — `TIME_ELAPSED` (~11 ms draw), GPU path, buffer − client | −43 946 ns | −43 564 ns | **0, 9/9 exact** |
| — CPU-then-GPU timestamp pair, buffer delta | −2.55e12 ns | −2.55e12 ns | **= client delta (~11.4 ms)** |
| — CPU path, AVAILABLE | pass | pass | pass |
| gl-timer-ordering: timing checks failed | 7 of 17 | 0 | **0** |
| — `KNOWN-DEFECT` query-buffer line | 0/9 equal | 0/9 equal | **PASS 9/9** |

Notes:

- Original passes the "time elapsed of compute" equality only because, without the ordering fix, the interval is 1 tick (83 ns) and both formulas give 83. It still fails the separate nonzero check.
- The `TIMESTAMP` error grows with the counter value. At about 30e9 ticks: a 36-bit mask on nanoseconds, plus 0.4 % truncation.
- [new-gl-compute-timing.log](new-gl-compute-timing.log), the original probe: timestamp pair 11.29 ms, `TIME_ELAPSED` 11.29 ms, explicit drains 11.50 ms, outputs pass. Unchanged from the ordering candidate.

### 3. Real 36-bit rollover

See [new-wrap-watch.log](new-wrap-watch.log) and [run-wrap-watch.sh](run-wrap-watch.sh). At 12 MHz the HD 620 counter wraps every 2^36 / 12e6 = 5726.6 s. `DRM_IOCTL_I915_REG_READ` of `TIMESTAMP` showed no bits above bit 35 ([rawts.c](mi-harness/rawts.c)). The watch loops GPU-path `TIME_ELAPSED` and `TIMESTAMP` resolves on the new build for 240 s around the rollover.

Result:

- **The rollover happened inside the window.** At 90 s before it, the raw counter read `0xfbfa05add`. Afterwards it read `0x6ba4d86f`. The last client `GL_TIMESTAMP` was 150.4 s, which is post-wrap: 240 − 90 = 150 s.
- **Every sample matched.** All 73 376 GPU-path samples, before and after the rollover, had buffer `TIMESTAMP` and `TIME_ELAPSED` exactly equal to the client values, with 0 mismatches. That includes the small post-wrap timestamps.
- **No interval straddled the wrap.** The probe found no sample with `t1 < t0`: the rollover fell in the gap between samples, during the resolve and `glFinish`. So the wrap branch (`end < start`) was not exercised by a real hardware interval. It is covered only by the on-GPU harness with synthetic snapshots (section 1), where the CPU formula and the Iris MI expression agree for wraps of 1 tick, 8 ticks and almost 2^36. The next real rollover is 5 726 s later. Catching one would need a probe whose intervals tile the timeline with no gaps; that was not attempted.

## Cost

Measured on the HD 620 command streamer at 12 MHz (`zz_cost` in [mi-builder-hw.log](mi-harness/mi-builder-hw.log)). Each value is the median of 21 batches. "Dwords" is the MI_MATH and register-copy batch space for one conversion.

| f | Query | Old dwords / CS time | New dwords / CS time |
| --- | --- | ---: | ---: |
| 12 MHz (HD 620) | `TIMESTAMP` | 63 / 0.67 µs | 2 436 / 4.67 µs |
| 12 MHz (HD 620) | `TIME_ELAPSED` | 65 / 0.92 µs | 2 484 / 5.67 µs |
| 19.2 / 38.4 MHz | either | 51–57 / 0.7–0.9 µs | 2 444–2 492 / 4.5–5.5 µs |
| 12.5 MHz (den = 1) | `TIMESTAMP` / `TIME_ELAPSED` | 55 / 57 | 45 / 92 (0.6 / 1.6 µs) |
| 12 333 333 Hz (divide logic) | either | ~60 | ~7 950 / 14–15 µs |
| 2^31−1 Hz (out of contract) | either | — | ~63 200 / ~130 µs |

- **Where the cost lands.** Only GPU-path resolves pay it: a query written to a buffer before its snapshot has landed. The CPU path, `glGetQueryObject*` into client memory, Rusticl's `TIMESTAMP_RAW`, and query begin/end cost exactly what they did before.
- **Wall time.** In GL, the per-iteration time of "small draw + timer + immediate buffer resolve" did not change measurably (new build vs order-only, median of 5 × 200):
  - `TIMESTAMP`: 194.8 vs 193.2 µs
  - `TIME_ELAPSED`: 195.2 vs 191.4 µs
  - no query: 183.5 µs
- **Why 64-bit Gen9 needs ~2.4 k dwords.** Gen9's ALU has no shifts, so `mi_ishl_imm` is repeated adds, and so is the right shift inside `mi_udiv32_imm`. The ~2.4 k dwords are about 600 four-dword ALU ops over four 16-bit digits. By my count of the emitted sequence, most of each digit is the `0xAAAAAAAB` magic multiply in `mi_udiv32_imm` and its post-shift. A special case for `D < 2^8`, a single `ceil(2^32/D)` multiply, would save roughly a quarter (estimated, not built). It was not done. `mi_udiv32_imm` is correct and already tested, and the measured wall cost is in the noise.
- **Batch growth.** An application that resolves *n* timer queries to buffers on the GPU path per frame adds about *n* × 9.7 KB of batch and *n* × 5 µs of CS time. Iris chains batches as needed.

## Edge cases and limits

- **32-bit results (`GL_UNSIGNED_INT` buffers).** Unchanged and pre-existing. Both Iris buffer paths store the low 32 bits (CPU: `store_data_imm32`; GPU: `mi_mem32`). The Mesa frontend clamps client-memory `glGetQueryObjectuiv` to `0xffffffff` (`get_query_object` in `queryobj.c`). So for `TIMESTAMP` (about 2.5e12 ns), the 32-bit buffer holds the truncated low word while the client value is `0xffffffff`. The two Iris paths now agree with each other, but neither saturates. Fixing that means changing every query type, so it is out of scope here.
- **`TIMESTAMP_DISJOINT`.** Unchanged. Its GPU path still computes `end − start`, while the CPU path scales `start`. Nothing in this tree's GL frontend creates it (grep of `src/mesa`), so it cannot be exercised from GL.
- **`TIMESTAMP_RAW` / Rusticl.** Unchanged on both paths, which return the raw tick. Rusticl still cannot be built here (see the ordering README).
- **Values above 36 bits.** The GPU now handles all 64 bits exactly as the CPU does, including the `TIME_ELAPSED` wrap rule for out-of-range inputs, so the two paths cannot diverge even if a snapshot carried upper bits. On the HD 620 the upper bits read as zero.
- **Other generations.** Every Iris generation (gfx8–gfx35) compiles cleanly ([mi-harness/final-build.log](mi-harness/final-build.log): 0 warnings). Gen12.5+ has real shifts, so the sequence is shorter there. Only Gen9 hardware was tested.
- **Not run.** Piglit `arb_query_buffer_object` / `arb_timer_query`, the GL CTS, and Xe KMD.

## Commands

From `HD620-full-audit/`:

```sh
docker exec hd620-mesa sh -c 'cd /build/audit-iris && ninja -j4'
docker exec hd620-mesa sh -c 'cd /build/mi-test && sh build.sh && ./run'                # MI harness (sources in mi-harness/)
docker exec hd620-mesa sh -c 'cd /build/mi-test && sh build-neg.sh && ./run-negative iris_time_elapsed'
./evidence/iris-timer-conversion/run-serial.sh                                          # GL probes, 3 builds, serial
./evidence/iris-timer-conversion/run-wrap-watch.sh                                      # waits for the next rollover
```
