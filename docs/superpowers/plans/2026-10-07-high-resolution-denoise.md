# High-resolution denoise implementation plan

**Goal:** Native-resolution denoise for 1440p/4K fullscreen video.
**Architecture:** Raise browser eligibility cap, select native config above 1080p, preserve all existing gates and GPU pipeline.
**Spec:** docs/specs/2026-10-07-high-resolution-denoise.md

- [x] Add failing native high-resolution config/eligibility tests; implement bounded source policy.
- [x] Export full patch and migration candidates; verify CPU/Python and actual source updates.
- [x] Incremental build and visible log; verify 1440p/4K native denoise and baseline upscaling, review and open browser.
