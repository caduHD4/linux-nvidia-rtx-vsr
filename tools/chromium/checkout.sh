#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_root="${NVVFX_BROWSER_BUILD_ROOT:-$project_root/build/browser}"
revision=b510e9d7cd3a2fbd78d0ddc42234103206c5f78d
release=154.0.8037.97
mkdir -p "$build_root"
export PATH="$build_root/depot_tools:$PATH"
export DEPOT_TOOLS_UPDATE=0
export CIPD_CACHE_DIR="$build_root/cache/cipd"
export VPYTHON_VIRTUALENV_ROOT="$build_root/cache/vpython"
if [[ ! -d "$build_root/depot_tools/.git" ]]; then
  git clone --depth=1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "$build_root/depot_tools"
fi
if [[ ! -f "$build_root/.gclient" ]]; then
  (cd "$build_root" && gclient config --name=src "https://chromium.googlesource.com/chromium/src.git@$revision")
  printf "target_os = ['linux']\n" >> "$build_root/.gclient"
fi
if [[ ! -d "$build_root/src/.git" ]]; then
  git init "$build_root/src"
  git -C "$build_root/src" remote add origin https://chromium.googlesource.com/chromium/src.git
fi
if ! git -C "$build_root/src" rev-parse --verify HEAD >/dev/null 2>&1; then
  if ! git -C "$build_root/src" cat-file -e "$revision^{commit}" 2>/dev/null; then
    git -C "$build_root/src" -c protocol.version=2 fetch --depth=1 --no-tags origin "refs/tags/$release"
    actual=$(git -C "$build_root/src" rev-parse FETCH_HEAD)
    [[ "$actual" == "$revision" ]] || { echo 'Unexpected release revision; aborting.' >&2; exit 2; }
  fi
  git -C "$build_root/src" checkout -b feature/linux-nvidia-vsr "$revision"
fi
if ! git -C "$build_root/src" merge-base --is-ancestor "$revision" HEAD; then
  echo 'Existing source checkout has a different baseline; aborting.' >&2
  exit 2
fi
cd "$build_root"
gclient sync --no-history --nohooks --jobs=8
gclient runhooks
