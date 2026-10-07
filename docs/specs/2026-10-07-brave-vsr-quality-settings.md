# Brave VSR quality settings

User requested an in-browser selector and required publication of the tested baseline first. Baseline release brave-v0.1.0-preview.1 has been published, with relocated playback validation.

Add a Linux-only section to Settings > System with Off, 1080p, 1440p and 4K. Save a local-state integer preference `brave.nvidia_vsr_target_height`, default 2160. Show an explicit restart requirement and use the existing browser restart action. This iteration changes quality after browser restart, not in-flight.

Capture the preference once per browser session and propagate a validated `--nvidia-vsr-target-height` switch to renderer and GPU child processes. Both paths use a common core selector accepting the explicit height. Zero bypasses inference; malformed explicit values fail closed. Existing environment presets remain supported when the switch is absent (Chromium and older builds). Preserve fullscreen, source-size, HDR/protected/DRM gates and synchronization.

Do not change GN optimization settings, sandbox behavior, SDK distribution or GPU interop. Export a separate incremental patch after the existing Brave VSR patch, preserving the published baseline.

Verify core mode/size selection, disabled and invalid values, precedence over environment, Python regression tests, TypeScript/native compilation and real UI persistence/restart/playback. Open a terminal following the build log.
