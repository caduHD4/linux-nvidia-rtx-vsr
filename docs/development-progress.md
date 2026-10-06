# SDD ledger — plan: docs/superpowers/plans/2026-10-05-chromium-nvidia-vsr.md

Authorization: user approved design and explicitly requested complete inline execution without further questions.
Pre-flight: Task 1 ProcessorConfig/CudaFrameView -> Task 4 GPU backend; matching explicit context and pitched RGBA8.
Pre-flight: Task 3 cache and Task 4 IPC share generation/frame ID/exported image lifetime; scheduler originals remain authoritative.
Pre-flight: Task 2 checkout -> Tasks 3/4 modifications; exact revision fixed before patch creation.
Ruling: start on stable Chromium 154 release rather than main 157 — smaller API churn and matches installed Brave major — if wrong, adapters need rebase.
GPU revalidation: ultra 720p->1080p passed on RTX 4070 SUPER, 10.389 ms/frame.
Task 1: policy RED missing SelectProcessorConfig/ValidProcessorConfig definitions; GREEN CPU 4/4.
Task 1: processor GPU RED missing VsrProcessor symbols; GREEN mode4/mode11 output-channel/alpha/invalid-pitch/busy/drain checks.
Task 1: GPU CTest 6/6 passed; 10,000-frame stress running separately. Initial timings are not playback guarantees.
Ruling: checkout root is Builds/brave/src, not Builds/brave/chromium/src — gclient was configured at build root; no extra nested checkout required — cost if wrong: update tooling paths.

Task 1: complete — commit a7193a3; CPU 5/5 and core GPU 10,000 frames each mode.
Task 3 preparatory cache: RED undefined cache methods -> GREEN repeated refresh/late completion/stale generation/capacity. Chromium adapter still pending.
Task 4 standalone bridge: RED missing bridge implementation; initial GPU test default EGL selected llvmpipe and correctly failed CUDA GL 219. Explicit NVIDIA GLVND vendor plus EGL_PLATFORM=x11 selected RTX 4070 SUPER and passed mode4/mode11 GL->CUDA->VFX->GL 30 iterations each.
Ruling: pin NVIDIA EGL vendor for native interop diagnostic — default dispatch selected Mesa software — cost if wrong: explicit vendor may not generalize to another installation; test launcher scoped to this host.

Task 4 native GLES interop: explicit NVIDIA EGL GLES3 context passed both modes as well as desktop GL.
Ruling: standalone bridge validation runs before Chromium baseline completes — independent native-resource gate can be tested while depot_tools downloads — no claim that Chromium backing/thread/sandbox gate has passed.
Ruling: bootstrap infrastructure downloaded hundreds of MiB for >12 minutes; terminate only our parent orchestrator (not CIPD child) and prefetch pinned Chromium source directly with Git in parallel. Reuse the same src checkout for gclient dependencies — avoids duplicate checkout and tool-download serialization.
Native GL/GLES private texture sharing across two EGL contexts and a separate worker thread passed for both modes. Diagnostic owns original GL context on main test thread and initializes VFX/bridge only on worker.
Fetch investigation: v2 capability GET and narrow ls-refs POST return in <1 s. Packet trace confirms tag fetch reaches want-ref/deepen1/done; awaiting server pack generation, not initial API access. Keep one src checkout; do not duplicate.

Fetch unblocked: server generated shallow stable pack after ~8 minutes; src pack now downloading.
Processor instrumentation: RED first Drain left timing at zero; GREEN event elapsed time recorded after successful drain; full GPU/CPU CTest 9/9. Missing-runtime test now exercises a valid current CUDA context.

Task 2 source checkout completed on b510e9d7cd3a2fbd78d0ddc42234103206c5f78d, branch feature/linux-nvidia-vsr. gclient dependencies in progress. Checkout script now reuses an already verified commit instead of fetching a second time.
Task 3 metadata admission RED missing header -> GREEN protection-first, CPU, HDR/10-bit, geometry, alpha/color/above-target bypass; full suite 10/10. Chromium mapping/import tests pending.

