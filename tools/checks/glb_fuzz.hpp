// Deterministic mutation fuzzer for the two GLB loaders (deep/engine/model/skinned.cpp loadSkinnedFromMemory and editor/cpp/assets.cpp parse_glb / load_glb),
// plus the synthetic GLB fixtures it starts from (moved here unchanged from texture_checks.cpp so both check programs share them).
//
// Include this header exactly once per check program: it replaces the global operator new/delete to measure allocation per fuzz iteration.
//
// What every iteration must satisfy (the caller's callback reports the loader outcome, this file judges the rest):
//   - no crash and no exception other than a clean rejection (the callback decides what a clean rejection is);
//   - no hang: one iteration may take at most kIterationSeconds (a watchdog thread ends the process with a message if it does not return);
//   - no allocation beyond the stated caps (kSingleAllocCap for one request, kPeakCap for the live bytes one load holds at its peak). The
//     inputs are a few kilobytes, so a load that wants more has been amplified by a header field instead of by real data;
//   - a rejected input leaves the caller's invariants intact, an accepted input produces a model that passes the caller's validity check.
// Every input is a pure function of (corpus, base seed, iteration number), so any failure replays exactly with STARLIGHT_GLB_FUZZ_START=<n>.
// Switches (all optional): STARLIGHT_GLB_FUZZ_ITERATIONS (long run, default is the short suite run), STARLIGHT_GLB_FUZZ_SEED (base seed),
// STARLIGHT_GLB_FUZZ_START (first iteration), STARLIGHT_GLB_FUZZ_TRACE=1 (print every iteration number before it runs),
// STARLIGHT_GLB_FUZZ_SAVE=1 (write every input of the run into the scratch folder, for inspecting a slow or odd one).
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <malloc.h>
#include <new>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace glbfuzz {

// ---- allocation accounting (per thread, only while armed) ----
constexpr size_t kSingleAllocCap = 64ull << 20;
constexpr size_t kPeakCap = 128ull << 20;
constexpr double kIterationSeconds = 20.0;

struct AllocState {
    bool armed = false;
    bool refused = false;
    long long live = 0, peak = 0;
    size_t biggest = 0;
};
inline thread_local AllocState g_alloc;

// Also the allocator for cgltf (texture_checks.cpp defines CGLTF_MALLOC as this), which would otherwise be invisible: it uses malloc directly.
inline void* trackedMalloc(size_t n) noexcept {
    AllocState& a = g_alloc;
    if (a.armed) {
        if (n > a.biggest) a.biggest = n;
        if (n > kSingleAllocCap) { a.refused = true; return nullptr; }   // never really allocate an amplified request; the iteration is reported
    }
    void* p = std::malloc(n ? n : 1);
    if (p && a.armed) { a.live += (long long)_msize(p); if (a.live > a.peak) a.peak = a.live; }
    return p;
}
inline void* trackedAlloc(size_t n) {
    void* p = trackedMalloc(n);
    if (!p) throw std::bad_alloc();
    return p;
}
inline void trackedFree(void* p) noexcept {
    if (!p) return;
    AllocState& a = g_alloc;
    if (a.armed) a.live -= (long long)_msize(p);
    std::free(p);
}

// ---- fixtures (moved unchanged from texture_checks.cpp) ----
inline void putLE(std::vector<uint8_t>& v, uint32_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); v.push_back(uint8_t(x >> 16)); v.push_back(uint8_t(x >> 24)); }

struct GlbSpec { bool nanPosition = false, badIndex = false, badJoint = false, cubic = false; size_t trimBin = 0; int primitives = 1; bool step = false; bool matrixJoint = false; int badTimes = 0; bool nanKey = false; bool matrixTrs = false; int armature = 0; int extraMode = -1; bool hugeIndexCount = false; bool wideTimes = false; bool emptyChannel = false; bool noViewVerts = false; bool negativeWeight = false, zeroWeight = false; };

template <class T> size_t addBlob(std::vector<uint8_t>& bin, const std::vector<T>& v) {
    while (bin.size() % 4) bin.push_back(0);
    const size_t offset = bin.size();
    const uint8_t* p = reinterpret_cast<const uint8_t*>(v.data());
    bin.insert(bin.end(), p, p + v.size() * sizeof(T));
    return offset;
}

