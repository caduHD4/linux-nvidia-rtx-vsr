# Project status

**Transferência para nuvem:** build Chromium cancelado, browser VSR ainda não
validado. Estado atual e comandos em [CLOUD_HANDOFF.md](CLOUD_HANDOFF.md).
As seções abaixo são registros históricos, não o estado completo atual.


## 2026-10-05 — M0 baseline

Reference machine:

- GPU: NVIDIA GeForce RTX 4070 SUPER (`sm_89`, 12 GiB)
- NVIDIA KMD: 610.57.04
- CUDA UMD: 13.3
- Python environment: Python 3.14 environment from the legacy PoC

Command:

```bash
NVVFX_PYTHON=/home/caduhd4/rtx-vsr-linux/.venv/bin/python \
  tools/python/run_benchmark.sh --quick
```

Observed results:

| Input | Output | Mode | ms/frame | Theoretical throughput |
|---|---|---:|---:|---:|
| 1280x720 | 2560x1440 | LOW (1) | 11.42 | 87.6 fps |
| 1280x720 | 2560x1440 | MEDIUM (2) | 1.12 | 892.3 fps |

Both cases completed successfully. LOW remains anomalously slower than MEDIUM,
matching the behavior documented in the source plan. This result is a baseline,
not an architectural performance guarantee.

## Architecture status

The HTTP proxy, browser extension, and decode/VSR/re-encode pipeline in
`/home/caduhd4/rtx-vsr-linux` are legacy PoC/reference material only. They are
not part of the target playback architecture.

## 2026-10-05 — M1 C++ VFX smoke

Reference machine after aligning the loaded kernel module and userspace driver:

- Kernel: `7.2.9-1-cachyos`
- GPU: NVIDIA GeForce RTX 4070 SUPER (`sm_89`, 12 GiB)
- NVIDIA driver: `615.71.09`
- NVIDIA VFX SDK Core and VideoSuperRes: `1.3.0.0`

The official SDK was installed outside version control under `.sdk/VideoFX`.
The feature installer reported zero TensorRT models because VideoSuperRes uses
the NGX runtime and feature library supplied by this package and the NVIDIA
driver.

C++ command and result:

```bash
./build/gpu/vsr-smoke \
  --quality high --input 1920x1080 --output 3840x2160
```

```text
GPU: NVIDIA GeForce RTX 4070 SUPER
VSR quality: 3
1920x1080 -> 3840x2160
13.5361 ms/frame
PASS
```

Python diagnostic command:

```bash
NVVFX_PYTHON=/home/caduhd4/rtx-vsr-linux/.venv/bin/python \
  tools/python/run_benchmark.sh
```

The matching Python HIGH case completed at `5.37 ms/frame` (approximately
`186.1 fps`). These timings are evidence that both SDK paths execute, not a
like-for-like performance comparison: C++ uses a persistent interleaved RGBA8
synthetic image and synchronizes at measurement boundaries, while Python uses a
random planar float32 tensor, DLPack output wrapping, and synchronizes after
every measured call. Both perform three warm-up calls and fifteen measured
iterations.

M1 is complete: the native executable discovers the official SDK, creates and
loads VideoSuperRes, processes `1920x1080 -> 3840x2160` in HIGH mode, and cleans
up SDK resources on success and injected failures. M2/player-core integration
has not started and is not claimed here.

Complete milestone verification:

```bash
python -m unittest discover -s tests/python -v
ctest --test-dir build/cpu --output-on-failure
ctest --test-dir build/gpu --output-on-failure
```

Results after the final review fixes: 6/6 Python tests passed, 3/3 CPU CTests
passed, and 4/4 GPU-build CTests passed. The GPU suite includes the real
VideoSuperRes smoke and asserts its GPU, HIGH mode, dimensions, positive timing
text, and final `PASS` report.

## 2026-10-05 — Chromium core preparation

Approved architecture/spec and implementation plan are committed. Execution is
inline and authorized through a testable browser build. Worktree remains the
existing feature worktree; Chromium checkout uses the dedicated ext4 volume.

New independent policy selects ULTRA mode 4 for 720p->1080p and DENOISE_ULTRA
mode 11 for native 1080p, with strength 1.0. The dynamic VFX/CUDA core owns
persistent RGBA8 buffers, one private stream, completion/timing events, and
loads the model once. It refuses a second submission before completion is
acknowledged and drains partial enqueues before cleanup. Runtime absence is a
status, not a browser startup link dependency. CPU policy and presentation
cache tests cover target/quality, stale generations, fixed decisions over 180
refreshes, late completion, and bounded storage.

A diagnostic-only GPU test verified opaque RGBA output/channel ranges and
processed 10,000 frames for EACH mode. Recorded GPU event timings (including
core device copies, excluding Chromium GL staging/publication):

| Operation | frames | p50 ms | p95 ms | p99 ms |
|---|---:|---:|---:|---:|
| 720p->1080p mode 4 | 10,000 | 2.74227 | 3.08531 | 5.20397 |
| 1080p native mode 11 | 10,000 | 9.64506 | 10.1734 | 10.5124 |

GPU CTest 6/6 passed before the cache addition. Timing includes activity from
other desktop GPU consumers; it is not a browser playback guarantee. The
core test performs host upload/readback only for diagnostic fixtures; neither
is part of the production Submit path. Chromium integration/interop has not
yet passed and is not claimed.

The native GL/CUDA bridge now passes both modes with NVIDIA EGL desktop GL
and GLES3. Default EGL selected Mesa llvmpipe and correctly failed CUDA GL
registration/device validation; the host diagnostic pins NVIDIA's GLVND EGL
vendor and X11 platform explicitly. This is not yet validation of Chromium
SharedImage backing access, thread transfer or its sandbox. Current GPU CTest
suite is 8/8, including native GL->CUDA->VFX->GL image validation.

## 2026-10-06 — Local Chromium integration resumed

The cloud environment ran out of memory; work resumed on the original Linux
machine. The persistent core, native EGL/CUDA bridge, eligibility, admission
and presentation cache exist. The latest standalone suite passed **12/12**,
including a CUDA synchronization failure that verifies bridge teardown retains
registered resources. Python tests passed **6/6**.

The media IPC client, GPU service/worker, channel interface and pre-sandbox hook
are implemented in `chromium/overlay`. Updated client, renderer, IPC tests and
worker objects compile with Chromium's bundled compiler; the GPU service
library linked after using Chromium's GL framebuffer dispatch bindings.
The selected `media_unittests` suite passed **53/53**, covering the NVIDIA IPC
client/cache and existing VideoRendererImpl regressions. The final
`chrome` executable, sandbox SDK loading and real VSR playback remain pending.
Brave portability work has not started.

Test launchers and a local DevTools pipe playback diagnostic are prepared under
`tools/chromium`. They keep the sandbox enabled and store profiles/media/logs
outside Git. Synthetic MSE fixtures now cover a 720p-to-1080p configuration
change. These tools have passed syntax checks; actual browser execution is
still pending. Earlier milestone timings in this document describe standalone
SDK tests, not browser performance.
