#!/bin/sh
# Dependent-chain latency per op in GPU cycles.
# GPU timestamps (median of 41 runs, one thread) with the clock pinned at
# 1000 MHz, so 1 ns = 1 cycle: (time of the 48-op body - cycles of the 16-op body) / (32 * ITER).
# Pin the clock first: echo 1000 > /sys/class/drm/card1/gt_min_freq_mhz
# usage: run-lat.sh ITER > lat.txt
ITER=${1:-400}
echo "op simd cyc16 cyc48 cycles_per_op"
for simd in 8 16; do
  for op in fadd fmul fmad dadd dmul dmad iadd imul rcp sqrt rsq exp2 pow idiv; do
    c16=$(./lat-bench lat$simd/$op-16.spv 1 $ITER 41 $simd | cut -d" " -f1)
    c48=$(./lat-bench lat$simd/$op-48.spv 1 $ITER 41 $simd | cut -d" " -f1)
    awk -v o=$op -v s=$simd -v a=$c16 -v b=$c48 -v n=$ITER \
      'BEGIN { printf "%s %s %d %d %.2f\n", o, s, a, b, (b - a) / (32 * n) }'
  done
done
