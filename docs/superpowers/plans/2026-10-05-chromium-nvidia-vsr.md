# Chromium NVIDIA VSR Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Entregar Chromium Linux experimental testável com NVIDIA VSR GPU, denoise nativo e fallback; preparar porte Brave depois da validação.

**Architecture:** Core independente com loader dinâmico VFX/CUDA e buffers persistentes. Cliente media mantém originais no scheduler; serviço GPU processa staging GL privado; apresentação escolhe somente resultados prontos.

**Tech Stack:** C++20, CMake/CTest, CUDA Driver API, VFX 1.3.0.0, Chromium GN/Clang/Mojo/OpenGL, Python para tooling/testes.

**Spec:** `docs/specs/2026-10-05-chromium-nvidia-vsr-design.md`

## Global Constraints

- Chromium primeiro; Brave depois. Feature desligada por padrão, opt-in no launcher de testes.
- VSR_ULTRA 4 e DENOISE_ULTRA 11, strength 1.0, alvo 1920x1080.
- SDR GPU 8-bit; protegido/HDR/10-bit/incompatível faz bypass antes de import/processamento.
- Sem pixels em CPU no playback; cópias GPU->GPU permitidas. Nunca esperar VFX no compositor.
- Uma execução por frame único, sem recomputar a 180 Hz; original é fallback.
- Pool 3 slots, 1 job ativo + 1 aguardando, 256 MiB do adapter, recursos em voo não são reutilizados.
- Runtime/modelos oficiais externos, sem vendoring de bibliotecas proprietárias.
- Checkout/output em `/home/caduhd4/Builds/brave`; reserva 20 GiB, sem reparticionamento.
- Execução inline autorizada pelo usuário até concluir, sem perguntas. Essa instrução substitui gates de confirmação das skills.

## Review Focus

- Seek com PTS repetido: resultados de geração antiga nunca aparecem.
- Frame protegido: nenhuma importação ou chamada SDK.
- Timeout após enqueue: não libera nem reutiliza memória em voo.
- Refresh 180 Hz: escolha estável e Run por ID, não por refresh.
- Runtime ausente/sandbox: bypass sem falhar inicialização do browser.

### Task 1: Política de processamento e core mínimo

**Files:** Create `include/nvvfx_vsr/processor_config.h`, `src/core/processor_config.cpp`, `tests/unit/processor_config_test.cpp`, `include/nvvfx_vsr/vsr_processor.h`, `src/core/vsr_processor.cpp`, `tests/gpu/vsr_processor_test.cpp`; modify `CMakeLists.txt`.

**Interfaces:** `SelectProcessorConfig(Dimensions input, Dimensions target) -> std::optional<ProcessorConfig>`; config includes input/output, quality, strength. `VsrProcessor::Create(config, sdk_root, CUcontext)`, `Submit(CudaFrameView input, CudaFrameView output)`, `Poll()`, `Drain()`; explicit context/pitch/RGBA8, one job in flight.

- [x] Write failing policy tests: 720p->1080p mode 4, 1080p native mode 11, >target bypass, vertical fit, invalid dimensions; run RED.
- [x] Implement policy; run CPU suite GREEN.
- [x] Write GPU test driving persistent buffers for modes 4/11, output nonzero and alpha/channel patterns, repeated submits, busy handling; run RED.
- [x] Implement dynamic loader, version checks, context/stream/event ownership, Load once; failure cleanup drains in-flight work.
- [x] Run GPU modes plus 10,000 frames and record p50/p95/p99/VRAM; retain diagnostic-only readback in tests. Commit core.

### Task 2: Chromium baseline checkout/build

**Files:** Create `tools/chromium/checkout.sh`, `tools/chromium/build.sh`, `docs/chromium-build.md`; revision/args/logs under build volume.

**Interfaces:** checkout at `Builds/brave/chromium/src`, stable revision pinned to release JSON hash, output `out/Vsr`, system headers VFX read-only. Build script checks disk reserve and outputs logs.

