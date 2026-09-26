// SampleGame DX12 client — texture loading (see texture.h).
#include "texture.h"
#include <wincodec.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace model {

static size_t MipBytes(uint32_t w, uint32_t h, DXGI_FORMAT format) {
    const bool bc = format == DXGI_FORMAT_BC5_UNORM || format == DXGI_FORMAT_BC7_UNORM || format == DXGI_FORMAT_BC7_UNORM_SRGB;
    return bc ? size_t((w + 3) / 4) * ((h + 3) / 4) * 16 : size_t(w) * h * 4;
}

DXGI_FORMAT textureViewFormat(const TexImage& img, bool srgb) {
    if (img.format == DXGI_FORMAT_BC5_UNORM) return DXGI_FORMAT_BC5_UNORM;
    if (img.format == DXGI_FORMAT_BC7_UNORM || img.format == DXGI_FORMAT_BC7_UNORM_SRGB)
        return srgb ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM;
    return srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
}

bool loadTextureDDS(const std::string& path, uint32_t maxRes, TexImage& out, std::string& err) {
    out = {}; err.clear();
    auto fail = [&](const char* reason) { err = reason; return false; };
    if (!maxRes) return fail("Texture resolution must be positive");
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    // DDS magic + 124-byte header + 20-byte DX10 header, read without packed-struct assumptions.
    uint32_t header[37]{};
    if (!file.read(reinterpret_cast<char*>(header), sizeof(header))) return fail("Truncated DDS header");
    if (header[0] != 0x20534444 || header[1] != 124 || header[19] != 32 ||
        !(header[20] & 4) || header[21] != 0x30315844) return fail("Expected DX10 DDS header");
    auto format = static_cast<DXGI_FORMAT>(header[32]);
    if (format != DXGI_FORMAT_BC5_UNORM && format != DXGI_FORMAT_BC7_UNORM && format != DXGI_FORMAT_BC7_UNORM_SRGB)
        return fail("DDS requires BC5 UNORM or BC7 UNORM/sRGB");
    if (header[33] != 3 || header[35] != 1 || (header[34] & 4) || header[28] || header[6] > 1)
        return fail("DDS arrays, cubes and volumes are not supported");
    uint32_t w = header[4], h = header[3], levels = std::max(1u, header[7]);
    if (!w || !h || w > 16384 || h > 16384) return fail("DDS dimensions outside D3D12 limits");
    uint32_t maximumLevels = 1;
    for (auto edge = std::max(w, h); edge > 1; edge /= 2) ++maximumLevels;
    if (levels > maximumLevels) return fail("Invalid DDS mip count");
    const auto dataStart = file.tellg(); file.seekg(0, std::ios::end);
    const auto available = file.tellg() - dataStart; file.seekg(dataStart);
    size_t required = 0; uint32_t mw = w, mh = h;
    for (uint32_t l = 0; l < levels; ++l) {
        required += MipBytes(mw, mh, format); mw = std::max(1u, mw / 2); mh = std::max(1u, mh / 2);
    }
    if (available < 0 || uint64_t(available) < required) return fail("Truncated DDS mip data");
    TexImage image; image.format = format;
    for (uint32_t l = 0; l < levels; ++l) {
        const size_t bytes = MipBytes(w, h, format);
        if (w > maxRes || h > maxRes) file.seekg(std::streamoff(bytes), std::ios::cur);
        else {
            if (image.mips.empty()) { image.w = w; image.h = h; }
            image.mips.emplace_back(bytes);
            if (!file.read(reinterpret_cast<char*>(image.mips.back().data()), bytes)) return fail("DDS read failed");
        }
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    if (image.mips.empty()) return fail("DDS lacks a mip within the requested resolution");
    out = std::move(image); return true;
}

static const char kMagic[8] = { 'I','L','M','I','P','0','0','1' };
struct CacheHdr { char magic[8]; uint32_t w, h, levels; uint64_t srcSize, srcMtime; };

static bool StatFile(const std::string& p, uint64_t& size, uint64_t& mtime) {
    struct _stat64 st{};
    if (_stat64(p.c_str(), &st) != 0) return false;
    size = (uint64_t)st.st_size; mtime = (uint64_t)st.st_mtime; return true;
}

static std::string CachePath(const std::string& cacheDir, const std::string& pngPath, uint32_t maxRes) {
    size_t slash = pngPath.find_last_of("/\\");
    std::string base = pngPath.substr(slash == std::string::npos ? 0 : slash + 1);
    return cacheDir + "\\" + base + "." + std::to_string(maxRes) + ".mip";
}

static bool ReadCache(const std::string& path, uint64_t srcSize, uint64_t srcMtime, TexImage& out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    CacheHdr h{};
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && std::memcmp(h.magic, kMagic, 8) == 0 && h.srcSize == srcSize && h.srcMtime == srcMtime && h.levels > 0 && h.levels < 16;
    if (ok) {
        out.w = h.w; out.h = h.h; out.mips.resize(h.levels);
        uint32_t lw = h.w, lh = h.h;
        for (uint32_t l = 0; l < h.levels && ok; ++l) {
            out.mips[l].resize((size_t)lw * lh * 4);
            ok = fread(out.mips[l].data(), 1, out.mips[l].size(), f) == out.mips[l].size();
            lw = std::max(1u, lw / 2); lh = std::max(1u, lh / 2);
        }
    }
    fclose(f);
    if (!ok) out = TexImage();
    out.fromCache = ok;
    return ok;
}

static void WriteCache(const std::string& path, uint64_t srcSize, uint64_t srcMtime, const TexImage& img) {
    std::string tmp = path + ".tmp";
    FILE* f = nullptr;
    if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f) return;
    CacheHdr h{}; std::memcpy(h.magic, kMagic, 8); h.w = img.w; h.h = img.h; h.levels = (uint32_t)img.mips.size(); h.srcSize = srcSize; h.srcMtime = srcMtime;
    bool ok = fwrite(&h, sizeof(h), 1, f) == 1;
    for (const auto& m : img.mips) if (ok) ok = fwrite(m.data(), 1, m.size(), f) == m.size();
    fclose(f);
    if (ok) MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
    else DeleteFileA(tmp.c_str());
}

