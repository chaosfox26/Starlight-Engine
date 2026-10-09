"""Prepare the pinned app-local Direct3D validation runtime for the native checks."""
import hashlib
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
VERSION = '1.619.6'
SHA256 = '08f0489281401aa430fc37322d6c3fc98a8025175aacd714c10d562f4963f1e9'
URL = f'https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/{VERSION}/microsoft.direct3d.d3d12.{VERSION}.nupkg'


def fetch_verified(archive, fetch=urllib.request.urlretrieve):
    """The archive is only ever replaced by a complete download that matches the pinned hash, so a dropped connection cannot leave a bad cached file."""
    if archive.exists() and hashlib.sha256(archive.read_bytes()).hexdigest() == SHA256:
        return
    part = archive.with_name(archive.name + '.part')
    fetch(URL, part)
    if hashlib.sha256(part.read_bytes()).hexdigest() != SHA256:
        part.unlink()
        raise RuntimeError('Graphics validation SDK hash mismatch')
    part.replace(archive)


def prepare():
    cache = ROOT / 'out/dependency-review' / f'agility-{VERSION}'
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / 'sdk.nupkg'
    fetch_verified(archive)
    destination = ROOT / 'out/classic/Release/D3D12'
    destination.mkdir(parents=True, exist_ok=True)
    # Only the two DLLs expressly listed in the package's distributable list.
    with zipfile.ZipFile(archive) as package:
        for name in ('D3D12Core.dll', 'd3d12SDKLayers.dll'):
            (destination / name).write_bytes(package.read('build/native/bin/x64/' + name))
        for name in ('LICENSE.txt', 'distributable files.txt'):
            (destination / name).write_bytes(package.read(name))
    print(destination)


if __name__ == '__main__':
    prepare()
