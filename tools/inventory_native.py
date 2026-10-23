"""Capture a bounded stock race update through the existing debug rings.

Private ROM/state/trace/pixel artifacts go only to the explicit output path.
This is a source-mapping probe, not multiplayer or parity qualification.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--framework', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--route', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--checkpoint', type=int, default=2600)
    parser.add_argument('--frames', type=int, default=4)
    parser.add_argument('--port', type=int, default=15931)
    args = parser.parse_args()
    args.out = args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(args.framework.resolve() / 'tools'))
    from cosim import Runner
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    assert sha(args.rom) == '47421cd82dfd414d042d2f7f9db51e657d414d4ddfa46887efb544029fa11ce7'
    report = {'scope': 'stock source-mapping probe, not multiplayer qualification',
              'runtime_sha256': sha(args.runtime), 'rom_sha256': sha(args.rom)}
    with Runner(args.runtime, args.rom, 'hybrid', args.port, args.out / 'profile') as runner:
        client = runner.client
        remaining = args.checkpoint
        for segment in json.loads(args.route.read_text()):
            count = min(remaining, segment['frames'])
            client.command('set_input', pad=segment.get('pad', 0))
            while count:
                step = min(count, 32)
                client.command('replay_frames', frames=step)
                count -= step
                remaining -= step
            if not remaining:
                break
        assert not remaining
        client.command('set_input', pad=0)
        trace_start = client.command('fntrace_stats')['total']
        write_start = client.command('wtrace_stats')['total']
        report['frames'] = []
        for i in range(args.frames + 1):
            frame = client.command('frame')['frame']
            (args.out / f'wram-{frame}.bin').write_bytes(runner.memory(0x05000000))
            registers = client.command('get_registers')
            (args.out / f'registers-{frame}.json').write_text(json.dumps(registers, indent=2))
            if not i:
                client.command('save_state', path=(args.out / 'race.state').as_posix())
                for eye in range(2):
                    client.command('screenshot', eye=eye, path=(args.out / f'eye-{eye}.png').as_posix())
            report['frames'].append(frame)
            if i < args.frames:
                client.run_frames(1)
        def dump(command, start, **filters):
            entries = []
            while True:
                result = client.command(command, from_seq=start, limit=4096, **filters)
                rows = result['entries']
                entries.extend(rows)
                if not rows or len(rows) < 4096:
                    break
                start = rows[-1]['seq'] + 1
            (args.out / (command + '.json')).write_text(json.dumps(entries))
            return entries
        functions = dump('fntrace_dump', trace_start)
        writes = dump('wtrace_dump', write_start, addr_min=0x05000000, addr_max=0x05010000)
        report['function_calls'] = Counter(row['pc'] for row in functions).most_common()
        report['wram_store_pcs'] = Counter(row['pc'] for row in writes).most_common()
        report['execution'] = client.command('execution_stats')
        report['dispatch_misses_empty'] = not any(p.read_text().strip() for p in runner.directory.glob('dispatch_misses.log'))
        assert report['dispatch_misses_empty']
    (args.out / 'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
