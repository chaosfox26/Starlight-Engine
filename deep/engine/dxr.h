#pragma once
#include <map>
#include <dxr_shader.h>
#include "ray_reconstruction.h"

// Owned by one native render thread. Descriptor tables and upload data are per fence slot.
struct DxrItem {uint64_t key;const Mesh* mesh;XMFLOAT4X4 world;XMFLOAT4 color;uint64_t motionKey=0;};
struct DxrPass {
 static_assert(sizeof(Vertex)==56,"DXR shader vertex history layout must match native uploads");
 std::map<uint64_t,XMFLOAT4X4> previousWorldByKey;
 struct Blas {ComPtr<ID3D12Resource> result;UINT vertex=0,index=0;};
 struct Instance {XMFLOAT4X4 world,normal,previousWorld;XMFLOAT4 color;UINT meta[4];};
 struct Constants {XMFLOAT4X4 inverse;XMFLOAT4 eyeTime,sun,ambient,pos[32],color[32],dir[32],smoke[16];UINT settings[4];XMFLOAT4 frame;UINT sequence[4];XMFLOAT4X4 previousVP,view,previousView,currentVP;};
 static constexpr UINT stride=(sizeof(Constants)+255)&~255;
 ComPtr<ID3D12Device5> device;ComPtr<ID3D12GraphicsCommandList4> command;
 ComPtr<ID3D12RootSignature> signature;ComPtr<ID3D12PipelineState> trace,prefilter,temporal,atrous,composite;
 ComPtr<ID3D12Resource> vertices,indices,tlas,scratch,instanceUpload,descriptionUpload,constants;
 ComPtr<ID3D12Resource> readback;
 static constexpr UINT imageCount=26;
 ComPtr<ID3D12Resource> images[imageCount];D3D12_RESOURCE_STATES states[imageCount]{};
 RayReconstruction rr;int denoiserMode=1,previousMode=-1,previousTier=-1,previousBounces=-1;bool rrActive=false;
 std::vector<DxrItem> previousItems;XMFLOAT4X4 previousVP{},previousView{};XMFLOAT3 previousEye{};
 UINT historyParity=0;uint64_t lastTopology=0;bool historyValid=false;ULONGLONG previousTime=0;
 void imageState(RenderDevice& d,UINT i,D3D12_RESOURCE_STATES state){if(states[i]!=state){d.barrier(images[i].Get(),states[i],state);states[i]=state;}}
 void shutdown(){rr.shutdown();}

