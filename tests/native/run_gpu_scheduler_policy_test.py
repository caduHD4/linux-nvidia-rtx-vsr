#!/usr/bin/env python3
"""Build only this harness; link existing Chromium component libraries."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--chromium', required=True, type=Path)
parser.add_argument('--build-dir', default='out/Vsr')
args = parser.parse_args()
src = args.chromium.resolve()
build = (src / args.build_dir).resolve()
target = 'obj/sandbox/policy/policy/bpf_gpu_policy_linux.o'
command = subprocess.check_output(
    ['ninja', '-C', str(build), '-t', 'commands', '-s', target], text=True)
lines = command.splitlines()
if len(lines) != 1:
    raise SystemExit('Expected exactly one existing GPU policy compile command.')
words = shlex.split(lines[0])
source = Path(__file__).with_name('gpu_scheduler_policy_test.cc').resolve()
with tempfile.TemporaryDirectory(prefix='gpu-policy-test-') as temporary:
    temp = Path(temporary)
    obj = temp / 'test.o'
    binary = temp / 'test'
    for flag, replacement in [('-MF', str(temp / 'test.d')), ('-o', str(obj))]:
        words[words.index(flag) + 1] = replacement
    words[words.index('-c') + 1] = str(source)
    subprocess.run(words, cwd=build, check=True)
    compiler = str((build / words[0]).resolve())
    subprocess.run(
        [compiler, '-fuse-ld=lld', '-nostdlib++', str(obj),
         '-L' + str(build), '-Wl,-rpath,' + str(build),
         '-Wl,-rpath-link,' + str(build), '-lsandbox_policy',
         '-lsandbox_linux_seccomp_bpf', '-lsandbox_linux_sandbox_services',
         '-lbase', '-lc++', '-pthread',
         '-o', str(binary)], check=True)
    env = dict(os.environ, LD_LIBRARY_PATH=str(build))
    subprocess.run([str(binary)], env=env, check=True)
