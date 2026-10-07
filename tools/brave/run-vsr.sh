#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_root="${NVVFX_BRAVE_BUILD_ROOT:-$project_root/build/brave}"
browser="$build_root/src/out/BraveVsr/brave"
[[ -x "$browser" ]] || { echo "Build Brave first: $browser" >&2; exit 2; }
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/10_nvidia.json
export EGL_PLATFORM=wayland LIBVA_DRIVER_NAME=nvidia NVD_BACKEND=direct
export NVVFX_VSR_SHARPNESS="${NVVFX_VSR_SHARPNESS:-0.35}"
export NVVFX_VSR_TARGET_HEIGHT="${NVVFX_VSR_TARGET_HEIGHT:-2160}"
mode="${NVVFX_VSR_ENABLED:-1}"
case "$mode" in
  1) vsr_flags=(--enable-features=NvidiaVideoSuperResolution,NvidiaVsrNativeEgl,VaapiOnNvidiaGPUs,AcceleratedVideoDecodeLinuxGL) ;;
  0) vsr_flags=(--disable-features=NvidiaVideoSuperResolution --enable-features=NvidiaVsrNativeEgl,VaapiOnNvidiaGPUs,AcceleratedVideoDecodeLinuxGL) ;;
  *) echo 'NVVFX_VSR_ENABLED must be 0 or 1.' >&2; exit 2 ;;
esac
sandbox_flags=()
if [[ "${NVVFX_VSR_SANDBOX_EXPERIMENT:-0}" == 1 ]]; then
  sandbox_flags=(--gpu-sandbox-start-early --gpu-sandbox-failures-fatal=yes)
else
  echo 'Experimental Brave VSR: GPU sandbox has not passed functional inference validation. See docs/distribution.md.' >&2
fi
exec "$browser" "${sandbox_flags[@]}" --ozone-platform=wayland --use-gl=egl \
  --disable-gl-extensions=GL_EXT_multisampled_render_to_texture,GL_IMG_multisampled_render_to_texture \
  --use-cmd-decoder=validating "${vsr_flags[@]}" \
  --user-data-dir="$build_root/profiles/brave-vsr-$mode" \
  --no-first-run --no-default-browser-check --enable-logging=stderr "$@"
