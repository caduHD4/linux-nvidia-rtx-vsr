import contextlib
import io
import json
import shutil
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
    def launch(self, experiment=None, browser=None, mode=None):
        with tempfile.TemporaryDirectory(prefix='vsr launcher ') as temporary:
            root = Path(temporary)
            launcher = root/'launch.py'
            shutil.copy2(ROOT/'tools/release/launch.py', launcher)
            if browser is not None:
                (root/'BUILD-INFO.json').write_text(json.dumps({'browser': browser}))
            sdk = root/'SDK with spaces'
            for name in ('lib/libVideoFX.so', 'lib/libNVCVImage.so',
                         'external/cuda/lib/libcudart.so.12',
                         'features/nvvfxvideosuperres/lib/libnvVFXVideoSuperRes.so'):
                p = sdk/name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.touch()
            env = {'HOME': str(root), 'XDG_CONFIG_HOME': str(root/'config'),
                   'VFXSDK_ROOT': str(sdk), 'WAYLAND_DISPLAY': 'wayland-test'}
            if mode is not None:
                env['NVVFX_VSR_ENABLED'] = mode
            if experiment is not None:
                env['NVVFX_VSR_SANDBOX_EXPERIMENT'] = experiment
            stderr = io.StringIO()
            with mock.patch.dict(os.environ, env, clear=True), \
                 mock.patch.object(sys, 'argv', ['launch.py']), \
                 mock.patch('shutil.which', return_value='/fake/nvidia-smi'), \
                 mock.patch('subprocess.run', return_value=subprocess.CompletedProcess([], 0, '', '')), \
                 mock.patch('os.execve') as execute, contextlib.redirect_stderr(stderr):
                runpy.run_path(str(launcher), run_name='__main__')
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

    def test_brave_uses_separate_profile(self):
        flags, _ = self.launch(browser='brave')
        profile = next(flag for flag in flags if flag.startswith('--user-data-dir='))
        self.assertTrue(profile.endswith('/linux-nvidia-brave-vsr-profile'))

    def test_invalid_browser_identity_is_rejected_before_launch(self):
        with self.assertRaisesRegex(SystemExit, 'Unsupported release browser identity'):
            self.launch(browser='../escape')

    def test_baseline_disables_vsr_and_uses_separate_profile_for_both_browsers(self):
        for browser, app_id in (('chromium', 'linux-nvidia-vsr'), ('brave', 'linux-nvidia-brave-vsr')):
            with self.subTest(browser=browser):
                flags, _ = self.launch(browser=browser, mode='0')
                self.assertTrue(flags[0].endswith('/browser/'+('brave' if browser == 'brave' else 'chrome')))
                self.assertIn('--disable-features=NvidiaVideoSuperResolution', flags)
                enabled = next(flag for flag in flags if flag.startswith('--enable-features='))
                self.assertNotIn('NvidiaVideoSuperResolution', enabled)
                self.assertIn('NvidiaVsrNativeEgl', enabled)
                profile = next(flag for flag in flags if flag.startswith('--user-data-dir='))
                self.assertTrue(profile.endswith('/'+app_id+'-profile-baseline'))
                active, _ = self.launch(browser=browser, mode='1')
                self.assertTrue(next(flag for flag in active if flag.startswith('--user-data-dir=')).endswith('/'+app_id+'-profile'))

    def test_invalid_mode_is_rejected(self):
        with self.assertRaisesRegex(SystemExit, 'NVVFX_VSR_ENABLED must be 0 or 1'):
            self.launch(mode='false')
