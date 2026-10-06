"""Environment discovery shared by NVIDIA VSR diagnostic tools."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path


_VERSION_LABELS = ("Driver Version", "KMD Version")


def parse_driver_version(text: str) -> tuple[int, int, int]:
    """Return a three-component NVIDIA driver version from an SMI banner."""
    for label in _VERSION_LABELS:
        match = re.search(rf"{re.escape(label)}:\s*(\d+(?:\.\d+){{1,2}})", text)
        if match:
            components = [int(part) for part in match.group(1).split(".")]
            components.extend([0] * (3 - len(components)))
            return tuple(components[:3])
    raise ValueError("NVIDIA driver version not found in nvidia-smi output")


def site_packages(python: Path) -> Path:
    """Ask *python* for its purelib directory instead of guessing its version."""
    completed = subprocess.run(
        [
            str(python),
            "-c",
            "import sysconfig; print(sysconfig.get_paths()['purelib'])",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return Path(completed.stdout.strip()).resolve()
