"""Native source/output guard, with separately reported immutable audit references."""
from pathlib import Path
import argparse, hashlib, json, struct, subprocess
root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path);args=parser.parse_args()
violations=[]; images=0; historical=[]
# Exact paths AND hashes, never a directory-wide exclusion. Copies in a build
# or a changed archived binary still fail the native guard.
archive_manifest=json.loads((Path(__file__).with_name('historical-managed-references.json')).read_text(encoding='utf-8'))
archives={entry['path']:entry for entry in archive_manifest['references']}
for path in subprocess.check_output(['rg','--files','--hidden','--no-ignore',str(root)],text=True).splitlines():
    p=Path(path); name=p.name.lower(); reason=None
    if p.suffix.lower() in ('.cs','.csproj','.xaml') or name.endswith(('.deps.json','.runtimeconfig.json')):
        reason='Managed source/project/runtime manifest'
    if p.suffix.lower() in ('.props','.targets'):
        t=p.read_text(encoding='utf-8',errors='replace')
        if 'Microsoft.NET.Sdk' in t or 'NuGet' in t: reason='Managed build import'
    if p.suffix.lower() in ('.exe','.dll'):
        with p.open('rb') as f: b=f.read(4096)
        if b[:2]==b'MZ':
            try:
                pe=struct.unpack_from('<I',b,0x3c)[0]; opt=pe+24
                if b[pe:pe+4]!=b'PE\0\0': continue
                directories=opt+(112 if struct.unpack_from('<H',b,opt)[0]==0x20b else 96)
                if struct.unpack_from('<I',b,directories+14*8)[0]: reason='CLR assembly'
                images+=1
            except struct.error: reason='Unable to verify PE header'
    if reason:
        relative=p.relative_to(root).as_posix()
        archived=archives.get(relative)
        finding={'path':str(p.relative_to(root)),'reason':reason}
        if archived and reason=='CLR assembly' and hashlib.sha256(p.read_bytes()).hexdigest()==archived['sha256']:
            historical.append({**finding,'sha256':archived['sha256'],'purpose':archived['purpose']})
        else:
            violations.append(finding)
report={'success':not violations,'scope':str(root),'peImagesChecked':images,'violations':violations,
        'historicalReferences':historical,'scopePolicy':'All repository files scanned. Only exact hash-pinned historical audit reference binaries are classified separately; active sources and all build outputs remain checked.'}
output=args.output or root/'migration/no-managed-results.json'
output.parent.mkdir(parents=True,exist_ok=True)
output.write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
raise SystemExit(bool(violations))
