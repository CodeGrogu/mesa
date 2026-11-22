#!/usr/bin/env bash
# Test VA processing and direct VA-to-Vulkan mapping with no permanent config.
set -u
cd "$(dirname "$0")"
mkdir -p evidence/media
out="$PWD/evidence/media"
icd=$(python3 -c 'import json; print(json.load(open("../HD620-benchmark/provenance.json"))["patched"]["manifest"])')
export LIBVA_DRIVER_NAME=iHD VK_DRIVER_FILES="$icd" VK_LOADER_DEBUG=driver
unset VK_ICD_FILENAMES VK_ADD_DRIVER_FILES
common=(-hide_banner -nostdin -y -xerror -threads 2 -filter_threads 1 -v verbose \
    -hwaccel vaapi -hwaccel_device /dev/dri/renderD128 -hwaccel_output_format vaapi)
for spec in h264_vaapi-nv12.h264:nv12 hevc_vaapi-p010le.hevc:p010le; do
    IFS=: read -r stream format <<< "$spec"
    if ffmpeg "${common[@]}" -i "$out/$stream" \
        -vf "scale_vaapi=w=320:h=180:format=$format,hwdownload,format=$format" \
        -frames:v 60 -c:v wrapped_avframe -f null - > "$out/$stream-vpp.log" 2>&1; then
        printf '%s VA scale 320x180 passed\n' "$stream"
    else
        printf '%s VA scale failed\n' "$stream"
    fi
    if ffmpeg "${common[@]}" -i "$out/$stream" \
        -vf 'hwmap=derive_device=vulkan:mode=read+direct' -pix_fmt +vulkan -noauto_conversion_filters \
        -frames:v 60 -c:v wrapped_avframe -f null - > "$out/$stream-va-vulkan.log" 2>&1; then
        printf '%s direct VA→Vulkan mapping passed\n' "$stream"
    else
        printf '%s direct VA→Vulkan mapping failed\n' "$stream"
    fi
done
