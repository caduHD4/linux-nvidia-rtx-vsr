#!/usr/bin/env python3
"""Install an extracted preview for the current user; no root or compilation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile


def desktop_quote(value):
    return '"'+str(value).replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')+'"'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, help='External NVIDIA VideoFX directory')
    parser.add_argument('--target', choices=['1080', '1440', '2160'], default='2160')
    parser.add_argument('--uninstall', action='store_true', help='Keep SDK and browser profile')
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error('Run as your normal desktop user, without sudo.')
    source = Path(__file__).resolve().parent
    data = Path(os.environ.get('XDG_DATA_HOME', Path.home()/'.local/share')).expanduser()
    config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home()/'.config')).expanduser()/'linux-nvidia-vsr'
    bindir = Path.home()/'.local/bin'
    destination = data/'linux-nvidia-vsr'
    wrapper = bindir/'linux-nvidia-vsr'
    desktop = data/'applications/linux-nvidia-vsr.desktop'
    config_file = config/'config.json'
    ownership_file = config/'installed-files.json'
    managed = {'command': wrapper, 'desktop': desktop, 'config': config_file}
    if not data.is_absolute() or not config.is_absolute() or any('\n' in str(p) for p in (data, config, bindir)):
        parser.error('XDG paths must be absolute and contain no newlines.')
    if args.uninstall:
        if not (destination/'BUILD-INFO.json').is_file():
            parser.error('No managed installation found.')
        try:
            ownership = json.loads(ownership_file.read_text())
        except (OSError, ValueError):
            ownership = {}
        if not isinstance(ownership, dict):
            ownership = {}
        for name, p in managed.items():
            if not p.is_symlink() and p.is_file() and hashlib.sha256(p.read_bytes()).hexdigest() == ownership.get(name):
                p.unlink()
        shutil.rmtree(destination)
        ownership_file.unlink(missing_ok=True)
        print('Removed Chromium VSR. Your browser profile and NVIDIA SDK were kept.')
        return
    sdk = args.sdk or Path(os.environ.get('VFXSDK_ROOT', Path.home()/'.local/opt/nvidia-vfx/VideoFX'))
    sdk = sdk.expanduser().resolve()
    required = ['lib/libVideoFX.so', 'lib/libNVCVImage.so',
                'external/cuda/lib/libcudart.so.12',
                'features/nvvfxvideosuperres/lib/libnvVFXVideoSuperRes.so']
    if sdk == Path('/') or not all((sdk/p).is_file() for p in required):
        parser.error('Install NVIDIA VFX Core + VideoSuperRes 1.3.0.0 first; then use --sdk /path/to/VideoFX. See README.md.')
    info = source/'BUILD-INFO.json'
    if not info.is_file() or not (source/'browser/chrome').is_file():
        parser.error('Run install.py from an extracted browser release, not the source repository.')
    checksums = source/'SHA256SUMS'
    if not checksums.is_file():
        parser.error('Missing release checksums.')
    files = set()
    for p in source.rglob('*'):
        if p.is_symlink():
            parser.error('Release symlinks are not allowed: '+str(p.relative_to(source)))
        if p.is_file() and p != checksums:
            files.add(p.relative_to(source).as_posix())
        elif not p.is_file() and not p.is_dir():
            parser.error('Invalid release entry: '+str(p.relative_to(source)))
    listed = set()
    for line in checksums.read_text().splitlines():
        try:
            expected, name = line.split('  ', 1)
        except ValueError:
            parser.error('Malformed release checksums.')
        if name in listed or name not in files:
            parser.error('Invalid or duplicate release file: '+name)
        listed.add(name)
        candidate = source/name
        if not candidate.resolve().is_relative_to(source) or not candidate.is_file():
            parser.error('Invalid release file: '+name)
        with candidate.open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != expected:
            parser.error('Checksum mismatch: '+name+'. Download/extract the release again.')
    if listed != files:
        parser.error('Unlisted release files: '+', '.join(sorted(files-listed)))
    if wrapper.exists() and repr(str(destination/'launch.py')) not in wrapper.read_text():
        parser.error('An unrelated linux-nvidia-vsr command already exists; it was not overwritten.')
    data.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if not (destination/'BUILD-INFO.json').is_file() or (destination/'BUILD-INFO.json').read_bytes() != info.read_bytes():
            parser.error('A different installation exists. Close it and uninstall before replacing it.')
    else:
        with tempfile.TemporaryDirectory(prefix='.vsr-install-', dir=data) as temporary:
            staged = Path(temporary)/'app'
            shutil.copytree(source, staged, symlinks=True)
            staged.rename(destination)
    bindir.mkdir(parents=True, exist_ok=True)
    wrapper.write_text('#!/usr/bin/env python3\nimport os,sys\nos.execv(sys.executable, [sys.executable, '+repr(str(destination/'launch.py'))+', *sys.argv[1:]])\n')
    wrapper.chmod(0o755)
    config.mkdir(parents=True, exist_ok=True)
    config_file.write_text(json.dumps({'sdk': str(sdk), 'target': args.target}, indent=2)+'\n')
    config_file.chmod(0o600)
    desktop.parent.mkdir(parents=True, exist_ok=True)
    desktop.write_text('[Desktop Entry]\nType=Application\nName=Chromium RTX VSR (Experimental)\nExec='+desktop_quote(wrapper)+' %U\nIcon='+str(destination/'icon.png')+'\nTerminal=false\nCategories=Network;WebBrowser;\nStartupNotify=true\n')
    ownership_file.write_text(json.dumps({name: hashlib.sha256(p.read_bytes()).hexdigest()
                                         for name, p in managed.items()}, indent=2)+'\n')
    ownership_file.chmod(0o600)
    print('Installed. Open Chromium RTX VSR (Experimental) from your app menu.\nOr run: '+str(wrapper)+'\nUninstall: python3 '+str(destination/'install.py')+' --uninstall')


if __name__ == '__main__':
    main()
