#pragma once
#ifdef STARLIGHT_CMAKE_RENDERER
#include <shadow_shader.h>
#else
#include "../../editor/native/shadow_shader.h"
#endif

// Three camera-fitted, texel-stabilized directional cascades. Only actual
// triangles enter the depth map; no bounds/proxy silhouettes or CPU readback.
struct SunShadows {
 static constexpr UINT cascades=3;
 struct Item {const GpuMesh* mesh;XMMATRIX world;XMFLOAT3 low,high;};
 ComPtr<ID3D12Resource> depth;
 ComPtr<ID3D12DescriptorHeap> dsv;
 ComPtr<ID3D12RootSignature> root;
 ComPtr<ID3D12PipelineState> pipeline;
 UINT resolution=2048,requestedResolution=2048,descriptorSize=0;UINT64 bytes=0,draws=0;
 bool enabled=true,enabledOverride=false,resolutionOverride=false;
 void init(RenderDevice& d,ID3D12DescriptorHeap* surfaceHeap){
  wchar_t value[32]{};
  if(GetEnvironmentVariableW(L"STARLIGHT_SUN_SHADOWS",value,32)){
   if(wcscmp(value,L"0")&&wcscmp(value,L"1"))throw std::runtime_error("Sun shadows must be 0 or 1");enabled=value[0]==L'1';enabledOverride=true;
  }
  if(GetEnvironmentVariableW(L"STARLIGHT_SHADOW_RESOLUTION",value,32)){
   if(wcscmp(value,L"1024")&&wcscmp(value,L"2048")&&wcscmp(value,L"4096"))throw std::runtime_error("Shadow resolution must be 1024, 2048 or 4096");requestedResolution=resolution=UINT(_wtoi(value));resolutionOverride=true;
  }
  allocate(d,surfaceHeap,resolution);
  createPipeline(d);
 }
 void allocate(RenderDevice& d,ID3D12DescriptorHeap* surfaceHeap,UINT size){
  if(depth&&resolution==size)return;
  d.flush();depth.Reset();dsv.Reset();resolution=size;draws=0;
  D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=resolution;desc.Height=resolution;desc.DepthOrArraySize=cascades;desc.MipLevels=1;desc.Format=DXGI_FORMAT_R32_TYPELESS;desc.SampleDesc.Count=1;desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT;clear.DepthStencil.Depth=1;
  d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,&clear,IID_PPV_ARGS(&depth)),"allocate cascaded sun depth");bytes=d.device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=cascades;d.must(d.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsv)),"sun depth views");descriptorSize=d.device->GetDescriptorHandleIncrementSize(hd.Type);
  auto cpu=starlight_cpu_heap_start(dsv);
  for(UINT i=0;i<cascades;++i){D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2DARRAY;view.Texture2DArray.FirstArraySlice=i;view.Texture2DArray.ArraySize=1;d.device->CreateDepthStencilView(depth.Get(),&view,cpu);cpu.ptr+=descriptorSize;}
  cpu=starlight_cpu_heap_start(surfaceHeap);cpu.ptr+=5*d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R32_FLOAT;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;srv.Texture2DArray.ArraySize=cascades;srv.Texture2DArray.MipLevels=1;d.device->CreateShaderResourceView(depth.Get(),&srv,cpu);
 }
 void setQuality(RenderDevice& d,ID3D12DescriptorHeap* surfaceHeap,int quality){
  if(!enabledOverride)enabled=quality>0;
  UINT size=enabled?(resolutionOverride?requestedResolution:quality==1?1024:quality==3?4096:2048):1;
  allocate(d,surfaceHeap,size);
 }
 void createPipeline(RenderDevice& d){
  D3D12_ROOT_PARAMETER parameters[2]{};parameters[0].ParameterType=parameters[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants={0,0,32};parameters[1].Constants={1,0,4};parameters[0].ShaderVisibility=parameters[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
  D3D12_ROOT_SIGNATURE_DESC signature{};signature.NumParameters=2;signature.pParameters=parameters;signature.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> blob,error;d.must(D3D12SerializeRootSignature(&signature,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error),"serialize sun shadow root");d.must(d.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"sun shadow root");
  if(FAILED(D3DCompile(kSunShadowShader,strlen(kSunShadowShader),"sun-depth",nullptr,nullptr,"ShadowDepth","vs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error)))throw std::runtime_error(error?(char*)error->GetBufferPointer():"Sun depth shader failed");
  D3D12_INPUT_ELEMENT_DESC layout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
  D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};state.pRootSignature=root.Get();state.VS={blob->GetBufferPointer(),blob->GetBufferSize()};state.InputLayout={layout,2};state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;state.DSVFormat=DXGI_FORMAT_D32_FLOAT;state.SampleDesc.Count=1;state.SampleMask=UINT_MAX;state.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;state.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;state.RasterizerState.DepthClipEnable=TRUE;state.RasterizerState.DepthBias=64;state.RasterizerState.SlopeScaledDepthBias=.5f;state.DepthStencilState.DepthEnable=TRUE;state.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;state.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
  d.must(d.device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&pipeline)),"sun depth pipeline");
 }
 void draw(RenderDevice& d,const std::vector<Item>& items,FrameCB& frame,float time){
  draws=0;frame.shadowTexel={0,0,0,0};
  if(!enabled||d.editorRaytracing||frame.lightDir.w<=.001f||items.empty())return;
  const float splits[3]={12,40,120};const auto inverse=XMMatrixInverse(nullptr,d.viewProjNoJit);
  const auto direction=XMVector3Normalize(XMVectorSet(frame.lightDir.x,frame.lightDir.y,frame.lightDir.z,0));
  const auto up=fabsf(XMVectorGetY(direction))>.95f?XMVectorSet(0,0,1,0):XMVectorSet(0,1,0,0);
  const auto lightView=XMMatrixLookToRH(XMVectorZero(),direction,up);
  const auto cameraView=XMMatrixLookAtRH(XMLoadFloat3(&d.editorEye),XMLoadFloat3(&d.editorTarget),XMLoadFloat3(&d.editorUp));
  float first=.1f;float texels[3]{};
  for(UINT cascade=0;cascade<cascades;++cascade){
   const float last=splits[cascade];XMVECTOR points[8];XMVECTOR center=XMVectorZero();
   for(int corner=0;corner<4;++corner){
    const float x=corner&1?1.f:-1.f,y=corner&2?1.f:-1.f;
    auto nearPoint=XMVector3TransformCoord(XMVectorSet(x,y,0,1),inverse),farPoint=XMVector3TransformCoord(XMVectorSet(x,y,1,1),inverse);
    const float nearDepth=-XMVectorGetZ(XMVector3TransformCoord(nearPoint,cameraView)),farDepth=-XMVectorGetZ(XMVector3TransformCoord(farPoint,cameraView));
    for(int end=0;end<2;++end){float depth=end?last:first;auto p=XMVectorLerp(nearPoint,farPoint,(depth-nearDepth)/(farDepth-nearDepth));points[corner*2+end]=p;center=XMVectorAdd(center,p);}
   }
   center=XMVectorScale(center,1.f/8);float radius=0;
   for(auto point:points)radius=std::max(radius,XMVectorGetX(XMVector3Length(XMVectorSubtract(point,center))));
   radius=ceilf(radius*16)/16+.125f;const float texel=2*radius/resolution;texels[cascade]=texel;
   XMFLOAT3 c;XMStoreFloat3(&c,XMVector3TransformCoord(center,lightView));c.x=floorf(c.x/texel+.5f)*texel;c.y=floorf(c.y/texel+.5f)*texel;
   auto projection=XMMatrixOrthographicOffCenterRH(c.x-radius,c.x+radius,c.y-radius,c.y+radius,-c.z-radius-128,-c.z+radius+128);
   XMStoreFloat4x4(&frame.sunShadow[cascade],lightView*projection);
   // Overlap the final 12% of a slice for a smooth cascade transition.
   first=last-(last-first)*.12f;
  }
  frame.shadowCuts={splits[0],splits[1],splits[2],1.f/resolution};frame.shadowTexel={texels[0],texels[1],texels[2],1};
  d.barrier(depth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_DEPTH_WRITE);d.cmd->SetGraphicsRootSignature(root.Get());d.cmd->SetPipelineState(pipeline.Get());
  D3D12_VIEWPORT viewport{0,0,float(resolution),float(resolution),0,1};D3D12_RECT scissor{0,0,LONG(resolution),LONG(resolution)};d.cmd->RSSetViewports(1,&viewport);d.cmd->RSSetScissorRects(1,&scissor);
  XMFLOAT4 clock{0,0,0,time};d.cmd->SetGraphicsRoot32BitConstants(1,4,&clock,0);
  auto handle=starlight_cpu_heap_start(dsv);
  for(UINT cascade=0;cascade<cascades;++cascade){
   d.cmd->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);d.cmd->OMSetRenderTargets(0,nullptr,FALSE,&handle);
   for(const auto& item:items){XMFLOAT4X4 constants[2];XMStoreFloat4x4(&constants[0],item.world);constants[1]=frame.sunShadow[cascade];d.cmd->SetGraphicsRoot32BitConstants(0,32,constants,0);d.cmd->IASetVertexBuffers(0,1,&item.mesh->vbv);d.cmd->IASetIndexBuffer(&item.mesh->ibv);d.cmd->DrawIndexedInstanced(item.mesh->indexCount,1,0,0,0);++draws;d.triShadow+=item.mesh->indexCount/3;}
   handle.ptr+=descriptorSize;
  }
  d.cmd->OMSetRenderTargets(0,nullptr,FALSE,nullptr);d.barrier(depth.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  restore(d);
 }
 static void restore(RenderDevice& d){
  D3D12_CPU_DESCRIPTOR_HANDLE targets[2]{starlight_cpu_heap_start(d.rtvHeap),{}};targets[0].ptr+=SIZE_T(kFrameCount)*d.rtvSize;targets[1]=targets[0];targets[1].ptr+=d.rtvSize;auto mainDepth=starlight_cpu_heap_start(d.dsvHeap);d.cmd->OMSetRenderTargets(2,targets,FALSE,&mainDepth);
  D3D12_VIEWPORT viewport{0,0,float(d.rw),float(d.rh),0,1};D3D12_RECT scissor{0,0,LONG(d.rw),LONG(d.rh)};d.cmd->RSSetViewports(1,&viewport);d.cmd->RSSetScissorRects(1,&scissor);d.cmd->SetGraphicsRootSignature(d.rootSig.Get());d.cmd->SetPipelineState(d.pso.Get());
 }
};
