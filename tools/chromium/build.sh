#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_root="${NVVFX_BROWSER_BUILD_ROOT:-$project_root/build/browser}"
export PATH="$build_root/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=0
export CIPD_CACHE_DIR="$build_root/cache/cipd"
export VPYTHON_VIRTUALENV_ROOT="$build_root/cache/vpython"
available_kib=$(df --output=avail "$build_root" | tail -1)
if (( available_kib < 20*1024*1024 )); then
  echo 'Build refused: less than 20 GiB free on browser build volume.' >&2
  exit 2
fi
export NVVFX_ENABLE_NVIDIA_VSR="${NVVFX_ENABLE_NVIDIA_VSR:-1}"
export VFXSDK_ROOT="${VFXSDK_ROOT:-$project_root/.sdk/VideoFX}"
if [[ "$NVVFX_ENABLE_NVIDIA_VSR" == 1 ]]; then
  for required in include/nvVideoEffects.h include/nvCVImage.h external/cuda/include/cuda.h features/nvvfxvideosuperres/include/nvVFXVideoSuperRes.h; do
    [[ -f "$VFXSDK_ROOT/$required" ]] || { echo "Missing official SDK file: $required" >&2; exit 2; }
  done
elif [[ "$NVVFX_ENABLE_NVIDIA_VSR" != 0 ]]; then
  echo 'NVVFX_ENABLE_NVIDIA_VSR must be 0 or 1.' >&2
  exit 2
fi
cd "$build_root/src"
mkdir -p out/Vsr
python3 - <<'GNARGS'
import json, os
from pathlib import Path
sdk = str(Path(os.environ['VFXSDK_ROOT']).resolve())
args = ['is_debug=false', 'is_component_build=true', 'symbol_level=0',
        'blink_symbol_level=0', 'v8_symbol_level=0', 'use_remoteexec=false',
        'use_siso=false', 'enable_validating_command_decoder=true',
        'proprietary_codecs=true', 'ffmpeg_branding="Chrome"',
        'enable_nvidia_vsr=' + ('true' if os.environ['NVVFX_ENABLE_NVIDIA_VSR'] == '1' else 'false'),
        'nvidia_vsr_sdk_root=' + json.dumps(sdk)]
Path('out/Vsr/args.gn').write_text('\n'.join(args) + '\n')
GNARGS
gn gen out/Vsr
autoninja -C out/Vsr -j"${NVVFX_BUILD_JOBS:-8}" chrome "$@"