inline std::vector<uint8_t> makeGlb(const GlbSpec& spec = {}) {
    std::vector<float> pos{ 0,0,0, 1,0,0, 1,1,0, 0,1,0 };
    if (spec.nanPosition) pos[0] = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> nrm{ 0,0,1, 0,0,1, 0,0,1, 0,0,1 };
    std::vector<float> uv{ 0,0, 1,0, 1,1, 0,1 };
    std::vector<uint8_t> joints{ 0,0,0,0, uint8_t(spec.badJoint ? 7 : 1),0,0,0, 0,1,0,0, 1,0,0,0 };
    std::vector<float> weights{ 1,0,0,0, 1,0,0,0, 0.5f,0.5f,0,0, 1,0,0,0 };
    if (spec.negativeWeight) { weights[8] = 1.5f; weights[9] = -0.5f; }
    if (spec.zeroWeight) weights[12] = weights[13] = weights[14] = weights[15] = 0.f;
    std::vector<uint16_t> indices{ 0,1,2, 0,2,uint16_t(spec.badIndex ? 9 : 3) };
    std::vector<float> ibm{ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1, 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,-1,0,1 };
    std::vector<float> times{ 0.f, 1.f };
    if (spec.badTimes == 1) times[0] = std::nanf("");
    if (spec.badTimes == 2) times = { 1.f, 0.f };
    if (spec.wideTimes) times = { 0.f, 1.f, 0.f, 0.f };
    if (spec.badTimes == 3) times = { 0.f, 0.f };
    if (spec.badTimes == 4) times = { -1.f, 1.f };
    std::vector<float> rot = spec.cubic ? std::vector<float>{ 0,0,0,0, 0,0,0,1, 0,0,0,0, 0,0,0,0, 0,0,.7071f,.7071f, 0,0,0,0 } : std::vector<float>{ 0,0,0,1, 0,0,.7071f,.7071f };
    if (spec.nanKey) rot[4] = std::nanf("");
    std::vector<uint8_t> bin;
    const size_t offs[9] = { addBlob(bin, pos), addBlob(bin, nrm), addBlob(bin, uv), addBlob(bin, joints), addBlob(bin, weights), addBlob(bin, indices), addBlob(bin, ibm), addBlob(bin, times), addBlob(bin, rot) };
    const size_t lens[9] = { pos.size() * 4, nrm.size() * 4, uv.size() * 4, joints.size(), weights.size() * 4, indices.size() * 2, ibm.size() * 4, times.size() * 4, rot.size() * 4 };
    while (bin.size() % 4) bin.push_back(0);
    const size_t declared = bin.size();
    const struct { int component; size_t count; const char* type; } acc[9] = {
        { 5126, spec.noViewVerts ? 5000000u : 4u, "VEC3" }, { 5126, spec.noViewVerts ? 5000000u : 4u, "VEC3" }, { 5126, spec.noViewVerts ? 5000000u : 4u, "VEC2" }, { 5121, spec.noViewVerts ? 5000000u : 4u, "VEC4" }, { 5126, spec.noViewVerts ? 5000000u : 4u, "VEC4" },
        { spec.hugeIndexCount ? 5125 : 5123, spec.hugeIndexCount ? 4611686018427387905ull : 6ull, "SCALAR" }, { 5126, 2, "MAT4" }, { 5126, spec.emptyChannel ? 0u : 2u, spec.wideTimes ? "VEC2" : "SCALAR" }, { 5126, spec.emptyChannel ? 0u : spec.cubic ? 6u : 2u, "VEC4" } };
    std::string views, accessors;
    for (int i = 0; i < 9; ++i) {
        views += std::string(i ? "," : "") + "{\"buffer\":0,\"byteOffset\":" + std::to_string(offs[i]) + ",\"byteLength\":" + std::to_string(lens[i]) + "}";
        accessors += std::string(i ? "," : "") + "{" + (spec.noViewVerts && i < 5 ? "" : "\"bufferView\":" + std::to_string(i) + ",") + "\"componentType\":" + std::to_string(acc[i].component) + ",\"count\":" + std::to_string(acc[i].count) + ",\"type\":\"" + acc[i].type + "\"}";
    }
    std::string primitiveList;
    for (int i = 0; i < spec.primitives; ++i) primitiveList += std::string(i ? "," : "") + "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"JOINTS_0\":3,\"WEIGHTS_0\":4},\"indices\":5,\"material\":0}";
    if (spec.extraMode >= 0) primitiveList += std::string(primitiveList.empty() ? "" : ",") + "{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2,\"JOINTS_0\":3,\"WEIGHTS_0\":4},\"mode\":" + std::to_string(spec.extraMode) + ",\"material\":0}";
    const std::string extraNode = spec.armature == 1 ? ",{\"name\":\"armature\",\"children\":[1],\"translation\":[5,0,0],\"rotation\":[0,0,0.70710678,0.70710678],\"scale\":[2,2,2]}"
        : spec.armature == 2 ? ",{\"name\":\"mid\",\"translation\":[0,0,3],\"children\":[2]}" : "";
    std::string json = "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" + std::to_string(declared) + "}],\"bufferViews\":[" + views + "],\"accessors\":[" + accessors + "],"
        "\"materials\":[{\"name\":\"fur\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.5,0.25,0.75,1]}}],"
        "\"meshes\":[{\"primitives\":[" + primitiveList + "]}],"
        "\"nodes\":[{\"name\":\"body\",\"mesh\":0,\"skin\":0},{\"name\":\"root\",\"children\":[" + std::string(spec.armature == 2 ? "3" : "2") + "]},{\"name\":\"child\"," + std::string(spec.matrixTrs ? "\"matrix\":[0,2,0,0,-3,0,0,0,0,0,4,0,0,1,0,1]" : spec.matrixJoint ? "\"matrix\":[1,0,0,0,0,1,0,0,0,0,1,0,0,1,0,1]" : "\"translation\":[0,1,0]") + "}" + extraNode + "],"
        "\"skins\":[{\"joints\":[1,2],\"inverseBindMatrices\":6}],"
        "\"animations\":[{\"name\":\"idle\",\"samplers\":[{\"input\":7,\"output\":8,\"interpolation\":\"" + std::string(spec.cubic ? "CUBICSPLINE" : spec.step ? "STEP" : "LINEAR") + "\"}],\"channels\":[{\"sampler\":0,\"target\":{\"node\":2,\"path\":\"rotation\"}}]}],"
        "\"scenes\":[{\"nodes\":[0," + std::string(spec.armature == 1 ? "3" : "1") + "]}],\"scene\":0}";
    while (json.size() % 4) json.push_back(' ');
    bin.resize(bin.size() - spec.trimBin);
    while (bin.size() % 4) bin.push_back(0);
    std::vector<uint8_t> glb;
    putLE(glb, 0x46546C67u); putLE(glb, 2); putLE(glb, uint32_t(12 + 8 + json.size() + 8 + bin.size()));
    putLE(glb, uint32_t(json.size())); putLE(glb, 0x4E4F534Au); glb.insert(glb.end(), json.begin(), json.end());
    putLE(glb, uint32_t(bin.size())); putLE(glb, 0x004E4942u); glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

inline std::vector<uint8_t> makeSparseGlb(uint32_t indicesOffset) {
    std::vector<uint8_t> bin(104, 0);
    auto putF = [&](size_t at, float v) { std::memcpy(bin.data() + at, &v, 4); };
    for (int i = 0; i < 3; ++i) for (int c = 0; c < 3; ++c) putF(size_t(i) * 32 + c * 4, float(1 + c));
    bin[76] = 1; bin[77] = 2;
    for (int i = 0; i < 6; ++i) putF(80 + size_t(i) * 4, float(10 + i));
    std::string json =
        "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":104}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":76,\"byteStride\":32},{\"buffer\":0,\"byteOffset\":" + std::to_string(indicesOffset) + ",\"byteLength\":2},{\"buffer\":0,\"byteOffset\":80,\"byteLength\":24}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"sparse\":{\"count\":2,\"indices\":{\"bufferView\":1,\"componentType\":5121},\"values\":{\"bufferView\":2}}}]}";
    while (json.size() % 4) json.push_back(' ');
    std::vector<uint8_t> glb;
    putLE(glb, 0x46546C67u); putLE(glb, 2); putLE(glb, uint32_t(12 + 8 + json.size() + 8 + bin.size()));
    putLE(glb, uint32_t(json.size())); putLE(glb, 0x4E4F534Au); glb.insert(glb.end(), json.begin(), json.end());
    putLE(glb, uint32_t(bin.size())); putLE(glb, 0x004E4942u); glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

// A static quad on an interleaved view (stride 32: position, normal, uv), 32-bit indices, a parent/child node pair with scale, rotation and translation. No skin.
inline std::vector<uint8_t> makeInterleavedGlb() {
    std::vector<float> verts{ 0,0,0, 0,0,1, 0,0,  1,0,0, 0,0,1, 1,0,  1,1,0, 0,0,1, 1,1,  0,1,0, 0,0,1, 0,1 };
    std::vector<uint32_t> indices{ 0,1,2, 0,2,3 };
    std::vector<uint8_t> bin;
    const size_t vo = addBlob(bin, verts), io = addBlob(bin, indices);
    std::string json = "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" + std::to_string(bin.size()) + "}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":" + std::to_string(vo) + ",\"byteLength\":128,\"byteStride\":32,\"target\":34962},{\"buffer\":0,\"byteOffset\":" + std::to_string(io) + ",\"byteLength\":24,\"target\":34963}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},{\"bufferView\":0,\"byteOffset\":12,\"componentType\":5126,\"count\":4,\"type\":\"VEC3\"},"
        "{\"bufferView\":0,\"byteOffset\":24,\"componentType\":5126,\"count\":4,\"type\":\"VEC2\"},{\"bufferView\":1,\"componentType\":5125,\"count\":6,\"type\":\"SCALAR\"}],"
        "\"materials\":[{\"name\":\"cloth\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.2,0.4,0.6,1]}}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"mode\":4,\"material\":0}]}],"
        "\"nodes\":[{\"name\":\"parent\",\"children\":[1],\"translation\":[0,0.5,0],\"scale\":[1,2,1]},{\"name\":\"child\",\"mesh\":0,\"rotation\":[0,0.7071,0,0.7071]}],"
        "\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
    while (json.size() % 4) json.push_back(' ');
    std::vector<uint8_t> glb;
    putLE(glb, 0x46546C67u); putLE(glb, 2); putLE(glb, uint32_t(12 + 8 + json.size() + 8 + bin.size()));
    putLE(glb, uint32_t(json.size())); putLE(glb, 0x4E4F534Au); glb.insert(glb.end(), json.begin(), json.end());
    putLE(glb, uint32_t(bin.size())); putLE(glb, 0x004E4942u); glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

// A triangle whose only buffer is a base64 data URI (no BIN chunk): exercises the URI decoder.
inline std::vector<uint8_t> makeDataUriGlb() {
    std::vector<float> pos{ 0,0,0, 1,0,0, 0,1,0 };
    std::vector<float> uv{ 0,0, 1,0, 0,1 };
    std::vector<uint8_t> raw;
    addBlob(raw, pos); addBlob(raw, uv);
    while (raw.size() % 3) raw.push_back(0);
    static const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string b64;
    for (size_t i = 0; i < raw.size(); i += 3) {
        const uint32_t v = (uint32_t(raw[i]) << 16) | (uint32_t(raw[i + 1]) << 8) | raw[i + 2];
        for (int k = 3; k >= 0; --k) b64.push_back(alphabet[(v >> (6 * k)) & 63]);
    }
    std::string json = "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"byteLength\":" + std::to_string(raw.size()) + ",\"uri\":\"data:application/octet-stream;base64," + b64 + "\"}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":36,\"byteLength\":24}],"
        "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"TEXCOORD_0\":1}}]}],\"nodes\":[{\"mesh\":0}],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
    while (json.size() % 4) json.push_back(' ');
    std::vector<uint8_t> glb;
    putLE(glb, 0x46546C67u); putLE(glb, 2); putLE(glb, uint32_t(12 + 8 + json.size()));
    putLE(glb, uint32_t(json.size())); putLE(glb, 0x4E4F534Au); glb.insert(glb.end(), json.begin(), json.end());
    return glb;
}

// ---- deterministic generator ----
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t seed) : state(seed) {}
    uint64_t next() {   // splitmix64
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    size_t below(size_t n) { return n ? size_t(next() % n) : 0; }
};

// ---- GLB structure helpers (the standard layout: header, JSON chunk, optional BIN chunk) ----
struct Parts { std::string json; std::vector<uint8_t> bin; bool hasBin = false; };

inline uint32_t readLE(const std::vector<uint8_t>& g, size_t at) { uint32_t v; std::memcpy(&v, g.data() + at, 4); return v; }

inline bool split(const std::vector<uint8_t>& g, Parts& p) {
    if (g.size() < 20 || readLE(g, 0) != 0x46546C67u || readLE(g, 4) != 2) return false;
    size_t o = 12;
    const uint32_t jl = readLE(g, o), jt = readLE(g, o + 4);
    o += 8;
    if (jt != 0x4E4F534Au || jl > g.size() - o) return false;
    p.json.assign(reinterpret_cast<const char*>(g.data()) + o, jl);
    o += jl;
    p.bin.clear(); p.hasBin = false;
    if (o + 8 <= g.size()) {
        const uint32_t bl = readLE(g, o), bt = readLE(g, o + 4);
        o += 8;
        if (bt != 0x004E4942u || bl > g.size() - o) return false;
        p.bin.assign(g.begin() + (std::ptrdiff_t)o, g.begin() + (std::ptrdiff_t)(o + bl));
        p.hasBin = true;
    }
    return true;
}

inline std::vector<uint8_t> pack(Parts p) {
    while (p.json.size() % 4) p.json.push_back(' ');
    while (p.bin.size() % 4) p.bin.push_back(0);
    std::vector<uint8_t> g;
    putLE(g, 0x46546C67u); putLE(g, 2); putLE(g, uint32_t(12 + 8 + p.json.size() + (p.hasBin ? 8 + p.bin.size() : 0)));
    putLE(g, uint32_t(p.json.size())); putLE(g, 0x4E4F534Au); g.insert(g.end(), p.json.begin(), p.json.end());
    if (p.hasBin) { putLE(g, uint32_t(p.bin.size())); putLE(g, 0x004E4942u); g.insert(g.end(), p.bin.begin(), p.bin.end()); }
    return g;
}

struct Tok { size_t pos, len; };

inline void scanJson(const std::string& s, std::vector<Tok>& numbers, std::vector<Tok>& strings, std::vector<size_t>& objects) {
    bool in = false; size_t start = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (in) { if (c == '\\') ++i; else if (c == '"') { strings.push_back({ start + 1, i - start - 1 }); in = false; } continue; }
        if (c == '"') { in = true; start = i; continue; }
        if (c == '{') objects.push_back(i);
        if (c == '-' || (c >= '0' && c <= '9')) {
            size_t j = i + 1;
            while (j < s.size() && ((s[j] >= '0' && s[j] <= '9') || s[j] == '.' || s[j] == 'e' || s[j] == 'E' || s[j] == '+' || s[j] == '-')) ++j;
            numbers.push_back({ i, j - i });
            i = j - 1;
        }
    }
}

