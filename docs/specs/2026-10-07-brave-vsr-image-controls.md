# Brave VSR image controls

Extend Settings > System with a 0–100 post-sharpening control (default 35), native-resolution denoise Low/Medium/High/Ultra (SDK modes 8/9/10/11; default 11), and Restore defaults (2160/35/11). Clearly label denoise as native-resolution only; upscaling remains VSR Ultra mode 4 with strength 1.0. Keep restart semantics and fullscreen/protected/HDR/source-size gates. NVIDIA documents denoise modes as non-upscaling only. No second denoise pass.

Persist integer local-state preferences. Snapshot all three settings once per browser session and forward validated child switches to renderer/GPU. Post-sharpening travels in ProcessorConfig to the GPU processor, while the legacy environment fallback remains when no explicit value exists. Reject invalid settings before resource import. Restore defaults saves explicit defaults (not an environment-reinitialized unset value). Show errors for failed saves.

Validate CPU config range/mode rules, UI controls/save/reset/restart persistence, full patch and upgrades from both published baseline and quality-selector revision, Python regressions, incremental build, and real GPU playback with non-default controls. Keep the published baseline release untouched.
