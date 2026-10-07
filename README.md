# NVIDIA RTX VSR on Linux

Experimental NVIDIA RTX Video Super Resolution integration for Chromium on Linux.
Locally tested on CachyOS/Wayland with an RTX 4070 SUPER. A Brave port is planned.

**No installable browser release yet.** This repository contains source, patches,
and build tools. Some local playback improvements have not reached `main`.

## Requirements

- Linux x86_64, an NVIDIA RTX GPU, and a working driver (`nvidia-smi`).
- NVIDIA Video Effects SDK Core **1.3.0.0** and **nvvfxvideosuperres 1.3.0.0**.
- For building: CMake 3.25+, a C++20 compiler, and Chromium build dependencies.

## Install the NVIDIA SDK

1. Sign in to [NGC](https://ngc.nvidia.com/) and download **1.3.0.0_linux** from
   [Video Effects SDK Core](https://catalog.ngc.nvidia.com/orgs/nvidia/maxine/resources/vfx_sdk_core/-).
   Access may require joining the Developer Program or an appropriate entitlement.
2. Extract the archive using Bash. Replace the filename below with your download:

```bash
mkdir -p "$HOME/.local/opt/nvidia-vfx"
tar -xf "$HOME/Downloads/VFXSDK_linux_1.3.0.0.tgz" -C "$HOME/.local/opt/nvidia-vfx"
export VFXSDK_ROOT="$HOME/.local/opt/nvidia-vfx/VideoFX"
```

`VFXSDK_ROOT` must contain `include`, `lib`, `external`, and `features`.
Keep the bundled dependencies and directory structure intact.

3. Create a Personal API Key with NGC Catalog access in **Account Settings → API Keys**.
   Install the feature with the SDK's official script:

```bash
read -r -s -p 'NGC API key: ' NGC_CLI_API_KEY
printf '\n'
export NGC_CLI_API_KEY
(
  cd "$VFXSDK_ROOT/features" || exit 1
  bash ./install_feature.sh -f nvvfxvideosuperres -v 1.3.0.0
)
unset NGC_CLI_API_KEY
```

The script detects your GPU architecture. No `sudo` is needed for this user-owned
installation. Never share your API key. For access errors, check your account
permissions and key scopes. See [NVIDIA's installation guide](https://docs.nvidia.com/maxine/vfx/latest/LinuxVFXSDK/InstalltheVFXSDK.html).

## Build

For Chromium setup, patches, and build instructions, see
[the development handoff](docs/CLOUD_HANDOFF.md).
The SDK path is currently embedded at build time; changing `VFXSDK_ROOT` when
launching someone else's binary does not relocate it.

To build and test the standalone GPU backend:

```bash
cmake -S . -B build/gpu -DCMAKE_BUILD_TYPE=Release -DVFXSDK_ROOT="$VFXSDK_ROOT"
cmake --build build/gpu --parallel 2
ctest --test-dir build/gpu -L gpu --output-on-failure
```

## Status and licensing

Local playback supports fullscreen-only enhancement and up to 4K output.
Protected and HDR content bypass processing. Other GPUs and distributions still
need validation. The local GPU diagnostic reported `sandboxed=false`; security
and portability work remains before a public browser release.

NVIDIA libraries and models are downloaded separately. Redistribution depends on
[NVIDIA's license terms](https://www.nvidia.com/en-us/agreements/enterprise-software/product-specific-terms-for-ai-products/)
and how the SDK was obtained; Developer Program access has specific restrictions.
This is an independent experiment, not an NVIDIA-supported desktop integration.