inline const char* const kInterestingNumbers[] = {
    "0", "1", "2", "3", "4", "5", "-1", "-2", "7", "8", "12", "15", "16", "24", "32", "36", "255", "256", "257", "65535", "65536", "65537", "1000000", "2000000", "2000001", "6000000", "6000001", "16777216",
    "2147483647", "2147483648", "-2147483648", "-2147483649", "4294967295", "4294967296", "4294967297", "9007199254740991", "9007199254740993", "9223372036854775807", "9223372036854775808",
    "18446744073709551615", "18446744073709551616", "-9223372036854775808", "1e9", "1e19", "1e308", "1e309", "-1e309", "1e-320", "0.5", "-0.0", "3.4028235e38", "3.5e38", "123456789012345678901234567890" };
inline const char* const kOtherValues[] = { "null", "true", "false", "[]", "{}", "\"x\"", "\"\"", "[1]", "[0,0,0,0]", "[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0]", "[1e39,0,0]", "{\"a\":1}", "\"4\"", "[[0]]" };
inline const char* const kStrings[] = {
    "SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4", "LINEAR", "STEP", "CUBICSPLINE", "translation", "rotation", "scale", "weights", "pointer", "POSITION", "NORMAL", "TANGENT", "TEXCOORD_0", "TEXCOORD_1",
    "JOINTS_0", "WEIGHTS_0", "COLOR_0", "JOINTS_1", "foo.bin", "../x.bin", "data:application/octet-stream;base64,AAAA", "data:application/octet-stream;base64,", "data:;base64,@@@@", "%00%41", "", "2.0", "1.0", "glow", "catchlight" };

