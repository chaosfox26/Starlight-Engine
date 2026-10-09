#define CGLTF_IMPLEMENTATION
#include "glb_fuzz.hpp"
#define CGLTF_MALLOC(size) glbfuzz::trackedMalloc(size)
#define CGLTF_FREE(ptr) glbfuzz::trackedFree(ptr)
#include "../../deep/engine/model/skinned.cpp"
#include "../../deep/engine/model/texture.h"
#include "../../deep/engine/surface_texture_data.h"
#include <dxgi1_6.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <set>
#include <thread>
#include <wincodec.h>
using Microsoft::WRL::ComPtr;
namespace memload {
namespace fs = std::filesystem;
using model::ByteView;
using model::LoadCode;
using model::LoadError;

std::vector<uint8_t> readBytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<uint8_t> v;
    if (!f) return v;
    f.seekg(0, std::ios::end); const auto n = f.tellg(); f.seekg(0);
    v.resize(size_t(n));
    if (n > 0) f.read(reinterpret_cast<char*>(v.data()), n);
    return v;
}

void writeBytes(const fs::path& p, const std::vector<uint8_t>& v) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(v.data()), std::streamsize(v.size()));
}

uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = ~0u;
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1; }
    return ~c;
}

void putBE(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x >> 24)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x)); }
void putLE(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 24)); }

void putChunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
    putBE(png, uint32_t(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    png.insert(png.end(), body.begin(), body.end());
    putBE(png, crc32(body.data(), body.size()));
}

struct PngSpec { bool badZlibHeader = false; bool badAdler = false; bool interlaced = false; bool badFilter = false; };

std::vector<uint8_t> makePng(uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba, PngSpec spec = {}) {
    std::vector<uint8_t> raw;
    if (spec.interlaced) {
        const uint32_t x0[7] = { 0,4,0,2,0,1,0 }, y0[7] = { 0,0,4,0,2,0,1 }, dx[7] = { 8,8,4,4,2,2,1 }, dy[7] = { 8,8,8,4,4,2,2 };
        for (int p = 0; p < 7; ++p) {
            const uint32_t pw = w > x0[p] ? (w - x0[p] + dx[p] - 1) / dx[p] : 0, ph = h > y0[p] ? (h - y0[p] + dy[p] - 1) / dy[p] : 0;
            if (!pw || !ph) continue;
            for (uint32_t r = 0; r < ph; ++r) { raw.push_back(0); for (uint32_t i = 0; i < pw * 4; ++i) raw.push_back(uint8_t(i * 5 + r)); }
        }
    } else
        for (uint32_t y = 0; y < h; ++y) { raw.push_back(spec.badFilter && y == 2 ? 9 : 0); raw.insert(raw.end(), rgba.begin() + size_t(y) * w * 4, rgba.begin() + size_t(y + 1) * w * 4); }
    std::vector<uint8_t> z = spec.badZlibHeader ? std::vector<uint8_t>{ 0xFF, 0xFF } : std::vector<uint8_t>{ 0x78, 0x01 };
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        z.push_back(pos + n == raw.size() ? 1 : 0);
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8)); z.push_back(uint8_t(~n)); z.push_back(uint8_t((~n) >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) { a = (a + byte) % 65521u; b = (b + a) % 65521u; }
    if (spec.badAdler) a ^= 1u;
    putBE(z, (b << 16) | a);
    std::vector<uint8_t> png{ 137, 80, 78, 71, 13, 10, 26, 10 };
    std::vector<uint8_t> ihdr; putBE(ihdr, w); putBE(ihdr, h);
    for (uint8_t v : { 8, 6, 0, 0, spec.interlaced ? 1 : 0 }) ihdr.push_back(v);
    putChunk(png, "IHDR", ihdr); putChunk(png, "IDAT", z); putChunk(png, "IEND", {});
    return png;
}

std::vector<uint8_t> gradient(uint32_t w, uint32_t h) {
    std::vector<uint8_t> px;
    for (uint32_t y = 0; y < h; ++y) for (uint32_t x = 0; x < w; ++x) { px.push_back(uint8_t(x * 30 + 3)); px.push_back(uint8_t(y * 40 + 1)); px.push_back(uint8_t((x + y) * 10)); px.push_back(uint8_t(255 - x * 5)); }
    return px;
}

std::vector<uint8_t> referenceDown(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh, uint32_t& dw, uint32_t& dh) {
    dw = std::max(1u, sw / 2); dh = std::max(1u, sh / 2);
    std::vector<uint8_t> dst(size_t(dw) * dh * 4);
    for (uint32_t y = 0; y < dh; ++y) for (uint32_t x = 0; x < dw; ++x) for (uint32_t c = 0; c < 4; ++c) {
        const uint32_t x0 = std::min(sw - 1, x * 2), x1 = std::min(sw - 1, x * 2 + 1), y0 = std::min(sh - 1, y * 2), y1 = std::min(sh - 1, y * 2 + 1);
        dst[(size_t(y) * dw + x) * 4 + c] = uint8_t((src[(size_t(y0) * sw + x0) * 4 + c] + src[(size_t(y0) * sw + x1) * 4 + c] + src[(size_t(y1) * sw + x0) * 4 + c] + src[(size_t(y1) * sw + x1) * 4 + c] + 2) / 4);
    }
    return dst;
}

bool sameImage(const model::TexImage& a, const model::TexImage& b) {
    return a.w == b.w && a.h == b.h && a.format == b.format && a.mips == b.mips;
}

using glbfuzz::GlbSpec;
using glbfuzz::makeGlb;
using glbfuzz::makeSparseGlb;

std::vector<uint8_t> atlasBytes(const std::vector<float>& values) {
    std::vector<uint8_t> b(values.size() * 4);
    if (!values.empty()) std::memcpy(b.data(), values.data(), b.size());
    return b;
}

bool sameModel(const model::SkinnedModel& a, const model::SkinnedModel& b) {
    if (a.name != b.name || a.verts.size() != b.verts.size() || a.indices != b.indices || a.subs.size() != b.subs.size() || a.joints.size() != b.joints.size() || a.clips.size() != b.clips.size()) return false;
    if (a.atlasUV != b.atlasUV || a.loaded != b.loaded || std::memcmp(&a.bbMin, &b.bbMin, sizeof(a.bbMin)) || std::memcmp(&a.bbMax, &b.bbMax, sizeof(a.bbMax))) return false;
    if (!a.verts.empty() && std::memcmp(a.verts.data(), b.verts.data(), a.verts.size() * sizeof(model::SkinVertex))) return false;
    for (size_t i = 0; i < a.subs.size(); ++i) {
        const auto &x = a.subs[i], &y = b.subs[i];
        if (x.node != y.node || x.material != y.material || x.gearSlot != y.gearSlot || x.firstIndex != y.firstIndex || x.indexCount != y.indexCount || x.emissive != y.emissive || std::memcmp(&x.baseColor, &y.baseColor, sizeof(x.baseColor))) return false;
    }
    for (size_t i = 0; i < a.joints.size(); ++i) {
        const auto &x = a.joints[i], &y = b.joints[i];
        if (x.name != y.name || x.parent != y.parent || std::memcmp(&x.ibm, &y.ibm, sizeof(x.ibm)) || std::memcmp(&x.t, &y.t, sizeof(x.t)) || std::memcmp(&x.r, &y.r, sizeof(x.r)) || std::memcmp(&x.s, &y.s, sizeof(x.s))) return false;
    }
    for (size_t i = 0; i < a.clips.size(); ++i) {
        const auto &x = a.clips[i], &y = b.clips[i];
        if (x.name != y.name || x.duration != y.duration || x.loop != y.loop || x.channels.size() != y.channels.size()) return false;
        for (size_t c = 0; c < x.channels.size(); ++c)
            if (x.channels[c].joint != y.channels[c].joint || x.channels[c].path != y.channels[c].path || x.channels[c].times != y.channels[c].times || x.channels[c].values != y.channels[c].values) return false;
    }
    return true;
}

// A model the loader accepted must be safe for every consumer: indices and sub-meshes inside their arrays, finite vertices, sane skeleton and clips.
std::string modelProblem(const model::SkinnedModel& m) {
    if (!m.loaded) return "accepted but loaded is false";
    if (m.verts.empty() || m.indices.empty()) return "accepted with no vertices or indices";
    if (m.indices.size() % 3) return "index count is not a multiple of 3";
    for (uint32_t i : m.indices) if (i >= m.verts.size()) return "an index is outside the vertex array";
    for (const auto& v : m.verts) {
        const float all[] = { v.px, v.py, v.pz, v.nx, v.ny, v.nz, v.tx, v.ty, v.tz, v.tw, v.u, v.v, v.w[0], v.w[1], v.w[2], v.w[3] };
        for (float f : all) if (!std::isfinite(f)) return "a vertex holds a non-finite value";
        if (!m.joints.empty()) for (int k = 0; k < 4; ++k) if (v.w[k] > 0.f && v.j[k] >= m.joints.size()) return "a weighted joint index is outside the skeleton";
    }
    if (m.subs.empty() || m.subs.size() > 30) return "sub-mesh count is outside 1..30";
    for (const auto& s : m.subs) if (s.indexCount % 3 || size_t(s.firstIndex) + s.indexCount > m.indices.size()) return "a sub-mesh range is outside the index array";
    for (size_t j = 0; j < m.joints.size(); ++j) if (m.joints[j].parent >= int(j)) return "a joint parent does not precede the joint";
    for (const auto& c : m.clips) {
        if (!std::isfinite(c.duration) || c.duration < 0) return "a clip duration is not finite and non-negative";
        for (const auto& ch : c.channels) {
            if (ch.joint < 0 || size_t(ch.joint) >= m.joints.size() || ch.path < 0 || ch.path > 2) return "a channel targets a joint or path that does not exist";
            if (ch.times.empty() || ch.values.size() != ch.times.size() * (ch.path == 1 ? 4u : 3u)) return "a channel has " + std::to_string(ch.times.size()) + " times but " + std::to_string(ch.values.size()) + " values";
        }
    }
    const float box[] = { m.bbMin.x, m.bbMin.y, m.bbMin.z, m.bbMax.x, m.bbMax.y, m.bbMax.z };
    for (float f : box) if (!std::isfinite(f)) return "the bounding box is not finite";
    if (m.bbMin.x > m.bbMax.x || m.bbMin.y > m.bbMax.y || m.bbMin.z > m.bbMax.z) return "the bounding box is inverted";
    if (!m.joints.empty()) {   // sampling every clip must stay inside the arrays
        std::vector<DirectX::XMFLOAT4X4> palette(m.joints.size()), global(m.joints.size());
        for (int clip = 0; clip < (int)m.clips.size(); ++clip)
            for (float t : { 0.f, 0.37f, 1e6f }) model::evalPose(m, clip, t, (clip + 1) % (int)m.clips.size(), t * 0.5f, 0.5f, palette.data(), global.data());
    }
    return std::string();
}

