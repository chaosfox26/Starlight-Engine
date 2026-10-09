"""The archive policy must never exempt active or changed managed code."""
import json
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
import struct
import tempfile

source = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='starlight-native-guard-') as tmp:
    root = Path(tmp)
    checks = root / 'tools/checks'
    checks.mkdir(parents=True)
    for name in ('verify_no_managed.py', 'historical-managed-references.json'):
        shutil.copy2(source / name, checks / name)
    entry = json.loads((checks / 'historical-managed-references.json').read_text())['references'][0]
    # A tiny CLR-marked PE fixture keeps this policy test independent of the
    # user's untracked historical audit binaries and portable to a clean clone.
    binary = bytearray(512)
    binary[:2] = b'MZ'
    struct.pack_into('<I', binary, 0x3c, 0x80)
    binary[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', binary, 0x80 + 24, 0x20b)
    struct.pack_into('<I', binary, 0x80 + 24 + 112 + 14*8, 0x1000)
    entry['sha256'] = hashlib.sha256(binary).hexdigest()
    (checks / 'historical-managed-references.json').write_text(json.dumps({'references':[entry]}))
    archive = root / entry['path']
    archive.parent.mkdir(parents=True)
    archive.write_bytes(binary)
    def run(expected):
        result = subprocess.run([sys.executable, str(checks / 'verify_no_managed.py'), '--output', str(root / 'result.json')], capture_output=True, text=True)
        assert result.returncode == expected, result.stdout + result.stderr
        return json.loads((root / 'result.json').read_text())
    assert len(run(0)['historicalReferences']) == 1
    active = root / 'out/Release/Starlight.Editor.dll'
    active.parent.mkdir(parents=True)
    shutil.copy2(archive, active)
    assert len(run(1)['violations']) == 1
    active.unlink()
    with archive.open('ab') as out:
        out.write(b'changed')
    assert len(run(1)['violations']) == 1
    archive.write_bytes(binary)
    (archive.parent / 'unexpected.cs').write_text('class Unexpected {}')
    assert len(run(1)['violations']) == 1
    (archive.parent / 'unexpected.cs').unlink()
    late = bytearray(8192)
    late[:2] = b'MZ'
    struct.pack_into('<I', late, 0x3c, 0x1800)
    late[0x1800:0x1804] = b'PE\0\0'
    struct.pack_into('<H', late, 0x1800 + 24, 0x20b)
    struct.pack_into('<I', late, 0x1800 + 24 + 112 + 14*8, 0x1000)
    managed = root / 'out/Release/late-header.dll'
    managed.write_bytes(late)
    assert len(run(1)['violations']) == 1
    late[0x1800:0x1804] = b'XX\0\0'
    managed.write_bytes(late)
    assert len(run(1)['violations']) == 1
print('PASS: pinned archive, active copy, changed archive, unexpected archived source, late and invalid PE headers')
