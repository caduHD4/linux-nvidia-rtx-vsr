# Linux experimental preview

The user explicitly authorized publishing the working local preview despite the
known GPU sandbox limitation. The default mode reported **GPU sandbox inactive**
on the tested host. This is for isolated evaluation, not everyday browsing.
The early-sandbox diagnostic path reaches NGX but feature discovery fails (`-14`).
This release does not claim secure GPU isolation or universal compatibility.

Initial target: Linux x86_64, Arch/CachyOS, Wayland and NVIDIA RTX 40. Python 3.11+, working NVIDIA driver, libva-nvidia-driver, and NVIDIA VFX Core + nvvfxvideosuperres 1.3.0.0 are required. Other configurations are unverified. The NVIDIA SDK is not bundled.

## End-user installation

Download the archive and its published SHA256 file from this repository's Releases. Verify the downloaded archive with `sha256sum -c <archive>.sha256`, then extract it. Install the SDK using the README, then run from the extracted package:

```bash
python3 install.py --sdk "$HOME/.local/opt/nvidia-vfx/VideoFX"
```

Open **Chromium RTX VSR (Experimental)** in the application menu. This uses a separate profile and does not replace your default browser. Select 4K (default), 1440p or 1080p with `--target 2160`, `--target 1440` or `--target 1080` when installing. Close the browser before changing its configuration. Processing runs only in fullscreen.

For **Brave RTX VSR (Experimental)**, preview.2 and newer include resolution,
sharpness and Restore defaults in **Settings → System**. Restart to apply.
Videos above 1080p use Low denoise at native resolution (up to 4096×2160);
upscaling uses VSR Ultra. Brave uses the separate `linux-nvidia-brave-vsr` app,
configuration and profile names. To update from an older preview, close the
experimental Brave, then run:

```bash
python3 "$HOME/.local/share/linux-nvidia-brave-vsr/install.py" --uninstall
```

Then run `install.py --sdk /path/to/VideoFX` from the newly extracted package.
Adjust the old installation path if you use custom XDG_DATA_HOME. Your browser
profile and NVIDIA SDK are retained.

SDK path and preset are stored in `$XDG_CONFIG_HOME/linux-nvidia-vsr/config.json` (default `~/.config`). `VFXSDK_ROOT` and `NVVFX_VSR_TARGET_HEIGHT` override them. `~/.local/bin/linux-nvidia-vsr --check` checks prerequisites; it does not prove playback/inference.

Uninstall with `python3 ~/.local/share/linux-nvidia-vsr/install.py --uninstall` (adjust for custom XDG_DATA_HOME). The NVIDIA SDK and separate browser profile are kept.

## Build the package

Build Chromium at the pinned revision and generate credits using `tools/licenses/licenses.py credits --gn-out-dir out/Vsr --gn-target //chrome:chrome /external/path/credits.html` from the Chromium checkout.

```bash
python3 tools/release/package.py --source /path/to/chromium/src \
  --output /external/path/linux-nvidia-vsr-preview \
  --version 0.1.0-preview.1 --credits /external/path/credits.html
```

When publishing a snapshot from a different local Git history, use
`--project-revision <public-commit>`; its tree must exactly match local HEAD.

Output must remain outside the source repository. It contains browser executables, component libraries/resources, project/Chromium license notices, generated credits, scripts and SHA256SUMS. It excludes NVIDIA SDK, profiles and generated test videos. Compress the resulting directory and generate a SHA256 for the archive. Each GitHub release asset must remain below 2 GiB.

## Release status

This explicitly authorized prerelease is an exception to the previous secure
release gate. Relocated-package inference must work; GPU isolation is a known
unresolved limitation, not a passed check. No NVIDIA libraries/models are bundled.
GitHub Release uploads require authenticated GitHub API access in addition to Git SSH.

## Development sandbox investigation

`NVVFX_VSR_SANDBOX_EXPERIMENT=1 tools/chromium/run-vsr.sh` opts into early
sandbox initialization and TSYNC. This is a diagnostic path, not a usable release.
The development launcher retains the previously tested local path by default;
its GPU isolation remains unvalidated. The packaged launcher also accepts this diagnostic option; when selected it
requires early sandbox initialization and fatal initialization failures.
