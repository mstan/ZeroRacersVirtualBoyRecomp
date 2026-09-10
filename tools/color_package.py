"""Idempotent installation arguments, rejecting stale development packages."""
import tomllib
import zipfile


def install_args(mods, package):
    with zipfile.ZipFile(package) as archive:
        manifest = tomllib.loads(archive.read('manifest.toml').decode('utf-8'))
        installed = mods / 'packages' / manifest['id'] / manifest['version']
        if not installed.exists():
            return ['--install-mod', str(package)]
        for entry in archive.infolist():
            if entry.is_dir():
                continue
            path = installed / entry.filename
            if not path.is_file() or path.read_bytes() != archive.read(entry):
                raise RuntimeError(f'{installed} has different contents; use a fresh profile/output directory')
    return []
