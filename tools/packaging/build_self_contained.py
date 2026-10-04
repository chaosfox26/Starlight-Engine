import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--package', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
package = args.package.resolve(strict=True)
output = args.output.resolve()
assert not output.exists(), 'Output already exists'
manifest = json.loads((package / 'starlight-package.json').read_text(encoding='utf-8'))
paths = [row['path'] for row in manifest['files']] + ['starlight-package.json']
expected = {row['path']: row['sha256'] for row in manifest['files']}
build = root / 'out/self-contained-build'
build.mkdir(parents=True, exist_ok=True)
vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
vs = Path(subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
                                  'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                                  '-property', 'installationPath'], text=True).strip())
dev = vs / 'Common7/Tools/VsDevCmd.bat'
block = subprocess.check_output(f'cmd.exe /d /s /c ""{dev}" -arch=x64 -host_arch=x64 >nul && set"', text=True)
env = os.environ.copy()
for line in block.splitlines():
    if '=' in line and not line.startswith('='):
        key, value = line.split('=', 1)
        env[key] = value
compiler = sorted((vs / 'VC/Tools/MSVC').glob('*/bin/Hostx64/x64/cl.exe'))[-1]
rc = sorted((Path(os.environ['ProgramFiles(x86)']) / 'Windows Kits/10/bin').glob('*/x64/rc.exe'))[-1]
resource = build / 'setup.rc'
resource.write_text('103 RCDATA \"'+(root/'design/branding/starlight-setup-nebula-v1.png').as_posix()+'\"\n102 RCDATA \"' + (root/'design/branding/starlight-glass-icon-v1.png').as_posix() + '\"\n101 ICON "' + (root / 'design/branding/starlight-editor.ico').as_posix() + '"\n')
stub = build / 'Starlight-Setup-stub.exe'
with (build / 'build.log').open('w') as log:
    subprocess.run([str(rc), '/nologo', '/fo', str(build / 'setup.res'), str(resource)],
                   env=env, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
    subprocess.run([str(compiler), '/nologo', '/std:c++20', '/EHsc', '/O2', '/MT', '/utf-8',
                    str(root / 'tools/packaging/self_contained.cpp'), str(build / 'setup.res'),
                    '/Fe:' + str(stub), '/Fo:' + str(build / 'setup.obj'), '/link', '/SUBSYSTEM:WINDOWS',
                    '/MANIFEST:EMBED', 'user32.lib', 'shell32.lib', 'comctl32.lib', 'ole32.lib',
                    'advapi32.lib', 'bcrypt.lib', 'uuid.lib', 'gdiplus.lib', 'dwmapi.lib', 'gdi32.lib', 'cabinet.lib', 'gdi32.lib'],
                   env=env, cwd=build, stdout=log, stderr=subprocess.STDOUT, check=True)
cabinet = ctypes.WinDLL('cabinet.dll', use_last_error=True)
cabinet.CreateCompressor.argtypes = [wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
cabinet.CreateCompressor.restype = wintypes.BOOL
cabinet.Compress.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                            ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
cabinet.Compress.restype = wintypes.BOOL
cabinet.CloseCompressor.argtypes = [ctypes.c_void_p]
handle = ctypes.c_void_p()
if not cabinet.CreateCompressor(4, None, ctypes.byref(handle)):
    raise ctypes.WinError(ctypes.get_last_error())
output.parent.mkdir(parents=True, exist_ok=True)
records = []
try:
    with output.open('xb') as stream:
        stream.write(stub.read_bytes())
        for name in sorted(paths):
            path = package / name
            assert path.resolve(strict=True).is_relative_to(package)
            data = path.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            assert name not in expected or digest == expected[name], 'Package changed: ' + name
            stored, compressed = data, False
            if data:
                size = ctypes.c_size_t()
                cabinet.Compress(handle, data, len(data), None, 0, ctypes.byref(size))
                if not size.value:
                    raise ctypes.WinError(ctypes.get_last_error())
                buffer = ctypes.create_string_buffer(size.value)
                if not cabinet.Compress(handle, data, len(data), buffer, len(buffer), ctypes.byref(size)):
                    raise ctypes.WinError(ctypes.get_last_error())
                if size.value < len(data):
                    stored, compressed = buffer.raw[:size.value], True
            records.append(dict(path=name, offset=stream.tell(), stored=len(stored), bytes=len(data),
                                compressed=compressed, sha256=digest))
            stream.write(stored)
        index = json.dumps(dict(format='starlight-self-contained-v1', files=records), separators=(',', ':')).encode()
        stream.write(index)
        stream.write(b'STARLIGHT-PACK01' + struct.pack('<Q', len(index)) + hashlib.sha256(index).hexdigest().encode())
finally:
    cabinet.CloseCompressor(handle)
with output.open('rb') as stream:
    digest = hashlib.file_digest(stream, 'sha256').hexdigest()
result = dict(executable=str(output), files=len(records), bytes=output.stat().st_size, sha256=digest,
              compiler='Installed Visual Studio C++', compression='Windows XPRESS Huffman',
              runtimeDependencies='Windows built-in APIs; no external extractor', verifiedExtraction=False)
output.with_suffix('.build.json').write_text(json.dumps(result, indent=2))
print(json.dumps(result, indent=2))
