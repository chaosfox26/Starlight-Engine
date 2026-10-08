// Starlight DX12 client — texture loading (see texture.h).
#include "texture.h"
#include <wincodec.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <system_error>
#include <thread>
#include <utility>

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

static LoadError DdsFromMemory(ByteView dds, uint32_t maxRes, const std::string& label, TexImage& out) {
    out = TexImage();
    auto fail = [&](LoadCode code, const char* reason) { out = TexImage(); return makeLoadError(code, label, reason); };
    if (!maxRes) return fail(LoadCode::InvalidArgument, "Texture resolution must be positive");
    if (!dds.data) return fail(LoadCode::InvalidArgument, "DDS payload is null");
    uint32_t header[37]{};
    if (dds.size < sizeof(header)) return fail(LoadCode::Truncated, "Truncated DDS header");
    std::memcpy(header, dds.data, sizeof(header));
    if (header[0] != 0x20534444 || header[1] != 124 || header[19] != 32 ||
        !(header[20] & 4) || header[21] != 0x30315844) return fail(LoadCode::Unsupported, "Expected DX10 DDS header");
    auto format = static_cast<DXGI_FORMAT>(header[32]);
    if (format != DXGI_FORMAT_BC5_UNORM && format != DXGI_FORMAT_BC7_UNORM && format != DXGI_FORMAT_BC7_UNORM_SRGB)
        return fail(LoadCode::Unsupported, "DDS requires BC5 UNORM or BC7 UNORM/sRGB");
    if (header[33] != 3 || header[35] != 1 || (header[34] & 4) || header[28] || header[6] > 1)
        return fail(LoadCode::Unsupported, "DDS arrays, cubes and volumes are not supported");
    uint32_t w = header[4], h = header[3], levels = std::max(1u, header[7]);
    {
        const auto fit = starlight::texture_limits::checkImage(w, h, 4, 1, UINT64_MAX);
        if (!fit.ok()) return fail(LoadCode::TooLarge, ("DDS " + starlight::texture_limits::explain(fit, w, h, 4, 1, UINT64_MAX)).c_str());
    }
    uint32_t maximumLevels = 1;
    for (auto edge = std::max(w, h); edge > 1; edge /= 2) ++maximumLevels;
    if (levels > maximumLevels) return fail(LoadCode::Malformed, "Invalid DDS mip count");
    const size_t available = dds.size - sizeof(header);
    size_t required = 0; uint32_t mw = w, mh = h;
    for (uint32_t l = 0; l < levels; ++l) {
        required += MipBytes(mw, mh, format); mw = std::max(1u, mw / 2); mh = std::max(1u, mh / 2);
    }
    if (available < required) return fail(LoadCode::Truncated, "Truncated DDS mip data");
    TexImage image; image.format = format;
    size_t cursor = sizeof(header);
    for (uint32_t l = 0; l < levels; ++l) {
        const size_t bytes = MipBytes(w, h, format);
        if (w <= maxRes && h <= maxRes) {
            if (image.mips.empty()) { image.w = w; image.h = h; }
            image.mips.emplace_back(dds.data + cursor, dds.data + cursor + bytes);
        }
        cursor += bytes;
        w = std::max(1u, w / 2); h = std::max(1u, h / 2);
    }
    if (image.mips.empty()) return fail(LoadCode::Unsupported, "DDS lacks a mip within the requested resolution");
    out = std::move(image); return LoadError();
}

LoadError loadTextureDDSFromMemory(ByteView dds, uint32_t maxRes, const std::string& label, TexImage& out) {
    try { return DdsFromMemory(dds, maxRes, label, out); }
    catch (const std::bad_alloc&) { out = TexImage(); return makeLoadError(LoadCode::OutOfMemory, label, "not enough memory to retain the DDS mips"); }
    catch (...) { out = TexImage(); return makeLoadError(LoadCode::Internal, label, "unexpected exception while reading the DDS"); }
}

bool loadTextureDDS(const std::string& path, uint32_t maxRes, TexImage& out, std::string& err) {
    out = {}; err.clear();
    if (!maxRes) { err = "Texture resolution must be positive"; return false; }
    std::vector<uint8_t> bytes;
    LoadError e = readWholeFile(path, bytes);
    if (e.ok()) e = loadTextureDDSFromMemory(ByteView(bytes), maxRes, path, out);
    if (e.failed()) { out = {}; err = e.message; return false; }
    return true;
}

namespace {

constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha256 {
    uint32_t state[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t block[64]{};
    size_t filled = 0;
    uint64_t length = 0;

    void compress(const uint8_t* p) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kSha256K[i] + w[i];
            const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void update(const uint8_t* p, size_t n) {
        length += n;
        if (filled) {
            const size_t take = std::min(n, sizeof(block) - filled);
            std::memcpy(block + filled, p, take); filled += take; p += take; n -= take;
            if (filled < sizeof(block)) return;
            compress(block); filled = 0;
        }
        for (; n >= 64; p += 64, n -= 64) compress(p);
        if (n) { std::memcpy(block, p, n); filled = n; }
    }

    std::string hex() {
        const uint64_t bits = length * 8;
        const uint8_t one = 0x80, zero = 0;
        update(&one, 1);
        while (filled != 56) update(&zero, 1);
        uint8_t tail[8];
        for (int i = 0; i < 8; ++i) tail[i] = uint8_t(bits >> (56 - 8 * i));
        update(tail, 8);
        static const char digits[] = "0123456789abcdef";
        std::string text;
        for (uint32_t word : state) for (int shift = 28; shift >= 0; shift -= 4) text.push_back(digits[(word >> shift) & 15u]);
        return text;
    }
};

const uint32_t* Crc32Table() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    return table.data();
}

uint32_t Crc32(const uint8_t* p, size_t n) {
    const uint32_t* table = Crc32Table();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t ReadBE32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]); }

const uint8_t kPngSignature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

bool HasPngSignature(ByteView b) { return b.data && b.size >= 8 && std::memcmp(b.data, kPngSignature, 8) == 0; }

struct PngInfo {
    uint32_t width = 0, height = 0;
    uint8_t depth = 0, color = 0, interlace = 0;
    std::vector<std::pair<size_t, size_t>> data;
};

struct Huffman {
    std::vector<uint32_t> table;
    int bits = 0;