std::vector<uint8_t> editJson(const std::vector<uint8_t>& bytes, const char* from, const std::string& to) {
    glbfuzz::Parts parts;
    glbfuzz::split(bytes, parts);
    const size_t at = parts.json.find(from);
    if (at == std::string::npos) throw std::runtime_error("fuzz regression fixture text missing");
    parts.json.replace(at, std::strlen(from), to);
    return glbfuzz::pack(std::move(parts));
}
template <class R> void fuzzSkinned(const fs::path& dir, const fs::path& root, R& require) {
    std::vector<glbfuzz::Seed> seeds;
    seeds.push_back({ "makeGlb", glbfuzz::makeGlb() });
    GlbSpec step; step.step = true; seeds.push_back({ "makeGlb-step", glbfuzz::makeGlb(step) });
    GlbSpec matrix; matrix.matrixJoint = true; seeds.push_back({ "makeGlb-matrix", glbfuzz::makeGlb(matrix) });
    GlbSpec three; three.primitives = 3; seeds.push_back({ "makeGlb-3-primitives", glbfuzz::makeGlb(three) });
    seeds.push_back({ "sparse", glbfuzz::makeSparseGlb(76) });
    seeds.push_back({ "interleaved", glbfuzz::makeInterleavedGlb() });
    seeds.push_back({ "data-uri", glbfuzz::makeDataUriGlb() });
    for (const char* name : { "migration/fixtures/triangle.glb", "migration/fixtures/deep-node-graph.glb" }) {
        auto bytes = readBytes(root / name);
        if (bytes.empty()) { std::cout << "  NOT RUN, repo fixture missing: " << name << '\n'; require(false, "repo GLB fixture present for the fuzzer"); }
        seeds.push_back({ name, std::move(bytes) });
    }
    for (size_t s = 0; s < 4; ++s) {   // the unmutated fixtures that are meant to load do load
        model::SkinnedModel m;
        const LoadError e = model::loadSkinnedFromMemory(ByteView(seeds[s].bytes), nullptr, seeds[s].name, m);
        require(e.ok() && modelProblem(m).empty(), "GLB fuzz: each unmutated model fixture loads and is valid");
    }
    size_t counter = 0;
    const size_t iterations = (size_t)glbfuzz::envNumber("STARLIGHT_GLB_FUZZ_ITERATIONS", 20000);
    const auto report = glbfuzz::run("skinned", seeds, iterations, glbfuzz::envNumber("STARLIGHT_GLB_FUZZ_SEED", 20261008), dir / "glb-fuzz", [&](const std::vector<uint8_t>& bytes) {
        glbfuzz::Outcome out;
        model::SkinnedModel m;
        const LoadError e = model::loadSkinnedFromMemory(ByteView(bytes), nullptr, "fuzz.glb", m);
        out.accepted = e.ok();
        if (e.failed()) {
            if (m.loaded || !m.verts.empty() || !m.indices.empty() || !m.subs.empty() || !m.joints.empty() || !m.clips.empty()) { out.valid = false; out.what = "a rejected input left a partial model behind"; }
            else if (e.message.empty()) { out.valid = false; out.what = "a rejection with no message"; }
            else if (e.code == LoadCode::Internal || e.code == LoadCode::OutOfMemory) { out.valid = false; out.what = std::string("a rejection of kind ") + model::loadCodeName(e.code) + ": " + e.message; }
            return out;
        }
        const std::string problem = modelProblem(m);
        if (!problem.empty()) { out.valid = false; out.what = problem; return out; }
        if (++counter % 5 == 0) {   // the atlas path on an accepted model: a right-sized finite span applies, a span with a NaN is refused
            std::vector<float> uv(m.verts.size() * 2, 0.25f);
            std::vector<uint8_t> raw(uv.size() * 4);
            std::memcpy(raw.data(), uv.data(), raw.size());
            model::SkinnedModel withAtlas;
            const ByteView view(raw);
            if (model::loadSkinnedFromMemory(ByteView(bytes), &view, "fuzz.glb", withAtlas).failed() || !withAtlas.atlasUV || !modelProblem(withAtlas).empty()) { out.valid = false; out.what = "an accepted model rejected a right-sized finite atlas"; return out; }
            const float nan = std::numeric_limits<float>::quiet_NaN();
            std::memcpy(raw.data() + raw.size() - 4, &nan, 4);
            if (model::loadSkinnedFromMemory(ByteView(bytes), &view, "fuzz.glb", withAtlas).code != LoadCode::AtlasNotFinite) { out.valid = false; out.what = "an atlas with a NaN was not refused"; return out; }
        }
        return out;
    });
    require(report.failures == 0, "GLB fuzz: the skinned loader never crashes, hangs, over-allocates, or accepts an invalid model");
    require(report.accepted > 0 && report.rejected > 0, "GLB fuzz: the skinned corpus exercises both outcomes");
}
template <class R> void run(const fs::path& dir, const fs::path& root, R& require) {
    const fs::path work = dir / "memload";
    fs::create_directories(work);
    auto show = [&](const char* what, const LoadError& e) { std::cout << "  " << what << " -> " << model::loadCodeName(e.code) << ": " << e.message << '\n'; };
    {
        const auto abc = std::string("abc"), nist = std::string("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
        auto hash = [](const std::string& s) { return model::contentHashHex(ByteView(reinterpret_cast<const uint8_t*>(s.data()), s.size())); };
        require(model::contentHashHex(ByteView()) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of empty input matches the published value");
        require(hash(abc) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of abc matches the published value");
        require(hash(nist) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 of the 56-byte padding edge case matches the published value");
        const std::string million(1000000, 'a');
        require(hash(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 of one million a matches the published value");
    }
    {
        const uint32_t W = 8, H = 6;
        const auto pixels = gradient(W, H);
        const auto png = makePng(W, H, pixels);
        const fs::path pngPath = work / "gradient.png";
        writeBytes(pngPath, png);
        model::TexImage fromMemory, fromPath; std::string err;
        model::TextureLoadOptions options; options.maxRes = 16; options.label = "gradient.png";
        require(model::loadTextureFromMemory(ByteView(png), options, fromMemory).ok(), "PNG span decodes");
        require(fromMemory.w == W && fromMemory.h == H && fromMemory.mips.size() == 4 && fromMemory.format == DXGI_FORMAT_R8G8B8A8_UNORM, "PNG span yields the full RGBA8 mip chain");
        require(fromMemory.mips[0] == pixels, "PNG span level 0 equals the encoded pixels exactly");
        uint32_t dw = 0, dh = 0;
        auto level1 = referenceDown(pixels, W, H, dw, dh);
        require(fromMemory.mips[1] == level1, "level 1 equals an independent 2x2 box filter of the decoded pixels");
        auto level2 = referenceDown(level1, dw, dh, dw, dh);
        require(fromMemory.mips[2] == level2 && fromMemory.mips[3].size() == 4, "lower levels continue the chain down to 1x1");
        require(model::loadTextureRGBA(pngPath.string(), 16, "", fromPath, err), "path loader still works");
        require(sameImage(fromMemory, fromPath), "path and memory loaders return identical images for the synthetic PNG");
        options.maxRes = 4;
        model::TexImage clamped;
        require(model::loadTextureFromMemory(ByteView(png), options, clamped).ok() && clamped.w == 4 && clamped.h == 3 && clamped.mips.size() == 3, "maxRes downsamples from the decoded pixels");
        require(clamped.mips[0] == level1, "downsampled level 0 equals the reference filter");
        model::TextureProbe probe;
        require(model::probeTextureDecode(ByteView(png), "gradient.png", probe).ok() && probe.width == W && probe.height == H && probe.format == DXGI_FORMAT_R8G8B8A8_UNORM && probe.container == "png" && probe.mipLevels == 4 && probe.decodedBytes == W * H * 4, "decode-only readiness reports width, height and format");
        for (size_t cut = 0; cut < png.size(); ++cut) {
            model::TextureProbe p;
            if (model::probeTextureDecode(ByteView(png.data(), cut), "cut.png", p).ok()) { std::cout << "  truncation at " << cut << " was accepted\n"; require(false, "every truncation of a PNG is rejected"); }
        }
        require(true, "every truncation of a PNG is rejected");
        model::TexImage rejected;
        LoadError e = model::loadTextureFromMemory(ByteView(png.data(), png.size() - 20), options, rejected);
        show("truncated PNG", e);
        require(e.failed() && rejected.mips.empty() && !e.message.empty(), "truncated PNG returns an explicit error and no image");
        auto flipped = png; flipped[png.size() / 2] ^= 0x55;
        e = model::probeTextureDecode(ByteView(flipped), "flipped.png", probe);
        show("corrupted PNG chunk", e);
        require(e.code == LoadCode::Malformed, "corrupted PNG chunk is reported as malformed");
        PngSpec zlibBroken; zlibBroken.badZlibHeader = true;
        e = model::probeTextureDecode(ByteView(makePng(W, H, pixels, zlibBroken)), "zlib.png", probe);
        show("PNG with a valid CRC but a broken zlib stream", e);
        require(e.failed(), "decode-only readiness catches a PNG whose chunk CRCs pass but whose pixels cannot be decoded");
        {
            std::vector<uint8_t> wicOnly = makePng(W, H, pixels, zlibBroken); model::TextureLoadOptions lenient; lenient.maxRes = 16; lenient.verifyPngStream = false; model::TexImage lenientImage;
            const LoadError lenientError = model::loadTextureFromMemory(ByteView(wicOnly), lenient, lenientImage);
            std::cout << "  same PNG with verifyPngStream=false -> " << model::loadCodeName(lenientError.code) << (lenientError.ok() ? ", first pixel byte " + std::to_string(int(lenientImage.mips[0][0])) : std::string()) << " (WIC alone accepts it)\n";
        }
        PngSpec adlerBroken; adlerBroken.badAdler = true;
        e = model::probeTextureDecode(ByteView(makePng(W, H, pixels, adlerBroken)), "adler.png", probe);
        show("PNG with a wrong Adler-32", e);
        require(e.code == LoadCode::Malformed, "a wrong Adler-32 in the PNG pixel stream is rejected");
        PngSpec filterBroken; filterBroken.badFilter = true;
        e = model::probeTextureDecode(ByteView(makePng(W, H, pixels, filterBroken)), "filter.png", probe);
        show("PNG with an invalid scanline filter byte", e);
        require(e.code == LoadCode::Malformed, "an invalid scanline filter byte is rejected");
        PngSpec adam7; adam7.interlaced = true;
        e = model::probeTextureDecode(ByteView(makePng(W, H, pixels, adam7)), "adam7.png", probe);
        require(e.ok() && probe.width == W && probe.height == H, "an Adam7 interlaced PNG passes the pixel-stream verification");
        e = model::probeTextureDecode(ByteView(), "empty.png", probe);
        show("empty span", e);
        require(e.code == LoadCode::Truncated, "empty span is an explicit Truncated error");
        const std::string text = "this is not an image at all, just some text";
        e = model::probeTextureDecode(ByteView(reinterpret_cast<const uint8_t*>(text.data()), text.size()), "text.png", probe);
        show("non-image bytes", e);
        require(e.failed(), "non-image bytes are rejected");
        options.maxRes = 0;
        e = model::loadTextureFromMemory(ByteView(png), options, rejected);
        require(e.code == LoadCode::InvalidArgument, "zero resolution cap is rejected");
        std::string perr;
        require(!model::loadTextureRGBA((work / "missing.png").string(), 16, "", rejected, perr) && !perr.empty(), "path loader reports a missing file");
    }
    {
        using Bytes = std::vector<uint8_t>;
        struct Chunk { std::string name; Bytes data; bool flipCrc = false; };
        auto zlibStored = [](const Bytes& raw) {
            Bytes z{ 0x78, 0x01 };
            size_t pos = 0;
            do {
                const size_t n = std::min<size_t>(65535, raw.size() - pos);
                z.push_back(pos + n == raw.size() ? 1 : 0);
                z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8)); z.push_back(uint8_t(~n)); z.push_back(uint8_t((~n) >> 8));
                z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
                pos += n;
            } while (pos < raw.size());
            uint32_t a = 1, b = 0;
            for (uint8_t v : raw) { a = (a + v) % 65521u; b = (b + a) % 65521u; }
            putBE(z, (b << 16) | a);
            return z;
        };
        auto zlibZeros = [](uint64_t total) {
            Bytes out; uint32_t acc = 0; int used = 0;
            auto bit = [&](uint32_t v) { acc |= (v & 1u) << used; if (++used == 8) { out.push_back(uint8_t(acc)); acc = 0; used = 0; } };
            auto code = [&](uint32_t v, int len) { for (int i = len - 1; i >= 0; --i) bit(v >> i); };
            Bytes z{ 0x78, 0x01 };
            bit(1); bit(1); bit(0);
            uint64_t left = total;
            while (left >= 259) { code(0x30, 8); code(0xC5, 8); code(0, 5); left -= 259; }
            while (left) { code(0x30, 8); --left; }
            code(0, 7);
            if (used) out.push_back(uint8_t(acc));
            z.insert(z.end(), out.begin(), out.end());
            putBE(z, uint32_t((total % 65521u) << 16) | 1u);
            return z;
        };
        auto ihdrOf = [](uint32_t w, uint32_t h, uint8_t depth, uint8_t color, uint8_t interlace = 0) {
            Bytes d; putBE(d, w); putBE(d, h);
            for (uint8_t v : { depth, color, uint8_t(0), uint8_t(0), interlace }) d.push_back(v);
            return d;
        };
        auto build = [](const std::vector<Chunk>& chunks, const Bytes& tail = {}) {
            Bytes png{ 137, 80, 78, 71, 13, 10, 26, 10 };
            for (const auto& c : chunks) {
                putChunk(png, c.name.c_str(), c.data);
                if (c.flipCrc) png[png.size() - 1] ^= 0x01;
            }
            png.insert(png.end(), tail.begin(), tail.end());
            return png;
        };
        auto plainRows = [](uint32_t h, size_t rowBytes) {
            Bytes raw;
            for (uint32_t y = 0; y < h; ++y) { raw.push_back(0); for (size_t i = 0; i < rowBytes; ++i) raw.push_back(uint8_t(i * 37 + y * 11 + 5)); }
            return raw;
        };
        model::TextureProbe probe;
        auto verdict = [&](const Bytes& bytes, const char* label) { return model::probeTextureDecode(ByteView(bytes), label, probe); };
        const Bytes gray8 = plainRows(4, 4);
        const std::vector<Chunk> base{ { "IHDR", ihdrOf(4, 4, 8, 0) }, { "IDAT", zlibStored(gray8) }, { "IEND", {} } };
        require(verdict(build(base), "base").ok(), "a plain 8-bit gray PNG is accepted");
        {
            struct Valid { const char* what; uint32_t w, h; uint8_t depth, color; size_t rowBytes; bool palette; };
            const Valid variants[] = {
                { "1-bit gray", 9, 3, 1, 0, 2, false }, { "2-bit gray", 5, 3, 2, 0, 2, false }, { "4-bit gray", 5, 3, 4, 0, 3, false }, { "16-bit gray", 3, 3, 16, 0, 6, false },
                { "8-bit RGB", 3, 3, 8, 2, 9, false }, { "16-bit RGB", 3, 3, 16, 2, 18, false }, { "8-bit gray with alpha", 3, 3, 8, 4, 6, false },
                { "16-bit RGBA", 3, 3, 16, 6, 24, false }, { "2-bit palette", 5, 3, 2, 3, 2, true }, { "8-bit palette", 4, 3, 8, 3, 4, true } };
            for (const auto& v : variants) {
                std::vector<Chunk> chunks{ { "IHDR", ihdrOf(v.w, v.h, v.depth, v.color) } };
                if (v.palette) chunks.push_back({ "PLTE", Bytes{ 0,0,0, 255,0,0, 0,255,0, 0,0,255 } });
                chunks.push_back({ "IDAT", zlibStored(plainRows(v.h, v.rowBytes)) }); chunks.push_back({ "IEND", {} });
                const auto e = verdict(build(chunks), v.what);
                if (e.failed()) std::cout << "  valid " << v.what << " rejected: " << e.message << '\n';
                require(e.ok() && probe.width == v.w && probe.height == v.h, (std::string("a valid ") + v.what + " PNG is accepted with the declared size").c_str());
            }
        }
        {
            auto split = base; const auto z = zlibStored(gray8); split[1].data = Bytes(z.begin(), z.begin() + 7);
            split.insert(split.begin() + 2, Chunk{ "IDAT", Bytes(z.begin() + 7, z.end()) });
            require(verdict(build(split), "split").ok(), "an image stream split over several consecutive IDAT chunks is accepted");
            auto ancillary = base; ancillary.insert(ancillary.begin() + 1, Chunk{ "prVt", Bytes{ 1, 2, 3 } });
            ancillary.insert(ancillary.begin() + 1, Chunk{ "gAMA", Bytes{ 0, 1, 0x86, 0xA0 } });
            ancillary.insert(ancillary.begin() + 1, Chunk{ "tEXt", Bytes{ 'a', 0, 'b' } });
            require(verdict(build(ancillary), "ancillary").ok(), "unknown private ancillary chunks and valid gAMA and tEXt chunks are tolerated");
        }
        {
            auto badAncillary = base; badAncillary.insert(badAncillary.begin() + 1, Chunk{ "tEXt", Bytes{ 'a', 0, 'b' }, true });
            auto e = verdict(build(badAncillary), "bad-ancillary-crc");
            show("ancillary chunk with a wrong CRC", e);
            require(e.code == LoadCode::Malformed, "a wrong CRC in an ancillary chunk is rejected: an asset must be bit-exact");
            auto badCritical = base; badCritical[0].flipCrc = true;
            require(verdict(build(badCritical), "bad-ihdr-crc").code == LoadCode::Malformed, "a wrong CRC in IHDR is rejected");
            auto badData = base; badData[1].flipCrc = true;
            require(verdict(build(badData), "bad-idat-crc").code == LoadCode::Malformed, "a wrong CRC in IDAT is rejected");
            e = verdict(build(base, Bytes{ 'j', 'u', 'n', 'k' }), "trailing");
            show("bytes after IEND", e);
            require(e.code == LoadCode::Malformed, "bytes after the IEND chunk are rejected");
            auto noEnd = base; noEnd.pop_back();
            require(verdict(build(noEnd), "no-iend").code == LoadCode::Truncated, "a PNG without IEND is rejected as truncated");
            auto noData = base; noData.erase(noData.begin() + 1);
            require(verdict(build(noData), "no-idat").code == LoadCode::Malformed, "a PNG without IDAT is rejected");
            auto twoHeaders = base; twoHeaders.insert(twoHeaders.begin() + 1, twoHeaders[0]);
            require(verdict(build(twoHeaders), "two-ihdr").failed(), "a second IHDR chunk is rejected");
            auto interrupted = base; interrupted.insert(interrupted.begin() + 2, Chunk{ "tEXt", Bytes{ 'a', 0, 'b' } });
            auto parts = zlibStored(gray8); interrupted[1].data = Bytes(parts.begin(), parts.begin() + 7);
            interrupted.insert(interrupted.begin() + 3, Chunk{ "IDAT", Bytes(parts.begin() + 7, parts.end()) });
            require(verdict(build(interrupted), "interrupted-idat").code == LoadCode::Malformed, "IDAT chunks interrupted by another chunk are rejected");
            auto critical = base; critical.insert(critical.begin() + 1, Chunk{ "ABCD", Bytes{ 1 } });
            require(verdict(build(critical), "unknown-critical").code == LoadCode::Malformed, "an unknown critical chunk is rejected");
            require(verdict(build({ { "IHDR", ihdrOf(4, 4, 4, 2) }, { "IDAT", zlibStored(gray8) }, { "IEND", {} } }), "depth-colour").code == LoadCode::Malformed, "a bit depth that is invalid for the color type is rejected");
            require(verdict(build({ { "IHDR", ihdrOf(4, 4, 8, 3) }, { "IDAT", zlibStored(gray8) }, { "IEND", {} } }), "no-palette").code == LoadCode::Malformed, "a palette image without PLTE is rejected");
            require(verdict(build({ { "IHDR", ihdrOf(4, 4, 8, 0) }, { "PLTE", Bytes{ 0, 0, 0 } }, { "IDAT", zlibStored(gray8) }, { "IEND", {} } }), "gray-plte").code == LoadCode::Malformed, "a palette chunk in a gray image is rejected");
            require(verdict(build({ { "IHDR", ihdrOf(4, 4, 8, 0) }, { "tRNS", Bytes{ 0, 0, 0, 0 } }, { "IDAT", zlibStored(gray8) }, { "IEND", {} } }), "trns").code == LoadCode::Malformed, "a transparency chunk of the wrong size for its color type is rejected");
            require(verdict(build({ { "IHDR", ihdrOf(4, 4, 8, 0) }, { "tRNS", Bytes{ 0, 9 } }, { "IDAT", zlibStored(gray8) }, { "IEND", {} } }), "trns-ok").ok(), "a gray image with a valid 2-byte transparency chunk is accepted");
            auto extra = zlibStored(gray8); extra.push_back(0); extra.push_back(0);
            require(verdict(build({ { "IHDR", ihdrOf(4, 4, 8, 0) }, { "IDAT", extra }, { "IEND", {} } }), "extra-zlib").code == LoadCode::Malformed, "bytes after the end of the zlib stream are rejected");
        }
        {
            Bytes short4 = gray8; short4.resize(gray8.size() - 5);
            require(verdict(build({ { "IHDR", ihdrOf(4, 4, 8, 0) }, { "IDAT", zlibStored(short4) }, { "IEND", {} } }), "short").code == LoadCode::Truncated, "pixel data one row short is rejected");
            Bytes longer = gray8; longer.insert(longer.end(), 5, 0);
            require(verdict(build({ { "IHDR", ihdrOf(4, 4, 8, 0) }, { "IDAT", zlibStored(longer) }, { "IEND", {} } }), "long").failed(), "pixel data longer than the header needs is rejected");
            require(verdict(build({ { "IHDR", ihdrOf(4, 4, 8, 0) }, { "IDAT", Bytes{ 0x78, 0x01, 0x01, 0x14, 0x00, 0xEB, 0xFF } }, { "IEND", {} } }), "early-end").failed(), "a zlib stream cut inside a stored block is rejected");
        }
        {
            const auto big = build({ { "IHDR", ihdrOf(2048, 2048, 8, 0) }, { "IDAT", zlibZeros(2048ull * 2049ull) }, { "IEND", {} } });
            auto e = verdict(big, "big-zero");
            std::cout << "  2048x2048 zero PNG: " << big.size() << " bytes stored for " << 2048ull * 2049ull << " inflated -> " << model::loadCodeName(e.code) << '\n';
            require(e.ok() && probe.width == 2048 && probe.height == 2048, "a highly compressed but valid large PNG is fully inflated, checksummed and accepted within the cap");
            const auto expanding = build({ { "IHDR", ihdrOf(2, 2, 8, 6) }, { "IDAT", zlibZeros(1 + 258ull * 4000ull) }, { "IEND", {} } });
            e = verdict(expanding, "expanding");
            show("tiny image whose zlib stream expands to a megabyte", e);
            require(e.failed() && e.code != LoadCode::OutOfMemory, "a stream that inflates far beyond what the header declares is rejected at the declared size");
            const auto hugeDeclared = build({ { "IHDR", ihdrOf(16000, 16000, 8, 6) }, { "IDAT", zlibZeros(1 + 258ull * 10ull) }, { "IEND", {} } });
            const auto before = std::chrono::steady_clock::now();
            e = verdict(hugeDeclared, "huge-declared");
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - before).count();
            show("16000x16000 RGBA header over a tiny stream", e);
            std::cout << "  rejected in " << ms << " ms\n";
            require(e.code == LoadCode::Truncated && e.message.find("cannot hold") != std::string::npos, "a header declaring about 1 GB of pixels over a tiny stream is rejected by the size check, before any buffer is allocated");
            require(verdict(build({ { "IHDR", ihdrOf(16384, 16384, 8, 6) }, { "IDAT", zlibZeros(1000) }, { "IEND", {} } }), "over-cap").code == LoadCode::TooLarge, "a header whose pixel stream exceeds 1 GiB is rejected as too large");
            require(verdict(build({ { "IHDR", ihdrOf(70000, 70000, 8, 0) }, { "IDAT", zlibZeros(1000) }, { "IEND", {} } }), "over-side").code == LoadCode::TooLarge, "a header with a side above 16384 is rejected as too large");
            require(verdict(build({ { "IHDR", ihdrOf(0, 4, 8, 0) }, { "IDAT", zlibZeros(5) }, { "IEND", {} } }), "zero-side").failed(), "a zero-sized image is rejected");
        }
        {
            const auto tiny = makePng(8, 6, gradient(8, 6));
            size_t accepted = 0;
            std::string survivors;
            for (size_t i = 0; i < tiny.size(); ++i) {
                auto flipped = tiny; flipped[i] ^= 0x01;
                if (model::probeTextureDecode(ByteView(flipped), "flip", probe).ok()) { ++accepted; survivors += " " + std::to_string(i); }
            }
            std::cout << "  single-bit damage sweep over " << tiny.size() << " bytes of a PNG: " << accepted << " accepted" << survivors << '\n';
            require(accepted == 0, "every single-bit flip anywhere in a PNG is detected");
        }
    }
    {
        const fs::path repoPng[] = { root / "editor/checks/corner-controls/left-controls.png", root / "editor/checks/corner-controls/top-edge.png", root / "editor/checks/bottom-bar/shapes-bar.png", root / "editor/checks/feature-panel.png", root / "editor/checks/editor-preview.png" };
        for (const auto& p : repoPng) {
            const auto bytes = readBytes(p);
            if (bytes.empty()) { std::cout << "  NOT RUN, repo fixture missing: " << p.string() << '\n'; require(false, "repo PNG fixture present"); }
            model::TexImage a, b; std::string err; model::TextureLoadOptions options; options.maxRes = 64; options.label = p.filename().string();
            const bool memoryOk = model::loadTextureFromMemory(ByteView(bytes), options, a).ok();
            const bool pathOk = model::loadTextureRGBA(p.string(), 64, "", b, err);
            require(memoryOk && pathOk && sameImage(a, b) && !a.mips.empty(), "memory and path decode agree on a repo PNG fixture");
            model::TextureProbe probe;
            require(model::probeTextureDecode(ByteView(bytes), p.filename().string(), probe).ok() && probe.width > 0 && probe.height > 0, "decode-only readiness accepts a repo PNG fixture");
        }
        const fs::path jpg = root / "migration/classic-advanced.jpg";
        const auto bytes = readBytes(jpg);
        if (bytes.empty()) { std::cout << "  NOT RUN, repo fixture missing: " << jpg.string() << '\n'; require(false, "repo JPEG fixture present"); }
        model::TexImage a, b; std::string err; model::TextureLoadOptions options; options.maxRes = 256; options.label = "classic-advanced.jpg";
        require(model::loadTextureFromMemory(ByteView(bytes), options, a).ok() && model::loadTextureRGBA(jpg.string(), 256, "", b, err) && sameImage(a, b) && a.w <= 256 && a.h <= 256, "memory and path decode agree on a repo JPEG fixture");
        model::TextureProbe probe;
        require(model::probeTextureDecode(ByteView(bytes), "classic-advanced.jpg", probe).ok() && probe.container == "jpeg", "decode-only readiness identifies a JPEG");
        std::cout << "  JPEG fixture decoded to " << probe.width << "x" << probe.height << ", mip chain " << probe.mipLevels << "\n";
        LoadError e = model::probeTextureDecode(ByteView(bytes.data(), bytes.size() / 2), "half.jpg", probe);
        show("truncated JPEG", e);
        require(e.failed(), "truncated JPEG is rejected");
    }
    {
        const auto png = makePng(8, 6, gradient(8, 6));
        const fs::path cache = work / "mipcache";
        fs::remove_all(cache);
        model::resetTextureCacheStats();
        model::TextureLoadOptions options; options.maxRes = 16; options.label = "cached.png"; options.cacheDir = cache.string(); options.contentId = "content-id-1";
        const auto entryOf = [&](const std::string& id, uint32_t maxRes) { return cache / (model::textureCacheKey(id, maxRes, model::TextureRole::Srgb, model::TextureFilter::Box2x2) + ".mip3"); };
        const uintmax_t entryBytes = 192 + (8 * 6 + 4 * 3 + 2 * 1 + 1) * 4;
        model::TexImage first, second, other;
        require(model::loadTextureFromMemory(ByteView(png), options, first).ok() && !first.fromCache && first.cacheNote.empty(), "first load decodes and writes the derived cache");
        require(fs::exists(entryOf("content-id-1", 16)) && fs::file_size(entryOf("content-id-1", 16)) == entryBytes, "the cache entry is named by the key of content id and settings and has the exact expected size");
        require(model::loadTextureFromMemory(ByteView(png), options, second).ok() && second.fromCache && sameImage(first, second), "second load is served from the cache with identical mips");
        options.maxRes = 4;
        require(model::loadTextureFromMemory(ByteView(png), options, other).ok() && !other.fromCache && other.w == 4, "a different resolution cap uses a different cache entry");
        options.maxRes = 16; options.contentId.clear();
        model::TexImage hashed, hashedAgain;
        require(model::loadTextureFromMemory(ByteView(png), options, hashed).ok() && !hashed.fromCache && model::loadTextureFromMemory(ByteView(png), options, hashedAgain).ok() && hashedAgain.fromCache && sameImage(hashed, hashedAgain), "without a content id the SHA-256 of the bytes keys the cache");
        require(fs::exists(entryOf(model::contentHashHex(ByteView(png)), 16)), "hash-keyed cache entry carries the digest in its key");
        writeBytes(work / "name-a.png", png);
        writeBytes(work / "name-b.png", png);
        fs::last_write_time(work / "name-b.png", fs::last_write_time(work / "name-b.png") + std::chrono::hours(5));
        model::TexImage byPathA, byPathB, changed; std::string err;
        require(model::loadTextureRGBA((work / "name-a.png").string(), 16, cache.string(), byPathA, err) && byPathA.fromCache, "path loader hits the cache entry created from memory");
        require(model::loadTextureRGBA((work / "name-b.png").string(), 16, cache.string(), byPathB, err) && byPathB.fromCache && sameImage(byPathA, byPathB), "the cache ignores file name and modification time");
        auto altered = gradient(8, 6); altered[0] ^= 0x40;
        writeBytes(work / "name-a.png", makePng(8, 6, altered));
        require(model::loadTextureRGBA((work / "name-a.png").string(), 16, cache.string(), changed, err) && !changed.fromCache && changed.mips[0] == altered, "changed content under the same name misses the cache");
        options.contentId = "content-id-1";
        {
            std::ofstream damage(entryOf("content-id-1", 16), std::ios::binary | std::ios::app);
            damage.write("x", 1);
        }
        const auto beforeDamage = model::textureCacheStats();
        model::TexImage afterDamage;
        require(model::loadTextureFromMemory(ByteView(png), options, afterDamage).ok() && !afterDamage.fromCache && afterDamage.mips == first.mips, "a damaged cache file is ignored and the texture is decoded again");
        require(afterDamage.cacheNote.find("failed verification") != std::string::npos && model::textureCacheStats().corrupt == beforeDamage.corrupt + 1, "the damaged entry is reported in cacheNote and counted");
        require(fs::file_size(entryOf("content-id-1", 16)) == entryBytes, "the damaged cache file was rewritten");
        writeBytes(work / "blocker", { 1, 2, 3 });
        options.cacheDir = (work / "blocker" / "sub").string();
        model::TexImage uncached;
        const auto beforeBlocked = model::textureCacheStats();
        require(model::loadTextureFromMemory(ByteView(png), options, uncached).ok() && !uncached.cacheNote.empty() && uncached.mips == first.mips, "an unwritable cache directory is reported in cacheNote and the load still succeeds");
        require(model::textureCacheStats().writeFailures == beforeBlocked.writeFailures + 1, "the failed cache write is counted");
        std::cout << "  cache note: " << uncached.cacheNote << '\n';
        options.cacheDir = cache.string(); options.contentId = "../escape";
        require(model::loadTextureFromMemory(ByteView(png), options, uncached).code == LoadCode::InvalidArgument, "a content id that is not a plain token is rejected");
    }
    {
        const auto png = makePng(8, 6, gradient(8, 6));
        const fs::path cache = work / "mipcorrupt";
        model::TextureLoadOptions options; options.maxRes = 16; options.label = "corrupt.png"; options.cacheDir = cache.string(); options.contentId = "corrupt-1";
        const fs::path entry = cache / (model::textureCacheKey("corrupt-1", 16, model::TextureRole::Srgb, model::TextureFilter::Box2x2) + ".mip3");
        model::TexImage reference;
        require(model::loadTextureFromMemory(ByteView(png), options, reference).ok() && fs::exists(entry), "a reference load writes the entry used for the corruption cases");
        const auto good = readBytes(entry);
        auto fixCrc = [](std::vector<uint8_t>& b) { b[60] = b[61] = b[62] = b[63] = 0; const uint32_t c = crc32(b.data(), 192); std::memcpy(&b[60], &c, 4); };
        struct Case { const char* name; std::function<void(std::vector<uint8_t>&)> damage; };
        const std::vector<Case> cases = {
            { "one flipped payload byte", [](std::vector<uint8_t>& b) { b[200] ^= 1; } },
            { "flipped last byte", [](std::vector<uint8_t>& b) { b.back() ^= 0x80; } },
            { "flipped key byte", [](std::vector<uint8_t>& b) { b[70] ^= 1; } },
            { "flipped magic", [](std::vector<uint8_t>& b) { b[0] ^= 1; } },
            { "one byte missing", [](std::vector<uint8_t>& b) { b.pop_back(); } },
            { "cut inside the header", [](std::vector<uint8_t>& b) { b.resize(100); } },
            { "empty file", [](std::vector<uint8_t>& b) { b.clear(); } },
            { "entry not written after verification (valid checksum)", [&](std::vector<uint8_t>& b) { b[56] = 0; fixCrc(b); } },
            { "entry for other settings (valid checksum)", [&](std::vector<uint8_t>& b) { b[24] = 99; fixCrc(b); } },
            { "entry from a source of another size (valid checksum)", [&](std::vector<uint8_t>& b) { b[40] ^= 1; fixCrc(b); } },
            { "lying width (valid checksum)", [&](std::vector<uint8_t>& b) { b[28] = 4; fixCrc(b); } },
            { "lying payload length (valid checksum)", [&](std::vector<uint8_t>& b) { b[48] += 4; fixCrc(b); } },
        };
        for (const Case& c : cases) {
            auto bytes = good;
            c.damage(bytes);
            writeBytes(entry, bytes);
            const auto before = model::textureCacheStats();
            model::TexImage got;
            const bool ok = model::loadTextureFromMemory(ByteView(png), options, got).ok();
            const auto after = model::textureCacheStats();
            require(ok && !got.fromCache && got.mips == reference.mips && got.cacheNote.find("failed verification") != std::string::npos && after.corrupt == before.corrupt + 1 && after.hits == before.hits && readBytes(entry) == good,
                    (std::string("corrupt cache entry is ignored, counted and rebuilt: ") + c.name).c_str());
        }
        const fs::path foreign = cache / (model::textureCacheKey("corrupt-2", 16, model::TextureRole::Srgb, model::TextureFilter::Box2x2) + ".mip3");
        writeBytes(foreign, good);
        options.contentId = "corrupt-2";
        const auto before = model::textureCacheStats();
        model::TexImage got;
        require(model::loadTextureFromMemory(ByteView(png), options, got).ok() && !got.fromCache && model::textureCacheStats().corrupt == before.corrupt + 1 && readBytes(foreign) != good, "an entry copied under another key is never trusted");
        std::cout << "  rejection note: " << got.cacheNote << '\n';
        options.contentId = "corrupt-1";
        const auto bigPng = makePng(64, 64, gradient(64, 64));
        options.maxRes = 64; options.contentId = "bounded-1";
        model::TexImage bigFirst, bigTight;
        require(model::loadTextureFromMemory(ByteView(bigPng), options, bigFirst).ok() && bigFirst.w == 64, "a larger entry is written for the memory-bound check");
        starlight::texture_limits::setDecodedBudget(4096);
        const auto beforeBound = model::textureCacheStats();
        const LoadError tight = model::loadTextureFromMemory(ByteView(bigPng), options, bigTight);
        starlight::texture_limits::setDecodedBudget(0);
        show("64x64 under a 4096 byte decoded-memory budget", tight);
        require(tight.code == LoadCode::TooLarge && tight.message.find("budget") != std::string::npos && tight.message.find("never downsamples") != std::string::npos && bigTight.mips.empty(), "an image above the decoded-memory budget is refused with an explanation and is not downsampled");
        require(model::textureCacheStats().corrupt == beforeBound.corrupt + 1, "a cache entry larger than the budget allows is not read into memory and is counted as rejected");
        require(starlight::texture_limits::decodedBudget() == starlight::texture_limits::DefaultDecodedBudgetBytes, "the budget setter restores the default with zero");
    }
    {
        const auto png = makePng(8, 6, gradient(8, 6));
        std::vector<std::string> keys;
        using Rl = model::TextureRole; using F = model::TextureFilter;
        keys.push_back(model::textureCacheKey("id-a", 16, Rl::Srgb, F::Box2x2));
        keys.push_back(model::textureCacheKey("id-b", 16, Rl::Srgb, F::Box2x2));
        keys.push_back(model::textureCacheKey("id-a", 8, Rl::Srgb, F::Box2x2));
        keys.push_back(model::textureCacheKey("id-a", 16, Rl::Linear, F::Box2x2));
        keys.push_back(model::textureCacheKey("id-a", 16, Rl::Srgb, F(1)));
        keys.push_back(model::textureCacheKey("id-a", 16, Rl::Srgb, F::Box2x2, model::TextureCacheSchemaVersion + 1));
        keys.push_back(model::textureCacheKey("id-a", 16, Rl::Srgb, F::Box2x2, model::TextureCacheSchemaVersion, model::TextureDecoderVersion + 1));
        std::set<std::string> distinct(keys.begin(), keys.end());
        bool hex = true;
        for (const auto& k : keys) { if (k.size() != 64) hex = false; for (char c : k) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) hex = false; }
        require(distinct.size() == keys.size() && hex, "changing the content id, the resolution cap, the colour role, the filter, the schema version or the decoder version changes the cache key");
        require(model::textureCacheKey("id-a", 16, Rl::Srgb, F::Box2x2) == keys[0], "the key is deterministic");
        const fs::path cache = work / "mipkeys";
        model::TextureLoadOptions options; options.maxRes = 16; options.label = "keys.png"; options.cacheDir = cache.string(); options.contentId = "id-a";
        model::TexImage a, b, c, d;
        require(model::loadTextureFromMemory(ByteView(png), options, a).ok() && !a.fromCache, "key check: first load misses");
        options.role = Rl::Linear;
        require(model::loadTextureFromMemory(ByteView(png), options, b).ok() && !b.fromCache, "key check: another colour role does not reuse the entry");
        require(model::loadTextureFromMemory(ByteView(png), options, c).ok() && c.fromCache, "key check: the new role has its own entry");
        options.role = Rl::Srgb;
        require(model::loadTextureFromMemory(ByteView(png), options, d).ok() && d.fromCache, "key check: the first role still hits its own entry");
        size_t files = 0;
        for (const auto& item : fs::directory_iterator(cache)) if (item.path().extension() == ".mip3") ++files;
        require(files == 2, "two roles produced exactly two entries");
    }
    {
        const auto png = makePng(8, 6, gradient(8, 6));
        const fs::path cache = work / "mipproof";
        fs::create_directories(cache);
        writeBytes(cache / "legacy.16.mip", { 'I', 'L', 'M', 'I', 'P', '0', '0', '2', 1, 2, 3 });
        writeBytes(cache / "notes.txt", { 'k' });
        model::TextureLoadOptions options; options.maxRes = 16; options.label = "proof.png"; options.cacheDir = cache.string(); options.contentId = "proof-1";
        model::TexImage img;
        require(model::loadTextureFromMemory(ByteView(png), options, img).ok() && !img.fromCache, "proof: the first load decodes and verifies the source and writes the entry");
        const auto brokenAdler = makePng(8, 6, gradient(8, 6), PngSpec{ false, true, false, false });
        require(brokenAdler.size() == png.size(), "proof: the damaged fixture has the same byte size as the good one");
        model::TextureProbe probe;
        const auto decodesBefore = model::textureDecodeStats().decodes;
        const auto statsBefore = model::textureCacheStats();
        const LoadError probed = model::probeTextureDecode(ByteView(brokenAdler), "broken.png", probe);
        require(probed.failed() && model::textureDecodeStats().decodes == decodesBefore + 1, "proof: the readiness probe decodes and fails on the damaged source");
        require(model::probeTextureDecode(ByteView(png), "good.png", probe).ok() && model::textureDecodeStats().decodes == decodesBefore + 2, "proof: the readiness probe decodes the good source every time");
        const auto statsAfter = model::textureCacheStats();
        require(statsAfter.hits == statsBefore.hits && statsAfter.misses == statsBefore.misses && statsAfter.written == statsBefore.written, "proof: the readiness probe never reads or writes the cache");
        model::TexImage trusted;
        const LoadError trustedError = model::loadTextureFromMemory(ByteView(brokenAdler), options, trusted);
        std::cout << "  same-size damaged bytes presented under the verified content id -> " << model::loadCodeName(trustedError.code) << (trustedError.ok() ? ", served from the cache" : "") << "\n";
        require(trustedError.ok() && trusted.fromCache, "proof: a load trusts the content id for entries that were verified once (the id is the content hash; a caller that passes another id is outside the contract)");
        options.forceDecode = true;
        model::TexImage forced;
        const auto forcedBefore = model::textureCacheStats();
        const LoadError forcedError = model::loadTextureFromMemory(ByteView(brokenAdler), options, forced);
        require(forcedError.failed() && forced.mips.empty() && model::textureCacheStats().forcedDecodes == forcedBefore.forcedDecodes + 1, "proof: forceDecode never accepts a cache hit, so a decode proof can be demanded from a load");
        model::TexImage forcedGood;
        require(model::loadTextureFromMemory(ByteView(png), options, forcedGood).ok() && !forcedGood.fromCache && sameImage(forcedGood, img), "proof: forceDecode of the good source refreshes the entry with identical mips");
        options.forceDecode = false;
        options.verifyPngStream = false; options.contentId = "proof-unverified";
        const auto unverifiedBefore = model::textureCacheStats();
        model::TexImage lenient;
        require(model::loadTextureFromMemory(ByteView(png), options, lenient).ok() && !lenient.cacheNote.empty(), "proof: a load that skipped PNG verification says so in cacheNote");
        require(model::textureCacheStats().unverifiedSkipped == unverifiedBefore.unverifiedSkipped + 1 && !fs::exists(cache / (model::textureCacheKey("proof-unverified", 16, model::TextureRole::Srgb, model::TextureFilter::Box2x2) + ".mip3")), "proof: a load that skipped verification writes no entry, so an unverified source can never become a trusted entry");
        require(fs::exists(cache / "legacy.16.mip") && fs::exists(cache / "notes.txt"), "old ILMIP002 files and unrelated files in the cache folder are ignored and left in place");
    }
    {
        const auto png = makePng(8, 6, gradient(8, 6));
        const fs::path cache = work / "mipevict";
        fs::create_directories(cache);
        writeBytes(cache / "legacy.16.mip", { 'I', 'L', 'M', 'I', 'P', '0', '0', '2' });
        writeBytes(cache / "keep.txt", { 'k' });
        const uintmax_t entryBytes = 192 + (8 * 6 + 4 * 3 + 2 * 1 + 1) * 4;
        const auto savedLimit = model::textureCacheLimitBytes();
        model::setTextureCacheLimitBytes(entryBytes * 3 + 10);
        model::resetTextureCacheStats();
        model::TextureLoadOptions options; options.maxRes = 16; options.label = "evict.png"; options.cacheDir = cache.string();
        for (int i = 0; i < 6; ++i) {
            options.contentId = "evict-" + std::to_string(i);
            model::TexImage img;
            require(model::loadTextureFromMemory(ByteView(png), options, img).ok(), "eviction: load succeeds");
        }
        size_t files = 0; uintmax_t bytes = 0;
        for (const auto& item : fs::directory_iterator(cache))
            if (item.path().extension() == ".mip3") { ++files; bytes += item.file_size(); }
        const auto stats = model::textureCacheStats();
        std::cout << "  eviction: " << files << " entries, " << bytes << " bytes kept of 6 written, " << stats.evicted << " evicted, limit " << model::textureCacheLimitBytes() << "\n";
        require(files <= 3 && bytes <= model::textureCacheLimitBytes() && stats.evicted == 6 - files && stats.written == 6, "eviction keeps the cache folder within its byte limit and counts what it removed");
        require(fs::exists(cache / (model::textureCacheKey("evict-5", 16, model::TextureRole::Srgb, model::TextureFilter::Box2x2) + ".mip3")), "eviction never removes the entry that was just written");
        require(fs::exists(cache / "legacy.16.mip") && fs::exists(cache / "keep.txt"), "eviction touches only version 3 entries, never old ILMIP002 files or other files");
        model::setTextureCacheLimitBytes(entryBytes / 2);
        options.contentId = "evict-oversize";
        model::TexImage lone;
        require(model::loadTextureFromMemory(ByteView(png), options, lone).ok() && fs::exists(cache / (model::textureCacheKey("evict-oversize", 16, model::TextureRole::Srgb, model::TextureFilter::Box2x2) + ".mip3")), "a single entry larger than the limit is still kept (the limit never blocks the entry just built)");
        model::setTextureCacheLimitBytes(savedLimit);
    }
    {
        require(model::defaultTextureWorkerLimit(0) == 1 && model::defaultTextureWorkerLimit(1) == 1 && model::defaultTextureWorkerLimit(2) == 1 && model::defaultTextureWorkerLimit(4) == 2 &&
                model::defaultTextureWorkerLimit(8) == 4 && model::defaultTextureWorkerLimit(16) == 4 && model::defaultTextureWorkerLimit(128) == 4, "the default decode worker count is max(1, min(4, hardware threads / 2))");
        std::vector<std::vector<uint8_t>> pngs;
        for (uint32_t i = 0; i < 12; ++i) { auto pixels = gradient(256, 256); pixels[0] = uint8_t(i); pngs.push_back(makePng(256, 256, pixels)); }
        pngs.push_back(makePng(8, 6, gradient(8, 6), PngSpec{ false, true, false, false }));
        std::vector<model::TexturePrepareJob> jobs;
        for (size_t i = 0; i < pngs.size(); ++i) {
            model::TexturePrepareJob job; job.encoded = ByteView(pngs[i]); job.options.maxRes = 256; job.options.label = "pool-" + std::to_string(i);
            jobs.push_back(job);
        }
        for (uint32_t limit : { 1u, 2u, 3u }) {
            model::setTextureWorkerLimit(limit);
            require(model::textureWorkerLimit() == limit, "the decode worker limit is configurable");
            model::resetTextureDecodeStats();
            std::vector<model::TexImage> images;
            const auto errors = model::prepareTextures(jobs, &images, 8);
            size_t good = 0;
            for (size_t i = 0; i < errors.size(); ++i) if (errors[i].ok() && images[i].w == 256) ++good;
            const auto stats = model::textureDecodeStats();
            std::cout << "  worker limit " << limit << ": " << good << " of " << jobs.size() << " decoded, peak concurrent decodes " << stats.peakConcurrent << ", " << stats.decodes << " decodes, " << stats.waited << " had to wait\n";
            require(good == 12 && errors[12].failed() && images[12].mips.empty() && stats.decodes == jobs.size(), "the pool decodes every job and reports the damaged one by index");
            require(stats.peakConcurrent >= 1 && stats.peakConcurrent <= limit && stats.active == 0, "concurrent decodes never exceed the worker limit even when more workers are requested");
        }
        model::setTextureWorkerLimit(2);
        model::resetTextureDecodeStats();
        {
            std::vector<std::thread> callers;
            std::atomic<int> okCount{ 0 };
            for (size_t i = 0; i < 12; ++i)
                callers.emplace_back([&, i] {
                    model::TextureLoadOptions options; options.maxRes = 256; options.label = "caller-" + std::to_string(i);
                    model::TexImage img;
                    if (model::loadTextureFromMemory(ByteView(pngs[i]), options, img).ok()) ++okCount;
                });
            for (auto& t : callers) t.join();
            const auto stats = model::textureDecodeStats();
            std::cout << "  12 unmanaged caller threads, limit 2: peak " << stats.peakConcurrent << ", waited " << stats.waited << "\n";
            require(okCount == 12 && stats.peakConcurrent <= 2, "even twelve independent caller threads never run more decodes than the limit");
        }
        model::setTextureWorkerLimit(0);
        require(model::textureWorkerLimit() >= 1 && model::textureWorkerLimit() <= 64, "resetting the worker limit restores the default");
        const fs::path cache = work / "mipwarm";
        std::vector<model::TexturePrepareJob> warm;
        for (size_t i = 0; i < 4; ++i) {
            model::TexturePrepareJob job; job.encoded = ByteView(pngs[i]); job.options.maxRes = 256; job.options.cacheDir = cache.string(); job.options.contentId = "warm-" + std::to_string(i); job.options.label = "warm";
            warm.push_back(job);
        }
        model::resetTextureCacheStats();
        const auto cold = model::prepareTextures(warm, nullptr, 2);
        const auto warmStats = model::textureCacheStats();
        std::vector<model::TexImage> again;
        const auto hot = model::prepareTextures(warm, &again, 2);
        const auto hotStats = model::textureCacheStats();
        bool allOk = true, allHit = true;
        for (size_t i = 0; i < 4; ++i) { allOk = allOk && cold[i].ok() && hot[i].ok(); allHit = allHit && again[i].fromCache; }
        require(allOk && warmStats.written == 4 && warmStats.hits == 0 && allHit && hotStats.hits == 4, "preparing textures without keeping the results fills the cache, and a second pass is served entirely from it");
    }
    {
        namespace tl = starlight::texture_limits;
        require(tl::MaxSide == 16384 && tl::DefaultDecodedBudgetBytes == (1ull << 30) && tl::MaxPngPixelStreamBytes == (1ull << 30), "the central limits are 16384 per side and 1 GiB");
        require(tl::checkImage(16384, 16384, 4, 1, 1ull << 30).ok() && tl::checkImage(16384, 16384, 4, 1, 1ull << 30).bytes == (1ull << 30), "a 16384 square RGBA8 image fits the default budget exactly");
        require(tl::checkImage(16385, 1, 4, 1, UINT64_MAX).verdict == tl::Verdict::SideTooLarge && tl::checkImage(1, 16385, 4, 1, UINT64_MAX).verdict == tl::Verdict::SideTooLarge, "a side of 16385 is over the limit");
        require(tl::checkImage(0, 4, 4, 1, UINT64_MAX).verdict == tl::Verdict::EmptySide && tl::checkImage(4, 0, 4, 1, UINT64_MAX).verdict == tl::Verdict::EmptySide, "a zero side is refused");
        require(tl::checkImage(4, 4, 0, 1, UINT64_MAX).verdict == tl::Verdict::BadChannels && tl::checkImage(4, 4, 5, 1, UINT64_MAX).verdict == tl::Verdict::BadChannels && tl::checkImage(4, 4, 4, 3, UINT64_MAX).verdict == tl::Verdict::BadChannels && tl::checkImage(4, 4, 4, 0, UINT64_MAX).verdict == tl::Verdict::BadChannels, "channel counts outside 1 to 4 and sample sizes other than 1 or 2 bytes are refused");
        require(tl::checkImage(16384, 16384, 4, 2, 1ull << 30).verdict == tl::Verdict::OverBudget && tl::checkImage(16384, 16384, 4, 2, UINT64_MAX).bytes == (2ull << 30), "a 16 bit RGBA image of the maximum size is over the default budget and its size is computed exactly");
        uint64_t out = 0;
        require(!tl::multiplyChecked(UINT64_MAX, 2, out) && tl::multiplyChecked(1ull << 31, 1ull << 31, out) && out == (1ull << 62) && !tl::addChecked(UINT64_MAX, 1, out) && tl::addChecked(1, 2, out) && out == 3, "the checked arithmetic detects 64 bit overflow");
        require(tl::mipChainBytes(16384, 16384, 4) == 1431655764ull && tl::mipChainBytes(1, 1, 4) == 4 && tl::mipChainBytes(8, 6, 4) == (8 * 6 + 4 * 3 + 2 + 1) * 4, "the mip chain size is exact");
        const std::string side = tl::explain(tl::checkImage(20000, 1, 4, 1), 20000, 1, 4, 1);
        const std::string budget = tl::explain(tl::checkImage(16384, 16384, 4, 2, 1ull << 30), 16384, 16384, 4, 2, 1ull << 30);
        std::cout << "  oversize text: " << side << "\n  budget text: " << budget << "\n";
        require(side.find("20000x1") != std::string::npos && side.find("16384") != std::string::npos && side.find("never downsamples") != std::string::npos && side.find("graphics device") != std::string::npos, "the oversize text names the size, the limit, the no-silent-downsample rule and the later device check");
        require(budget.find("budget") != std::string::npos && budget.find("2147483648") != std::string::npos && budget.find("never downsamples") != std::string::npos, "the budget text names the bytes needed and the budget");
        const auto narrow = makePng(8, 6, gradient(8, 6));
        tl::setDecodedBudget(100);
        model::TexImage img; model::TextureLoadOptions options; options.maxRes = 16; options.label = "budget.png";
        const LoadError overBudget = model::loadTextureFromMemory(ByteView(narrow), options, img);
        model::TextureProbe probe;
        const LoadError probeOver = model::probeTextureDecode(ByteView(narrow), "budget.png", probe);
        tl::setDecodedBudget(0);
        show("8x6 under a 100 byte budget", overBudget);
        require(overBudget.code == LoadCode::TooLarge && probeOver.code == LoadCode::TooLarge && img.mips.empty(), "both the loader and the readiness probe enforce the decoded-memory budget");
    }
    {
        (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        struct Variant { const char* name; GUID format; uint32_t bytes; bool interlace; };
        const std::vector<Variant> variants = {
            { "8-bit gray", GUID_WICPixelFormat8bppGray, 1, false },
            { "24-bit BGR", GUID_WICPixelFormat24bppBGR, 3, false },
            { "32-bit BGRA", GUID_WICPixelFormat32bppBGRA, 4, false },
            { "16-bit gray", GUID_WICPixelFormat16bppGray, 2, false },
            { "64-bit RGBA", GUID_WICPixelFormat64bppRGBA, 8, false },
            { "32-bit BGRA interlaced", GUID_WICPixelFormat32bppBGRA, 4, true },
            { "24-bit BGR interlaced", GUID_WICPixelFormat24bppBGR, 3, true },
        };
        size_t produced = 0;
        for (const Variant& v : variants) {
            const uint32_t W = 37, H = 21;
            ComPtr<IWICImagingFactory> factory;
            ComPtr<IStream> stream;
            ComPtr<IWICBitmapEncoder> encoder;
            ComPtr<IWICBitmapFrameEncode> frame;
            ComPtr<IPropertyBag2> bag;
            if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
                FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) || FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(encoder->CreateNewFrame(&frame, &bag))) continue;
            if (v.interlace) {
                PROPBAG2 option{}; option.pstrName = const_cast<LPOLESTR>(L"InterlaceOption");
                VARIANT value{}; value.vt = VT_BOOL; value.boolVal = VARIANT_TRUE;
                if (FAILED(bag->Write(1, &option, &value))) continue;
            }
            WICPixelFormatGUID format = v.format;
            std::vector<BYTE> pixels(size_t(W) * H * v.bytes);
            for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = BYTE(i * 7 + i / W);
            if (FAILED(frame->Initialize(bag.Get())) || FAILED(frame->SetSize(W, H)) || FAILED(frame->SetPixelFormat(&format)) || format != v.format ||
                FAILED(frame->WritePixels(H, W * v.bytes, UINT(pixels.size()), pixels.data())) || FAILED(frame->Commit()) || FAILED(encoder->Commit())) continue;
            STATSTG statistics{};
            stream->Stat(&statistics, STATFLAG_NONAME);
            LARGE_INTEGER start{};
            stream->Seek(start, STREAM_SEEK_SET, nullptr);
            std::vector<uint8_t> encoded(size_t(statistics.cbSize.QuadPart));
            ULONG read = 0;
            stream->Read(encoded.data(), ULONG(encoded.size()), &read);
            encoded.resize(read);
            ++produced;
            model::TextureProbe probe; model::TexImage img; model::TextureLoadOptions options; options.maxRes = 64; options.label = v.name;
            const LoadError probed = model::probeTextureDecode(ByteView(encoded), v.name, probe);
            const LoadError loaded = model::loadTextureFromMemory(ByteView(encoded), options, img);
            show(v.name, probed);
            require(probed.ok() && loaded.ok() && probe.width == W && probe.height == H && img.w == W, (std::string("re-encode check: a PNG written by the Windows encoder (") + v.name + ") passes the strict runtime loader").c_str());
        }
        require(produced >= 5, "re-encode check: at least five encoder variants were produced");
    }
    {
        const fs::path rootA = fs::absolute(work / "dataroot");
        wchar_t previous[32768]{};
        const DWORD previousLength = GetEnvironmentVariableW(L"STARLIGHT_DATA_ROOT", previous, 32768);
        SetEnvironmentVariableW(L"STARLIGHT_DATA_ROOT", rootA.c_str());
        const std::string resolved = model::textureCacheDirectory();
        const fs::path expected = rootA / "AssetCache" / "textures";
        require(!resolved.empty() && fs::path(resolved).lexically_normal() == expected.lexically_normal(), "the shared texture cache lives in the data root's AssetCache folder (the engine's derived-data folder)");
        require(std::string(starlight::texture_limits::CacheRootFolder) == "AssetCache" && std::string(starlight::texture_limits::TextureCacheFolder) == "textures", "the cache folder names come from the central header");
        const auto png = makePng(8, 6, gradient(8, 6));
        model::TextureLoadOptions options; options.maxRes = 16; options.label = "shared.png"; options.contentId = "shared-1"; options.sharedCache = true;
        model::TexImage a, b;
        require(model::loadTextureFromMemory(ByteView(png), options, a).ok() && !a.fromCache && model::loadTextureFromMemory(ByteView(png), options, b).ok() && b.fromCache, "sharedCache selects the engine-managed location without the host naming a folder");
        require(fs::exists(expected / (model::textureCacheKey("shared-1", 16, model::TextureRole::Srgb, model::TextureFilter::Box2x2) + ".mip3")), "the shared entry is in the resolved folder");
        SetEnvironmentVariableW(L"STARLIGHT_DATA_ROOT", L"relative\\root");
        require(model::textureCacheDirectory().empty(), "a relative data root is refused");
        model::TexImage c;
        require(model::loadTextureFromMemory(ByteView(png), options, c).ok() && !c.fromCache && !c.cacheNote.empty(), "an unresolvable shared location is reported in cacheNote and the load still succeeds");
        if (previousLength && previousLength < 32768) SetEnvironmentVariableW(L"STARLIGHT_DATA_ROOT", previous);
        else SetEnvironmentVariableW(L"STARLIGHT_DATA_ROOT", nullptr);
    }
    {
        std::vector<uint8_t> dds(148);
        uint32_t header[37]{};
        header[0] = 0x20534444; header[1] = 124; header[3] = 8; header[4] = 8; header[7] = 4;
        header[19] = 32; header[20] = 4; header[21] = 0x30315844; header[32] = DXGI_FORMAT_BC7_UNORM; header[33] = 3; header[35] = 1;
        std::memcpy(dds.data(), header, sizeof(header));
        for (uint32_t l = 0, w = 8, h = 8; l < 4; ++l, w = std::max(1u, w / 2), h = std::max(1u, h / 2)) dds.insert(dds.end(), size_t((w + 3) / 4) * ((h + 3) / 4) * 16, uint8_t(l + 1));
        const fs::path ddsPath = work / "span.dds";
        writeBytes(ddsPath, dds);
        model::TexImage a, b; std::string err;
        require(model::loadTextureDDSFromMemory(ByteView(dds), 4096, "span.dds", a).ok() && model::loadTextureDDS(ddsPath.string(), 4096, b, err) && sameImage(a, b) && a.mips.size() == 4, "memory and path DDS loaders agree");
        model::TextureProbe probe;
        require(model::probeTextureDecode(ByteView(dds), "span.dds", probe).ok() && probe.format == DXGI_FORMAT_BC7_UNORM && probe.width == 8 && probe.mipLevels == 4 && probe.container == "dds", "decode-only readiness validates a DDS payload");
        LoadError e = model::loadTextureDDSFromMemory(ByteView(dds.data(), dds.size() - 1), 4096, "span.dds", a);
        show("truncated DDS", e);
        require(e.code == LoadCode::Truncated && a.mips.empty(), "truncated DDS span returns Truncated");
    }
    {
        const auto glb = makeGlb();
        const auto atlas = atlasBytes({ 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f });
        const ByteView atlasView(atlas);
        model::SkinnedModel plain, withAtlas, viaPath, viaPathAtlas;
        LoadError e = model::loadSkinnedFromMemory(ByteView(glb), nullptr, "fixture-model.glb", plain);
        show("synthetic GLB without atlas", e);
        require(e.ok() && plain.loaded && plain.verts.size() == 4 && plain.indices.size() == 6 && plain.joints.size() == 2 && plain.clips.size() == 1 && plain.subs.size() == 1 && !plain.atlasUV, "GLB span loads geometry, skeleton and clip");
        require(plain.name == "fixture-model" && plain.source == "fixture-model.glb" && plain.subs[0].material == "fur" && plain.subs[0].node == "body" && plain.joints[1].parent == 0, "model names, material and joint hierarchy are preserved");
        require(plain.verts[1].u == 1.f && plain.verts[2].v == 1.f && std::isfinite(plain.verts[0].tw) && plain.verts[2].w[0] == 0.5f && plain.verts[2].j[1] == 1, "GLB UVs, weights and joints are read and tangents are generated");
        e = model::loadSkinnedFromMemory(ByteView(glb), &atlasView, "fixture-model.glb", withAtlas);
        require(e.ok() && withAtlas.atlasUV && withAtlas.verts[0].u == 0.1f && withAtlas.verts[0].v == 0.2f && withAtlas.verts[3].u == 0.7f && withAtlas.verts[3].v == 0.8f && std::isfinite(withAtlas.verts[3].tx), "atlas span replaces the UVs and the tangents follow");
        const fs::path glbPath = work / "fixture-model.glb", uvPath = work / "fixture-model_atlas_uv.bin";
        writeBytes(glbPath, glb); writeBytes(uvPath, atlas);
        std::string perr;
        require(model::loadSkinned(glbPath.string(), "", viaPath, perr) && perr.empty() && sameModel(plain, viaPath) && viaPath.source == glbPath.string(), "path loader equals memory loader without atlas");
        require(model::loadSkinned(glbPath.string(), uvPath.string(), viaPathAtlas, perr) && perr.empty() && sameModel(withAtlas, viaPathAtlas), "path loader equals memory loader with atlas");
        model::SkinnedModel scratch;
        for (size_t bytes : { size_t(0), size_t(4), size_t(28), size_t(36), size_t(64) }) {
            std::vector<uint8_t> wrong(bytes, 0);
            const ByteView view(wrong);
            e = model::loadSkinnedFromMemory(ByteView(glb), &view, "fixture-model.glb", scratch);
            if (bytes == 4) show("atlas of the wrong size", e);
            require(e.code == LoadCode::AtlasSizeMismatch && !scratch.loaded && scratch.verts.empty(), "atlas span with the wrong byte count is an AtlasSizeMismatch error");
        }
        auto nanAtlas = std::vector<float>{ 0.1f, 0.2f, 0.3f, 0.4f, std::numeric_limits<float>::quiet_NaN(), 0.6f, 0.7f, 0.8f };
        auto nanBytes = atlasBytes(nanAtlas); const ByteView nanView(nanBytes);
        e = model::loadSkinnedFromMemory(ByteView(glb), &nanView, "fixture-model.glb", scratch);
        show("atlas containing NaN", e);
        require(e.code == LoadCode::AtlasNotFinite && !scratch.loaded, "atlas span with a NaN is an AtlasNotFinite error");
        nanAtlas[4] = std::numeric_limits<float>::infinity();
        nanBytes = atlasBytes(nanAtlas);
        const ByteView infinityView(nanBytes);
        require(model::loadSkinnedFromMemory(ByteView(glb), &infinityView, "fixture-model.glb", scratch).code == LoadCode::AtlasNotFinite, "atlas span with an infinity is an AtlasNotFinite error");
        e = model::applyAtlasUv(plain, ByteView(atlas.data(), atlas.size() - 4), "fixture-model.glb");
        require(e.code == LoadCode::AtlasSizeMismatch && !plain.atlasUV, "applyAtlasUv validates before touching the model");
        require(model::applyAtlasUv(plain, atlasView, "fixture-model.glb").ok() && plain.atlasUV && plain.verts[0].u == 0.1f, "applyAtlasUv applies a valid span");
        const std::string softErr = [&] { std::string err; model::SkinnedModel soft; std::vector<uint8_t> shortBin = atlas; shortBin.resize(12); writeBytes(work / "short_atlas_uv.bin", shortBin);
            return model::loadSkinned(glbPath.string(), (work / "short_atlas_uv.bin").string(), soft, err) && !soft.atlasUV && soft.loaded ? err : std::string(); }();
        std::cout << "  legacy path loader keeps GLB UVs on a bad atlas: " << softErr << '\n';
        require(!softErr.empty(), "legacy path loader keeps the soft atlas fallback and reports it");
        std::string missingErr; model::SkinnedModel softMissing;
        require(model::loadSkinned(glbPath.string(), (work / "absent_atlas_uv.bin").string(), softMissing, missingErr) && softMissing.loaded && !softMissing.atlasUV && !missingErr.empty(), "legacy path loader reports a missing atlas file and keeps the GLB UVs");

        for (size_t cut = 0; cut < glb.size(); ++cut) {
            if (model::loadSkinnedFromMemory(ByteView(glb.data(), cut), nullptr, "cut.glb", scratch).ok()) { std::cout << "  GLB truncation at " << cut << " was accepted\n"; require(false, "every truncation of a GLB is rejected"); }
            if (scratch.loaded) require(false, "a rejected GLB leaves no loaded model");
        }
        require(true, "every truncation of a GLB is rejected");
        e = model::loadSkinnedFromMemory(ByteView(glb.data(), glb.size() - 16), nullptr, "cut.glb", scratch);
        show("truncated GLB", e);
        require(e.code == LoadCode::Truncated, "truncated GLB is reported as Truncated");
        GlbSpec shortBin; shortBin.trimBin = 24;
        e = model::loadSkinnedFromMemory(ByteView(makeGlb(shortBin)), nullptr, "short-bin.glb", scratch);
        show("GLB whose BIN chunk is shorter than the buffer", e);
        require(e.code == LoadCode::Truncated, "BIN chunk shorter than the declared buffer is Truncated");
        GlbSpec nanSpec; nanSpec.nanPosition = true;
        e = model::loadSkinnedFromMemory(ByteView(makeGlb(nanSpec)), nullptr, "nan.glb", scratch);
        show("GLB with a NaN position", e);
        require(e.code == LoadCode::Malformed && !scratch.loaded, "non-finite vertex data is Malformed");
        GlbSpec indexSpec; indexSpec.badIndex = true;
        e = model::loadSkinnedFromMemory(ByteView(makeGlb(indexSpec)), nullptr, "index.glb", scratch);
        show("GLB with an out-of-range index", e);
        require(e.code == LoadCode::Malformed && !scratch.loaded, "out-of-range indices are rejected as Malformed");
        GlbSpec jointSpec; jointSpec.badJoint = true;
        e = model::loadSkinnedFromMemory(ByteView(makeGlb(jointSpec)), nullptr, "joint.glb", scratch);
        show("GLB with a joint index beyond the skin", e);
        require(e.code == LoadCode::SkeletonInvalid, "a weighted joint index beyond the skin is SkeletonInvalid");
        GlbSpec cubicSpec; cubicSpec.cubic = true;
        e = model::loadSkinnedFromMemory(ByteView(makeGlb(cubicSpec)), nullptr, "cubic.glb", scratch);
        show("GLB with CUBICSPLINE animation", e);
        require(e.code == LoadCode::Unsupported, "CUBICSPLINE animation is an explicit Unsupported error");
        {
            auto sparse = [&](uint32_t indicesOffset, cgltf_result& validated, std::vector<float>& out) {
                const auto bytes = makeSparseGlb(indicesOffset);
                cgltf_options opts{}; cgltf_data* d = nullptr;
                if (cgltf_parse(&opts, bytes.data(), bytes.size(), &d) != cgltf_result_success) return false;
                bool loaded = cgltf_load_buffers(&opts, d, nullptr) == cgltf_result_success;
                validated = loaded ? cgltf_validate(d) : cgltf_result_data_too_short;
                out.assign(9, 0.f);
                bool unpacked = validated == cgltf_result_success && cgltf_accessor_unpack_floats(&d->accessors[0], out.data(), 9) == 9;
                cgltf_free(d);
                return unpacked || validated != cgltf_result_success;
            };
            cgltf_result validated = cgltf_result_success; std::vector<float> got;
            require(sparse(76, validated, got) && validated == cgltf_result_success, "sparse interleaved fixture validates and unpacks");
            require(got[0] == 1.f && got[3] == 10.f && got[4] == 11.f && got[5] == 12.f && got[6] == 13.f && got[7] == 14.f && got[8] == 15.f, "R7: sparse values are read tightly packed, not at the interleaved base stride");
            require(sparse(200, validated, got) && validated == cgltf_result_data_too_short, "DEP-01: a sparse index view outside its real buffer is rejected before any read");
        }
        GlbSpec manySpec; manySpec.primitives = 30;
        require(model::loadSkinnedFromMemory(ByteView(makeGlb(manySpec)), nullptr, "thirty.glb", scratch).ok() && scratch.subs.size() == 30, "CORE-12: a model with 30 drawable primitives loads");
        manySpec.primitives = 31;
        e = model::loadSkinnedFromMemory(ByteView(makeGlb(manySpec)), nullptr, "thirtyone.glb", scratch);
        show("GLB with 31 drawable primitives", e);
        require(e.code == LoadCode::Unsupported, "CORE-12: a model with more primitives than the instance buckets hold is refused");
        GlbSpec stepSpec; stepSpec.step = true;
        require(model::loadSkinnedFromMemory(ByteView(makeGlb(stepSpec)), nullptr, "step.glb", scratch).ok() && scratch.clips[0].channels[0].step, "R3: STEP interpolation is kept on the channel");
        require(model::loadSkinnedFromMemory(ByteView(glb), nullptr, "linear.glb", scratch).ok() && !scratch.clips[0].channels[0].step, "R3: LINEAR interpolation is not held");
        for (int bad : { 1, 2 }) { GlbSpec timeSpec; timeSpec.badTimes = bad; e = model::loadSkinnedFromMemory(ByteView(makeGlb(timeSpec)), nullptr, "times.glb", scratch);
            require(e.code == LoadCode::Malformed, "R5: keyframe times that are not finite and strictly increasing are Malformed"); }
        GlbSpec matrixSpec; matrixSpec.matrixJoint = true;
        require(model::loadSkinnedFromMemory(ByteView(makeGlb(matrixSpec)), nullptr, "matrix.glb", scratch).ok() && std::fabs(scratch.joints[1].t.y - 1.f) < 1e-5f && std::fabs(scratch.joints[1].s.x - 1.f) < 1e-5f, "R4: a joint given as a matrix keeps its translation");
        {   // GLB fuzz finding: bracket nesting made cgltf's tokenizer quadratic, so a 400 KB file stalled the load for minutes
            auto nested = [&](size_t depth) { glbfuzz::Parts parts; glbfuzz::split(glb, parts); parts.json.insert(1, "\"extras\":" + std::string(depth, '[') + std::string(depth, ']') + ","); return glbfuzz::pack(std::move(parts)); };
            require(model::loadSkinnedFromMemory(ByteView(nested(200)), nullptr, "nest200.glb", scratch).ok(), "GLB fuzz: a model whose JSON nests 200 levels loads");
            const auto started = std::chrono::steady_clock::now();
            e = model::loadSkinnedFromMemory(ByteView(nested(200000)), nullptr, "nest200000.glb", scratch);
            show("GLB nested 200000 levels", e);
            require(e.code == LoadCode::Malformed && e.message.find("nests deeper") != std::string::npos && !scratch.loaded, "GLB fuzz: JSON nested beyond 256 levels is Malformed");
            require(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() < 5.0, "GLB fuzz: deeply nested JSON is refused in well under a second");
            // the same nesting behind a damaged quote, which puts a scanner that tracks strings out of step with the tokenizer (found by the fuzzer at 30000 levels, 0.8 s)
            const auto damaged = editJson(editJson(glbfuzz::makeInterleavedGlb(), "34963}],\"acce", "3496"), "\"scale\":[1,2,1]", "\"scale\":[1," + std::string(100000, '[') + std::string(100000, ']') + ",1]");
            const auto damagedStart = std::chrono::steady_clock::now();
            e = model::loadSkinnedFromMemory(ByteView(damaged), nullptr, "damaged-nest.glb", scratch);
            require(e.failed() && !scratch.loaded && std::chrono::duration<double>(std::chrono::steady_clock::now() - damagedStart).count() < 2.0, "GLB fuzz: deep nesting behind a damaged quote is still refused quickly");
        }
        {   // GLB fuzz finding: cgltf allocates a buffer's declared byteLength before decoding its base64 text, so a tiny data URI could demand gigabytes
            const auto uri = glbfuzz::makeDataUriGlb();
            require(model::loadSkinnedFromMemory(ByteView(uri), nullptr, "uri.glb", scratch).ok(), "GLB fuzz: the data URI fixture loads");
            e = model::loadSkinnedFromMemory(ByteView(editJson(uri, "\"byteLength\":60,\"uri\"", "\"byteLength\":9000000000000,\"uri\"")), nullptr, "uri-bomb.glb", scratch);
            show("GLB whose data URI claims 9 TB", e);
            require(e.code == LoadCode::Malformed && e.message.find("data URI holds fewer") != std::string::npos, "GLB fuzz: a data URI buffer cannot claim more bytes than its text holds");
        }
        {   // GLB fuzz findings: a node scaled to zero gave NaN normals (no inverse), and a transform that overflows gave infinite vertices
            const auto quad = glbfuzz::makeInterleavedGlb();
            require(model::loadSkinnedFromMemory(ByteView(quad), nullptr, "quad.glb", scratch).ok() && scratch.verts.size() == 4, "GLB fuzz: the interleaved quad fixture loads");
            e = model::loadSkinnedFromMemory(ByteView(editJson(quad, "\"scale\":[1,2,1]", "\"scale\":[0,2,1]")), nullptr, "zero-scale.glb", scratch);
            show("GLB whose node scale is zero", e);
            require(e.ok() && modelProblem(scratch).empty(), "GLB fuzz: a node scaled to zero loads with finite normals");
            e = model::loadSkinnedFromMemory(ByteView(editJson(quad, "\"translation\":[0,0.5,0],\"scale\":[1,2,1]", "\"translation\":[3e38,0,0],\"scale\":[3e38,3e38,3e38]")), nullptr, "overflow.glb", scratch);
            show("GLB whose node transform overflows", e);
            require(e.code == LoadCode::Malformed && !scratch.loaded, "GLB fuzz: vertices that overflow to infinity under the node transform are Malformed");
        }
        const std::string junk = "definitely not a glb file, only text that is long enough to have a header";
        e = model::loadSkinnedFromMemory(ByteView(reinterpret_cast<const uint8_t*>(junk.data()), junk.size()), nullptr, "junk.glb", scratch);
        show("non-GLB bytes", e);
        require(e.failed(), "non-GLB bytes are rejected");
        e = model::loadSkinnedFromMemory(ByteView(), nullptr, "empty.glb", scratch);
        require(e.code == LoadCode::Truncated, "empty GLB span is Truncated");
        std::string missing;
        require(!model::loadSkinned((work / "absent.glb").string(), "", scratch, missing) && !missing.empty(), "path loader reports a missing GLB");

        for (const char* name : { "migration/fixtures/triangle.glb", "migration/fixtures/deep-node-graph.glb" }) {
            const auto bytes = readBytes(root / name);
            if (bytes.empty()) { std::cout << "  NOT RUN, repo fixture missing: " << name << '\n'; require(false, "repo GLB fixture present"); }
            std::string pathErr;
            LoadError memoryError = model::loadSkinnedFromMemory(ByteView(bytes), nullptr, name, scratch);
            const bool pathOk = model::loadSkinned((root / name).string(), "", viaPath, pathErr);
            show(name, memoryError);
            require(memoryError.code == LoadCode::NoGeometry && !pathOk && pathErr.find("no textured triangle geometry") != std::string::npos, "repo GLB fixture without UVs fails identically through memory and path loaders");
        }
    }
}
}
int main(int argc, char** argv) {
    try {
        const auto dir = std::filesystem::path(argc > 1 ? argv[1] : "texture-checks");
        const bool cpuOnly = argc > 2 && std::string(argv[2]) == "--cpu-only";
        const auto root = argc > 3 ? std::filesystem::path(argv[3]) : std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
        std::filesystem::create_directories(dir);
        int count = 0;
        auto require = [&](bool okay, const char* label) { if (!okay) throw std::runtime_error(label); ++count; };
        {
            using namespace starlight::surface;
            Tile tile(1);tile[0].size=2;for(auto& map:tile[0].maps)map.resize(16,255);
            for(int p=0;p<4;++p){for(int c=0;c<3;++c)tile[0].maps[0][p*4+c]=p<2?0:255;tile[0].maps[1][p*4]=128;tile[0].maps[1][p*4+1]=128;tile[0].maps[1][p*4+2]=255;tile[0].maps[2][p*4+1]=128;}
            appendMips(tile);
            require(tile.size()==2 && tile.back().maps[0][0]>=187 && tile.back().maps[0][0]<=188,"albedo mips average linear light rather than darkening sRGB");
            require(tile.back().maps[1][2]==255 && tile.back().maps[2][1]<=129,"flat normal mips remain normalized without roughness inflation");
            Tile tilted(1);tilted[0]=tile[0];
            for(int p=0;p<4;++p){tilted[0].maps[1][p*4]=p<2?32:223;tilted[0].maps[1][p*4+1]=128;tilted[0].maps[1][p*4+2]=211;}
            appendMips(tilted);
            require(tilted.back().maps[1][0]>=127 && tilted.back().maps[1][0]<=128 && tilted.back().maps[1][2]==255,"opposing normal detail resolves to a unit surface guide");
            require(tilted.back().maps[2][1]>150,"normal variance increases distant roughness to suppress sparkling");
            for(int kind=0;kind<5;++kind){
                auto a=builtin(64,kind),b=builtin(64,kind);
                require(a.size()==7 && a.back().size==1 && a[0].maps==b[0].maps,"built-in material maps are deterministic and include a complete mip tail");
                bool valid=true;for(const auto& level:a)for(size_t k=0;k<level.maps[1].size();k+=4){float x=level.maps[1][k]/127.5f-1,y=level.maps[1][k+1]/127.5f-1,z=level.maps[1][k+2]/127.5f-1;valid&=std::abs(std::sqrt(x*x+y*y+z*z)-1)<.012f && z>0 && level.maps[2][k+2]==0;}
                require(valid,"built-in normal and ORM mips retain finite unit normals and dielectric materials");
                require(std::abs(noise(.13f,.27f,32,43)-noise(1.13f,1.27f,32,43))<.00001f,"authored surface noise wraps without discontinuities");
                bool periodic=true,range=true;float minHeight=1,maxHeight=-1,minRough=1,maxRough=0;
                for(int y=0;y<32;++y)for(int x=0;x<32;++x){
                    float u=(x+.37f)/32,v=(y+.61f)/32;auto s=structure(u,v,kind,1.f/1024),wrapped=structure(u+1,v-1,kind,1.f/1024);
                    periodic&=std::abs(s.height-wrapped.height)<.000001f && std::abs(s.ao-wrapped.ao)<.0001f && std::abs(s.roughness-wrapped.roughness)<.0001f;
                    for(int c=0;c<3;++c){periodic&=std::abs(s.color[c]-wrapped.color[c])<.0001f;range&=std::isfinite(s.color[c])&&s.color[c]>0&&s.color[c]<=1;}
                    range&=std::isfinite(s.height)&&s.ao>=.4f&&s.ao<=1&&s.roughness>=.4f&&s.roughness<=1;
                    minHeight=std::min(minHeight,s.height);maxHeight=std::max(maxHeight,s.height);minRough=std::min(minRough,s.roughness);maxRough=std::max(maxRough,s.roughness);
                }
                require(periodic,"material structure wraps in colour, height, occlusion and roughness without seams");
                require(range && maxHeight-minHeight>.0002f && maxRough-minRough>.025f,"material structure has bounded physical relief and variable dielectric roughness");
                // Save maps for inspection alongside render evidence, with no new image library.
                auto detail=builtin(256,kind);
                for(int channel=0;channel<3;++channel){std::ofstream image(dir/("surface-"+std::to_string(kind)+"-"+std::to_string(channel)+".ppm"),std::ios::binary);image<<"P6\n256 256\n255\n";for(size_t p=0;p<detail[0].maps[channel].size();p+=4)image.write(reinterpret_cast<const char*>(detail[0].maps[channel].data()+p),3);}
            }
            require(resolved(128,1.f/64)==0 && resolved(128,1.f/1024)==1,"unresolved fine structure is prefiltered rather than aliased into low-quality maps");
            for(unsigned workers:{1u,2u,5u}){
                auto prepared=builtinSet(64,workers);bool same=true;
                for(int kind=0;kind<5;++kind){auto reference=builtin(64,kind);same&=prepared[kind].size()==reference.size();
                    for(size_t level=0;same && level<reference.size();++level)same&=prepared[kind][level].size==reference[level].size && prepared[kind][level].maps==reference[level].maps;
                }
                require(same,"bounded concurrent material preparation preserves every albedo, normal and ORM mip byte");
            }
            bool preparationRejected=false;try{builtinSet(63,5);}catch(const std::runtime_error&){preparationRejected=true;}
            require(preparationRejected,"concurrent material preparation propagates invalid-resolution failures");
            bool rejected=false;try{builtin(63,3);}catch(const std::runtime_error&){rejected=true;}require(rejected,"unsupported surface resolution rejected");
        }
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
        memload::run(dir, root, require);
        if (cpuOnly) { std::cout << count << " texture checks passed (CPU only: DDS, memory loaders; GPU section not run)\n"; return 0; }
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
        std::cout << count << " texture checks passed (2K/4K DDS, BC5/BC7, memory loaders, WARP upload)\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
