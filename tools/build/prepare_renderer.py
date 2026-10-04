from pathlib import Path
import json,sys
root=Path(__file__).resolve().parents[2]
native=root/'editor/native'
s=(root/'deep/engine/render_device.h').read_text()
for op in json.loads((root/'tools/build/renderer-transforms.json').read_text())['operations']:
 if op['kind']=='replace':
  assert op['find'] in s,op['find'][:100]
  s=s.replace(op['find'],op['replacement'])
 else:
  a=s.index(op['start']);b=s.index(op['end'],a)
  hlsl=(root/op['source']).read_text().replace('#include "surface_material.hlsl"',(root/'deep/engine/surface_material.hlsl').read_text())
  hlsl=hlsl.replace('#include "foliage_material.hlsl"',(root/'deep/engine/foliage_material.hlsl').read_text())
  hlsl=hlsl.replace('#include "surface_lighting.hlsl"',(root/'deep/engine/surface_lighting.hlsl').read_text())
  hlsl=hlsl.replace('#include "sky_radiance.hlsl"',(root/'deep/engine/sky_radiance.hlsl').read_text())
  hlsl=hlsl.replace('#include "night_sky.hlsl"',(root/'deep/engine/night_sky.hlsl').read_text())
  hlsl=hlsl.replace('#include "solar_radiance.hlsl"',(root/'deep/engine/solar_radiance.hlsl').read_text())
  hlsl=hlsl.replace('#include "shadow_surface.hlsl"',(native/'shadow_surface.hlsl').read_text())
  s=s[:a]+''.join('            '+json.dumps(line+'\n')+'\n' for line in hlsl.splitlines())+s[b:]
(Path(sys.argv[1]) if len(sys.argv)>1 else native/'render_device_editor.h').write_text(s)
(native/'shadow_shader.h').write_text('static const char* kSunShadowShader=R"SUN(\n'+(native/'shadow_depth.hlsl').read_text().replace('#include "grass_wind.hlsl"',(native/'grass_wind.hlsl').read_text())+'\n)SUN";\n')
print('Generated editor renderer from shared HDR-capable engine.')
