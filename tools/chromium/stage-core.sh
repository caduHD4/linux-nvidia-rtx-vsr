#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_root="${NVVFX_BROWSER_BUILD_ROOT:-$project_root/build/browser}"
source_root="$build_root/src"
revision=b510e9d7cd3a2fbd78d0ddc42234103206c5f78d
git -C "$source_root" merge-base --is-ancestor "$revision" HEAD
adapter="$source_root/third_party/nvidia_vsr"
mkdir -p "$adapter/include/nvvfx_vsr" "$adapter/src"
cp "$project_root/chromium/core/BUILD.gn" "$project_root/chromium/core/features.gni" "$adapter/"
cp "$project_root/include/nvvfx_vsr/dimensions.h" "$project_root/include/nvvfx_vsr/processor_config.h" "$project_root/include/nvvfx_vsr/frame_eligibility.h" "$project_root/include/nvvfx_vsr/job_admission.h" "$project_root/include/nvvfx_vsr/presentation_cache.h" "$project_root/include/nvvfx_vsr/vsr_processor.h" "$project_root/include/nvvfx_vsr/gl_bridge.h" "$adapter/include/nvvfx_vsr/"
cp "$project_root/src/core/processor_config.cpp" "$project_root/src/core/frame_eligibility.cpp" "$project_root/src/core/job_admission.cpp" "$project_root/src/core/vsr_processor.cpp" "$project_root/src/core/gl_bridge.cpp" "$project_root/src/core/image_descriptor.c" "$adapter/src/"