    bool build(const uint8_t* lengths, int count) {
        int counts[16]{};
        for (int i = 0; i < count; ++i) ++counts[lengths[i]];
        counts[0] = 0;
        int maxLen = 0;
        for (int l = 1; l <= 15; ++l) if (counts[l]) maxLen = l;
        int left = 1;
        for (int l = 1; l <= 15; ++l) { left <<= 1; left -= counts[l]; if (left < 0) return false; }
        bits = std::max(maxLen, 1);
        table.assign(size_t(1) << bits, 0);
        uint32_t next[16]{}, code = 0;
        for (int l = 1; l <= 15; ++l) { code = (code + uint32_t(counts[l - 1])) << 1; next[l] = code; }
        for (int s = 0; s < count; ++s) {
            const int l = lengths[s];
            if (!l) continue;
            uint32_t c = next[l]++, r = 0;
            for (int k = 0; k < l; ++k) { r = (r << 1) | (c & 1u); c >>= 1; }
            for (uint32_t index = r; index < table.size(); index += 1u << l) table[index] = (uint32_t(l) << 16) | uint32_t(s);
        }
        return true;
    }
};

struct FixedTables {
    Huffman lit, dist;
    FixedTables() {
        uint8_t lengths[288];
        for (int i = 0; i < 144; ++i) lengths[i] = 8;
        for (int i = 144; i < 256; ++i) lengths[i] = 9;
        for (int i = 256; i < 280; ++i) lengths[i] = 7;
        for (int i = 280; i < 288; ++i) lengths[i] = 8;
        lit.build(lengths, 288);
        uint8_t fixedDist[30];
        for (auto& d : fixedDist) d = 5;
        dist.build(fixedDist, 30);
    }
};

struct Inflater {
    const uint8_t* in = nullptr;
    size_t inSize = 0, inPos = 0;
    uint64_t tableWork = 0, tableLimit = UINT64_MAX;   // Huffman table entries built so far, and the most the stream's size can justify (real encoders stay under 2 per input byte).
    uint64_t buffer = 0;
    int have = 0;
    uint8_t* out = nullptr;
    size_t outCap = 0, outPos = 0;
    std::string failure;

    void refill() {
        while (have <= 56 && inPos < inSize) { buffer |= uint64_t(in[inPos++]) << have; have += 8; }
    }
    bool take(int n, uint32_t& value) {
        if (n == 0) { value = 0; return true; }
        if (have < n) refill();
        if (have < n) { failure = "compressed data ends early"; return false; }
        value = uint32_t(buffer & ((uint64_t(1) << n) - 1));
        buffer >>= n; have -= n;
        return true;
    }
    bool symbol(const Huffman& h, uint32_t& sym) {
        if (have < 15) refill();
        const uint32_t entry = h.table[size_t(buffer & ((uint64_t(1) << h.bits) - 1))];
        const int len = int(entry >> 16);
        if (!len) { failure = "invalid Huffman code"; return false; }
        if (len > have) { failure = "compressed data ends early"; return false; }
        buffer >>= len; have -= len;
        sym = entry & 0xFFFFu;
        return true;
    }
    bool codes(const Huffman& lit, const Huffman& dist) {
        static const uint16_t lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
        static const uint8_t lext[29] = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
        static const uint16_t dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
        static const uint8_t dext[30] = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };
        for (;;) {
            uint32_t sym;
            if (!symbol(lit, sym)) return false;
            if (sym < 256) {
                if (outPos >= outCap) { failure = "decompressed data is longer than the image needs"; return false; }
                out[outPos++] = uint8_t(sym);
                continue;
            }
            if (sym == 256) return true;
            sym -= 257;
            if (sym >= 29) { failure = "invalid length symbol"; return false; }
            uint32_t extra, length, distance;
            if (!take(lext[sym], extra)) return false;
            length = lbase[sym] + extra;
            if (!symbol(dist, sym) || sym >= 30) { if (failure.empty()) failure = "invalid distance symbol"; return false; }
            if (!take(dext[sym], extra)) return false;
            distance = dbase[sym] + extra;
            if (distance > outPos) { failure = "back-reference reaches before the start of the data"; return false; }
            if (length > outCap - outPos) { failure = "decompressed data is longer than the image needs"; return false; }
            const uint8_t* from = out + outPos - distance;
            uint8_t* to = out + outPos;
            if (distance >= length) std::memcpy(to, from, length);
            else for (uint32_t i = 0; i < length; ++i) to[i] = from[i];
            outPos += length;
        }
    }
    bool run() {
        for (;;) {
            uint32_t last, type;
            if (!take(1, last) || !take(2, type)) return false;
            if (type == 0) {
                const int drop = have & 7;
                buffer >>= drop; have -= drop;
                uint32_t len, nlen;
                if (!take(16, len) || !take(16, nlen)) return false;
                if ((len ^ 0xFFFFu) != nlen) { failure = "stored block length check failed"; return false; }
                if (len > outCap - outPos) { failure = "decompressed data is longer than the image needs"; return false; }
                while (len && have >= 8) { out[outPos++] = uint8_t(buffer); buffer >>= 8; have -= 8; --len; }
                if (len > inSize - inPos) { failure = "compressed data ends early"; return false; }
                std::memcpy(out + outPos, in + inPos, len);
                inPos += len; outPos += len;
            } else if (type == 1 || type == 2) {
                Huffman lit, dist;
                const Huffman* useLit = &lit;
                const Huffman* useDist = &dist;
                if (type == 1) {
                    static const FixedTables fixed;
                    useLit = &fixed.lit; useDist = &fixed.dist;
                } else {
                    uint8_t lengths[320]{};
                    uint32_t nlit, ndist, ncode;
                    if (!take(5, nlit) || !take(5, ndist) || !take(4, ncode)) return false;
                    nlit += 257; ndist += 1; ncode += 4;
                    if (nlit > 286 || ndist > 30) { failure = "too many length or distance codes"; return false; }
                    static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                    uint8_t clen[19]{};
                    for (uint32_t i = 0; i < ncode; ++i) { uint32_t v; if (!take(3, v)) return false; clen[order[i]] = uint8_t(v); }
                    Huffman lenCode;
                    if (!lenCode.build(clen, 19)) { failure = "invalid code length code"; return false; }
                    uint32_t index = 0;
                    while (index < nlit + ndist) {
                        uint32_t sym;
                        if (!symbol(lenCode, sym)) return false;
                        if (sym < 16) { lengths[index++] = uint8_t(sym); continue; }
                        uint32_t repeat, value = 0;
                        if (sym == 16) {
                            if (!index) { failure = "repeat with no previous code length"; return false; }
                            value = lengths[index - 1];
                            if (!take(2, repeat)) return false; repeat += 3;
                        } else if (sym == 17) { if (!take(3, repeat)) return false; repeat += 3; }
                        else { if (!take(7, repeat)) return false; repeat += 11; }
                        if (index + repeat > nlit + ndist) { failure = "code length repeat overruns the table"; return false; }
                        while (repeat--) lengths[index++] = uint8_t(value);
                    }
                    if (!lengths[256]) { failure = "block has no end-of-block code"; return false; }
                    if (!lit.build(lengths, int(nlit)) || !dist.build(lengths + nlit, int(ndist))) { failure = "invalid Huffman table"; return false; }
                    tableWork += lenCode.table.size() + lit.table.size() + dist.table.size();
                    if (tableWork > tableLimit) { failure = "the block headers need far more table building than the data size can justify"; return false; }
                }
                if (!codes(*useLit, *useDist)) return false;
            } else { failure = "invalid block type"; return false; }
            if (last) return true;
        }
    }
};

