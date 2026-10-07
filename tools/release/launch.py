#!/usr/bin/env python3
"""Launch the standalone Chromium VSR preview with an external NVIDIA SDK."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parent
    info_path = root/'BUILD-INFO.json'
    try:
        info = json.loads(info_path.read_text()) if info_path.exists() else {}
        browser_kind = info.get('browser', 'chromium')
    except (OSError, ValueError, AttributeError):
        raise SystemExit('Invalid release BUILD-INFO.json.')
    identities = {'chromium': 'linux-nvidia-vsr', 'brave': 'linux-nvidia-brave-vsr'}
    if not isinstance(browser_kind, str) or browser_kind not in identities:
        raise SystemExit('Unsupported release browser identity.')
    app_id = identities[browser_kind]
    config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home()/'.config'))/app_id/'config.json'
    settings = json.loads(config.read_text()) if config.exists() else {}
    sdk = Path(os.environ.get('VFXSDK_ROOT', settings.get('sdk', '/usr/local/VideoFX'))).expanduser()
    if not sdk.is_absolute() or sdk.resolve() == Path('/'):
        raise SystemExit('Set VFXSDK_ROOT to an absolute VideoFX directory.')
    required = ['lib/libVideoFX.so', 'lib/libNVCVImage.so',
                'external/cuda/lib/libcudart.so.12',
                'features/nvvfxvideosuperres/lib/libnvVFXVideoSuperRes.so']
    missing = [name for name in required if not (sdk/name).is_file()]
    if missing:
        raise SystemExit('NVIDIA VFX SDK 1.3.0.0 is missing or incomplete at '+str(sdk)+
                         '\nInstall it using the README, then rerun install.py --sdk /path/to/VideoFX.\nMissing: '+', '.join(missing))
    if not os.environ.get('WAYLAND_DISPLAY'):
        raise SystemExit('This preview requires a Wayland desktop session.')
    if not shutil.which('nvidia-smi') or subprocess.run(
            ['nvidia-smi', '-L'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        raise SystemExit('A working NVIDIA driver is required. Check nvidia-smi.')
    target = str(os.environ.get('NVVFX_VSR_TARGET_HEIGHT', settings.get('target', '2160')))
    if target not in ('1080', '1440', '2160'):
        raise SystemExit('NVVFX_VSR_TARGET_HEIGHT must be 1080, 1440 or 2160.')
    browser = root/'browser'/('brave' if browser_kind == 'brave' else 'chrome')
    env = dict(os.environ, VFXSDK_ROOT=str(sdk.resolve()),
               NVVFX_VSR_TARGET_HEIGHT=target,
               NVVFX_VSR_SHARPNESS=os.environ.get('NVVFX_VSR_SHARPNESS', '0.35'),
               __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/10_nvidia.json',
               EGL_PLATFORM='wayland', LIBVA_DRIVER_NAME='nvidia', NVD_BACKEND='direct')
    env['LD_LIBRARY_PATH'] = ':'.join([str(root/'browser'), str(sdk/'lib'),
                                     str(sdk/'external/cuda/lib'), str(sdk/'external/tensorrt/lib'),
                                     str(sdk/'features/nvvfxvideosuperres/lib')])
    check = subprocess.run(['ldd', str(browser)], env=env, capture_output=True, text=True)
    missing_libs = [line.strip() for line in check.stdout.splitlines() if 'not found' in line]
    if check.returncode or missing_libs:
        raise SystemExit('Missing system dependencies:\n'+'\n'.join(missing_libs or [check.stderr]))
    if '--check' in sys.argv[1:]:
        print('SDK files, NVIDIA driver, Wayland and browser dependencies found. Playback is not yet verified.')
        return
    mode = os.environ.get('NVVFX_VSR_ENABLED', '1')
    if mode not in ('0', '1'):
        raise SystemExit('NVVFX_VSR_ENABLED must be 0 or 1.')
    features = 'NvidiaVsrNativeEgl,VaapiOnNvidiaGPUs,AcceleratedVideoDecodeLinuxGL'
    feature_flags = (['--enable-features=NvidiaVideoSuperResolution,'+features] if mode == '1'
                     else ['--disable-features=NvidiaVideoSuperResolution', '--enable-features='+features])
    profile = Path(os.environ.get('XDG_DATA_HOME', Path.home()/'.local/share'))/(app_id+'-profile'+('-baseline' if mode == '0' else ''))
    flags = ['--ozone-platform=wayland', '--use-gl=egl', '--use-cmd-decoder=validating',
             '--disable-gl-extensions=GL_EXT_multisampled_render_to_texture,GL_IMG_multisampled_render_to_texture',
             *feature_flags,
             '--user-data-dir='+str(profile), '--no-first-run', '--no-default-browser-check',
             '--enable-logging=stderr']
    if os.environ.get('NVVFX_VSR_SANDBOX_EXPERIMENT') == '1':
        flags = ['--gpu-sandbox-start-early', '--gpu-sandbox-failures-fatal=yes', *flags]
    else:
        print('Experimental preview: GPU sandbox is not active in the tested working mode. '
              'Not intended for everyday browsing. See README.md.', file=sys.stderr)
    os.execve(browser, [str(browser), *flags, *sys.argv[1:]], env)


if __name__ == '__main__':
    main()