// 2x2 box filter (odd edges clamp). Normals/roughness/AO get the same plain average: adequate
// for the mip distances the club camera uses; a renormalizing/linear-space variant is an M6 gap.
static void Downsample(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh, std::vector<uint8_t>& dst, uint32_t& dw, uint32_t& dh) {
    dw = std::max(1u, sw / 2); dh = std::max(1u, sh / 2);
    dst.resize((size_t)dw * dh * 4);
    for (uint32_t y = 0; y < dh; ++y) {
        uint32_t y0 = std::min(sh - 1, y * 2), y1 = std::min(sh - 1, y * 2 + 1);
        const uint8_t* r0 = src.data() + (size_t)y0 * sw * 4; const uint8_t* r1 = src.data() + (size_t)y1 * sw * 4;
        uint8_t* d = dst.data() + (size_t)y * dw * 4;
        for (uint32_t x = 0; x < dw; ++x) {
            uint32_t x0 = std::min(sw - 1, x * 2) * 4, x1 = std::min(sw - 1, x * 2 + 1) * 4;
            for (uint32_t c = 0; c < 4; ++c) d[x * 4 + c] = (uint8_t)((r0[x0 + c] + r0[x1 + c] + r1[x0 + c] + r1[x1 + c] + 2) / 4);
        }
    }
}

static bool DecodePng(const std::string& path, std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h, std::string& err) {
    // COM belongs to the calling thread. Balance only successful initializations,
    // after the WIC objects below have been destroyed, including early-return paths.
    struct ComScope {
        HRESULT result;
        ComScope() : result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
        ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
        ComScope(const ComScope&) = delete;
        ComScope& operator=(const ComScope&) = delete;
    } com;
    // A caller already using STA can still decode on that apartment; we do not own it.
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE) {
        char message[80];
        std::snprintf(message, sizeof(message), "COM initialization failed (0x%08lX)",
                      static_cast<unsigned long>(com.result));
        err = message;
        return false;
    }
    ComPtr<IWICImagingFactory> fac;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac)))) { err = "WIC factory"; return false; }
    std::wstring wpath;
    { int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0); wpath.resize(n > 0 ? (size_t)n - 1 : 0); MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), n); }
    ComPtr<IWICBitmapDecoder> dec;
    if (FAILED(fac->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) { err = "cannot open " + path; return false; }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) { err = "no frame in " + path; return false; }
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(fac->CreateFormatConverter(&conv)) || FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) { err = "format convert " + path; return false; }
    if (FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0) { err = "size " + path; return false; }
    rgba.resize((size_t)w * h * 4);
    if (FAILED(conv->CopyPixels(nullptr, w * 4, (UINT)rgba.size(), rgba.data()))) { err = "CopyPixels " + path; return false; }
    return true;
}

