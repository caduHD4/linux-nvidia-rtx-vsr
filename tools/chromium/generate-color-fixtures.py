#!/usr/bin/env python3
"""Generate synthetic H.264 color/range fixtures for diagnostic decoder tests.

The files are test-pattern media only. The RGB decode below is a CPU-side
round-trip check of the fixtures, not part of Chromium's playback path.
"""

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import NamedTuple


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DURATION_SECONDS = 12
FRAME_RATE = 30
ENCODER_THREADS = 2
ROUNDTRIP_TOLERANCE = 4

# Left-to-right across two rows: midtones exercise matrix differences; the
# second row exposes saturation clipping and full/limited black/white levels.
COLORS = (
    ('warm_mid', (180, 60, 30)),
    ('cyan_mid', (25, 150, 200)),
    ('green_mid', (50, 180, 65)),
    ('magenta_mid', (190, 100, 180)),
    ('red_saturated', (255, 0, 0)),
    ('blue_saturated', (0, 0, 255)),
    ('black', (0, 0, 0)),
    ('white', (255, 255, 255)),
)
GRID_COLUMNS = 4
GRID_ROWS = 2


class Fixture(NamedTuple):
    filename: str
    width: int
    height: int
    matrix: str
    range_name: str
    primaries: str
    transfer: str


FIXTURES = (
    Fixture('h264-709-limited-720p-30.mp4', 1280, 720, 'bt709', 'tv',
            'bt709', 'bt709'),
    Fixture('h264-709-full-720p-30.mp4', 1280, 720, 'bt709', 'pc',
            'bt709', 'bt709'),
    Fixture('h264-601-limited-720p-30.mp4', 1280, 720, 'smpte170m', 'tv',
            'bt709', 'bt709'),
    Fixture('h264-709-limited-1080p-30.mp4', 1920, 1080, 'bt709', 'tv',
            'bt709', 'bt709'),
)


