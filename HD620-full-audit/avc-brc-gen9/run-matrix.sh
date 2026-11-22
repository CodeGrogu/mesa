#!/bin/bash
# Rate-control matrix: ANV (Vulkan, h264_vulkan) vs iHD (VA-API, h264_vaapi).
# usage: run-matrix.sh WORKDIR ANV_ICD_JSON > rate-matrix.txt
W=$1; ICD=$2; D=$(dirname "$0")
[ -f $W/static.yuv ] || ffmpeg -loglevel error -y -f lavfi -i testsrc2=size=1920x1080:rate=30 -t 25 -c:v rawvideo -pix_fmt nv12 -f rawvideo $W/static.yuv
enc() { drv=$1 src=$2 fr=$3 name=$4; shift 4
  if [ $drv = anv ]; then
    ANV_DEBUG=video-encode VK_DRIVER_FILES=$ICD ffmpeg -hide_banner -loglevel error -y -init_hw_device vulkan=vk:0 -filter_hw_device vk -f rawvideo -pix_fmt nv12 -s 1920x1080 -r 30 -i $W/$src.yuv -frames:v $fr -vf hwupload -c:v h264_vulkan "$@" $W/m-$name.mp4 2>&1 | grep -v Estimating
  else
    ffmpeg -hide_banner -loglevel error -y -vaapi_device /dev/dri/renderD128 -f rawvideo -pix_fmt nv12 -s 1920x1080 -r 30 -i $W/$src.yuv -frames:v $fr -vf 'format=nv12,hwupload' -c:v h264_vaapi "$@" $W/m-$name.mp4 2>&1 | grep -v Estimating
  fi; }
for drv in ${DRIVERS:-anv ihd}; do
  RC=cbr; VRC=vbr; [ $drv = ihd ] && RC=CBR && VRC=VBR
  for src in moving static; do
    fr=900; [ $src = static ] && fr=750
    for cfg in "cbr 2M g30 bf0|-rc_mode $RC -b:v 2M -maxrate 2M -bufsize 2M -g 30 -bf 0|2000000" \
               "cbr 8M g120 bf0|-rc_mode $RC -b:v 8M -maxrate 8M -bufsize 8M -g 120 -bf 0|8000000" \
               "cbr 4M g60 bf2|-rc_mode $RC -b:v 4M -maxrate 4M -bufsize 4M -g 60 -bf 2|4000000" \
               "vbr 6M/9M g60 bf0|-rc_mode $VRC -b:v 6M -maxrate 9M -bufsize 9M -g 60 -bf 0|9000000"; do
      label=${cfg%%|*}; rest=${cfg#*|}; args=${rest%%|*}; rate=${rest#*|}
      name="$drv-$src-$(echo $label | tr ' /' '__')"
      enc $drv $src $fr "$name" $args
      echo "## $drv $src $label"
      python3 $D/rate-check.py $W/m-$name.mp4 30 $rate $rate && rm -f $W/m-$name.mp4
    done
  done
done
