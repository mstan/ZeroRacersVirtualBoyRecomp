"""Open the color preview launcher in a separate profile; wait until it closes."""
import argparse
import os
from pathlib import Path
import subprocess
import socket
from color_package import install_args

GAME = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=GAME / 'build/vbrecomp/runtime/ZeroRacersVirtualBoyRecomp.exe')
    parser.add_argument('--rom', type=Path, default=GAME / 'roms/zero_racers.vb')
    parser.add_argument('--package', type=Path, default=GAME / 'build/mod-packages/zero-racers-full-color-0.2.0.vbmod')
    parser.add_argument('--profile', type=Path, default=GAME / 'build/color-profile-0.2.0')
    args = parser.parse_args()
    for path in (args.runtime, args.rom, args.package):
        if not path.is_file():
            parser.error(f'Missing {path}; build the color branch first')
    profile = args.profile.resolve()
    profile.mkdir(parents=True, exist_ok=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('VBRECOMP_')}
    toolchain = Path('C:/msys64/mingw64/bin')
    if os.name == 'nt' and toolchain.is_dir():
        env['PATH'] = str(toolchain) + os.pathsep + env.get('PATH', '')
    # Pick an unused diagnostic port so an earlier preview can remain open.
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    command = [str(args.runtime.resolve()), '--rom', str(args.rom.resolve()),
               '--launcher', '--config', str(profile / 'settings.cfg'),
               '--mods-dir', str(profile / 'mods'), '--save', str(profile / 'zero_racers.sav'),
               '--port', str(port)]
    command += install_args(profile / 'mods', args.package.resolve())
    command += ['--enable-mod', 'zero-racers.full-color:full-color']
    return subprocess.run(command, cwd=profile, env=env, check=False).returncode


if __name__ == '__main__':
    raise SystemExit(main())
