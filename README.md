# NVIDIA RTX VSR on Linux

Integração experimental do NVIDIA RTX Video Super Resolution no Chromium Linux,
com porte para Brave planejado. O core SDK/CUDA/GL já foi testado localmente;
**a integração de playback no navegador ainda está incompleta**.

Para continuar na nuvem, leia **[CLOUD_HANDOFF.md](docs/CLOUD_HANDOFF.md)**: contém
setup, revisão fixada, SDK externo, aplicação dos patches, estado dos testes,
erros conhecidos e próximos passos. Leia também o
[design](docs/specs/2026-10-05-chromium-nvidia-vsr-design.md) e o
[plano](docs/superpowers/plans/2026-10-05-chromium-nvidia-vsr.md).

O repositório contém fontes, testes, scripts e patches, sem checkout Chromium,
SDK proprietário, binários, caches ou vídeos gerados.

## Requirements

- NVIDIA RTX GPU and a working NVIDIA driver
- CMake 3.25 or newer and a C++20 compiler
- NVIDIA Video Effects SDK Core 1.3.0.0 for Linux
- `nvvfxvideosuperres` feature 1.3.0.0 matching the GPU compute capability

The SDK and feature are external NVIDIA/NGC prerequisites and are intentionally
not committed to this repository. After installing both under one root, point
CMake at that directory with `VFXSDK_ROOT`. The expected feature layout is:

```text
VideoFX/
├── include/{nvVideoEffects.h,nvCVImage.h}
├── lib/
└── features/nvvfxvideosuperres/
    ├── include/nvVFXVideoSuperRes.h
    └── lib/libnvVFXVideoSuperRes.so
```

## Build and run the GPU smoke test

```bash
cmake -S . -B build/gpu \
  -DCMAKE_BUILD_TYPE=Release \
  -DVFXSDK_ROOT="$PWD/.sdk/VideoFX"
cmake --build build/gpu --parallel 2
ctest --test-dir build/gpu -L gpu --output-on-failure
```

Run it directly to see the selected GPU and timing:

```bash
./build/gpu/vsr-smoke \
  --quality high \
  --input 1920x1080 \
  --output 3840x2160
```

The named qualities map to NVIDIA VSR modes `low=1`, `medium=2`, `high=3`, and
`ultra=4`. Use `./build/gpu/vsr-smoke --help` for all options.

## CPU-only development tests

The parser, resource lifecycle, failure cleanup, and SDK discovery tests can run
without loading the NVIDIA SDK:

```bash
cmake -S . -B build/cpu -DNVVFX_VSR_BUILD_SMOKE=OFF
cmake --build build/cpu
ctest --test-dir build/cpu --output-on-failure
python -m unittest discover -s tests/python -v
```

If `nvidia-smi` reports `Driver/library version mismatch`, reboot into the
kernel installed with the current NVIDIA packages before diagnosing VFX.