LoadError VerifyPngPixelStream(ByteView b, const PngInfo& info, const std::string& label) {
    int channels = 0;
    switch (info.color) { case 0: channels = 1; break; case 2: channels = 3; break; case 3: channels = 1; break; case 4: channels = 2; break; case 6: channels = 4; break; default: break; }
    if (!channels || (info.depth != 1 && info.depth != 2 && info.depth != 4 && info.depth != 8 && info.depth != 16) || info.interlace > 1)
        return makeLoadError(LoadCode::Malformed, label, "PNG header has an invalid color type, bit depth or interlace method");
    const uint64_t bitsPerPixel = uint64_t(channels) * info.depth;
    struct Pass { uint64_t rowBytes, rows; };
    std::vector<Pass> passes;
    if (info.interlace == 0) passes.push_back({ (uint64_t(info.width) * bitsPerPixel + 7) / 8, info.height });
    else {
        static const uint32_t x0[7] = { 0,4,0,2,0,1,0 }, y0[7] = { 0,0,4,0,2,0,1 }, dx[7] = { 8,8,4,4,2,2,1 }, dy[7] = { 8,8,8,4,4,2,2 };
        for (int p = 0; p < 7; ++p) {
            const uint64_t pw = info.width > x0[p] ? (info.width - x0[p] + dx[p] - 1) / dx[p] : 0;
            const uint64_t ph = info.height > y0[p] ? (info.height - y0[p] + dy[p] - 1) / dy[p] : 0;
            if (pw && ph) passes.push_back({ (pw * bitsPerPixel + 7) / 8, ph });
        }
    }
    uint64_t expected = 0;
    for (const Pass& p : passes) expected += p.rows * (p.rowBytes + 1);
    if (expected > starlight::texture_limits::MaxPngPixelStreamBytes)
        return makeLoadError(LoadCode::TooLarge, label, "PNG pixel stream would be " + std::to_string(expected) + " bytes, larger than the " + std::to_string(starlight::texture_limits::MaxPngPixelStreamBytes) + " byte (1 GiB) limit for one image. Starlight never downsamples silently, so reduce the image or split it into tiles");
    size_t compressed = 0;
    for (const auto& span : info.data) compressed += span.second;
    if (expected > uint64_t(compressed) * 1032 + 1024)
        return makeLoadError(LoadCode::Truncated, label, "PNG header needs " + std::to_string(expected) + " bytes of pixel data but its compressed data (" + std::to_string(compressed) + " bytes) cannot hold that much");
    std::vector<uint8_t> joined;
    joined.reserve(compressed);
    for (const auto& span : info.data) joined.insert(joined.end(), b.data + span.first, b.data + span.first + span.second);
    if (joined.size() < 6) return makeLoadError(LoadCode::Truncated, label, "PNG image data is too short to hold a zlib stream");
    const unsigned cmf = joined[0], flg = joined[1];
    if ((cmf & 15u) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31u != 0 || (flg & 0x20u))
        return makeLoadError(LoadCode::Malformed, label, "PNG image data does not start with a valid zlib header");
    std::unique_ptr<uint8_t[]> raw(new uint8_t[size_t(expected)]);
    Inflater z;
    z.in = joined.data() + 2; z.inSize = joined.size() - 2; z.out = raw.get(); z.outCap = size_t(expected);
    z.tableLimit = (uint64_t(1) << 20) + 32 * uint64_t(z.inSize);
    if (!z.run())
        return makeLoadError(LoadCode::DecodeFailed, label, "PNG image data does not decompress: " + z.failure);
    if (z.outPos != expected)
        return makeLoadError(LoadCode::Truncated, label, "PNG image data decompresses to " + std::to_string(z.outPos) + " bytes but the header needs " + std::to_string(expected));
    const int spare = z.have & 7;
    z.buffer >>= spare; z.have -= spare;
    uint32_t stored = 0;
    for (int i = 0; i < 4; ++i) { uint32_t byte; if (!z.take(8, byte)) return makeLoadError(LoadCode::Truncated, label, "PNG zlib stream has no Adler-32 trailer"); stored = (stored << 8) | byte; }
    if (z.inSize - z.inPos + size_t(z.have / 8) != 0) return makeLoadError(LoadCode::Malformed, label, "PNG image data has extra bytes after its zlib stream");
    uint32_t a = 1, s = 0;
    for (size_t i = 0; i < z.outPos;) {
        const size_t stop = std::min<size_t>(z.outPos, i + 5552);
        for (; i < stop; ++i) { a += raw[i]; s += a; }
        a %= 65521u; s %= 65521u;
    }
    if (((s << 16) | a) != stored) return makeLoadError(LoadCode::Malformed, label, "PNG image data fails its Adler-32 check");
    size_t row = 0;
    for (const Pass& p : passes)
        for (uint64_t r = 0; r < p.rows; ++r, row += size_t(p.rowBytes + 1))
            if (raw[row] > 4) return makeLoadError(LoadCode::Malformed, label, "PNG scanline " + std::to_string(r) + " has the invalid filter type " + std::to_string(int(raw[row])));
    return LoadError();
}

