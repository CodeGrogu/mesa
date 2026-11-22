#!/usr/bin/env bash
# Exercise advertised VA-API paths on the render node, with decoded frame verification.
set -u
cd "$(dirname "$0")"
mkdir -p evidence/media
out="$PWD/evidence/media"
export LIBVA_DRIVER_NAME=iHD
device=/dev/dri/renderD128
common=(-hide_banner -nostdin -y -xerror -threads 2 -filter_threads 1)
for spec in h264_vaapi:nv12:h264 hevc_vaapi:nv12:hevc hevc_vaapi:p010le:hevc \
    mpeg2_vaapi:nv12:mpeg2video vp8_vaapi:nv12:ivf mjpeg_vaapi:nv12:mjpeg; do
    IFS=: read -r encoder format container <<< "$spec"
    key="$encoder-$format"
    stream="$out/$key.$container"
    log="$out/$key.log"
    if ! ffmpeg "${common[@]}" -vaapi_device "$device" \
        -f lavfi -i 'testsrc2=size=640x360:rate=30' -vf "format=$format,hwupload" \
        -frames:v 60 -c:v "$encoder" -qp 24 -f "$container" "$stream" > "$log" 2>&1; then
        printf '%s encode failed\n' "$key"
        continue
    fi
    ffprobe -v error -show_entries stream=codec_name,profile,pix_fmt -of compact "$stream" >> "$log" 2>&1
    if ! ffmpeg "${common[@]}" -hwaccel vaapi -hwaccel_device "$device" -hwaccel_output_format vaapi \
        -i "$stream" -vf "hwdownload,format=$format" -c:v rawvideo -f framemd5 "$out/$key-hw.md5" >> "$log" 2>&1 ||
       ! ffmpeg "${common[@]}" -i "$stream" -vf "format=$format" -c:v rawvideo \
        -f framemd5 "$out/$key-sw.md5" >> "$log" 2>&1; then
        printf '%s decode failed\n' "$key"
        continue
    fi
    count=$(awk '!/^#/ {n++} END {print n+0}' "$out/$key-hw.md5")
    if diff -q <(awk -F, '!/^#/ {print $NF}' "$out/$key-hw.md5") <(awk -F, '!/^#/ {print $NF}' "$out/$key-sw.md5"); then
        printf '%s frames=%s hardware/software hashes equal\n' "$key" "$count"
    else
        printf '%s frames=%s hardware/software hashes differ\n' "$key" "$count"
    fi
done
