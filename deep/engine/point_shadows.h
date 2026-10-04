#pragma once

// Every active authored/emitted light retains its shadow. Allocate only the
// required cube-array capacity; no fixed set of "important" lights replaces
// the rest. Six triangle-rendered directions also cover wide cones/omnis.
struct PointShadows {
 ComPtr<ID3D12Resource> depth;
 ComPtr<ID3D12DescriptorHeap> dsv;
 UINT resolution=512,capacity=0,descriptorSize=0;UINT64 bytes=0,draws=0,culled=0;
 bool enabled=true,culling=true,enabledOverride=false,resolutionOverride=false;
 static bool live(const SpotLight& light){return light.pos.w>.06f&&std::max({light.color.x,light.color.y,light.color.z})>0;}
 static UINT count(const std::vector<SpotLight>& lights){UINT result=0;for(const auto& light:lights)if(live(light))++result;return result;}
 void init(RenderDevice& d,ID3D12DescriptorHeap* surfaceHeap){
  wchar_t value[32]{};
  if(GetEnvironmentVariableW(L"STARLIGHT_LOCAL_SHADOWS",value,32)){
   if(wcscmp(value,L"0")&&wcscmp(value,L"1"))throw std::runtime_error("Local shadows must be 0 or 1");enabled=value[0]==L'1';enabledOverride=true;
  }
  if(GetEnvironmentVariableW(L"STARLIGHT_LOCAL_SHADOW_RESOLUTION",value,32)){
   if(wcscmp(value,L"256")&&wcscmp(value,L"512")&&wcscmp(value,L"1024"))throw std::runtime_error("Local shadow resolution must be 256, 512 or 1024");resolution=UINT(_wtoi(value));resolutionOverride=true;
  }
  if(GetEnvironmentVariableW(L"STARLIGHT_LOCAL_SHADOW_CULLING",value,32)){
   if(wcscmp(value,L"0")&&wcscmp(value,L"1"))throw std::runtime_error("Local shadow culling must be 0 or 1");culling=value[0]==L'1';
  }
  prepare(d,surfaceHeap,0);
 }
 void setQuality(RenderDevice& d,ID3D12DescriptorHeap* surfaceHeap,int quality){
  if(!enabledOverride)enabled=quality>0;
  UINT size=resolutionOverride?resolution:quality==1?256:quality==3?1024:512;
  if(size!=resolution){auto requested=capacity;d.flush();depth.Reset();dsv.Reset();capacity=0;resolution=size;prepare(d,surfaceHeap,requested);}
  else if(!enabled)prepare(d,surfaceHeap,0);
 }
 void prepare(RenderDevice& d,ID3D12DescriptorHeap* surfaceHeap,UINT requested){
  if(requested>32)throw std::runtime_error("Local shadow light capacity exceeded");
  if(!enabled)requested=0;
  UINT next=0;if(requested){next=1;while(next<requested)next*=2;}
  // Hysteresis avoids reallocating when intermittent discharges add/remove a
  // light. Release substantially oversized maps on smaller/empty scenes.
  if(depth&&next<=capacity&&next>=capacity/4&&!(next==0&&capacity>0))return;
  d.flush();depth.Reset();dsv.Reset();capacity=next;draws=culled=0;
  const UINT cubes=std::max(1u,capacity),size=capacity?resolution:1;
  D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=size;desc.Height=size;desc.DepthOrArraySize=UINT16(cubes*6);desc.MipLevels=1;desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
  d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(&depth)),"allocate local shadow cubes");bytes=d.device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=cubes*6;d.must(d.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)),"local shadow depth views");descriptorSize=d.device->GetDescriptorHandleIncrementSize(hd.Type);
  auto cpu=starlight_cpu_heap_start(dsv);
  for(UINT i=0;i<cubes*6;++i){D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;view.Texture2DArray.FirstArraySlice=i;view.Texture2DArray.ArraySize=1;d.device->CreateDepthStencilView(depth.Get(),&view,cpu);cpu.ptr+=descriptorSize;}
  cpu=starlight_cpu_heap_start(surfaceHeap);cpu.ptr+=6*d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=DXGI_FORMAT_R32_FLOAT;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;view.TextureCubeArray.NumCubes=cubes;view.TextureCubeArray.MipLevels=1;d.device->CreateShaderResourceView(depth.Get(),&view,cpu);
 }
 static bool visible(const SunShadows::Item& item,const XMMATRIX& matrix){
  unsigned outside=63;const auto combined=item.world*matrix;
  for(int i=0;i<8;++i){XMFLOAT4 p;XMStoreFloat4(&p,XMVector4Transform(XMVectorSet(i&1?item.high.x:item.low.x,i&2?item.high.y:item.low.y,i&4?item.high.z:item.low.z,1),combined));
   unsigned bits=(p.x<-p.w?1:0)|(p.x>p.w?2:0)|(p.y<-p.w?4:0)|(p.y>p.w?8:0)|(p.z<0?16:0)|(p.z>p.w?32:0);outside&=bits;
  }
  return outside==0;
 }
 void draw(RenderDevice& d,SunShadows& sun,const std::vector<SunShadows::Item>& items,FrameCB& frame,float time){
  draws=culled=0;for(auto& entry:frame.localShadow)entry={0,0,0,0};
  if(!enabled||d.editorRaytracing||items.empty()||capacity==0)return;
  static const XMFLOAT3 directions[6]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  static const XMFLOAT3 ups[6]={{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}};
  d.barrier(depth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);d.cmd->SetGraphicsRootSignature(sun.root.Get());d.cmd->SetPipelineState(sun.pipeline.Get());
  D3D12_VIEWPORT viewport{0,0,float(resolution),float(resolution),0,1};D3D12_RECT scissor{0,0,LONG(resolution),LONG(resolution)};d.cmd->RSSetViewports(1,&viewport);d.cmd->RSSetScissorRects(1,&scissor);
  XMFLOAT4 clock{0,0,0,time};d.cmd->SetGraphicsRoot32BitConstants(1,4,&clock,0);
  UINT cube=0;auto handle=starlight_cpu_heap_start(dsv);
  for(UINT light=0;light<32;++light){if(!live(frame.spots[light]))continue;if(cube>=capacity)throw std::runtime_error("Local shadow allocation was not prepared before the frame");
   const auto p=frame.spots[light].pos;const float nearPlane=.04f;frame.localShadow[light]={float(cube+1),nearPlane,p.w,1.f/resolution};
   const auto eye=XMVectorSet(p.x,p.y,p.z,1);const auto projection=XMMatrixPerspectiveFovLH(XM_PIDIV2,1,nearPlane,p.w);
   for(UINT face=0;face<6;++face){const auto matrix=XMMatrixLookToLH(eye,XMLoadFloat3(&directions[face]),XMLoadFloat3(&ups[face]))*projection;
    d.cmd->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);d.cmd->OMSetRenderTargets(0,nullptr,FALSE,&handle);
    for(const auto& item:items){if(culling&&!visible(item,matrix)){++culled;continue;}XMFLOAT4X4 constants[2];XMStoreFloat4x4(&constants[0],item.world);XMStoreFloat4x4(&constants[1],matrix);d.cmd->SetGraphicsRoot32BitConstants(0,32,constants,0);d.cmd->IASetVertexBuffers(0,1,&item.mesh->vbv);d.cmd->IASetIndexBuffer(&item.mesh->ibv);d.cmd->DrawIndexedInstanced(item.mesh->indexCount,1,0,0,0);++draws;}
    handle.ptr+=descriptorSize;
   }
   ++cube;
  }
  d.cmd->OMSetRenderTargets(0,nullptr,FALSE,nullptr);d.barrier(depth.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);SunShadows::restore(d);
 }
};
