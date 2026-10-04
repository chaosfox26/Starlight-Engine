#pragma once
struct EdgeAA {
 ComPtr<ID3D12RootSignature> signature;ComPtr<ID3D12PipelineState> pipeline;ComPtr<ID3D12DescriptorHeap> heap;
 void init(RenderDevice& d){
  D3D12_DESCRIPTOR_RANGE ranges[2]{};ranges[0]={D3D12_DESCRIPTOR_RANGE_TYPE_SRV,1,0,0,0};ranges[1]={D3D12_DESCRIPTOR_RANGE_TYPE_UAV,1,0,0,1};
  D3D12_ROOT_PARAMETER parameter{};parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;parameter.DescriptorTable={2,ranges};
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=1;desc.pParameters=&parameter;
  ComPtr<ID3DBlob> blob,error;if(FAILED(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error)))throw std::runtime_error("AA root signature failed");
  if(FAILED(d.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&signature))))throw std::runtime_error("AA signature allocation failed");
  const char* code=R"(
Texture2D<float4> inputColor:register(t0);RWTexture2D<float4> outputColor:register(u0);
float3 read(int2 p,int2 size){return inputColor.Load(int3(clamp(p,0,size-1),0)).rgb;}
float luma(float3 c){return dot(c,float3(.299,.587,.114));}
[numthreads(8,8,1)]void main(uint3 id:SV_DispatchThreadID){uint w,h;outputColor.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;int2 size=int2(w,h),p=int2(id.xy);
 float3 c=read(p,size),n=read(p+int2(0,-1),size),s=read(p+int2(0,1),size),e=read(p+int2(1,0),size),west=read(p+int2(-1,0),size);
 float lc=luma(c),ln=luma(n),ls=luma(s),le=luma(e),lw=luma(west);float lo=min(lc,min(min(ln,ls),min(le,lw))),hi=max(lc,max(max(ln,ls),max(le,lw)));
 float contrast=hi-lo;float3 result=c;
 if(contrast>max(.035,hi*.12)){float horizontal=abs(ln+ls-2*lc),vertical=abs(le+lw-2*lc);float3 across=horizontal>=vertical?(n+s)*.5:(e+west)*.5;float blend=saturate(abs((ln+ls+le+lw)*.25-lc)/max(contrast,.0001));result=lerp(c,across,min(.45,blend*.65));}
 outputColor[p]=float4(result,1);
})";
  if(FAILED(D3DCompile(code,strlen(code),"editor-edge-aa",nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,&error)))throw std::runtime_error(error?(char*)error->GetBufferPointer():"AA shader failed");
  D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};ps.pRootSignature=signature.Get();ps.CS={blob->GetBufferPointer(),blob->GetBufferSize()};if(FAILED(d.device->CreateComputePipelineState(&ps,IID_PPV_ARGS(&pipeline))))throw std::runtime_error("AA pipeline failed");
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=2;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;if(FAILED(d.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap))))throw std::runtime_error("AA heap failed");
  auto cpu=starlight_cpu_heap_start(heap);D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;d.device->CreateShaderResourceView(d.sceneColor.Get(),&srv,cpu);
  cpu.ptr+=d.device->GetDescriptorHandleIncrementSize(hd.Type);D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=srv.Format;uav.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;d.device->CreateUnorderedAccessView(d.upOutput.Get(),nullptr,&uav,cpu);
 }
 void draw(RenderDevice& d){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=d.upOutput.Get();d.cmd->ResourceBarrier(1,&b);ID3D12DescriptorHeap* heaps[]={heap.Get()};d.cmd->SetDescriptorHeaps(1,heaps);d.cmd->SetComputeRootSignature(signature.Get());d.cmd->SetPipelineState(pipeline.Get());d.cmd->SetComputeRootDescriptorTable(0,starlight_gpu_heap_start(heap));d.cmd->Dispatch((d.w+7)/8,(d.h+7)/8,1);}
};