inline std::string pickNumber(Rng& r) {
    if (r.below(2)) return std::to_string(r.below(13));
    return kInterestingNumbers[r.below(sizeof(kInterestingNumbers) / sizeof(*kInterestingNumbers))];
}

inline std::string insertedMember(Rng& r) {
    const std::string n = pickNumber(r), m = pickNumber(r), k = pickNumber(r);
    switch (r.below(16)) {
    case 0: return "\"byteStride\":" + n + ",";
    case 1: return "\"byteOffset\":" + n + ",";
    case 2: return "\"bufferView\":" + n + ",";
    case 3: return "\"count\":" + n + ",";
    case 4: return "\"componentType\":" + std::to_string(5120 + r.below(8)) + ",";
    case 5: return "\"sparse\":{\"count\":" + n + ",\"indices\":{\"bufferView\":" + m + ",\"byteOffset\":" + k + ",\"componentType\":" + std::to_string(5121 + r.below(5)) + "},\"values\":{\"bufferView\":" + m + ",\"byteOffset\":" + k + "}},";
    case 6: return "\"mode\":" + std::to_string(r.below(8)) + ",";
    case 7: return "\"skin\":" + n + ",";
    case 8: return "\"children\":[" + n + "," + m + "],";
    case 9: return "\"mesh\":" + n + ",";
    case 10: return "\"matrix\":[" + n + ",0,0,0,0," + m + ",0,0,0,0,1,0," + k + ",0,0,1],";
    case 11: return "\"scale\":[" + n + "," + m + "," + k + "],";
    case 12: return "\"rotation\":[" + n + "," + m + "," + k + ",0],";
    case 13: return "\"normalized\":true,";
    case 14: return "\"scene\":" + n + ",";
    default: return "\"buffer\":" + n + ",";
    }
}

