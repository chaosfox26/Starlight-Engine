// Starlight DX12 client — texture loading : PNG via WIC -> RGBA8 mip chain, with a raw mip
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
#include "load_error.h"
#include "texture_limits.h"

namespace model {

struct TexImage {
    uint32_t w = 0, h = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    std::vector<std::vector<uint8_t>> mips;   // Tightly packed rows or BC blocks; never padded upload rows.
    bool fromCache = false;
    std::string cacheNote;
    size_t bytes() const { size_t b = 0; for (auto& m : mips) b += m.size(); return b; }
};

// DX10 DDS, single 2D BC5/BC7 image. Retains compressed blocks and authored mips;
// maxRes selects the first resident mip without decompressing or rescaling.
bool loadTextureDDS(const std::string& path, uint32_t maxRes, TexImage& out, std::string& err);
DXGI_FORMAT textureViewFormat(const TexImage& img, bool srgb);

// maxRes clamps level 0 (a 4096 source loaded at 2048 skips its top level). cacheDir empty = no cache.
bool loadTextureRGBA(const std::string& pngPath, uint32_t maxRes, const std::string& cacheDir, TexImage& out, std::string& err);

enum class TextureRole : uint32_t { Srgb = 0, Linear = 1 };
enum class TextureFilter : uint32_t { Box2x2 = 0 };

constexpr uint32_t TextureCacheSchemaVersion = 3;
constexpr uint32_t TextureDecoderVersion = 1;

struct TextureLoadOptions {
    uint32_t maxRes = 0;
    std::string label;
    std::string cacheDir;
    std::string contentId;
    bool verifyPngStream = true;
    bool sharedCache = false;
    bool forceDecode = false;
    TextureRole role = TextureRole::Srgb;
    TextureFilter filter = TextureFilter::Box2x2;
};

struct TextureProbe {
    uint32_t width = 0, height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t mipLevels = 0;
    size_t decodedBytes = 0;
    std::string container;
};

// Encoded PNG/JPEG/other WIC image or DX10 DDS, as bytes. cacheDir/contentId select the optional derived .mip cache (contentId empty = SHA-256 of the bytes).
LoadError loadTextureFromMemory(ByteView encoded, const TextureLoadOptions& options, TexImage& out);
LoadError loadTextureDDSFromMemory(ByteView dds, uint32_t maxRes, const std::string& label, TexImage& out);
LoadError probeTextureDecode(ByteView encoded, const std::string& label, TextureProbe& out);
std::string contentHashHex(ByteView bytes);

std::string textureCacheDirectory();
std::string textureCacheKey(const std::string& contentId, uint32_t maxRes, TextureRole role, TextureFilter filter,
                            uint32_t schemaVersion = TextureCacheSchemaVersion, uint32_t decoderVersion = TextureDecoderVersion);

struct TextureCacheStats {
    uint64_t hits = 0, misses = 0, corrupt = 0, written = 0, writeFailures = 0, evicted = 0, unverifiedSkipped = 0, forcedDecodes = 0;
};
TextureCacheStats textureCacheStats();
void resetTextureCacheStats();
uint64_t textureCacheLimitBytes();
void setTextureCacheLimitBytes(uint64_t bytes);

struct TextureDecodeStats {
    uint64_t decodes = 0, waited = 0;
    uint32_t active = 0, peakConcurrent = 0;
};
TextureDecodeStats textureDecodeStats();
void resetTextureDecodeStats();
uint32_t defaultTextureWorkerLimit(uint32_t hardwareThreads);
uint32_t textureWorkerLimit();
void setTextureWorkerLimit(uint32_t workers);

struct TexturePrepareJob {
    ByteView encoded;
    TextureLoadOptions options;
};
std::vector<LoadError> prepareTextures(const std::vector<TexturePrepareJob>& jobs, std::vector<TexImage>* images, uint32_t workers = 0);

// Creates a DEFAULT-heap Texture2D with the full mip chain, records the upload copies on cmd,
// leaves the resource in PIXEL_SHADER_RESOURCE. The upload buffer goes into keepAlive (free
// it after the copy has executed). Uses RGBA8/BC5/BC7 TYPELESS storage as appropriate;
// the caller selects the SRV via textureViewFormat (albedo sRGB, guides linear).
Microsoft::WRL::ComPtr<ID3D12Resource> uploadTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const TexImage& img,
                                                     std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>& keepAlive, std::string& err);

} // namespace model
