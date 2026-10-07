# Brave VSR quality settings implementation plan

**Goal:** Native quality selector in Brave Settings > System.
**Architecture:** Persist local-state preference; snapshot once at child launch; pass identical validated target switch to renderer and GPU. Apply on browser restart.
**Tech Stack:** Brave C++, Lit/TypeScript, existing VSR C++ core, Python patch tooling.
**Spec:** docs/specs/2026-10-07-brave-vsr-quality-settings.md

- [x] Publish and verify the current tested browser as a separate baseline release.
- [x] Add failing core tests for explicit target, disabled, invalid and environment precedence; implement and run.
- [x] Register/allowlist preference; add native Settings > System selector and restart control. Snapshot and forward preference to renderer/GPU; consume in both config paths.
- [x] Export quality changes into the complete atomic patch and verify pinned apply tooling; run Python regressions.
- [x] Start bounded incremental Brave build and show terminal log. Validate UI persistence, disabled bypass and enabled playback.

Maintain all previous safety and fullscreen gates. No live mutation of active inference buffers.
