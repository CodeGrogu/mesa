#!/usr/bin/env bash
# Vulkan HEVC decode on HD 620 with HuC loaded (i915.enable_guc=2):
# compare every frame against software decode of the same stream.
set -uo pipefail
cd "$(dirname "$0")/.."
out=hevc-huc
icd=${VK_DRIVER_FILES:-$HOME/.local/mesa-round2/share/vulkan/icd.d/intel_icd.x86_64.json}
unset VK_ICD_FILENAMES VK_ADD_DRIVER_FILES
fail=0
for spec in evidence/media/hevc_vaapi-nv12.hevc:nv12 evidence/media/hevc_vaapi-p010le.hevc:p010le \
            evidence/hevc-regression/inputs/main.hevc:nv12 evidence/hevc-regression/inputs/main10.hevc:p010le; do
    clip=${spec%%:*} pix=${spec#*:} base=$(basename "$clip" .hevc)
    VK_DRIVER_FILES=$icd VK_LOADER_DEBUG=driver timeout 60 ffmpeg -y -hide_banner -nostdin -xerror \
        -loglevel debug -hwaccel vulkan -hwaccel_output_format vulkan -i "$clip" -map 0:v:0 \
        -vf "hwdownload,format=$pix" -c:v rawvideo -f framemd5 "$out/$base-vulkan.md5" > "$out/$base-vulkan.log" 2>&1
    rc=$?
    timeout 60 ffmpeg -y -hide_banner -nostdin -xerror -loglevel error -i "$clip" -map 0:v:0 \
        -vf "format=$pix" -c:v rawvideo -f framemd5 "$out/$base-software.md5" > "$out/$base-software.log" 2>&1
    hw=$(grep -c 'Format vulkan chosen by get_format' "$out/$base-vulkan.log")
    n=$(grep -cv '^#' "$out/$base-software.md5")
    m=$(diff <(awk -F ', ' '!/^#/ {print $NF}' "$out/$base-software.md5") \
             <(awk -F ', ' '!/^#/ {print $NF}' "$out/$base-vulkan.md5" 2>/dev/null) | grep -c '^>')
    if [[ $rc -eq 0 && $hw -ge 1 && $m -eq 0 && $(grep -cv '^#' "$out/$base-vulkan.md5") -eq $n ]]; then
        printf '%-22s PASS  %d/%d Vulkan frames match software\n' "$base" "$n" "$n"
    else
        printf '%-22s FAIL  rc=%d vulkan-format=%d mismatched=%d of %d\n' "$base" "$rc" "$hw" "$m" "$n"
        fail=1
    fi
done
exit $fail