inline bool parseInt(const std::string& s, long long& out) {
    if (s.empty() || s.size() > 18) return false;
    for (size_t i = 0; i < s.size(); ++i) if (!((s[i] >= '0' && s[i] <= '9') || (i == 0 && s[i] == '-' && s.size() > 1))) return false;
    out = std::stoll(s);
    return true;
}

// Applies one mutation to a GLB. Structure-aware mutations need the standard layout and fall back to a byte flip when the input no longer has it.
inline void mutateOnce(std::vector<uint8_t>& g, Rng& r) {
    auto byteAt = [&] { return g.empty() ? size_t(0) : r.below(g.size()); };
    const size_t kind = r.below(20);
    if (kind < 5) {   // byte level
        if (g.empty()) { g.push_back(uint8_t(r.next())); return; }
        switch (kind) {
        case 0: for (size_t n = 1 + r.below(3); n--;) g[byteAt()] ^= uint8_t(1u << r.below(8)); return;
        case 1: { static const uint8_t v[] = { 0, 1, 0x7F, 0x80, 0xFF, 0xFE }; for (size_t n = 1 + r.below(3); n--;) g[byteAt()] = r.below(2) ? v[r.below(sizeof(v))] : uint8_t(r.next()); return; }
        case 2: { const size_t at = r.below(g.size() + 1); std::vector<uint8_t> add(1 + r.below(16)); for (auto& b : add) b = uint8_t(r.next()); g.insert(g.begin() + (std::ptrdiff_t)at, add.begin(), add.end()); return; }
        case 3: { const size_t at = byteAt(), n = std::min<size_t>(1 + r.below(32), g.size() - at); g.erase(g.begin() + (std::ptrdiff_t)at, g.begin() + (std::ptrdiff_t)(at + n)); return; }
        default: g.resize(r.below(3) ? r.below(g.size() + 1) : g.size() - std::min<size_t>(g.size(), 1 + r.below(8))); return;
        }
    }
    Parts p;
    const bool structured = split(g, p);
    if (kind == 5 || !structured) {   // container fields: total length, chunk lengths, chunk types
        if (g.size() < 20) { g.resize(g.size() + 1 + r.below(20), 0); return; }
        std::vector<size_t> fields{ 4, 8, 12, 16 };
        if (structured) { const size_t bin = 20 + p.json.size(); if (p.hasBin && bin + 8 <= g.size()) { fields.push_back(bin); fields.push_back(bin + 4); } }
        const size_t at = fields[r.below(fields.size())];
        const uint32_t size = uint32_t(g.size());
        const uint32_t choices[] = { 0, 1, 2, 3, 4, 8, size, size - 1, size + 1, size + 4, size - 4, size - 8, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 0xFFFFFFFCu, 0x4E4F534Au, 0x004E4942u, 0x00000000u, 3u };
        const uint32_t v = choices[r.below(sizeof(choices) / sizeof(*choices))];
        std::memcpy(g.data() + at, &v, 4);
        return;
    }
    if (kind == 6) {   // BIN payload: floats to special values, integer-looking bytes to boundary values
        if (!p.hasBin || p.bin.empty()) { g[byteAt()] ^= 0x40; return; }
        static const uint32_t words[] = { 0x7FC00000u, 0xFFC00000u, 0x7F800000u, 0xFF800000u, 0x7F7FFFFFu, 0x00000001u, 0x80000000u, 0xFFFFFFFFu, 0x00000000u, 0x3F800000u, 0x7FFFFFFFu, 0x40000000u, 0x4F000000u };
        for (size_t n = 1 + r.below(3); n--;) {
            const size_t at = r.below(p.bin.size());
            const uint32_t w = words[r.below(sizeof(words) / sizeof(*words))];
            const size_t take = std::min<size_t>(4, p.bin.size() - at);
            std::memcpy(p.bin.data() + at, &w, take);
        }
        g = pack(std::move(p));
        return;
    }
    std::vector<Tok> numbers, strings; std::vector<size_t> objects;
    scanJson(p.json, numbers, strings, objects);
    if (kind <= 9) {   // replace a number with a boundary value
        if (numbers.empty()) return;
        const Tok t = numbers[r.below(numbers.size())];
        p.json.replace(t.pos, t.len, pickNumber(r));
    } else if (kind == 10) {   // replace a number with another JSON type
        if (numbers.empty()) return;
        const Tok t = numbers[r.below(numbers.size())];
        p.json.replace(t.pos, t.len, kOtherValues[r.below(sizeof(kOtherValues) / sizeof(*kOtherValues))]);
    } else if (kind == 11 || kind == 12) {   // near-miss integer: the value +/- a little, doubled or halved (offset and length boundaries)
        if (numbers.empty()) return;
        const Tok t = numbers[r.below(numbers.size())];
        long long v;
        if (!parseInt(p.json.substr(t.pos, t.len), v)) return;
        switch (r.below(6)) { case 0: v += 1; break; case 1: v -= 1; break; case 2: v += 4; break; case 3: v *= 2; break; case 4: v /= 2; break; default: v += (long long)r.below(64) - 32; break; }
        p.json.replace(t.pos, t.len, std::to_string(v));
    } else if (kind == 13) {   // swap two numbers (wrong but plausible references)
        if (numbers.size() < 2) return;
        size_t a = r.below(numbers.size()), b = r.below(numbers.size());
        if (a == b) return;
        if (a > b) std::swap(a, b);
        const std::string va = p.json.substr(numbers[a].pos, numbers[a].len), vb = p.json.substr(numbers[b].pos, numbers[b].len);
        p.json.replace(numbers[b].pos, numbers[b].len, va);
        p.json.replace(numbers[a].pos, numbers[a].len, vb);
    } else if (kind == 14 || kind == 15) {   // replace a string with a dictionary word or with another string of the document
        if (strings.empty()) return;
        const Tok t = strings[r.below(strings.size())];
        if (kind == 14) p.json.replace(t.pos, t.len, kStrings[r.below(sizeof(kStrings) / sizeof(*kStrings))]);
        else { const Tok o = strings[r.below(strings.size())]; p.json.replace(t.pos, t.len, p.json.substr(o.pos, o.len)); }
    } else if (kind == 16 || kind == 17) {   // add a member at the start of an object
        if (objects.empty()) return;
        p.json.insert(objects[r.below(objects.size())] + 1, insertedMember(r));
    } else if (kind == 18) {   // deep nesting where a number was (a recursive parser or destructor overflows the stack on this)
        if (numbers.empty()) return;
        const Tok t = numbers[r.below(numbers.size())];
        static const size_t depths[] = { 40, 300, 3000, 30000, 200000 };   // 200000 only in a long run: the editor's JSON parser takes about half a second on it
        static const bool longRun = std::getenv("STARLIGHT_GLB_FUZZ_ITERATIONS") != nullptr;
        const size_t d = depths[r.below(longRun ? sizeof(depths) / sizeof(*depths) : sizeof(depths) / sizeof(*depths) - 1)];
        p.json.replace(t.pos, t.len, std::string(d, '[') + std::string(d, ']'));
    } else {   // truncate or cut the JSON text itself (the chunk lengths stay consistent, the document does not)
        if (p.json.empty()) return;
        if (r.below(2)) p.json.resize(r.below(p.json.size()));
        else { const size_t at = r.below(p.json.size()); p.json.erase(at, std::min<size_t>(1 + r.below(24), p.json.size() - at)); }
    }
    g = pack(std::move(p));
}