Task 2 dependencies/hooks completed; GN 34,297 targets generated. Probe/cache Chromium object build underway. First full build required generated DAWN_VERSION; ran exact upstream metadata hooks and resumed selected objects.
Ruling: native EGL must use the existing validating command decoder, compile enable_validating_command_decoder=true — passthrough explicitly CHECKs ANGLE for WebGL validation — retain that security CHECK unchanged; cost: additional GPU objects/build time.
Core GN RED official SDK structs rejected by Chromium raw_ptr plugin because installed outside third_party. Add narrowly scoped SDK-path plugin exclusion, preserving proprietary ABI and checks on browser adapter.
Ruling: private input AND output GL staging with GPU publication copy is preferable to registering the renderer output SharedImage — prevents CUDA registrations outliving renderer leases — cost: one additional GPU copy. Source-release fence will cover staging copy only, not the entire VFX job, to prevent a late enhancement from blocking decoder recycling through SharedImageInterface.

Task 2 native decoder build configured: use_siso=false, enable_validating_command_decoder=true, enable_nvidia_vsr=true with external SDK root. Full chrome + media_unittests build started; reuse existing objects. All downloads/tool caches explicitly scoped to browser volume.
Core GN GREEN after SDK system-header treatment and path-only raw_ptr exclusion. Dimensions extracted from smoke_config.h into own header; GPU/CPU suite remains 10/10.
Pre-sandbox preload RED missing PreloadRuntime -> GREEN loads/version-checks modules before CUDA context initialization; retains handles until process exit, no effect or image created. Full core suite 10/10. Chromium sandbox permissions and actual SDK use remain pending.
Probe renderer/cache and their test objects compiled with bundled Chromium toolchain; corrected actual metadata header path in test. Full test executable not linked/run yet.
Synthetic fixture check found FFmpeg codec options alone emitted unspecified primaries/transfer. Adding setparams filter produced explicit BT.709 primaries/transfer/matrix; regenerating all five owned clips.

Task 4 admission policy RED missing job_admission -> GREEN two-job limit, single running job, 50ms queue expiry, >100ms watchdog disables admission without freeing textures, physical-completion-only release, quarantine retention; core CTest 11/11. Controller currently independent; wire into actual service before claiming integration coverage.
User reiterated continue during full build. Browser build ~53,453 incremental steps on Ryzen 5 5600X; no browser delivery claimed.

Ruling: acknowledge source-copy completion via nonblocking GL fence polling on the owning GPU sequence, independent of the single CUDA worker queue — model initialization or an earlier VFX job must not delay decoder-surface release/SII dependencies — use separate owner/worker GL fences at the same submitted point to avoid concurrent wait/delete. Cost: one additional lightweight GL fence and polling task per admitted frame.
GPU worker source prepared in project overlay; not applied or compiled against browser yet. It receives only private native texture IDs; caches bridges, reinitializes effect on config change, skips inference after queued expiry, and conservatively retains all context/private resources after failures.

Ruling: VideoFrame::UpdateReleaseSyncToken explicitly requires ordering the previous release. A custom SyncTokenClient collects that previous token as a source-copy scheduler dependency and publishes the known source-copy token. This merges previous uses and the new copy without enqueuing a future-token wait into SharedImageInterface; output inference completion stays on an independent sequence. Receiver initialization must acknowledge both sync-point client states before this client is used.
Readonly channel-owned SharedImage metadata query prepared in overlay to reject protected resources before representation creation. Overlay remains unapplied while initial browser compilation runs.

User explicitly canceled work to upload to GitHub. Sent SIGINT to owned browser build process group 231170; Ninja confirmed interrupted at 13,270/53,453 steps. Browser has not been delivered or validated. Preserved current Chromium tracked/probe changes in chromium/patches/chromium-current-wip.patch; core is reproduced by stage-core.sh. New GPU service/client/synchronization work remains uncompiled in chromium/overlay. Source-release helper test reached expected missing-header RED; implementation compile then rejected inline virtual methods by chromium-style plugin (not fixed due cancellation). No completed browser claim.
