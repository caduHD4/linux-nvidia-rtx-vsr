#!/usr/bin/env python3
"""Stage a Chromium or Brave preview. Never includes the NVIDIA SDK or user profiles."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path, help='Chromium src checkout')
    parser.add_argument('--browser', choices=['chromium', 'brave'], default='chromium')
    parser.add_argument('--build-dir', type=Path, help='Build output directory (relative to src or absolute)')
    parser.add_argument('--brave-credits', type=Path, help='Generated Brave-specific credits HTML')
    parser.add_argument('--output', required=True, type=Path, help='External staging directory')
    parser.add_argument('--version', required=True)
    parser.add_argument('--project-revision', help='Public commit with the same source tree (default HEAD)')
    parser.add_argument('--credits', required=True, type=Path, help='Generated Chromium credits HTML')
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[2]
    revision = subprocess.check_output(['git', '-C', str(project), 'rev-parse',
                                        (args.project_revision or 'HEAD')+'^{commit}'], text=True).strip()
    trees = [subprocess.check_output(['git', '-C', str(project), 'rev-parse', ref+'^{tree}'],
                                    text=True).strip() for ref in (revision, 'HEAD')]
    if trees[0] != trees[1]:
        parser.error('Public project revision must contain the same source tree as HEAD.')
    source = args.source.resolve()
    build_dir = args.build_dir or Path('out/BraveVsr' if args.browser == 'brave' else 'out/Vsr')
    build = (source/build_dir).resolve()
    executable = 'brave' if args.browser == 'brave' else 'chrome'
    brave_source = source/'brave'
    if args.browser == 'brave' and not (brave_source/'LICENSE').is_file():
        parser.error('Missing Brave source license; use a real Brave checkout.')
    stage = args.output.resolve()
    if stage.exists():
        parser.error('Output already exists; choose a fresh staging directory.')
    if stage == project or project in stage.parents:
        parser.error('Keep release binaries outside the source repository.')
    required = [executable, 'chrome_crashpad_handler', 'icudtl.dat', 'resources.pak',
                'chrome_100_percent.pak', 'chrome_200_percent.pak',
                'v8_context_snapshot.bin', 'locales']
    if args.browser == 'brave':
        required += ['brave_100_percent.pak', 'brave_200_percent.pak', 'brave_resources.pak']
    for name in required:
        if not (build/name).exists():
            parser.error('Missing browser artifact: '+name)
    if not args.credits.is_file():
        parser.error('Generate Chromium license credits first.')
    (stage/'browser').mkdir(parents=True)
    # Package only the browser's ELF dependency closure, plus its known optional
    # graphics modules. Never sweep all shared objects from a developer build.
    optional = ['libEGL.so', 'libGLESv2.so', 'libvk_swiftshader.so', 'libvulkan.so.1']
    modules = [build/name for name in [executable, 'chrome_crashpad_handler', *optional]
               if (build/name).is_file()]
    libraries = set(optional)
    forbidden = ('libcuda', 'libcudnn', 'libnpp', 'libnv', 'libVideoFX', 'libNVCV', 'libnvinfer')
    for module in modules:
        result = subprocess.run(['ldd', str(module)], capture_output=True, text=True, check=True)
        if 'not found' in result.stdout:
            parser.error('Unresolved browser dependency: '+result.stdout)
        for line in result.stdout.splitlines():
            if ' => ' not in line:
                continue
            resolved = Path(line.split(' => ', 1)[1].split(' (', 1)[0].strip())
            if resolved.is_absolute() and resolved.resolve().parent == build.resolve():
                if resolved.name.startswith(forbidden):
                    parser.error('Refusing NVIDIA runtime dependency: '+resolved.name)
                libraries.add(resolved.name)
    names = required + sorted(libraries) + ['vk_swiftshader_icd.json', 'snapshot_blob.bin'] + (['resources', 'version'] if args.browser == 'brave' else [])
    for name in dict.fromkeys(names):
        p = build/name
        if not p.exists():
            continue
        if p.is_dir():
            shutil.copytree(p, stage/'browser'/name)
        else:
            shutil.copy2(p, stage/'browser'/name, follow_symlinks=True)
    for name in ('launch.py', 'install.py'):
        shutil.copy2(project/'tools/release'/name, stage/name)
    shutil.copy2(project/'README.md', stage/'README.md')
    (stage/'docs').mkdir()
    shutil.copy2(project/'docs/distribution.md', stage/'docs/distribution.md')
    shutil.copy2(project/'LICENSE', stage/'PROJECT-LICENSE')
    shutil.copy2(source/'LICENSE', stage/'CHROMIUM-LICENSE')
    shutil.copy2(args.credits, stage/'CHROMIUM-CREDITS.html')
    icon = source/'chrome/app/theme/chromium/product_logo_128.png'
    if args.browser == 'brave':
        shutil.copy2(brave_source/'LICENSE', stage/'BRAVE-LICENSE')
        if args.brave_credits:
            shutil.copy2(args.brave_credits, stage/'BRAVE-CREDITS.html')
        (stage/'BRAVE-SOURCE.md').write_text(
            'This experimental Brave build includes modified MPL-2.0 source.\n'
            'VSR modifications and build instructions: https://github.com/caduHD4/linux-nvidia-rtx-vsr\n'
            'Upstream Brave source: https://github.com/brave/brave-core\n'
            'See BUILD-INFO.json for the exact upstream and modification revisions.\n')
        candidate = brave_source/'app/theme/brave/linux/product_logo_128.png'
        if candidate.is_file():
            icon = candidate
    shutil.copy2(icon, stage/'icon.png')
    info = {'version': args.version, 'browser': args.browser, 'chromium_revision': subprocess.check_output(
        ['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip(),
        'project_revision': revision,
        'sdk_version': '1.3.0.0', 'platform': 'Linux x86_64 Wayland',
        'nvidia_sdk_included': False,
        'release_channel': 'experimental',
        'gpu_sandbox_validated': False,
        'default_launcher_mode': 'experimental-local',
        'project_dirty': bool(subprocess.check_output(
            ['git', '-C', str(project), 'status', '--porcelain'], text=True).strip())}
    if args.browser == 'brave':
        info['brave_revision'] = subprocess.check_output(
            ['git', '-C', str(brave_source), 'rev-parse', 'HEAD'], text=True).strip()
        info['brave_version'] = json.loads((brave_source/'package.json').read_text())['version']
    (stage/'BUILD-INFO.json').write_text(json.dumps(info, indent=2)+'\n')
    lines=[]
    for p in sorted(stage.rglob('*')):
        if p.is_file():
            with p.open('rb') as stream:
                digest=hashlib.file_digest(stream, 'sha256').hexdigest()
            lines.append(digest+'  '+p.relative_to(stage).as_posix())
    (stage/'SHA256SUMS').write_text('\n'.join(lines)+'\n')
    print(stage)


if __name__ == '__main__':
    main()
