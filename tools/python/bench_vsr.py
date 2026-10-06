#!/usr/bin/env python3
"""Benchmark the NVIDIA VFX VideoSuperRes Python binding.

This is a diagnostic baseline only. Python is not part of the playback path.
"""

from __future__ import annotations

import argparse
import sys
import time

def bench(
    gpu: int,
    mode: int,
    in_w: int,
    in_h: int,
    out_w: int,
    out_h: int,
    iters: int = 15,
) -> dict[str, object]:
    import cupy as cp
    from nvvfx import VideoSuperRes

    device = cp.cuda.Device(gpu)
    with device:
        free_before, _ = cp.cuda.runtime.memGetInfo()
        image = cp.random.rand(3, in_h, in_w, dtype=cp.float32)

        effect = VideoSuperRes(quality=mode, device=gpu)
        effect.output_width = out_w
        effect.output_height = out_h
        effect.load()

        output = None
        for _ in range(3):
            output = cp.from_dlpack(effect.run(image).image)
        device.synchronize()

        start = time.perf_counter()
        for _ in range(iters):
            output = cp.from_dlpack(effect.run(image).image)
            device.synchronize()
        elapsed_ms = (time.perf_counter() - start) * 1000.0 / iters

        free_after, _ = cp.cuda.runtime.memGetInfo()
        result = {
            "gpu": gpu,
            "mode": mode,
            "in": f"{in_w}x{in_h}",
            "out": f"{out_w}x{out_h}",
            "ms_per_frame": round(elapsed_ms, 2),
            "fps_at_budget": round(1000.0 / elapsed_ms, 1),
            "vram_used_during_run_mb": (free_before - free_after) // (1024 * 1024),
            "vram_free_before_mb": free_before // (1024 * 1024),
        }
        del image, output, effect
        cp.get_default_memory_pool().free_all_blocks()
        return result


def benchmark_exit_code(*, failures: int) -> int:
    """Return success only when every requested benchmark case passed."""
    return 0 if failures == 0 else 2


def main() -> int:
    import cupy as cp

    parser = argparse.ArgumentParser()
    parser.add_argument("gpu", nargs="?", type=int, default=0)
    parser.add_argument("--quick", action="store_true")
    args = parser.parse_args()

    if cp.cuda.runtime.getDeviceCount() <= args.gpu:
        print(f"no CUDA device {args.gpu}", file=sys.stderr)
        return 1

    properties = cp.cuda.runtime.getDeviceProperties(args.gpu)
    name = properties["name"]
    if isinstance(name, bytes):
        name = name.decode()
    print(f"# GPU {args.gpu}: {name}")

    cases = (
        [(1, 1280, 720, 2560, 1440), (2, 1280, 720, 2560, 1440)]
        if args.quick
        else [
            (1, 1280, 720, 2560, 1440),
            (2, 1280, 720, 2560, 1440),
            (3, 1280, 720, 2560, 1440),
            (2, 1920, 1080, 3840, 2160),
            (3, 1920, 1080, 3840, 2160),
            (4, 1920, 1080, 3840, 2160),
        ]
    )

    successes = 0
    failures = 0
    for mode, in_w, in_h, out_w, out_h in cases:
        try:
            print(bench(args.gpu, mode, in_w, in_h, out_w, out_h))
            successes += 1
        except Exception as error:  # OOM and unsupported modes remain observable.
            message = str(error).splitlines()[0][:200]
            print(
                {
                    "gpu": args.gpu,
                    "mode": mode,
                    "in": f"{in_w}x{in_h}",
                    "out": f"{out_w}x{out_h}",
                    "error": message,
                },
                file=sys.stderr,
            )
            failures += 1

    print(f"# done: {successes} ok, {failures} failed", file=sys.stderr)
    return benchmark_exit_code(failures=failures)


if __name__ == "__main__":
    raise SystemExit(main())
