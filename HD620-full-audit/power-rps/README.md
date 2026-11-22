# Item 40: RPS thresholds and RC6 power-gate hysteresis (2026-11-22)

HD 620 (KBL GT2), i915 on Linux 7.0, round-2 Mesa. GPU power is the RAPL `uncore` domain.

## RPS up/down thresholds

[`run-rps.sh`](run-rps.sh) runs a paced job, one 168-thread wave of an FMA chain, at 120, 60 and 30 fps.
Each case ran 20 s (10 s at 120 fps), two interleaved rounds ([`rps-matrix.txt`](rps-matrix.txt)):

| up/down % | GPU W at 120 / 60 / 30 fps | Light-frame GPU time (60 fps, p50) |
| --- | --- | --- |
| 30/15 | 0.66–0.76 / 0.35–0.39 / 0.18–0.30 | 2.8–3.0 ms |
| **50/30 (i915 default)** | 0.58–0.64 / 0.34–0.36 / 0.17–0.18 | 3.6 ms |
| 70/40 | 0.55–0.56 / 0.33 / 0.17 | 3.9 ms (+7%) |
| 85/60, 95/85 | 0.54–0.55 / 0.33 / 0.17 | 3.9 ms |

70/40 captures all the available saving: about −10% GPU power at 120 fps, −7% at 60 fps and −3% at
30 fps. Every frame still finished far inside its period. Sustained load is unchanged: 1.18 ms per
job at 967 MHz with either setting ([`heavy.txt`](heavy.txt)). Package power was dominated by
desktop noise and is not used.

## RC6 render/media power-gate idle hysteresis

i915 programs `GEN9_{RENDER,MEDIA}_PG_IDLE_HYSTERESIS` = 250 (×1.28 µs) on every Gen9 part. The
kernel comment ties that to Broxton's ~100 µs wake-up; Gen11+ uses 60. Written at runtime with
`intel_reg` ([`run-pg.sh`](run-pg.sh), [`pg-matrix.txt`](pg-matrix.txt)):

| Hysteresis | 30 fps render: GPU W / RC6 | 1080p30 VA-API decode: GPU W / RC6 |
| --- | --- | --- |
| **250 (0xfa, default)** | 0.191 / 72.7–73.4% | 0.130 / 72.2–72.5% |
| 60 (0x3c) | 0.181–0.183 / 74.9–75.4% | 0.123–0.126 / 74.4–74.5% |
| 37 (0x25) | 0.181–0.183 / 75.1–75.4% | 0.122 / 74.4–74.7% |
| 10 (0x0a) | 0.178–0.179 / 75.6–76.2% | 0.121–0.122 / 74.7–74.8% |

60 saves about 5% of GPU power with no change in job time. Rapid submission round-trips, however
([`pg-latency.txt`](pg-latency.txt)), get slower in the cross-context synchronised case: a draw plus
dispatch inside a timer query goes from 264–269 µs to 278–283 µs (+5–7%). The other cases stay
within about 1%. An earlier run, in [`pg-matrix-hexvalues-592-37-4096.txt`](pg-matrix-hexvalues-592-37-4096.txt),
passed decimal strings that `intel_reg` read as hex. Its 0x1000 (4096) case shows how much an overly
long hysteresis costs: 0.44–0.49 W, with RC6 at 6–10%.

## Result: opt-in profile

Both knobs trade latency for power, so neither is a kernel or Mesa default. An opt-in profile is
installed:

- `/usr/local/sbin/hd620-power-profile save|default|status` ([source](hd620-power-profile)). `save`
  sets 70/40 and 60.
- `hd620-power-profile.service` re-applies the chosen profile at boot and after resume, because i915
  reprograms the hysteresis on resume. The stored profile is `default`, so nothing changes until you
  run `sudo hd620-power-profile save`.
- `intel_reg` and `libigt` from IGT 2.x are copied to `/usr/local/lib/hd620/`.

Remove with `sudo systemctl disable --now hd620-power-profile`, then delete the files above and
`/etc/hd620-power-profile`.