def make_ppm(width, height):
    """Return a P6 PPM header and RGB bytes for the 4x2 equal-cell grid."""
    if width % GRID_COLUMNS or height % GRID_ROWS:
        raise ValueError('image dimensions must divide evenly into a 4x2 grid')
    header = f'P6\n{width} {height}\n255\n'.encode('ascii')
    pixels = b''.join(
        b''.join(bytes(rgb) * (width // GRID_COLUMNS)
                 for _, rgb in COLORS[row * GRID_COLUMNS:(row + 1) * GRID_COLUMNS])
        * (height // GRID_ROWS) for row in range(GRID_ROWS))
    return header, pixels


def encode_command(ffmpeg, source, output, fixture):
    vf = (f'scale=in_range=pc:out_color_matrix={fixture.matrix}:'
          f'out_range={fixture.range_name},format=yuv420p')
    return [
        ffmpeg, '-hide_banner', '-loglevel', 'warning', '-nostdin',
        '-filter_threads', '1',
        '-loop', '1', '-framerate', str(FRAME_RATE), '-i', str(source),
        '-vf', vf, '-t', str(DURATION_SECONDS), '-an',
        '-c:v', 'libx264', '-preset', 'veryfast', '-crf', '10',
        '-profile:v', 'high', '-level:v', '4.0', '-pix_fmt', 'yuv420p',
        '-threads', str(ENCODER_THREADS),
        '-x264-params',
        f'colorprim={fixture.primaries}:transfer={fixture.transfer}:'
        f'colormatrix={fixture.matrix}',
        '-color_primaries', fixture.primaries,
        '-color_trc', fixture.transfer,
        '-colorspace', fixture.matrix,
        '-color_range', fixture.range_name,
        '-movflags', '+faststart', '-y', str(output),
    ]


def browser_build_root(environ=None):
    env = os.environ if environ is None else environ
    root = env.get('NVVFX_BROWSER_BUILD_ROOT') or PROJECT_ROOT / 'build/browser'
    return Path(root).expanduser().resolve()


def default_output_dir(environ=None):
    return browser_build_root(environ) / 'media'


def allowed_output(path, build_root=None):
    resolved = path.resolve()
    root = browser_build_root() if build_root is None else build_root.resolve()
    allowed_roots = (root / 'media', root / 'validation')
    return any(resolved == allowed or allowed in resolved.parents
               for allowed in allowed_roots)


def run(command):
    subprocess.run(command, check=True)


def probe(ffprobe, clip):
    result = subprocess.check_output([
        ffprobe, '-v', 'error', '-select_streams', 'v:0',
        '-show_streams', '-show_format', '-of', 'json', str(clip),
    ], text=True)
    return json.loads(result)


def verify_stream(ffprobe, clip, fixture):
    result = probe(ffprobe, clip)
    stream = result['streams'][0]
    fmt = result['format']
    expected = {
        'codec_name': 'h264',
        'profile': 'High',
        'level': 40,
        'width': fixture.width,
        'height': fixture.height,
        'avg_frame_rate': '30/1',
        'color_range': fixture.range_name,
        'color_space': fixture.matrix,
        'color_primaries': fixture.primaries,
        'color_transfer': fixture.transfer,
    }
    mismatches = {key: (stream.get(key), value)
                  for key, value in expected.items()
                  if stream.get(key) != value}
    allowed_pix_fmts = ('yuvj420p', 'yuv420p') if fixture.range_name == 'pc' \
        else ('yuv420p',)
    if stream.get('pix_fmt') not in allowed_pix_fmts:
        mismatches['pix_fmt'] = (stream.get('pix_fmt'), allowed_pix_fmts)
    if 'mp4' not in fmt.get('format_name', '').split(','):
        mismatches['container'] = (fmt.get('format_name'), 'mp4')
    duration = float(fmt.get('duration', 'nan'))
    if not math.isfinite(duration) or abs(duration - DURATION_SECONDS) > 0.1:
        mismatches['duration'] = (duration, DURATION_SECONDS)
    if mismatches:
        raise RuntimeError(f'{clip.name} has unexpected stream metadata: {mismatches}')
    return {
        'codec': stream['codec_name'],
        'profile': stream['profile'],
        'width': stream['width'],
        'height': stream['height'],
        'pixel_format': stream['pix_fmt'],
        'frame_rate': stream['avg_frame_rate'],
        'color_range': stream['color_range'],
        'matrix': stream['color_space'],
        'color_primaries': stream['color_primaries'],
        'transfer': stream['color_transfer'],
        'container': fmt['format_name'],
        'duration_seconds': duration,
    }


def decoded_first_frame(ffmpeg, clip, fixture):
    return subprocess.check_output([
        ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin',
        '-threads', str(ENCODER_THREADS), '-filter_threads', '1',
        '-i', str(clip),
        '-frames:v', '1', '-pix_fmt', 'rgb24', '-f', 'rawvideo', 'pipe:1',
    ])


def verify_roundtrip(ffmpeg, clip, fixture):
    decoded = decoded_first_frame(ffmpeg, clip, fixture)
    expected_size = fixture.width * fixture.height * 3
    if len(decoded) != expected_size:
        raise RuntimeError(
            f'{clip.name}: decoded {len(decoded)} bytes, expected {expected_size}')
    samples = []
    max_error = 0
    for index, (name, expected) in enumerate(COLORS):
        row, col = divmod(index, GRID_COLUMNS)
        x = (2 * col + 1) * fixture.width // (2 * GRID_COLUMNS)
        y = (2 * row + 1) * fixture.height // (2 * GRID_ROWS)
        offset = (y * fixture.width + x) * 3
        actual = tuple(decoded[offset:offset + 3])
        error = max(abs(actual[channel] - expected[channel])
                    for channel in range(3))
        max_error = max(max_error, error)
        if error > ROUNDTRIP_TOLERANCE:
            raise RuntimeError(
                f'{clip.name}: {name} RGB {actual} differs from {expected} '
                f'by {error} (tolerance {ROUNDTRIP_TOLERANCE})')
        samples.append({'cell': name, 'expected_rgb': list(expected),
                        'decoded_rgb': list(actual), 'max_channel_error': error})
    return {'max_channel_error': max_error, 'samples': samples}


def metadata(fixture):
    return {
        'file': fixture.filename,
        'source_ppm': f'color-grid-{fixture.width}x{fixture.height}.ppm',
        'width': fixture.width,
        'height': fixture.height,
        'frame_rate_fps': FRAME_RATE,
        'duration_seconds': DURATION_SECONDS,
        'codec': 'h264',
        'profile': 'High',
        'level': '4.0',
        'pixel_format': ('yuvj420p or yuv420p' if fixture.range_name == 'pc'
                         else 'yuv420p'),
        'matrix': fixture.matrix,
        'range_ffmpeg': fixture.range_name,
        'range_description': 'limited/tv' if fixture.range_name == 'tv'
                             else 'full/pc',
        'color_primaries': fixture.primaries,
        'transfer': fixture.transfer,
        'grid': {
            'columns': GRID_COLUMNS,
            'rows': GRID_ROWS,
            'cell_width': fixture.width // GRID_COLUMNS,
            'cell_height': fixture.height // GRID_ROWS,
            'cells': [{'name': name, 'rgb': list(rgb),
                       'row': index // GRID_COLUMNS,
                       'column': index % GRID_COLUMNS,
                       'x': (index % GRID_COLUMNS) *
                            (fixture.width // GRID_COLUMNS),
                       'y': (index // GRID_COLUMNS) *
                            (fixture.height // GRID_ROWS),
                       'center_x_fraction':
                           (index % GRID_COLUMNS + 0.5) / GRID_COLUMNS,
                       'center_y_fraction':
                           (index // GRID_COLUMNS + 0.5) / GRID_ROWS}
                      for index, (name, rgb) in enumerate(COLORS)],
        },
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--ffmpeg', default='ffmpeg')
    parser.add_argument('--ffprobe', default='ffprobe')
    args = parser.parse_args(argv)

    output_dir = (args.output_dir.expanduser() if args.output_dir
                  else default_output_dir())
    build_root = browser_build_root()
    if not allowed_output(output_dir, build_root):
        parser.error(f'output must be under {build_root}/media or '
                     f'{build_root}/validation')
    ffmpeg = shutil.which(args.ffmpeg)
    ffprobe = shutil.which(args.ffprobe)
    if not ffmpeg or not ffprobe:
        parser.error('both ffmpeg and ffprobe must be available on PATH')

    output_dir.mkdir(parents=True, exist_ok=True)
    roundtrips = {}
    probe_results = {}
    for width, height in sorted({(f.width, f.height) for f in FIXTURES}):
        source = output_dir / f'color-grid-{width}x{height}.ppm'
        header, pixels = make_ppm(width, height)
        source.write_bytes(header + pixels)

    for fixture in FIXTURES:
        source = output_dir / f'color-grid-{fixture.width}x{fixture.height}.ppm'
        output = output_dir / fixture.filename
        reused = False
        if output.is_file():
            try:
                probe_results[fixture.filename] = verify_stream(
                    ffprobe, output, fixture)
                roundtrips[fixture.filename] = verify_roundtrip(
                    ffmpeg, output, fixture)
                reused = True
                print(f'Reusing verified {output.name}', flush=True)
            except (OSError, ValueError, KeyError, RuntimeError,
                    subprocess.CalledProcessError, json.JSONDecodeError):
                pass
        if not reused:
            print(f'Generating {output.name}', flush=True)
            run(encode_command(ffmpeg, source, output, fixture))
            probe_results[fixture.filename] = verify_stream(
                ffprobe, output, fixture)
            roundtrips[fixture.filename] = verify_roundtrip(
                ffmpeg, output, fixture)

    golden = {
        'purpose': 'synthetic CPU-verified color/range decode fixtures; not browser playback evidence',
        'roundtrip': {
            'decoder': 'FFmpeg software decode to rgb24',
            'max_channel_error_tolerance': ROUNDTRIP_TOLERANCE,
            'results': roundtrips,
        },
        'ffprobe': probe_results,
        'fixtures': [metadata(fixture) for fixture in FIXTURES],
    }
    golden_path = output_dir / 'color-fixtures-golden.json'
    golden_path.write_text(json.dumps(golden, indent=2) + '\n', encoding='utf-8')
    print(f'Wrote verified fixture metadata: {golden_path}', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
