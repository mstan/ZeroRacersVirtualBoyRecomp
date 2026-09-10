"""Author color-only material masks from private TCP source captures.

World numbers label known captured artwork here, never runtime ownership.
The output stores source atlas positions, CHR content hashes and color IDs;
no original image, CHR bitmap or ROM data is packaged.
"""
import argparse
from pathlib import Path
import struct

GAME = Path(__file__).resolve().parents[1]


def ink_for(frame, s):
    h, x, y, tile, u, v, atlas, kind, raw, world = s
    if not world or kind > 3:
        return 0
    # Logo keeps the same source location when it slides up above the menus.
    if frame in (600, 700, 800) and atlas == 0:
        return 5 if x < 170 else 6
    if frame == 500:
        return 4 if world == 31 else 2
    if frame == 800 and world in (14, 16):
        return 4
    if frame == 1000:
        return 4 if world == 32 else 3
    if frame == 1100:
        return {17: 3, 11: 5, 18: 5, 19: 7, 20: 7, 29: 4}.get(world, 2)
    if frame in (1200, 1300):
        return 4 if world in (6, 8) else 3 if world in (22, 28, 27) else 2
    if frame >= 1500:
        return {25: 7, 26: 3, 29: 6, 9: (7 if frame == 2200 else 4),
                21: 1, 20: 3, 16: 3, 19: 4, 28: 3, 30: 3,
                24: 3, 23: 2, 22: 2, 31: 2, 14: 3}.get(world, 2)
    return 2


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--captures', type=Path, default=GAME / 'validation/color-sources')
    parser.add_argument('--out', type=Path, default=GAME / 'mods/full-color/materials.txt')
    args = parser.parse_args()
    masks = {}
    for path in sorted(args.captures.glob('*.src')):
        data = path.read_bytes()
        if data[:8] != b'VBSRC001' or len(data) != 24 + 384 * 224 * 16:
            raise ValueError(f'Invalid source capture: {path}')
        frame = int(path.name[:4])
        for s in struct.iter_unpack('<IHHHBBBBBB', data[24:]):
            ink = ink_for(frame, s)
            if not ink:
                continue
            h, x, y, tile, u, v, atlas, kind, raw, world = s
            key = atlas, kind, x // 8, y // 8, h
            masks.setdefault(key, [0] * 64)[v * 8 + u] = ink
    if not masks:
        raise ValueError('No source artwork was captured')
    rows = [f'{m} {k} {x} {y} {h:08x} ' + ''.join(map(str, mask))
            for (m, k, x, y, h), mask in sorted(masks.items())]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(('\n'.join(rows) + '\n').encode('ascii'))
    print(f'Wrote {len(rows)} source material masks to {args.out}')


if __name__ == '__main__':
    main()
