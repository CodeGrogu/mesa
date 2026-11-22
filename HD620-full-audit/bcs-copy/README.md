# BCS copy fixture: correctness, latency and throughput on the HD 620

This fixture is the first step of the [BCS row](../IMPLEMENTATION.md). It compares verified linear buffer copies on three routes: the Gen9 blitter (BCS) through raw i915, the render engine through ANV's existing general queue, and a CPU `memcpy` baseline. **It has been built and self-tested on the host only. No GPU run has happened yet.** The parent scheduler owns the GPU runs.

| File | Role |
| --- | --- |
| [i915-bcs-copy.c](i915-bcs-copy.c) | BCS route plus CPU baseline. Opens a private render-node fd and creates a context, GEM objects and batch for this run only. |
| [vulkan-render-copy.c](vulkan-render-copy.c) | Render route: `vkCmdCopyBuffer` on the existing general queue family. It creates no new queue family and changes nothing in ANV. |
| [bcs-common.h](bcs-common.h) | Shared code for deterministic patterns, canaries and guards, full verification, statistics, fdinfo engine time and the read-only GT frequency. |
| [build.sh](build.sh) | Compiles both fixtures in the existing `hd620-mesa` container. Source goes in and binaries come out over stdin/stdout. Output is bit-reproducible. |
| [run-serial.sh](run-serial.sh) | GPU run sequence. **Only the parent scheduler runs this, serially and with the GPU otherwise idle.** |
| [summarize.py](summarize.py) | Turns the JSONL results into a Markdown table. It only reads files. |

## Safety envelope

The GPU fixtures stay inside these limits:

- **Device access:** they open only `/dev/dri/renderD*`. The path is resolved with `realpath` and must be a character device with minor ≥ 128. They refuse anything that is not driver `i915` with `I915_PARAM_CHIPSET_ID == 0x5916`. The Vulkan fixture refuses anything that is not `8086:5916`.
- **No IGT or privileged paths:** they do not run stock IGT. They use no debugfs, card or master node, reset, sysfs write, root or module parameter. The only sysfs read is `gt_act_freq_mhz`.
- **Private context:** the BCS fixture creates its own context through `CONTEXT_CREATE_EXT` and sets `I915_CONTEXT_PARAM_RECOVERABLE=0`, so a hang bans this context instead of replaying it. Each GEM object gets a softpinned address from a bump allocator, so addresses are never reused.
- **Bounded waits:** `GEM_WAIT` and `vkWaitForFences` default to 2 s, with a maximum of 10 s. On a timeout the fixture exits with code 4, and closing the fd releases the context. `SET_DOMAIN` is called only on objects that have already been waited idle.
- **Input limits:** sizes must be 4 KiB multiples up to 256 MiB, iterations at most 10 000, queue and batch depth at most 256, the payload working set at most 512 MiB per buffer, and a BCS batch at most 256 KiB, the size the Gen9 parser expects.

## Packets: provenance

All packet values come from the checked-out kernel, not from IGT.

