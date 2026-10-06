#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_root="${NVVFX_BROWSER_BUILD_ROOT:-$project_root/build/browser}"
media_root="$build_root/media"
mkdir -p "$media_root"
cp "$project_root/tests/media/index.html" "$media_root/index.html"
# FFmpeg synthetic patterns: no copyrighted source material, SDR BT.709.
make_clip() {
  local name=$1 size=$2 rate=$3 encoder=$4
  shift 4
  if [[ -s "$media_root/$name" ]] &&
     [[ $(ffprobe -v error -select_streams v:0 -show_entries stream=color_space,color_transfer,color_primaries -of csv=p=0 "$media_root/$name") == bt709,bt709,bt709 ]]; then
    return
  fi
  ffmpeg -hide_banner -loglevel warning -nostdin -f lavfi \
    -i "testsrc2=size=$size:rate=$rate:duration=12" \
    -vf "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709:range=limited" \
    -an -pix_fmt yuv420p \
    -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv \
    -c:v "$encoder" -threads 4 "$@" -y "$media_root/$name"
}
make_clip h264-720p-30.mp4 1280x720 30 libx264 -preset veryfast -crf 28 -movflags +faststart
make_clip h264-720p-60.mp4 1280x720 60 libx264 -preset veryfast -crf 28 -movflags +faststart
make_clip h264-1080p-30.mp4 1920x1080 30 libx264 -preset veryfast -crf 28 -movflags +faststart
make_clip vp9-720p-30.webm 1280x720 30 libvpx-vp9 -deadline realtime -cpu-used 8 -crf 38 -b:v 0
make_clip av1-720p-30.webm 1280x720 30 libsvtav1 -preset 12 -crf 38 -svtav1-params lp=4
