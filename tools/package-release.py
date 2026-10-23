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
p.add_argument('--net', type=Path, help='Shared Net checkout; defaults to the build cache path')
p.add_argument('--juice', type=Path, help='Exact libjuice source tree used by the ICE build')
p.add_argument('--review', action='store_true', help='Package a qualified review build with local diagnostic tools')
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
def cache_path(name):
    match = re.search(rf'^{re.escape(name)}:[^=]+=(.+)$', cache, re.MULTILINE)
    if not match:raise SystemExit(f'Missing dependency source path: {name}')
    return Path(match[1]).resolve()
net_enabled = 'ZERO_RACERS_NETPLAY:BOOL=ON' in cache
ice_enabled = net_enabled and 'RNET_ENABLE_ICE:BOOL=ON' in cache
if net_enabled:args.net = (args.net or cache_path('RECOMP_NET_ROOT')).resolve()
if ice_enabled:args.juice = (args.juice or cache_path('libjuice_SOURCE_DIR')).resolve()
for flag, value in [('ZERO_RACERS_UI', 'ON'), ('VBRECOMP_DEBUG_TOOLS', 'ON' if args.review else 'OFF'), ('VBRECOMP_CPUHOOK', 'OFF')]:
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
if net_enabled:
    if git(args.net, 'status', '--porcelain'):
        raise SystemExit('Commit shared Net changes before packaging')
    if git(args.framework, 'rev-parse', 'HEAD:lib/recomp-net') != git(args.net, 'rev-parse', 'HEAD'):
        raise SystemExit('Shared Net source does not match the engine pin')

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
        'configuration': ('Review, Release, recomp-ui, local diagnostic tools, CPU trace disabled' if args.review
                          else 'Release, recomp-ui, hybrid fallback, TCP/CPU trace disabled'),
        'netplay': net_enabled, 'ice': ice_enabled,
        'executable_sha256': sha(exe),
        'default_color_enabled': False,
        'optional_mod': {'file': mod.name, 'id': manifest['id'],
                         'version': manifest['version'], 'sha256': sha(mod)}}
multi_source=ROOT/'mods/multiplayer'
multi_manifest=tomllib.loads((multi_source/'manifest.toml').read_text(encoding='utf-8'))
if any(f.get('default_enabled',False) for f in multi_manifest['feature']):
    raise SystemExit('Multiplayer must be opt-in')
multi=args.build/'mod-packages'/f"zero-racers-multiplayer-{multi_manifest['version']}.vbmod"
with zipfile.ZipFile(multi) as z:
    sources={p.relative_to(multi_source).as_posix():p for p in multi_source.rglob('*') if p.is_file()}
    if set(z.namelist())!=set(sources) or any(z.read(n)!=p.read_bytes() for n,p in sources.items()):
        raise SystemExit('Rebuild the multiplayer package: contents differ from source')
info['multiplayer_mod']={'file':multi.name,'id':multi_manifest['id'],'version':multi_manifest['version'],
                         'sha256':sha(multi),'default_enabled':False,'preinstalled':True}
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
    shutil.copy2(multi,stage/multi.name)
    shutil.copytree(multi_source,stage/'mods/packages'/multi_manifest['id']/multi_manifest['version'])
    licenses = stage / 'licenses'
    shutil.copytree(ROOT / 'assets/licenses', licenses)
    for source, dest in [(ROOT / 'LICENSE.md', 'project.txt'),
                         (args.framework / 'LICENSE', 'vbrecomp.txt'),
                         (args.ui / 'LICENSE', 'recomp-ui.txt'),
                         (args.framework / 'runtime/licenses/snes-mod-runtime.txt', 'snes-mod-runtime.txt'),
                         (args.ui / 'src/third_party/imgui/LICENSE.txt', 'imgui.txt'),
                         (args.framework / 'runtime/external/SDL2/COPYING.txt', 'SDL2.txt')]:
        shutil.copy2(source, licenses / dest)
    shutil.copy2(args.framework / 'lib/retcomm-rbengine/LICENSE', licenses / 'retcomm-rbengine.txt')
    if net_enabled:
        shutil.copy2(args.net / 'LICENSE', licenses / 'recomp-net.txt')
        info['net_commit'] = git(args.net, 'rev-parse', 'HEAD')
    if ice_enabled:
        shutil.copy2(args.juice / 'LICENSE', licenses / 'libjuice-MPL-2.0.txt')
        # Ship the exact source form beside the statically linked executable.
        # FetchContent's release tree contains no build outputs or Git database.
        juice_archive = stage / 'libjuice-source.zip'
        with zipfile.ZipFile(juice_archive, 'w', zipfile.ZIP_DEFLATED) as source_zip:
            for source in sorted(args.juice.rglob('*')):
                relative = source.relative_to(args.juice)
                if source.is_file() and '.git' not in relative.parts:
                    source_zip.write(source, Path('libjuice') / relative)
        info['libjuice_source_sha256'] = sha(juice_archive)
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
        'Multiplayer: enable Multiplayer (2-4 players) on Mods before launching.\n'
        'The bundled Multiplayer package is installed but OFF by default.\n'
        'Keyboard: arrows, WASD, X/Z, Q/E, Enter/Right Shift. Escape: settings.\n'
        'Keep vbrecomp.cfg, saves/ and mods/ when updating. See README.md.\n')
    archive = args.out / ('ZeroRacersVirtualBoyRecomp-versus-review-windows-x64.zip' if args.review
                          else 'ZeroRacersVirtualBoyRecomp-windows-x64.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for source in sorted(stage.rglob('*')):
            if source.is_file():
                if source.suffix.lower() in ('.vb', '.vboy', '.cfg', '.sav', '.src'):
                    raise SystemExit(f'Unexpected private/profile data in release: {source.name}')
                z.write(source, source.relative_to(stage).as_posix())
shutil.copy2(mod, args.out / mod.name)
shutil.copy2(multi,args.out/multi.name)
(args.out / 'SHA256SUMS.txt').write_text(''.join(
    f'{sha(path)}  {path.name}\n' for path in (archive, args.out / mod.name,args.out/multi.name)))
print(json.dumps({'archive': str(archive), 'bytes': archive.stat().st_size, **info}, indent=2))