bool loadTextureRGBA(const std::string& pngPath, uint32_t maxRes, const std::string& cacheDir, TexImage& out, std::string& err) {
    out = TexImage();
    if (!maxRes) { err = "Texture resolution must be positive"; return false; }
    auto extension = std::filesystem::u8path(pngPath).extension().wstring();
    if (_wcsicmp(extension.c_str(), L".dds") == 0) return loadTextureDDS(pngPath, maxRes, out, err);
    uint64_t srcSize = 0, srcMtime = 0;
    if (!StatFile(pngPath, srcSize, srcMtime)) { err = "missing " + pngPath; return false; }
    std::string cache = cacheDir.empty() ? "" : CachePath(cacheDir, pngPath, maxRes);
    if (!cache.empty() && ReadCache(cache, srcSize, srcMtime, out)) return true;

    std::vector<uint8_t> level; uint32_t w = 0, h = 0;
    if (!DecodePng(pngPath, level, w, h, err)) return false;
    while ((w > maxRes || h > maxRes) && (w > 1 || h > 1)) {   // drop levels above the requested resolution
        std::vector<uint8_t> next; uint32_t nw, nh; Downsample(level, w, h, next, nw, nh); level.swap(next); w = nw; h = nh;
    }
    out.w = w; out.h = h; out.mips.push_back(std::move(level));
    while (w > 1 || h > 1) {
        std::vector<uint8_t> next; uint32_t nw, nh; Downsample(out.mips.back(), w, h, next, nw, nh);
        out.mips.push_back(std::move(next)); w = nw; h = nh;
    }
    if (!cache.empty()) { CreateDirectoryA(cacheDir.c_str(), nullptr); WriteCache(cache, srcSize, srcMtime, out); }
    return true;
}

ComPtr<ID3D12Resource> uploadTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const TexImage& img,
                                     std::vector<ComPtr<ID3D12Resource>>& keepAlive, std::string& err) {
    ComPtr<ID3D12Resource> tex;
    if (!device || !cmd || !img.w || !img.h || img.w > 16384 || img.h > 16384 || img.mips.empty()) { err = "invalid image or upload context"; return tex; }
    if (img.format != DXGI_FORMAT_R8G8B8A8_UNORM && img.format != DXGI_FORMAT_BC5_UNORM &&
        img.format != DXGI_FORMAT_BC7_UNORM && img.format != DXGI_FORMAT_BC7_UNORM_SRGB) { err = "unsupported texture format"; return tex; }
    uint32_t mw = img.w, mh = img.h;
    for (size_t level = 0; level < img.mips.size(); ++level) {
        if (img.mips[level].size() != MipBytes(mw, mh, img.format)) { err = "invalid mip byte length"; return tex; }
        if (mw == 1 && mh == 1 && level + 1 != img.mips.size()) { err = "excess mip levels"; return tex; }
        mw = std::max(1u, mw / 2); mh = std::max(1u, mh / 2);
    }
    const UINT levels = (UINT)img.mips.size();
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; td.Width = img.w; td.Height = img.h; td.DepthOrArraySize = 1;
    td.MipLevels = (UINT16)levels;
    td.Format = img.format == DXGI_FORMAT_BC5_UNORM ? DXGI_FORMAT_BC5_TYPELESS
              : img.format == DXGI_FORMAT_BC7_UNORM || img.format == DXGI_FORMAT_BC7_UNORM_SRGB ? DXGI_FORMAT_BC7_TYPELESS
              : DXGI_FORMAT_R8G8B8A8_TYPELESS;
    td.SampleDesc.Count = 1;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) { err = "create texture"; return tex; }

    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp(levels); std::vector<UINT> rows(levels); std::vector<UINT64> rowBytes(levels);
    UINT64 total = 0;
    device->GetCopyableFootprints(&td, 0, levels, 0, fp.data(), rows.data(), rowBytes.data(), &total);
    D3D12_HEAP_PROPERTIES hu{}; hu.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
    bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> up;
    if (FAILED(device->CreateCommittedResource(&hu, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up)))) { err = "create texture upload"; tex.Reset(); return tex; }
    uint8_t* p = nullptr; D3D12_RANGE none{ 0, 0 };
    if (FAILED(up->Map(0, &none, reinterpret_cast<void**>(&p))) || !p) { err = "map texture upload"; tex.Reset(); return tex; }
    for (UINT l = 0; l < levels; ++l) {
        const uint8_t* src = img.mips[l].data();
        const size_t srcPitch = size_t(rowBytes[l]); // BC rows are rows of 4x4 blocks, not pixels.
        for (UINT y = 0; y < rows[l]; ++y)
            std::memcpy(p + fp[l].Offset + (size_t)y * fp[l].Footprint.RowPitch, src + (size_t)y * srcPitch, std::min<size_t>(srcPitch, (size_t)rowBytes[l]));
    }
    up->Unmap(0, nullptr);
    for (UINT l = 0; l < levels; ++l) {
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = tex.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dst.SubresourceIndex = l;
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = up.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp[l];
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    D3D12_RESOURCE_BARRIER br{}; br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    br.Transition.pResource = tex.Get(); br.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    br.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE; br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    cmd->ResourceBarrier(1, &br);
    keepAlive.push_back(up);
    return tex;
}

} // namespace model
