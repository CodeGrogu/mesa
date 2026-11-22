# Item 36: Vulkan performance counters on Gfx9 (2026-11-22)

Two commits on `hd620-round2`:

1. **`intel/perf: fix OA report accumulation on Gfx8 to Gfx11`, an upstream regression.** i915 reports OA
   data in `A32u40_A4u32_B8_C8` on Gfx8 to Gfx12. Upstream `0a51842f7ab` (May 2024) reordered the format
   checks so that Gfx8–11 fell into the Haswell `A45_B8_C8` branch, both in the accumulator and in its
   offsets. Every OA counter on Broadwell through Ice Lake was wrong, in Iris, ANV and hasvk alike. The
   raw MI_RPC reports were fine, which a temporary dump confirmed: the timestamp is in DW1, GPU ticks in
   DW3, and the A counters are live.
2. **`anv: offer pipeline statistics counters in VK_KHR_performance_query`.** The extension was only
   exposed when OA metric sets load, which needs OA access. Pipeline statistics registers need no OA
   stream, so they are now separate counters with their own pass, and the extension works under the
   default `dev.i915.perf_stream_paranoid=1`. The host security policy is unchanged.

## Probe ([perf-probe.c](perf-probe.c))

The probe brackets one compute dispatch of 32-invocation SIMD8 workgroups with a performance query, on
any named counters, and submits every pass.

| Access | Result |
| --- | --- |
| paranoid=1, before | `VK_KHR_performance_query` not exposed |
| paranoid=1, after | 20 counters, and "N compute shader invocations" is exact: 32, 5,376, 32,000 and 131,072 for 1–4,096 groups ([probe-paranoid1.txt](probe-paranoid1.txt)) |
| paranoid=0 (temporary), before the OA fix | "GPU Core Clocks" = the 12 MHz timestamp delta, "GPU Busy" 2,495%, "EU Active" 0, "CS Threads Dispatched" 0. The same on the installed driver ([probe-installed-paranoid0.txt](probe-installed-paranoid0.txt), [probe-paranoid0.txt](probe-paranoid0.txt)) |
| paranoid=0 (temporary), after | "CS Threads Dispatched" = exactly 4 per group (672, 4,000), "GPU Busy" 100%, "EU Active" 26–97% rising with load, "GPU Core Clocks" = time × average frequency ([probe-fixed-paranoid0.txt](probe-fixed-paranoid0.txt)) |

`perf_stream_paranoid` was set to 0 only for the duration of these runs and restored to 1 each time.

## CTS: `dEQP-VK.query_pool.performance_query.*` (12 cases, [perfq.txt](perfq.txt))

| Driver | paranoid=1 | paranoid=0 |
| --- | --- | --- |
| Installed | 0 pass, 12 not supported | 4 pass, 0 fail |
| New | **4 pass, 0 fail** | 4 pass, 0 fail |

The other 8 cases need `performanceCounterMultipleQueryPools`, which the hardware cannot provide (one OA
configuration at a time).
