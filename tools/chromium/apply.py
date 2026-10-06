#!/usr/bin/env python3
"""Stage the tested core and probe patch; optionally restore unfinished overlay."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
REVISION = "b510e9d7cd3a2fbd78d0ddc42234103206c5f78d"
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--overlay", action="store_true", help="Include uncompiled GPU-service WIP (known compile errors)")
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
    raise SystemExit("Patch cannot be applied cleanly or recognized as already applied: " + forward.stderr)
subprocess.run(["bash", str(ROOT / "tools/chromium/stage-core.sh")], check=True)
if args.overlay:
    overlay = ROOT / "chromium/overlay"
    for path in sorted(overlay.rglob("*")):
        if path.is_file():
            target = source / path.relative_to(overlay)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
    print("Restored unfinished overlay; NOT a working VSR browser. Read docs/CLOUD_HANDOFF.md.")
else:
    print("Core + metadata probe staged. No browser pixel processing yet.")
