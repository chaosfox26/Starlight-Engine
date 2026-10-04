#pragma once
#include "surface_texture_data.h"
#include <chrono>

struct SurfaceTextures {
 static constexpr UINT count=5;
 ComPtr<ID3D12Resource> maps[3],opaqueColor,opaqueDepth;ComPtr<ID3D12DescriptorHeap> heap;UINT resolution=0,levels=0;UINT64 residentBytes=0,waterSceneBytes=0;
 void views(ID3D12Device* device,D3D12_CPU_DESCRIPTOR_HANDLE cpu){
  UINT inc=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  for(UINT i=0;i<3;++i){D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=i==0?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_UNORM;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2DArray.MipLevels=levels;view.Texture2DArray.ArraySize=count;device->CreateShaderResourceView(maps[i].Get(),&view,cpu);cpu.ptr+=inc;}
 }
 void init(RenderDevice& d){
  if(resolution)return;
  resolution=1024;wchar_t cap[32]{};if(GetEnvironmentVariableW(L"STARLIGHT_SURFACE_RESOLUTION",cap,32)){int requested=_wtoi(cap);if(requested!=256&&requested!=512&&requested!=1024)throw std::runtime_error("Surface texture resolution must be 256, 512 or 1024");resolution=UINT(requested);}
  auto started=std::chrono::steady_clock::now();
  auto tiles=starlight::surface::builtinSet(resolution);levels=UINT(tiles[0].size());
  logLine("surface textures: five "+std::to_string(resolution)+"-pixel materials prepared in "+std::to_string(std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count())+" seconds");
  d.flush();d.must(d.alloc->Reset(),"reset surface allocator");d.must(d.cmd->Reset(d.alloc.Get(),nullptr),"reset surface upload");std::vector<ComPtr<ID3D12Resource>> uploads;
  for(UINT channel=0;channel<3;++channel){
   D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;desc.Width=resolution;desc.Height=resolution;desc.DepthOrArraySize=count;desc.MipLevels=UINT16(levels);desc.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;desc.SampleDesc.Count=1;
   D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;d.must(d.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&maps[channel])),"allocate surface texture array");
   D3D12_RESOURCE_ALLOCATION_INFO allocation=d.device->GetResourceAllocationInfo(0,1,&desc);residentBytes+=allocation.SizeInBytes;
   UINT subs=levels*count;std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subs);std::vector<UINT> rows(subs);std::vector<UINT64> rowBytes(subs);UINT64 total=0;d.device->GetCopyableFootprints(&desc,0,subs,0,layouts.data(),rows.data(),rowBytes.data(),&total);
   D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;hp.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> upload;d.must(d.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)),"allocate surface staging");
   uint8_t* data=nullptr;D3D12_RANGE empty{0,0};d.must(upload->Map(0,&empty,reinterpret_cast<void**>(&data)),"map surface staging");
   for(UINT slice=0;slice<count;++slice)for(UINT mip=0;mip<levels;++mip){UINT sub=mip+slice*levels;const auto& bytes=tiles[slice][mip].maps[channel];for(UINT y=0;y<rows[sub];++y)memcpy(data+layouts[sub].Offset+size_t(y)*layouts[sub].Footprint.RowPitch,bytes.data()+size_t(y)*rowBytes[sub],size_t(rowBytes[sub]));}
   upload->Unmap(0,nullptr);
   for(UINT sub=0;sub<subs;++sub){D3D12_TEXTURE_COPY_LOCATION dest{};dest.pResource=maps[channel].Get();dest.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dest.SubresourceIndex=sub;D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=upload.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;source.PlacedFootprint=layouts[sub];d.cmd->CopyTextureRegion(&dest,0,0,0,&source,nullptr);}
   d.barrier(maps[channel].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);uploads.push_back(std::move(upload));
  }
  d.must(d.cmd->Close(),"close surface upload");ID3D12CommandList* lists[]{d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();
  D3D12_DESCRIPTOR_HEAP_DESC desc{};desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;desc.NumDescriptors=7;desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;d.must(d.device->CreateDescriptorHeap(&desc,IID_PPV_ARGS(&heap)),"surface descriptor heap");views(d.device.Get(),starlight_cpu_heap_start(heap));resize(d);
 }
 void resize(RenderDevice& d){
  d.flush();opaqueColor.Reset();opaqueDepth.Reset();waterSceneBytes=0;
  D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
  auto create=[&](ID3D12Resource* source,ComPtr<ID3D12Resource>& destination){auto desc=starlight_resource_desc(source);desc.Flags=D3D12_RESOURCE_FLAG_NONE;d.must(d.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&destination)),"allocate water scene guide");waterSceneBytes+=d.device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;};
  create(d.sceneColor.Get(),opaqueColor);create(d.depth.Get(),opaqueDepth);
  auto cpu=starlight_cpu_heap_start(heap);UINT inc=d.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);cpu.ptr+=inc*3;
  D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;view.Format=starlight_resource_desc(opaqueColor).Format;d.device->CreateShaderResourceView(opaqueColor.Get(),&view,cpu);cpu.ptr+=inc;view.Format=DXGI_FORMAT_R32_FLOAT;d.device->CreateShaderResourceView(opaqueDepth.Get(),&view,cpu);
 }
 void capture(RenderDevice& d){
  d.cmd->OMSetRenderTargets(0,nullptr,FALSE,nullptr);
  const bool multisampled=d.msaa>1;const auto sceneRest=multisampled?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_RENDER_TARGET,depthRest=multisampled?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_DEPTH_WRITE;if(multisampled)d.resolveScene();
  d.barrier(d.sceneColor.Get(),sceneRest,D3D12_RESOURCE_STATE_COPY_SOURCE);d.barrier(d.depth.Get(),depthRest,D3D12_RESOURCE_STATE_COPY_SOURCE);
  d.barrier(opaqueColor.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);d.barrier(opaqueDepth.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
  d.cmd->CopyResource(opaqueColor.Get(),d.sceneColor.Get());d.cmd->CopyResource(opaqueDepth.Get(),d.depth.Get());
  d.barrier(opaqueColor.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);d.barrier(opaqueDepth.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  d.barrier(d.sceneColor.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,sceneRest);d.barrier(d.depth.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,depthRest);
  D3D12_CPU_DESCRIPTOR_HANDLE targets[2]{starlight_cpu_heap_start(d.rtvHeap),{}};targets[0].ptr+=SIZE_T(kFrameCount)*d.rtvSize;targets[1]=targets[0];targets[1].ptr+=d.rtvSize;auto depth=starlight_cpu_heap_start(d.dsvHeap);d.cmd->OMSetRenderTargets(2,targets,FALSE,&depth);
 }
 void bind(RenderDevice& d){ID3D12DescriptorHeap* heaps[]{heap.Get()};d.cmd->SetDescriptorHeaps(1,heaps);d.cmd->SetGraphicsRootDescriptorTable(6,starlight_gpu_heap_start(heap));}
 void tail(RenderDevice& d,uint8_t* output){
  d.flush();std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,3*count> layouts{};
  D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=3*count*512;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;ComPtr<ID3D12Resource> readback;d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"surface tail readback");
  d.must(d.alloc->Reset(),"surface tail allocator");d.must(d.cmd->Reset(d.alloc.Get(),nullptr),"surface tail command");
  auto state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  for(UINT channel=0;channel<3;++channel){d.barrier(maps[channel].Get(),state,D3D12_RESOURCE_STATE_COPY_SOURCE);auto desc=starlight_resource_desc(maps[channel]);
   for(UINT slice=0;slice<count;++slice){UINT i=channel*count+slice,sub=slice*levels+levels-1,rows;UINT64 rowBytes,total;d.device->GetCopyableFootprints(&desc,sub,1,UINT64(i)*512,&layouts[i],&rows,&rowBytes,&total);
    D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=maps[channel].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=sub;D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=layouts[i];d.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
   }
   d.barrier(maps[channel].Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,state);
  }
  d.must(d.cmd->Close(),"surface tail close");ID3D12CommandList* lists[]{d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();uint8_t* data=nullptr;D3D12_RANGE read{0,size_t(buffer.Width)};d.must(readback->Map(0,&read,reinterpret_cast<void**>(&data)),"surface tail map");for(UINT i=0;i<3*count;++i)memcpy(output+i*4,data+layouts[i].Offset,4);D3D12_RANGE empty{0,0};readback->Unmap(0,&empty);
 }
};
