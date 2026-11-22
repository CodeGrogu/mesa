#!/usr/bin/env bash
# Observe RC6 residency and RPS under the existing desktop and a known render load.
set -eu
cd "$(dirname "$0")"
gt=/sys/class/drm/card1/gt/gt0
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json
libs="$PWD/tools/root/usr/lib/x86_64-linux-gnu"
export LD_LIBRARY_PATH="$libs"
vk="$PWD/tools/root/usr/bin/vkmark"
args=(--winsys headless --winsys-dir "$libs/vkmark" --data-dir "$PWD/tools/root/usr/share/vkmark")
sample() {
    local kind=$1 start end now
    start=$(cat "$gt/rc6_residency_ms")
    printf '%s wall_ms=%s rc6_ms=%s\n' "$kind" "$(date +%s%3N)" "$start"
    for ((i=0; i<12; i++)); do
        sleep .25
        printf '   actual frequency MHz %s\n' "$(cat "$gt/rps_act_freq_mhz")"
    done
    end=$(cat "$gt/rc6_residency_ms")
    now=$(date +%s%3N)
    printf '%s wall_ms=%s rc6_ms=%s delta=%s\n' "$kind" "$now" "$end" "$((end-start))"
}
sample desktop
"$vk" "${args[@]}" -b effect2d:kernel=blur:duration=5 > evidence/rc6-load-vkmark.txt 2>&1 &
load=$!
trap 'kill "$load" 2>/dev/null || true' EXIT
sleep .8
sample render
wait "$load"
trap - EXIT
sample desktop-recovery
