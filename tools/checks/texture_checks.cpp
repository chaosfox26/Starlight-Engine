#include "../../deep/engine/model/texture.h"
#include <dxgi1_6.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <array>
using Microsoft::WRL::ComPtr;
int main(int argc, char** argv) {
    try {
        const auto dir = std::filesystem::path(argc > 1 ? argv[1] : "texture-checks");
        std::filesystem::create_directories(dir);
        int count = 0;
        auto require = [&](bool okay, const char* label) { if (!okay) throw std::runtime_error(label); ++count; };
        auto write = [&](const char* name, uint32_t format, uint32_t w, uint32_t h, uint32_t levels, int trim = 0) {
            std::array<uint32_t, 37> header{};
            header[0]=0x20534444; header[1]=124; header[3]=h; header[4]=w; header[7]=levels;
            header[19]=32; header[20]=4; header[21]=0x30315844; header[32]=format; header[33]=3; header[35]=1;
            std::vector<uint8_t> data;
            for (uint32_t l=0;l<levels;++l) {
                data.insert(data.end(), size_t((w+3)/4)*((h+3)/4)*16, uint8_t(l+1));
                w=std::max(1u,w/2); h=std::max(1u,h/2);
            }
            std::ofstream f(dir/name, std::ios::binary);
            f.write(reinterpret_cast<const char*>(header.data()), sizeof(header));
            f.write(reinterpret_cast<const char*>(data.data()), data.size()-trim);
            return (dir/name).string();
        };
        model::TexImage image; std::string err;
        const auto bc7=write("bc7.dds",DXGI_FORMAT_BC7_UNORM_SRGB,2048,2048,12);
        require(model::loadTextureDDS(bc7,4096,image,err),"2K BC7 load");
        require(image.w==2048 && image.mips.size()==12 && image.mips.back().size()==16,"BC7 block mip sizes");
        require(model::textureViewFormat(image,true)==DXGI_FORMAT_BC7_UNORM_SRGB,"BC7 albedo sRGB view");
        require(model::loadTextureRGBA(bc7,512,"",image,err) && image.w==512 && image.mips[0][0]==3,"mip selection preserves compressed bytes");
        require(!model::loadTextureDDS(bc7,0,image,err) && image.mips.empty(),"zero cap rejected");
        const auto bc5=write("bc5.dds",DXGI_FORMAT_BC5_UNORM,4096,4096,13);
        require(model::loadTextureDDS(bc5,4096,image,err) && image.w==4096,"4K BC5 load");
        require(model::textureViewFormat(image,true)==DXGI_FORMAT_BC5_UNORM,"normal map stays linear");
        require(!model::loadTextureDDS(write("truncated.dds",DXGI_FORMAT_BC7_UNORM,8,8,4,1),4096,image,err),"truncated tail rejected");
        require(!model::loadTextureDDS(write("format.dds",DXGI_FORMAT_BC1_UNORM,8,8,1),4096,image,err),"unsupported format rejected");
        require(!model::loadTextureDDS(write("mips.dds",DXGI_FORMAT_BC7_UNORM,4,4,8),4096,image,err),"excess levels rejected");
        require(!model::loadTextureDDS(write("missing-tail.dds",DXGI_FORMAT_BC7_UNORM,16,16,1),4,image,err),"missing resident mip rejected");
        ComPtr<IDXGIFactory4> factory; ComPtr<IDXGIAdapter> warp; ComPtr<ID3D12Device> device;
        require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))),"DXGI factory");
        require(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))),"WARP adapter");
        require(SUCCEEDED(D3D12CreateDevice(warp.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))),"D3D12 device");
        ComPtr<ID3D12CommandQueue> queue; D3D12_COMMAND_QUEUE_DESC q{};
        require(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))),"upload queue");
        ComPtr<ID3D12CommandAllocator> allocator; ComPtr<ID3D12GraphicsCommandList> cmd;
        require(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))),"allocator");
        require(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&cmd))),"command list");
        std::vector<ComPtr<ID3D12Resource>> alive, textures;
        struct Readback { ComPtr<ID3D12Resource> buffer; D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp; UINT rows; UINT64 rowBytes; std::vector<uint8_t> expected; };
        std::vector<Readback> readbacks;
        for (auto path : {bc5,bc7}) {
            require(model::loadTextureDDS(path,128,image,err),"upload fixture load");
            auto texture = model::uploadTexture(device.Get(),cmd.Get(),image,alive,err);
            require(bool(texture),"compressed GPU upload recording"); textures.push_back(texture);
            Readback readback{}; UINT64 size=0; auto desc=texture->GetDesc();
            device->GetCopyableFootprints(&desc,0,1,0,&readback.fp,&readback.rows,&readback.rowBytes,&size);
            D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width=size; buffer.Height=1; buffer.DepthOrArraySize=1; buffer.MipLevels=1;
            buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
            require(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback.buffer))),"readback buffer");
            D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource=texture.Get(); barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
            cmd->ResourceBarrier(1,&barrier);
            D3D12_TEXTURE_COPY_LOCATION src{},dst{}; src.pResource=texture.Get(); src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            dst.pResource=readback.buffer.Get(); dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint=readback.fp;
            cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            readback.expected=image.mips[0]; readbacks.push_back(std::move(readback));
            image.mips[0].pop_back();
            require(!model::uploadTexture(device.Get(),cmd.Get(),image,alive,err),"malformed upload rejected before copy");
        }
        require(SUCCEEDED(cmd->Close()),"close upload list");
        ID3D12CommandList* lists[]{cmd.Get()}; queue->ExecuteCommandLists(1,lists);
        ComPtr<ID3D12Fence> fence; require(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))),"fence");
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr); require(event!=nullptr,"fence event");
        const bool signaled=SUCCEEDED(queue->Signal(fence.Get(),1)) && SUCCEEDED(fence->SetEventOnCompletion(1,event));
        const DWORD waited=signaled ? WaitForSingleObject(event,10000) : WAIT_FAILED;
        CloseHandle(event); require(waited==WAIT_OBJECT_0,"uploads retire before releasing staging buffers");
        require(SUCCEEDED(device->GetDeviceRemovedReason()),"device remains valid");
        for (auto& readback : readbacks) {
            void* mapped=nullptr; require(SUCCEEDED(readback.buffer->Map(0,nullptr,&mapped)),"readback map");
            bool equal=true;
            for (UINT row=0;row<readback.rows;++row) {
                const auto p=static_cast<const uint8_t*>(mapped)+readback.fp.Offset+size_t(row)*readback.fp.Footprint.RowPitch;
                equal &= std::equal(p,p+readback.rowBytes,readback.expected.data()+size_t(row)*readback.rowBytes);
            }
            D3D12_RANGE none{0,0}; readback.buffer->Unmap(0,&none);
            require(equal,"GPU compressed block rows round-trip exactly");
        }
        std::cout << count << " texture checks passed (2K/4K DDS, BC5/BC7, WARP upload)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
