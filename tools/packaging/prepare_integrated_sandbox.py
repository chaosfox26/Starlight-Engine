"""Create an isolated, repeatable full-package Sandbox test from the latest stage."""
import json
import argparse
from pathlib import Path
import shutil
import subprocess
import time
from xml.sax.saxutils import escape

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--focused', action='store_true', help='Only rerun portable crash diagnostics and denoiser checks')
args = parser.parse_args()
script = 'check_focused_sandbox.ps1' if args.focused else 'check_integrated_sandbox.ps1'
record = json.loads((root/'out/native-package-checks/latest-check.json').read_text(encoding='utf-8'))
package = Path(record['package']).resolve()
if not package.is_relative_to((root/'out/native-package-checks').resolve()):
    raise RuntimeError('Latest stage is outside the package staging directory')
subprocess.run([str(package/'Starlight.exe'), '--verify-package'], check=True, timeout=120)
base = root/'out/integrated-sandbox'/time.strftime('%Y%m%d-%H%M%S')
base.mkdir(parents=True, exist_ok=False)
source, results = base/'input', base/'results'
source.mkdir()
results.mkdir()
shutil.copytree(package, source/'package')
shutil.copy2(root/'tools/packaging'/script, source)
config = f'''<Configuration>
<VGpu>Enable</VGpu><Networking>Disable</Networking><ClipboardRedirection>Disable</ClipboardRedirection>
<MappedFolders>
<MappedFolder><HostFolder>{escape(str(source))}</HostFolder><SandboxFolder>C:\\StarlightInput</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
<MappedFolder><HostFolder>{escape(str(results))}</HostFolder><SandboxFolder>C:\\StarlightResults</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
</MappedFolders>
<LogonCommand><Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\\StarlightInput\\{script}</Command></LogonCommand>
</Configuration>'''
path = base/'integrated-check.wsb'
path.write_text(config, encoding='utf-8')
result = dict(configuration=str(path), results=str(results), package=str(package), profile='focused' if args.focused else 'full')
(base.parent/'latest-check.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result))
