#!/bin/sh
# RPS threshold experiment (item 40).  Runs a paced GPU job (one wave of a
# dependent FMA chain, about 2.5 ms at 1 GHz) at a given period for N frames
# and reports GPU job time percentiles, uncore (GPU) and package energy from
# RAPL, mean actual frequency and RC6 residency.
# usage: run-rps.sh PERIOD_US FRAMES UP_PCT DOWN_PCT   (needs $P = sudo password file)
PERIOD=$1; FRAMES=$2; UP=$3; DOWN=$4
G=/sys/class/drm/card1/gt/gt0
B=$(dirname "$0")/../compiler-gfx9
sudo -S -p '' sh -c "echo $UP > $G/rps_up_threshold_pct; echo $DOWN > $G/rps_down_threshold_pct" < "$P"
rd() { sudo -S -p '' cat /sys/class/powercap/intel-rapl:0/energy_uj /sys/class/powercap/intel-rapl:0:1/energy_uj < "$P" | tr '\n' ' '; }
sleep 3   # settle to idle
set -- $(rd); pkg0=$1; unc0=$2; rc0=$(cat $G/rc6_residency_ms); t0=$(date +%s%N)
( while :; do cat $G/rps_act_freq_mhz; sleep 0.05; done ) > freq.tmp & sp=$!
res=$(cd $B && PACE_US=$PERIOD ./lat-bench lat8/fmad-48.spv 168 2000 $FRAMES 8)
kill $sp; wait $sp 2>/dev/null
set -- $(rd); pkg1=$1; unc1=$2; rc1=$(cat $G/rc6_residency_ms); t1=$(date +%s%N)
mhz=$(awk '{s+=$1} END {printf "%.0f", s/NR}' freq.tmp); rm -f freq.tmp
echo "$res" | awk -v up=$UP -v dn=$DOWN -v per=$PERIOD -v pk=$((pkg1-pkg0)) -v un=$((unc1-unc0)) \
  -v rc=$((rc1-rc0)) -v dt=$(( (t1-t0)/1000000 )) -v mhz=$mhz '{
  sub("p90=","",$4); sub("p99=","",$5);
  printf "up=%d down=%d period_us=%d job_p50_us=%.0f p90=%.0f p99=%.0f gpu_W=%.3f pkg_W=%.3f mean_MHz=%s rc6_pct=%.1f\n",
    up, dn, per, $1/1000, $4/1000, $5/1000, un/dt/1000, pk/dt/1000, mhz, 100*rc/dt }'
