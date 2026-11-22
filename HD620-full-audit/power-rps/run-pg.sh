#!/bin/sh
# RC6 render/media power-gate idle hysteresis experiment (item 40).
# usage: run-pg.sh HYST WORKLOAD (HYST in hex, e.g. 0xfa; intel_reg parses hex)   (render30 | decode)   needs $P and $CLIP
HYST=$1; WL=$2
R=$(dirname "$0")/../tools/root; G=/sys/class/drm/card1/gt/gt0; B=$(dirname "$0")/../compiler-gfx9
reg() { sudo -S -p '' env LD_LIBRARY_PATH=$R/usr/lib/x86_64-linux-gnu $R/usr/bin/intel_reg "$@" < "$P"; }
reg write 0xa0c4 $HYST >/dev/null; reg write 0xa0c8 $HYST >/dev/null
rd() { sudo -S -p '' cat /sys/class/powercap/intel-rapl:0:1/energy_uj < "$P"; }
sleep 3
u0=$(rd); rc0=$(cat $G/rc6_residency_ms); t0=$(date +%s%N)
case $WL in
  render30) out=$(cd $B && PACE_US=33333 ./lat-bench lat8/fmad-48.spv 168 2000 600 8);;
  decode) out=$(ffmpeg -loglevel error -re -hwaccel vaapi -hwaccel_device /dev/dri/renderD128 -i "$CLIP" -t 20 -f null - 2>&1; echo "rc=$?");;
esac
u1=$(rd); rc1=$(cat $G/rc6_residency_ms); t1=$(date +%s%N)
now=$(reg read 0xa0c4 | awk '{print $2}')
awk -v h=$HYST -v w=$WL -v un=$((u1-u0)) -v rc=$((rc1-rc0)) -v dt=$(( (t1-t0)/1000000 )) -v o="$out" -v n=$now \
  'BEGIN { printf "hyst=%s (reads %s) %s gpu_W=%.3f rc6_pct=%.1f out=%s\n", h, n, w, un/dt/1000, 100*rc/dt, o }'