LoadError ValidatePngStructure(ByteView b, const std::string& label, PngInfo& info) {
    size_t pos = 8;
    bool sawHeader = false, sawData = false, sawEnd = false, sawPalette = false, dataEnded = false;
    uint64_t paletteEntries = 0;
    auto malformed = [&](const std::string& what) { return makeLoadError(LoadCode::Malformed, label, what); };
    while (pos < b.size && !sawEnd) {
        if (b.size - pos < 12) return makeLoadError(LoadCode::Truncated, label, "PNG chunk header is cut off at byte " + std::to_string(pos));
        const uint32_t length = ReadBE32(b.data + pos);
        const std::string name(reinterpret_cast<const char*>(b.data + pos + 4), 4);
        if (length > 0x7FFFFFFFu || length > b.size - pos - 12)
            return makeLoadError(LoadCode::Truncated, label, "PNG chunk " + name + " at byte " + std::to_string(pos) + " claims " + std::to_string(length) + " bytes but the data ends first");
        for (unsigned char c : name)
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return malformed("PNG chunk at byte " + std::to_string(pos) + " has a damaged chunk name");
        if (!sawHeader && name != "IHDR") return malformed("PNG does not start with an IHDR chunk");
        if (Crc32(b.data + pos + 4, size_t(length) + 4) != ReadBE32(b.data + pos + 8 + length))
            return malformed("PNG chunk " + name + " at byte " + std::to_string(pos) + " fails its CRC check. A damaged chunk is refused even when it is optional (ancillary), because its bytes cannot be trusted; re-export the image");
        const uint8_t* body = b.data + pos + 8;
        if (name == "IHDR") {
            if (sawHeader) return malformed("PNG has a second IHDR chunk");
            if (length != 13) return malformed("PNG IHDR chunk has " + std::to_string(length) + " bytes instead of 13");
            info.width = ReadBE32(body); info.height = ReadBE32(body + 4);
            info.depth = body[8]; info.color = body[9]; info.interlace = body[12];
            const uint8_t depth = info.depth;
            const bool depthOk = info.color == 0 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)
                               : info.color == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8)
                               : (info.color == 2 || info.color == 4 || info.color == 6) ? (depth == 8 || depth == 16) : false;
            if (!depthOk) return malformed("PNG header has an invalid color type or bit depth for it");
            if (body[10] != 0 || body[11] != 0 || info.interlace > 1) return malformed("PNG header has an unsupported compression, filter or interlace method");
            sawHeader = true;
        }
        else if (name == "PLTE") {
            if (sawData || sawPalette) return malformed("PNG palette is repeated or comes after the image data");
            if (info.color == 0 || info.color == 4 || !length || length % 3 || length > 768 || (info.color == 3 && length / 3 > (uint64_t(1) << info.depth))) return malformed("PNG palette is invalid for its color type");
            sawPalette = true; paletteEntries = length / 3;
        }
        else if (name == "tRNS") {
            if (sawData) return malformed("PNG transparency chunk comes after the image data");
            const bool sized = info.color == 0 ? length == 2 : info.color == 2 ? length == 6 : info.color == 3 ? (sawPalette && length >= 1 && length <= paletteEntries) : false;
            if (!sized) return malformed("PNG transparency chunk does not fit its color type");
        }
        else if (name == "IDAT") {
            if (dataEnded) return malformed("PNG image data chunks are not consecutive");
            sawData = true; info.data.emplace_back(pos + 8, size_t(length));
        }
        else if (name == "IEND") {
            if (length) return malformed("PNG IEND chunk has data");
            sawEnd = true;
        }
        else if ((b.data[pos + 4] & 0x20) == 0) return malformed("PNG has the critical chunk " + name + " at byte " + std::to_string(pos) + ", which this reader does not know; a critical chunk cannot be skipped safely");
        if (name != "IDAT" && sawData) dataEnded = true;
        pos += size_t(length) + 12;
    }
    if (!sawEnd) return makeLoadError(LoadCode::Truncated, label, "PNG ends without an IEND chunk");
    if (pos != b.size) return malformed("PNG has " + std::to_string(b.size - pos) + " bytes after its IEND chunk, starting at byte " + std::to_string(pos) + ". The PNG specification lets a reader ignore them, but Starlight does not accept data that no decoder checks, so remove the trailing bytes");
    if (!sawData) return malformed("PNG has no IDAT image data");
    if (info.color == 3 && !sawPalette) return malformed("PNG needs a palette and has none");
    return LoadError();
}

const char kCacheMagic[8] = { 'I','L','M','I','P','0','0','3' };
const char* const kCacheExtension = ".mip3";

struct CacheHdr {
    char magic[8];
    uint32_t schema, decoder, role, filter, maxRes, w, h, levels;
    uint64_t srcSize, payloadBytes;
    uint32_t verified, headerCrc;
    char key[64];
    char payloadHash[64];
};
static_assert(sizeof(CacheHdr) == 192, "cache header layout");

struct CacheWant { uint32_t schema, decoder, role, filter, maxRes; };

struct CacheCounters {
    std::atomic<uint64_t> hits{ 0 }, misses{ 0 }, corrupt{ 0 }, written{ 0 }, writeFailures{ 0 }, evicted{ 0 }, unverified{ 0 }, forced{ 0 };
};

CacheCounters& Counters() {
    static CacheCounters counters;
    return counters;
}

std::atomic<uint64_t>& CacheLimitCell() {
    static std::atomic<uint64_t> limit{ uint64_t(8) << 30 };
    return limit;
}

struct DecodeGate {
    std::mutex lock;
    std::condition_variable wake;
    uint32_t active = 0, peak = 0;
    uint64_t decodes = 0, waited = 0;
};

DecodeGate& Gate() {
    static DecodeGate gate;
    return gate;
}

std::atomic<uint32_t>& WorkerCell() {
    static std::atomic<uint32_t> workers{ 0 };
    return workers;
}

uint32_t EnvWorkers() {
    static const uint32_t value = [] {
        wchar_t text[16]{};
        const DWORD n = GetEnvironmentVariableW(L"STARLIGHT_TEXTURE_WORKERS", text, 16);
        if (!n || n >= 16) return 0u;
        const unsigned long parsed = std::wcstoul(text, nullptr, 10);
        return parsed >= 1 && parsed <= 64 ? uint32_t(parsed) : 0u;
    }();
    return value;
}

uint32_t EffectiveWorkers() {
    const uint32_t set = WorkerCell().load();
    if (set) return set;
    if (const uint32_t env = EnvWorkers()) return env;
    return defaultTextureWorkerLimit(std::thread::hardware_concurrency());
}

struct DecodeSlot {
    DecodeSlot() {
        DecodeGate& g = Gate();
        std::unique_lock<std::mutex> hold(g.lock);
        bool waited = false;
        while (g.active >= EffectiveWorkers()) { waited = true; g.wake.wait(hold); }
        ++g.active; ++g.decodes;
        if (waited) ++g.waited;
        if (g.active > g.peak) g.peak = g.active;
    }
    ~DecodeSlot() {
        DecodeGate& g = Gate();
        { std::lock_guard<std::mutex> hold(g.lock); --g.active; }
        g.wake.notify_all();
    }
    DecodeSlot(const DecodeSlot&) = delete;
    DecodeSlot& operator=(const DecodeSlot&) = delete;
};

uint32_t ChainLevels(uint32_t w, uint32_t h) {
    uint32_t levels = 1;
    while (w > 1 || h > 1) { w = std::max(1u, w / 2); h = std::max(1u, h / 2); ++levels; }
    return levels;
}

bool ValidCacheId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-')) return false;
    return true;
}

std::filesystem::path PathFromUtf8(const std::string& text) {
    if (text.empty()) return std::filesystem::path();
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    if (n <= 0) return std::filesystem::path(text);
    std::wstring wide(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), wide.data(), n);
    return std::filesystem::path(wide);
}

std::string Utf8FromPath(const std::filesystem::path& path) {
    const std::wstring wide = path.wstring();
    if (wide.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string text(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), text.data(), n, nullptr, nullptr);
    return text;
}

