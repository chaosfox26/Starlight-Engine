#pragma once
#include <array>

// A shared linear-light finish for raster, ray tracing and volumetric effects.
// Only excess radiance is blurred; authored geometry and material detail stay
// at native resolution. Descriptors are immutable until a drained resize.
struct HdrPostPass {
 ComPtr<ID3D12RootSignature> signature;ComPtr<ID3D12PipelineState> pipeline;
 ComPtr<ID3D12DescriptorHeap> heap;
 ComPtr<ID3D12Resource> scene;
 std::array<ComPtr<ID3D12Resource>,6> down,up;
 struct Pass {ID3D12Resource* output;UINT descriptor,mode;};
 std::vector<Pass> passes;
 UINT levels=5;bool enabled=true;float strength=.12f;UINT64 allocationBytes=0;
 void init(RenderDevice& d){
  wchar_t setting[32]{};
  if(GetEnvironmentVariableW(L"STARLIGHT_HDR_BLOOM",setting,32)){
   if(wcscmp(setting,L"0")&&wcscmp(setting,L"1"))throw std::runtime_error("Bloom setting must be 0 or 1");enabled=setting[0]==L'1';
  }
  if(GetEnvironmentVariableW(L"STARLIGHT_BLOOM_LEVELS",setting,32)){
   wchar_t* end=nullptr;auto value=wcstoul(setting,&end,10);if(end==setting||*end||value<3||value>6)throw std::runtime_error("Bloom levels must be from 3 to 6");levels=UINT(value);
  }
  D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,2,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,2}};
  D3D12_ROOT_PARAMETER params[2]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={2,ranges};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[1].Constants={0,0,4};
  D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=params;desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
  ComPtr<ID3DBlob> blob,error;d.must(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"HDR post signature serialization");d.must(d.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&signature)),"HDR post signature");
  const char* code=R"(
