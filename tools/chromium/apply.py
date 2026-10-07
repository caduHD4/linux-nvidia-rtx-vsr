#!/usr/bin/env python3
"""Stage the pinned Chromium probe/core and experimental integration overlay."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
REVISION = "b510e9d7cd3a2fbd78d0ddc42234103206c5f78d"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--overlay", action="store_true", help="Include experimental GPU service/client integration")
args = parser.parse_args()
source = Path(os.environ.get("NVVFX_BROWSER_BUILD_ROOT", ROOT / "build/browser")).resolve() / "src"
def git(*arguments, check=True):
    return subprocess.run(["git", "-C", str(source), *arguments], check=check, capture_output=True, text=True)
if git("rev-parse", "HEAD").stdout.strip() != REVISION:
    raise SystemExit("Refusing: checkout HEAD must match the pinned Chromium revision")
patch = ROOT / "chromium/patches/chromium-current-wip.patch"
forward = git("apply", "--check", str(patch), check=False)
if forward.returncode == 0:
    git("apply", str(patch))
elif git("apply", "--reverse", "--check", str(patch), check=False).returncode != 0:
    # Overlay replaces probe files, so reverse-check only the untouched probe
    # files when recognizing an already staged integration for reapplication.
    excluded = ["--exclude=" + str(path.relative_to(ROOT / "chromium/overlay"))
                for path in sorted((ROOT / "chromium/overlay").rglob("*"))
                if path.is_file()]
    # These files are also extended by wayland-native-egl.patch. Ignore their
    # overlapping hunks when recognizing the base WIP patch as already staged.
    excluded += ["--exclude=" + path for path in (
        "ui/gl/gl_features.cc", "ui/gl/gl_features.h",
        "ui/ozone/platform/x11/x11_surface_factory.cc",
        "ui/ozone/platform/wayland/gpu/wayland_surface_factory.cc")]
    if not args.overlay or git("apply", "--reverse", "--check", *excluded,
                               str(patch), check=False).returncode != 0:
        raise SystemExit("Patch cannot be applied cleanly or recognized as already applied: " + forward.stderr)
vaapi_stubs_patch = ROOT / "chromium/patches/vaapi-component-stubs.patch"
forward = git("apply", "--check", str(vaapi_stubs_patch), check=False)
if forward.returncode == 0:
    git("apply", str(vaapi_stubs_patch))
elif git("apply", "--reverse", "--check", str(vaapi_stubs_patch),
         check=False).returncode != 0:
    raise SystemExit("VA-API component stubs patch cannot be applied or recognized: " + forward.stderr)
queue_patch = ROOT / "chromium/patches/vsr-queue-lookahead.patch"
forward = git("apply", "--check", str(queue_patch), check=False)
if forward.returncode == 0:
    git("apply", str(queue_patch))
elif git("apply", "--reverse", "--check", str(queue_patch), check=False).returncode != 0:
    raise SystemExit("VSR queue patch cannot be applied or recognized: " + forward.stderr)
if args.overlay:
    fullscreen_patch = ROOT / "chromium/patches/vsr-fullscreen-gate.patch"
    forward = git("apply", "--check", str(fullscreen_patch), check=False)
    if forward.returncode == 0:
        git("apply", str(fullscreen_patch))
    elif git("apply", "--reverse", "--check", str(fullscreen_patch), check=False).returncode != 0:
        raise SystemExit("VSR fullscreen patch cannot be applied or recognized: " + forward.stderr)
if args.overlay:
    sandbox_patch = ROOT / "chromium/patches/vsr-sandbox-startup.patch"
    forward = git("apply", "--check", str(sandbox_patch), check=False)
    if forward.returncode == 0:
        git("apply", str(sandbox_patch))
    elif git("apply", "--reverse", "--check", str(sandbox_patch), check=False).returncode != 0:
        raise SystemExit("VSR sandbox startup patch cannot be applied or recognized: " + forward.stderr)

opaque_metadata_patch = ROOT / "chromium/patches/opaque-frame-metadata.patch"
if opaque_metadata_patch.is_file():
    forward = git("apply", "--check", str(opaque_metadata_patch), check=False)
    if forward.returncode == 0:
        git("apply", str(opaque_metadata_patch))
    elif git("apply", "--reverse", "--check", str(opaque_metadata_patch),
             check=False).returncode != 0:
        raise SystemExit(
            "Opaque-frame metadata patch cannot be applied cleanly or recognized as already applied: " + forward.stderr)
wayland_native_egl_patch = ROOT / "chromium/patches/wayland-native-egl.patch"
if wayland_native_egl_patch.is_file():
    forward = git("apply", "--check", str(wayland_native_egl_patch), check=False)
    if forward.returncode == 0:
        git("apply", str(wayland_native_egl_patch))
    elif (git("apply", "--reverse", "--check", str(wayland_native_egl_patch),
              check=False).returncode != 0 and
          not all(marker in (source / path).read_text()
                  for path, marker in (
                      ("ui/gl/gl_features.cc", "BASE_FEATURE(kNvidiaVsrNativeEgl"),
                      ("ui/gl/gl_features.h", "BASE_DECLARE_FEATURE(kNvidiaVsrNativeEgl)"),
                      ("ui/ozone/platform/wayland/gpu/wayland_surface_factory.cc",
                       "kNvidiaVsrNativeEgl"),
                      ("ui/ozone/platform/x11/x11_surface_factory.cc",
                       "kNvidiaVsrNativeEgl")))):
        raise SystemExit(
            "Wayland native-EGL patch cannot be applied cleanly or recognized as already applied: " + forward.stderr)
subprocess.run(["bash", str(ROOT / "tools/chromium/stage-core.sh")], check=True)
if args.overlay:
    overlay = ROOT / "chromium/overlay"
    for path in sorted(overlay.rglob("*")):
        if path.is_file():
            target = source / path.relative_to(overlay)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    print("Experimental integration staged; browser tests/playback validation still required. Read docs/CLOUD_HANDOFF.md.")
else:
    print("Core + metadata probe staged. No browser pixel processing yet.")
