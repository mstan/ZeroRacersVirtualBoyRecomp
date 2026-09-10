"""Capture synchronized Zero Racers menus and an idle starting grid over TCP.

Runs owned frontends synchronously, closes them before returning, and saves
unmodified native captures plus labeled side-by-side comparison images.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time
from contextlib import ExitStack

from PIL import Image, ImageChops, ImageDraw, ImageFont, ImageGrab

GAME = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--framework', type=Path, default=GAME / 'vbrecomp')
p.add_argument('--runtime', type=Path, default=GAME / 'build/vbrecomp/runtime/ZeroRacersVirtualBoyRecomp.exe')
p.add_argument('--oracle', type=Path, default=GAME / 'build/vbrecomp/runtime/vb-beetle.exe')
p.add_argument('--rom', type=Path, default=GAME / 'roms/zero_racers.vb')
p.add_argument('--preview', type=Path, help='Optional older executable to compare')
p.add_argument('--out', type=Path, default=GAME / 'validation/menu-hud-comparison')
p.add_argument('--port', type=int, default=4590)
args = p.parse_args()
FRAMEWORK = args.framework.resolve()
sys.path.insert(0, str(FRAMEWORK / 'tools'))
from cosim import Runner
from debug_client import DebugClient

OUT = args.out.resolve()
OUT.mkdir(parents=True, exist_ok=True)
ROM = args.rom.resolve()
RUNTIME = args.runtime.resolve()
ORACLE = args.oracle.resolve()
PREVIEW = args.preview.resolve() if args.preview else None
USER = ctypes.WinDLL('user32', use_last_error=True)
USER.SetProcessDPIAware()
USER.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
USER.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
USER.SetWindowPos.argtypes = [wintypes.HWND, wintypes.HWND, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int, wintypes.UINT]
USER.SetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPCWSTR]
USER.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
USER.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
USER.ClientToScreen.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.POINT)]
CALLBACK = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)


def window_for(pid):
    found = []
    @CALLBACK
    def visit(hwnd, _):
        owner = wintypes.DWORD()
        USER.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        name = ctypes.create_unicode_buffer(128)
        USER.GetClassNameW(hwnd, name, 128)
        if owner.value == pid and name.value == 'SDL_app':
            found.append(hwnd)
        return True
    USER.EnumWindows(visit, 0)
    return found[0] if found else None


class WindowRunner(Runner):
    def __init__(self, exe, mode, port, directory, x, label):
        self.exe = exe
        self.directory = directory
        directory.mkdir(parents=True, exist_ok=True)
        self.client = None
        command = [str(exe), '--rom', str(ROM), '--paused', '--port', str(port)]
        if mode != 'oracle':
            config = directory / 'settings.cfg'
            config.write_text('scale 2\nsource 1\naudio 0\nfullscreen 0\nfilter 0\n')
            command += ['--no-launcher', '--no-save', '--execution', 'hybrid',
                        '--config', str(config), '--mods-dir', str(directory / 'mods')]
        with socket.socket() as probe:
            probe.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
            probe.bind(('127.0.0.1', port))
        environment = {k: v for k, v in os.environ.items() if not k.startswith('VBRECOMP_')}
        self.process = subprocess.Popen(command, cwd=directory, env=environment,
            creationflags=subprocess.CREATE_NO_WINDOW, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 15
            self.hwnd = None
            while time.monotonic() < deadline:
                if self.process.poll() is not None:
                    raise RuntimeError('frontend exited during startup')
                self.hwnd = window_for(self.process.pid)
                if self.client is None:
                    try:
                        self.client = DebugClient(port)
                    except OSError:
                        pass
                if self.client and self.hwnd:
                    break
                time.sleep(.02)
            if not self.client or not self.hwnd:
                raise RuntimeError('window/debugger unavailable')
            USER.SetWindowTextW(self.hwnd, label)
            # Keep the complete client area visible and use native 2x geometry.
            USER.SetWindowPos(self.hwnd, None, x, 70, 0, 0, 0x0045)
            self.client.command('set_input', pad=0)
            assert self.client.command('frame')['frame'] == 0
            # Tab is the existing turbo shortcut. This goes only to our window.
            scan = USER.MapVirtualKeyW(9, 0)
            USER.PostMessageW(self.hwnd, 0x100, 9, 1 | (scan << 16))
            time.sleep(.05)
        except BaseException:
            self.close()
            raise

    def desktop_capture(self, path):
        rect = wintypes.RECT()
        origin = wintypes.POINT(0, 0)
        if not USER.GetClientRect(self.hwnd, ctypes.byref(rect)):
            raise ctypes.WinError()
        if not USER.ClientToScreen(self.hwnd, ctypes.byref(origin)):
            raise ctypes.WinError()
        ImageGrab.grab(bbox=(origin.x, origin.y, origin.x + rect.right, origin.y + rect.bottom),
                       all_screens=True).save(path)


def pixels_different(a, b):
    if a.size != b.size:
        return {'dimensions': [a.size, b.size]}
    return sum(pixel != (0, 0, 0) for pixel in ImageChops.difference(a, b).getdata())


def capture(runner, frame, eye=0):
    path = runner.directory / f'frame-{frame}-eye-{eye}.png'
    runner.client.command('screenshot', eye=eye, path=path.as_posix())
    return Image.open(path).convert('RGB')


labels = {500: 'Automatic pause menu', 600: 'Title', 700: 'Name entry',
          800: 'Single-player mode menu', 1000: 'Machine selection',
          1100: 'Machine statistics and confirmation', 1200: 'Area selection',
          1300: 'Difficulty selection', 1400: 'Pre-race menu', 1500: 'Starting grid',
          1800: 'Starting camera', 2000: 'READY overlay', 2100: 'Countdown',
          2200: 'Start overlay', 2300: 'Idle after start', 2400: 'Idle HUD'}
schedule = json.loads((GAME / 'tests' / 'race-route.json').read_text())[:23]
assert sum(s['frames'] for s in schedule) == 1500
schedule += [{'frames': 50, 'pad': 0} for _ in range(18)]
report = {'passed': False, 'scope': 'Menus, single-player starting sequence, no input after race selection',
          'runtime_sha256': hashlib.sha256(RUNTIME.read_bytes()).hexdigest(),
          'oracle_sha256': hashlib.sha256(ORACLE.read_bytes()).hexdigest(),
          'preview_sha256': hashlib.sha256(PREVIEW.read_bytes()).hexdigest() if PREVIEW else None,
          'schedule': schedule, 'checkpoints': []}
font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 20)
small_font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 16)

try:
    with ExitStack() as stack:
        a = stack.enter_context(WindowRunner(RUNTIME, 'hybrid', args.port, OUT / 'current', 20, 'Zero Racers - current recomp'))
        b = stack.enter_context(WindowRunner(ORACLE, 'oracle', args.port+1, OUT / 'oracle', 820, 'Zero Racers - independent Beetle oracle'))
        old = stack.enter_context(Runner(PREVIEW, ROM, 'hybrid', args.port+2, OUT / 'older-preview')) if PREVIEW else None
        report['capabilities'] = {name: runner.client.command('capabilities') for name, runner in [('current', a), ('oracle', b)]}
        frame = 0
        for segment in schedule:
            for runner in ([a, b, old] if old else [a, b]):
                runner.client.command('set_input', pad=segment['pad'])
                runner.client.run_frames(segment['frames'], timeout=60)
            frame += segment['frames']
            eyes = [(capture(a, frame, eye), capture(b, frame, eye)) for eye in (0, 1)]
            preview = capture(old, frame) if old else None
            row = {'frame': frame, 'label': labels.get(frame, ''),
                   'current_oracle_pixels': [pixels_different(x, y) for x, y in eyes],
                   'preview_oracle_pixels': pixels_different(preview, eyes[0][1]) if preview is not None else None}
            if frame in labels:
                time.sleep(.1)
                host = a.directory / f'frame-{frame}-host.png'
                a.client.command('screenshot', host=1, path=host.as_posix())
                row['host_oracle_pixels'] = pixels_different(Image.open(host).convert('RGB'), eyes[0][1])
                a.desktop_capture(a.directory / f'frame-{frame}-window.png')
                b.desktop_capture(b.directory / f'frame-{frame}-window.png')
                sheet = Image.new('RGB', (1568, 530), '#181c24')
                draw = ImageDraw.Draw(sheet)
                draw.text((16, 10), f'{labels[frame]} | frame {frame}', font=font, fill='white')
                for x, title, im in [(16, 'CURRENT RECOMP', eyes[0][0]), (800, 'INDEPENDENT BEETLE ORACLE', eyes[0][1])]:
                    draw.text((x, 42), title, font=small_font, fill='#bdc8da')
                    sheet.paste(im.resize((768, 448), Image.Resampling.NEAREST), (x, 70))
                sheet.save(OUT / f'compare-{frame}.png')
            report['checkpoints'].append(row)
            print(json.dumps(row), flush=True)
        report['passed'] = all(r['current_oracle_pixels'] == [0, 0] and r.get('host_oracle_pixels', 0) == 0 for r in report['checkpoints'])
except BaseException as exc:
    report['error'] = repr(exc)
finally:
    (OUT / 'report.json').write_text(json.dumps(report, indent=2))
print(json.dumps({'passed': report['passed'], 'checkpoints': len(report['checkpoints']), 'error': report.get('error')}))
raise SystemExit(0 if report['passed'] else 1)
