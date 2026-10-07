import contextlib
import io
import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]


class ReleaseLaunchTest(unittest.TestCase):
    def launch(self, experiment=None):
        with tempfile.TemporaryDirectory(prefix='vsr launcher ') as temporary:
            root = Path(temporary)
            sdk = root/'SDK with spaces'
            for name in ('lib/libVideoFX.so', 'lib/libNVCVImage.so',
                         'external/cuda/lib/libcudart.so.12',
                         'features/nvvfxvideosuperres/lib/libnvVFXVideoSuperRes.so'):
                p = sdk/name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.touch()
            env = {'HOME': str(root), 'XDG_CONFIG_HOME': str(root/'config'),
                   'VFXSDK_ROOT': str(sdk), 'WAYLAND_DISPLAY': 'wayland-test'}
            if experiment is not None:
                env['NVVFX_VSR_SANDBOX_EXPERIMENT'] = experiment
            stderr = io.StringIO()
            with mock.patch.dict(os.environ, env, clear=True), \
                 mock.patch.object(sys, 'argv', ['launch.py']), \
                 mock.patch('shutil.which', return_value='/fake/nvidia-smi'), \
                 mock.patch('subprocess.run', return_value=subprocess.CompletedProcess([], 0, '', '')), \
                 mock.patch('os.execve') as execute, contextlib.redirect_stderr(stderr):
                runpy.run_path(str(ROOT/'tools/release/launch.py'), run_name='__main__')
            return execute.call_args.args[1], stderr.getvalue()

    def test_default_uses_tested_local_path_and_discloses_gpu_isolation(self):
        flags, warning = self.launch()
        self.assertNotIn('--gpu-sandbox-start-early', flags)
        self.assertIn('GPU sandbox is not active', warning)
        self.assertNotIn('--no-sandbox', flags)
        self.assertNotIn('--disable-gpu-sandbox', flags)

    def test_diagnostic_path_requires_early_sandbox_without_silent_fallback(self):
        flags, _ = self.launch('1')
        self.assertIn('--gpu-sandbox-start-early', flags)
        self.assertIn('--gpu-sandbox-failures-fatal=yes', flags)
