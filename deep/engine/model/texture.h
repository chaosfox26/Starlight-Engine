// Starlight DX12 client — texture loading (M6): PNG via WIC -> RGBA8 mip chain, with a raw mip
// cache next to the bakes so the second run is one read instead of a 4K PNG decode.
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>

namespace model {

struct TexImage {
    uint32_t w = 0, h = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::vector<std::vector<uint8_t>> mips;   // Tightly packed rows or BC blocks; never padded upload rows.
    bool fromCache = false;
    size_t bytes() const { size_t b = 0; for (auto& m : mips) b += m.size(); return b; }
};

// DX10 DDS, single 2D BC5/BC7 image. Retains compressed blocks and authored mips;
// maxRes selects the first resident mip without decompressing or rescaling.
bool loadTextureDDS(const std::string& path, uint32_t maxRes, TexImage& out, std::string& err);
DXGI_FORMAT textureViewFormat(const TexImage& img, bool srgb);

// maxRes clamps level 0 (a 4096 source loaded at 2048 skips its top level). cacheDir empty = no cache.
bool loadTextureRGBA(const std::string& pngPath, uint32_t maxRes, const std::string& cacheDir, TexImage& out, std::string& err);

// Creates a DEFAULT-heap Texture2D with the full mip chain, records the upload copies on cmd,
// leaves the resource in PIXEL_SHADER_RESOURCE. The upload buffer goes into keepAlive (free
// it after the copy has executed). Uses RGBA8/BC5/BC7 TYPELESS storage as appropriate;
// the caller selects the SRV via textureViewFormat (albedo sRGB, guides linear).
Microsoft::WRL::ComPtr<ID3D12Resource> uploadTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const TexImage& img,
                                                     std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& keepAlive, std::string& err);

} // namespace model
