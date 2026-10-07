#!/usr/bin/env python3
"""Create an isolated, pinned Brave checkout; never modifies reused sources."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHROMIUM_URL = 'https://chromium.googlesource.com/chromium/src.git'
BRAVE_URL = 'https://github.com/brave/brave-core.git'


def validate_paths(root, reuse=None):
    root = root.expanduser().resolve()
    if ' ' in str(root):
        raise ValueError('Brave build paths must not contain spaces.')
    if root.exists() and (not root.is_dir() or any(root.iterdir())):
        raise ValueError('Build root must be absent or empty; existing checkouts are never reset.')
    if reuse is not None:
        reuse = reuse.expanduser().resolve()
        if root == reuse or root in reuse.parents or reuse in root.parents:
            raise ValueError('Build root and reused source must be separate, non-nested paths.')
        if not (reuse / '.git').exists() or not (reuse / 'chrome/VERSION').is_file():
            raise ValueError('--reuse-chromium must identify a Chromium src checkout.')
    return root, reuse


def version_tuple(text):
    match = re.match(r'^v?(\d+)\.(\d+)\.(\d+)(?:\s|$)', text.strip())
    if not match:
        raise ValueError('Cannot parse tool version: ' + text.strip())
    return tuple(map(int, match.groups()))


def check_tools(reuse):
    for tool in ('git', 'node', 'pnpm', *(['rsync'] if reuse else [])):
        if not shutil.which(tool):
            raise ValueError(f'Missing {tool}; install build prerequisites first (docs/brave.md).')
    node = version_tuple(subprocess.check_output(['node', '--version'], text=True))
    pnpm = version_tuple(subprocess.check_output(['pnpm', '--version'], text=True))
    if not (24, 16, 0) <= node < (25, 0, 0):
        raise ValueError('Pinned Brave requires Node >=24.16.0 and <25.0.0.')
    if pnpm < (11, 11, 0):
        raise ValueError('Pinned Brave requires pnpm >=11.11.0.')


def run(*args, cwd=None):
    subprocess.run([str(arg) for arg in args], cwd=cwd, check=True)


def checkout(source, revision, url):
    found = subprocess.run(['git', '-C', str(source), 'cat-file', '-e', revision + '^{commit}'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if found.returncode:
        run('git', '-C', source, 'fetch', '--depth=1', url, revision)
    run('git', '-C', source, 'checkout', '--detach', revision)


def bootstrap(root, reuse, pins):
    root.mkdir(parents=True, exist_ok=True)
    source = root / 'src'
    if reuse:
        # Shared Git objects are read-only; all worktree/dependency files are
        # independent copies. The original object store must remain available.
        run('git', 'clone', '--shared', '--no-checkout', reuse, source)
        run('git', '-C', source, 'remote', 'set-url', 'origin', CHROMIUM_URL)
        checkout(source, pins['chromium_revision'], CHROMIUM_URL)
        untracked = subprocess.check_output(
            ['git', '-C', str(reuse), 'ls-files', '--others', '--exclude-standard', '-z'])
        with tempfile.NamedTemporaryFile() as excluded:
            # Reuse ignored dependency trees, not source integration additions.
            excluded.write(b'/.git\0/out/\0/brave/\0/third_party/nvidia_vsr/\0')
            for filename in untracked.split(b'\0'):
                if filename:
                    excluded.write(b'/' + filename + b'\0')
            excluded.flush()
            run('rsync', '-a', '--ignore-existing', '--from0',
                '--exclude-from=' + excluded.name, str(reuse) + '/', str(source) + '/')
    else:
        source.mkdir()
        run('git', 'init', source)
        run('git', '-C', source, 'remote', 'add', 'origin', CHROMIUM_URL)
        checkout(source, pins['chromium_revision'], CHROMIUM_URL)
    brave = source / 'brave'
    run('git', 'clone', '--depth=1', '--branch', pins['brave_tag'], BRAVE_URL, brave)
    actual = subprocess.check_output(['git', '-C', str(brave), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != pins['brave_revision']:
        raise RuntimeError('Official Brave tag no longer matches the pinned revision; stopping.')
    (brave / '.env').write_text('use_remoteexec=false\nuse_siso=true\n')
    # Use Brave's supported Siso path: Ninja's redirect_cc bootstrap does not
    # see the chromium_src overrides required by this pinned Brave revision.
    # No extra "--": pnpm11 forwards it as a positional argument.
    # Full sync reconciles copied dependencies with the exact .98 DEPS pin.
    # It runs Brave/Chromium hooks and applies official Brave patches first.
    run('pnpm', 'run', 'sync', '--no-history', '--no-bootstrap', cwd=brave)
    run('python3', ROOT / 'tools/brave/apply.py', '--source', source)
    print('Pinned Brave source prepared. Build and playback validation remain required.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=os.environ.get('NVVFX_BRAVE_BUILD_ROOT'))
    parser.add_argument('--reuse-chromium', type=Path)
    args = parser.parse_args()
    if args.root is None:
        parser.error('Provide --root or NVVFX_BRAVE_BUILD_ROOT (a new dedicated build directory).')
    try:
        root, reuse = validate_paths(args.root, args.reuse_chromium)
        check_tools(reuse)
        bootstrap(root, reuse, json.loads((ROOT / 'brave/version.json').read_text()))
    except (ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