| Packet | Source | Encoding used |
| --- | --- | --- |
| `XY_SRC_COPY_BLT` (default) | [intel_gpu_commands.h](../../linux/drivers/gpu/drm/i915/gt/intel_gpu_commands.h#L270), [intel_migrate.c `emit_copy`](../../linux/drivers/gpu/drm/i915/gt/intel_migrate.c#L604) | `2<<29 \| 0x53<<22 \| WRITE_RGBA \| 8`, `DEPTH_32 \| ROP 0xCC \| pitch 4096`, x2 = 1024 px, y2 = rows, 64-bit dst/src |
| `XY_FAST_COPY_BLT` (`--packet fast`) | same header L269, [client_blt selftest](../../linux/drivers/gpu/drm/i915/gem/selftests/i915_gem_client_blt.c#L192), and Xe's `XY_FAST_COPY_BLT_DEPTH_32 = 3<<24` | `2<<29 \| 0x42<<22 \| 8` (linear/linear), `DEPTH_32 \| pitch 4096`; no `BLIT_CCTL` (Gen12 only) |
| `MI_STORE_REGISTER_MEM` of BCS `RING_TIMESTAMP` (0x22358) | whitelisted in [`gen9_blt_regs`](../../linux/drivers/gpu/drm/i915/i915_cmd_parser.c#L684) | before the blits, and after the flush |
| `MI_FLUSH_DW` (gen8 length 4), `MI_BATCH_BUFFER_END` | [`gen9_blt_cmds`](../../linux/drivers/gpu/drm/i915/i915_cmd_parser.c#L474) | waits for the blits before the second timestamp |

Gen9 BCS requires the i915 command parser (`I915_ENGINE_REQUIRES_CMD_PARSER`). Every execbuf is therefore scanned and copied to a shadow batch. That CPU cost appears in `submit_ioctl_ns` and `host_ns`. It is a real cost that any userspace BCS route on this GPU would also pay. Blits are split at 2048 rows (8 MiB), matching the kernel's `CHUNK_SZ`.

`--selftest` builds every batch shape for both packets: 7 sizes covering chunk boundaries and remainders, with 1 and 3 slots. A host interpreter then executes each batch the way the parser and blitter decode it. The interpreter checks length fields, opcodes, pitch, depth, ROP, register and that `BB_END` falls within `batch_len`. The test then runs full verification. As a negative control, it flips a byte in a guard and a byte in a payload and requires exactly those two words to be detected. The interpreter is written from the same definitions as the encoder, so it proves internal consistency and verifier sensitivity. Hardware semantics still depend on the GPU run.

## Correctness method, same for every route

- **Layout:** each buffer is laid out as `[guard 4K][slot0][guard][slot1]…[guard]`.
- **Fresh data every iteration:** before each iteration, every source slot gets new data from splitmix64. The seed depends on the route, size, iteration and slot, so a stale copy or a copy into the wrong slot cannot verify. Every guard and the whole destination are filled with canaries indexed by position, which catches shifted writes.
- **Full check:** after the bounded wait, every byte of both buffers is checked: payload, guards and an unchanged source. Mismatches are written as `{"record":"mismatch"…}` with the first offset and the values found and expected.
- **Engine check:** fdinfo deltas must show the expected engine only. A BCS phase needs `drm-engine-copy > 0` and `drm-engine-render == 0`. The Vulkan run needs render > 0 and copy == 0. Any other result counts as a failure.
- **Gate:** `--verify` runs a fixed matrix: 4K, 8K, 12K, 1M, 8M, 8M+4K and 24M+12K, in latency, queued ×4 and batch ×4 modes, for both packets, with 3 iterations each. `run-serial.sh` stops before timing if this gate fails.

## Timing method

The fixtures report three GPU modes and a CPU baseline:

| Mode | One sample is… | Answers |
| --- | --- | --- |
| `latency` | 1 copy, 1 submission, wait | submit→complete latency of one copy |
| `queued` | K copies as K back-to-back submissions, one wait | submission pipelining and per-submit overhead |
| `batch` | N copies in 1 batch or command buffer, one wait | in-batch (amortised) throughput |
| `cpu` | one `memcpy` between the same WB-mapped GEM objects | CPU baseline, single thread |

K and N default to 16. They are capped so that each buffer's payload stays within `--max-working-set`, which defaults to 64 MiB. The actual counts are reported as `copies_per_sample`. The working set is reported in bytes because small working sets fit in the LLC.

Each record reports:

- **`host_ns`:** CLOCK_MONOTONIC_RAW from the first submission until the wait returns.
- **`submit_ioctl_ns`/`submit_call_ns`:** time spent inside execbuf or `vkQueueSubmit`.
- **`gpu_span_ns`:** GPU timestamps from the first program's start to the last program's end. On BCS these come from `RING_TIMESTAMP` at `I915_PARAM_CS_TIMESTAMP_FREQUENCY`. On Vulkan they come from `vkCmdWriteTimestamp × timestampPeriod`.
- **`gpu_program_sum_ns`:** the sum of the per-program spans.
- **fdinfo:** engine time for the measured phase. The BCS fixture reads its own fd. The Vulkan fixture sums every renderD fd in the process, de-duplicated by `drm-client-id`. Each record also gives the ratio of engine time to host time.
- **GT frequency:** `gt_act_freq_mhz` before and after the phase.
- **`verified_bytes`, `bad_words`.**

Statistics are n, min, nearest-rank p50 and p95, max and mean. Times are in ns. Bandwidth is GB/s = 10⁹ bytes/s and is computed per sample from p50, plus an aggregate over all samples. Warm-up iterations, 5 by default, run and are verified but are not recorded. fdinfo is read only after warm-up. The run sequence is fixed and is recorded in the header: for each size, ascending, run cpu → latency → queued → batch. `run-serial.sh` also repeats the run once with the sizes in reverse order to expose drift from warm-up or frequency.

## Commands

Compiling and the self-test touch no GPU device:

```sh
cd HD620-full-audit/bcs-copy
./build.sh                         # container compile, -Werror, prints sha256s
./bin/i915-bcs-copy --selftest     # host-only packet interpreter + verifier negatives
```

These are GPU runs, for the parent scheduler only. They run serially with nothing else on the GPU:

```sh
./run-serial.sh                    # verify gates, then timing; writes ../evidence/bcs-copy/<UTC>/
# or individually:
./bin/i915-bcs-copy --verify
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json ./bin/vulkan-render-copy --verify
./bin/i915-bcs-copy --sizes 4K,64K,1M,8M,64M --iters 50 --warmup 5 --modes cpu,latency,queued,batch
VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json ./bin/vulkan-render-copy --sizes 4K,64K,1M,8M,64M
python3 -I summarize.py ../evidence/bcs-copy/<UTC>/*.jsonl
```

`SIZES`, `ITERS`, `WARMUP` and `VK_ICD` override the defaults in `run-serial.sh`. The exit codes are 0 for ok, 1 for usage, 2 for the wrong device or a missing capability, 3 for a mismatch or wrong-engine accounting, and 4 for a GPU error or timeout.

## Scope and what remains

- **Render route:** the render comparison uses the installed ANV through the public API, with no new queue. A GL variant, Iris `glCopyBufferSubData` through blorp on RCS, would add only a second render implementation. It is planned, not implemented. If needed, it would follow the EGL/Python pattern of the existing `gl-timer-*.py` probes, with the same seeds and fdinfo method.
- **Not yet covered:** these items in the BCS row are not covered yet: BCS copies under concurrent render load, contention between the two engines, cross-engine cache visibility (BCS write → render read and the reverse), and ownership and semaphore tests. They are the next fixture, built on the verified single-engine baseline produced here. Results here do not justify a public transfer-only queue.
- **Tiled copies:** only linear, page-aligned, 4096-pitch buffer copies are exercised. Tiled surfaces and `BCS_SWCTRL` are deliberately out of scope.
