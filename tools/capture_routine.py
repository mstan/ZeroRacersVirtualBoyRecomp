"""Capture one real native routine invocation between verified breakpoints."""
import argparse
from collections import Counter
import json
from pathlib import Path
import sys
import time


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('framework', 'runtime', 'rom', 'state', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--entry', type=lambda s: int(s, 0), required=True)
    p.add_argument('--return-pc', type=lambda s: int(s, 0), required=True)
    p.add_argument('--r6', type=lambda s: int(s, 0), help='Select this native actor argument at entry')
    p.add_argument('--mask-irqs',action='store_true',help='Isolate a pure kernel; this is not device/timing qualification')
    p.add_argument('--video',action='store_true',help='Capture each eye framebuffer and video RAM for raster comparisons')
    p.add_argument('--port', type=int, default=15931)
    a = p.parse_args()
    a.out = a.out.resolve(); a.out.mkdir(parents=True, exist_ok=False)
    sys.path.insert(0, str(a.framework.resolve() / 'tools'))
    from cosim import Runner
    with Runner(a.runtime, a.rom, 'hybrid', a.port, a.out / 'profile') as r:
        c = r.client
        c.command('load_state', path=a.state.resolve().as_posix())
        def stop(pc):
            c.command('breakpoint', pc=pc); c.command('continue')
            end = time.monotonic() + 10
            while time.monotonic() < end:
                stats = c.command('execution_stats')
                if stats['stopped']:
                    registers = c.command('get_registers')
                    actual = registers['pc']
                    if isinstance(actual, str): actual = int(actual, 0)
                    assert actual == pc, registers
                    return registers
                time.sleep(.005)
            raise TimeoutError(f'Breakpoint {pc:08x}')
        for attempt in range(16):
            before = stop(a.entry)
            if a.r6 is None or int(before['gpr'][6],0)==a.r6:break
        else:raise RuntimeError('Actor argument did not reach the selected routine')
        if a.mask_irqs:
            c.command('psw_set',value=int(before['psw'],0)|0x1000)
            before=c.command('get_registers')
        (a.out / 'before-wram.bin').write_bytes(r.memory(0x05000000))
        if a.video:(a.out/'before-vram.bin').write_bytes(r.memory(0,0x40000))
        (a.out / 'before-registers.json').write_text(json.dumps(before, indent=2))
        c.command('save_state', path=(a.out / 'before.state').as_posix())
        start = c.command('wtrace_stats')['total']
        after = stop(a.return_pc)
        (a.out / 'after-wram.bin').write_bytes(r.memory(0x05000000))
        if a.video:(a.out/'after-vram.bin').write_bytes(r.memory(0,0x40000))
        (a.out / 'after-registers.json').write_text(json.dumps(after, indent=2))
        c.command('save_state', path=(a.out / 'after.state').as_posix())
        rows = []
        while True:
            result = c.command('wtrace_dump', from_seq=start, limit=4096)
            rows.extend(result['entries'])
            if len(result['entries']) < 4096: break
            start = result['entries'][-1]['seq'] + 1
        (a.out / 'writes.json').write_text(json.dumps(rows))
        addresses = Counter(row['addr'] for row in rows)
        report = {'scope': 'native routine source map, not multiplayer qualification',
                  'entry': a.entry, 'return_pc': a.return_pc,
                  'before': before, 'after': after, 'stores': len(rows),
                  'addresses': sorted(addresses.items(), key=lambda x: int(x[0], 16))}
        (a.out / 'report.json').write_text(json.dumps(report, indent=2))
        print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
