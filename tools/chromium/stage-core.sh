#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_root="${NVVFX_BROWSER_BUILD_ROOT:-$project_root/build/browser}"
source_root="$build_root/src"
revision=b510e9d7cd3a2fbd78d0ddc42234103206c5f78d
git -C "$source_root" merge-base --is-ancestor "$revision" HEAD
adapter="$source_root/third_party/nvidia_vsr"
mkdir -p "$adapter/include/nvvfx_vsr" "$adapter/src"
copy_if_different() {
  local source=$1 target=$2
  if [[ ! -f "$target" ]] || ! cmp -s -- "$source" "$target"; then
    cp -- "$source" "$target"
  fi
}

for name in BUILD.gn features.gni; do
  copy_if_different "$project_root/chromium/core/$name" "$adapter/$name"
done
for name in dimensions.h processor_config.h frame_eligibility.h job_admission.h presentation_cache.h vsr_processor.h gl_bridge.h; do
  copy_if_different "$project_root/include/nvvfx_vsr/$name" "$adapter/include/nvvfx_vsr/$name"
done
for name in processor_config.cpp frame_eligibility.cpp job_admission.cpp vsr_processor.cpp gl_bridge.cpp image_descriptor.c; do
  copy_if_different "$project_root/src/core/$name" "$adapter/src/$name"
done