enum class CacheRead { Hit, Absent, Rejected };

CacheRead ReadCache(const std::filesystem::path& path, const std::string& key, const CacheWant& want, uint64_t srcSize, TexImage& out, std::string& why) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return CacheRead::Absent;
    auto reject = [&](const char* reason) { why = reason; return CacheRead::Rejected; };
    f.seekg(0, std::ios::end);
    const std::streamoff total = f.tellg();
    f.seekg(0, std::ios::beg);
    if (total < static_cast<std::streamoff>(sizeof(CacheHdr))) return reject("the file is shorter than its header");
    CacheHdr h{};
    if (!f.read(reinterpret_cast<char*>(&h), sizeof(h))) return reject("the header cannot be read");
    if (std::memcmp(h.magic, kCacheMagic, 8) != 0) return reject("it is not a version 3 mip cache entry");
    CacheHdr zeroed = h;
    zeroed.headerCrc = 0;
    if (Crc32(reinterpret_cast<const uint8_t*>(&zeroed), sizeof(zeroed)) != h.headerCrc) return reject("the header fails its checksum");
    if (key.size() != 64 || std::memcmp(h.key, key.data(), 64) != 0) return reject("the entry belongs to another key");
    if (h.schema != want.schema || h.decoder != want.decoder || h.role != want.role || h.filter != want.filter || h.maxRes != want.maxRes)
        return reject("the entry was made for other settings");
    if (h.srcSize != srcSize) return reject("the entry was made from a source of another size");
    if (h.verified != 1) return reject("the entry was not written after a complete verification of its source");
    if (!h.w || !h.h || h.w > starlight::texture_limits::MaxSide || h.h > starlight::texture_limits::MaxSide || h.w > want.maxRes || h.h > want.maxRes || h.levels != ChainLevels(h.w, h.h))
        return reject("its dimensions or level count are invalid");
    const uint64_t chain = starlight::texture_limits::mipChainBytes(h.w, h.h, 4);
    const uint64_t budget = starlight::texture_limits::decodedBudget();
    if (chain == UINT64_MAX || chain != h.payloadBytes) return reject("its payload size does not match its dimensions");
    if (chain > budget / 3 * 4 + 1024) return reject("its payload is larger than the decoded-memory budget allows");
    if (uint64_t(total) != sizeof(CacheHdr) + chain) return reject("its length does not match its header");
    TexImage image; image.w = h.w; image.h = h.h; image.mips.resize(h.levels);
    Sha256 sha;
    uint32_t lw = h.w, lh = h.h;
    for (uint32_t l = 0; l < h.levels; ++l) {
        image.mips[l].resize(size_t(lw) * lh * 4);
        if (!f.read(reinterpret_cast<char*>(image.mips[l].data()), static_cast<std::streamsize>(image.mips[l].size()))) return reject("the file ends early");
        sha.update(image.mips[l].data(), image.mips[l].size());
        lw = std::max(1u, lw / 2); lh = std::max(1u, lh / 2);
    }
    if (sha.hex() != std::string(h.payloadHash, 64)) return reject("its pixel data fails its checksum");
    image.fromCache = true;
    out = std::move(image);
    return CacheRead::Hit;
}

std::string WriteCache(const std::filesystem::path& path, const std::string& key, const CacheWant& want, uint64_t srcSize, const TexImage& img) {
    if (key.size() != 64) return "mip cache key is invalid; loaded without caching";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return "mip cache directory could not be created (" + ec.message() + "); loaded without caching";
    Sha256 sha;
    uint64_t payload = 0;
    for (const auto& m : img.mips) { sha.update(m.data(), m.size()); payload += m.size(); }
    CacheHdr h{};
    std::memcpy(h.magic, kCacheMagic, 8);
    h.schema = want.schema; h.decoder = want.decoder; h.role = want.role; h.filter = want.filter; h.maxRes = want.maxRes;
    h.w = img.w; h.h = img.h; h.levels = static_cast<uint32_t>(img.mips.size());
    h.srcSize = srcSize; h.payloadBytes = payload; h.verified = 1;
    std::memcpy(h.key, key.data(), 64);
    const std::string hash = sha.hex();
    std::memcpy(h.payloadHash, hash.data(), 64);
    h.headerCrc = 0;
    h.headerCrc = Crc32(reinterpret_cast<const uint8_t*>(&h), sizeof(h));
    static std::atomic<uint64_t> serial{ 0 };
    const std::filesystem::path tmp = path.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(serial.fetch_add(1)) + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return "mip cache file could not be opened for writing; loaded without caching";
        f.write(reinterpret_cast<const char*>(&h), sizeof(h));
        for (const auto& m : img.mips) f.write(reinterpret_cast<const char*>(m.data()), static_cast<std::streamsize>(m.size()));
        f.flush();
        if (!f) { f.close(); std::filesystem::remove(tmp, ec); return "mip cache write failed; loaded without caching"; }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::error_code ignored; std::filesystem::remove(tmp, ignored); return "mip cache could not be finalized (" + ec.message() + "); loaded without caching"; }
    return std::string();
}

bool HasCacheMagic(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    char magic[8]{};
    return f && f.read(magic, 8) && std::memcmp(magic, kCacheMagic, 8) == 0;
}

void EvictCache(const std::filesystem::path& dir, const std::filesystem::path& keep) {
    struct Item { std::filesystem::path path; uint64_t size; std::filesystem::file_time_type time; };
    std::vector<Item> items;
    uint64_t total = 0;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code entry;
        if (!it->is_regular_file(entry) || entry || it->path().extension() != kCacheExtension) continue;
        const uint64_t size = it->file_size(entry);
        const auto time = it->last_write_time(entry);
        if (entry) continue;
        items.push_back({ it->path(), size, time });
        total += size;
    }
    const uint64_t limit = CacheLimitCell().load();
    if (total <= limit) return;
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.time != b.time ? a.time < b.time : a.path < b.path; });
    for (const Item& item : items) {
        if (total <= limit) break;
        if (item.path.filename() == keep.filename() || !HasCacheMagic(item.path)) continue;
        std::error_code removed;
        if (std::filesystem::remove(item.path, removed) && !removed) { total -= item.size; ++Counters().evicted; }
    }
}

}

std::string contentHashHex(ByteView bytes) {
    Sha256 hash;
    if (bytes.data && bytes.size) hash.update(bytes.data, bytes.size);
    return hash.hex();
}

// 2x2 box filter (odd edges clamp). Normals/roughness/AO get the same plain average: adequate
// for the mip distances the club camera uses; a renormalizing/linear-space variant is not implemented.
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

