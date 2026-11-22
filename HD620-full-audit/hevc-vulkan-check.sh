#!/usr/bin/env bash
# Compare Vulkan HEVC decode with software using the same HEVC files.
set -euo pipefail
cd "$(dirname "$0")"
out=evidence/hardware-paths
mkdir -p "$out"
unset VK_ICD_FILENAMES VK_ADD_DRIVER_FILES
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json ANV_DEBUG=video-decode VK_LOADER_DEBUG=driver
for pix in nv12 p010le; do
    base="hevc_vaapi-$pix"
    clip="evidence/media/$base.hevc"
    timeout 30 ffmpeg -y -hide_banner -nostdin -xerror -loglevel debug -threads 2 -filter_threads 1 \
        -hwaccel vulkan -hwaccel_output_format vulkan -i "$clip" -map 0:v:0 \
        -vf "hwdownload,format=$pix" -c:v rawvideo -f framemd5 "$out/$base-vulkan.md5" \
        > "$out/$base-vulkan.log" 2>&1
    timeout 30 ffmpeg -y -hide_banner -nostdin -xerror -loglevel error -threads 2 -filter_threads 1 \
        -i "$clip" -map 0:v:0 -vf "format=$pix" -c:v rawvideo -f framemd5 "$out/$base-software.md5" \
        > "$out/$base-software.log" 2>&1
    grep -q 'Format vulkan chosen by get_format' "$out/$base-vulkan.log"
    grep -q 'Using .*HD Graphics 620.*libvulkan_intel.so' "$out/$base-vulkan.log"
    diff -u <(awk -F ', ' '!/^#/ {print $NF}' "$out/$base-software.md5") <(awk -F ', ' '!/^#/ {print $NF}' "$out/$base-vulkan.md5")
    count=$(grep -cv '^#' "$out/$base-vulkan.md5")
    test "$count" -eq 60
    printf '%s: %s Vulkan-decoded frames matched software hashes\n' "$base" "$count"
done
