import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('brave_bootstrap', ROOT / 'tools/brave/bootstrap.py')
bootstrap = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bootstrap)


class BootstrapSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.source = self.base / 'existing'
        (self.source / '.git').mkdir(parents=True)
        (self.source / 'chrome').mkdir()
        (self.source / 'chrome/VERSION').write_text('preserve\n')

    def test_separate_destination_is_accepted_without_writes(self):
        root = self.base / 'new'
        self.assertEqual(bootstrap.validate_paths(root, self.source), (root, self.source))
        self.assertFalse(root.exists())
        self.assertEqual((self.source / 'chrome/VERSION').read_text(), 'preserve\n')

    def test_nested_paths_rejected_in_both_directions(self):
        for root, source in ((self.source / 'new', self.source),
                             (self.base / 'new', self.base / 'new/src')):
            with self.subTest(root=root), self.assertRaisesRegex(ValueError, 'non-nested'):
                bootstrap.validate_paths(root, source)

    def test_existing_work_is_never_reset(self):
        root = self.base / 'new'
        root.mkdir()
        (root / 'precious').write_text('keep')
        with self.assertRaisesRegex(ValueError, 'absent or empty'):
            bootstrap.validate_paths(root, self.source)
        self.assertEqual((root / 'precious').read_text(), 'keep')

    def test_alias_to_original_rejected(self):
        alias = self.base / 'alias'
        alias.symlink_to(self.source, target_is_directory=True)
        with self.assertRaises(ValueError):
            bootstrap.validate_paths(alias, self.source)

    def test_unsupported_path_and_version_fail_explicitly(self):
        with self.assertRaisesRegex(ValueError, 'spaces'):
            bootstrap.validate_paths(self.base / 'with spaces')
        self.assertEqual(bootstrap.version_tuple('v24.16.0\n'), (24, 16, 0))
        with self.assertRaises(ValueError):
            bootstrap.version_tuple('unknown')


class BootstrapCommandsTests(unittest.TestCase):
    def test_fresh_checkout_uses_local_siso_and_runs_official_hooks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'new'
            pins = {'chromium_revision': 'a' * 40, 'brave_revision': 'b' * 40,
                    'brave_tag': 'v1.96.61'}
            calls = []

            def fake_run(*args, cwd=None):
                calls.append((args, cwd))
                if args[:2] == ('git', 'clone'):
                    Path(args[-1]).mkdir(parents=True)

            with mock.patch.object(bootstrap, 'run', side_effect=fake_run), \
                 mock.patch.object(bootstrap, 'checkout'), \
                 mock.patch.object(bootstrap.subprocess, 'check_output',
                                   return_value=pins['brave_revision'] + '\n'):
                bootstrap.bootstrap(root, None, pins)
            self.assertEqual((root / 'src/brave/.env').read_text(),
                             'use_remoteexec=false\nuse_siso=true\n')
            sync = [args for args, cwd in calls if args[:3] == ('pnpm', 'run', 'sync')]
            self.assertEqual(len(sync), 1)
            self.assertNotIn('--', sync[0])
            self.assertNotIn('--nohooks', sync[0])
