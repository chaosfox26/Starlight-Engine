"""Inventory a Windows preview without copying files or running its executables."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def imports(path):
    data = path.read_bytes()
    def u16(offset): return struct.unpack_from('<H', data, offset)[0]
    def u32(offset): return struct.unpack_from('<I', data, offset)[0]
    if data[:2] != b'MZ':
        raise ValueError('Not a Windows executable')
    pe = u32(0x3c)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Invalid PE header')
    optional = pe + 24
    magic = u16(optional)
    if magic not in (0x10b, 0x20b):
        raise ValueError('Unsupported PE format')
    directories = optional + (112 if magic == 0x20b else 96)
    sections = optional + u16(pe + 20)
    def offset(rva):
        if rva < u32(optional + 60):
            return rva
        for i in range(u16(pe + 6)):
            s = sections + 40 * i
            size, virtual, rawsize, raw = struct.unpack_from('<IIII', data, s + 8)
            if virtual <= rva < virtual + max(size, rawsize):
                result = raw + rva - virtual
                if result >= len(data):
                    break
                return result
        raise ValueError('Import address outside executable')
    def name(rva):
        start = offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii')
    result = []
    for index, stride, name_field in ((1, 20, 12), (13, 32, 4)):
        rva = u32(directories + index * 8)
        if not rva:
            continue
        cursor = offset(rva)
        while any(data[cursor:cursor + stride]):
            if cursor + stride > len(data):
                raise ValueError('Truncated import table')
            if index == 13 and not u32(cursor) & 1:
                raise ValueError('Legacy delay import needs explicit review')
            result.append({'name': name(u32(cursor + name_field)),
                           'delayLoaded': index == 13})
            cursor += stride
    return result


# Explicit Windows components, not every DLL found on the developer's machine.
SYSTEM = set('kernel32.dll user32.dll gdi32.dll advapi32.dll shell32.dll ole32.dll '
             'oleaut32.dll comdlg32.dll comctl32.dll shlwapi.dll shcore.dll '
             'bcrypt.dll crypt32.dll wintrust.dll ws2_32.dll winhttp.dll '
             'winmm.dll ntdll.dll ucrtbase.dll msvcrt.dll imm32.dll version.dll '
             'setupapi.dll cfgmgr32.dll propsys.dll powrprof.dll dwmapi.dll '
             'uxtheme.dll psapi.dll dbghelp.dll d3d12.dll dxgi.dll d3dcompiler_47.dll '
             'windowscodecs.dll gdiplus.dll normaliz.dll winspool.drv rpcrt4.dll'.split())


def inspect(root, manifest):
    runtime = root / manifest['runtimeDirectory']
    available = {p.name.lower(): p for p in runtime.iterdir() if p.is_file()}
    queue = list(manifest['required']) + list(manifest['checks'])
    seen, files, missing, errors = set(), [], set(), []
    while queue:
        requested = queue.pop(0)
        key = requested.lower()
        if key in seen:
            continue
        seen.add(key)
        path = available.get(key)
        if path is None:
            missing.add(requested)
            continue
        dependencies = []
        try:
            for entry in imports(path):
                dll = entry['name'].lower()
                system = dll in SYSTEM or dll.startswith(('api-ms-win-', 'ext-ms-win-'))
                dependencies.append(dict(entry, resolution='Windows' if system else 'package'))
                if not system:
                    queue.append(entry['name'])
        except (ValueError, struct.error, UnicodeError) as error:
            errors.append({'file': path.name, 'reason': str(error)})
        files.append({'path': path.relative_to(root).as_posix(),
                      'bytes': path.stat().st_size,
                      'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                      'imports': dependencies})
    blockers = [name for name, passed in manifest['releaseGates'].items() if not passed]
    return {'product': manifest['product'], 'publisher': manifest['publisher'],
            'readyForRelease': not (missing or errors or blockers),
            'files': files, 'missingImportsOrPrograms': sorted(missing),
            'errors': errors, 'unfinishedReleaseGates': blockers,
            'scope': 'Static and delay imports only. Dynamic loading, assets, subprocesses, '
                     'license grants and Sandbox behavior require separate verification.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = json.loads((root / 'tools/packaging/windows-preview.json').read_text())
    result = inspect(root, manifest)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'programsAndLibraries': len(result['files']),
                      'missing': result['missingImportsOrPrograms'],
                      'errors': result['errors'],
                      'unfinishedReleaseGates': result['unfinishedReleaseGates'],
                      'readyForRelease': result['readyForRelease']}))
    return 0 if result['readyForRelease'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