cbuffer Post : register(b0){uint mode;float strength;uint bloomEnabled;uint unused;}
Texture2D<float4> inputColor:register(t0);Texture2D<float4> lowColor:register(t1);
RWTexture2D<float4> outputColor:register(u0);SamplerState linearClamp:register(s0);
float3 sampleColor(float2 uv){return max(0,inputColor.SampleLevel(linearClamp,uv,0).rgb);}
float3 prefilter(float3 c){float brightness=max(c.x,max(c.y,c.z));float soft=clamp(brightness-.5,0,1);soft=soft*soft*.25;return c*(max(brightness-1,soft)/max(brightness,.00001));}
float3 tone(float3 c){c=max(0,c);return saturate((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14));}
float luma(float3 c){return dot(c,float3(.299,.587,.114));}
float3 displayColor(float2 uv){return tone(sampleColor(uv)+(bloomEnabled?lowColor.SampleLevel(linearClamp,uv,0).rgb*strength:0));}
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){
 uint w,h;outputColor.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;float2 uv=(id.xy+.5)/float2(w,h);
 uint iw,ih;inputColor.GetDimensions(iw,ih);float2 texel=1./float2(iw,ih);
 if(mode<=1){
  // A tent filter across the source footprint keeps subpixel emitters stable
  // as they enter the pyramid. Apply the soft knee before averaging.
  float3 result=0;
  [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x){float3 c=sampleColor(uv+float2(x,y)*texel);result+=(mode==0?prefilter(c):c)*(x==0?2:1)*(y==0?2:1)/16.;}
  outputColor[id.xy]=float4(result,1);return;
 }
 if(mode==2){
  uint lw,lh;lowColor.GetDimensions(lw,lh);float2 step=1./float2(lw,lh);float3 blurred=0;
  [unroll]for(int y=-1;y<=1;++y)[unroll]for(int x=-1;x<=1;++x)blurred+=lowColor.SampleLevel(linearClamp,uv+float2(x,y)*step,0).rgb*(x==0?2:1)*(y==0?2:1)/16.;
  outputColor[id.xy]=float4(lerp(sampleColor(uv),blurred,.65),1);return;
 }
 float3 c=displayColor(uv),n=displayColor(uv+float2(0,-1)*texel),s=displayColor(uv+float2(0,1)*texel),e=displayColor(uv+float2(1,0)*texel),west=displayColor(uv+float2(-1,0)*texel);
 float lc=luma(c),ln=luma(n),ls=luma(s),le=luma(e),lw=luma(west);
 float contrast=max(lc,max(max(ln,ls),max(le,lw)))-min(lc,min(min(ln,ls),min(le,lw)));
 if(contrast>.07){float3 across=abs(ln+ls-2*lc)>=abs(le+lw-2*lc)?(n+s)*.5:(e+west)*.5;float blend=min(.35,abs((ln+ls+le+lw)*.25-lc)/max(contrast,.0001)*.55);c=lerp(c,across,blend);}
 // A stable, zero-centred one-code-value dither before UNORM quantization
 // breaks broad sky bands without temporal noise or blurring native detail.
 uint bits=id.x*1664525u+id.y*1013904223u+374761393u;
 bits=(bits^(bits>>16))*2246822519u;bits=(bits^(bits>>13))*3266489917u;bits^=bits>>16;
 float dither=((bits&65535u)/65535.-.5)/255.;
 outputColor[id.xy]=float4(saturate(pow(c,1/2.2)+dither),1);
}
)";
  if(FAILED(D3DCompile(code,strlen(code),"hdr-post",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error)))throw std::runtime_error(error?(char*)error->GetBufferPointer():"HDR post shader failed");
  D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=signature.Get();ps.CS={blob->GetBufferPointer(),blob->GetBufferSize()};d.must(d.device->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pipeline)),"HDR post pipeline");resize(d);
 }
 void resize(RenderDevice& d){
  allocationBytes=0;passes.clear();down={};up={};
  auto texture=[&](UINT width,UINT height){ComPtr<ID3D12Resource> resource;D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=width;rd.Height=height;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;rd.SampleDesc.Count=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;d.must(d.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&resource)),"HDR post texture");allocationBytes+=d.device->GetResourceAllocationInfo(0,1,&rd).SizeInBytes;return resource;};
  scene=texture(d.w,d.h);UINT width=d.w,height=d.h;
  if(enabled)for(UINT i=0;i<levels;++i){width=std::max(1u,(width+1)/2);height=std::max(1u,(height+1)/2);down[i]=texture(width,height);if(i+1<levels)up[i]=texture(width,height);}
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=levels*6;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;d.must(d.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"HDR post descriptors");
  UINT inc=d.device->GetDescriptorHandleIncrementSize(hd.Type);auto cpu=starlight_cpu_heap_start(heap);
  auto pass=[&](ID3D12Resource* input,ID3D12Resource* low,ID3D12Resource* output,UINT mode){
   UINT descriptor=UINT(passes.size()*3);D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
   d.device->CreateShaderResourceView(input,&srv,cpu);cpu.ptr+=inc;d.device->CreateShaderResourceView(low,&srv,cpu);cpu.ptr+=inc;
   D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=starlight_resource_desc(output).Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d.device->CreateUnorderedAccessView(output,nullptr,&uav,cpu);cpu.ptr+=inc;passes.push_back({output,descriptor,mode});
  };
  if(enabled){for(UINT i=0;i<levels;++i)pass(i?down[i-1].Get():scene.Get(),scene.Get(),down[i].Get(),i?1:0);
   for(int i=int(levels)-2;i>=0;--i)pass(down[i].Get(),i==int(levels)-2?down[i+1].Get():up[i+1].Get(),up[i].Get(),2);}
  pass(scene.Get(),enabled?up[0].Get():scene.Get(),d.upOutput.Get(),3);
 }
 void draw(RenderDevice& d){
  ID3D12DescriptorHeap* heaps[]={heap.Get()};d.cmd->SetDescriptorHeaps(1,heaps);d.cmd->SetComputeRootSignature(signature.Get());d.cmd->SetPipelineState(pipeline.Get());
  const UINT inc=d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  for(const auto& pass:passes){if(!enabled&&pass.mode!=3)continue;
   bool final=pass.mode==3;if(!final)d.barrier(pass.output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   auto table=starlight_gpu_heap_start(heap);table.ptr+=SIZE_T(pass.descriptor)*inc;d.cmd->SetComputeRootDescriptorTable(0,table);
   struct Constants {UINT mode;float strength;UINT enabled,pad;} c{pass.mode,strength,enabled?1u:0u,0};d.cmd->SetComputeRoot32BitConstants(1,4,&c,0);
   auto desc=starlight_resource_desc(pass.output);d.cmd->Dispatch((UINT(desc.Width)+7)/8,(desc.Height+7)/8,1);
   if(!final)d.barrier(pass.output,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
 }
};