namespace {

struct DecodedImage {
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    std::string container;
    bool verified = false;
};

std::string HrText(HRESULT hr) {
    char text[32];
    std::snprintf(text, sizeof(text), "HRESULT 0x%08lX", static_cast<unsigned long>(hr));
    return text;
}

std::string ContainerName(const GUID& g) {
    if (g == GUID_ContainerFormatPng) return "png";
    if (g == GUID_ContainerFormatJpeg) return "jpeg";
    if (g == GUID_ContainerFormatBmp) return "bmp";
    if (g == GUID_ContainerFormatGif) return "gif";
    if (g == GUID_ContainerFormatTiff) return "tiff";
    if (g == GUID_ContainerFormatWmp) return "wmp";
    if (g == GUID_ContainerFormatIco) return "ico";
    return "wic";
}

LoadError CheckDecodedSize(uint64_t w, uint64_t h, const std::string& label) {
    const auto fit = starlight::texture_limits::checkDecodedRgba8(w, h);
    if (fit.ok()) return LoadError();
    const LoadCode code = fit.verdict == starlight::texture_limits::Verdict::EmptySide ? LoadCode::Malformed : LoadCode::TooLarge;
    return makeLoadError(code, label, starlight::texture_limits::explain(fit, w, h, starlight::texture_limits::DecodedChannels, starlight::texture_limits::DecodedBytesPerChannel));
}

// Smallest file a baseline Huffman JPEG of the declared geometry can be: every 8x8 block codes at least a DC code and an end-of-block code, one bit each.
// 0 when the header is not one this can bound (progressive, arithmetic, lossless, or a walk that does not reach a frame header).
uint64_t JpegMinimumBytes(ByteView b, uint32_t& width, uint32_t& height) {
    size_t pos = 2;
    while (pos + 4 <= b.size) {
        if (b.data[pos] != 0xFF) return 0;
        const uint8_t marker = b.data[pos + 1];
        if (marker == 0xFF) { ++pos; continue; }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) { pos += 2; continue; }
        const size_t length = (size_t(b.data[pos + 2]) << 8) | b.data[pos + 3];
        if (length < 2 || length > b.size - pos - 2) return 0;
        if (marker == 0xC0 || marker == 0xC1) {
            const uint8_t* s = b.data + pos + 4;
            const size_t count = length >= 8 ? s[5] : 0;
            if (count < 1 || count > 4 || length < 8 + 3 * count) return 0;
            height = (uint32_t(s[1]) << 8) | s[2]; width = (uint32_t(s[3]) << 8) | s[4];
            if (!width || !height) return 0;
            uint32_t hMax = 1, vMax = 1;
            for (size_t c = 0; c < count; ++c) {
                const uint32_t h = s[7 + 3 * c] >> 4, v = s[7 + 3 * c] & 15u;
                if (h < 1 || h > 4 || v < 1 || v > 4) return 0;
                hMax = std::max(hMax, h); vMax = std::max(vMax, v);
            }
            uint64_t blocks = 0;
            for (size_t c = 0; c < count; ++c) {
                const uint32_t h = s[7 + 3 * c] >> 4, v = s[7 + 3 * c] & 15u;
                blocks += uint64_t((uint64_t(width) * h + hMax - 1) / hMax + 7) / 8 * (uint64_t((uint64_t(height) * v + vMax - 1) / vMax + 7) / 8);
            }
            return (blocks * 2 + 7) / 8;
        }
        if (marker == 0xDA || marker == 0xD9 || (marker >= 0xC2 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)) return 0;
        pos += 2 + length;
    }
    return 0;
}

LoadError DecodeEncoded(ByteView encoded, const std::string& label, bool verifyPngStream, DecodedImage& out) {
    out = DecodedImage();
    if (encoded.size == 0) return makeLoadError(LoadCode::Truncated, label, "encoded image is empty");
    if (!encoded.data) return makeLoadError(LoadCode::InvalidArgument, label, "encoded image pointer is null");
    if (encoded.size > 0xFFFFFFFFull) return makeLoadError(LoadCode::TooLarge, label, "encoded image is larger than 4 GiB");
    DecodeSlot slot;
    PngInfo info;
    const bool isPng = HasPngSignature(encoded);
    if (isPng) {
        LoadError structure = ValidatePngStructure(encoded, label, info);
        if (structure.ok() && (info.width == 0 || info.height == 0)) structure = makeLoadError(LoadCode::Malformed, label, "PNG has no valid size");
        if (structure.ok()) structure = CheckDecodedSize(info.width, info.height, label);
        if (structure.ok()) {
            const uint64_t channels = info.color == 0 || info.color == 3 ? 1 : info.color == 2 ? 3 : info.color == 4 ? 2 : 4;
            const uint64_t bytesPerChannel = info.depth == 16 ? 2 : 1;
            const auto raw = starlight::texture_limits::checkImage(info.width, info.height, channels, bytesPerChannel, UINT64_MAX);
            if (!raw.ok()) structure = makeLoadError(LoadCode::TooLarge, label, starlight::texture_limits::explain(raw, info.width, info.height, channels, bytesPerChannel, UINT64_MAX));
        }
        if (structure.ok() && verifyPngStream) structure = VerifyPngPixelStream(encoded, info, label);
        if (structure.failed()) return structure;
    }
    if (encoded.size >= 3 && encoded.data[0] == 0xFF && encoded.data[1] == 0xD8 && encoded.data[2] == 0xFF) {
        size_t end = encoded.size;
        while (end > 2 && encoded.data[end - 1] == 0) --end;
        if (end < 4 || encoded.data[end - 2] != 0xFF || encoded.data[end - 1] != 0xD9)
            return makeLoadError(LoadCode::Truncated, label, "JPEG ends without an EOI marker");
        uint32_t jpegWidth = 0, jpegHeight = 0;
        const uint64_t jpegFloor = JpegMinimumBytes(encoded, jpegWidth, jpegHeight);
        if (jpegFloor > encoded.size)
            return makeLoadError(LoadCode::Truncated, label, "JPEG header declares " + std::to_string(jpegWidth) + "x" + std::to_string(jpegHeight) + " pixels, which needs at least " + std::to_string(jpegFloor) + " bytes of image data, but the file is only " + std::to_string(encoded.size) + " bytes");
    }
    struct ComScope {
        HRESULT result;
        ComScope() : result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
        ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
        ComScope(const ComScope&) = delete;
        ComScope& operator=(const ComScope&) = delete;
    } com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE)
        return makeLoadError(LoadCode::DecoderUnavailable, label, "COM initialization failed (" + HrText(com.result) + ")");
    ComPtr<IWICImagingFactory> fac;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac));
    if (FAILED(hr)) return makeLoadError(LoadCode::DecoderUnavailable, label, "WIC imaging factory could not be created (" + HrText(hr) + ")");
    ComPtr<IWICStream> stream;
    hr = fac->CreateStream(&stream);
    if (FAILED(hr)) return makeLoadError(LoadCode::DecoderUnavailable, label, "WIC stream could not be created (" + HrText(hr) + ")");
    hr = stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data), static_cast<DWORD>(encoded.size));
    if (FAILED(hr)) return makeLoadError(LoadCode::DecodeFailed, label, "WIC could not read the memory span (" + HrText(hr) + ")");
    ComPtr<IWICBitmapDecoder> dec;
    hr = fac->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr))
        return makeLoadError(hr == WINCODEC_ERR_COMPONENTNOTFOUND ? LoadCode::Unsupported : LoadCode::DecodeFailed, label,
                             std::string("no image decoder accepted the data (") + HrText(hr) + ")");
    GUID container{};
    if (SUCCEEDED(dec->GetContainerFormat(&container))) out.container = ContainerName(container);
    UINT frames = 0;
    if (FAILED(dec->GetFrameCount(&frames)) || frames == 0) return makeLoadError(LoadCode::Malformed, label, "image contains no frames");
    ComPtr<IWICBitmapFrameDecode> frame;
    hr = dec->GetFrame(0, &frame);
    if (FAILED(hr)) return makeLoadError(LoadCode::DecodeFailed, label, "first frame could not be read (" + HrText(hr) + ")");
    UINT fw = 0, fh = 0;
    hr = frame->GetSize(&fw, &fh);
    if (FAILED(hr) || fw == 0 || fh == 0) return makeLoadError(LoadCode::Malformed, label, "image has no valid size");
    {
        const LoadError fit = CheckDecodedSize(fw, fh, label);
        if (fit.failed()) return fit;
    }
    if (isPng && (fw != info.width || fh != info.height))
        return makeLoadError(LoadCode::Malformed, label, "decoder reports " + std::to_string(fw) + "x" + std::to_string(fh) + " but the PNG header declares " + std::to_string(info.width) + "x" + std::to_string(info.height));
    ComPtr<IWICFormatConverter> conv;
    hr = fac->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr)) hr = conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return makeLoadError(LoadCode::DecodeFailed, label, "conversion to RGBA8 failed (" + HrText(hr) + ")");
    out.w = fw; out.h = fh;
    out.verified = !isPng || verifyPngStream;
    out.rgba.resize(size_t(fw) * fh * 4);
    hr = conv->CopyPixels(nullptr, fw * 4, static_cast<UINT>(out.rgba.size()), out.rgba.data());
    if (FAILED(hr)) {
        out = DecodedImage();
        return makeLoadError(LoadCode::DecodeFailed, label, "pixel decode failed, the image data is truncated or corrupt (" + HrText(hr) + ")");
    }
    return LoadError();
}

