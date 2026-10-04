"""Prepare a minimal launcher lifecycle fixture, never a public engine release."""
import hashlib
import json
from pathlib import Path
import shutil
from xml.sax.saxutils import escape

root = Path(__file__).resolve().parents[2]
base = root / 'out' / 'launcher-sandbox'
source, results = base / 'input', base / 'results'
package = source / 'package'
package.mkdir(parents=True, exist_ok=True)
results.mkdir(parents=True, exist_ok=True)
exe = package / 'Starlight.exe'
shutil.copy2(root / 'out/launcher/Starlight.exe', exe)
manifest = {'format': 'starlight-portable-v1', 'publisher': 'ChaosFox', 'testFixture': True,
            'editor': 'Starlight.exe', 'files': [
                {'path': exe.name, 'sha256': hashlib.sha256(exe.read_bytes()).hexdigest()}]}
(package / 'starlight-package.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
shutil.copy2(root / 'tools/packaging/check_launcher_sandbox.ps1', source)
config = fr'''<Configuration>
<Networking>Disable</Networking>
<ClipboardRedirection>Disable</ClipboardRedirection>
<MappedFolders>
<MappedFolder><HostFolder>{escape(str(source))}</HostFolder><SandboxFolder>C:\StarlightFixture</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
<MappedFolder><HostFolder>{escape(str(results))}</HostFolder><SandboxFolder>C:\StarlightResults</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
</MappedFolders>
<LogonCommand><Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\StarlightFixture\check_launcher_sandbox.ps1</Command></LogonCommand>
</Configuration>'''
(base / 'launcher-check.wsb').write_text(config, encoding='utf-8')
print(base / 'launcher-check.wsb')
