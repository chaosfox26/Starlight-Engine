from pathlib import Path
import json,sys
root=Path(__file__).resolve().parents[2]
native=root/'editor/native'
s=(root/'deep/engine/render_device.h').read_text()
def swap(a,b):
 global s
 assert a in s,a[:100]
 s=s.replace(a,b)
swap('struct Vertex { float px, py, pz; float nx, ny, nz; };','struct Vertex { float px, py, pz; float nx, ny, nz; float r=1,g=1,b=1,kind=0; float previousX=0,previousY=0,previousZ=0,hasPrevious=0; };')
swap('    SpotLight  spots[32];','    SpotLight  spots[32];\n    XMFLOAT4 shadowBounds[16];\n    XMFLOAT4 motionTime;')
swap('"  Spot spots[32];\\n"','"  Spot spots[32]; float4 shadowBounds[16]; float4 motionTime;\\n"')
swap('PSOut PSBg(BgOut i) { PSOut po; po.color = float4(fog.rgb, 1.0); po.mvec = 0; return po; }','PSOut PSBg(BgOut i) { PSOut po; float t=saturate(i.pos.y/max(1,-2*mvScale.y)); po.color=float4(lerp(ambient.rgb*.22,ambient.rgb*.7,pow(t,.65)),1);po.mvec=0;return po; }')
swap('float3 nrm : NORMAL; };','float3 nrm : NORMAL; float4 surface : COLOR0; float4 history : TEXCOORD3; };')
swap('float4 prv : TEXCOORD2; };','float4 prv : TEXCOORD2; float4 surface : COLOR0; };')
swap('"  VSOut o;\\n"','"  VSOut o; o.surface=i.surface;\\n"')
swap('{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },','{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },\n            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },\n            { "TEXCOORD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },')
a=s.index('            "PSOut PSMain(VSOut i) {');b=s.index('            "PSOut PSAdd',a)
hlsl=(native/'world_surface.hlsl').read_text()
s=s[:a]+''.join('            '+json.dumps(line+'\n')+'\n' for line in hlsl.splitlines())+s[b:]
swap('s.params = XMFLOAT4(rough, textured ? 1.0f : 0.0f, metal, 0.0f);','s.params = XMFLOAT4(rough, textured ? 1.0f : 0.0f, metal, (float)editorTime); s.params.y=editorRaytracing?2:s.params.y;')
deform = """float3 environmentPosition(float3 p,float4 surface,float time) {
 float3 world=mul(float4(p,1),model).xyz;
 if(surface.w>.5&&surface.w<1.2){float bend=max(p.y,0);bend*=bend;float gust=sin(time*1.7+world.x*.4+world.z*.25);p.x+=bend*(.12*gust+.035*sin(time*4.1+world.z));p.z+=bend*(.075*cos(time*1.3+world.z*.3)+.02*sin(time*3+world.x));}
 if(params.y<2&&surface.w>8.5&&surface.w<9.2){float anchor=surface.y;p.x+=anchor*(sin(p.y*13+time*29)*.12+sin(p.y*29-time*41)*.045);p.z+=anchor*(cos(p.y*17-time*31)*.12+sin(p.y*31+time*37)*.045);}
 return p;
}
"""
swap('            "VSOut VSMain(VSIn i) {\\n"', ''.join('            '+json.dumps(line+'\n')+'\n' for line in deform.splitlines())+'            "VSOut VSMain(VSIn i) {\\n"')
swap('o.pos = mul(float4(i.pos, 1.0), mvp);','float3 position=environmentPosition(i.pos,i.surface,motionTime.x);float3 previousPosition=environmentPosition((i.history.w>0&&motionTime.z>0)?i.history.xyz:i.pos,i.surface,motionTime.y);o.pos=mul(float4(position,1),mvp);')
swap('o.wp  = mul(float4(i.pos, 1.0), model).xyz;','o.wp=mul(float4(position,1),model).xyz;')
swap('o.cur = mul(float4(i.pos, 1.0), mvpNoJit);','o.cur=mul(float4(position,1),mvpNoJit);')
swap('o.prv = mul(float4(i.pos, 1.0), prevMvp);','o.prv=mul(float4(previousPosition,1),prevMvp);')

# Correct normals for nonuniform scaling in the editor's object path.
swap('o.wn  = mul(float4(i.nrm, 0.0), model).xyz;','float3 scale2=float3(dot(model[0].xyz,model[0].xyz),dot(model[1].xyz,model[1].xyz),dot(model[2].xyz,model[2].xyz)); o.wn=mul(float4(i.nrm/max(scale2,.000001),0),model).xyz;')
swap('    std::function<void()> editorDraw;','    std::function<void()> editorDraw;\n    double editorTime=0; bool editorRaytracing=false;\n    std::function<void()> editorPost;')
swap('        if(!linearHdrScene)ups->evaluate(fi);','        if(!linearHdrScene)ups->evaluate(fi);\n        if(editorPost)editorPost();')
# Stable object identities keep rigid motion history valid when draw order changes.
swap('    std::function<void()> editorDraw;', '    uint64_t editorMotionKey=0; bool editorKeyedHistory=false;\n    std::function<void()> editorDraw;')
swap('        s.mvScale = XMFLOAT4(0.5f * (float)rw, -0.5f * (float)rh, 0, 0);', '        if(editorKeyedHistory){\n            auto previous=prevModelByKey.find(editorMotionKey);\n            if(resetHistory)s.prevMvp=s.mvpNoJit;\n            else if(editorMotionKey && previous!=prevModelByKey.end())XMStoreFloat4x4(&s.prevMvp,XMMatrixMultiply(XMLoadFloat4x4(&previous->second),prevViewProjNoJit));\n            else XMStoreFloat4x4(&s.prevMvp,XMMatrixMultiply(model,prevViewProjNoJit));\n            if(editorMotionKey)XMStoreFloat4x4(&curModelByKey[editorMotionKey],model);\n        }\n        s.mvScale = XMFLOAT4(0.5f * (float)rw, -0.5f * (float)rh, 0, 0);')
swap('resetHistory = (instCursor != prevInstCount);','resetHistory = !editorKeyedHistory && (instCursor != prevInstCount);')
(Path(sys.argv[1]) if len(sys.argv)>1 else native/'render_device_editor.h').write_text(s)
print('Generated editor renderer from shared HDR-capable engine.')
