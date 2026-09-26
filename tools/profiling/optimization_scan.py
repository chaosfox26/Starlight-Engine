from pathlib import Path
import os,re,json,hashlib,collections,subprocess
root=Path.cwd(); out=root/'out/optimization-audit';out.mkdir(parents=True,exist_ok=True)
ignored={'.git','out','build','bin','third_party','node_modules','audit-runs','generated','__pycache__'}
exts={'.cpp','.h','.hpp','.inl','.hlsl','.c','.cs','.py','.cmake'}
files=[]; hits=[]; groups=collections.defaultdict(list); totals=collections.Counter()
patterns={
 'allocation_or_growth':r'\b(?:new|make_unique|make_shared)\b|\.\s*(?:resize|reserve|push_back|emplace_back|insert)\s*\(',
 'associative_container':r'(?:std::)?(?:unordered_map|unordered_set|map|set)\s*<',
 'sync_wait_flush':r'\b(?:flush|wait|wait_until|wait_for|WaitForSingleObject|SetEvent|notify_all|lock_guard|unique_lock)\b',
 'validation':r'\bvalidate\s*\(',
 'gui_churn':r'\b(?:CreateWindow\w*|DestroyWindow|MoveWindow|SetWindowPos|InvalidateRect|SetWindowTextW?|SetTimer|KillTimer)\s*\(',
 'simd_candidate':r'XMMatrixInverse|world_point\(|local_point\(|std::hypot|std::sin|std::cos|memcpy\(',
 'dispatch_abstraction':r'\bvirtual\b|std::function|__declspec\(dllimport\)',
 'worker_layout':r'alignas|atomic|thread_local|threadBuckets|PersistentWorkers',
}
for base,dirs,names in os.walk(root):
 dirs[:]=[d for d in dirs if d not in ignored and not (Path(base)/d).is_symlink()]
 for name in names:
  p=Path(base)/name
  if p.suffix.lower() not in exts and name!='CMakeLists.txt':continue
  if p.stat().st_size>20*1024*1024:continue
  data=p.read_bytes();text=data.decode('utf-8',errors='replace');rel=p.relative_to(root).as_posix();lines=text.splitlines()
  bucket='engine' if rel.startswith('deep/engine/') else 'editor' if rel.startswith('editor/') else 'projects' if rel.startswith('deep/projects/') else 'physics' if rel.startswith('deep/Physics/') else 'tools/other'
  digest=hashlib.sha256(data).hexdigest();groups[digest].append(rel)
  files.append(dict(file=rel,lines=len(lines),bytes=len(data),sha256=digest,scope=bucket));totals[bucket]+=1
  for n,line in enumerate(lines,1):
   for category,pattern in patterns.items():
    if re.search(pattern,line):hits.append(dict(file=rel,line=n,category=category,text=line.strip()[:1500]))
manifest=dict(files=files,scopeCounts=dict(totals),totalLines=sum(f['lines'] for f in files),excludedDirectoryNames=sorted(ignored),findingsAre='lexical review sites, not compiler reachability or hardware-counter measurements',duplicateFiles=[g for g in groups.values() if len(g)>1])
(out/'manifest.json').write_text(json.dumps(manifest,indent=2));(out/'review-sites.json').write_text(json.dumps(hits,indent=2))
probe=out/'layout_probe.cpp'
probe.write_text('''#include "native_session.hpp"
#include <cstdio>
#include <type_traits>
using namespace starlight::editor;
#define EMIT(T) std::printf("\\\"" #T "\\\":{\\\"bytes\\\":%zu,\\\"alignment\\\":%zu,\\\"standardLayout\\\":%s},",sizeof(T),alignof(T),std::is_standard_layout_v<T>?"true":"false")
int main(){ std::printf("{"); EMIT(Vec3);EMIT(Vertex);EMIT(Mesh);EMIT(MeshPart);EMIT(Character);EMIT(SceneData);EMIT(TerrainData);EMIT(TerrainFlow);EMIT(FlowObstacleMask);EMIT(RenderStatus);EMIT(CameraState);EMIT(SceneHistory);EMIT(std::string);std::printf("\\\"configuration\\\":\\\"MSVC x64 C++20 Release /MD\\\"}"); }
''')
vs=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
install=subprocess.check_output([str(vs),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
vc=Path(install)/'Common7/Tools/VsDevCmd.bat'
env=os.environ.copy();block=subprocess.check_output(f'cmd.exe /d /s /c ""{vc}" -arch=x64 -host_arch=x64 >nul && set"',text=True)
for line in block.splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
exe=out/'layout_probe.exe'
with (out/'layout-build.log').open('w') as log:
 compiler=sorted((Path(install)/'VC/Tools/MSVC').glob('*/bin/Hostx64/x64/cl.exe'))[-1]
 subprocess.run([str(compiler),'/nologo','/std:c++20','/EHsc','/O2','/MD','/DUNICODE','/D_UNICODE','/DNOMINMAX','/I'+str(root/'editor/cpp'),str(probe),'/Fe:'+str(exe),'/Fo:'+str(out/'layout_probe.obj')],env=env,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
layout=json.loads(subprocess.check_output([str(exe)],text=True));(out/'layouts.json').write_text(json.dumps(layout,indent=2))
print(json.dumps(dict(files=len(files),lines=manifest['totalLines'],scopeCounts=totals,sites=len(hits),layouts=layout),indent=2))
