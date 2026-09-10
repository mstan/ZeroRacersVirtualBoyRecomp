"""Capture menus and a stationary race start for color work, using an owned TCP instance.

Source/VRAM captures are private owner-ROM artifacts under ignored validation/.
The process runs for this command only and is closed before the command returns.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

GAME = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(GAME / 'vbrecomp' / 'tools'))
from cosim import Runner
from color_package import install_args

LABELS = {500: 'automatic-pause', 600: 'title', 700: 'name-entry',
          800: 'mode', 1000: 'machines', 1100: 'machine-stats',
          1200: 'area', 1300: 'difficulty', 1500: 'grid',
          1800: 'camera', 2000: 'ready', 2200: 'go', 2400: 'hud'}


def cpu_state(client):
    return {k: v for k, v in client.command('get_registers').items()
            if k not in ('id', 'ok', 'cmd')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=GAME / 'build/vbrecomp/runtime/ZeroRacersVirtualBoyRecomp.exe')
    parser.add_argument('--rom', type=Path, default=GAME / 'roms/zero_racers.vb')
    parser.add_argument('--out', type=Path, default=GAME / 'validation/color-sources')
    parser.add_argument('--port', type=int, default=4690)
    parser.add_argument('--package', type=Path)
    parser.add_argument('--enabled', action='store_true')
    args = parser.parse_args()
    output = args.out.resolve()
    extra = []
    if args.package:
        extra = install_args(output / 'mods', args.package.resolve())
        extra += ['--enable-mod' if args.enabled else '--disable-mod',
                 'zero-racers.full-color:full-color']
    route = json.loads((GAME / 'tests/race-route.json').read_text())[:23]
    route += [{'frames': 100, 'pad': 0} for _ in range(9)]
    report = {'runtime_sha256': hashlib.sha256(args.runtime.read_bytes()).hexdigest(),
              'rom_sha256': hashlib.sha256(args.rom.read_bytes()).hexdigest(),
              'enabled': args.enabled, 'frames': []}
    with Runner(args.runtime, args.rom, 'hybrid', args.port, output, extra) as runner:
        frame = 0
        for segment in route:
            runner.client.command('set_input', pad=segment['pad'])
            frame = runner.client.run_frames(segment['frames'], timeout=60)
            if frame not in LABELS:
                continue
            stem = output / f'{frame:04}-{LABELS[frame]}'
            state = cpu_state(runner.client)
            for eye in (0, 1):
                runner.client.command('screenshot', path=f'{stem.as_posix()}-raw-{eye}.png', eye=eye)
                runner.client.command('screenshot', path=f'{stem.as_posix()}-color-{eye}.png', eye=eye, presented=1)
                runner.client.command('source_dump', path=f'{stem.as_posix()}-{eye}.src', eye=eye)
            stem.with_suffix('.wram').write_bytes(runner.memory(0x05000000))
            stem.with_suffix('.vram').write_bytes(runner.memory(0, 0x40000))
            stem.with_suffix('.json').write_text(json.dumps(state, indent=2) + '\n')
            assert cpu_state(runner.client) == state, 'Capture advanced the guest'
            report['frames'].append({'frame': frame, 'label': LABELS[frame]})
            print(f'Captured {frame}: {LABELS[frame]}', flush=True)
    (output / 'capture.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
