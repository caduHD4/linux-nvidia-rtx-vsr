import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('release_package', ROOT/'tools/release/package.py')
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class ReleasePackageTest(unittest.TestCase):
    def test_brave_stage_keeps_identity_resources_and_source_notices(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root/'src'
            build = source/'out/BraveVsr'
            build.mkdir(parents=True)
            for name in ('brave', 'chrome_crashpad_handler', 'icudtl.dat', 'resources.pak',
                         'chrome_100_percent.pak', 'chrome_200_percent.pak', 'v8_context_snapshot.bin',
                         'brave_100_percent.pak', 'brave_200_percent.pak', 'brave_resources.pak'):
                (build/name).write_text(name)
            (build/'locales').mkdir()
            (build/'locales/en-US.pak').touch()
            (source/'brave').mkdir()
            (source/'brave/LICENSE').write_text('MPL-2.0 fixture')
            (source/'brave/package.json').write_text(json.dumps({'version': '1.96.61'}))
            (source/'LICENSE').write_text('Chromium fixture')
            icon = source/'chrome/app/theme/chromium/product_logo_128.png'
            icon.parent.mkdir(parents=True)
            icon.touch()
            credits = root/'credits.html'
            credits.write_text('credits fixture')
            output = root/'stage'
            argv = ['package.py', '--source', str(source), '--output', str(output),
                    '--browser', 'brave', '--version', 'fixture', '--credits', str(credits)]
            with mock.patch.object(sys, 'argv', argv), \
                 mock.patch('subprocess.check_output', return_value='a'*40+'\n'), \
                 mock.patch('subprocess.run', return_value=subprocess.CompletedProcess([], 0, '', '')):
                PACKAGE.main()
            info = json.loads((output/'BUILD-INFO.json').read_text())
            self.assertEqual(info['browser'], 'brave')
            self.assertEqual(info['brave_version'], '1.96.61')
            self.assertEqual(info['brave_revision'], 'a'*40)
            self.assertEqual((output/'browser/brave').read_text(), 'brave')
            self.assertFalse((output/'browser/chrome').exists())
            self.assertTrue((output/'browser/brave_resources.pak').is_file())
            self.assertTrue((output/'BRAVE-LICENSE').is_file())
            self.assertIn('MPL-2.0', (output/'BRAVE-SOURCE.md').read_text())
            self.assertFalse(info['nvidia_sdk_included'])
