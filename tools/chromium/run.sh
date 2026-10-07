#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_root="${NVVFX_BROWSER_BUILD_ROOT:-$project_root/build/browser}"
browser="$build_root/src/out/Vsr/chrome"
[[ -x "$browser" ]] || { echo "Build Chromium first: $browser" >&2; exit 2; }
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
export EGL_PLATFORM=wayland
export LIBVA_DRIVER_NAME=nvidia
export NVD_BACKEND=direct
# Mild GPU post-sharpen after VSR/denoise; 0 restores the prior image.
export NVVFX_VSR_SHARPNESS="${NVVFX_VSR_SHARPNESS:-0.35}"
# 4K supersampling, downscaled by Chromium to the player display size.
# Set 1080 to restore native-1080 denoise / previous upscaling target.
export NVVFX_VSR_TARGET_HEIGHT="${NVVFX_VSR_TARGET_HEIGHT:-2160}"
mode="${NVVFX_VSR_ENABLED:-1}"
case "$mode" in
  1) vsr_flags=(--enable-features=NvidiaVideoSuperResolution,NvidiaVsrNativeEgl,VaapiOnNvidiaGPUs,AcceleratedVideoDecodeLinuxGL) ;;
  0) vsr_flags=(--disable-features=NvidiaVideoSuperResolution --enable-features=NvidiaVsrNativeEgl,VaapiOnNvidiaGPUs,AcceleratedVideoDecodeLinuxGL) ;;
  *) echo 'NVVFX_VSR_ENABLED must be 0 or 1.' >&2; exit 2 ;;
esac
# Early TSYNC sandbox is under investigation; opt in only for diagnostics.
# The historical development path has not passed GPU isolation validation.
sandbox_flags=()
if [[ "${NVVFX_VSR_SANDBOX_EXPERIMENT:-0}" == 1 ]]; then
  sandbox_flags=(--gpu-sandbox-start-early --gpu-sandbox-failures-fatal=yes)
fi
# Native NVIDIA EGL/GLES with validating decoder; no sandbox-disabling flags.
# Local SystemInfo reports GPU sandboxed=false; do not infer isolation from flags.
exec "$browser" "${sandbox_flags[@]}" --ozone-platform=wayland --use-gl=egl --disable-gl-extensions=GL_EXT_multisampled_render_to_texture,GL_IMG_multisampled_render_to_texture \
  --use-cmd-decoder=validating "${vsr_flags[@]}" \
  --user-data-dir="$build_root/profiles/chromium-vsr-$mode" \
  --no-first-run --no-default-browser-check --enable-logging=stderr "$@"
