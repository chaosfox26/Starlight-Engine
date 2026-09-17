"""Generate shader artifacts into the CMake build tree, never the source tree."""
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[2]
output = Path(sys.argv[1])
dxc = sys.argv[2]
output.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, str(root/'editor/native/prepare_renderer.py'), str(output/'render_device_editor.h')], check=True)
(output/'atmosphere_shader.h').write_text('static const char* kAtmosphereShader=R"ATMOS(\n'+(root/'deep/engine/atmosphere.hlsl').read_text()+'\n)ATMOS";', encoding='utf-8')
arrays=[]
for name, entry, source, defines in [(name,name,'dxr.hlsl',[]) for name in ['Trace','Prefilter','Temporal','Atrous','Composite']]+[('Fog','Inject','atmosphere.hlsl',['-DSTARLIGHT_RT_FOG=1'])]:
    binary=output/(name+'.dxil')
    subprocess.run([dxc,'-T','cs_6_5','-E',entry,'-O3',*defines,'-Fo',str(binary),str(root/'deep/engine'/source)], check=True)
    arrays.append('static const unsigned char kDxr'+name+'[]={'+','.join(map(str,binary.read_bytes()))+'};')
(output/'dxr_shader.h').write_text('#pragma once\n'+'\n'.join(arrays),encoding='utf-8')
