# Brave NVIDIA VSR port

User authorization: deliver a functional Brave port autonomously; automate upstream updates only if feasible.

Use official Brave core v1.96.61 and its Chromium pin 154.0.8037.98. Preserve the working Chromium checkout/output and user profiles. Build a real Brave browser, retaining Brave UI and Shields, with a separate build output and test profile.

Transplant the integration as a unified delta against pristine Chromium, after Brave applies its own patches. Never overwrite Brave changes with complete Chromium overlay files. Stage the original backend independently; SDK headers, libraries and models stay outside Git. Resolve overlapping modifications explicitly and record both source revisions.

Keep fullscreen-only activation, target 2160 (3840x2160 for 16:9), VSR_ULTRA mode 4 / strength 1.0, denoise mode 11 at native target, sharpen 0.35, decoded-frame scheduling and protected/DRM/HDR bypass. Preserve GPU-only pixel transfer, fences, completion quarantine and fallback. The existing experimental GPU sandbox limitation remains disclosed; no claim of isolated inference without evidence.

Validate the Brave executable identity and Shields, then actual fullscreen playback with enhanced presentation selections, continuity, exit-fullscreen bypass and clean shutdown. Package/install identity must be distinct from official Brave and the Chromium preview. Do not publish an untested browser.

Investigate scheduled GitHub upstream refresh and builds using official build requirements, runner resources and external SDK availability. Only describe automatic delivery as working after an actual workflow run. If unavailable without new infrastructure, document the limitation and prioritize the local functional browser.
