import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ReleaseInstallTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='vsr installer ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root/'release with spaces'
        self.source.mkdir()
        for name in ('install.py', 'launch.py'):
            shutil.copy2(ROOT/'tools/release'/name, self.source/name)
        (self.source/'browser').mkdir()
        (self.source/'browser/chrome').write_text('placeholder')
        (self.source/'BUILD-INFO.json').write_text(json.dumps({'version': 'fixture'}))
        self.sdk = self.root/'VideoFX'
        for name in ('lib/libVideoFX.so', 'lib/libNVCVImage.so',
                'external/cuda/lib/libcudart.so.12',
                     'features/nvvfxvideosuperres/lib/libnvVFXVideoSuperRes.so'):
            p = self.sdk/name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.touch()
        self.env = dict(os.environ, HOME=str(self.root/'home'),
                        XDG_DATA_HOME=str(self.root/'data with spaces'),
                        XDG_CONFIG_HOME=str(self.root/'config with spaces'))
        lines = []
        for p in sorted(self.source.rglob('*')):
            if p.is_file():
                lines.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(self.source).as_posix())
        (self.source/'SHA256SUMS').write_text('\n'.join(lines)+'\n')

    def set_browser(self, browser):
        executable = 'brave' if browser == 'brave' else 'chrome'
        for name in ('chrome', 'brave'):
            (self.source/'browser'/name).unlink(missing_ok=True)
        (self.source/'browser'/executable).write_text('placeholder')
        (self.source/'BUILD-INFO.json').write_text(json.dumps({'version': 'fixture', 'browser': browser}))
        lines = [hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.relative_to(self.source).as_posix()
                 for p in sorted(self.source.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS']
        (self.source/'SHA256SUMS').write_text('\n'.join(lines)+'\n')

    def test_brave_coexists_with_chromium_and_uninstalls_independently(self):
        self.assertEqual(self.run_installer('--sdk', str(self.sdk)).returncode, 0)
        self.set_browser('brave')
        result = self.run_installer('--sdk', str(self.sdk))
        self.assertEqual(result.returncode, 0, result.stderr)
        data = Path(self.env['XDG_DATA_HOME'])
        for app in ('linux-nvidia-vsr', 'linux-nvidia-brave-vsr'):
            executable = 'brave' if app == 'linux-nvidia-brave-vsr' else 'chrome'
            self.assertTrue((data/app/'browser'/executable).is_file())
            self.assertTrue((Path(self.env['HOME'])/'.local/bin'/app).is_file())
            self.assertTrue((data/'applications'/(app+'.desktop')).is_file())
        self.assertIn('Name=Brave RTX VSR', (data/'applications/linux-nvidia-brave-vsr.desktop').read_text())
        result = self.run_installer('--uninstall')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((data/'linux-nvidia-vsr/browser/chrome').is_file())
        self.assertFalse((data/'linux-nvidia-brave-vsr').exists())

    def test_invalid_browser_identity_does_not_write_anything(self):
        for browser in ('../../escape', 'firefox', ['brave']):
            with self.subTest(browser=browser):
                self.set_browser(browser)
                result = self.run_installer('--sdk', str(self.sdk))
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Unsupported release browser identity', result.stderr)
                self.assertFalse(Path(self.env['XDG_DATA_HOME']).exists())

    def run_installer(self, *args):
        return subprocess.run([sys.executable, str(self.source/'install.py'), *args],
                              env=self.env, text=True, capture_output=True)

    def test_install_paths_with_spaces_and_uninstall_preserves_profile(self):
        result = self.run_installer('--sdk', str(self.sdk), '--target', '1440')
        self.assertEqual(result.returncode, 0, result.stderr)
        data = Path(self.env['XDG_DATA_HOME'])
        self.assertTrue((data/'linux-nvidia-vsr/browser/chrome').is_file())
        settings = json.loads((Path(self.env['XDG_CONFIG_HOME'])/'linux-nvidia-vsr/config.json').read_text())
        self.assertEqual(settings, {'sdk': str(self.sdk), 'target': '1440'})
        profile = data/'linux-nvidia-vsr-profile'
        profile.mkdir()
        (profile/'keep').touch()
        result = self.run_installer('--uninstall')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((profile/'keep').exists())
        self.assertTrue(self.sdk.exists())
        self.assertFalse((data/'linux-nvidia-vsr').exists())
        self.assertFalse((Path(self.env['HOME'])/'.local/bin/linux-nvidia-vsr').exists())
        self.assertFalse((data/'applications/linux-nvidia-vsr.desktop').exists())
        self.assertFalse((Path(self.env['XDG_CONFIG_HOME'])/'linux-nvidia-vsr/config.json').exists())

    def test_missing_sdk_does_not_create_installation(self):
        result = self.run_installer('--sdk', str(self.root/'missing'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Install NVIDIA', result.stderr)
        self.assertFalse(Path(self.env['XDG_DATA_HOME']).exists())

    def test_tampered_archive_is_rejected(self):
        (self.source/'browser/chrome').write_text('tampered')
        result = self.run_installer('--sdk', str(self.sdk))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Checksum mismatch', result.stderr)
        self.assertFalse(Path(self.env['XDG_DATA_HOME']).exists())

    def test_conflicting_command_is_not_overwritten(self):
        wrapper = Path(self.env['HOME'])/'.local/bin/linux-nvidia-vsr'
        wrapper.parent.mkdir(parents=True)
        wrapper.write_text('unrelated')
        result = self.run_installer('--sdk', str(self.sdk))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(wrapper.read_text(), 'unrelated')

    def test_unlisted_file_is_rejected(self):
        (self.source/'unlisted').write_text('not checksummed')
        result = self.run_installer('--sdk', str(self.sdk))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(Path(self.env['XDG_DATA_HOME']).exists())

    def test_external_directory_symlink_is_rejected(self):
        (self.source/'sdk-link').symlink_to(self.sdk, target_is_directory=True)
        result = self.run_installer('--sdk', str(self.sdk))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(Path(self.env['XDG_DATA_HOME']).exists())

    def test_checksummed_file_symlink_is_rejected(self):
        chrome = self.source/'browser/chrome'
        outside = self.root/'chrome'
        chrome.rename(outside)
        chrome.symlink_to(outside)
        result = self.run_installer('--sdk', str(self.sdk))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(Path(self.env['XDG_DATA_HOME']).exists())

    def test_internal_symlink_is_rejected(self):
        (self.source/'internal-link').symlink_to('browser/chrome')
        checksums = self.source/'SHA256SUMS'
        with checksums.open('a') as stream:
            stream.write(hashlib.sha256((self.source/'browser/chrome').read_bytes()).hexdigest()+'  internal-link\n')
        result = self.run_installer('--sdk', str(self.sdk))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('symlinks', result.stderr)
        self.assertFalse(Path(self.env['XDG_DATA_HOME']).exists())

    def test_uninstall_preserves_replaced_command_desktop_and_config(self):
        result = self.run_installer('--sdk', str(self.sdk))
        self.assertEqual(result.returncode, 0, result.stderr)
        paths = [Path(self.env['HOME'])/'.local/bin/linux-nvidia-vsr',
                 Path(self.env['XDG_DATA_HOME'])/'applications/linux-nvidia-vsr.desktop',
                 Path(self.env['XDG_CONFIG_HOME'])/'linux-nvidia-vsr/config.json']
        for path in paths:
            path.write_text('replacement unrelated file')
        result = self.run_installer('--uninstall')
        self.assertEqual(result.returncode, 0, result.stderr)
        for path in paths:
            self.assertEqual(path.read_text(), 'replacement unrelated file')


if __name__ == '__main__':
    unittest.main()
