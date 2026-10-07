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
  if [[ -s "$media_root/$name" ]] && python3 - "$media_root/$name" "$size" "$rate" "$encoder" <<'VERIFY'
import json, subprocess, sys
from fractions import Fraction
try:
    clip, size, rate, encoder = sys.argv[1:]
    result = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-select_streams', 'v:0',
        '-show_streams', '-show_format', '-of', 'json', clip]))
    stream = result['streams'][0]
    width, height = map(int, size.split('x'))
    codec = {'libx264': 'h264', 'libvpx-vp9': 'vp9', 'libsvtav1': 'av1'}[encoder]
    expected_container = 'mp4' if encoder == 'libx264' else 'webm'
    valid = (stream['codec_name'] == codec and
             (stream['width'], stream['height']) == (width, height) and
             Fraction(stream['avg_frame_rate']) == int(rate) and
             stream['pix_fmt'] == 'yuv420p' and
             all(stream.get(tag) == 'bt709' for tag in
                 ['color_space', 'color_transfer', 'color_primaries']) and
             expected_container in result['format']['format_name'].split(',') and
             abs(float(result['format']['duration']) - 12) < 0.1)
    sys.exit(0 if valid else 1)
except (KeyError, ValueError, IndexError, subprocess.CalledProcessError):
    sys.exit(1)
VERIFY
  then
    return
  fi
  ffmpeg -hide_banner -loglevel warning -nostdin -f lavfi \
    -i "testsrc2=size=$size:rate=$rate:duration=12" \
    -vf "setparams=color_primaries=bt709:color_trc=bt709:colorspace=bt709:range=limited" \
    -an -pix_fmt yuv420p \
    -color_primaries bt709 -color_trc bt709 -colorspace bt709 -color_range tv \
    -c:v "$encoder" -threads 4 "$@" -y "$media_root/$name"
}
make_clip h264-360p-30.mp4 640x360 30 libx264 -preset veryfast -crf 28 -movflags +faststart
make_clip h264-480p-30.mp4 854x480 30 libx264 -preset veryfast -crf 28 -movflags +faststart
make_clip h264-720p-30.mp4 1280x720 30 libx264 -preset veryfast -crf 28 -movflags +faststart
make_clip h264-720p-60.mp4 1280x720 60 libx264 -preset veryfast -crf 28 -movflags +faststart
make_clip h264-1080p-30.mp4 1920x1080 30 libx264 -preset veryfast -crf 28 -movflags +faststart
make_clip vp9-720p-30.webm 1280x720 30 libvpx-vp9 -deadline realtime -cpu-used 8 -crf 38 -b:v 0
make_clip av1-720p-30.webm 1280x720 30 libsvtav1 -preset 12 -crf 38 -svtav1-params lp=4
# Matching AVC profile/level, fragmented MP4s for an MSE configuration change.
make_clip mse-720p-30.mp4 1280x720 30 libx264 -preset veryfast -crf 28 -profile:v high -level:v 4.0 -g 30 -movflags +frag_keyframe+empty_moov+default_base_moof
make_clip mse-1080p-30.mp4 1920x1080 30 libx264 -preset veryfast -crf 28 -profile:v high -level:v 4.0 -g 30 -movflags +frag_keyframe+empty_moov+default_base_moof
