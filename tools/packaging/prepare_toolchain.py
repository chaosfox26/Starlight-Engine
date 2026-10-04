import argparse
import hashlib
import json
from pathlib import Path
import shutil

from preview_preflight import imports, SYSTEM


def prepare(llvm, cmake, ninja, dxc, math, streamline, output, resume=False):
    sources = dict(llvm=llvm, cmake=cmake, ninja=ninja, dxc=dxc,
                   math=math, streamline=streamline)
    sources = {key: Path(value).resolve(strict=True) for key, value in sources.items()}
    output = Path(output).resolve()
    if any(output == source or output.is_relative_to(source) for source in sources.values()):
        raise ValueError('Output must be outside input directories')
    output.mkdir(parents=True, exist_ok=resume)
    records = {}

    def copy(category, relative, destination=None):
        source = sources[category] / relative
        if not source.resolve(strict=True).is_relative_to(sources[category]):
            raise ValueError('Source escapes selected toolchain input')
        destination = Path(destination or (category + '/' + Path(relative).as_posix()))
        if destination.is_absolute() or '..' in destination.parts:
            raise ValueError('Invalid bundle destination')
        target = output / destination
        key = destination.as_posix()
        if key in records:
            return
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.resolve().is_relative_to(output):
            raise ValueError('Destination escapes bundle')
        if target.exists():
            if hashlib.sha256(target.read_bytes()).digest() != hashlib.sha256(source.read_bytes()).digest():
                raise ValueError('Existing bundle input differs: ' + key)
        else:
            shutil.copy2(source, target)
        records[key] = dict(path=key, origin=category + '/' + Path(relative).as_posix(),
                            bytes=target.stat().st_size,
                            sha256=hashlib.sha256(target.read_bytes()).hexdigest())

    def tree(category, relative, destination=None):
        base = sources[category] / relative
        for path in sorted(base.rglob('*')):
            if path.is_file():
                dest = Path(destination) / path.relative_to(base) if destination else None
                copy(category, path.relative_to(sources[category]), dest)

    def programs(category, folder, names):
        available = {p.name.lower(): p for p in (sources[category] / folder).iterdir() if p.is_file()}
        pending, seen = list(names), set()
        while pending:
            name = pending.pop()
            if name.lower() in seen:
                continue
            seen.add(name.lower())
            source = available[name.lower()]
            copy(category, source.relative_to(sources[category]))
            for entry in imports(source):
                dependency = entry['name'].lower()
                if dependency in SYSTEM | {'secur32.dll', 'iphlpapi.dll'} or dependency.startswith(('api-ms-win-', 'ext-ms-win-')):
                    continue
                if dependency not in available:
                    raise ValueError('Missing bundled dependency: ' + category + '/' + dependency)
                pending.append(dependency)

    programs('llvm', 'bin', ['clang++.exe', 'clang.exe', 'clang-23.exe',
             'clang-target-wrapper.exe', 'ld.lld.exe', 'llvm-ar.exe',
             'llvm-ranlib.exe', 'llvm-rc.exe', 'llvm-windres.exe'])
    for name in ('mingw32-common.cfg', 'x86_64-pc-windows-gnu.cfg', 'x86_64-w64-windows-gnu.cfg'):
        copy('llvm', 'bin/' + name)
    for name in ('include', 'lib/clang', 'x86_64-w64-mingw32', 'share'):
        tree('llvm', name)
    copy('llvm', 'LICENSE.TXT')
    programs('cmake', 'bin', ['cmake.exe', 'ctest.exe'])
    for path in sources['cmake'].joinpath('share').glob('cmake-*'):
        tree('cmake', path.relative_to(sources['cmake']))
    for path in sources['cmake'].joinpath('doc/cmake').rglob('*'):
        if path.is_file() and 'html' not in path.relative_to(sources['cmake']).parts and path.suffix != '.qch':
            copy('cmake', path.relative_to(sources['cmake']))
    programs('ninja', '.', ['ninja.exe'])
    copy('ninja', 'COPYING')
    programs('dxc', 'bin/x64', ['dxc.exe', 'dxcompiler.dll', 'dxil.dll'])
    for name in ('LICENCE-MIT.txt', 'LICENSE-LLVM.txt', 'LICENSE-MS.txt'):
        copy('dxc', name)
    tree('math', 'Inc')
    copy('math', 'LICENSE')
    tree('streamline', 'include')
    copy('streamline', 'license.txt')
    copy('streamline', '3rd-party-licenses.md')
    manifest = dict(format='starlight-build-toolchain-v1',
                    cmake='cmake/bin/cmake.exe', ctest='cmake/bin/ctest.exe',
                    ninja='ninja/ninja.exe', compiler='llvm/bin/clang++.exe',
                    dxc='dxc/bin/x64/dxc.exe',
                    cmakeArguments=['-DSTARLIGHT_RR_STREAMLINE=ON'],
                    cmakePaths=dict(STARLIGHT_DIRECTXMATH_INCLUDE='math/Inc',
                                    STARLIGHT_STREAMLINE_SDK='streamline'))
    (output / 'toolchain.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    inventory = dict(format='starlight-toolchain-inventory-v1',
                     scope='Internal x64 source-build candidate; runtime and redistribution approval remain separate',
                     redistributionCleared=False, files=list(records.values()),
                     bytes=sum(item['bytes'] for item in records.values()))
    (output / 'inventory.json').write_text(json.dumps(inventory, indent=2), encoding='utf-8')
    print(json.dumps(dict(output=str(output), files=len(records), bytes=inventory['bytes'])))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('llvm', 'cmake', 'ninja', 'dxc', 'math', 'streamline', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--resume', action='store_true')
    prepare(**vars(parser.parse_args()))
