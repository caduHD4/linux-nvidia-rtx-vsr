import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[2] / 'tools/chromium/generate-color-fixtures.py'
SPEC = importlib.util.spec_from_file_location('generate_color_fixtures', SCRIPT)
fixtures = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fixtures)


class ColorFixtureTests(unittest.TestCase):
    def test_ppm_contains_expected_rgb_at_each_grid_center(self):
        width, height = 8, 4
        header, pixels = fixtures.make_ppm(width, height)
        self.assertEqual(b'P6\n8 4\n255\n', header)
        expected = (
            ((180, 60, 30), (25, 150, 200), (50, 180, 65), (190, 100, 180)),
            ((255, 0, 0), (0, 0, 255), (0, 0, 0), (255, 255, 255)),
        )
        for row, y in enumerate((1, 3)):
            for col, x in enumerate((1, 3, 5, 7)):
                offset = (y * width + x) * 3
                self.assertEqual(bytes(expected[row][col]),
                                 pixels[offset:offset + 3])

    def test_output_root_follows_checkout_or_browser_build_environment(self):
        self.assertEqual(fixtures.PROJECT_ROOT / 'build/browser',
                         fixtures.browser_build_root({}))
        with tempfile.TemporaryDirectory() as temp_root:
            root = Path(temp_root) / 'brave build'
            env = {'NVVFX_BROWSER_BUILD_ROOT': str(root)}
            self.assertEqual(root / 'media', fixtures.default_output_dir(env))
            self.assertTrue(fixtures.allowed_output(root / 'media', root))
            self.assertTrue(fixtures.allowed_output(root / 'validation', root))
            self.assertFalse(fixtures.allowed_output(root / 'src', root))


if __name__ == '__main__':
    unittest.main()
