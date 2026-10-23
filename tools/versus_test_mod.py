"""Explicit opt-in for versus qualification, including isolated profiles."""
from pathlib import Path

def versus_mod_args(runtime):
    exe=Path(runtime).resolve();name='zero-racers-multiplayer-0.1.0.vbmod'
    # Installed review ZIP, single-config build, then MSVC multi-config build.
    candidates=(exe.parent/name,*(p/'mod-packages'/name for p in exe.parents[2:4]))
    package=next((p for p in candidates if p.is_file()),None)
    if package is None:raise FileNotFoundError('Build/package the multiplayer mod before versus qualification: '+str(candidates))
    return ['--install-mod',str(package),'--enable-mod','zero-racers.multiplayer:versus']
