# M0–M1 NVIDIA VFX Smoke Test Design

## Source specification

This design implements the first two milestones of
`/home/caduhd4/Área de trabalho/PLANO_NVIDIA_RTX_VSR_LINUX.md`, supplied and
approved by the user on 2026-10-05. The existing
`/home/caduhd4/rtx-vsr-linux` checkout is read-only prior art; this repository
is the new architecture.

## Goal

Establish a reproducible Python baseline on the reference RTX 4070 SUPER and
build a minimal C++ executable, `vsr-smoke`, that proves the official NVIDIA
Video Effects SDK can create, load, and run `VideoSuperRes` on GPU-resident
RGBA8 images.

## Decisions

- Scope is M0 and M1 only. M2 (the reusable core) gets a separate design and
  implementation plan after the smoke test passes.
- Use CMake and C++20. CUDA kernels and `nvcc` are not required for this
  milestone; VFX/NvCVImage owns GPU allocation and transfer.
- Link the official VFX SDK normally for the smoke test. Dynamic `dlopen()`
  loading is deferred to M2, where runtime absence must become a clean bypass.
- Accept `VFXSDK_ROOT` as a CMake cache variable or environment variable and
  search `/usr/local/VideoFX` as the final default.
- Never vendor proprietary SDK libraries or models. Do not reconstruct the SDK
  ABI manually. Official 1.3.0.0 headers are required.
- Preserve the existing Python benchmark as a diagnostic tool, but make its
  Python site-packages lookup version-independent.
- The smoke input is synthetic and deterministic. A one-time CPU-to-GPU upload
  is acceptable in M1; it is not the playback fast path.
- `NvVFX_Load()` occurs once before warm-up and timed iterations. It must never
  be included in per-frame timing.

## Components

- `tools/python/`: reference benchmark and environment/driver discovery.
- `cmake/FindNvidiaVFX.cmake`: official header/library discovery and an
  imported `NvidiaVFX::VideoFX` target.
- `src/smoke/`: argument parsing, VFX RAII session, and the CLI entry point.
- `tests/`: CPU-only parser/configuration tests plus an opt-in GPU smoke test.
- `docs/status.md`: observed environment, baseline, build commands, and exact
  blockers.

## Execution flow

`vsr-smoke` validates dimensions and quality, creates one VFX CUDA stream,
allocates persistent RGBA8 input/output images, uploads the synthetic frame,
creates `NVVFX_FX_VIDEO_SUPER_RES`, binds images and stream, sets quality and
strength, calls `NvVFX_Load()` once, performs warm-up, then times repeated
`NvVFX_Run(..., 1)` calls with stream synchronization at the measurement
boundary. Every VFX resource is released by RAII on success or error.

## Failure behavior

- Missing SDK at configure time: clear diagnostic naming `VFXSDK_ROOT` and the
  expected official headers; no fallback to private declarations.
- Missing runtime/model at execution: nonzero exit with the SDK status and a
  concise installation hint.
- Unsupported quality or dimensions: rejected before creating GPU resources.
- Any VFX error: release resources and fail; a smoke test must not claim PASS.

## Acceptance

- The Python quick benchmark passes and its result is recorded.
- CPU-only tests and CMake discovery tests pass without a GPU SDK installation.
- With official SDK 1.3.0.0 installed, `vsr-smoke --quality high --input
  1920x1080 --output 3840x2160` prints GPU, mode, dimensions, milliseconds per
  frame, and `PASS`.
- Build/test/log evidence is recorded in `docs/status.md`.

## Current environment findings

- GPU: NVIDIA GeForce RTX 4070 SUPER, `sm_89`, 12 GiB.
- Driver/KMD: 610.57.04; CUDA UMD: 13.3.
- GStreamer 1.28.6 exposes `memory:CUDAMemory` from `nvh264dec`.
- The `cudaconvert` element is currently unavailable.
- The Python wheel provides VFX runtime libraries and models, but no official
  C++ VFX headers were found.
- `nvcc`, `ngc`, and `NGC_CLI_API_KEY` are absent. Official VFX SDK Core
  installation is therefore a prerequisite for the real C++ run.

