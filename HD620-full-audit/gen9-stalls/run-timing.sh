#!/usr/bin/env bash
# Interleaved A/B timing of each Gen9 flush toggle (ANV_GEN9_STALL_EXP).
# Every round runs the control (mask 0) and the toggle for each scenario,
# swapping their order on alternate rounds. Output: timing.csv
set -euo pipefail
cd "$(dirname "$0")"
ICD=${ICD:-$HOME/.local/state/catchingup/round2/stall-exp/icd.json}
ROUNDS=${ROUNDS:-8}
unset INTEL_DEBUG ANV_DEBUG VK_ICD_FILENAMES VK_ADD_DRIVER_FILES
echo "round,scenario,mask,gpu_ns_per_iter,cpu_ns_per_iter" > timing.csv
for r in $(seq 1 "$ROUNDS"); do
    for pair in vf:1 push:2 vfe:4 load:8; do
        s=${pair%%:*} b=${pair#*:}
        if (( r % 2 )); then order="0 $b"; else order="$b 0"; fi
        for m in $order; do
            out=$(VK_DRIVER_FILES=$ICD ANV_GEN9_STALL_EXP=$m ./stall-bench "$s" tri.vert.spv tri.frag.spv tick.comp.spv)
            read -r _ gpu cpu px <<< "$out"
            [[ $px == pixels=ok ]] || { echo "pixel check failed: $out" >&2; exit 1; }
            echo "$r,$s,$m,$gpu,$cpu" >> timing.csv
        done
    done
done
python3 -I - <<'EOF'
import csv, statistics
rows = list(csv.DictReader(open("timing.csv")))
print(f"{'scenario':8} {'control GPU':>12} {'toggle GPU':>12} {'GPU change':>11} {'control CPU':>12} {'toggle CPU':>12} {'CPU change':>11}  paired GPU deltas (ns)")
for s in ["vf", "push", "vfe", "load"]:
    by = {}
    for r in rows:
        if r["scenario"] == s:
            by.setdefault(r["round"], {})[r["mask"] == "0"] = r
    ctl_g = [float(v[True]["gpu_ns_per_iter"]) for v in by.values()]
    tog_g = [float(v[False]["gpu_ns_per_iter"]) for v in by.values()]
    ctl_c = [float(v[True]["cpu_ns_per_iter"]) for v in by.values()]
    tog_c = [float(v[False]["cpu_ns_per_iter"]) for v in by.values()]
    d = sorted(t - c for t, c in zip(tog_g, ctl_g))
    mg, tg = statistics.median(ctl_g), statistics.median(tog_g)
    mc, tc = statistics.median(ctl_c), statistics.median(tog_c)
    print(f"{s:8} {mg:12.1f} {tg:12.1f} {100*(tg-mg)/mg:10.1f}% {mc:12.1f} {tc:12.1f} {100*(tc-mc)/mc:10.1f}%  "
          f"min {d[0]:.0f} median {statistics.median(d):.0f} max {d[-1]:.0f}")
EOF
