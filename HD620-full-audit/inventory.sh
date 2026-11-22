#!/usr/bin/env bash
# Capture installed runtime and read-only device state.
set -u
cd "$(dirname "$0")"
out="$PWD/evidence"
local_tools="$PWD/tools/root/usr/bin"
mkdir -p "$out"
{
  date --iso-8601=seconds
  uname -r
  lspci -nnk -s 00:02.0
  printf 'Session %s\n' "${XDG_SESSION_TYPE:-none}"
  git -C ../linux rev-parse HEAD
  git -C ../mesa rev-parse HEAD
  dpkg-query -W mesa-vulkan-drivers libgl1-mesa-dri libegl-mesa0 libdrm2 libva2 \
    intel-media-va-driver-non-free i965-va-driver ocl-icd-libopencl1 ffmpeg
  cat /proc/sys/kernel/perf_event_paranoid
  for node in /sys/class/drm/card1-*/{status,enabled,modes} /sys/class/drm/card1/gt/gt0/{rc6_enable,rc6_residency_ms,rps_*freq_mhz} \
      /sys/class/drm/card1/device/power/{runtime_status,runtime_active_time,runtime_suspended_time,control}; do
    printf '\n%s\n' "$node"
    cat "$node"
  done
  ls /etc/OpenCL/vendors /usr/share/vulkan/icd.d
  ls /sys/bus/event_source/devices/i915/events
} > "$out/host.txt" 2>&1
for driver in iHD i965; do
  LIBVA_DRIVER_NAME="$driver" vainfo --display drm --device /dev/dri/renderD128 > "$out/va-$driver.txt" 2>&1
done
if [[ -x "$local_tools/eglinfo.x86_64-linux-gnu" ]]; then
  "$local_tools/eglinfo.x86_64-linux-gnu" -B > "$out/egl.txt" 2>&1
fi
if [[ -x "$local_tools/vulkaninfo" ]]; then
  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json \
    "$local_tools/vulkaninfo" > "$out/vulkan-installed.txt" 2>&1
fi
if [[ -x "$local_tools/clinfo" ]]; then
  "$local_tools/clinfo" > "$out/opencl-installed.txt" 2>&1
fi