LoadError LoadTextureImpl(ByteView encoded, const TextureLoadOptions& opt, TexImage& out) {
    if (!opt.maxRes) return makeLoadError(LoadCode::InvalidArgument, opt.label, "Texture resolution must be positive");
    if (encoded.data && encoded.size >= 4 && std::memcmp(encoded.data, "DDS ", 4) == 0)
        return loadTextureDDSFromMemory(encoded, opt.maxRes, opt.label, out);
    std::filesystem::path dir;
    if (!opt.cacheDir.empty()) dir = PathFromUtf8(opt.cacheDir);
    else if (opt.sharedCache) dir = PathFromUtf8(textureCacheDirectory());
    std::filesystem::path cache;
    std::string id, key, note;
    CacheWant want{ TextureCacheSchemaVersion, TextureDecoderVersion, uint32_t(opt.role), uint32_t(opt.filter), opt.maxRes };
    if (!dir.empty()) {
        if (opt.contentId.empty()) id = contentHashHex(encoded);
        else if (ValidCacheId(opt.contentId)) id = opt.contentId;
        else return makeLoadError(LoadCode::InvalidArgument, opt.label, "content id must be 1 to 64 characters of letters, digits, '-' or '_'");
        key = textureCacheKey(id, opt.maxRes, opt.role, opt.filter);
        cache = dir / PathFromUtf8(key + kCacheExtension);
        if (opt.forceDecode) ++Counters().forced;
        else {
            TexImage cached;
            std::string why;
            const CacheRead read = ReadCache(cache, key, want, encoded.size, cached, why);
            if (read == CacheRead::Hit) {
                ++Counters().hits;
                std::error_code touch;
                std::filesystem::last_write_time(cache, std::filesystem::file_time_type::clock::now(), touch);
                out = std::move(cached);
                return LoadError();
            }
            if (read == CacheRead::Rejected) {
                ++Counters().corrupt;
                note = "mip cache entry " + Utf8FromPath(cache.filename()) + " failed verification (" + why + "); it was ignored and is rebuilt from the source";
            }
            else ++Counters().misses;
        }
    }
    else if (opt.sharedCache) note = "no shared mip cache location could be resolved; loaded without caching";
    DecodedImage decoded;
    LoadError e = DecodeEncoded(encoded, opt.label, opt.verifyPngStream, decoded);
    if (e.failed()) return e;
    std::vector<uint8_t> level = std::move(decoded.rgba);
    uint32_t w = decoded.w, h = decoded.h;
    while ((w > opt.maxRes || h > opt.maxRes) && (w > 1 || h > 1)) {
        std::vector<uint8_t> next; uint32_t nw, nh; Downsample(level, w, h, next, nw, nh); level.swap(next); w = nw; h = nh;
    }
    TexImage image;
    image.w = w; image.h = h; image.mips.push_back(std::move(level));
    while (w > 1 || h > 1) {
        std::vector<uint8_t> next; uint32_t nw, nh; Downsample(image.mips.back(), w, h, next, nw, nh);
        image.mips.push_back(std::move(next)); w = nw; h = nh;
    }
    if (!cache.empty()) {
        std::string problem;
        if (!decoded.verified) {
            ++Counters().unverified;
            problem = "mip cache not written because the source was loaded without full verification";
        }
        else {
            problem = WriteCache(cache, key, want, encoded.size, image);
            if (problem.empty()) { ++Counters().written; EvictCache(dir, cache); }
            else ++Counters().writeFailures;
        }
        if (!problem.empty()) note += (note.empty() ? "" : "; ") + problem;
    }
    image.cacheNote = note;
    out = std::move(image);
    return LoadError();
}

}

LoadError loadTextureFromMemory(ByteView encoded, const TextureLoadOptions& options, TexImage& out) {
    out = TexImage();
    try { return LoadTextureImpl(encoded, options, out); }
    catch (const std::bad_alloc&) { out = TexImage(); return makeLoadError(LoadCode::OutOfMemory, options.label, "not enough memory to decode the texture"); }
    catch (...) { out = TexImage(); return makeLoadError(LoadCode::Internal, options.label, "unexpected exception while decoding the texture"); }
}

