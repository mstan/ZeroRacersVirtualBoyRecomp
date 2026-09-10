"""Package committed production sources and their non-system DLL dependencies."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import tomllib
import zipfile

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, default=ROOT / 'build-release')
p.add_argument('--framework', type=Path, default=ROOT / 'vbrecomp')
p.add_argument('--ui', type=Path, default=ROOT / 'recomp-ui')
p.add_argument('--toolchain', type=Path, default=Path('C:/msys64/mingw64/bin'))
p.add_argument('--out', type=Path, default=ROOT / 'dist')
p.add_argument('--git', default='C:/Program Files/Git/cmd/git.exe')
args = p.parse_args()
args.build = args.build.resolve()
args.framework = args.framework.resolve()
args.ui = args.ui.resolve()
args.out = args.out.resolve()
args.out.mkdir(parents=True, exist_ok=True)
cache = (args.build / 'CMakeCache.txt').read_text()
for flag, value in [('ZERO_RACERS_UI', 'ON'), ('VBRECOMP_DEBUG_TOOLS', 'OFF'), ('VBRECOMP_CPUHOOK', 'OFF')]:
    if f'{flag}:BOOL={value}' not in cache:
        raise SystemExit(f'Expected production configuration {flag}={value}')
if 'CMAKE_BUILD_TYPE:STRING=Release' not in cache:
    raise SystemExit('Expected Release build')
for key, expected in [('CMAKE_HOME_DIRECTORY', ROOT), ('VBRECOMP_ROOT', args.framework),
                      ('RECOMP_UI_ROOT', args.ui)]:
    match = re.search(rf'^{key}:[^=]+=(.+)$', cache, re.MULTILINE)
    if not match or Path(match[1]).resolve() != expected.resolve():
        raise SystemExit(f'Build cache does not match source directory: {key}')

def git(path, *command):
    return subprocess.check_output([args.git, '-C', str(path), *command], text=True).strip()

for tree in (ROOT, args.framework, args.ui):
    if git(tree, 'status', '--porcelain'):
        raise SystemExit(f'Commit source changes before packaging: {tree}')

exe = args.build / 'vbrecomp/runtime/ZeroRacersVirtualBoyRecomp.exe'
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
mod_source = ROOT / 'mods/full-color'
manifest = tomllib.loads((mod_source / 'manifest.toml').read_text(encoding='utf-8'))
if any(feature.get('default_enabled', False) for feature in manifest['feature']):
    raise SystemExit('The bundled color mod must be opt-in')
mod = args.build / 'mod-packages' / f"zero-racers-full-color-{manifest['version']}.vbmod"
with zipfile.ZipFile(mod) as z:
    expected_files = {path.relative_to(mod_source).as_posix(): path
                      for path in mod_source.rglob('*') if path.is_file()}
    if set(z.namelist()) != set(expected_files) or any(
        z.read(name) != path.read_bytes() for name, path in expected_files.items()
    ):
        raise SystemExit('Rebuild the color package: its contents differ from source')
info = {'version': (ROOT / 'VERSION').read_text().strip(),
        'game_commit': git(ROOT, 'rev-parse', 'HEAD'),
        'framework_commit': git(args.framework, 'rev-parse', 'HEAD'),
        'ui_commit': git(args.ui, 'rev-parse', 'HEAD'),
        'configuration': 'Release, recomp-ui, hybrid fallback, TCP/CPU trace disabled',
        'executable_sha256': sha(exe),
        'default_color_enabled': False,
        'optional_mod': {'file': mod.name, 'id': manifest['id'],
                         'version': manifest['version'], 'sha256': sha(mod)}}
for name, tree in [('vbrecomp', args.framework), ('recomp-ui', args.ui)]:
    if git(ROOT, 'rev-parse', f'HEAD:{name}') != git(tree, 'rev-parse', 'HEAD'):
        raise SystemExit(f'Build dependency does not match committed pin: {name}')

with tempfile.TemporaryDirectory(prefix='zero-racers-release-', dir=args.out) as temp:
    stage = Path(temp).resolve()
    if not stage.is_relative_to(args.out) or stage == args.out:
        raise SystemExit('Unexpected packaging staging directory')
    shutil.copy2(exe, stage / exe.name)
    shutil.copytree(exe.parent / 'assets', stage / 'assets')
    shutil.copy2(mod, stage / mod.name)
    licenses = stage / 'licenses'
    shutil.copytree(ROOT / 'assets/licenses', licenses)
    for source, dest in [(ROOT / 'LICENSE.md', 'project.txt'),
                         (args.framework / 'LICENSE', 'vbrecomp.txt'),
                         (args.ui / 'LICENSE', 'recomp-ui.txt'),
                         (args.framework / 'runtime/licenses/snes-mod-runtime.txt', 'snes-mod-runtime.txt'),
                         (args.ui / 'src/third_party/imgui/LICENSE.txt', 'imgui.txt'),
                         (args.framework / 'runtime/external/SDL2/COPYING.txt', 'SDL2.txt')]:
        shutil.copy2(source, licenses / dest)
    queue = [exe]
    copied = set()
    while queue:
        binary = queue.pop()
        imports = subprocess.check_output([str(args.toolchain / 'objdump.exe'), '-p', str(binary)], text=True)
        for name in re.findall(r'DLL Name:\s*(\S+)', imports):
            if name.lower() in copied:
                continue
            candidate = next((folder / name for folder in (exe.parent, args.toolchain) if (folder / name).is_file()), None)
            if candidate:
                copied.add(name.lower())
                shutil.copy2(candidate, stage / name)
                queue.append(candidate)
            elif not (Path(os.environ['SystemRoot']) / 'System32' / name).is_file() and not name.lower().startswith(('api-ms-', 'ext-ms-')):
                raise SystemExit(f'Unresolved dependency: {name}')
    info['dlls'] = {path.name: sha(path) for path in stage.glob('*.dll')}
    (stage / 'build-info.json').write_text(json.dumps(info, indent=2) + '\n')
    shutil.copy2(ROOT / 'README.md', stage / 'README.md')
    shutil.copytree(ROOT / 'docs', stage / 'docs')
    (stage / 'README.txt').write_text('Zero Racers Recompiled ' + info['version'] + '\n\n'
        'Extract the entire ZIP. Run ZeroRacersVirtualBoyRecomp.exe, select your\n'
        'own extracted Zero Racers .vb cartridge, and press Play. No ROM included.\n'
        f'Optional color: in Mods, install {mod.name}, then enable Wireframe color.\n'
        'Color is OFF by default. Disable it to restore native red.\n'
        'Keyboard: arrows, WASD, X/Z, Q/E, Enter/Right Shift. Escape: settings.\n'
        'Keep vbrecomp.cfg, saves/ and mods/ when updating. See README.md.\n')
    archive = args.out / 'ZeroRacersVirtualBoyRecomp-windows-x64.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for source in sorted(stage.rglob('*')):
            if source.is_file():
                if source.suffix.lower() in ('.vb', '.vboy', '.cfg', '.sav', '.src'):
                    raise SystemExit(f'Unexpected private/profile data in release: {source.name}')
                z.write(source, source.relative_to(stage).as_posix())
shutil.copy2(mod, args.out / mod.name)
(args.out / 'SHA256SUMS.txt').write_text(''.join(
    f'{sha(path)}  {path.name}\n' for path in (archive, args.out / mod.name)))
print(json.dumps({'archive': str(archive), 'bytes': archive.stat().st_size, **info}, indent=2))
