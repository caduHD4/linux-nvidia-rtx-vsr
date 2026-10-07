#!/usr/bin/env python3
"""Apply the complete VSR delta after the pinned official Brave patches."""
import argparse
import json
from pathlib import Path
import subprocess

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Dedicated Brave Chromium src directory')
    args = parser.parse_args()
    source = args.source.expanduser().resolve()
    pins = json.loads((ROOT / 'brave/version.json').read_text())
    try:
        validate_source(source, pins)
        status = apply_patch(source, ROOT / 'brave/patches/nvidia-vsr.patch')
    except RuntimeError as error:
        parser.exit(1, str(error) + '\n')
    print(f'Brave NVIDIA VSR delta {status}. Build and playback validation are still required.')


if __name__ == '__main__':
    main()
