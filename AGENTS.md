# Agent instructions

Read `docs/CLOUD_HANDOFF.md` first, then the approved design and implementation
plan linked there. This repo contains a Chromium Linux NVIDIA VSR integration with tested local
browser playback. Public binary validation is incomplete: sandbox-enabled
inference remains blocked. Brave has not been ported.

Keep Chromium pinned to b510e9d7cd3a2fbd78d0ddc42234103206c5f78d until a deliberate
port is needed. Restore the probe patch before optional WIP overlay, and stage
third_party/nvidia_vsr from this repo. Never check in Chromium checkout, build
outputs, caches, SDK headers/libraries/models, credentials or generated videos.
Use official NVIDIA headers; empty discovery fixtures cannot compile a backend.

Keep DRM/protected/HDR/unsupported frames on bypass before resource import.
Never disable sandbox or weaken Chromium ANGLE security checks. No playback
pixels may cross the CPU. Preserve safe completion/quarantine and original-frame
fallback. Test one inference per decoded frame, not display refresh.

Report compiler/test/playback evidence honestly; a core test passing does not
prove browser integration. Complete Chromium validation before Brave port.
