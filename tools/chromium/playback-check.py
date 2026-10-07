#!/usr/bin/env python3
"""Capture actual playback/GPU/media diagnostics via a local DevTools pipe."""
import argparse
import base64
import functools
import http.server
import json
import os
from pathlib import Path
import re
import select
import subprocess
import tempfile
import threading
import time

VSR_SELECTION_RE = re.compile(
    r'NVIDIA VSR selected_enhanced=(\d+) generation=(\d+) frame_id=(\d+)')


def extract_enhanced_selections(media_events):
    """Read renderer presentation selections from DevTools Media messages."""
    selections = []
    for event in media_events:
        if event.get('method') != 'Media.playerMessagesLogged':
            continue
        messages = event.get('params', {}).get('messages', [])
        for message in messages:
            match = VSR_SELECTION_RE.search(message.get('message', ''))
            if not match:
                continue
            selected_count, generation, frame_id = map(int, match.groups())
            if selected_count > 0:
                selections.append({
                    'selected_enhanced': selected_count,
                    'generation': generation,
                    'frame_id': frame_id,
                })
    return selections


PRESENTATION_RE = re.compile(
    r'NVIDIA VSR presentation total=(\d+) generation=(\d+) '
    r'enhanced_window=(\d+) original_window=(\d+) switches_window=(\d+)')


def extract_vsr_presentations(media_events):
    windows=[]
    for event in media_events:
        if event.get('method') != 'Media.playerMessagesLogged':
            continue
        for message in event.get('params',{}).get('messages',[]):
            match=PRESENTATION_RE.search(message.get('message',''))
            if match:
                windows.append(dict(zip(
                    ['total','generation','enhanced_window','original_window','switches_window'],
                    map(int,match.groups())),player_id=event['params'].get('playerId','')))
    return windows


def validate_vsr_continuity(windows):
    warmed=set()
    steady=[]
    for window in windows:
        key=(window.get('player_id',''),window.get('generation',0))
        if key not in warmed:
            warmed.add(key)
        else:
            steady.append(window)
    if not steady:
        raise RuntimeError('no post-warmup presentation window to verify continuity')
    if any(window['original_window'] for window in steady):
        raise RuntimeError('VSR is not continuous: original frames presented after warmup')


def validate_vsr_selection(selections, minimum=120, browser_log=''):
    if 'NVIDIA VSR watchdog disabled session' in browser_log:
        raise RuntimeError('VSR watchdog disabled the session during playback')
    if not any(item['selected_enhanced'] > 0 for item in selections):
        raise RuntimeError(
            'no enhanced frame was selected for presentation; inspect Media messages and browser.log')
    count = max(item['selected_enhanced'] for item in selections)
    if count < minimum:
        raise RuntimeError(
            f'insufficient enhanced frames: selected={count}, required={minimum}; '
            'a warmup selection does not prove sustained processing')


class DevTools:
    def __init__(self, read_fd, write_fd):
        self.read_fd, self.write_fd = read_fd, write_fd
        self.pending = b''
        self.sequence = 0
        self.events = []

    def call(self, method, params=None, session=None, timeout=30):
        self.sequence += 1
        request = {'id': self.sequence, 'method': method, 'params': params or {}}
        if session:
            request['sessionId'] = session
        wire = json.dumps(request).encode() + b'\0'
        while wire:
            wire = wire[os.write(self.write_fd, wire):]
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if b'\0' not in self.pending:
                ready, _, _ = select.select([self.read_fd], [], [],
                                             max(0, deadline-time.monotonic()))
                if not ready:
                    break
                chunk = os.read(self.read_fd, 65536)
                if not chunk:
                    raise RuntimeError('browser closed DevTools pipe')
                self.pending += chunk
                continue
            wire, self.pending = self.pending.split(b'\0', 1)
            if not wire:
                continue
            message = json.loads(wire)
            if message.get('id') == request['id']:
                if 'error' in message:
                    raise RuntimeError(f'{method}: {message["error"]}')
                return message.get('result', {})
            if 'method' in message:
                self.events.append(message)
        raise TimeoutError(method)


    def wait_event(self, method, session, after=0, timeout=30):
        def matches(event):
            return event.get('method') == method and event.get('sessionId') == session
        for event in self.events[after:]:
            if matches(event):
                return event
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if b'\0' not in self.pending:
                ready, _, _ = select.select([self.read_fd], [], [],
                                             max(0, deadline-time.monotonic()))
                if not ready:
                    break
                chunk = os.read(self.read_fd, 65536)
                if not chunk:
                    raise RuntimeError('browser closed DevTools pipe')
                self.pending += chunk
                continue
            wire, self.pending = self.pending.split(b'\0', 1)
            if not wire:
                continue
            event = json.loads(wire)
            if 'method' in event:
                self.events.append(event)
                if matches(event):
                    return event
        raise TimeoutError(method)