- [x] Fetch depot_tools and shallow Linux-only stable Chromium revision; record exact commit before patches.
- [x] Configure reduced-symbol release development output with GN; use bundled clang/sysroot, no system package changes unless required.
- [ ] Build `chrome` and selected media/gpu test targets; launch with separate test profile and sandbox enabled; record baseline GPU backend and actual video decoder.

### Task 3: Client policy/cache and resource probe

**Files:** Create `media/renderers/nvidia_vsr_presentation_cache.{h,cc}`, `*_unittest.cc`, `nvidia_vsr_frame_client.{h,cc}`; modify `media/renderers/video_renderer_impl.{h,cc}`, `media/renderers/BUILD.gn`, `media/base/media_switches.{h,cc}`. Mirror patch series under project `chromium/patches`.

**Interfaces:** Cache `(generation, frame ID)` -> completed frame; `Select(original)` freezes decision on first presentation, `Invalidate()` rejects late callbacks. FrameClient owns GPU service remote and exports only allowed GPU references.

- [ ] Write/run failing tests for completed/pending, fixed choice across refresh, repeated PTS different IDs, invalidation and bounded capacity.
- [ ] Implement cache and async submission hooks outside renderer lock; keep originals in algorithm; run media tests.
- [ ] Add probe flag and report frame storage/format/protection/color/backend/representation without VFX; build and run local H264/VP9/AV1. Unsupported goes explicit bypass.

### Task 4: GPU service and GL/CUDA bridge

**Files:** Create service under `gpu/ipc/service/nvidia_vsr_*`, Mojo under `gpu/ipc/common/nvidia_vsr.mojom`, client bind in GPU channel; test targets in corresponding `BUILD.gn`. Core adapter code from Task 1 via isolated GN target.

**Interfaces:** `Process(session,generation,frame_id,exported_image,acquire_token,geometry,color,config) -> completion(exported_output,token,status)`; `Release(slot,release_token)`. Bridge source representation remains on owning sequence; worker gets private staging textures sharing a proven GL context.

- [ ] Test protection rejection before import, channel ownership, queue/pool limits and unavailable runtime with service mocks at external API boundary.
- [ ] Implement GL pattern bridge without VFX; verify CUDA GL device matching, map/copy/unmap, source-read token and output release. Validate sandbox enabled and exact thread ownership.
- [ ] Bind VFX processor, validate RGBA then NV12 color conversion. Test timeout after enqueue retains resources; no CPU readback in service.
- [ ] Run gpu tests and real HTML5 probe with ready/not-ready fallback.

### Task 5: Playback validation and user launchers

**Files:** Create `tools/chromium/run-vsr.sh`, `tools/chromium/run-baseline.sh`, `tests/media/index.html`, legal synthetic H264/VP9/AV1 fixtures generated outside Git, `docs/chromium-validation.md`; update `docs/status.md`.

**Interfaces:** launchers use separate profile, correct GPU backend and opt-in feature, sandbox enabled; local page toggle playback/settings and diagnostic tracing via browser infrastructure.

- [ ] Stress pause/seek/fullscreen/MSE resolution changes/toggle/dual videos and slow mock VFX.
- [ ] Confirm mode 4 720p->1080p and mode 11 1080p, no Run growth from 180 Hz repaint, output valid and p95/p99 measured. Protected/HDR/CPU paths bypass.
- [ ] Run all core and selected Chromium tests; build final browser; provide launch command and known unsupported combinations.
- [ ] Fresh whole-branch review using requesting-code-review skill; fix important issues with RED/GREEN tests. Commit patch series/docs; leave test build installed in build tree, no merge/push.

### Task 6: Brave portability

**Files:** Create `docs/brave-port.md`, patch mapping and standalone GN adapter compatibility notes.

**Interfaces:** Chromium patch series is reviewed against Brave's actual pinned Chromium version, not latest upstream. No second simultaneous full checkout if disk cannot fit.

- [ ] After Chromium playback passes, determine Brave source/version and apply compatible patches to its Chromium tree using the same disk budget.
- [ ] Build/validate Brave with separate test profile if capacity permits; otherwise retain complete Chromium test deliverable and record exact concrete storage blocker without altering partitions.
