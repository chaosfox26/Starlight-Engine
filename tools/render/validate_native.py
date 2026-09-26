"""Exercise native rendering fallbacks and inspect PE runtime headers without .NET."""
import argparse, json, os, pathlib, struct, subprocess

root = pathlib.Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--config', default='Release')
p.add_argument('--pe-only', action='store_true')
p.add_argument('--build-dir',type=pathlib.Path,default=root/'out/audit-engine')
p.add_argument('--packet',type=pathlib.Path)
p.add_argument('--output',type=pathlib.Path,required=True)
args = p.parse_args()
if not args.pe_only and (not args.packet or not args.packet.is_file()):p.error('--packet must identify an existing scene packet')
build = args.build_dir.resolve()
output = args.output.resolve()
output.mkdir(parents=True,exist_ok=False)
results = []
cases = [(f'tier-{i}', i, 0, {}) for i in range(5)]
cases += [('rr-auto', 3, 1, {}), ('rr-disabled', 3, 1, {'STARLIGHT_DISABLE_RR':'1'}),
          ('dxr-disabled', 4, 1, {'STARLIGHT_DISABLE_DXR':'1'}),
          ('rr-missing', 3, 1, {'STARLIGHT_RR_DIRECTORY':str(output/'missing-sdk')})]
if args.pe_only: cases=[]
for name,tier,denoiser,extra in cases:
    report,shot = output/(name+'.json'), output/(name+'.bmp')
    env = dict(os.environ)
    env.update(extra)
    cmd = [str(build/args.config/'Starlight.NativeRenderChecks.exe'), str(args.packet),
           str(report),str(shot),'960','540',str(tier),'headless','4',str(denoiser)]
    run = subprocess.run(cmd,env=env,timeout=75,capture_output=True)
    data = json.loads(report.read_text()) if report.exists() else {}
    data['case'] = name
    data['exitCode'] = run.returncode
    data['passed'] = run.returncode == 0 and data.get('success') and data.get('renderWidth') == 960
    if name == 'dxr-disabled': data['passed'] &= data.get('activeTier') == 0
    if name in ('rr-disabled','rr-missing'): data['passed'] &= not data.get('rrActive')
    results.append(data)
    print(name, 'PASS' if data['passed'] else 'FAIL', flush=True)
if results: (output/'render-matrix.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
pe=[]
for file in (build/args.config).rglob('*'):
    if file.suffix.lower() not in ('.exe','.dll'): continue
    b=file.read_bytes(); offset=struct.unpack_from('<I',b,0x3c)[0]
    machine, count=struct.unpack_from('<HH',b,offset+4)
    optional=offset+24; magic=struct.unpack_from('<H',b,optional)[0]
    directories=optional+(112 if magic==0x20b else 96)
    clr=struct.unpack_from('<II',b,directories+14*8)
    sections=optional+struct.unpack_from('<H',b,offset+20)[0]
    def raw(rva):
        for i in range(count):
            s=sections+i*40
            vs,va,rs,rp=struct.unpack_from('<IIII',b,s+8)
            if va<=rva<va+max(vs,rs): return rp+rva-va
        raise ValueError('Invalid PE RVA')
    irva,isize=struct.unpack_from('<II',b,directories+8)
    imports=[]
    if irva:
        cursor=raw(irva)
        while any(struct.unpack_from('<IIIII',b,cursor)):
            name=raw(struct.unpack_from('<I',b,cursor+12)[0]); end=b.index(0,name)
            imports.append(b[name:end].decode('ascii')); cursor+=20
    managed=[x for x in imports if any(y in x.lower() for y in ('mscoree','coreclr','hostfxr','hostpolicy','clr.dll'))]
    pe.append(dict(path=str(file.relative_to(root)),x64=machine==0x8664,clr=list(clr),imports=imports,
                   passed=machine==0x8664 and not any(clr) and not managed))
(output/'native-pe.json').write_text(json.dumps(pe,indent=2),encoding='utf-8')
if not pe or not all(x['passed'] for x in results+pe): raise SystemExit(1)