def validate_browser_health(browser_log):
    if 'GPU process exited unexpectedly' in browser_log:
        raise RuntimeError('GPU process crashed during playback; software recovery is not a pass')


def validate_playback(samples, mse):
    if len(samples) < 2 or any(sample.get('error') or sample['paused'] for sample in samples):
        raise RuntimeError('playback errored or paused')
    first, previous, last = samples[0], samples[-2], samples[-1]
    if (last['decoded'] <= first['decoded'] or last['decoded'] <= previous['decoded'] or
            last['time'] == previous['time'] or not last['width'] or not last['height']):
        raise RuntimeError('playback did not continue advancing')
    if mse and not {(1280, 720), (1920, 1080)}.issubset(
            {(sample['width'], sample['height']) for sample in samples}):
        raise RuntimeError('MSE did not present both 720p and 1080p configurations')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--require-continuous-vsr', action='store_true',
                        help='Require zero original frames in complete 120-frame windows after each generation warmup')
    parser.add_argument('--require-vsr', action='store_true',
                        help='Require sustained enhanced-frame selections with no watchdog shutdown')
    parser.add_argument('--min-enhanced-frames', type=int, default=120,
                        help='Minimum renderer selection counter required by --require-vsr (default: 120)')
    parser.add_argument('--fullscreen', action='store_true',
                        help='Request player fullscreen (implied by --require-vsr)')
    parser.add_argument('--capture-frame', action='store_true',
                        help='Save a diagnostic screenshot of the video for color and visual comparisons')
    parser.add_argument('--clip', default='h264-720p-30.mp4')
    parser.add_argument('--mse', action='store_true', help='Exercise 720p to 1080p configuration change')
    parser.add_argument('--seconds', type=float, default=15)
    args = parser.parse_args()
    if args.require_continuous_vsr:
        args.require_vsr = True
    if args.min_enhanced_frames < 1:
        parser.error('--min-enhanced-frames must be positive')
    if args.baseline and args.require_vsr:
        parser.error('--require-vsr cannot be used with --baseline')
    if args.mse and args.seconds < 14:
        parser.error('--mse needs at least 14 seconds to observe the change at 12 seconds')
    if not 2 <= args.seconds <= 300:
        parser.error('--seconds must be between 2 and 300')
    project = Path(__file__).resolve().parents[2]
    build = Path(os.environ.get('NVVFX_BROWSER_BUILD_ROOT', project/'build/browser'))
    media = build/'media'
    if args.mse:
        args.clip = 'mse'
    required = ['mse-720p-30.mp4', 'mse-1080p-30.mp4'] if args.mse else [args.clip]
    if Path(args.clip).name != args.clip or not all((media/name).is_file() for name in required):
        parser.error('generate fixtures first; --clip must name a file in media/')
    browser = build/'src/out/Vsr/chrome'
    if not browser.is_file():
        parser.error('compile Chromium first')
    mode = 'baseline' if args.baseline else 'vsr'
    results = build/'validation'/f'{mode}-{time.time_ns()}'
    results.mkdir(parents=True)
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(media))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    browser_read, control_write = os.pipe()
    control_read, browser_write = os.pipe()
    process = None
    report = {'mode': mode, 'clip': args.clip, 'samples': []}
    try:
        with tempfile.TemporaryDirectory(prefix='vsr-profile-', dir=build) as profile, \
             (results/'browser.log').open('wb') as log:
            launcher = project/'tools/chromium'/f'run-{mode}.sh'
            # Chromium reads fd 3 and writes fd 4. Bash sets those descriptors
            # before exec, avoiding preexec_fn in a process with server threads.
            command = ['/bin/bash', '-c',
                       'exec 3<&"$1" 4>&"$2"; shift 2; exec "$@"',
                       'devtools-pipe', str(browser_read), str(browser_write),
                       str(launcher), f'--user-data-dir={profile}',
                       '--remote-debugging-pipe', '--autoplay-policy=no-user-gesture-required',
                       '--disable-background-networking', 'about:blank']
            process = subprocess.Popen(command, pass_fds=(browser_read, browser_write),
                                       stdout=log, stderr=log)
            os.close(browser_read); os.close(browser_write)
            browser_read = browser_write = None
            cdp = DevTools(control_read, control_write)
            report['gpu'] = cdp.call('SystemInfo.getInfo')
            url = f'http://127.0.0.1:{server.server_port}/index.html'
            target = cdp.call('Target.createTarget', {'url': 'about:blank'})['targetId']
            session = cdp.call('Target.attachToTarget',
                               {'targetId': target, 'flatten': True})['sessionId']
            cdp.call('Runtime.enable', session=session)
            cdp.call('Media.enable', session=session)
            cdp.call('Page.enable', session=session)
            marker = len(cdp.events)
            cdp.call('Page.navigate', {'url': url}, session)
            cdp.wait_event('Page.loadEventFired', session, after=marker)
            expression = '''(async()=>{while(!document.querySelector('#video'))
              await new Promise(r=>setTimeout(r,50)); const v=document.querySelector('#video');
              v.muted=true; const clip=CLIP;
              if(clip==='mse')await loadMse(); else {v.src=clip; v.load();}
              await v.play(); return true;})()'''
            setup = cdp.call('Runtime.evaluate', {'expression': expression.replace('CLIP', json.dumps(args.clip)),
                                         'awaitPromise': True, 'returnByValue': True}, session)
            if setup.get('exceptionDetails'):
                raise RuntimeError(f'playback setup: {setup["exceptionDetails"]}')
            if args.fullscreen or args.require_vsr:
                fullscreen=cdp.call('Runtime.evaluate', {'expression':
                    "(async()=>{await document.querySelector('#video').requestFullscreen();return !!document.fullscreenElement;})()",
                    'awaitPromise':True,'returnByValue':True,'userGesture':True},session)
                if fullscreen.get('exceptionDetails') or not fullscreen.get('result',{}).get('value'):
                    raise RuntimeError(f'fullscreen setup: {fullscreen}')
                report['fullscreen_requested']=True
            start = time.monotonic()
            while time.monotonic()-start < args.seconds:
                result = cdp.call('Runtime.evaluate', {'expression': '''(()=>{
                  const v=document.querySelector('#video'),q=v.getVideoPlaybackQuality();
                  return {time:v.currentTime,width:v.videoWidth,height:v.videoHeight,
                    decoded:q.totalVideoFrames,dropped:q.droppedVideoFrames,fullscreen:!!document.fullscreenElement,
                    paused:v.paused,error:v.error?.message};})()''', 'returnByValue': True}, session)
                if result.get('exceptionDetails'):
                    raise RuntimeError(f'playback sample: {result["exceptionDetails"]}')
                report['samples'].append(result['result']['value'])
                time.sleep(0.5)
            # The last sample may have returned just before the renderer's
            # asynchronous MediaLog event reached the DevTools pipe.
            cdp.call('Runtime.evaluate', {'expression': '0', 'returnByValue': True}, session)
            report['media_events'] = [e for e in cdp.events if e['method'].startswith('Media.')]
            report['enhanced_selections'] = extract_enhanced_selections(
                report['media_events'])
            report['presentation_windows'] = extract_vsr_presentations(report['media_events'])
            report['gpu_after_playback'] = cdp.call('SystemInfo.getInfo')
            if args.capture_frame:
                geometry = cdp.call('Runtime.evaluate', {'expression': '''(()=>{
                  const r=document.querySelector('#video').getBoundingClientRect();
                  return {x:r.x,y:r.y,width:r.width,height:r.height,
                    deviceScaleFactor:devicePixelRatio};})()''',
                    'returnByValue': True}, session)['result']['value']
                report['captured_video'] = geometry
                clip = {key: geometry[key] for key in ('x', 'y', 'width', 'height')}
                image = cdp.call('Page.captureScreenshot', {
                    'format': 'png', 'clip': {**clip, 'scale': 1},
                    'fromSurface': True, 'captureBeyondViewport': True}, session)
                (results/'frame.png').write_bytes(base64.b64decode(image['data'], validate=True))
            cdp.call('Browser.close', timeout=10)
            process.wait(timeout=15)
        report['browser_exit_code'] = process.returncode
        (results/'report.json').write_text(json.dumps(report, indent=2))
        print(results)
        if process.returncode != 0:
            raise RuntimeError('browser failed; inspect report.json and browser.log')
        validate_browser_health((results/'browser.log').read_text(errors='replace'))
        validate_playback(report['samples'], args.mse)
        if args.require_continuous_vsr:
            validate_vsr_continuity(report['presentation_windows'])
        if args.require_vsr:
            validate_vsr_selection(report['enhanced_selections'],
                                   minimum=args.min_enhanced_frames,
                                   browser_log=(results/'browser.log').read_text(errors='replace'))
            count = max(item['selected_enhanced'] for item in report['enhanced_selections'])
            print(f'Playback advanced; renderer selected enhanced frames for presentation (count={count}).')
        else:
            print('Playback advanced. Use --require-vsr to demand renderer evidence that an enhanced frame was selected for presentation.')
    except Exception as error:
        report['error'] = str(error)
        (results/'report.json').write_text(json.dumps(report, indent=2))
        print(results)
        raise
    finally:
        if process and process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
        for fd in [browser_read, browser_write, control_read, control_write]:
            if fd is not None:
                os.close(fd)
        server.shutdown(); server.server_close()


if __name__ == '__main__':
    main()