struct Seed { std::string name; std::vector<uint8_t> bytes; };

// The input for iteration i: a pure function of (seeds, base seed, i).
inline std::vector<uint8_t> inputFor(const std::vector<Seed>& seeds, uint64_t base, size_t i) {
    Rng r(base * 0x100000001B3ull + i * 0x9E3779B97F4A7C15ull + 1);
    std::vector<uint8_t> g = seeds[i % seeds.size()].bytes;
    const size_t steps = 1 + r.below(3);
    for (size_t s = 0; s < steps; ++s) mutateOnce(g, r);
    return g;
}

struct Outcome { bool accepted = false; bool valid = true; std::string what; };   // the loader's verdict; valid=false with a reason = an invariant was broken
struct Report { size_t iterations = 0, accepted = 0, rejected = 0, failures = 0; size_t worstPeak = 0, worstSingle = 0; double seconds = 0, slowest = 0; size_t slowestIteration = 0; };

inline unsigned long long envNumber(const char* name, unsigned long long fallback) {
    const char* v = std::getenv(name);
    return v && *v ? std::strtoull(v, nullptr, 10) : fallback;
}

// Crash and hang evidence: the iteration number and the input that was running.
inline std::atomic<long long> g_iteration{ -1 };
inline std::atomic<long long> g_iterationStartNs{ 0 };
inline const std::vector<uint8_t>* g_currentInput = nullptr;
inline const char* g_currentName = "";
inline long long nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline void dumpCurrent(const char* why) {
    std::fprintf(stderr, "GLB fuzz %s: %s, iteration %lld (replay with STARLIGHT_GLB_FUZZ_START=%lld)\n", why, g_currentName, g_iteration.load(), g_iteration.load());
    if (g_currentInput) {
        if (FILE* f = std::fopen("glb-fuzz-last-input.glb", "wb")) { std::fwrite(g_currentInput->data(), 1, g_currentInput->size(), f); std::fclose(f); std::fprintf(stderr, "  input saved to glb-fuzz-last-input.glb in the working directory\n"); }
    }
    std::fflush(stderr);
}
#if defined(_WIN32)
inline LONG WINAPI crashFilter(EXCEPTION_POINTERS*) { dumpCurrent("CRASH"); return EXCEPTION_CONTINUE_SEARCH; }
#endif

