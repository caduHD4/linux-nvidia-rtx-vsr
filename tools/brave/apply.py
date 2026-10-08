#!/usr/bin/env python3
"""Apply the complete VSR delta after the pinned official Brave patches."""
import argparse
import json
from pathlib import Path
import subprocess
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def git(source, *arguments):
    return subprocess.run(['git', '-C', str(source), *arguments],
                          capture_output=True, text=True)


def validate_source(source, pins):
    for directory, key, label in ((source, 'chromium_revision', 'Chromium'),
                                  (source / 'brave', 'brave_revision', 'Brave')):
        result = git(directory, 'rev-parse', 'HEAD')
        if result.returncode or result.stdout.strip() != pins[key]:
            raise RuntimeError(f'Refusing: {label} HEAD must match {pins[key]}')
    sentinels = {
        'content/common/BUILD.gn': 'visibility += [ "//brave/content/*" ]',
        'media/base/media_switches.cc': 'BASE_OVERRIDDEN_FEATURE(kEnableTabMuting,',
    }
    for filename, marker in sentinels.items():
        path = source / filename
        if not path.is_file() or marker not in path.read_text():
            raise RuntimeError('Refusing: official Brave patches must be applied first: ' + filename)


def apply_patch(source, patch):
    """Apply all hunks or none; recognize only a completely applied patch."""
    forward = git(source, 'apply', '--check', str(patch))
    if forward.returncode == 0:
        # git apply without --reject is atomic across the complete patch.
        result = git(source, 'apply', str(patch))
        if result.returncode:
            raise RuntimeError('VSR patch application failed: ' + result.stderr.strip())
        return 'applied'
    reverse = git(source, 'apply', '--reverse', '--check', str(patch))
    if reverse.returncode == 0:
        return 'already applied'
    raise RuntimeError('Refusing: VSR patch conflicts or is partially applied; no changes made.\n'
                       + forward.stderr.strip())



def apply_complete_patch(source, complete, upgrade):
    """Accept pristine, complete, or verified baseline-only checkouts atomically."""
    try:
        return apply_patch(source, complete)
    except RuntimeError as original_error:
        candidates = [upgrade] if isinstance(upgrade, Path) else upgrade
        for candidate in candidates:
            # Validate the entire resulting tree, not just the incremental hunks.
            with tempfile.TemporaryDirectory(prefix='brave-vsr-upgrade-') as temporary:
                staged = Path(temporary)
                git(staged, 'init', '-q')
                for line in complete.read_text().splitlines():
                    if not line.startswith('diff --git '):
                        continue
                    relative = Path(line.split(' b/', 1)[1])
                    if relative.is_absolute() or '..' in relative.parts:
                        raise RuntimeError('Invalid patch path')
                    current = source / relative
                    if current.is_symlink():
                        raise original_error
                    if current.is_file():
                        destination = staged / relative
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(current, destination)
                if git(staged, 'apply', '--check', str(candidate)).returncode:
                    continue
                if git(staged, 'apply', str(candidate)).returncode:
                    continue
                if git(staged, 'apply', '--reverse', '--check', str(complete)).returncode:
                    continue
            apply_patch(source, candidate)
            return 'upgraded'
        raise original_error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Dedicated Brave Chromium src directory')
    args = parser.parse_args()
    source = args.source.expanduser().resolve()
    pins = json.loads((ROOT / 'brave/version.json').read_text())
    try:
        validate_source(source, pins)
        status = apply_complete_patch(source, ROOT / 'brave/patches/nvidia-vsr.patch',
                                      [ROOT / 'brave/patches/quality-settings-upgrade.patch',
                                       ROOT / 'brave/patches/image-controls-upgrade.patch',
                                       ROOT / 'brave/patches/high-resolution-denoise-upgrade.patch'])
    except RuntimeError as error:
        parser.exit(1, str(error) + '\n')
    print(f'Brave NVIDIA VSR delta {status}. Build and playback validation are still required.')


if __name__ == '__main__':
    main()
