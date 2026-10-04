"""Prepare the pinned app-local Direct3D validation runtime for the native checks."""
import hashlib
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[2]
VERSION = '1.619.6'
SHA256 = '08f0489281401aa430fc37322d6c3fc98a8025175aacd714c10d562f4963f1e9'
URL = f'https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/{VERSION}/microsoft.direct3d.d3d12.{VERSION}.nupkg'


def prepare():
    cache = ROOT / 'out/dependency-review' / f'agility-{VERSION}'
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / 'sdk.nupkg'
    if not archive.exists():
        urllib.request.urlretrieve(URL, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError('Graphics validation SDK hash mismatch')
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
