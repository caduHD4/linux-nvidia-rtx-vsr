import sys
import sysconfig
import unittest
from pathlib import Path

from tools.python.nvidia_env import parse_driver_version, site_packages
from tools.python.bench_vsr import benchmark_exit_code


class ParseDriverVersionTests(unittest.TestCase):
    def test_parses_legacy_driver_version_label(self):
        banner = "NVIDIA-SMI 595.84  Driver Version: 595.84  CUDA Version: 13.2"

        self.assertEqual((595, 84, 0), parse_driver_version(banner))

    def test_parses_kmd_version_label(self):
        banner = "NVIDIA-SMI 610.57.04  KMD Version: 610.57.04  CUDA UMD Version: 13.3"

        self.assertEqual((610, 57, 4), parse_driver_version(banner))

    def test_prefers_driver_version_when_both_labels_exist(self):
        banner = "Driver Version: 595.84  KMD Version: 610.57.04"

        self.assertEqual((595, 84, 0), parse_driver_version(banner))

    def test_rejects_banner_without_a_driver_label(self):
        with self.assertRaisesRegex(ValueError, "driver version"):
            parse_driver_version("CUDA UMD Version: 13.3")


class SitePackagesTests(unittest.TestCase):
    def test_queries_the_selected_interpreter(self):
        expected = Path(sysconfig.get_paths()["purelib"]).resolve()

        self.assertEqual(expected, site_packages(Path(sys.executable)))


class BenchmarkExitCodeTests(unittest.TestCase):
    def test_requires_every_requested_case_to_pass(self):
        self.assertEqual(0, benchmark_exit_code(failures=0))
        self.assertNotEqual(0, benchmark_exit_code(failures=1))


if __name__ == "__main__":
    unittest.main()
