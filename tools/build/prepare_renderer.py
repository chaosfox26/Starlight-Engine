from pathlib import Path
import json,sys
root=Path(__file__).resolve().parents[2]
native=root/'editor/native'
s=(root/'deep/engine/render_device.h').read_text()
def swap(a,b):
 global s
 assert a in s,a[:100]
 s=s.replace(a,b)
swap('struct Vertex { float px, py, pz; float nx, ny, nz; };','struct Vertex { float px, py, pz; float nx, ny, nz; float r=1,g=1,b=1,kind=0; };')
swap('    SpotLight  spots[32];','    SpotLight  spots[32];\n    XMFLOAT4 shadowBounds[16];')
swap('"  Spot spots[32];\\n"','"  Spot spots[32]; float4 shadowBounds[16];\\n"')
swap('PSOut PSBg(BgOut i) { PSOut po; po.color = float4(fog.rgb, 1.0); po.mvec = 0; return po; }','PSOut PSBg(BgOut i) { PSOut po; float t=saturate(i.pos.y/max(1,-2*mvScale.y)); po.color=float4(lerp(ambient.rgb*.22,ambient.rgb*.7,pow(t,.65)),1);po.mvec=0;return po; }')
swap('float3 nrm : NORMAL; };','float3 nrm : NORMAL; float4 surface : COLOR0; };')
swap('float4 prv : TEXCOORD2; };','float4 prv : TEXCOORD2; float4 surface : COLOR0; };')
swap('"  VSOut o;\\n"','"  VSOut o; o.surface=i.surface;\\n"')
swap('{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },','{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },\n            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },')
a=s.index('            "PSOut PSMain(VSOut i) {');b=s.index('            "PSOut PSAdd',a)
hlsl=(native/'world_surface.hlsl').read_text()
s=s[:a]+''.join('            '+json.dumps(line+'\n')+'\n' for line in hlsl.splitlines())+s[b:]
swap('s.params = XMFLOAT4(rough, textured ? 1.0f : 0.0f, metal, 0.0f);','s.params = XMFLOAT4(rough, textured ? 1.0f : 0.0f, metal, (float)editorTime); s.params.y=editorRaytracing?2:s.params.y;')
swap('o.pos = mul(float4(i.pos, 1.0), mvp);','float3 position=i.pos; if(params.y<2&&i.surface.w>8.5&&i.surface.w<9.5){float anchor=saturate(position.y)*saturate(3-position.y);position.x+=sin(position.y*21+floor(params.w*24)*9)*.018*anchor;position.z+=cos(position.y*17+floor(params.w*24)*7)*.018*anchor;} o.pos = mul(float4(position, 1.0), mvp);')
# Correct normals for nonuniform scaling in the editor's object path.
swap('o.wn  = mul(float4(i.nrm, 0.0), model).xyz;','float3 scale2=float3(dot(model[0].xyz,model[0].xyz),dot(model[1].xyz,model[1].xyz),dot(model[2].xyz,model[2].xyz)); o.wn=mul(float4(i.nrm/max(scale2,.000001),0),model).xyz;')
swap('    std::function<void()> editorDraw;','    std::function<void()> editorDraw;\n    double editorTime=0; bool editorRaytracing=false;\n    std::function<void()> editorPost;')
swap('        if(!linearHdrScene)ups->evaluate(fi);','        if(!linearHdrScene)ups->evaluate(fi);\n        if(editorPost)editorPost();')
(Path(sys.argv[1]) if len(sys.argv)>1 else native/'render_device_editor.h').write_text(s)
print('Generated editor renderer from shared HDR-capable engine.')
