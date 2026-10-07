#!/usr/bin/env bash
set -euo pipefail
export NVVFX_VSR_ENABLED=0
exec "$(dirname -- "${BASH_SOURCE[0]}")/run.sh" "$@"
