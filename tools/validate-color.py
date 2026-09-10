"""Compare saved off/on route captures: guest state, raw output and visible lines.

Run color-capture.py once with the package disabled and once enabled, using
separate output folders. This check performs no further gameplay runs.
"""
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disabled', type=Path)
    parser.add_argument('enabled', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    a = json.loads((args.disabled / 'capture.json').read_text())
    b = json.loads((args.enabled / 'capture.json').read_text())
    assert not a['enabled'] and b['enabled'], 'Expected an off/on capture pair'
    assert a['frames'] and a['frames'] == b['frames'], 'Routes/checkpoints differ'
    assert a['rom_sha256'] == b['rom_sha256'], 'Cartridges differ'
    report = {'passed': False, 'rom_sha256': a['rom_sha256'],
              'disabled_runtime': a['runtime_sha256'], 'enabled_runtime': b['runtime_sha256'],
              'checkpoints': []}
    changed_total = 0
    for entry in a['frames']:
        stem = f"{entry['frame']:04}-{entry['label']}"
        for suffix in ('.json', '.wram', '.vram'):
            assert (args.disabled / (stem + suffix)).read_bytes() == (args.enabled / (stem + suffix)).read_bytes(), f'{stem}: guest state changed ({suffix})'
        changed = []
        for eye in (0, 1):
            raw = Image.open(args.enabled / f'{stem}-raw-{eye}.png').convert('RGB')
            original = Image.open(args.disabled / f'{stem}-raw-{eye}.png').convert('RGB')
            off = Image.open(args.disabled / f'{stem}-color-{eye}.png').convert('RGB')
            color = Image.open(args.enabled / f'{stem}-color-{eye}.png').convert('RGB')
            assert raw.size == color.size == original.size == off.size == (384, 224)
            assert raw.tobytes() == original.tobytes() == off.tobytes(), f'{stem}: native output changed'
            # Exact visible mask and native per-pixel intensity, including dim
            # lines/fades. Comparing a bounding box or pixel count is too weak.
            count = 0
            for native, presented in zip(raw.get_flattened_data(), color.get_flattened_data()):
                assert bool(max(native)) == bool(max(presented)), f'{stem}: visible line/gap changed'
                assert max(native) == max(presented), f'{stem}: native intensity changed'
                count += native != presented
            changed.append(count)
            changed_total += count
        report['checkpoints'].append({**entry, 'recolored_pixels': changed,
                                      'changed_native_pixels': [0, 0], 'changed_visible_masks': [0, 0]})
    assert changed_total, 'Renderer did not recolor any pixels'
    package_hashes = [hashlib.sha256((folder / 'capture.json').read_bytes()).hexdigest()
                      for folder in (args.disabled, args.enabled)]
    report.update(passed=True, capture_manifest_sha256=package_hashes)
    output = args.report or args.enabled / 'validation.json'
    output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {len(a["frames"])} checkpoints, both eyes; CPU/WRAM/VRAM and native pixels unchanged; exact line masks and intensities preserved')


if __name__ == '__main__':
    main()
