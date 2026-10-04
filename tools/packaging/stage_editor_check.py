"""Stage the real editor for internal launch testing, not a feature-complete release."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
from preview_preflight import inspect

root = Path(__file__).resolve().parents[2]
config = json.loads((root / 'tools/packaging/windows-preview.json').read_text())
preflight = inspect(root, config)
if preflight['missingImportsOrPrograms'] or preflight['errors']:
    raise RuntimeError('Runtime import preflight failed')
target = root / 'out' / 'editor-package-checks' / time.strftime('%Y%m%d-%H%M%S')
target.mkdir(parents=True, exist_ok=False)
entries = []

def copy(source, relative, origin):
    destination = target / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    entries.append({'path': relative, 'sha256': hashlib.sha256(destination.read_bytes()).hexdigest(),
                    'origin': origin})

copy(root / 'out/launcher/Starlight.exe', 'Starlight.exe', 'Native package launcher')
for item in preflight['files']:
    source = root / item['path']
    copy(source, 'runtime/' + source.name, 'Current native runtime build')
for name in ('starlight-editor.ico', 'starlight-glass-icon-v1.png',
             'starlight-alpha-one-cursive-v3.png', 'starlight-alpha-one-audio.wav'):
    relative = 'design/branding/' + name
    copy(root / relative, relative, 'Approved first-party branding')
for name in ('LICENSE', 'THIRD-PARTY-NOTICES.md'):
    copy(root / name, name, 'Current repository notice')
for source in (root / 'licenses').iterdir():
    if source.is_file():
        copy(source, 'runtime/licenses/' + source.name, 'Preserved dependency notice')
copy(root / 'LICENSE', 'runtime/licenses/Starlight-LICENSE.txt', 'Starlight original-code license')
copy(root / 'THIRD-PARTY-NOTICES.md', 'runtime/licenses/THIRD-PARTY-NOTICES.md', 'Runtime dependency notices')
manifest = {'format': 'starlight-portable-v1', 'publisher': 'ChaosFox',
            'editor': 'runtime/Starlight.NativeEditor.exe', 'files': entries,
            'purpose': 'Internal editor launch check. Analysis parity and final redistribution review incomplete.',
            'releaseReady': False, 'unfinishedReleaseGates': preflight['unfinishedReleaseGates']}
(target / 'starlight-package.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
subprocess.run([str(target / 'Starlight.exe'), '--verify-package'], check=True, timeout=60)
result = subprocess.run([str(target / 'runtime/Starlight.NativeEditor.exe'), '--describe'],
                        capture_output=True, text=True, timeout=60)
if result.returncode:
    raise RuntimeError('Staged editor command check failed: ' + result.stderr)
description = json.loads(result.stdout)
if not description.get('success'):
    raise RuntimeError('Staged editor command check did not report success')
(target.parent / 'latest-check.json').write_text(json.dumps(
    {'package': str(target), 'manifestVerified': True, 'editorCommandCheck': True,
     'interactiveLaunchVerified': False, 'releaseReady': False}, indent=2), encoding='utf-8')
print(json.dumps({'package': str(target), 'files': len(entries),
                  'bytes': sum(p.stat().st_size for p in target.rglob('*') if p.is_file()),
                  'editorCommandCheck': True, 'releaseReady': False}))
