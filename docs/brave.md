# Brave VSR

The port pins **Brave v1.96.61 / Chromium 154.0.8037.98**. Exact commits are in
[`brave/version.json`](../brave/version.json). It retains Brave UI and Shields,
fullscreen-only VSR, the 2160 target and sharpening 0.35. NVIDIA SDK files remain
external. The experimental GPU sandbox limitation described in the main README
also applies here; a source build alone does not validate playback.

## Build prerequisites

Use Linux x86_64, Python 3, Git, rsync, **Node >=24.16.0 <25**, and **pnpm >=11.11.0**.
On Arch/CachyOS, Cargo also needs the `libcurl-gnutls` runtime package.
Install the [official Linux build dependencies](https://github.com/brave/brave-core/blob/v1.96.61/README.md)
and NVIDIA SDK Core + VideoSuperRes 1.3.0.0 as described in the main README.
Allow substantial disk space for sources, dependencies and a separate build;
a fresh checkout downloads many gigabytes. The bootstrap stops rather than
resetting an existing destination.

## Prepare sources

From this repository, choose a new dedicated directory with no spaces:

```bash
export NVVFX_BRAVE_BUILD_ROOT="$HOME/Builds/brave/brave-vsr"
export VFXSDK_ROOT="$HOME/.local/opt/nvidia-vfx/VideoFX"
python3 tools/brave/bootstrap.py --root "$NVVFX_BRAVE_BUILD_ROOT"
```

To avoid downloading existing Chromium dependencies again, optionally add:

```bash
python3 tools/brave/bootstrap.py --root "$NVVFX_BRAVE_BUILD_ROOT" \
  --reuse-chromium "$HOME/Builds/brave/src"
```

Run **one** preparation command. Reuse copies dependency files independently,
checks out pristine pinned Chromium files, and excludes previous build outputs,
Brave and VSR additions. Git objects are shared read-only, so keep the original
Chromium repository. Official sync reconciles dependency revisions, applies
Brave patches and runs required hooks before the VSR delta. The original
checkout, binaries and profiles are preserved. Bootstrap requires network access.
Official hooks download toolchains and other build dependencies; if an endpoint
is unavailable, preparation stops rather than claiming the checkout is ready.

The scripts disable remote execution and use Brave's supported **Siso** build
path. This pin's Ninja `redirect_cc` bootstrap fails to see required
`chromium_src` overrides; do not switch to Ninja. Additional local build settings
belong in the dedicated checkout's `src/brave/.env`. Do not add credentials or SDK
files to this repository.

To verify/reapply the complete VSR delta after an official Brave sync:

```bash
python3 tools/brave/apply.py --source "$NVVFX_BRAVE_BUILD_ROOT/src"
```

It verifies both pins and official Brave patch sentinels. A fully applied delta
is accepted; conflicts or partial application fail without applying remaining
hunks. Do not replace Brave files with the Chromium overlay.

## Compile and run

```bash
python3 tools/brave/build.py --root "$NVVFX_BRAVE_BUILD_ROOT" --sdk "$VFXSDK_ROOT" --jobs 6
NVVFX_BRAVE_BUILD_ROOT="$NVVFX_BRAVE_BUILD_ROOT" tools/brave/run-vsr.sh
```

The component build disables debug symbols, always-on DCHECKs, expensive DCHECKs and remote execution. It uses the
pinned Brave Siso build path; the older redirect_cc bootstrap fails against this
revision's patched base headers. The browser uses a separate `profiles/brave-vsr-1`
directory. `NVVFX_VSR_ENABLED=0` selects a separate baseline profile.

For actual fullscreen playback validation, generate the existing Chromium media
fixtures and point the checker at their directory:

```bash
python3 tools/chromium/playback-check.py --browser brave --media-root /path/to/media \
  --clip h264-1080p-30.mp4 --seconds 20 --require-continuous-vsr --verify-fullscreen-exit
```

The fullscreen-exit check requires at least 120 newly decoded frames and no
new enhanced-selection log events after draining. Those events are sampled
every 120 enhanced selections; this is a regression observation, not exact
per-frame output measurement. Verify Brave identity and Shields separately.

Reports appear under the dedicated build root's `validation/`. On the original
RTX 4070 SUPER host, the pinned Brave binary compiled and passed local
1080p30 fullscreen playback: decoded frames were 1920x1080 and post-warmup
selection windows were 120/120 enhanced, with no originals or switches. On
fullscreen exit, 178 additional frames decoded without new enhanced-selection
events. The GPU process reported `sandboxed=false`; inference with GPU sandbox
active remains unvalidated. Selection events are sampled every120 frames, so
the exit check is a regression signal rather than exact per-frame measurement.
The executable identifies as `Brave Browser Development 154.1.96.0`.

## In-browser quality (source builds after the first preview)

Open **Settings → System** (`brave://settings/system`) and choose **Off**, **1080p**,
**1440p**, or **4K** under **RTX Video Super Resolution**. Click **Restart Brave**
to apply it. The setting is saved for subsequent launches; it is shared by all
windows using that browser user-data directory. Enhancement remains fullscreen-only.
1080p source at the 1080p target uses native denoise; 1440p/4K supersample it.

The existing launcher preset initializes new profiles. Once saved, the in-browser
preference takes priority. The first published Brave binary preview predates this
selector; its installer still chooses the preset through `--target`.

### Image controls

The same panel includes **Sharpness** (0–100; default 35), **Native-resolution
denoise** (Low, Medium, High, Ultra; default Ultra), and **Restore defaults**.
Restore defaults saves 4K / 35 / Ultra; click **Restart Brave** to apply.
Sharpness 0 disables the extra sharpening pass. Denoise level applies only when
input and output resolution match, such as 1080p video at the 1080p target.
Upscaling keeps VSR Ultra; there is no separate denoise pass while upscaling.
Sources above 1920×1080 and up to 4096×2160 receive the selected denoise at
their original resolution, regardless of upscale target. Off disables all
enhancement. HDR and protected video continue to bypass processing.
These controls are available in source builds after the first binary preview.

## Updates

Update source pins deliberately, apply official Brave patches first, resolve VSR
patch conflicts, rebuild, and validate fullscreen playback before publishing.
Existing pins intentionally reject unreviewed upstream changes.

Automatic GitHub build/release updates are not configured. Standard public Linux
[runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
provide 4 CPU cores, 16 GB RAM and 14 GB SSD; existing Chromium dependencies alone exceed that
advertised storage. Hosted jobs also have a
[six-hour limit](https://docs.github.com/en/actions/reference/limits), and standard
runners cannot validate RTX inference. The official
[NVIDIA installation](https://docs.nvidia.com/maxine/vfx/latest/LinuxVFXSDK/InstalltheVFXSDK.html)
requires external SDK provisioning and an NGC key for features. Reliable automatic
delivery would need additional build/test infrastructure. Release notification
workflows alone would not produce a working updated browser.
