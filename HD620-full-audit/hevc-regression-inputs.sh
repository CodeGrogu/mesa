#!/usr/bin/env bash
# Generate deterministic HEVC streams with inter frames and multiple GOPs.
set -euo pipefail
cd "$(dirname "$0")"
out="${1:-evidence/hevc-regression/inputs}"
mkdir -p "$out"
for spec in main:yuv420p:nv12:640x384 main10:yuv420p10le:p010le:962x514; do
    IFS=: read -r profile planar semiplanar size <<< "$spec"
    stream="$out/$profile.hevc"
    timeout 60 ffmpeg -y -hide_banner -nostdin -xerror -loglevel error         -f lavfi -i "testsrc2=size=$size:rate=30" -filter_threads 1 -threads 1 -frames:v 36         -pix_fmt "$planar" -c:v libx265 -preset fast -profile:v "$profile"         -x265-params 'pools=none:frame-threads=1:wpp=0:bframes=3:keyint=18:min-keyint=18:scenecut=0:log-level=error'         -f hevc "$stream"
    timeout 30 ffmpeg -y -hide_banner -nostdin -xerror -loglevel error -threads 1 -filter_threads 1         -i "$stream" -map 0:v:0 -vf "format=$semiplanar" -c:v rawvideo -f framemd5 "$out/$profile-software.md5"
    ffprobe -v error -select_streams v:0 -show_frames -show_entries frame=pict_type         -show_streams -show_entries stream=codec_name,profile,pix_fmt,width,height         -of json "$stream" > "$out/$profile-structure.json"
    timeout 30 ffmpeg -hide_banner -nostdin -xerror -loglevel info -i "$stream"         -map 0:v:0 -c:v copy -bsf:v trace_headers -f null - 2> "$out/$profile-headers.log"
done