std::string textureCacheDirectory() {
    wchar_t root[32768];
    DWORD n = GetEnvironmentVariableW(L"STARLIGHT_DATA_ROOT", root, 32768);
    std::filesystem::path base;
    if (n && n < 32768) {
        base = std::filesystem::path(std::wstring(root, n));
        if (!base.is_absolute()) return std::string();
    }
    else {
        n = GetEnvironmentVariableW(L"LOCALAPPDATA", root, 32768);
        if (!n || n >= 32768) return std::string();
        base = std::filesystem::path(std::wstring(root, n)) / L"Starlight";
    }
    return Utf8FromPath(base / starlight::texture_limits::CacheRootFolder / starlight::texture_limits::TextureCacheFolder);
}

std::string textureCacheKey(const std::string& contentId, uint32_t maxRes, TextureRole role, TextureFilter filter, uint32_t schemaVersion, uint32_t decoderVersion) {
    const std::string text = "starlight-texture-mips|schema=" + std::to_string(schemaVersion) + "|decoder=" + std::to_string(decoderVersion) +
                             "|id=" + contentId + "|maxRes=" + std::to_string(maxRes) + "|role=" + std::to_string(uint32_t(role)) + "|filter=" + std::to_string(uint32_t(filter));
    return contentHashHex(ByteView(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

TextureCacheStats textureCacheStats() {
    CacheCounters& c = Counters();
    TextureCacheStats s;
    s.hits = c.hits.load(); s.misses = c.misses.load(); s.corrupt = c.corrupt.load(); s.written = c.written.load(); s.writeFailures = c.writeFailures.load();
    s.evicted = c.evicted.load(); s.unverifiedSkipped = c.unverified.load(); s.forcedDecodes = c.forced.load();
    return s;
}

void resetTextureCacheStats() {
    CacheCounters& c = Counters();
    c.hits = 0; c.misses = 0; c.corrupt = 0; c.written = 0; c.writeFailures = 0; c.evicted = 0; c.unverified = 0; c.forced = 0;
}

uint64_t textureCacheLimitBytes() { return CacheLimitCell().load(); }

void setTextureCacheLimitBytes(uint64_t bytes) { CacheLimitCell().store(bytes); }

TextureDecodeStats textureDecodeStats() {
    DecodeGate& g = Gate();
    std::lock_guard<std::mutex> hold(g.lock);
    TextureDecodeStats s;
    s.decodes = g.decodes; s.waited = g.waited; s.active = g.active; s.peakConcurrent = g.peak;
    return s;
}

void resetTextureDecodeStats() {
    DecodeGate& g = Gate();
    std::lock_guard<std::mutex> hold(g.lock);
    g.decodes = 0; g.waited = 0; g.peak = g.active;
}

uint32_t defaultTextureWorkerLimit(uint32_t hardwareThreads) { return std::max(1u, std::min(4u, hardwareThreads / 2)); }

uint32_t textureWorkerLimit() { return EffectiveWorkers(); }

void setTextureWorkerLimit(uint32_t workers) {
    DecodeGate& g = Gate();
    { std::lock_guard<std::mutex> hold(g.lock); WorkerCell().store(std::min(workers, 64u)); }
    g.wake.notify_all();
}

std::vector<LoadError> prepareTextures(const std::vector<TexturePrepareJob>& jobs, std::vector<TexImage>* images, uint32_t workers) {
    std::vector<LoadError> errors(jobs.size());
    if (images) { images->clear(); images->resize(jobs.size()); }
    if (jobs.empty()) return errors;
    uint32_t count = std::min(workers ? workers : textureWorkerLimit(), 64u);
    count = std::max(1u, std::min<uint32_t>(count, uint32_t(jobs.size())));
    std::atomic<size_t> next{ 0 };
    auto work = [&] {
        for (;;) {
            const size_t i = next.fetch_add(1);
            if (i >= jobs.size()) return;
            TexImage image;
            errors[i] = loadTextureFromMemory(jobs[i].encoded, jobs[i].options, image);
            if (images && errors[i].ok()) (*images)[i] = std::move(image);
        }
    };
    std::vector<std::thread> pool;
    try { for (uint32_t t = 1; t < count; ++t) pool.emplace_back(work); }
    catch (...) {}
    work();
    for (std::thread& t : pool) t.join();
    return errors;
}

LoadError probeTextureDecode(ByteView encoded, const std::string& label, TextureProbe& out) {
    out = TextureProbe();
    try {
        if (encoded.data && encoded.size >= 4 && std::memcmp(encoded.data, "DDS ", 4) == 0) {
            TexImage dds;
            LoadError e = loadTextureDDSFromMemory(encoded, starlight::texture_limits::MaxSide, label, dds);
            if (e.failed()) return e;
            out.width = dds.w; out.height = dds.h; out.format = dds.format;
            out.mipLevels = static_cast<uint32_t>(dds.mips.size()); out.decodedBytes = dds.bytes(); out.container = "dds";
            return LoadError();
        }
        DecodedImage decoded;
        LoadError e = DecodeEncoded(encoded, label, true, decoded);
        if (e.failed()) return e;
        out.width = decoded.w; out.height = decoded.h; out.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        out.mipLevels = ChainLevels(decoded.w, decoded.h); out.decodedBytes = decoded.rgba.size(); out.container = decoded.container;
        return LoadError();
    }
    catch (const std::bad_alloc&) { out = TextureProbe(); return makeLoadError(LoadCode::OutOfMemory, label, "not enough memory to decode the texture"); }
    catch (...) { out = TextureProbe(); return makeLoadError(LoadCode::Internal, label, "unexpected exception while decoding the texture"); }
}

bool loadTextureRGBA(const std::string& pngPath, uint32_t maxRes, const std::string& cacheDir, TexImage& out, std::string& err) {
    out = TexImage(); err.clear();
    if (!maxRes) { err = "Texture resolution must be positive"; return false; }
    std::vector<uint8_t> bytes;
    LoadError e = readWholeFile(pngPath, bytes);
    if (e.ok()) {
        TextureLoadOptions options;
        options.maxRes = maxRes; options.label = pngPath; options.cacheDir = cacheDir;
        e = loadTextureFromMemory(ByteView(bytes), options, out);
    }
    if (e.failed()) { out = TexImage(); err = e.message; return false; }
    return true;
}

ComPtr<ID3D12Resource> uploadTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmd, const TexImage& img,
                                     std::vector<ComPtr<ID3D12Resource>>& keepAlive, std::string& err) {
    ComPtr<ID3D12Resource> tex;
    if (!device || !cmd || !img.w || !img.h || img.w > starlight::texture_limits::MaxSide || img.h > starlight::texture_limits::MaxSide || img.mips.empty()) { err = "invalid image or upload context"; return tex; }
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
