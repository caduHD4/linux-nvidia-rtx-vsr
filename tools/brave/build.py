#!/usr/bin/env python3
"""Build the pinned experimental Brave VSR browser with bounded parallelism."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess

PROJECT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(os.environ.get('NVVFX_BRAVE_BUILD_ROOT', PROJECT/'build/brave')))
    parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('VFXSDK_ROOT', '/usr/local/VideoFX')))
    parser.add_argument('--jobs', type=int, default=6)
    args = parser.parse_args()
    source = args.root.expanduser().resolve()/'src'
    sdk = args.sdk.expanduser().resolve()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if not (sdk/'include/nvVideoEffects.h').is_file():
        parser.error('Install the official NVIDIA VideoFX Core headers first; use --sdk /path/to/VideoFX.')
    subprocess.run(['python3', str(PROJECT/'tools/brave/apply.py'), '--source', str(source)], check=True)
    node, pnpm = shutil.which('node'), shutil.which('pnpm')
    if not node or not pnpm:
        parser.error('Node24.16.x and pnpm11.11+ must be available on PATH.')
    brave = source/'brave'
    env = dict(os.environ, DEPOT_TOOLS_UPDATE='0')
    check = subprocess.run([node, '--input-type=module', '-e',
        "import config from './build/commands/lib/config.ts';process.exit(config.useSiso ? 0 : 1)"],
        cwd=brave, env=env)
    if check.returncode:
        parser.error('Pinned Brave requires use_siso=true in src/brave/.env (run bootstrap first).')
    gn = ['symbol_level:0', 'blink_symbol_level:0', 'v8_symbol_level:0',
          'dcheck_always_on:false', 'enable_expensive_dchecks:false',
          'enable_nvidia_vsr:true', 'enable_validating_command_decoder:true',
          'use_siso:true', 'nvidia_vsr_sdk_root:'+json.dumps(str(sdk))]
    command = [pnpm, 'run', 'build', 'Component', '-C', 'BraveVsr', '--use_remoteexec=false']
    for value in gn:
        command.extend(['--gn', value])
    command.extend(['--ninja', 'j:'+str(args.jobs)])
    subprocess.run(command, cwd=brave, env=env, check=True)


if __name__ == '__main__':
    main()