 ComPtr<ID3D12DescriptorHeap> heaps[kFrameCount];ComPtr<ID3D12QueryHeap> queries;
 uint8_t *instanceMap=nullptr,*descriptionMap=nullptr,*constantMap=nullptr;
 std::map<uint64_t,Blas> cache;bool supported=false,initialized=false;std::string status="DXR not initialized";
 int requested=0,bounces=3,active=0;UINT samples=0;uint64_t builds=0,refits=0,lastHash=0,historyResets=0;double rayMs=0,asMs=0,denoiseMs=0;UINT64 frequency=0;
 bool timed[kFrameCount]{};UINT capacity=0,previousCount=0;bool tlasBuilt=false;
 static void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("DXR resource/pipeline failure (HRESULT "+std::to_string((unsigned)hr)+")");}
 ComPtr<ID3D12Resource> buffer(UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,bool uav=false){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=type;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;r.Width=std::max<UINT64>(256,bytes);r.Height=1;r.DepthOrArraySize=1;r.MipLevels=1;r.SampleDesc.Count=1;r.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;r.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;ComPtr<ID3D12Resource> resource;auto initial=state==D3D12_RESOURCE_STATE_UNORDERED_ACCESS?D3D12_RESOURCE_STATE_COMMON:state;check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,initial,nullptr,IID_PPV_ARGS(&resource)));if(initial!=state){D3D12_RESOURCE_BARRIER transition{};transition.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;transition.Transition={resource.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,initial,state};command->ResourceBarrier(1,&transition);}return resource;
 }
 void uav(ID3D12Resource* r){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=r;command->ResourceBarrier(1,&b);}
 void init(RenderDevice& d){
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 caps{};D3D12_FEATURE_DATA_SHADER_MODEL shader{D3D_SHADER_MODEL_6_5};
  if(GetEnvironmentVariableW(L"STARLIGHT_DISABLE_DXR",nullptr,0)){status="DXR disabled by test override; raster fallback";return;}
  if(FAILED(d.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&caps,sizeof(caps)))||caps.RaytracingTier<D3D12_RAYTRACING_TIER_1_1||FAILED(d.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&shader,sizeof(shader)))||shader.HighestShaderModel<D3D_SHADER_MODEL_6_5){status="DXR 1.1 / shader model 6.5 unsupported; raster fallback";return;}
  if(FAILED(d.device.As(&device))||FAILED(d.cmd.As(&command))){status="DXR interfaces unavailable; raster fallback";return;}supported=true;status="DXR 1.1 supported";
 }
 void createImages(RenderDevice& d){
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_RESOURCE_DESC r{};r.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;r.Width=d.w;r.Height=d.h;r.DepthOrArraySize=1;r.MipLevels=1;r.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;r.SampleDesc.Count=1;r.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  for(UINT i=0;i<imageCount;i++){r.Format=(i==4||i==24)?DXGI_FORMAT_R32_FLOAT:(i==25?DXGI_FORMAT_R16G16_FLOAT:((i==13||i==17)?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R16G16B16A16_FLOAT));images[i].Reset();states[i]=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&r,states[i],nullptr,IID_PPV_ARGS(&images[i])));}
 }
 void resize(RenderDevice& d){
  if(!initialized)return;
  createImages(d);
  for(auto& heap:heaps){
   auto cpu=heap->GetCPUDescriptorHandleForHeapStart();UINT inc=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
   for(UINT i=0;i<imageCount;i++){D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;srv.Format=images[i]->GetDesc().Format;device->CreateShaderResourceView(images[i].Get(),&srv,cpu);cpu.ptr+=inc;}
   for(UINT i=0;i<imageCount;i++){D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;uav.Format=images[i]->GetDesc().Format;device->CreateUnorderedAccessView(images[i].Get(),nullptr,&uav,cpu);cpu.ptr+=inc;}
  }
  rr.shutdown();rr.attempted=false;rr.failed=false;historyValid=false;historyParity=0;samples=0;
 }
 void initializeResources(RenderDevice& d){
  D3D12_DESCRIPTOR_RANGE ranges[2]={{D3D12_DESCRIPTOR_RANGE_TYPE_SRV,imageCount,4,0,0},{D3D12_DESCRIPTOR_RANGE_TYPE_UAV,imageCount,0,0,imageCount}};
  D3D12_ROOT_PARAMETER p[7]{};for(int i=0;i<4;i++){p[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;p[i].Descriptor.ShaderRegister=i;}p[4].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;p[5].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p[5].DescriptorTable={2,ranges};
  p[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;p[6].Constants.ShaderRegister=1;p[6].Constants.Num32BitValues=2;
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=7;desc.pParameters=p;ComPtr<ID3DBlob> blob,error;check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&signature)));
  auto pipeline=[&](const unsigned char* bytes,size_t size,ComPtr<ID3D12PipelineState>& dest){D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=signature.Get();pd.CS={bytes,size};check(device->CreateComputePipelineState(&pd,IID_PPV_ARGS(&dest)));};pipeline(kDxrTrace,sizeof(kDxrTrace),trace);pipeline(kDxrPrefilter,sizeof(kDxrPrefilter),prefilter);pipeline(kDxrTemporal,sizeof(kDxrTemporal),temporal);pipeline(kDxrAtrous,sizeof(kDxrAtrous),atrous);pipeline(kDxrComposite,sizeof(kDxrComposite),composite);
  createImages(d);
  constants=buffer(stride*kFrameCount,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);check(constants->Map(0,nullptr,(void**)&constantMap));
  instanceUpload=buffer(sizeof(Instance)*kMaxInstances*kFrameCount,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);check(instanceUpload->Map(0,nullptr,(void**)&instanceMap));
  descriptionUpload=buffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC)*kMaxInstances*kFrameCount,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);check(descriptionUpload->Map(0,nullptr,(void**)&descriptionMap));
  for(int slot=0;slot<kFrameCount;slot++){
   D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=imageCount*2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;check(device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heaps[slot])));auto cpu=heaps[slot]->GetCPUDescriptorHandleForHeapStart();UINT inc=device->GetDescriptorHandleIncrementSize(hd.Type);
   for(UINT i=0;i<imageCount;i++){D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;srv.Format=images[i]->GetDesc().Format;device->CreateShaderResourceView(images[i].Get(),&srv,cpu);cpu.ptr+=inc;}
   for(UINT i=0;i<imageCount;i++){D3D12_UNORDERED_ACCESS_VIEW_DESC view{};view.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;view.Format=images[i]->GetDesc().Format;device->CreateUnorderedAccessView(images[i].Get(),nullptr,&view,cpu);cpu.ptr+=inc;}
  }
  D3D12_QUERY_HEAP_DESC q{};q.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;q.Count=kFrameCount*4;check(device->CreateQueryHeap(&q,IID_PPV_ARGS(&queries)));readback=buffer(kFrameCount*4*sizeof(UINT64),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);check(d.queue->GetTimestampFrequency(&frequency));initialized=true;
 }
 // Called between frames, before renderEmpty resets the allocator. Old resources retire after flush.
 void retireGeometry(RenderDevice& d){
  d.flush();for(auto& item:cache)d.recycleBuffer(item.second.result,true);cache.clear();d.recycleBuffer(vertices);d.recycleBuffer(indices);d.recycleBuffer(tlas,true);d.recycleBuffer(scratch);
  capacity=previousCount=0;tlasBuilt=false;historyValid=false;previousItems.clear();
  samples=0;lastHash=lastTopology=0;
 }
 void prepare(RenderDevice& d,const std::vector<DxrItem>& items){
  active=supported?requested:0;if(!active)return;
  if(!initialized)initializeResources(d);
  std::map<uint64_t,const Mesh*> unique;for(auto& item:items)unique.emplace(item.key,item.mesh);
  bool changed=unique.size()!=cache.size();for(auto& pair:unique)if(!cache.count(pair.first))changed=true;
  if(!changed)return;
  d.flush();d.alloc->Reset();d.cmd->Reset(d.alloc.Get(),nullptr);
  d.recycleBuffer(vertices);d.recycleBuffer(indices);
  std::vector<Vertex> v;std::vector<uint32_t> ix;std::map<uint64_t,Blas> next;
  for(auto& pair:unique){Blas b;if(cache.count(pair.first))b=cache.at(pair.first);b.vertex=(UINT)v.size();b.index=(UINT)ix.size();v.insert(v.end(),pair.second->verts.begin(),pair.second->verts.end());ix.insert(ix.end(),pair.second->indices.begin(),pair.second->indices.end());next.emplace(pair.first,b);}
  if(!v.empty()){
   vertices=d.createDefaultBuffer(v.data(),v.size()*sizeof(Vertex),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);indices=d.createDefaultBuffer(ix.data(),ix.size()*sizeof(uint32_t),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   std::vector<ComPtr<ID3D12Resource>> temporary;
   for(auto& pair:next)if(!pair.second.result){auto& b=pair.second;auto mesh=unique.at(pair.first);D3D12_RAYTRACING_GEOMETRY_DESC g{};g.Type=D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;g.Flags=D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;g.Triangles.VertexBuffer={vertices->GetGPUVirtualAddress()+b.vertex*sizeof(Vertex),sizeof(Vertex)};g.Triangles.VertexCount=(UINT)mesh->verts.size();g.Triangles.VertexFormat=DXGI_FORMAT_R32G32B32_FLOAT;g.Triangles.IndexBuffer=indices->GetGPUVirtualAddress()+b.index*sizeof(uint32_t);g.Triangles.IndexCount=(UINT)mesh->indices.size();g.Triangles.IndexFormat=DXGI_FORMAT_R32_UINT;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;build.Inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;build.Inputs.NumDescs=1;build.Inputs.pGeometryDescs=&g;build.Inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};device->GetRaytracingAccelerationStructurePrebuildInfo(&build.Inputs,&info);if(!info.ResultDataMaxSizeInBytes)throw std::runtime_error("Invalid BLAS prebuild size");b.result=d.acquireBuffer(info.ResultDataMaxSizeInBytes,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,true);auto temp=d.acquireBuffer(info.ScratchDataSizeInBytes,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);build.DestAccelerationStructureData=b.result->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=temp->GetGPUVirtualAddress();command->BuildRaytracingAccelerationStructure(&build,0,nullptr);uav(b.result.Get());temporary.push_back(temp);builds++;
   }
   d.cmd->Close();ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();d.retireUploads();for(auto& temp:temporary)d.recycleBuffer(temp);
  }else{d.cmd->Close();d.recycleBuffer(tlas,true);d.recycleBuffer(scratch);capacity=0;}
  for(auto& old:cache)if(!next.count(old.first))d.recycleBuffer(old.second.result,true);
  cache=std::move(next);tlasBuilt=false;samples=0;lastHash=0;historyValid=false;previousItems.clear();
 }
 ID3D12Resource* draw(RenderDevice& d,const std::vector<DxrItem>& items,const FrameCB& light,float time,float density,const std::vector<XMFLOAT4>& smoke,bool animated){
  if(!active||items.empty()){rrActive=false;historyValid=false;rayMs=asMs=denoiseMs=0;if(items.empty())tlasBuilt=false;return d.sceneColor.Get();}
  const UINT slot=d.slot,count=(UINT)items.size();
  if(timed[slot]){UINT64* data=nullptr;D3D12_RANGE read{slot*4*sizeof(UINT64),(slot+1)*4*sizeof(UINT64)};check(readback->Map(0,&read,(void**)&data));auto values=data+slot*4;asMs=(values[1]-values[0])*1000.0/frequency;rayMs=(values[2]-values[1])*1000.0/frequency;denoiseMs=(values[3]-values[2])*1000.0/frequency;D3D12_RANGE none{0,0};readback->Unmap(0,&none);}
  command->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*4);
  auto data=(Instance*)(instanceMap+slot*kMaxInstances*sizeof(Instance));auto desc=(D3D12_RAYTRACING_INSTANCE_DESC*)(descriptionMap+slot*kMaxInstances*sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
  uint64_t hash=1469598103934665603ull;auto mix=[&](const void* p,size_t n){auto bytes=(const uint8_t*)p;for(size_t i=0;i<n;i++){hash^=bytes[i];hash*=1099511628211ull;}};
  // Upload heaps may be write-combined: never hash/read or bitfield-update them in place.
  for(UINT i=0;i<count;i++){
   auto& item=items[i];auto& b=cache.at(item.key);Instance entry{};entry.world=item.world;auto previous=previousWorldByKey.find(item.motionKey);entry.previousWorld=item.motionKey&&previous!=previousWorldByKey.end()?previous->second:item.world;
   XMStoreFloat4x4(&entry.normal,XMMatrixTranspose(XMMatrixInverse(nullptr,XMLoadFloat4x4(&item.world))));entry.color=item.color;entry.meta[0]=b.vertex;entry.meta[1]=b.index;
   D3D12_RAYTRACING_INSTANCE_DESC instance{};for(UINT row=0;row<3;row++)for(UINT col=0;col<4;col++)instance.Transform[row][col]=item.world.m[col][row];instance.InstanceID=i;instance.InstanceMask=255;instance.Flags=D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;instance.AccelerationStructure=b.result->GetGPUVirtualAddress();
   mix(&entry.world,sizeof(entry.world));mix(&entry.color,sizeof(entry.color));memcpy(data+i,&entry,sizeof(entry));memcpy(desc+i,&instance,sizeof(instance));
  }
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build{};build.Inputs.Type=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;build.Inputs.DescsLayout=D3D12_ELEMENTS_LAYOUT_ARRAY;build.Inputs.NumDescs=count;build.Inputs.InstanceDescs=descriptionUpload->GetGPUVirtualAddress()+slot*kMaxInstances*sizeof(D3D12_RAYTRACING_INSTANCE_DESC);build.Inputs.Flags=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE|D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info{};device->GetRaytracingAccelerationStructurePrebuildInfo(&build.Inputs,&info);
  if(count>capacity){if(tlas)d.flush();d.recycleBuffer(tlas,true);d.recycleBuffer(scratch);tlas=d.acquireBuffer(info.ResultDataMaxSizeInBytes,D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,true);scratch=d.acquireBuffer(std::max(info.ScratchDataSizeInBytes,info.UpdateScratchDataSizeInBytes),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,true);capacity=count;tlasBuilt=false;}
  if(tlasBuilt&&previousCount==count){build.Inputs.Flags|=D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;build.SourceAccelerationStructureData=tlas->GetGPUVirtualAddress();refits++;}
  uav(tlas.Get());uav(scratch.Get());build.DestAccelerationStructureData=tlas->GetGPUVirtualAddress();build.ScratchAccelerationStructureData=scratch->GetGPUVirtualAddress();command->BuildRaytracingAccelerationStructure(&build,0,nullptr);uav(tlas.Get());previousCount=count;tlasBuilt=true;
  command->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*4+1);
  Constants c{};XMStoreFloat4x4(&c.inverse,XMMatrixInverse(nullptr,d.viewProjNoJit));c.eyeTime={d.camWorld.x,d.camWorld.y,d.camWorld.z,time};c.sun=light.lightDir;c.ambient=light.ambient;for(UINT i=0;i<32;i++)if(light.spots[i].pos.w>0){UINT j=c.settings[3]++;c.pos[j]=light.spots[i].pos;c.color[j]=light.spots[i].color;c.dir[j]=light.spots[i].dir;}for(size_t i=0;i<smoke.size()&&i<16;i++)c.smoke[i]=smoke[i];c.settings[0]=active;c.settings[1]=bounces;c.frame={(float)d.w,(float)d.h,density,(float)smoke.size()};

  XMMATRIX view=XMMatrixLookAtRH(XMLoadFloat3(&d.editorEye),XMLoadFloat3(&d.editorTarget),XMLoadFloat3(&d.editorUp));
  XMFLOAT4X4 projection;XMStoreFloat4x4(&projection,XMMatrixInverse(nullptr,view)*d.viewProjNoJit);XMStoreFloat4x4(&c.view,view);c.previousVP=previousVP;c.previousView=previousView;XMStoreFloat4x4(&c.currentVP,d.viewProjNoJit);
  uint64_t topology=1469598103934665603ull;for(auto& item:items){topology^=item.key;topology*=1099511628211ull;}
  float dx=d.camWorld.x-previousEye.x,dy=d.camWorld.y-previousEye.y,dz=d.camWorld.z-previousEye.z;
  bool reset=!historyValid||lastTopology!=topology||previousMode!=denoiserMode||previousTier!=active||previousBounces!=bounces||dx*dx+dy*dy+dz*dz>9;
  mix(&c.inverse,sizeof(c.inverse));mix(&c.sun,sizeof(c.sun));mix(&c.ambient,sizeof(c.ambient));mix(c.pos,sizeof(c.pos));mix(c.color,sizeof(c.color));mix(c.dir,sizeof(c.dir));mix(c.smoke,sizeof(c.smoke));mix(c.settings,sizeof(c.settings));mix(&c.frame,sizeof(c.frame));
  if(hash!=lastHash||animated){samples=0;historyResets++;}lastHash=hash;c.settings[2]=samples;c.sequence[0]=(UINT)d.frameIndex;c.sequence[1]=reset?1:0;c.sequence[2]=animated?1:0;c.sequence[3]=light.motionTime.z>0?1:0;
  memcpy(constantMap+slot*stride,&c,sizeof(c));
  auto bind=[&]{ID3D12DescriptorHeap* hs[]={heaps[slot].Get()};command->SetDescriptorHeaps(1,hs);command->SetComputeRootSignature(signature.Get());command->SetComputeRootShaderResourceView(0,tlas->GetGPUVirtualAddress());command->SetComputeRootShaderResourceView(1,vertices->GetGPUVirtualAddress());command->SetComputeRootShaderResourceView(2,indices->GetGPUVirtualAddress());command->SetComputeRootShaderResourceView(3,instanceUpload->GetGPUVirtualAddress()+slot*kMaxInstances*sizeof(Instance));command->SetComputeRootConstantBufferView(4,constants->GetGPUVirtualAddress()+slot*stride);command->SetComputeRootDescriptorTable(5,heaps[slot]->GetGPUDescriptorHandleForHeapStart());};
  auto dispatch=[&](ID3D12PipelineState* pipeline,UINT iteration){UINT constants[2]={iteration,historyParity};command->SetComputeRoot32BitConstants(6,2,constants,0);command->SetPipelineState(pipeline);command->Dispatch((d.w+7)/8,(d.h+7)/8,1);};
  for(UINT i=0;i<=8;i++)imageState(d,i,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);imageState(d,24,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);imageState(d,25,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);bind();dispatch(trace.Get(),0);
  command->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*4+2);
  for(UINT i=0;i<=8;i++)imageState(d,i,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  imageState(d,24,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);imageState(d,25,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);bool previouslyRR=rrActive;rrActive=false;ULONGLONG now=GetTickCount64();float delta=previousTime?float(now-previousTime):16.667f;previousTime=now;
  bool rrReady=false;if(denoiserMode==1){rrReady=rr.create(device.Get(),command.Get(),d.w,d.h);bind();}
  if(rrReady){
   imageState(d,23,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   rrActive=rr.evaluate(command.Get(),images[0].Get(),images[23].Get(),images[2].Get(),images[3].Get(),images[1].Get(),images[4].Get(),images[25].Get(),images[24].Get(),d.w,d.h,reset,delta,c.view,projection);
   imageState(d,23,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);bind();
  }
  if(!rrActive){
   if(previouslyRR){c.sequence[1]=1;memcpy(constantMap+slot*stride,&c,sizeof(c));}
   if(denoiserMode==0)rr.status="Native temporal wavelet denoiser selected";
   for(UINT i=9;i<=10;i++)imageState(d,i,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);dispatch(prefilter.Get(),0);for(UINT i=9;i<=10;i++)imageState(d,i,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   UINT current=11+historyParity*4;for(UINT i=current;i<current+4;i++)imageState(d,i,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);dispatch(temporal.Get(),0);for(UINT i=current;i<current+4;i++)imageState(d,i,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   for(UINT iteration=0;iteration<3;iteration++){UINT dest=iteration==1?21:19;imageState(d,dest,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);imageState(d,dest+1,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);dispatch(atrous.Get(),iteration);imageState(d,dest,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);imageState(d,dest+1,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
   imageState(d,23,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);dispatch(composite.Get(),0);imageState(d,23,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  }
  historyValid=true;historyParity=1-historyParity;lastTopology=topology;previousMode=denoiserMode;previousTier=active;previousBounces=bounces;previousItems=items;previousWorldByKey.clear();for(const auto& item:items)if(item.motionKey)previousWorldByKey[item.motionKey]=item.world;XMStoreFloat4x4(&previousVP,d.viewProjNoJit);previousView=c.view;previousEye=d.camWorld;
  command->EndQuery(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*4+3);command->ResolveQueryData(queries.Get(),D3D12_QUERY_TYPE_TIMESTAMP,slot*4,4,readback.Get(),slot*4*sizeof(UINT64));timed[slot]=true;samples=std::min(samples+1,1024u);return images[23].Get();
 }
};
