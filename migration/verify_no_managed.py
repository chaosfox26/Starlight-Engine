"""Repository guard: no C# sources/projects/runtime manifests or managed PE images."""
from pathlib import Path
import json, struct, subprocess
root=Path(__file__).resolve().parents[1]
violations=[]; images=0
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
    if reason: violations.append({'path':str(p.relative_to(root)),'reason':reason})
report={'success':not violations,'scope':str(root),'peImagesChecked':images,'violations':violations}
(root/'migration/no-managed-results.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
raise SystemExit(bool(violations))
