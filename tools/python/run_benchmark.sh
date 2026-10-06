#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
python_bin="${NVVFX_PYTHON:-$repo_root/.venv/bin/python}"

if [[ ! -x "$python_bin" ]]; then
  echo "VSR benchmark interpreter not found: $python_bin" >&2
  echo "Set NVVFX_PYTHON to a Python environment containing nvidia-vfx and cupy." >&2
  exit 2
fi

site_packages="$($python_bin -c "import sysconfig; print(sysconfig.get_paths()['purelib'])")"
cuda_runtime="$site_packages/nvidia/cuda_runtime"
curand_lib="$site_packages/nvidia/curand/lib"

export CUDA_PATH="$cuda_runtime"
export LD_LIBRARY_PATH="$curand_lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

exec "$python_bin" "$repo_root/tools/python/bench_vsr.py" "$@"
