#!/usr/bin/env bash
set -euo pipefail
export NVVFX_VSR_ENABLED=1
exec "$(dirname -- "${BASH_SOURCE[0]}")/run.sh" "$@"
