# Brave VSR image controls implementation plan

**Goal:** Native sharpness/denoise/reset controls.
**Architecture:** Extend the existing restart-bound settings snapshot and common processor config. No new per-frame pipeline.
**Spec:** docs/specs/2026-10-07-brave-vsr-image-controls.md

- [x] Add failing config validation/selection tests; implement safe modes 8–11 and explicit sharpness precedence.
- [x] Add local-state prefs, native controls/reset, and identical child switches; route settings through renderer, GPU and worker config equality.
- [x] Export complete and migration patches; test atomic updates from both older versions.
- [x] Build incrementally with terminal log; test UI persistence/reset and actual native denoise/upscale/sharpen processing; review, commit, and open for user.
