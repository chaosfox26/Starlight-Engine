#pragma once
#include <array>
#include <atmosphere_shader.h>
#include <dxr_shader.h>
struct AtmospherePass {
 ComPtr<ID3D12RootSignature> signature;
 ComPtr<ID3D12PipelineState> inject,integrate,composite,rtInject,advect;
 ComPtr<ID3D12DescriptorHeap> heap;ComPtr<ID3D12DescriptorHeap> frameHeaps[kFrameCount];
 ComPtr<ID3D12Resource> cells,integral,constants,fogField[2],linearOutput;
 unsigned fogIndex=0;float previousTime=0;bool resetFog=true,chaosSeen=false;
 std::vector<float> sourceTypes;
 std::vector<XMFLOAT4> obstacleLow,obstacleHigh,obstacleVelocity;
 struct FireVolume {XMFLOAT4 low,high,tint;};
 std::vector<FireVolume> fires;
 struct DustVolume {XMFLOAT4 low,high,tint;};
 std::vector<DustVolume> dust;
 std::array<float,4096> terrainHeights{};
 void reset(){resetFog=true;chaosSeen=false;}
 uint8_t* mapped=nullptr;
 struct Constants { XMFLOAT4X4 inverse;XMFLOAT4 eyeTime,sun,skyDensity;XMFLOAT4 pos[32],color[32],dir[32],smoke[16],counts,simulation,obstacleLow[128],obstacleHigh[128],obstacleVelocity[128],terrain[1024],fireLow[32],fireHigh[32],fireTint[32],smokeType[16],post,dustLow[24],dustHigh[24],dustTint[24]; };
 static_assert(sizeof(Constants)<=65536);
 static constexpr size_t stride=(sizeof(Constants)+255)&~255;
 void init(RenderDevice& d,ID3D12Resource* output=nullptr){
  linearOutput=output;
  D3D12_DESCRIPTOR_RANGE range[5]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,4,0,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,3,0,0,4},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,5,0,7},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,3,0,8},{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,6,0,9}};
  D3D12_ROOT_PARAMETER params[3]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[0].DescriptorTable={5,range};params[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[1].Descriptor.ShaderRegister=0;params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[2].Descriptor.ShaderRegister=4;
  D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;sampler.MaxLOD=D3D12_FLOAT32_MAX;
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=3;desc.pParameters=params;desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
  ComPtr<ID3DBlob> blob,error;auto check=[](HRESULT hr){if(FAILED(hr))throw std::runtime_error("Atmosphere DX12 resource/pipeline failure");};
  check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));check(d.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&signature)));
  D3D_SHADER_MACRO verifyMacros[]={{"STARLIGHT_DUST_VERIFY","1"},{nullptr,nullptr}};const bool verifyDust=GetEnvironmentVariableA("STARLIGHT_DUST_VERIFY",nullptr,0)>0; /* diagnostic: paints a sample magenta if the filtered obstacle test ever differs from the full test */
  auto compile=[&](const char* entry,ComPtr<ID3D12PipelineState>& pipeline){if(FAILED(D3DCompile(kAtmosphereShader,strlen(kAtmosphereShader),"atmosphere",verifyDust?verifyMacros:nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error)))throw std::runtime_error(error?(char*)error->GetBufferPointer():"Atmosphere shader failed");D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=signature.Get();pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};check(d.device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pipeline)));};
  compile("Inject",inject);compile("Integrate",integrate);compile("Composite",composite);compile("AdvectFog",advect);
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE3D;rd.Width=160;rd.Height=90;rd.DepthOrArraySize=64;rd.MipLevels=1;rd.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;rd.SampleDesc.Count=1;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  check(d.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&cells)));check(d.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&integral)));
  rd.Width=64;rd.Height=32;rd.DepthOrArraySize=64;
  for(auto& field:fogField)check(d.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&field)));
  hp.Type=D3D12_HEAP_TYPE_UPLOAD;rd={};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=stride*kFrameCount;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  check(d.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&constants)));check(constants->Map(0,nullptr,(void**)&mapped));
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=10;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;check(d.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
  auto cpu=starlight_cpu_heap_start(heap);UINT inc=d.device->GetDescriptorHandleIncrementSize(hd.Type);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.device->CreateShaderResourceView(d.sceneColor.Get(),&srv,cpu);cpu.ptr+=inc;srv.Format=DXGI_FORMAT_R32_FLOAT;d.device->CreateShaderResourceView(d.depth.Get(),&srv,cpu);cpu.ptr+=inc;
  srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;srv.Texture3D.MipLevels=1;srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;d.device->CreateShaderResourceView(cells.Get(),&srv,cpu);cpu.ptr+=inc;d.device->CreateShaderResourceView(integral.Get(),&srv,cpu);cpu.ptr+=inc;
  D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE3D;uav.Texture3D.WSize=64;uav.Format=srv.Format;d.device->CreateUnorderedAccessView(cells.Get(),nullptr,&uav,cpu);cpu.ptr+=inc;d.device->CreateUnorderedAccessView(integral.Get(),nullptr,&uav,cpu);cpu.ptr+=inc;uav={};uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;uav.Format=linearOutput?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;d.device->CreateUnorderedAccessView(linearOutput?linearOutput.Get():d.upOutput.Get(),nullptr,&uav,cpu);
 }
 void resize(RenderDevice& d){
  auto cpu=starlight_cpu_heap_start(heap);UINT inc=d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
  d.device->CreateShaderResourceView(d.sceneColor.Get(),&srv,cpu);cpu.ptr+=inc;srv.Format=DXGI_FORMAT_R32_FLOAT;d.device->CreateShaderResourceView(d.depth.Get(),&srv,cpu);
  cpu.ptr+=5*inc;D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;uav.Format=linearOutput?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;d.device->CreateUnorderedAccessView(linearOutput?linearOutput.Get():d.upOutput.Get(),nullptr,&uav,cpu);
 }
 void draw(RenderDevice& d,const FrameCB& light,float time,float density,const std::vector<XMFLOAT4>& smoke,ID3D12Resource* scene=nullptr,D3D12_GPU_VIRTUAL_ADDRESS acceleration=0,int quality=0){
  if(acceleration&&!rtInject){D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=signature.Get();pd.CS={kDxrFog,sizeof(kDxrFog)};if(FAILED(d.device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&rtInject))))throw std::runtime_error("RT fog pipeline failed");}
  Constants c{};c.post.x=linearOutput?1.f:0.f;c.post.y=float(std::min<size_t>(24,dust.size()));
  for(int i=0;i<int(c.post.y);++i){c.dustLow[i]=dust[i].low;c.dustHigh[i]=dust[i].high;c.dustTint[i]=dust[i].tint;}XMStoreFloat4x4(&c.inverse,XMMatrixInverse(nullptr,d.viewProjNoJit));c.eyeTime=XMFLOAT4(d.camWorld.x,d.camWorld.y,d.camWorld.z,time);c.sun=light.lightDir;c.skyDensity=XMFLOAT4(light.ambient.x,light.ambient.y,light.ambient.z,density);
  for(int i=0;i<32;i++)if(light.spots[i].pos.w>0){int j=(int)c.counts.x++;c.pos[j]=light.spots[i].pos;c.color[j]=light.spots[i].color;c.dir[j]=light.spots[i].dir;}
  c.counts.y=(float)std::min<size_t>(16,smoke.size());for(int i=0;i<(int)c.counts.y;i++)c.smoke[i]=smoke[i];
  for(int i=0;i<int(c.counts.y);++i){c.smokeType[i].x=i<int(sourceTypes.size())?sourceTypes[i]:1.f;chaosSeen|=c.smokeType[i].x>.5f;}
  c.counts.z=float(std::min<size_t>(32,fires.size()));c.counts.w=float(quality);
  for(int i=0;i<int(c.counts.z);++i){c.fireLow[i]=fires[i].low;c.fireHigh[i]=fires[i].high;c.fireTint[i]=fires[i].tint;}
  float dt=std::clamp(time-previousTime,0.f,.05f);bool resetField=resetFog||time<previousTime;previousTime=time;
  c.simulation={dt,resetField?1.f:0.f,float(std::min<size_t>(128,obstacleLow.size())),chaosSeen?1.f:0.f};
  for(int i=0;i<int(c.simulation.z);++i){c.obstacleLow[i]=obstacleLow[i];c.obstacleHigh[i]=obstacleHigh[i];if(i<int(obstacleVelocity.size()))c.obstacleVelocity[i]=obstacleVelocity[i];}
  memcpy(c.terrain,terrainHeights.data(),sizeof(c.terrain));
  bool updateField=resetField||dt>0;unsigned target=updateField?1-fogIndex:fogIndex;
  size_t slot=d.frameIndex%kFrameCount;if(!frameHeaps[slot]){auto hd=starlight_heap_desc(heap);hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(FAILED(d.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&frameHeaps[slot]))))throw std::runtime_error("Atmosphere descriptor allocation failed");}
  auto table=frameHeaps[slot].Get();auto cpu=starlight_cpu_heap_start(table);
  // Copy from a non shader-visible staging heap: shader-visible heaps cannot be copy sources.
  d.device->CopyDescriptorsSimple(7,cpu,starlight_cpu_heap_start(heap),D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;d.device->CreateShaderResourceView(scene?scene:d.sceneColor.Get(),&srv,cpu);
  UINT inc=d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);cpu.ptr+=7*inc;
  srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE3D;srv.Texture3D.MipLevels=1;d.device->CreateShaderResourceView(fogField[fogIndex].Get(),&srv,cpu);cpu.ptr+=inc;
  D3D12_UNORDERED_ACCESS_VIEW_DESC fieldUav{};fieldUav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE3D;fieldUav.Format=srv.Format;fieldUav.Texture3D.WSize=64;
  d.device->CreateUnorderedAccessView(fogField[target].Get(),nullptr,&fieldUav,cpu);cpu.ptr+=inc;d.device->CreateShaderResourceView(fogField[target].Get(),&srv,cpu);

