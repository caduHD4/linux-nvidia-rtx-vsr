import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('brave_apply', ROOT / 'tools/brave/apply.py')
apply = importlib.util.module_from_spec(spec)
spec.loader.exec_module(apply)


class BraveApplyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / 'src'
        self.source.mkdir()
        self.run_git('init', '-q')
        for name in ('one', 'two'):
            (self.source / name).write_text('original\n')
        self.run_git('add', '.')
        self.run_git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                     'commit', '-qm', 'base')
        for name in ('one', 'two'):
            (self.source / name).write_text('enhanced\n')
        self.patch = Path(self.temp.name) / 'change.patch'
        self.patch.write_text(self.run_git('diff').stdout)
        self.run_git('restore', '.')

    def run_git(self, *args):
        return subprocess.run(['git', '-C', str(self.source), *args],
                              capture_output=True, text=True, check=True)

    def test_complete_apply_and_repeat_preserve_unrelated_change(self):
        (self.source / 'unrelated').write_text('keep me\n')
        self.assertEqual(apply.apply_patch(self.source, self.patch), 'applied')
        before = self.run_git('diff').stdout
        self.assertEqual(apply.apply_patch(self.source, self.patch), 'already applied')
        self.assertEqual(self.run_git('diff').stdout, before)
        self.assertEqual((self.source / 'two').read_text(), 'enhanced\n')
        self.assertEqual((self.source / 'unrelated').read_text(), 'keep me\n')

    def test_partial_patch_rejected_without_finishing_it(self):
        (self.source / 'one').write_text('enhanced\n')
        before = self.run_git('diff').stdout
        with self.assertRaisesRegex(RuntimeError, 'partially applied'):
            apply.apply_patch(self.source, self.patch)
        self.assertEqual(self.run_git('diff').stdout, before)
        self.assertEqual((self.source / 'two').read_text(), 'original\n')

    def test_conflict_rejected_without_applying_clean_hunks(self):
        (self.source / 'two').write_text('Brave-specific change\n')
        before = self.run_git('diff').stdout
        with self.assertRaisesRegex(RuntimeError, 'conflicts'):
            apply.apply_patch(self.source, self.patch)
        self.assertEqual(self.run_git('diff').stdout, before)
        self.assertEqual((self.source / 'one').read_text(), 'original\n')

    def make_upgrade(self):
        apply.apply_patch(self.source, self.patch)
        for name in ('one', 'two'):
            (self.source / name).write_text('quality settings\n')
        complete = Path(self.temp.name) / 'complete.patch'
        complete.write_text(self.run_git('diff').stdout)
        self.run_git('restore', '.')
        apply.apply_patch(self.source, self.patch)
        self.run_git('add', '.')
        self.run_git('-c', 'user.name=Test', '-c', 'user.email=test@example.invalid',
                     'commit', '-qm', 'baseline')
        for name in ('one', 'two'):
            (self.source / name).write_text('quality settings\n')
        upgrade = Path(self.temp.name) / 'upgrade.patch'
        upgrade.write_text(self.run_git('diff').stdout)
        self.run_git('restore', '.')
        return complete, upgrade

    def test_baseline_upgrade_and_repeat(self):
        complete, upgrade = self.make_upgrade()
        self.assertEqual(apply.apply_complete_patch(self.source, complete, upgrade), 'upgraded')
        self.assertEqual((self.source/'one').read_text(), 'quality settings\n')
        self.assertEqual(apply.apply_complete_patch(self.source, complete, upgrade), 'already applied')

    def test_upgrade_corrupt_baseline_leaves_all_files_unchanged(self):
        complete, upgrade = self.make_upgrade()
        # A full-patch-only third file proves migration validates the entire base,
        # not just the files changed by the incremental update.
        complete.write_text(complete.read_text()+
            'diff --git a/three b/three\n--- a/three\n+++ b/three\n@@ -1 +1 @@\n-old\n+baseline\n')
        (self.source/'three').write_text('corrupt baseline\n')
        before = self.run_git('diff').stdout
        with self.assertRaises(RuntimeError):
            apply.apply_complete_patch(self.source, complete, upgrade)
        self.assertEqual(self.run_git('diff').stdout, before)
        self.assertEqual((self.source/'one').read_text(), 'enhanced\n')

    def test_wrong_chromium_pin_rejected(self):
        with self.assertRaisesRegex(RuntimeError, 'Chromium HEAD'):
            apply.validate_source(self.source, {'chromium_revision': '0' * 40})
        self.assertEqual(self.run_git('diff').stdout, '')

    def test_wrong_brave_pin_rejected(self):
        chromium = self.run_git('rev-parse', 'HEAD').stdout.strip()
        with self.assertRaisesRegex(RuntimeError, 'Brave HEAD'):
            apply.validate_source(self.source,
                                  {'chromium_revision': chromium, 'brave_revision': '0' * 40})
        self.assertEqual(self.run_git('diff').stdout, '')

    def test_unpatched_brave_rejected(self):
        revision = self.run_git('rev-parse', 'HEAD').stdout.strip()
        # A missing nested repository resolves to its parent; even matching
        # revisions must not bypass the official-patch sentinel check.
        (self.source / 'brave').mkdir()
        with self.assertRaisesRegex(RuntimeError, 'official Brave patches'):
            apply.validate_source(self.source,
                                  {'chromium_revision': revision, 'brave_revision': revision})


if __name__ == '__main__':
    unittest.main()
