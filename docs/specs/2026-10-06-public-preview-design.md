# Public preview distribution

User authorized publishing current code, relocatable NVIDIA runtime, sandbox investigation and an installable preview. Scope: Linux x86_64 Arch/CachyOS Wayland, RTX40 as the initial validated family. No NVIDIA binaries/models/credentials in Git or release assets. SDK1.3.0.0 downloaded by each user. Preserve fullscreen-only, protected/HDR bypass and4K default.

Runtime root: a shared C++ resolver uses absolute VFXSDK_ROOT if present, otherwise the compiled default. Empty or relative overrides fail closed; hook broker and worker must use the same resolver. No new arbitrary directory grants beyond the selected SDK.

Sandbox: identify driver-created threads and use Chromium's existing TSYNC support if appropriate. Preserve policies, no --no-sandbox or disabled checks; do not label a preview secure solely from SystemInfo. Read actual thread seccomp status and verify playback. If the policy cannot run the workload safely, withhold browser binary release and publish source progress with the concrete blocker.

Packaging: stage Chromium executable/resources/component shared libraries plus license notices, exclude SDK, profiles, crash dumps and fixtures. Install under XDG user directories, desktop launcher and user config for SDK path; no root needed. Fail early on missing SDK/Wayland/NVIDIA/dependencies. Browser loads only bundled browser libraries and selected SDK; system driver stays system-provided. SHA256 archive and release notes with support scope. Do not promise universal Linux compatibility or a fixed install duration.

Validation: resolver unit tests; relocated package playback with external SDK, fullscreen continuity and GPU sandbox; installer/uninstaller in temporary XDG dirs; Python/shell checks. Source commits preserve remote README history. Publish prerelease only after gates pass.


User override (2026-10-06): after the secured NGX discovery blocker was reported,
the user explicitly instructed “publique de qualquer forma!!!”. Publish the
working development path as a clearly labeled experimental prerelease with the
GPU isolation limitation disclosed. Keep early sandbox as diagnostic opt-in;
no sandbox-disabling flags or security-check removals. Validate relocated-package
inference before upload; the SDK remains external. The previous binary gate is
superseded only for this experimental release, not considered passed.