memcpy(mapped+slot*stride,&c,sizeof(c));ID3D12DescriptorHeap* heaps[]={table};d.cmd->SetDescriptorHeaps(1,heaps);d.cmd->SetComputeRootSignature(signature.Get());d.cmd->SetComputeRootDescriptorTable(0,starlight_gpu_heap_start(table));d.cmd->SetComputeRootConstantBufferView(1,constants->GetGPUVirtualAddress()+slot*stride);
  if(updateField){d.barrier(fogField[target].Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);d.cmd->SetPipelineState(advect.Get());d.cmd->Dispatch(16,8,16);d.barrier(fogField[target].Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);fogIndex=target;resetFog=false;}
  d.barrier(cells.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);if(acceleration)d.cmd->SetComputeRootShaderResourceView(2,acceleration);d.cmd->SetPipelineState(acceleration?rtInject.Get():inject.Get());d.cmd->Dispatch(20,18,16);d.barrier(cells.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  d.barrier(integral.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);d.cmd->SetPipelineState(integrate.Get());d.cmd->Dispatch(20,18,1);d.barrier(integral.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  if(linearOutput)d.barrier(linearOutput.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  d.cmd->SetPipelineState(composite.Get());d.cmd->Dispatch((d.w+7)/8,(d.h+7)/8,1);
  if(linearOutput)d.barrier(linearOutput.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
 }
};