// Runs `iterations` mutated inputs through `load` (Outcome(const std::vector<uint8_t>&)). Returns the report; the caller requires failures == 0.
template <class Load>
Report run(const char* name, const std::vector<Seed>& seeds, size_t iterations, uint64_t baseSeed, const std::filesystem::path& scratch, Load&& load) {
    Report rep;
    const size_t first = (size_t)envNumber("STARLIGHT_GLB_FUZZ_START", 0);
    const bool trace = envNumber("STARLIGHT_GLB_FUZZ_TRACE", 0) != 0, save = envNumber("STARLIGHT_GLB_FUZZ_SAVE", 0) != 0;
    std::filesystem::create_directories(scratch);
    g_currentName = name;
#if defined(_WIN32)
    SetUnhandledExceptionFilter(&crashFilter);
#endif
    std::atomic<bool> done{ false };
    std::thread watchdog([&] {
        while (!done.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const long long started = g_iterationStartNs.load();
            if (started && double(nowNs() - started) / 1e9 > kIterationSeconds) { dumpCurrent("HANG"); std::_Exit(7); }
        }
    });
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = first; i < first + iterations; ++i) {
        std::vector<uint8_t> input = inputFor(seeds, baseSeed, i);
        g_currentInput = &input;
        g_iteration.store((long long)i);
        if (trace) { std::fprintf(stderr, "%s iteration %zu (%zu bytes)\n", name, i, input.size()); std::fflush(stderr); }
        if (save) std::ofstream(scratch / ("glb-fuzz-" + std::string(name) + "-" + std::to_string(i) + ".glb"), std::ios::binary).write(reinterpret_cast<const char*>(input.data()), (std::streamsize)input.size());
        Outcome out;
        std::string problem;
        g_alloc = AllocState();
        g_alloc.armed = true;
        g_iterationStartNs.store(nowNs());
        try { out = load(input); }
        catch (const std::exception& e) { out.valid = false; out.what = std::string("an exception escaped the loader: ") + e.what(); }
        catch (...) { out.valid = false; out.what = "a non-standard exception escaped the loader"; }
        const double took = double(nowNs() - g_iterationStartNs.load()) / 1e9;
        g_iterationStartNs.store(0);
        if (took > rep.slowest) { rep.slowest = took; rep.slowestIteration = i; }
        g_alloc.armed = false;
        const size_t peak = (size_t)std::max<long long>(g_alloc.peak, 0), single = g_alloc.biggest;
        rep.worstPeak = std::max(rep.worstPeak, peak);
        rep.worstSingle = std::max(rep.worstSingle, single);
        if (!out.valid) problem = out.what.empty() ? "an invariant failed" : out.what;
        else if (g_alloc.refused || single > kSingleAllocCap) problem = "one allocation of " + std::to_string(single) + " bytes (cap " + std::to_string(kSingleAllocCap) + ") for a " + std::to_string(input.size()) + "-byte input";
        else if (peak > kPeakCap) problem = "peak " + std::to_string(peak) + " live bytes (cap " + std::to_string(kPeakCap) + ") for a " + std::to_string(input.size()) + "-byte input";
        ++rep.iterations;
        (out.accepted ? rep.accepted : rep.rejected)++;
        if (!problem.empty()) {
            ++rep.failures;
            if (rep.failures <= 8) {
                const auto file = scratch / ("glb-fuzz-" + std::string(name) + "-" + std::to_string(i) + ".glb");
                std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(input.data()), (std::streamsize)input.size());
                std::cout << "  GLB fuzz " << name << " iteration " << i << " (seed corpus entry " << seeds[i % seeds.size()].name << "): " << problem << "; input saved to " << file.string() << "\n";
            }
        }
    }
    done.store(true);
    watchdog.join();
    g_currentInput = nullptr;
    rep.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "  GLB fuzz " << name << ": " << rep.iterations << " inputs, " << rep.accepted << " loaded, " << rep.rejected << " rejected, " << rep.failures << " failures, worst single allocation "
              << rep.worstSingle << " bytes, worst live peak " << rep.worstPeak << " bytes (caps " << kSingleAllocCap << " / " << kPeakCap << "), " << rep.seconds << " s (slowest input: iteration " << rep.slowestIteration << ", " << rep.slowest << " s)\n";
    return rep;
}

}   // namespace glbfuzz

void* operator new(size_t n) { return glbfuzz::trackedAlloc(n); }
void* operator new[](size_t n) { return glbfuzz::trackedAlloc(n); }
void operator delete(void* p) noexcept { glbfuzz::trackedFree(p); }
void operator delete[](void* p) noexcept { glbfuzz::trackedFree(p); }
void operator delete(void* p, size_t) noexcept { glbfuzz::trackedFree(p); }
void operator delete[](void* p, size_t) noexcept { glbfuzz::trackedFree(p); }
