#pragma once
#define NOMINMAX
#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include "d3d12_descriptors.h"
#include "event_log.h"
#include <dxgi1_6.h>
#include <dxgidebug.h>
static const GUID kDxgiDebugAll = { 0xe48ae283, 0xda80, 0x490b, { 0x87, 0xe6, 0x43, 0xe9, 0xa9, 0xcf, 0xda, 0x8 } };
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <cmath>
#include <memory>
#include <functional>

// upscaler / frame-generation backends (upscale\*.cpp). One path is chosen at
// startup (--upscaler auto|dlss|fsr|xess|off); the scene renders to offscreen color+depth+mvec
// at the mode's render size, the backend reconstructs to output size, the renderer copies to
// the backbuffer. --bench <ms> + --report <json> make the client its own measurement program.
#include "upscale/upscale.h"
#include "latency/antilag2.h"
#include "model/skinned.h"
#include "model/texture.h"
#include "gen/gen_config.h"


// cgltf is a vendored single-header C99 loader; silence its CRT-deprecation (fopen/strncpy) noise
// so our own build stays warning-clean. Do not edit the vendored header.
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(push)
#pragma warning(disable: 4996)
#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"
#pragma warning(pop)

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "ws2_32.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;

// three frames in flight. Every per-frame GPU-read resource (command allocator, instance
// CB ring, joint palettes) exists once per slot; the CPU only waits when the slot it is about
// to reuse has not retired yet (--inflight caps how many frames may be queued, 1 = the old
// fully-synchronous behaviour, for A/B numbers).
static const UINT kFrameCount = 3;
static const UINT kMaxInstances = 8192;  // per-frame constant-buffer slots (512B each). Each skinned char uses ~16 (submesh draws), so this is the real crowd ceiling: 8192/16 ~= 500 chars. Was 512 (=32 chars) - a hidden crowd cap, not a HW limit.
static const UINT kCbStride = 512;
static const UINT kFrameCbBytes = 2048;   // per-frame lights/camera block (FrameCB) after the instance slots
static const UINT kMaxSkinned = STARLIGHT_MAX_SKINNED;   // skinned characters per frame (joint palettes), flags.json limits
static const UINT kMaxInstEntries = 262144;            // per-frame instanced skinned-submesh entries (chars x shown submeshes); the t7 Inst ring
static const UINT kFarPoses = 24;                      // shared phase-offset pose palettes PER MODEL for far-LOD crowd (pose-LOD: ~100k solves -> ~100)
static const UINT kMaxJoints = STARLIGHT_MAX_JOINTS;     // palette entries per character (rig v1 29, rig v2 197), flags.json limits
static const UINT kPaletteBytes = kMaxJoints * 64;
static const UINT kTexPerSet = 6;        // SRVs per character texture set (t1..t6)

// ---- multicore job pool (v1): parallel_for over [0,count) across PHYSICAL cores; main thread helps; serial below a floor ----
static const int kPoolFloor = 2000;      // below this item count, run serial (spawn/coord overhead not worth it — measured)
static const int kPoolMinChunk = 512;    // min items per worker so we don't oversubscribe tiny work
static unsigned physicalCoreCount() {
    DWORD len = 0; GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);   // OS topology: PHYSICAL cores, not logical/HT
    if (len) {
        std::vector<uint8_t> buf(len);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buf.data(), &len)) {
            unsigned cores = 0; uint8_t* p = buf.data(); uint8_t* end = p + len;
            while (p < end) { auto* info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)p; if (info->Relationship == RelationProcessorCore) ++cores; p += info->Size; }
            if (cores) return cores;
        }
    }
    unsigned hc = std::thread::hardware_concurrency(); return hc ? std::max(1u, hc / 2) : 1;   // fallback: assume 2-way HT
}
// fn(lo, hi, chunkIdx) — chunkIdx in [0,T) lets each worker own a disjoint scratch/bucket set (main = 0)
static void parallelFor(int count, unsigned nthreads, const std::function<void(int, int, int)>& fn) {
    if (nthreads <= 1 || count < kPoolFloor) { if (count > 0) fn(0, count, 0); return; }   // serial fallback below the granularity floor
    unsigned T = std::min(nthreads, (unsigned)((count + kPoolMinChunk - 1) / kPoolMinChunk));
    if (T <= 1) { fn(0, count, 0); return; }
    int per = (count + (int)T - 1) / (int)T;
    std::vector<std::thread> th; th.reserve(T - 1);
    for (unsigned k = 1; k < T; ++k) { int lo = (int)k * per, hi = std::min(count, lo + per); if (lo < hi) th.emplace_back(fn, lo, hi, (int)k); }
    fn(0, std::min(count, per), 0);   // main thread does chunk 0 (helps, per the design)
    for (auto& t : th) t.join();      // barrier
}
static_assert(STARLIGHT_MAX_JOINTS <= 256, "vertex joint indices are uint8");

struct Vertex { float px, py, pz; float nx, ny, nz; };

// Skin-aware vertex-cluster decimator: snap verts to a grid, merge each cell to one vert (averaged pos/nrm/uv,
// DOMINANT top-4 joints by summed weight so skinning survives), rebuild tris dropping degenerates. Crude but
// only ever used on FAR characters where the reduction is imperceptible. gridN sets the LOD coarseness.
static model::SkinnedModel decimateSkinned(const model::SkinnedModel& in, int gridN) {
    using namespace DirectX;
    model::SkinnedModel out;
    out.joints = in.joints; out.clips = in.clips; out.bbMin = in.bbMin; out.bbMax = in.bbMax;
    const XMFLOAT3 mn = in.bbMin, mx = in.bbMax;
    const float ext = (std::max)({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, 1e-4f });
    const float cell = ext / (float)(std::max)(1, gridN);
    auto cellOf = [&](const model::SkinVertex& v) -> uint64_t {
        uint32_t cx = (uint32_t)(int)((v.px - mn.x) / cell), cy = (uint32_t)(int)((v.py - mn.y) / cell), cz = (uint32_t)(int)((v.pz - mn.z) / cell);
        return ((uint64_t)cx << 42) ^ ((uint64_t)cy << 21) ^ (uint64_t)cz;
    };
    struct Acc { double px = 0, py = 0, pz = 0, nx = 0, ny = 0, nz = 0, u = 0, v = 0; float jw[64] = { 0 }; int n = 0; uint32_t outIdx = 0; };
    std::unordered_map<uint64_t, Acc> cells;
    for (const auto& v : in.verts) {
        Acc& a = cells[cellOf(v)];
        a.px += v.px; a.py += v.py; a.pz += v.pz; a.nx += v.nx; a.ny += v.ny; a.nz += v.nz; a.u += v.u; a.v += v.v; a.n++;
        for (int k = 0; k < 4; ++k) if (v.j[k] < 64) a.jw[v.j[k]] += v.w[k];
    }
    out.verts.reserve(cells.size());
    for (auto& kv : cells) {
        Acc& a = kv.second; float inv = a.n ? 1.0f / a.n : 1.0f;
        model::SkinVertex v{};
        v.px = (float)(a.px * inv); v.py = (float)(a.py * inv); v.pz = (float)(a.pz * inv);
        XMVECTOR nn = XMVector3Normalize(XMVectorSet((float)a.nx, (float)a.ny, (float)a.nz, 0)); XMFLOAT3 nf; XMStoreFloat3(&nf, nn);
        v.nx = nf.x; v.ny = nf.y; v.nz = nf.z; v.tx = 1; v.ty = 0; v.tz = 0; v.tw = 1; v.u = (float)(a.u * inv); v.v = (float)(a.v * inv);
        int best[4] = { 0,0,0,0 }; float bw[4] = { 0,0,0,0 };
        for (int j = 0; j < 64; ++j) { float w = a.jw[j]; if (w <= 0) continue;
            for (int sI = 0; sI < 4; ++sI) if (w > bw[sI]) { for (int t = 3; t > sI; --t) { bw[t] = bw[t - 1]; best[t] = best[t - 1]; } bw[sI] = w; best[sI] = j; break; } }
        float sum = bw[0] + bw[1] + bw[2] + bw[3]; if (sum <= 0) { bw[0] = 1; sum = 1; }
        for (int k = 0; k < 4; ++k) { v.j[k] = (uint8_t)best[k]; v.w[k] = bw[k] / sum; }
        kv.second.outIdx = (uint32_t)out.verts.size();
        out.verts.push_back(v);
    }
    for (size_t i = 0; i + 2 < in.indices.size(); i += 3) {
        uint32_t r0 = cells[cellOf(in.verts[in.indices[i]])].outIdx, r1 = cells[cellOf(in.verts[in.indices[i + 1]])].outIdx, r2 = cells[cellOf(in.verts[in.indices[i + 2]])].outIdx;
        if (r0 == r1 || r1 == r2 || r0 == r2) continue;
        out.indices.push_back(r0); out.indices.push_back(r1); out.indices.push_back(r2);
    }
    model::Submesh sm; sm.firstIndex = 0; sm.indexCount = (uint32_t)out.indices.size(); out.subs.push_back(sm);
    return out;
}

struct SceneCB {
    XMFLOAT4X4 mvp;        // JITTERED model-view-proj (rasterization)
    XMFLOAT4X4 model;
    XMFLOAT4X4 mvpNoJit;   // unjittered current MVP  } motion vectors = prevPixel - curPixel
    XMFLOAT4X4 prevMvp;    // unjittered previous MVP }  (object + camera motion)
    XMFLOAT4   mvScale;    // xy = (0.5*renderW, -0.5*renderH): NDC delta -> pixels, y-down
    XMFLOAT4   baseColor;  // rgb albedo (sRGB) or texture tint; a = emissive mix (0 lit .. 1 unlit/glow)
    XMFLOAT4   params;     // x = roughness (untextured), y = 1 when the PS samples textures, z = metallic, w unused
    XMFLOAT4   fur0;       // shell fur (fur draws only): x = length (m), y = softness, z = stiffness, w = sheen
    XMFLOAT4   fur1;       // x = sway, y = strand cell density (cells per uv unit), z = shell count, w = time (s)
};
static_assert(sizeof(SceneCB) <= kCbStride, "SceneCB must fit one CB slot");

// per-instance record for cross-character instancing (t7). Mirrors HLSL struct Inst = SceneCB fields + paletteBase.
struct InstData { SceneCB s; UINT paletteBase; UINT _pad[3]; };
static_assert(sizeof(InstData) == sizeof(SceneCB) + 16, "InstData must match HLSL Inst layout");

// per-character fur profile, picked by look.sex
struct FurProfile { float length, softness, stiffness, sheen, sway, density; };



// per-frame block (b1): the lights + camera + fog, shared by every draw of the frame. The three
// follow-spotlights are real cone lights shared by draws in the frame.
struct SpotLight { XMFLOAT4 pos; XMFLOAT4 dir; XMFLOAT4 color; };   // pos.w = range, dir.w = cos outer, color.w = cos inner
struct FrameCB {
    XMFLOAT4   lightDir;   // xyz world-space direction the key light travels, w = intensity
    XMFLOAT4   ambient;    // rgb sky/ambient (sRGB), w = ground bounce factor
    XMFLOAT4   camPos;     // xyz camera world pos (specular + fog distance)
    XMFLOAT4   fog;        // rgb fog color (sRGB), w = fog density (0..1)
    SpotLight  spots[32];
};

static_assert(sizeof(FrameCB)<=kFrameCbBytes,"Frame constant buffer capacity exceeded");

// ---- dev log (wire / phase evidence; --log <file>) ---------------------------------------------
// Called from the render thread AND the ws reader thread, so it is mutex-guarded. No-op unless
// a log path was given on the command line.
static std::mutex  g_logMu;
static FILE*       g_logFile = nullptr;
static uint64_t    g_logT0 = 0;
static void logLine(const std::string& s) {
    std::lock_guard<std::mutex> lk(g_logMu);
    if (!g_logFile) return;
    uint64_t ms = GetTickCount64() - g_logT0;
    std::fprintf(g_logFile, "[%6llu.%03llu] %s\n", (unsigned long long)(ms / 1000), (unsigned long long)(ms % 1000), s.c_str());
    std::fflush(g_logFile);
}

// ---- gear colorways -> rgb (server COLORWAYS set + #rrggbb; empty = neutral placeholder) -------


struct Mesh {
    std::vector<Vertex>   verts;
    std::vector<uint32_t> indices;
    XMFLOAT3 bbMin{ 0,0,0 }, bbMax{ 0,0,0 };
    bool loaded = false;
    std::string source;
};

struct GpuMesh {
    ComPtr<ID3D12Resource>   vb, ib;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW  ibv{};
    UINT                     indexCount = 0;
};

// ---- glTF / GLB loading via cgltf -------------------------------------------------------------
static bool LoadGLB(const std::wstring& path, Mesh& out, std::string& err) {
    std::string upath;
    { int n = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
      upath.resize(n > 0 ? n - 1 : 0);
      WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, upath.data(), n, nullptr, nullptr); }
    out.source = upath;

    cgltf_options opt{};
    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse_file(&opt, upath.c_str(), &data);
    if (r != cgltf_result_success) { err = "cgltf_parse_file failed (code " + std::to_string((int)r) + ") for " + upath; return false; }
    r = cgltf_load_buffers(&opt, data, upath.c_str());
    if (r != cgltf_result_success) { cgltf_free(data); err = "cgltf_load_buffers failed (code " + std::to_string((int)r) + ")"; return false; }

    bool first = true;
    auto grow = [&](const XMFLOAT3& p) {
        if (first) { out.bbMin = out.bbMax = p; first = false; return; }
        out.bbMin.x = std::min(out.bbMin.x, p.x); out.bbMin.y = std::min(out.bbMin.y, p.y); out.bbMin.z = std::min(out.bbMin.z, p.z);
        out.bbMax.x = std::max(out.bbMax.x, p.x); out.bbMax.y = std::max(out.bbMax.y, p.y); out.bbMax.z = std::max(out.bbMax.z, p.z);
    };

    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        float world[16];
        cgltf_node_transform_world(node, world);
        XMMATRIX W = XMMatrixSet(
            world[0], world[1], world[2], world[3],
            world[4], world[5], world[6], world[7],
            world[8], world[9], world[10], world[11],
            world[12], world[13], world[14], world[15]);
        XMMATRIX Wn = XMMatrixTranspose(XMMatrixInverse(nullptr, W));

        cgltf_mesh* mesh = node->mesh;
        for (cgltf_size pi = 0; pi < mesh->primitives_count; ++pi) {
            cgltf_primitive* prim = &mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor* pos = nullptr; const cgltf_accessor* nrm = nullptr;
            for (cgltf_size a = 0; a < prim->attributes_count; ++a) {
                if (prim->attributes[a].type == cgltf_attribute_type_position) pos = prim->attributes[a].data;
                else if (prim->attributes[a].type == cgltf_attribute_type_normal) nrm = prim->attributes[a].data;
            }
            if (!pos) continue;

            uint32_t base = (uint32_t)out.verts.size();
            cgltf_size vcount = pos->count;
            for (cgltf_size v = 0; v < vcount; ++v) {
                float p[3] = { 0,0,0 }, nn[3] = { 0,1,0 };
                cgltf_accessor_read_float(pos, v, p, 3);
                if (nrm) cgltf_accessor_read_float(nrm, v, nn, 3);
                XMVECTOR wp = XMVector3Transform(XMVectorSet(p[0], p[1], p[2], 1.0f), W);
                XMVECTOR wn = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(nn[0], nn[1], nn[2], 0.0f), Wn));
                XMFLOAT3 fp; XMStoreFloat3(&fp, wp);
                XMFLOAT3 fn; XMStoreFloat3(&fn, wn);
                Vertex vert{ fp.x, fp.y, fp.z, fn.x, fn.y, fn.z };
                out.verts.push_back(vert);
                grow(fp);
            }

            if (prim->indices) {
                cgltf_size ic = prim->indices->count;
                for (cgltf_size i = 0; i < ic; ++i)
                    out.indices.push_back(base + (uint32_t)cgltf_accessor_read_index(prim->indices, i));
            } else {
                for (cgltf_size i = 0; i < vcount; ++i) out.indices.push_back(base + (uint32_t)i);
            }
        }
    }

    cgltf_free(data);
    if (out.verts.empty() || out.indices.empty()) { err = "no triangle geometry with POSITION found in " + upath; return false; }
    out.loaded = true;
    return true;
}

// ---- procedural primitive meshes --------------------------------------------------------------
// Cone: apex at (0,1,0), base radius 0.5 on y=0. Used (scaled/tinted) for craxis/boss/dummies/
// fires/proj markers. Side + base cap, per-vertex normals good enough for N.L shading.
static void BuildCone(Mesh& m, int seg = 20) {
    const float R = 0.5f, H = 1.0f;
    XMFLOAT3 apex{ 0, H, 0 };
    for (int i = 0; i < seg; ++i) {
        float a0 = (float)i / seg * XM_2PI, a1 = (float)(i + 1) / seg * XM_2PI;
        XMFLOAT3 p0{ cosf(a0) * R, 0, sinf(a0) * R };
        XMFLOAT3 p1{ cosf(a1) * R, 0, sinf(a1) * R };
        // side normal ~ average of the two rim outward+up
        auto sideN = [&](const XMFLOAT3& p) {
            XMVECTOR n = XMVector3Normalize(XMVectorSet(p.x, R * 0.6f, p.z, 0));
            XMFLOAT3 o; XMStoreFloat3(&o, n); return o; };
        XMFLOAT3 n0 = sideN(p0), n1 = sideN(p1), na = sideN(XMFLOAT3{ (p0.x + p1.x) * 0.5f, 0, (p0.z + p1.z) * 0.5f });
        uint32_t b = (uint32_t)m.verts.size();
        m.verts.push_back({ p0.x,p0.y,p0.z, n0.x,n0.y,n0.z });
        m.verts.push_back({ p1.x,p1.y,p1.z, n1.x,n1.y,n1.z });
        m.verts.push_back({ apex.x,apex.y,apex.z, na.x,na.y,na.z });
        m.indices.push_back(b); m.indices.push_back(b + 2); m.indices.push_back(b + 1);
        // base cap (facing down)
        uint32_t c = (uint32_t)m.verts.size();
        m.verts.push_back({ p1.x,0,p1.z, 0,-1,0 });
        m.verts.push_back({ p0.x,0,p0.z, 0,-1,0 });
        m.verts.push_back({ 0,0,0, 0,-1,0 });
        m.indices.push_back(c); m.indices.push_back(c + 2); m.indices.push_back(c + 1);
    }
    m.loaded = true;
}

// Flat annulus on the y=0 plane (outer radius 1, inner `inner`). Telegraph ring on the floor
// (0.82), and a thin variant (0.96) for the neon edge of the dancefloor.
static void BuildRing(Mesh& m, int seg = 64, float inner = 0.82f) {
    const float Ro = 1.0f, Ri = inner;
    for (int i = 0; i < seg; ++i) {
        float a0 = (float)i / seg * XM_2PI, a1 = (float)(i + 1) / seg * XM_2PI;
        XMFLOAT3 o0{ cosf(a0) * Ro, 0, sinf(a0) * Ro }, o1{ cosf(a1) * Ro, 0, sinf(a1) * Ro };
        XMFLOAT3 i0{ cosf(a0) * Ri, 0, sinf(a0) * Ri }, i1{ cosf(a1) * Ri, 0, sinf(a1) * Ri };
        uint32_t b = (uint32_t)m.verts.size();
        m.verts.push_back({ i0.x,0,i0.z, 0,1,0 });
        m.verts.push_back({ o0.x,0,o0.z, 0,1,0 });
        m.verts.push_back({ o1.x,0,o1.z, 0,1,0 });
        m.verts.push_back({ i1.x,0,i1.z, 0,1,0 });
        m.indices.push_back(b); m.indices.push_back(b + 1); m.indices.push_back(b + 2);
        m.indices.push_back(b); m.indices.push_back(b + 2); m.indices.push_back(b + 3);
    }
    m.loaded = true;
}

// Unit box centered on the origin (half-extent 0.5), 6 faces with flat normals. Room greybox
// (floor / walls / ceiling / bar / pillars / neon strips) and most gear placeholders.
static void BuildBox(Mesh& m) {
    const XMFLOAT3 N[6] = { {1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1} };
    for (int f = 0; f < 6; ++f) {
        XMFLOAT3 n = N[f];
        // pick two tangent axes for this face
        XMFLOAT3 u = (n.y != 0) ? XMFLOAT3{ 1,0,0 } : XMFLOAT3{ 0,1,0 };
        XMVECTOR nv = XMLoadFloat3(&n), uv = XMLoadFloat3(&u);
        XMVECTOR vv = XMVector3Cross(nv, uv);
        XMFLOAT3 v; XMStoreFloat3(&v, vv);
        uint32_t b = (uint32_t)m.verts.size();
        const float su[4] = { -0.5f, 0.5f, 0.5f, -0.5f }, sv[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
        for (int k = 0; k < 4; ++k) {
            float px = n.x * 0.5f + u.x * su[k] + v.x * sv[k];
            float py = n.y * 0.5f + u.y * su[k] + v.y * sv[k];
            float pz = n.z * 0.5f + u.z * su[k] + v.z * sv[k];
            m.verts.push_back({ px,py,pz, n.x,n.y,n.z });
        }
        m.indices.push_back(b); m.indices.push_back(b + 1); m.indices.push_back(b + 2);
        m.indices.push_back(b); m.indices.push_back(b + 2); m.indices.push_back(b + 3);
    }
    m.loaded = true;
}

// Flat unit disc on the y=0 plane, normal +Y. Spotlight floor pools + the tattoo fallback.
static void BuildDisc(Mesh& m, int seg = 48) {
    uint32_t c = (uint32_t)m.verts.size();
    m.verts.push_back({ 0,0,0, 0,1,0 });
    for (int i = 0; i <= seg; ++i) {
        float a = (float)i / seg * XM_2PI;
        m.verts.push_back({ cosf(a), 0, sinf(a), 0,1,0 });
    }
    for (int i = 0; i < seg; ++i) { m.indices.push_back(c); m.indices.push_back(c + 1 + i); m.indices.push_back(c + 2 + i); }
    m.loaded = true;
}

// Cylinder: radius 1, y in [0,1], side + both caps. The raised dancefloor slab.
static void BuildCylinder(Mesh& m, int seg = 48) {
    for (int i = 0; i < seg; ++i) {
        float a0 = (float)i / seg * XM_2PI, a1 = (float)(i + 1) / seg * XM_2PI;
        float x0 = cosf(a0), z0 = sinf(a0), x1 = cosf(a1), z1 = sinf(a1);
        uint32_t b = (uint32_t)m.verts.size();
        m.verts.push_back({ x0,0,z0, x0,0,z0 }); m.verts.push_back({ x0,1,z0, x0,0,z0 });
        m.verts.push_back({ x1,1,z1, x1,0,z1 }); m.verts.push_back({ x1,0,z1, x1,0,z1 });
        m.indices.push_back(b); m.indices.push_back(b + 1); m.indices.push_back(b + 2);
        m.indices.push_back(b); m.indices.push_back(b + 2); m.indices.push_back(b + 3);
        uint32_t c = (uint32_t)m.verts.size();
        m.verts.push_back({ 0,1,0, 0,1,0 }); m.verts.push_back({ x0,1,z0, 0,1,0 }); m.verts.push_back({ x1,1,z1, 0,1,0 });
        m.indices.push_back(c); m.indices.push_back(c + 1); m.indices.push_back(c + 2);
        uint32_t d = (uint32_t)m.verts.size();
        m.verts.push_back({ 0,0,0, 0,-1,0 }); m.verts.push_back({ x1,0,z1, 0,-1,0 }); m.verts.push_back({ x0,0,z0, 0,-1,0 });
        m.indices.push_back(d); m.indices.push_back(d + 1); m.indices.push_back(d + 2);
    }
    m.loaded = true;
}

// Flat heart on the y=0 plane (normal +Y), ~unit size, lobes toward +Z. Tattoo design 0 = heart.
// Classic parametric heart curve, triangle-fanned from the center (the curve is star-shaped
// from the origin so the fan is valid).
static void BuildHeart(Mesh& m, int seg = 64) {
    uint32_t c = (uint32_t)m.verts.size();
    m.verts.push_back({ 0,0,0, 0,1,0 });
    for (int i = 0; i <= seg; ++i) {
        float t = (float)i / seg * XM_2PI;
        float st = sinf(t);
        float hx = 16.0f * st * st * st;
        float hy = 13.0f * cosf(t) - 5.0f * cosf(2 * t) - 2.0f * cosf(3 * t) - cosf(4 * t);
        m.verts.push_back({ hx / 17.0f, 0, hy / 17.0f, 0,1,0 });
    }
    for (int i = 0; i < seg; ++i) { m.indices.push_back(c); m.indices.push_back(c + 1 + i); m.indices.push_back(c + 2 + i); }
    m.loaded = true;
}

// ---- per-entity smoothed transform (interpolation between snapshots) ---------------------------
struct SmoothXf { float x = 0, z = 0, y = 0, yaw = 0; bool init = false; };

#include "persistent_workers.h"
#include "parallel_metrics.h"
struct RenderDevice {
    PersistentWorkers persistentWorkers;
    ParallelMetrics parallelMetrics;
    int persistentWorkerMode = -1; // persistent default; external legacy comparison override
    void parallelFor(int count, unsigned nthreads, const std::function<void(int,int,int)>& fn) {
        if (persistentWorkerMode < 0) {
            wchar_t value[8]{};
            persistentWorkerMode = !(GetEnvironmentVariableW(L"STARLIGHT_EXPERIMENT_PERSISTENT_WORKERS", value, 8) && value[0] == L'0');
        }
        unsigned threads=(nthreads<=1 || count<kPoolFloor)?1u:
            std::min(nthreads,unsigned((count+kPoolMinChunk-1)/kPoolMinChunk));
        parallelMetrics.run(count,threads,[&](const auto& measured){
            if(!persistentWorkerMode)::parallelFor(count,nthreads,measured);
            else if(threads==1){if(count>0)measured(0,count,0);}
            else persistentWorkers.run(count,threads,measured);
        },fn);
    }

    // One request when a project opens; never fight a user's later Alt-Tab.
    static bool activateProjectWindow(HWND window) {
        wchar_t setting[4]{};
        GetEnvironmentVariableW(L"STARLIGHT_FOREGROUND", setting, 4);
        if (setting[0] == L'0') { ShowWindow(window, SW_SHOWNOACTIVATE); return false; }
        ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
        if (SetForegroundWindow(window) && GetForegroundWindow() == window) return true;
        HWND previous = GetForegroundWindow();
        DWORD other = previous ? GetWindowThreadProcessId(previous, nullptr) : 0;
        DWORD current = GetCurrentThreadId();
        bool attached = other && other != current && AttachThreadInput(current, other, TRUE);
        BringWindowToTop(window);
        SetForegroundWindow(window);
        SetActiveWindow(window);
        SetFocus(window);
        if (attached) AttachThreadInput(current, other, FALSE);
        return GetForegroundWindow() == window;
    }

    // Shared native-rendering ceiling. Applies regardless of VSync/benchmark mode.
    unsigned frameRateLimit = 0; // supplied by the selected external settings
    double measuredFenceWaitMs=0,measuredPacingWaitMs=0,measuredPresentWaitMs=0;
    static double timingNow(){LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);return double(q.QuadPart)/f.QuadPart;}
    HANDLE framePacingTimer = nullptr; double paceNaturalMs = 0;
    LONGLONG lastFrameStart = 0, pacingFrequency = 0;
    virtual ~RenderDevice() { if (framePacingTimer) CloseHandle(framePacingTimer); if (frameLatencyWaitable) CloseHandle(frameLatencyWaitable); if (fenceEvent) CloseHandle(fenceEvent); }
    // Audit-only offscreen throughput: retain GPU fences and resource barriers,
    // but do not enter DXGI/DWM presentation or the editor's frame limiter.
    bool auditOffscreen = false;
    ComPtr<ID3D12Resource> auditOutput;
    void setAuditOffscreen(bool enabled) {
        flush();
        if (enabled && !auditOutput) {
            auto desc = starlight_resource_desc(targets[0]);
            desc.Flags = D3D12_RESOURCE_FLAG_NONE;
            D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            must(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                IID_PPV_ARGS(&auditOutput)), "Create audit output");
            auditOutput->SetName(L"Starlight owned offscreen benchmark output");
        }
        auditOffscreen = enabled;
        if (!enabled) auditOutput.Reset();
        frame = swap->GetCurrentBackBufferIndex();
        lastFrameStart = 0;
    }
    void paceFrame() {
        if (auditOffscreen) return;
        if (!frameRateLimit) {
            wchar_t value[32]{};
            if (!GetEnvironmentVariableW(L"STARLIGHT_FRAME_LIMIT", value, 32)) throw std::runtime_error("Frame limit must be supplied by launch settings");
            frameRateLimit = (unsigned)wcstoul(value, nullptr, 10);
            if (!frameRateLimit) throw std::runtime_error("Invalid frame limit in launch settings");
        }
        LARGE_INTEGER now{};
        if (!pacingFrequency) {
            LARGE_INTEGER frequency{};
            if (!QueryPerformanceFrequency(&frequency)) throw std::runtime_error("Frame pacing clock unavailable");
            pacingFrequency = frequency.QuadPart;
            framePacingTimer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002, TIMER_ALL_ACCESS);
            if (!framePacingTimer) framePacingTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
            if (!framePacingTimer) throw std::runtime_error("Frame pacing timer unavailable");
        }
        QueryPerformanceCounter(&now);
        LONGLONG interval = (pacingFrequency + frameRateLimit - 1) / frameRateLimit;
        // Optional (STARLIGHT_PACE_SMOOTH=1): on a variable-refresh OLED panel, slow drift of the frame time shows as brightness flicker in
        // dark scenes. Present at the steady rate the machine can sustain (the configured ceiling is untouched) instead of as fast as each
        // frame happens to finish. The target follows a ~0.5 s average of the natural frame time plus a small margin.
        static const bool paceSmooth = []{ wchar_t v[8]{}; return GetEnvironmentVariableW(L"STARLIGHT_PACE_SMOOTH", v, 8) && v[0] == L'1'; }();
        if (paceSmooth && lastFrameStart) {
            const double natural = double(now.QuadPart - lastFrameStart) / double(pacingFrequency) * 1000.0;
            if (natural > 0 && natural < 100) paceNaturalMs = paceNaturalMs > 0 ? paceNaturalMs * 0.98 + natural * 0.02 : natural;
            const LONGLONG smooth = LONGLONG(paceNaturalMs * 1.04 / 1000.0 * double(pacingFrequency));
            if (smooth > interval) interval = smooth;
        }
        if (lastFrameStart) {
            const double waitStarted=timingNow();
            const LONGLONG deadline = lastFrameStart + interval;
            while (now.QuadPart < deadline) {
                // Avoid putting the thread to sleep for the final fraction of
                // a millisecond, where scheduler wake-up variance exceeds the
                // requested wait. The bounded spin never changes the cap.
                static const LONGLONG spinUs=[]{wchar_t v[16]{};return GetEnvironmentVariableW(L"STARLIGHT_PACE_SPIN_US",v,16)?LONGLONG(_wtoi(v)):LONGLONG(500);}();
                const LONGLONG guard=std::max<LONGLONG>(1,std::min(interval/2,pacingFrequency*spinUs/1000000));
                if(deadline-now.QuadPart<=guard){YieldProcessor();QueryPerformanceCounter(&now);continue;}
                LARGE_INTEGER due{};
                due.QuadPart = -((deadline - now.QuadPart - guard) * 10000000 + pacingFrequency - 1) / pacingFrequency;
                if (!SetWaitableTimer(framePacingTimer, &due, 0, nullptr, nullptr, FALSE) ||
                    WaitForSingleObject(framePacingTimer, INFINITE) != WAIT_OBJECT_0)
                    throw std::runtime_error("Frame pacing wait failed");
                QueryPerformanceCounter(&now);
            }
            measuredPacingWaitMs+=(timingNow()-waitStarted)*1000;
        }
        // Reset to actual start: never catch up with a burst after a slow frame.
        lastFrameStart = now.QuadPart;
    }

    virtual void loadCharacters() {}
    bool hasServer = false;
    int lastPlayers = 0, lastCrowd = 0;
    ComPtr<IDXGIFactory6>        factory;
    ComPtr<ID3D12Device>         device;
    ComPtr<ID3D12CommandQueue>   queue;
    ComPtr<IDXGISwapChain4>      swap;
    ComPtr<IDXGIAdapter1>        adapter;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;   // kFrameCount backbuffers + sceneColor + sceneMvec
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12Resource>       targets[kFrameCount];
    ComPtr<ID3D12Resource>       depth;      // render-size D32 (upscaler input)

    // ---- offscreen scene at render size, reconstructed to output size ----
    std::unique_ptr<up::Upscaler> ups;
    std::string upsRequest = UP_DEFAULT_REQUEST;          // --upscaler
    std::string upsReason;                    // why the active path was chosen (title/report)
    std::string modeRequest = "quality";      // --quality
    bool  fgRequest = false;                  // --fg on
    std::string latencyRequest = "auto";      // --latency auto|reflex|antilag|xell|off
    std::string latencyChoice = "off";        // what was actually selected (report/title)
    std::string latencyWhy;                   // why (report)
    lat::AntiLag2 antilag;                    // path-independent AMD Anti-Lag 2
    bool  vsync = true;                       // --novsync for benchmarks
    bool  tearing = false;                    // swapchain created with ALLOW_TEARING
    bool  d3dDebug = false;                   // --d3ddebug: debug layers on, messages drained to the log
    ComPtr<ID3D12InfoQueue> d3dq; ComPtr<IDXGIInfoQueue> dxgiq;
    UINT  rw = 0, rh = 0;                     // render size (mode-driven); w,h = output size
    bool linearHdrScene=false;
    DXGI_FORMAT sceneFormat()const{return linearHdrScene?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM;}
    ComPtr<ID3D12Resource> sceneColor, sceneMvec, exposureTex, upOutput;
    // Multisampled scene targets. When msaa>1 the scene pass draws into these (the rtv/dsv heap slots point at them) and
    // resolveScene() resolves into sceneColor/sceneMvec/depth, which every later pass keeps reading unchanged.
    UINT msaaRequest = 8, msaa = 1;
    ComPtr<ID3D12Resource> msaaColor, msaaMvec, msaaDepth;
    uint32_t frameIndex = 0;
    bool  resetHistory = true;
    XMMATRIX viewProjNoJit = XMMatrixIdentity(), prevViewProjNoJit = XMMatrixIdentity();
    std::vector<XMFLOAT4X4> curModel, prevModel;   // per-slot model matrices (motion vectors)
    UINT  prevInstCount = 0;
    float jitterX = 0, jitterY = 0;
    int   pendingMode = -1; int pendingFG = -1; bool pendingMult = false;   // F2 / F3 / F4
    // --bench <ms> --report <json>: measure after a 1.5s warm-up, write the results, exit
    uint64_t benchMs = 0; std::string reportPath; std::vector<float> benchFrames; uint64_t benchT0 = 0; uint64_t benchPresented = 0;
    float fpsNow = 0; uint64_t fpsTick = 0; uint32_t fpsFrames = 0;
    // GPU-timestamp perf monitor: true per-frame GPU time, immune to the DWM/present throttle that
    // makes headless (non-foreground) windows read a fake ~20fps. 2 timestamps per in-flight slot.
    ComPtr<ID3D12QueryHeap> tsHeap;
    // Per-pass GPU timing (diagnostic): marks 0 frame start, 1 after shadow maps, 2 after the scene pass, 3 after fog/post, 4 frame end.
    ComPtr<ID3D12QueryHeap> passHeap; ComPtr<ID3D12Resource> passReadback; float lastPassMs[4]{};
    void gpuMark(UINT mark) { if (passHeap && mark < 16) cmd->EndQuery(passHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 16 * slot + mark); }
    ComPtr<ID3D12Resource> tsReadback; UINT64 tsFreq = 0;
    std::vector<float> benchGpuMs; float lastGpuMs = 0; float gpuMsNow = 0;
    UINT benchInstMax = 0;   // peak instance slots used in a bench frame (drop-diagnosis: pins at kMaxInstances when the buffer is the limit)
    std::vector<float> benchCpuRecMs; float lastCpuRecMs = 0;   // CPU wall-time spent RECORDING the frame's draws on this one thread (throttle-immune; the single-thread cost the multicore refactor targets)
    std::vector<float> benchPoseMs; float lastPoseMs = 0; double poseAccumMs = 0;   // of the record cost, the evalPose (per-char animation) portion = the PARALLELIZABLE part
    double profileGatherSum = 0, profileEmitSum = 0, profileMergeSum = 0;
    size_t profileSamples = 0;
    float lastGatherMs = 0, lastEmitMs = 0, lastMergeMs = 0;   // profiling breakdown of the CPU-record wall
    std::string adapterName; UINT adapterVendor = 0;
    ComPtr<ID3D12CommandAllocator> allocs[kFrameCount];   // one per in-flight slot
    ComPtr<ID3D12CommandAllocator> alloc;                  // = allocs[slot] for the frame being recorded (init uses slot 0)
    ComPtr<ID3D12GraphicsCommandList> cmd;
    ComPtr<ID3D12Fence>          fence;
    UINT64 slotFence[kFrameCount] = {};                    // fence value that retires each slot's last submission
    UINT   slot = 0;                                        // = frameIndex % kFrameCount
    bool   warnedBbIndex = false;                          // one-shot: log if the swapchain ever returns an out-of-range backbuffer index (Streamline proxy swapchain, #35)
    int    inflight = 3;                                    // --inflight 1..3
    HANDLE frameLatencyWaitable = nullptr;                  // DXGI frame-latency waitable: waited at frame TOP so present() runs async (CPU records N+1 while GPU+present handle N)
    uint32_t stallCount = 0;                                // frames where the CPU had to wait for a slot (report)
    double   stallMs = 0;
    ComPtr<ID3D12Resource>       palettes;                 // upload heap: kFrameCount x kMaxSkinned x kPaletteBytes
    uint8_t*                     paletteBase = nullptr;
    UINT   skinCursor = 0;                                  // palettes used this frame
    UINT   skinnedDrawn = 0, skinnedDrawnLast = 0;          // skinned characters drawn (title/report)
    // ---- unified cross-character instancing (t7 Inst ring + per-(model,variant,submesh) buckets) ----
    bool instanceGpuUpload = false;
    ComPtr<ID3D12Resource> instData; uint8_t* instBase = nullptr;
    // parallel EMIT: per-chunk dense bucket sets keyed (model,variant,submesh); merged lock-free in flushInstances
    static const UINT kBktVariants = 8, kBktMaxSubs = 32;
    std::vector<std::vector<std::vector<InstData>>> threadBuckets;           // [chunk][bucketIdx] -> instances
    UINT bktStride() const { return kBktVariants * kBktMaxSubs; }
    UINT bktCount()  const { return (UINT)models.size() * bktStride(); }
    UINT bktIndex(UINT cmIdx, UINT variant, UINT sub) const { return (cmIdx * kBktVariants + variant) * kBktMaxSubs + sub; }
    UINT   instEntryCursor = 0, instEntriesLast = 0, drawCallCount = 0, drawCallLast = 0;
    std::unordered_map<uint64_t, XMFLOAT4X4> prevModelByKey, curModelByKey;       // per-character motion-vector history (uint64 key = hashed once; string-keyed didn't scale to 100k)
    static uint64_t keyHash(const std::string& k) { uint64_t h = 1469598103934665603ull; for (unsigned char c : k) { h ^= c; h *= 1099511628211ull; } return h; }
    static uint64_t eid(char ns, const std::string& id) { uint64_t h = 1469598103934665603ull; h ^= (unsigned char)ns; h *= 1099511628211ull; for (unsigned char c : id) { h ^= c; h *= 1099511628211ull; } return h; }
    bool   instancingOn = true;                             // hotkey I: batched instanced draws (on) vs one draw per character (off) — live A/B
    bool   perfHud = true;                                  // hotkey T: live perf HUD in the title bar vs the full client title

    // ---- skinned characters + bakes-v1 textures ----
    struct TexSet { ComPtr<ID3D12Resource> basecolor, normal, roughness, ao, furflow, sparkle; UINT table = 0; bool ok = false; bool fur = false; };
    struct CharModel {
        model::SkinnedModel data;
        GpuMesh gpu;
        model::SkinnedModel lodData;   // decimated far-LOD (skin-aware); rendered only for tiny on-screen chars
        GpuMesh lodGpu; bool hasLod = false; size_t lodVerts = 0;
        model::SkinnedModel lodData2;  // TIER-2 ultra-coarse LOD (skin-aware); only for sub-~15px specks (few pixels = imperceptible)
        GpuMesh lodGpu2; bool hasLod2 = false; size_t lodVerts2 = 0;
        std::vector<TexSet> variants;    // v0..v3 basecolor/roughness (normal + ao shared, held in each)
        int clipIdle = -1, clipWalk = -1, clipRun = -1, clipJump = -1, clipDodge = -1, clipCharge = -1, clipFire = -1, clipSway = -1;
        int jHead = -1, jShoulderL = -1, jShoulderR = -1, jHandL = -1, jHandR = -1, jFootL = -1, jFootR = -1;
        float scale = 1.0f;              // to ~1.8 m
    };
    std::vector<CharModel> models;       // [0] = lead (players), others rotate for crowd/npcs
    std::unordered_map<std::string, int> modelByName;
    // ---- multicore collect: gather (serial) -> parallel per-char pose -> serial emit (DrawItem needs CharModel, defined above) ----
    struct DrawItem {
        CharModel* cm = nullptr; UINT pslot = 0;
        int clip = 0; float t = 0; int prevClip = -1; float prevT = 0; float blend = 0;   // evalPose inputs
        SceneCB base{};                                     // per-char transforms (computed in gather; emit copies + sets material)
        const void* look = nullptr; int variant = 0;
        XMFLOAT4X4 bodyF{};                                 // pre-scale body (drawLook attach)
        std::vector<XMFLOAT4X4> joints;                     // world joints for drawLook (filled iff look present)
        bool lod = false;                                   // far character -> render the decimated LOD mesh (tier1); implied true when lod2
        bool lod2 = false;                                  // very-far speck -> render the ultra-coarse tier-2 mesh (sub-~15px)
        bool valid = true;                                  // false = culled/skipped; phases B/C ignore it (index-stable parallel gather)
    };
    bool   lodOn = true;                                    // hotkey L: far-LOD meshes on/off — live A/B
    UINT   lodDrawn = 0, lodDrawnLast = 0;                  // characters rendered at far-LOD this frame
    bool   parallelGather = true;                           // hotkey G: parallel crowd gather on/off — live A/B
    std::vector<DrawItem> items;                            // gathered this frame; posed in parallel; emitted serially
    unsigned poolThreads = 1;                               // physical cores (set in init)
    bool   parallelPose = true;                             // hotkey P: parallel per-char pose (multicore) vs serial — live A/B
    // ---- frustum culling: skip characters the camera can't see (cuts CPU pose/emit AND GPU draw/shade) ----
    XMFLOAT4 frustum[6]{};                                  // camera planes (from viewProjNoJit), inward normals
    bool   cullOn = true;                                   // hotkey C: frustum culling on/off — live A/B
    UINT   culled = 0, culledLast = 0;                      // characters culled this frame
    void computeFrustum(const XMMATRIX& m) {                // Gribb-Hartmann from a row-major (row-vector) view-proj
        XMFLOAT4X4 M; XMStoreFloat4x4(&M, m);
        auto set = [&](int i, float a, float b, float c, float d) {
            float n = sqrtf(a*a + b*b + c*c); if (n < 1e-8f) n = 1.0f;
            frustum[i] = XMFLOAT4(a/n, b/n, c/n, d/n);
        };
        set(0, M._14+M._11, M._24+M._21, M._34+M._31, M._44+M._41);   // left
        set(1, M._14-M._11, M._24-M._21, M._34-M._31, M._44-M._41);   // right
        set(2, M._14+M._12, M._24+M._22, M._34+M._32, M._44+M._42);   // bottom
        set(3, M._14-M._12, M._24-M._22, M._34-M._32, M._44-M._42);   // top
        set(4, M._13, M._23, M._33, M._43);                           // near (z in [0,1])
        set(5, M._14-M._13, M._24-M._23, M._34-M._33, M._44-M._43);   // far
    }
    bool sphereVisible(float x, float y, float z, float r) const {
        for (int i = 0; i < 6; ++i) { const XMFLOAT4& p = frustum[i]; if (p.x*x + p.y*y + p.z*z + p.w < -r) return false; }
        return true;
    }
    ComPtr<ID3D12DescriptorHeap> srvHeap; UINT srvSize = 0; UINT srvCount = 0;
    ComPtr<ID3D12PipelineState>  psoSkin, psoFur;
    std::string modelsDir, texturesDir;  // --models / --textures (defaults under build/)
    UINT   texRes = 2048;                // --texres
    bool   noChars = false;              // --nochars: keep the M5 primitives (A/B)
    bool   noFur = false;                // --nofur: skip the GPU shell fur (A/B)
    bool   flatLight = false;            // --flatlight: neutral light, spots off (texture/colour QA)
    float  camDist = 9.0f, camHigh = 5.0f;   // --camdist / --camhigh follow-camera offsets (close-up shots)
    bool   hideRoom = false;                  // --stress: skip the greybox room box so it can't occlude the pulled-back crowd view
    UINT   furShellsDrawn = 0, furShellsLast = 0;   // shells drawn this frame / last presented (report)
    size_t textureBytes = 0; int texturesLoaded = 0, texturesFromCache = 0; double modelLoadMs = 0, textureLoadMs = 0;
    std::string charsWhy;                // why characters are off (report)
    struct AnimState { int clip = -1; float t = 0; int prevClip = -1; float prevT = 0; float blend = 0; uint64_t last = 0; };
    std::unordered_map<uint64_t, AnimState> anims;
    // data-oriented PARALLEL crowd gather: per-crowd-index SoA state (no shared maps => lock-free); persistent across frames
    std::vector<SmoothXf> cSmooth; std::vector<AnimState> cAnim;
    std::vector<XMFLOAT4X4> cPrev, cCur;
    std::vector<uint8_t> cTier;   // per-crowd-index last LOD tier (0 full / 1 far / 2 speck) — for hysteresis (no per-frame flicker at boundaries)
    ComPtr<ID3D12RootSignature>  rootSig;
    ComPtr<ID3D12PipelineState>  pso;       // opaque, depth write
    ComPtr<ID3D12PipelineState>  psoAdd;    // additive blend, depth test only (spotlight beams / pools)
    ComPtr<ID3D12PipelineState>  psoBg;     // fullscreen background fill (fog colour + zero mvec), no depth
    ComPtr<ID3D12PipelineState>  psoDepthOnly, psoDepthEqual; // opaque depth pre-pass (no colour) and the colour pass that shades only the surviving samples
    ComPtr<ID3D12Resource>       cb;
    uint8_t*                     cbBase = nullptr;   // persistently-mapped instance CB (kMaxInstances*256)
    struct UploadStorage { ComPtr<ID3D12Resource> resource; void* mapped = nullptr; };
    std::vector<UploadStorage> uploadKeepAlive;
    // Reusable staging storage, never scene geometry. Pending copies remain in
    // uploadKeepAlive until their queue fence has completed.
    std::vector<UploadStorage> uploadPool;
    UINT64 uploadPoolBytes = 0;
    UINT64 uploadCreated = 0, uploadReused = 0;
    struct IdleBuffer { ComPtr<ID3D12Resource> resource; bool accelerationStructure=false; UINT64 width=0,bytes=0; D3D12_RESOURCE_FLAGS flags=D3D12_RESOURCE_FLAG_NONE; };   // width/bytes/flags are cached so pool searches never call GetDesc
    std::vector<IdleBuffer> bufferPool;
    UINT64 bufferPoolBytes=0,bufferCreated=0,bufferReused=0,bufferEvicted=0;
    static constexpr UINT64 bufferPoolLimit=16ull*1024*1024; static constexpr size_t bufferPoolCount=640;   // many small buffers (rock fragments) are kept for the next break instead of being destroyed one by one
    // Only fence-completed buffers enter this pool. No mesh identities or scene
    // references survive retirement. AS resources must retain their fixed state.
    void recycleBuffer(ComPtr<ID3D12Resource>& resource,bool accelerationStructure=false) {
        if(!resource)return;
        for(const auto& idle:bufferPool)if(idle.resource.Get()==resource.Get()){resource.Reset();return;}
        const auto desc=starlight_resource_desc(resource);const auto bytes=uploadStorageBytes(resource.Get());
        if(bytes>bufferPoolLimit){resource.Reset();return;}
        while(bufferPool.size()>=bufferPoolCount || bytes>bufferPoolLimit-bufferPoolBytes){
            // Evict the largest idle buffer first: one huge, rarely reusable buffer must not push out dozens of small, reusable ones.
            auto smallest=std::max_element(bufferPool.begin(),bufferPool.end(),[](const auto& a,const auto& b){return a.width<b.width;});
            bufferPoolBytes-=smallest->bytes;bufferPool.erase(smallest);++bufferEvicted;
        }
        bufferPoolBytes+=bytes;bufferPool.push_back({std::move(resource),accelerationStructure,desc.Width,bytes,desc.Flags});
    }
    ComPtr<ID3D12Resource> acquireBuffer(UINT64 bytes,D3D12_RESOURCE_STATES state,bool uav=false) {
        const bool accelerationStructure=state==D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
        const auto flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
        size_t best=bufferPool.size();
        for(size_t i=0;i<bufferPool.size();++i){const auto& idle=bufferPool[i];
            if(idle.accelerationStructure==accelerationStructure && idle.flags==flags && idle.width>=bytes &&
               (best==bufferPool.size() || idle.width<bufferPool[best].width))best=i;
        }
        ComPtr<ID3D12Resource> result;
        if(best<bufferPool.size()){
            bufferPoolBytes-=bufferPool[best].bytes;result=std::move(bufferPool[best].resource);bufferPool.erase(bufferPool.begin()+best);++bufferReused;
        }else{
            D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=std::max<UINT64>(256,bytes);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=flags;
            must(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,accelerationStructure?state:D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&result)),"create reusable graphics buffer");++bufferCreated;
        }
        // Ordinary buffers decay to COMMON after completed ExecuteCommandLists.
        if(!accelerationStructure && state!=D3D12_RESOURCE_STATE_COMMON)barrier(result.Get(),D3D12_RESOURCE_STATE_COMMON,state);
        return result;
    }
    static constexpr UINT64 uploadPoolLimit = 16ull * 1024 * 1024;
    static UINT64 uploadStorageBytes(ID3D12Resource* resource) {
        const auto width = starlight_resource_desc(resource).Width;
        return (width + D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1) &
               ~(UINT64(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT) - 1);
    }
    // Call only after submitting the copies and completing their fence.
    void retireUploads() {
        for (auto& resource : uploadKeepAlive) {
            auto bytes = uploadStorageBytes(resource.resource.Get());
            if (bytes <= uploadPoolLimit) {
                while (uploadPool.size() >= 16 || bytes > uploadPoolLimit - uploadPoolBytes) {
                    auto smallest = std::min_element(uploadPool.begin(), uploadPool.end(),
                        [](const auto& a, const auto& b) { return starlight_resource_desc(a.resource).Width < starlight_resource_desc(b.resource).Width; });
                    uploadPoolBytes -= uploadStorageBytes(smallest->resource.Get());
                    uploadPool.erase(smallest);
                }
                uploadPoolBytes += bytes;
                uploadPool.push_back(std::move(resource));
            }
        }
        uploadKeepAlive.clear();
    }

    GpuMesh  charMesh, coneMesh, ringMesh, ringThinMesh, boxMesh, discMesh, cylMesh, heartMesh;
    XMMATRIX charNorm = XMMatrixIdentity();          // normalize forge mesh: feet at y=0, ~1.8m tall

    // camera + net
    XMMATRIX          viewProj = XMMatrixIdentity();
    XMFLOAT3          camWorld{ 0,0,0 };
    std::unordered_map<uint64_t, SmoothXf> smooth;
    uint64_t          lastTick = 0;
    uint64_t          startTick = 0;

    // --shot <file.bmp>: backbuffer readback after shotAtMs
    std::wstring shotPath; uint64_t shotAtMs = 12000; bool shotDone = false;
    ComPtr<ID3D12Resource> readback;

    UINT   rtvSize = 0, w = 0, h = 0;
    UINT   frame = 0;
    UINT   instCursor = 0;
    uint64_t instDropped = 0;   // draws refused because the per-frame constant buffer was full (cumulative; must stay 0)
    UINT64 fenceVal = 0;
    HANDLE fenceEvent = nullptr;

    void must(HRESULT hr, const char* what) { if (FAILED(hr)) { drainDebug(); throw std::runtime_error(what); } }   // debug-layer reason lands in the log first
    // Test hook (audit CC-07): the n-th checked D3D12 result after setting the countdown is reported as failed, so the failure paths run without a real device fault.
    inline static int hrFaultCountdown = 0;
    void mustHr(HRESULT hr, const char* what) { if (hrFaultCountdown > 0 && --hrFaultCountdown == 0) hr = E_FAIL; must(hr, what); }

    // full GPU idle: init / mode change / screenshot / teardown only (never per frame)
    void flush() {
        const UINT64 v = ++fenceVal;
        mustHr(queue->Signal(fence.Get(), v), "signal GPU completion");
        waitFence(v);
    }
    void waitFence(UINT64 v) {
        if (fence->GetCompletedValue() < v) {
            const double waitStarted=timingNow();
            must(fence->SetEventOnCompletion(v, fenceEvent), "arm GPU completion");
            if (WaitForSingleObject(fenceEvent, INFINITE) != WAIT_OBJECT_0)
                throw std::runtime_error("Wait for GPU completion failed");
            measuredFenceWaitMs+=(timingNow()-waitStarted)*1000;
        }
    }
    // --d3ddebug: pull every queued D3D12 / DXGI debug-layer message into the log (once per frame + at exit)
    void drainDebug() {
        if (d3dq) {
            UINT64 n = d3dq->GetNumStoredMessages();
            for (UINT64 i = 0; i < n; ++i) {
                SIZE_T len = 0; d3dq->GetMessage(i, nullptr, &len);
                std::vector<char> buf(len); D3D12_MESSAGE* m = (D3D12_MESSAGE*)buf.data();
                if (SUCCEEDED(d3dq->GetMessage(i, m, &len)) && m->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) logLine(std::string("d3d12 debug: ") + m->pDescription);
            }
            d3dq->ClearStoredMessages();
        }
        if (dxgiq) {
            UINT64 n = dxgiq->GetNumStoredMessages(kDxgiDebugAll);
            for (UINT64 i = 0; i < n; ++i) {
                SIZE_T len = 0; dxgiq->GetMessage(kDxgiDebugAll, i, nullptr, &len);
                std::vector<char> buf(len); DXGI_INFO_QUEUE_MESSAGE* m = (DXGI_INFO_QUEUE_MESSAGE*)buf.data();
                if (SUCCEEDED(dxgiq->GetMessage(kDxgiDebugAll, i, m, &len)) && m->Severity <= DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING) logLine(std::string("dxgi debug: ") + m->pDescription);
            }
            dxgiq->ClearStoredMessages(kDxgiDebugAll);
        }
    }
    void reportDeviceLoss() {
        drainDebug();
        char reason[64]{};
        snprintf(reason, sizeof(reason), "device removed HRESULT=0x%08lx",
                 static_cast<unsigned long>(device->GetDeviceRemovedReason()));
        logLine(reason);
        ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
        if (FAILED(device.As(&dred))) { logLine("DRED unavailable"); return; }
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
        if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs))) {
            unsigned count = 0;
            for (auto node = breadcrumbs.pHeadAutoBreadcrumbNode; node && count++ < 128; node = node->pNext)
                logLine("DRED command list: " + std::string(node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "unnamed") +
                        " completed=" + std::to_string(node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0) +
                        " total=" + std::to_string(node->BreadcrumbCount));
        }
        D3D12_DRED_PAGE_FAULT_OUTPUT1 fault{};
        if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&fault)))
            logLine("DRED page fault VA=" + std::to_string(fault.PageFaultVA));
    }
    // orderly exit: GPU idle -> drop our swapchain/backbuffer refs -> vendor backend (contexts, its
    // swapchain proxy, worker threads, DLL) -> the device dies later at static destruction.
    void enterFullscreen() {
        if (!swap) return;
        HRESULT hr = swap->SetFullscreenState(TRUE, nullptr);   // best-effort; the monitor-covering popup already gives independent-flip
        logLine(std::string("fullscreen exclusive: ") + (SUCCEEDED(hr) ? "on" : "unavailable (staying borderless)"));
    }
    void teardown() {
        if (!queue) return;
        flush(); drainDebug();
        if (swap) swap->SetFullscreenState(FALSE, nullptr);   // must leave exclusive before releasing the swapchain
        for (UINT i = 0; i < kFrameCount; ++i) targets[i].Reset();
        swap.Reset();
        if (ups) { ups->shutdown(); ups.reset(); }
        antilag.shutdown();
    }

    ComPtr<ID3D12Resource> createDefaultBuffer(const void* src, size_t bytes, D3D12_RESOURCE_STATES finalState) {
        D3D12_HEAP_PROPERTIES hpDefault{}; hpDefault.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_HEAP_PROPERTIES hpUpload{};  hpUpload.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = bytes; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        auto def=acquireBuffer(bytes,D3D12_RESOURCE_STATE_COMMON);
        UploadStorage up;
        size_t best = uploadPool.size();
        for (size_t i = 0; i < uploadPool.size(); ++i) {
            const auto width = starlight_resource_desc(uploadPool[i].resource).Width;
            if (width >= bytes && (best == uploadPool.size() || width < starlight_resource_desc(uploadPool[best].resource).Width)) best = i;
        }
        if (best != uploadPool.size()) {
            uploadPoolBytes -= uploadStorageBytes(uploadPool[best].resource.Get());
            up = std::move(uploadPool[best]);
            ++uploadReused;
            uploadPool.erase(uploadPool.begin() + best);
        } else {
            must(device->CreateCommittedResource(&hpUpload, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up.resource)), "create upload buffer");
            ++uploadCreated;
        }

        // Upload heaps remain mapped for their lifetime. Only fence-completed
        // storage is checked out, so the CPU never overwrites an in-flight copy.
        if (!up.mapped) {
            D3D12_RANGE none{ 0,0 };
            must(up.resource->Map(0, &none, &up.mapped), "map upload buffer");
        }
        memcpy(up.mapped, src, bytes);

        cmd->CopyBufferRegion(def.Get(), 0, up.resource.Get(), 0, bytes);
        D3D12_RESOURCE_BARRIER br{}; br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        br.Transition.pResource = def.Get(); br.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;   // promoted by the copy above
        br.Transition.StateAfter = finalState; br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &br);

        uploadKeepAlive.push_back(std::move(up));
        return def;
    }

    GpuMesh uploadMesh(const Mesh& mesh) { return uploadMesh(mesh.verts.data(), mesh.verts.size() * sizeof(Vertex), sizeof(Vertex), mesh.indices); }

    void init(HWND hwnd, UINT width, UINT height, const Mesh& forge) {
        w = width; h = height; startTick = GetTickCount64();
        UINT flags = 0;
        if (d3dDebug) {
            ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
                dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
                logLine("DRED breadcrumbs and page faults enabled");
            }
            ComPtr<ID3D12Debug> dbg;
            const HRESULT debugResult = D3D12GetDebugInterface(IID_PPV_ARGS(&dbg));
            if (SUCCEEDED(debugResult)) { dbg->EnableDebugLayer(); flags |= DXGI_CREATE_FACTORY_DEBUG; logLine("d3d12 debug layer ON"); }
            else { char code[32]{}; snprintf(code,sizeof(code),"0x%08lx",static_cast<unsigned long>(debugResult)); logLine(std::string("d3d12 debug layer unavailable HRESULT ")+code); }
            for(const auto* moduleName:{L"D3D12Core.dll",L"d3d12SDKLayers.dll"}) {
                wchar_t loadedPath[32768]{};
                const auto module=GetModuleHandleW(moduleName);
                if(module && GetModuleFileNameW(module,loadedPath,32768)) {
                    const int bytes=WideCharToMultiByte(CP_UTF8,0,loadedPath,-1,nullptr,0,nullptr,nullptr);
                    std::string path(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,loadedPath,-1,path.data(),bytes,nullptr,nullptr);
                    if(!path.empty())path.pop_back();logLine("graphics runtime module: "+path);
                }
            }
            if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&dxgiq)))) { dxgiq->SetMuteDebugOutput(kDxgiDebugAll, FALSE); logLine("dxgi debug queue ON"); }
        }
        // ---- pick the adapter first (vendor drives the auto path), then let the chosen backend
        // create factory + device (DLSS proxies them through the Streamline interposer).
        {
            ComPtr<IDXGIFactory6> probe; must(CreateDXGIFactory2(flags, IID_PPV_ARGS(&probe)), "CreateDXGIFactory2 (probe)");
            adapter.Reset();
            for (UINT i = 0; ; ++i) {
                ComPtr<IDXGIAdapter1> candidate;
                const HRESULT found = probe->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
                if (found == DXGI_ERROR_NOT_FOUND) break;
                must(found, "enumerate graphics adapters");
                DXGI_ADAPTER_DESC1 d{}; must(candidate->GetDesc1(&d), "read graphics adapter capabilities");
                if (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
                if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr))) {
                    adapter = candidate;
                    adapterVendor = d.VendorId; adapterName.clear(); for (wchar_t c : std::wstring(d.Description)) adapterName.push_back(c < 128 ? (char)c : '?');
                    break;
                }
            }
            must(adapter ? S_OK : E_FAIL, "Starlight requires a hardware graphics adapter supporting Direct3D 12 feature level 12.0. Update the graphics driver or choose a supported GPU.");
            logLine("adapter " + adapterName + " vendor 0x" + [&] { char b[16]; snprintf(b, 16, "%04x", adapterVendor); return std::string(b); }());
        }
        chooseBackend();
        must(ups->createFactory(flags, factory.ReleaseAndGetAddressOf()), "CreateDXGIFactory2");
        must(ups->createDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, device.ReleaseAndGetAddressOf()), "D3D12CreateDevice");
        if (d3dDebug) device.As(&d3dq);
        antilag.init(device.Get(), &logLine);

        D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        must(device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue)), "CreateCommandQueue");

        {   // GPU-timestamp perf monitor: query heap (2 per slot) + readback buffer + tick frequency
            D3D12_QUERY_HEAP_DESC qh{}; qh.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; qh.Count = 2 * kFrameCount;
            must(device->CreateQueryHeap(&qh, IID_PPV_ARGS(&tsHeap)), "CreateQueryHeap(timestamp)");
            D3D12_HEAP_PROPERTIES rbHeap{}; rbHeap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC rbDesc{}; rbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rbDesc.Width = sizeof(UINT64) * 2 * kFrameCount; rbDesc.Height = 1; rbDesc.DepthOrArraySize = 1;
            rbDesc.MipLevels = 1; rbDesc.SampleDesc.Count = 1; rbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            must(device->CreateCommittedResource(&rbHeap, D3D12_HEAP_FLAG_NONE, &rbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tsReadback)), "ts readback buffer");
            queue->GetTimestampFrequency(&tsFreq);
            logLine("gpu-timer: query heap ready, freq " + std::to_string(tsFreq));
            {   // per-pass timestamps (8 per slot)
                D3D12_QUERY_HEAP_DESC ph{}; ph.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; ph.Count = 16 * kFrameCount;
                if (SUCCEEDED(device->CreateQueryHeap(&ph, IID_PPV_ARGS(&passHeap)))) {
                    rbDesc.Width = sizeof(UINT64) * 16 * kFrameCount;
                    if (FAILED(device->CreateCommittedResource(&rbHeap, D3D12_HEAP_FLAG_NONE, &rbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&passReadback)))) passHeap.Reset();
                }
            }
        }

        DXGI_SWAP_CHAIN_DESC1 sc{}; sc.BufferCount = kFrameCount; sc.Width = w; sc.Height = h;
        sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; sc.SampleDesc.Count = 1;
        sc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;   // vendor FG paths call SetMaximumFrameLatency, which DXGI rejects without this
        {   // tearing support: required for sync-interval-0 presents (vendor FG presents with ALLOW_TEARING when vsync is off)
            BOOL tear = FALSE;
            if (SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tear, sizeof(tear))) && tear) { sc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; tearing = true; }
            logLine(std::string("tearing ") + (tearing ? "supported" : "unsupported"));
        }
        {   // scoped: a backend may REPLACE this swapchain in init(); DXGI allows one per hwnd, so
            // no stray reference (like sc1 living to the end of init) may outlive this block.
            ComPtr<IDXGISwapChain1> sc1;
            must(factory->CreateSwapChainForHwnd(queue.Get(), hwnd, &sc, nullptr, nullptr, &sc1), "CreateSwapChainForHwnd");
            must(sc1.As(&swap), "SwapChain As IDXGISwapChain4");
        }

        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.NumDescriptors = kFrameCount + 2; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        must(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvHeap)), "CreateDescriptorHeap RTV");
        rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_DESCRIPTOR_HEAP_DESC dh{}; dh.NumDescriptors = 1; dh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        must(device->CreateDescriptorHeap(&dh, IID_PPV_ARGS(&dsvHeap)), "CreateDescriptorHeap DSV");

        for (UINT i = 0; i < kFrameCount; ++i) must(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocs[i])), "CreateCommandAllocator");
        alloc = allocs[0];
        must(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&cmd)), "CreateCommandList");
        cmd->Close();   // created open; every user Reset()s it
        must(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
        fenceVal = 1;
        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        // ---- backend init (may REPLACE the swapchain with a frame-generation one), then
        // backbuffer views, then the mode's render size + offscreen targets.
        {
            up::DeviceCtx ctx; ctx.device = device.Get(); ctx.queue = queue.Get(); ctx.adapter = adapter.Get();
            ctx.hwnd = hwnd; ctx.outW = w; ctx.outH = h; ctx.backbufferCount = kFrameCount; ctx.log = &logLine;
            if (!ups->init(ctx, swap)) {
                logLine(std::string("upscaler ") + up::pathName(ups->path()) + " init FAILED -> off");
                ups->shutdown(); ups.reset(up::createOff()); upsReason += "; init failed -> off";
                must(ups->init(ctx, swap) ? S_OK : E_FAIL, "off backend init");
            }
            logLine(std::string("upscaler active: ") + up::pathName(ups->path()) + " (" + ups->versionString() + ") " + upsReason);
        }
        createBackbufferViews();
        {
            int m = -1;
            for (int i = 0; i < ups->modeCount(); ++i) if (modeRequest == ups->modeName(i)) { m = i; break; }
            if (m < 0) { m = -1; for (int i = 0; i < ups->modeCount(); ++i) if (std::string("quality") == ups->modeName(i)) { m = i; break; } if (m < 0) m = 0; logLine("unknown --quality '" + modeRequest + "' for " + up::pathName(ups->path()) + ", using " + ups->modeName(m)); }
            applyMode(m);
            if (fgRequest) ups->setFG(true);
            chooseLatency();
            logLine(std::string("mode ") + ups->modeName(ups->mode()) + " render " + std::to_string(rw) + "x" + std::to_string(rh)
                + " -> " + std::to_string(w) + "x" + std::to_string(h) + " | " + ups->fgStatus());
        }

        inflight = std::max(1, std::min((int)kFrameCount, inflight));
        // Present pipelining: cap queued frames and grab the frame-latency waitable so we can pace at the frame TOP
        // (below) instead of blocking inside present(). This overlaps present with the next frame's CPU record.
        swap->SetMaximumFrameLatency((UINT)inflight);
        frameLatencyWaitable = swap->GetFrameLatencyWaitableObject();
        logLine("frames in flight " + std::to_string(inflight) + " (backbuffers " + std::to_string(kFrameCount) + ")" + (frameLatencyWaitable ? " + latency-waitable" : ""));
        buildPipeline();
        uploadGeometry(forge);
        createExposure();
        loadCharacters();

        // normalize the forge character mesh: center it on x/z, stand feet on y=0, ~1.8m tall.
        float cx = (forge.bbMin.x + forge.bbMax.x) * 0.5f;
        float cz = (forge.bbMin.z + forge.bbMax.z) * 0.5f;
        float minY = forge.bbMin.y;
        float ht = std::max(0.001f, forge.bbMax.y - forge.bbMin.y);
        float s = 1.8f / ht;
        charNorm = XMMatrixMultiply(XMMatrixTranslation(-cx, -minY, -cz), XMMatrixScaling(s, s, s));
    }

    // ---- backend selection. --upscaler auto = adapter vendor (NVIDIA dlss, AMD fsr, Intel
    // xess, else fsr), then the portable candidates; an explicit request tries that path, then off.
    // A backend that is not built or whose SDK/driver refuses is logged with the reason and skipped.
    void chooseBackend() {
        std::vector<std::string> order;
        if (upsRequest == "auto") {
            if (adapterVendor == 0x10DE) order = { "dlss", "fsr", "xess" };
            else if (adapterVendor == 0x1002) order = { "fsr", "xess" };
            else if (adapterVendor == 0x8086) order = { "xess", "fsr" };
            else order = { "fsr", "xess" };
        } else if (upsRequest != "off") order = { upsRequest };
        for (const std::string& name : order) {
            up::Upscaler* u = name == "dlss" ? up::createDLSS() : name == "fsr" ? up::createFSR() : name == "xess" ? up::createXeSS() : nullptr;
            if (!u) { logLine("upscaler " + name + ": not built / unknown"); upsReason += name + ":not-built "; continue; }
            if (!u->preDevice(&logLine)) { logLine("upscaler " + name + ": preDevice refused (SDK or driver missing)"); upsReason += name + ":unavailable "; delete u; continue; }
            ups.reset(u); upsReason += "chosen " + name + (upsRequest == "auto" ? " (auto)" : " (requested)"); return;
        }
        ups.reset(up::createOff()); upsReason += (order.empty() ? "requested off" : "-> off");
        ups->preDevice(&logLine);
    }

    // Native editor resize: preserve device, pipelines, geometry and frame counters.
    // Called only between frames on the owning render thread.
    void resizeNativeOutput(UINT width, UINT height) {
        if (!width || !height) throw std::runtime_error("Invalid resize dimensions");
        if (width == w && height == h) return;
        if (ups->path() != up::Path::Off) throw std::runtime_error("Native resize requires native backend");
        flush();
        DXGI_SWAP_CHAIN_DESC1 desc{};
        must(swap->GetDesc1(&desc), "Resize swap description");
        for (auto& target : targets) target.Reset();
        must(swap->ResizeBuffers(kFrameCount, width, height, desc.Format, desc.Flags), "ResizeBuffers");
        w=width; h=height;
        must(ups->resizeOutput(w,h) ? S_OK : E_FAIL, "Resize native backend");
        createBackbufferViews();
        applyMode(ups->mode());
        auditOutput.Reset();
        if (auditOffscreen) setAuditOffscreen(true);
        lastFrameStart=0;
    }

    void createBackbufferViews() {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = starlight_cpu_heap_start(rtvHeap);
        for (UINT i = 0; i < kFrameCount; ++i) {
            targets[i].Reset();
            must(swap->GetBuffer(i, IID_PPV_ARGS(&targets[i])), "GetBuffer");
            device->CreateRenderTargetView(targets[i].Get(), nullptr, rtv);
            rtv.ptr += rtvSize;
        }
        frame = swap->GetCurrentBackBufferIndex();
        if (frame >= kFrameCount) {   // Streamline proxy swapchain (DLSS-G loaded) can report an index beyond our BufferCount (issue #35) -> targets[frame] OOB = intermittent crash. Guard it.
            if (!warnedBbIndex) { warnedBbIndex = true; logLine("WARN: swapchain backbuffer index " + std::to_string(frame) + " >= kFrameCount " + std::to_string(kFrameCount) + " (Streamline proxy) - clamping (evidence of the #35 crash vector)"); }
            frame %= kFrameCount;
        }
    }

    ComPtr<ID3D12Resource> createTex(UINT tw, UINT th, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS fl, D3D12_RESOURCE_STATES st, const D3D12_CLEAR_VALUE* cv, const char* what, UINT samples = 1) {
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC dd{};
        dd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; dd.Width = tw; dd.Height = th;
        dd.DepthOrArraySize = 1; dd.MipLevels = 1; dd.Format = fmt; dd.SampleDesc.Count = samples; dd.Flags = fl;
        ComPtr<ID3D12Resource> r;
        must(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &dd, st, cv, IID_PPV_ARGS(&r)), what);
        return r;
    }

    // Anisotropic filtering level for the texture samplers (STARLIGHT_AF = 1, 2, 4, 8 or 16; default 16).
    static UINT anisotropySetting() {
        wchar_t value[8]{};
        UINT level = 16;
        if (GetEnvironmentVariableW(L"STARLIGHT_AF", value, 8)) level = (UINT)_wtoi(value);
        return (level == 1 || level == 2 || level == 4 || level == 8 || level == 16) ? level : 16;
    }

    // Sample count for the scene pass: STARLIGHT_MSAA (1, 2, 4 or 8) or msaaRequest, reduced to what the adapter supports.
    void chooseMsaa() {
        UINT want = msaaRequest;
        wchar_t value[8]{};
        if (GetEnvironmentVariableW(L"STARLIGHT_MSAA", value, 8)) want = (UINT)_wtoi(value);
        if (want != 2 && want != 4 && want != 8) want = 1;
        const DXGI_FORMAT formats[3] = { sceneFormat(), DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_D32_FLOAT };
        while (want > 1) {
            bool ok = true;
            for (auto format : formats) {
                D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{}; q.Format = format; q.SampleCount = want;
                if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) || q.NumQualityLevels == 0) ok = false;
            }
            if (ok) break;
            want /= 2;
        }
        const UINT requested = msaaRequest; msaa = want;
        { wchar_t value[8]{}; UINT envValue = 0; if (GetEnvironmentVariableW(L"STARLIGHT_MSAA", value, 8)) envValue = (UINT)_wtoi(value); starlight_diag::evlog("cfg.msaa", envValue ? envValue : requested, msaa, anisotropySetting()); logLine("effective settings: msaa " + std::to_string(msaa) + "x (requested " + std::to_string(envValue ? envValue : requested) + "x), anisotropy " + std::to_string(anisotropySetting()) + "x, scene " + std::to_string(rw) + "x" + std::to_string(rh)); }
    }

    // Resolve the multisampled scene into the single-sample targets that the post passes read. Leaves every resource in
    // its resting state (multisampled: render target / depth write; single sample: non-pixel shader resource).
    void resolveScene() {
        ComPtr<ID3D12GraphicsCommandList1> list1; must(cmd.As(&list1), "command list 1");
        barrier(msaaColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        barrier(msaaMvec.Get(),  D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        barrier(msaaDepth.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE,   D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        barrier(sceneColor.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        barrier(sceneMvec.Get(),  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        barrier(depth.Get(),      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        cmd->ResolveSubresource(sceneColor.Get(), 0, msaaColor.Get(), 0, sceneFormat());
        cmd->ResolveSubresource(sceneMvec.Get(),  0, msaaMvec.Get(),  0, DXGI_FORMAT_R16G16_FLOAT);
        list1->ResolveSubresourceRegion(depth.Get(), 0, 0, 0, msaaDepth.Get(), 0, nullptr, DXGI_FORMAT_R32_FLOAT, D3D12_RESOLVE_MODE_MIN);
        barrier(msaaColor.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        barrier(msaaMvec.Get(),  D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        barrier(msaaDepth.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        barrier(sceneColor.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(sceneMvec.Get(),  D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(depth.Get(),      D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    // (re)create the render-size scene targets + the output-size reconstruction target.
    // Resting states = what the backend receives at evaluate(); render() transitions around the pass.
    void applyMode(int m) {
        flush();
        UINT nrw = 0, nrh = 0;
        if (!ups->setMode(m, nrw, nrh)) return;
        rw = nrw; rh = nrh;
        D3D12_CLEAR_VALUE cc{}; cc.Format = sceneFormat();
        D3D12_CLEAR_VALUE mc{}; mc.Format = DXGI_FORMAT_R16G16_FLOAT;
        D3D12_CLEAR_VALUE dc{}; dc.Format = DXGI_FORMAT_D32_FLOAT; dc.DepthStencil.Depth = 1.0f;
        sceneColor = createTex(rw, rh, sceneFormat(), D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &cc, "create scene color");
        sceneMvec  = createTex(rw, rh, DXGI_FORMAT_R16G16_FLOAT,   D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &mc, "create scene mvec");
        depth      = createTex(rw, rh, DXGI_FORMAT_R32_TYPELESS,   D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &dc, "create depth");
        upOutput   = createTex(w, h,   DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, "create upscale output");
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = starlight_cpu_heap_start(rtvHeap);
        chooseMsaa(); msaaColor.Reset(); msaaMvec.Reset(); msaaDepth.Reset();
        if (msaa > 1) {
            D3D12_CLEAR_VALUE dm{}; dm.Format = DXGI_FORMAT_D32_FLOAT; dm.DepthStencil.Depth = 1.0f;
            msaaColor = createTex(rw, rh, sceneFormat(), D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &cc, "create msaa scene color", msaa);
            msaaMvec  = createTex(rw, rh, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &mc, "create msaa scene mvec", msaa);
            msaaDepth = createTex(rw, rh, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dm, "create msaa depth", msaa);
        }
        rtv.ptr += (SIZE_T)kFrameCount * rtvSize; device->CreateRenderTargetView(msaa > 1 ? msaaColor.Get() : sceneColor.Get(), nullptr, rtv);
        rtv.ptr += rtvSize;                        device->CreateRenderTargetView(msaa > 1 ? msaaMvec.Get() : sceneMvec.Get(), nullptr, rtv);
        D3D12_DEPTH_STENCIL_VIEW_DESC dv{}; dv.Format = DXGI_FORMAT_D32_FLOAT; dv.ViewDimension = msaa > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(msaa > 1 ? msaaDepth.Get() : depth.Get(), &dv, starlight_cpu_heap_start(dsvHeap));
        resetHistory = true;
        readback.Reset();
    }

    // 1x1 R32_FLOAT exposure = 1.0 (the scene is LDR; the SDKs want an explicit exposure input).
    void createExposure() {
        exposureTex = createTex(1, 1, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, "create exposure");
        D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT; bd.Height = 1;
        bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> up;
        must(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&up)), "exposure upload");
        float one = 1.0f; void* p = nullptr; D3D12_RANGE none{ 0,0 }; mustHr(up->Map(0, &none, &p), "map constant upload"); memcpy(p, &one, 4); up->Unmap(0, nullptr);
        mustHr(alloc->Reset(), "reset command allocator"); mustHr(cmd->Reset(alloc.Get(), nullptr), "reset command list");
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = exposureTex.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = up.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint = { DXGI_FORMAT_R32_FLOAT, 1, 1, 1, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT };
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        barrier(exposureTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        mustHr(cmd->Close(), "close command list"); ID3D12CommandList* l[] = { cmd.Get() }; queue->ExecuteCommandLists(1, l); flush();
    }

    static float halton(uint32_t i, uint32_t b) {
        float f = 1.0f, r = 0.0f;
        while (i > 0) { f /= (float)b; r += f * (float)(i % b); i /= b; }
        return r;
    }

    void buildPipeline() {
        // root: b0 instance CB | b1 frame CB (lights) | t0 joint palette (root SRV) | t1..t4 textures | s0 aniso
        D3D12_DESCRIPTOR_RANGE texRange{}; texRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; texRange.NumDescriptors = kTexPerSet;
        texRange.BaseShaderRegister = 1; texRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        D3D12_ROOT_PARAMETER rp[6] = {};
        rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rp[0].Descriptor.ShaderRegister = 0; rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; rp[1].Descriptor.ShaderRegister = 1; rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        rp[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[2].Descriptor.ShaderRegister = 0; rp[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        rp[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; rp[3].DescriptorTable.NumDescriptorRanges = 1; rp[3].DescriptorTable.pDescriptorRanges = &texRange; rp[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;   // the fur VS samples the furflow map
        rp[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; rp[4].Descriptor.ShaderRegister = 7; rp[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;                                   // t7: per-instance StructuredBuffer<Inst> (cross-character instancing)
        rp[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; rp[5].Constants.ShaderRegister = 2; rp[5].Constants.Num32BitValues = 1; rp[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;   // b2: groupBase (added to SV_InstanceID)
        D3D12_STATIC_SAMPLER_DESC ss{}; ss.Filter = D3D12_FILTER_ANISOTROPIC; ss.MaxAnisotropy = anisotropySetting();
        ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP; ss.MaxLOD = D3D12_FLOAT32_MAX;
        ss.ShaderRegister = 0; ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsd{}; rsd.NumParameters = 6; rsd.pParameters = rp; rsd.NumStaticSamplers = 1; rsd.pStaticSamplers = &ss;
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> sig, sigErr;
        must(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &sigErr), "SerializeRootSignature");
        must(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&rootSig)), "CreateRootSignature");

        // Shading: one GGX/Cook-Torrance model for everything. CB colours are sRGB (the room's
        // tuned palette) -> linear in, lit by the key light + hemisphere ambient + three cone
        // spotlights, fogged, then encoded back to sRGB into the UNORM target. Skinned characters
        // add the bakes-v1 maps (basecolor sRGB, tangent-space normal, roughness, AO).
        const char* hlsl =
            "cbuffer Scene : register(b0) {\n"
            "  row_major float4x4 mvp;\n"
            "  row_major float4x4 model;\n"
            "  row_major float4x4 mvpNoJit;\n"
            "  row_major float4x4 prevMvp;\n"
            "  float4 mvScale;\n"
            "  float4 baseColor;\n"
            "  float4 params;\n"
            "  float4 fur0;\n"
            "  float4 fur1;\n"
            "};\n"
            "struct Spot { float4 pos; float4 dir; float4 color; };\n"
            "cbuffer Frame : register(b1) {\n"
            "  float4 lightDir;\n"
            "  float4 ambient;\n"
            "  float4 camPos;\n"
            "  float4 fog;\n"
            "  Spot spots[32];\n"
            "};\n"
            "struct JointM { float4 r0, r1, r2, r3; };\n"
            "StructuredBuffer<JointM> joints : register(t0);\n"
            "Texture2D texBase : register(t1);\n"
            "Texture2D texNrm  : register(t2);\n"
            "Texture2D texRgh  : register(t3);\n"
            "Texture2D texAO   : register(t4);\n"
            "Texture2D texFlow : register(t5);\n"   // bakes-v1 furflow: rg = tangent-space strand direction (0.5 = none)
            "Texture2D texSpk  : register(t6);\n"   // bakes-v1 sparkle: r = glint mask
            "SamplerState samp : register(s0);\n"
            "struct Inst { row_major float4x4 mvp; row_major float4x4 model; row_major float4x4 mvpNoJit; row_major float4x4 prevMvp; float4 mvScale; float4 baseColor; float4 params; float4 fur0; float4 fur1; uint paletteBase; uint3 _ipad; };\n"
            "StructuredBuffer<Inst> insts : register(t7);\n"
            "cbuffer Grp : register(b2) { uint groupBase; uint3 _gpad; };\n"
            "float3 toLin(float3 c) { return pow(max(c, 0.0), 2.2); }\n"
            "float3 toSrgb(float3 c) { return pow(max(c, 0.0), 1.0 / 2.2); }\n"
            "float3 brdf(float3 n, float3 v, float3 l, float3 albedo, float rough, float metal) {\n"
            "  float3 h = normalize(v + l);\n"
            "  float ndl = saturate(dot(n, l)); float ndv = max(dot(n, v), 1e-4); float ndh = saturate(dot(n, h)); float vdh = saturate(dot(v, h));\n"
            "  float a = max(rough * rough, 0.002); float a2 = a * a;\n"
            "  float d = a2 / (3.14159265 * pow(ndh * ndh * (a2 - 1.0) + 1.0, 2.0));\n"
            "  float k = (rough + 1.0) * (rough + 1.0) / 8.0;\n"
            "  float g = (ndv / (ndv * (1.0 - k) + k)) * (ndl / (ndl * (1.0 - k) + k));\n"
            "  float3 f0 = lerp(0.04, albedo, metal);\n"
            "  float3 f = f0 + (1.0 - f0) * pow(1.0 - vdh, 5.0);\n"
            "  float3 spec = d * g * f / max(4.0 * ndv * ndl, 1e-4);\n"
            "  float3 kd = (1.0 - f) * (1.0 - metal);\n"
            "  return (kd * albedo / 3.14159265 + spec) * ndl;\n"
            "}\n"
            "float spotAtt(int i, float3 wp, out float3 l) { l=float3(0,1,0); if(spots[i].pos.w<=0)return 0;\n"
            "  float3 tl = spots[i].pos.xyz - wp; float dist = length(tl); l = tl / max(dist, 1e-3);\n"
            "  float cone = smoothstep(spots[i].dir.w, spots[i].color.w, dot(-l, normalize(spots[i].dir.xyz)));\n"
            "  float win = saturate(1.0 - (dist * dist) / (spots[i].pos.w * spots[i].pos.w)); return cone * win * win;\n"
            "}\n"
            "float3 shadeLin(float3 wp, float3 n, float3 albedo, float rough, float metal, float ao, float emissive) {\n"   // linear, unfogged
            "  float3 v = normalize(camPos.xyz - wp);\n"
            "  float3 c = brdf(n, v, -normalize(lightDir.xyz), albedo, rough, metal) * lightDir.w;\n"
            "  float hemi = 0.5 + 0.5 * n.y;\n"
            "  c += albedo * toLin(ambient.rgb) * lerp(ambient.w, 1.0, hemi) * ao;\n"
            "  [unroll] for (int i = 0; i < 32; ++i) {\n"
            "    float3 l; float att = spotAtt(i, wp, l);\n"
            "    c += brdf(n, v, l, albedo, rough, metal) * spots[i].color.rgb * att;\n"
            "  }\n"
            "  return lerp(c, albedo, emissive);\n"
            "}\n"
            "float3 tonemap(float3 c) {\n"   // hue-preserving neutral curve: highlights roll off instead of clipping to white (spotlights are 22x)
            "  float x = min(c.r, min(c.g, c.b)); float offset = (x < 0.08) ? x - 6.25 * x * x : 0.04; c -= offset;\n"
            "  float peak = max(c.r, max(c.g, c.b)); if (peak < 0.76) return c;\n"
            "  float d = 0.24; float np = 1.0 - d * d / (peak + d - 0.76); c *= np / peak;\n"
            "  float g = 1.0 - 1.0 / (0.15 * (peak - np) + 1.0); return lerp(c, np.xxx, g);\n"
            "}\n"
            "float3 finish(float3 wp, float3 c) {\n"   // fog + tonemap + sRGB encode
            "  float f = saturate((length(wp - camPos.xyz) - 8.0) / 40.0) * fog.w;\n"
            "#if STARLIGHT_LINEAR_HDR\n"
            "  return c;\n"
            "#else\n"
            "  return toSrgb(tonemap(lerp(c, toLin(fog.rgb), f)));\n"
            "#endif\n"
            "}\n"
            "float3 shade(float3 wp, float3 n, float3 albedo, float rough, float metal, float ao, float emissive) {\n"
            "  return finish(wp, shadeLin(wp, n, albedo, rough, metal, ao, emissive));\n"
            "}\n"
            "struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; };\n"
            "struct VSOut { float4 pos : SV_POSITION; float3 wn : NORMAL; float3 wp : TEXCOORD0; float4 cur : TEXCOORD1; float4 prv : TEXCOORD2; };\n"
            "struct PSOut { float4 color : SV_TARGET0; float2 mvec : SV_TARGET1; };\n"
            "VSOut VSMain(VSIn i) {\n"
            "  VSOut o;\n"
            "  o.pos = mul(float4(i.pos, 1.0), mvp);\n"
            "  o.wn  = mul(float4(i.nrm, 0.0), model).xyz;\n"
            "  o.wp  = mul(float4(i.pos, 1.0), model).xyz;\n"
            "  o.cur = mul(float4(i.pos, 1.0), mvpNoJit);\n"
            "  o.prv = mul(float4(i.pos, 1.0), prevMvp);\n"
            "  return o;\n"
            "}\n"
            "struct BgOut { float4 pos : SV_POSITION; };\n"
            "BgOut VSBg(uint id : SV_VertexID) {\n"
            "  BgOut o; float2 p = float2((id << 1) & 2, id & 2);\n"
            "  o.pos = float4(p * 2.0 - 1.0, 1.0, 1.0);\n"
            "  return o;\n"
            "}\n"
            "PSOut PSBg(BgOut i) { PSOut po; po.color = float4(fog.rgb, 1.0); po.mvec = 0; return po; }\n"
            "PSOut PSMain(VSOut i) {\n"
            "  PSOut po;\n"
            "  float2 cn = i.cur.xy / i.cur.w; float2 pn = i.prv.xy / i.prv.w;\n"
            "  po.mvec = (pn - cn) * mvScale.xy;\n"   // previousPixel - currentPixel, y-down
            "  float3 n = normalize(i.wn);\n"
            "  if (dot(n, camPos.xyz - i.wp) < 0.0) n = -n;\n"   // CULL_NONE: light the side that faces the camera
            "  po.color = float4(shade(i.wp, n, toLin(baseColor.rgb), params.x, params.z, 1.0, baseColor.a), 1.0);\n"
            "  return po;\n"
            "}\n"
            "PSOut PSAdd(VSOut i) {\n"   // additive light geometry (beams / pools): unlit colour, fogged toward black, no encode
            "  PSOut po; po.mvec = 0;\n"
            "  float f = saturate((length(i.wp - camPos.xyz) - 8.0) / 40.0) * fog.w;\n"
            "  po.color = float4(baseColor.rgb * (1.0 - f), 1.0);\n"
            "  return po;\n"
            "}\n"
            "struct VSSkinIn { float3 pos : POSITION; float3 nrm : NORMAL; float4 tan : TANGENT; float2 uv : TEXCOORD0; uint4 j : BLENDINDICES; float4 w : BLENDWEIGHT; };\n"
            "struct VSSkinOut { float4 pos : SV_POSITION; float3 wn : NORMAL; float3 wp : TEXCOORD0; float4 cur : TEXCOORD1; float4 prv : TEXCOORD2; float4 wt : TEXCOORD3; float2 uv : TEXCOORD4; nointerpolation float4 ibc : COLOR0; nointerpolation float4 iprm : COLOR1; nointerpolation float4 ifur : COLOR2; };\n"
            "float4x4 jointMat(uint j) { JointM m = joints[j]; return float4x4(m.r0, m.r1, m.r2, m.r3); }\n"
            "VSSkinOut VSSkin(VSSkinIn i, uint iid : SV_InstanceID) {\n"
            "  Inst it = insts[groupBase + iid]; uint pb = it.paletteBase;\n"   // UNIFIED instancing: per-instance transform + palette base + material from the buffer
            "  float4x4 sk = jointMat(pb + i.j.x) * i.w.x + jointMat(pb + i.j.y) * i.w.y + jointMat(pb + i.j.z) * i.w.z + jointMat(pb + i.j.w) * i.w.w;\n"
            "  float4 lp = mul(float4(i.pos, 1.0), sk);\n"
            "  float3 ln = mul(float4(i.nrm, 0.0), sk).xyz;\n"
            "  float3 lt = mul(float4(i.tan.xyz, 0.0), sk).xyz;\n"
            "  VSSkinOut o;\n"
            "  o.pos = mul(lp, it.mvp);\n"
            "  o.wn  = mul(float4(ln, 0.0), it.model).xyz;\n"
            "  o.wt  = float4(mul(float4(lt, 0.0), it.model).xyz, i.tan.w);\n"
            "  o.wp  = mul(lp, it.model).xyz;\n"
            "  o.cur = mul(lp, it.mvpNoJit);\n"
            "  o.prv = mul(lp, it.prevMvp);\n"   // per-instance prev transform => motion vectors bit-identical to the old per-draw path
            "  o.uv  = i.uv;\n"
            "  o.ibc = it.baseColor; o.iprm = it.params; o.ifur = it.fur0;\n"
            "  return o;\n"
            "}\n"
            "PSOut PSSkin(VSSkinOut i) {\n"
            "  PSOut po;\n"
            "  float2 cn = i.cur.xy / i.cur.w; float2 pn = i.prv.xy / i.prv.w;\n"
            "  po.mvec = (pn - cn) * mvScale.xy;\n"
            "  float3 n = normalize(i.wn);\n"
            "  float3 albedo = toLin(i.ibc.rgb); float rough = i.iprm.x; float ao = 1.0;\n"
            "  float3 t = normalize(i.wt.xyz - n * dot(n, i.wt.xyz)); float3 b = cross(n, t) * i.wt.w;\n"
            "  if (i.iprm.y > 0.5) {\n"
            "    albedo *= texBase.Sample(samp, i.uv).rgb;\n"   // SRGB view: sampled in linear
            "    float2 normalXY = texNrm.Sample(samp, i.uv).xy * 2.0 - 1.0;\n"
            "    float3 tn = float3(normalXY, sqrt(saturate(1.0 - dot(normalXY, normalXY))));\n"
            "    n = normalize(t * tn.x + b * tn.y + n * tn.z);\n"
            "    rough = texRgh.Sample(samp, i.uv).r; ao = texAO.Sample(samp, i.uv).r;\n"
            "  }\n"
            "  if (dot(i.wn, camPos.xyz - i.wp) < 0.0) n = -n;\n"
            "  float3 c = shadeLin(i.wp, n, albedo, rough, i.iprm.z, ao, i.ibc.a);\n"
            "  if (i.ifur.w > 0.0) {\n"   // velvet coat on the fur skin itself (spec §9): strand sheen along the groom + grazing-angle fuzz
            "    float2 flow = texFlow.Sample(samp, i.uv).rg * 2.0 - 1.0; float spk = texSpk.Sample(samp, i.uv).r;\n"
            "    float3 T = normalize(t * flow.x + b * flow.y + n * 0.35); float3 v = normalize(camPos.xyz - i.wp);\n"
            "    float shin = lerp(20.0, 110.0, i.ifur.w); float gl = i.ifur.w * (0.03 + spk * 0.5); float3 fuzz = albedo * 0.85 + 0.15;\n"
            "    float rim = pow(1.0 - saturate(dot(n, v)), 3.0) * 0.06;\n"
            "    float3 L = -normalize(lightDir.xyz); float3 H = normalize(L + v); float th = dot(T, H);\n"
            "    c += fuzz * (pow(sqrt(saturate(1.0 - th * th)), shin) * gl * saturate(dot(n, L)) + rim * saturate(dot(n, L) * 0.5 + 0.5)) * lightDir.w;\n"
            "    [unroll] for (int k = 0; k < 32; ++k) {\n"
            "      float3 l; float att = spotAtt(k, i.wp, l); float3 hh = normalize(l + v); float t2 = dot(T, hh);\n"
            "      c += fuzz * (pow(sqrt(saturate(1.0 - t2 * t2)), shin) * gl * saturate(dot(n, l)) + rim * saturate(dot(n, l) * 0.5 + 0.5)) * spots[k].color.rgb * att;\n"
            "    }\n"
            "  }\n"
            "  po.color = float4(finish(i.wp, c), 1.0);\n"
            "  return po;\n"
            "}\n"
            // ---- shell fur. The 'fur' submesh is drawn again N times
            // (SV_InstanceID = shell); every shell is the skinned body pushed out along the normal by
            // h*length and bent in the tangent plane by the furflow map (grooming direction),
            // gravity and a wind sway (softness scales the bend, stiffness resists gravity/wind).
            // The PS keeps only the pixels inside a per-cell strand disc that thins toward the tip
            // and drops strands whose random length is shorter than the shell -> alpha-tested
            // strands, no sorting. Lighting: PBR with the geometric normal darkened toward the
            // roots (self-shadow) + Kajiya-Kay anisotropic highlight along the strand (sheen),
            // sparkle map boosts the glints, back-lit tips get a rim.
            "struct VSFurOut { float4 pos : SV_POSITION; float3 wn : NORMAL; float3 wp : TEXCOORD0; float4 cur : TEXCOORD1; float4 prv : TEXCOORD2; float2 uv : TEXCOORD3; float4 strand : TEXCOORD4; float3 bp : TEXCOORD5; float3 bn : TEXCOORD6; };\n"
            "float hash21(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }\n"
            "VSFurOut VSFur(VSSkinIn i, uint sid : SV_InstanceID) {\n"
            "  float4x4 sk = jointMat(i.j.x) * i.w.x + jointMat(i.j.y) * i.w.y + jointMat(i.j.z) * i.w.z + jointMat(i.j.w) * i.w.w;\n"
            "  float4 lp = mul(float4(i.pos, 1.0), sk);\n"
            "  float3 ln = normalize(mul(float4(i.nrm, 0.0), sk).xyz);\n"
            "  float3 lt = normalize(mul(float4(i.tan.xyz, 0.0), sk).xyz);\n"
            "  float3 lb = cross(ln, lt) * i.tan.w;\n"
            "  float h = (sid + 1.0) / fur1.z;\n"
            "  float2 flow = texFlow.SampleLevel(samp, i.uv, 3.0).rg * 2.0 - 1.0;\n"
            "  float3 down = float3(0.0, -1.0, 0.0); down -= ln * dot(down, ln);\n"   // bodies only yaw: model-space down == world down
            "  float t = fur1.w;\n"
            "  float3 sway = float3(sin(t * 1.7 + lp.y * 3.0 + lp.z), 0.0, cos(t * 1.3 + lp.x * 2.0)) * (0.6 * fur1.x); sway -= ln * dot(sway, ln);\n"
            "  float3 bend = (lt * flow.x + lb * flow.y) * 0.8 + (down * 0.5 + sway) * (1.0 - fur0.z);\n"
            "  float3 off = (ln * h + bend * (h * h * fur0.y)) * (fur0.x / length(model[0].xyz));\n"   // metres -> model units
            "  float4 sp = float4(lp.xyz + off, 1.0);\n"
            "  VSFurOut o;\n"
            "  o.pos = mul(sp, mvp);\n"
            "  o.wn  = mul(float4(ln, 0.0), model).xyz;\n"
            "  o.wp  = mul(sp, model).xyz;\n"
            "  o.cur = mul(sp, mvpNoJit);\n"
            "  o.prv = mul(sp, prevMvp);\n"
            "  o.uv  = i.uv;\n"
            "  o.strand = float4(normalize(mul(float4(ln + bend * (2.0 * h * fur0.y), 0.0), model).xyz), h);\n"
            "  o.bp = i.pos * length(model[0].xyz); o.bn = i.nrm;\n"   // bind pose in metres: strand cells stay glued to the skin under animation
            "  return o;\n"
            "}\n"
            "PSOut PSFur(VSFurOut i) {\n"
            "  float h = i.strand.w;\n"
            "  float3 an = abs(i.bn);\n"
            "  float2 pc = (an.x > an.y && an.x > an.z) ? i.bp.yz : ((an.y > an.z) ? i.bp.xz : i.bp.xy);\n"   // triplanar bind-pose cells: uniform strands/metre, immune to UV seams and degenerate UV islands
            "  float2 cuv = pc * fur1.y; float2 cell = floor(cuv);\n"
            "  float rnd = hash21(cell);\n"
            "  float len = 0.55 + 0.45 * rnd;\n"   // per-strand length 55..100 % of the profile length
            "  float fw = max(length(ddx(cuv)), length(ddy(cuv)));\n"   // strand cells per pixel: sub-pixel cells would alias into speckle
            "  len *= saturate(1.8 - fw);\n"                            // ... so far fur shortens to a thin coat instead
            "  if (h > len) discard;\n"
            "  float2 sc = frac(cuv) - 0.5 - (float2(hash21(cell + 7.3), hash21(cell + 19.1)) - 0.5) * 0.6;\n"
            "  float taper = 1.0 - h / len; float r = 0.62 * lerp(0.3, 1.0, taper);\n"   // roots nearly cover the cell -> reads as a coat, tips thin out
            "  if (dot(sc, sc) > r * r) discard;\n"
            "  PSOut po;\n"
            "  float2 cn = i.cur.xy / i.cur.w; float2 pn = i.prv.xy / i.prv.w;\n"
            "  po.mvec = (pn - cn) * mvScale.xy;\n"
            "  float3 n = normalize(i.wn);\n"
            "  float3 albedo = toLin(baseColor.rgb); float rough = 0.75; float ao = 1.0; float spk = 0.0;\n"
            "  if (params.y > 0.5) { albedo *= texBase.Sample(samp, i.uv).rgb; rough = texRgh.Sample(samp, i.uv).r; ao = texAO.Sample(samp, i.uv).r; spk = texSpk.Sample(samp, i.uv).r; }\n"
            "  if (dot(n, camPos.xyz - i.wp) < 0.0) n = -n;\n"
            "  float shadow = lerp(0.35, 1.0, pow(h, 0.6));\n"   // roots sit under the coat
            "  float3 c = shadeLin(i.wp, n, albedo * shadow, max(rough, 0.55), 0.0, ao, baseColor.a);\n"
            "  float3 v = normalize(camPos.xyz - i.wp); float3 T = normalize(i.strand.xyz);\n"
            "  float shin = lerp(20.0, 110.0, fur0.w); float gl = fur0.w * (0.08 + spk * 0.5) * h;\n"
            "  float3 L = -normalize(lightDir.xyz); float3 H = normalize(L + v); float th = dot(T, H);\n"
            "  c += (albedo * 0.85 + 0.15) * pow(sqrt(saturate(1.0 - th * th)), shin) * gl * saturate(dot(n, L)) * lightDir.w;\n"
            "  c += albedo * lightDir.w * pow(saturate(dot(v, -L)), 4.0) * (0.04 * h);\n"   // back-lit tips
            "  [unroll] for (int k = 0; k < 32; ++k) {\n"
            "    float3 l; float att = spotAtt(k, i.wp, l); float3 hh = normalize(l + v); float t2 = dot(T, hh);\n"
            "    c += (albedo * 0.85 + 0.15) * pow(sqrt(saturate(1.0 - t2 * t2)), shin) * gl * saturate(dot(n, l)) * spots[k].color.rgb * att;\n"
            "  }\n"
            "  po.color = float4(finish(i.wp, c), 1.0);\n"
            "  return po;\n"
            "}\n";

        UINT cflags = 0;
#ifdef _DEBUG
        cflags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
        D3D_SHADER_MACRO macros[]={{"STARLIGHT_LINEAR_HDR",linearHdrScene?"1":"0"},{nullptr,nullptr}};
        auto compile = [&](const char* entry, const char* target) {
            ComPtr<ID3DBlob> blob, e;
            if (FAILED(D3DCompile(hlsl, strlen(hlsl), "shaders", macros, nullptr, entry, target, cflags, 0, &blob, &e)))
                throw std::runtime_error(std::string(entry) + ": " + (e ? (const char*)e->GetBufferPointer() : "compile failed"));
            return blob;
        };
        ComPtr<ID3DBlob> vs = compile("VSMain", "vs_5_0"), ps = compile("PSMain", "ps_5_0"), bvs = compile("VSBg", "vs_5_0"), bps = compile("PSBg", "ps_5_0");
        ComPtr<ID3DBlob> aps = compile("PSAdd", "ps_5_0"), svs = compile("VSSkin", "vs_5_0"), sps = compile("PSSkin", "ps_5_0");
        ComPtr<ID3DBlob> fvs = compile("VSFur", "vs_5_0"), fps = compile("PSFur", "ps_5_0");

        D3D12_INPUT_ELEMENT_DESC layout[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
        pd.pRootSignature = rootSig.Get();
        pd.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pd.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pd.InputLayout = { layout, _countof(layout) };
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pd.NumRenderTargets = 2; pd.RTVFormats[0] = sceneFormat(); pd.RTVFormats[1] = DXGI_FORMAT_R16G16_FLOAT;
        pd.BlendState.IndependentBlendEnable = TRUE;
        pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pd.SampleDesc.Count = msaa; pd.SampleMask = UINT_MAX;
        pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pd.RasterizerState.DepthClipEnable = TRUE;
        for (int i = 0; i < 8; ++i) pd.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        { wchar_t mv[8]{}; if (GetEnvironmentVariableW(L"STARLIGHT_DIAG_NO_MVEC", mv, 8) && mv[0] == L'1') pd.BlendState.RenderTarget[1].RenderTargetWriteMask = 0; }   // diagnostic: attribute the GPU cost of the motion-vector target
        pd.DepthStencilState.DepthEnable = TRUE;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        must(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso)), "CreateGraphicsPipelineState");
        {   // depth pre-pass variants of the main pipeline (same vertex shader, so positions match exactly)
            D3D12_GRAPHICS_PIPELINE_STATE_DESC dd = pd;
            dd.PS = { nullptr, 0 };
            for (int i = 0; i < 8; ++i) dd.BlendState.RenderTarget[i].RenderTargetWriteMask = 0;
            must(device->CreateGraphicsPipelineState(&dd, IID_PPV_ARGS(&psoDepthOnly)), "CreateGraphicsPipelineState (depth pre-pass)");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC ee = pd;
            ee.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
            ee.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            must(device->CreateGraphicsPipelineState(&ee, IID_PPV_ARGS(&psoDepthEqual)), "CreateGraphicsPipelineState (depth equal)");
        }

        // skinned characters: palette skinning in the VS, textured PBR in the PS. Back faces are
        // culled (closed bodies; the sheer skirt is double-sided by geometry in the GLB).
        {
            D3D12_INPUT_ELEMENT_DESC skinLayout[] = {
                { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TANGENT",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "BLENDINDICES", 0, DXGI_FORMAT_R8G8B8A8_UINT,      0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
                { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            };
            static_assert(sizeof(model::SkinVertex) == 68, "SkinVertex layout");
            D3D12_GRAPHICS_PIPELINE_STATE_DESC sd = pd;
            sd.VS = { svs->GetBufferPointer(), svs->GetBufferSize() };
            sd.PS = { sps->GetBufferPointer(), sps->GetBufferSize() };
            sd.InputLayout = { skinLayout, _countof(skinLayout) };
            sd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            must(device->CreateGraphicsPipelineState(&sd, IID_PPV_ARGS(&psoSkin)), "CreateGraphicsPipelineState (skinned)");
            // shell fur: same layout/state, instanced shells, alpha-tested strands (discard) so depth stays exact
            sd.VS = { fvs->GetBufferPointer(), fvs->GetBufferSize() };
            sd.PS = { fps->GetBufferPointer(), fps->GetBufferSize() };
            must(device->CreateGraphicsPipelineState(&sd, IID_PPV_ARGS(&psoFur)), "CreateGraphicsPipelineState (fur)");
        }

        // background pass: one fullscreen triangle writes the fog colour + zero motion into both
        // targets, replacing the per-frame colour clears (whose dynamic mood colour can never
        // match an optimized clear value). Depth stays untouched (test + write off).
        D3D12_GRAPHICS_PIPELINE_STATE_DESC bd = pd;
        bd.VS = { bvs->GetBufferPointer(), bvs->GetBufferSize() };
        bd.PS = { bps->GetBufferPointer(), bps->GetBufferSize() };
        bd.InputLayout = { nullptr, 0 };
        bd.DepthStencilState.DepthEnable = FALSE;
        bd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        must(device->CreateGraphicsPipelineState(&bd, IID_PPV_ARGS(&psoBg)), "CreateGraphicsPipelineState (background)");

        // additive variant for the spotlight beams / floor pools: src ONE + dst ONE, depth test
        // but no depth write (translucent light should not occlude, but IS hidden by walls/props).
        pd.PS = { aps->GetBufferPointer(), aps->GetBufferSize() };
        D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D12_BLEND_ONE;  rt.DestBlend = D3D12_BLEND_ONE;  rt.BlendOp = D3D12_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D12_BLEND_ONE; rt.DestBlendAlpha = D3D12_BLEND_ZERO; rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
        pd.BlendState.RenderTarget[1].RenderTargetWriteMask = 0;   // translucent light writes no motion vectors
        must(device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&psoAdd)), "CreateGraphicsPipelineState (additive)");
    }

    void uploadGeometry(const Mesh& forge) {
        mustHr(alloc->Reset(), "reset command allocator");
        mustHr(cmd->Reset(alloc.Get(), nullptr), "reset command list");

        charMesh = uploadMesh(forge);
        Mesh cone; BuildCone(cone);  coneMesh = uploadMesh(cone);
        Mesh ring; BuildRing(ring);  ringMesh = uploadMesh(ring);
        Mesh ringT; BuildRing(ringT, 96, 0.96f); ringThinMesh = uploadMesh(ringT);
        Mesh box;  BuildBox(box);    boxMesh = uploadMesh(box);
        Mesh disc; BuildDisc(disc);  discMesh = uploadMesh(disc);
        Mesh cyl;  BuildCylinder(cyl); cylMesh = uploadMesh(cyl);
        Mesh heart; BuildHeart(heart); heartMesh = uploadMesh(heart);

        // instance constant buffer ring (upload heap, persistently mapped): kFrameCount x
        // (kMaxInstances slots + the FrameCB), and the joint palette ring beside it.
        {
            D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bd.Width = (UINT64)kFrameCount * cbSlotBytes(); bd.Height = 1;
            bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            must(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&cb)), "create CB");
            D3D12_RANGE none{ 0,0 }; mustHr(cb->Map(0, &none, reinterpret_cast<void**>(&cbBase)), "map constant buffer");
            bd.Width = (UINT64)kFrameCount * kMaxSkinned * kPaletteBytes;
            must(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&palettes)), "create palettes");
            mustHr(palettes->Map(0, &none, reinterpret_cast<void**>(&paletteBase)), "map palettes");
            bd.Width = (UINT64)kFrameCount * kMaxInstEntries * sizeof(InstData);   // per-instance ring for cross-character instancing (t7)
            wchar_t gpuUploadSetting[8]{};
            const bool gpuUploadRequested = GetEnvironmentVariableW(L"STARLIGHT_EXPERIMENT_GPU_UPLOAD", gpuUploadSetting, 8) && gpuUploadSetting[0] == L'1';
            D3D12_FEATURE_DATA_D3D12_OPTIONS16 uploadSupport{};
            const bool gpuUploadSupported = SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS16, &uploadSupport, sizeof(uploadSupport))) && uploadSupport.GPUUploadHeapSupported;
            if (gpuUploadRequested && gpuUploadSupported) {
                D3D12_HEAP_PROPERTIES gpuHeap = hp; gpuHeap.Type = starlightGpuUploadHeap;
                HRESULT made = device->CreateCommittedResource(&gpuHeap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&instData));
                if (SUCCEEDED(made)) made = instData->Map(0, &none, reinterpret_cast<void**>(&instBase));
                instanceGpuUpload = SUCCEEDED(made);
                if (!instanceGpuUpload) { instData.Reset(); instBase = nullptr; }
            }
            if (!instanceGpuUpload) {
                must(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&instData)), "create instData");
                must(instData->Map(0, &none, reinterpret_cast<void**>(&instBase)), "map instData");
            }
            logLine(std::string("instance memory: ") + (instanceGpuUpload ? "gpu-upload" : "system-upload") + (gpuUploadRequested && !instanceGpuUpload ? " (requested GPU upload unavailable; fallback)" : ""));
        }

        mustHr(cmd->Close(), "close command list");
        ID3D12CommandList* lists[] = { cmd.Get() };
        queue->ExecuteCommandLists(1, lists);
        flush();
        retireUploads();
    }

    static UINT cbSlotBytes() { return kMaxInstances * kCbStride + kFrameCbBytes; }
    uint8_t* cbSlotBase() { return cbBase + (size_t)slot * cbSlotBytes(); }
    D3D12_GPU_VIRTUAL_ADDRESS cbSlotGpu() { return cb->GetGPUVirtualAddress() + (UINT64)slot * cbSlotBytes(); }

    // ---- load the charfactory GLBs + their bakes-v1 texture sets. Missing files degrade
    // to the fallback primitives per model (never a crash); everything is reported in the log/report.
    GpuMesh uploadMesh(const void* verts, size_t vbytes, UINT stride, const std::vector<uint32_t>& indices) {
        std::vector<uint8_t> alignedVertices;
        if(stride==68) {
            if(vbytes%stride)throw std::runtime_error("Invalid vertex storage size");
            size_t count=vbytes/stride;alignedVertices.resize(count*96,0);
            const uint8_t* original=(const uint8_t*)verts;
            for(size_t i=0;i<count;++i) {
                memcpy(alignedVertices.data()+i*96,original+i*68,68);
                if(memcmp(alignedVertices.data()+i*96,original+i*68,68))throw std::runtime_error("Vertex padding changed data");
            }
            verts=alignedVertices.data();vbytes=alignedVertices.size();stride=96;
            logLine("vertex stride: 96");
        }
        GpuMesh g;
        const size_t ibytes = indices.size() * sizeof(uint32_t);
        g.vb = createDefaultBuffer(verts, vbytes, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
        g.ib = createDefaultBuffer(indices.data(), ibytes, D3D12_RESOURCE_STATE_INDEX_BUFFER);
        g.indexCount = (UINT)indices.size();
        g.vbv.BufferLocation = g.vb->GetGPUVirtualAddress(); g.vbv.StrideInBytes = stride; g.vbv.SizeInBytes = (UINT)vbytes;
        g.ibv.BufferLocation = g.ib->GetGPUVirtualAddress(); g.ibv.Format = DXGI_FORMAT_R32_UINT; g.ibv.SizeInBytes = (UINT)ibytes;
        return g;
    }

    void barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER br{}; br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        br.Transition.pResource = r; br.Transition.StateBefore = a; br.Transition.StateAfter = b;
        br.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        cmd->ResourceBarrier(1, &br);
    }

    // exponential smoothing toward the snapshot target (interpolation between snapshots).
    SmoothXf& track(uint64_t key, float tx, float tz, float ty, float tyaw, float a) {
        SmoothXf& s = smooth[key];
        if (!s.init) { s.x = tx; s.z = tz; s.y = ty; s.yaw = tyaw; s.init = true; return s; }
        s.x += (tx - s.x) * a; s.z += (tz - s.z) * a; s.y += (ty - s.y) * a;
        float dyaw = tyaw - s.yaw;
        while (dyaw > XM_PI) dyaw -= XM_2PI; while (dyaw < -XM_PI) dyaw += XM_2PI;
        s.yaw += dyaw * a;
        return s;
    }

    void writeInstance(UINT inst, const XMMATRIX& model, const XMFLOAT3& color, float emissive, float rough, float metal, bool textured,
                       const FurProfile* fur = nullptr, UINT shells = 0) {
        SceneCB s{};
        if (fur) {
            s.fur0 = XMFLOAT4(fur->length, fur->softness, fur->stiffness, fur->sheen);
            s.fur1 = XMFLOAT4(fur->sway, STARLIGHT_FUR_STRANDSPERMETER * fur->density, (float)shells, (float)(lastTick % 100000) * 0.001f);
        }
        XMStoreFloat4x4(&s.mvp, XMMatrixMultiply(model, viewProj));
        XMStoreFloat4x4(&s.model, model);
        XMStoreFloat4x4(&s.mvpNoJit, XMMatrixMultiply(model, viewProjNoJit));
        // previous MVP for this instance: draw order is stable frame to frame (room first, then
        // entities in snapshot order), so instance N last frame is the same object unless the
        // count changed — then history is reset for one frame (resetHistory) and prev = current.
        if (inst < curModel.size()) XMStoreFloat4x4(&curModel[inst], model);
        if (!resetHistory && inst < prevInstCount && inst < prevModel.size())
            XMStoreFloat4x4(&s.prevMvp, XMMatrixMultiply(XMLoadFloat4x4(&prevModel[inst]), prevViewProjNoJit));
        else s.prevMvp = s.mvpNoJit;
        s.mvScale = XMFLOAT4(0.5f * (float)rw, -0.5f * (float)rh, 0, 0);
        s.baseColor = XMFLOAT4(color.x, color.y, color.z, emissive);
        s.params = XMFLOAT4(rough, textured ? 1.0f : 0.0f, metal, 0.0f);
        memcpy(cbSlotBase() + (size_t)inst * kCbStride, &s, sizeof(s));
    }
    void writeFrameCB(const FrameCB& f) {
        memcpy(cbSlotBase() + (size_t)kMaxInstances * kCbStride, &f, sizeof(f));
        cmd->SetGraphicsRootConstantBufferView(1, cbSlotGpu() + (UINT64)kMaxInstances * kCbStride);
    }

    // Two-step drawing for the depth pre-pass: stage the per-draw constants once, then issue the same draw in both passes.
    UINT stageMesh(const XMMATRIX& model, const XMFLOAT3& color, float emissive, float rough = 0.6f, float metal = 0.0f) {
        if (instCursor >= kMaxInstances) { ++instDropped; return UINT_MAX; }
        UINT inst = instCursor++;
        writeInstance(inst, model, color, emissive, rough, metal, false);
        return inst;
    }
    void issueMesh(UINT inst, const GpuMesh& m) {
        if (inst == UINT_MAX) return;
        cmd->SetGraphicsRootConstantBufferView(0, cbSlotGpu() + (UINT64)inst * kCbStride);
        cmd->IASetVertexBuffers(0, 1, &m.vbv);
        cmd->IASetIndexBuffer(&m.ibv);
        cmd->DrawIndexedInstanced(m.indexCount, 1, 0, 0, 0);
    }

    void drawMesh(const GpuMesh& m, const XMMATRIX& model, const XMFLOAT3& color, float emissive, float rough = 0.6f, float metal = 0.0f) {
        if (instCursor >= kMaxInstances) { ++instDropped; return; }
        UINT inst = instCursor++;
        writeInstance(inst, model, color, emissive, rough, metal, false);
        cmd->SetGraphicsRootConstantBufferView(0, cbSlotGpu() + (UINT64)inst * kCbStride);
        cmd->IASetVertexBuffers(0, 1, &m.vbv);
        cmd->IASetIndexBuffer(&m.ibv);
        cmd->DrawIndexedInstanced(m.indexCount, 1, 0, 0, 0);
    }

    // ---- one skinned character. Evaluates the pose into this frame's palette slot, then
    // draws the submeshes the look allows (gear:* only when that slot is shown, tattoo only when
    // on) with the model's texture set. Returns false when the palette ring is full.
    // Drain the per-(model,variant,submesh) buckets: pack each into the t7 ring and issue ONE instanced draw
    // per bucket (instancingOn) or one draw per character (off) — the live A/B toggle. Single fence, in-order.
    void flushInstances() {
        if (threadBuckets.empty()) return;
        InstData* dst = reinterpret_cast<InstData*>(instBase + (size_t)slot * kMaxInstEntries * sizeof(InstData));
        const UINT64 ringGpu = instData->GetGPUVirtualAddress() + (UINT64)slot * kMaxInstEntries * sizeof(InstData);
        cmd->SetPipelineState(psoSkin.Get());
        cmd->SetGraphicsRootShaderResourceView(2, palettes->GetGPUVirtualAddress() + (UINT64)slot * kMaxSkinned * kPaletteBytes);   // global palette base; Inst.paletteBase indexes within
        cmd->SetGraphicsRootShaderResourceView(4, ringGpu);                                                                        // t7 Inst ring (this slot)
        const UINT nb = bktCount();
        UINT cursor = 0;
        for (UINT b = 0; b < nb; ++b) {
            UINT total = 0;
            for (auto& tb : threadBuckets) if (b < tb.size()) total += (UINT)tb[b].size();   // sum this bucket across chunks
            if (!total) continue;
            if (cursor + total > kMaxInstEntries) break;   // ring full — remaining dropped this frame
            const UINT baseIdx = cursor;
            for (auto& tb : threadBuckets) {               // concatenate each chunk's contribution (bucket-contiguous)
                if (b >= tb.size() || tb[b].empty()) continue;
                memcpy(dst + cursor, tb[b].data(), tb[b].size() * sizeof(InstData));
                cursor += (UINT)tb[b].size();
            }
            const UINT sub = b % kBktMaxSubs, tmp = b / kBktMaxSubs, variant = tmp % kBktVariants, cmIdx = tmp / kBktVariants;
            if (cmIdx >= models.size()) continue;
            CharModel& cm = models[cmIdx];
            if (variant >= cm.variants.size()) continue;
            const bool isLod = (sub == kBktMaxSubs - 1);   // reserved LOD slot -> the tier-1 decimated mesh
            const bool isLod2 = (sub == kBktMaxSubs - 2);  // reserved slot -> the tier-2 ultra-coarse mesh
            const GpuMesh* mesh; UINT idxCount, firstIdx;
            if (isLod2) { if (!cm.hasLod2) continue; mesh = &cm.lodGpu2; idxCount = (UINT)cm.lodData2.indices.size(); firstIdx = 0; lodDrawn += total; }
            else if (isLod) { if (!cm.hasLod) continue; mesh = &cm.lodGpu; idxCount = (UINT)cm.lodData.indices.size(); firstIdx = 0; lodDrawn += total; }
            else { if (sub >= cm.data.subs.size()) continue; mesh = &cm.gpu; idxCount = cm.data.subs[sub].indexCount; firstIdx = cm.data.subs[sub].firstIndex; }
            const TexSet& ts = cm.variants[variant];
            if (ts.ok) { D3D12_GPU_DESCRIPTOR_HANDLE gh = starlight_gpu_heap_start(srvHeap); gh.ptr += (UINT64)ts.table * srvSize; cmd->SetGraphicsRootDescriptorTable(3, gh); }
            cmd->IASetVertexBuffers(0, 1, &mesh->vbv); cmd->IASetIndexBuffer(&mesh->ibv);
            if (instancingOn) {
                cmd->SetGraphicsRoot32BitConstant(5, baseIdx, 0);
                cmd->DrawIndexedInstanced(idxCount, total, firstIdx, 0, 0);
                ++drawCallCount;
            } else {
                for (UINT k = 0; k < total; ++k) { cmd->SetGraphicsRoot32BitConstant(5, baseIdx + k, 0); cmd->DrawIndexedInstanced(idxCount, 1, firstIdx, 0, 0); ++drawCallCount; }
            }
        }
        instEntryCursor = cursor;
        cmd->SetPipelineState(pso.Get());
    }

    // clip selection from the wire state + crossfade bookkeeping (0.15 s blend)
    void advanceAnim(AnimState& an, int clip, float dt, float speed) {
        if (an.clip != clip) { an.prevClip = an.clip; an.prevT = an.t; an.blend = an.clip < 0 ? 0.0f : 1.0f; an.clip = clip; an.t = 0; }
        an.t += dt * speed;
        if (an.blend > 0) { an.prevT += dt; an.blend = std::max(0.0f, an.blend - dt / 0.15f); }
    }

    // axis-aligned box: center + full size (unit box mesh is centered, half-extent 0.5)
    static XMMATRIX BoxAt(float cx, float cy, float cz, float sx, float sy, float sz) {
        return XMMatrixMultiply(XMMatrixScaling(sx, sy, sz), XMMatrixTranslation(cx, cy, cz));
    }

    // beam: cone with its BASE (radius r) at floor point B and its APEX at ceiling anchor A.
    // The unit cone has apex (0,1,0) / base at y=0, so scale then rotate +Y onto (A-B).
    static XMMATRIX BeamXf(const XMFLOAT3& A, const XMFLOAT3& B, float r) {
        XMVECTOR a = XMLoadFloat3(&A), b = XMLoadFloat3(&B);
        XMVECTOR d = XMVectorSubtract(a, b);
        float len = XMVectorGetX(XMVector3Length(d)); if (len < 0.001f) len = 0.001f;
        XMVECTOR y = XMVector3Normalize(d);
        XMVECTOR ref = (fabsf(XMVectorGetX(y)) < 0.9f) ? XMVectorSet(1, 0, 0, 0) : XMVectorSet(0, 0, 1, 0);
        XMVECTOR x = XMVector3Normalize(XMVector3Cross(ref, y));
        XMVECTOR z = XMVector3Cross(x, y);
        XMMATRIX R; R.r[0] = x; R.r[1] = y; R.r[2] = z; R.r[3] = XMVectorSet(0, 0, 0, 1);
        return XMMatrixMultiply(XMMatrixMultiply(XMMatrixScaling(r * 2.0f, len, r * 2.0f), R), XMMatrixTranslation(B.x, B.y, B.z));
    }

    // visual floor height at (x,z): the dancefloor slab is raised kStageH (server has no stage
    // height — every entity is y=0 there — so anyone standing on the slab is lifted cosmetically).
    void captureBackbuffer(ID3D12Resource* target) {
        D3D12_RESOURCE_DESC td = starlight_resource_desc(target);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT64 total = 0;
        device->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &total);
        if (!readback) {
            D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC bd{}; bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = total; bd.Height = 1;
            bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1; bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            must(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)), "create readback");
        }
        D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = target; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
        cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    void writeShotBmp() {
        // Use the CAPTURED resource's own descriptor (upOutput — what captureBackbuffer copied and sized the
        // readback from), not targets[frame]. Same format/size today, but this removes a latent footprint-mismatch
        // read (a proxy swapchain / format change on targets[frame] could otherwise over-read the readback buffer).
        D3D12_RESOURCE_DESC td = starlight_resource_desc(upOutput);
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64 capturedBytes = 0;
        device->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, &capturedBytes);
        uint8_t* p = nullptr; D3D12_RANGE rr{ 0, (SIZE_T)capturedBytes };
        if (FAILED(readback->Map(0, &rr, reinterpret_cast<void**>(&p))) || !p) { logLine("shot: map failed"); return; }
        FILE* fo = nullptr;
        if (_wfopen_s(&fo, shotPath.c_str(), L"wb") != 0 || !fo) { readback->Unmap(0, nullptr); logLine("shot: open failed"); return; }
        const uint32_t rowBytes = ((w * 3 + 3) / 4) * 4, imgBytes = rowBytes * h, fileBytes = 54 + imgBytes;
        uint8_t hdr[54] = { 'B','M' };
        auto put32 = [&](int at, uint32_t v) { hdr[at] = (uint8_t)v; hdr[at + 1] = (uint8_t)(v >> 8); hdr[at + 2] = (uint8_t)(v >> 16); hdr[at + 3] = (uint8_t)(v >> 24); };
        put32(2, fileBytes); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h);
        hdr[26] = 1; hdr[28] = 24; put32(34, imgBytes);
        fwrite(hdr, 1, 54, fo);
        std::vector<uint8_t> row(rowBytes, 0);
        for (UINT y = 0; y < h; ++y) {                       // BMP is bottom-up, BGR
            const uint8_t* srcRow = p + (size_t)(h - 1 - y) * fp.Footprint.RowPitch;
            for (UINT x = 0; x < w; ++x) { row[x * 3] = srcRow[x * 4 + 2]; row[x * 3 + 1] = srcRow[x * 4 + 1]; row[x * 3 + 2] = srcRow[x * 4]; }
            fwrite(row.data(), 1, rowBytes, fo);
        }
        fclose(fo);
        readback->Unmap(0, nullptr);
        logLine("shot: wrote backbuffer BMP");
    }

    // ---- latency reduction: one of Reflex (DLSS path) / XeLL (XeSS path) / Anti-Lag 2 (any path, AMD driver) ----
    void chooseLatency() {
        std::string want = latencyRequest;
        std::string vendorOwn = ups->latencyName(); for (char& c : vendorOwn) c = (char)tolower((unsigned char)c);   // "reflex" / "xell" / ""
        if (want == "auto") {
            if (ups->latencySupported()) want = vendorOwn;
            else if (antilag.supported()) want = "antilag";
            else want = "off";
            latencyWhy = "auto";
        }
        const bool vendorOn = (want == vendorOwn && !vendorOwn.empty()), antilagOn = (want == "antilag");
        ups->setLatency(vendorOn); antilag.setEnabled(antilagOn && antilag.supported()); latencyChoice = "off";
        if (want == "off") { latencyWhy += (latencyWhy.empty() ? "" : "; ") + std::string("requested off"); }
        else if (antilagOn) {
            latencyChoice = "antilag2";
            if (!antilag.supported()) latencyWhy += (latencyWhy.empty() ? "" : "; ") + antilag.status();
        }
        else if (vendorOn) {
            latencyChoice = vendorOwn;
            if (!ups->latencyEnabled()) latencyWhy += (latencyWhy.empty() ? "" : "; ") + ups->latencyStatus();
        }
        else {
            latencyChoice = want;
            latencyWhy += (latencyWhy.empty() ? "" : "; ") + std::string("unavailable: ") + want + " is not on the " + up::pathName(ups->path()) + " path"
                + (want == "reflex" ? " (needs --upscaler dlss)" : want == "xell" ? " (needs --upscaler xess)" : " (unknown --latency)");
        }
        logLine("latency " + latencyChoice + ": " + latencyStatus() + (latencyWhy.empty() ? "" : " [" + latencyWhy + "]"));
    }
    // "live" | "off" | "hardware-gated (..)" | "unavailable (..)" | "n/a (..)"
    std::string latencyStatus() const {
        if (latencyChoice == "off") return "off";
        if (latencyChoice == "antilag2") return antilag.status();
        std::string own = ups ? ups->latencyName() : ""; for (char& c : own) c = (char)tolower((unsigned char)c);
        if (ups && latencyChoice == own) return ups->latencyStatus();
        return latencyWhy.empty() ? std::string("unavailable") : latencyWhy;
    }
    std::string latencyTitle() const {
        std::string st = latencyStatus(); size_t sp = st.find(' '); if (sp != std::string::npos) st = st.substr(0, sp);
        return latencyChoice == "off" ? std::string("latency off") : latencyChoice + " " + st;
    }

    // MULTICORE PROOF: the engine's real per-character animation math (evalPose) is pure compute with
    // thread_local scratch => embarrassingly parallel. Run N poses on 1 thread, then on 2/4/8/16/hw
    // threads, report the speedup.
    void mtProof(int topN) {
        if (models.empty() || models[0].data.joints.empty() || models[0].data.clips.empty()) {
            logLine("MT-PROOF: no skinned model+clip loaded, cannot run"); return;
        }
        const model::SkinnedModel& d = models[0].data;
        unsigned hw = std::thread::hardware_concurrency(); if (!hw) hw = 1;
        auto run = [&](int N, unsigned T) -> double {
            auto work = [&](int lo, int hi) {
                std::vector<XMFLOAT4X4> pal((size_t)kMaxJoints);
                for (int i = lo; i < hi; ++i) model::evalPose(d, 0, (float)i * 0.0001f, -1, 0.0f, 0.0f, pal.data(), nullptr);
            };
            auto t0 = std::chrono::steady_clock::now();
            if (T <= 1) work(0, N);
            else {
                std::vector<std::thread> th; int per = (N + (int)T - 1) / (int)T;
                for (unsigned k = 0; k < T; ++k) { int lo = (int)k * per, hi = std::min(N, lo + per); if (lo < hi) th.emplace_back(work, lo, hi); }
                for (auto& x : th) x.join();
            }
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        };
        std::vector<unsigned> Ts; for (unsigned t : { 1u,2u,4u,8u,16u,hw }) if (t <= hw && std::find(Ts.begin(), Ts.end(), t) == Ts.end()) Ts.push_back(t);
        std::sort(Ts.begin(), Ts.end());
        std::vector<int> Ns; for (int n : { 2000,10000,50000,topN }) if (n > 0 && std::find(Ns.begin(), Ns.end(), n) == Ns.end()) Ns.push_back(n);
        std::sort(Ns.begin(), Ns.end());
        logLine("=== MT-PROOF: parallel evalPose | hw_threads=" + std::to_string(hw) + " joints=" + std::to_string(d.joints.size()) + " ===");
        std::string js = "{\"proof\":\"evalPose-multicore\",\"hwThreads\":" + std::to_string(hw) + ",\"joints\":" + std::to_string(d.joints.size()) + ",\"runs\":[";
        bool firstR = true;
        for (int N : Ns) {
            run(N, 1);                       // warm caches
            double base = run(N, 1);
            std::string row = "  N=" + std::to_string(N) + " poses: ";
            if (!firstR) js += ","; firstR = false;
            js += "{\"N\":" + std::to_string(N) + ",\"ms\":{"; bool f2 = true;
            for (unsigned T : Ts) {
                double best = 1e18; for (int rep = 0; rep < 3; ++rep) best = std::min(best, run(N, T));
                char b[96];  snprintf(b, sizeof(b), "%ut=%.2fms(%.1fx) ", T, best, base > 0 ? base / best : 0.0); row += b;
                if (!f2) js += ","; f2 = false;
                char jb[64]; snprintf(jb, sizeof(jb), "\"%u\":%.3f", T, best); js += jb;
            }
            js += "}}";
            logLine(row);
        }
        js += "]}\n";
        if (!reportPath.empty()) { FILE* f = nullptr; if (fopen_s(&f, reportPath.c_str(), "w") == 0 && f) { fputs(js.c_str(), f); fclose(f); } }
        logLine("=== MT-PROOF done ===");
    }

    void writeReport() {
        double sum = 0; for (float f : benchFrames) sum += f;
        std::vector<float> sorted = benchFrames; std::sort(sorted.begin(), sorted.end());
        double avgMs = benchFrames.empty() ? 0 : sum / benchFrames.size() * 1000.0;
        double p99Ms = sorted.empty() ? 0 : sorted[std::min(sorted.size() - 1, (size_t)(sorted.size() * 0.99))] * 1000.0;
        double fps = avgMs > 0 ? 1000.0 / avgMs : 0;
        // GPU-timestamp stats = the REAL frame cost, immune to the present/DWM throttle (the "fps"/"msAvg"
        // above are CPU-frame-time and get inflated to ~20fps when a non-foreground window is throttled).
        double gpuSum = 0; for (float f : benchGpuMs) gpuSum += f;
        std::vector<float> gsort = benchGpuMs; std::sort(gsort.begin(), gsort.end());
        double gpuAvg = benchGpuMs.empty() ? 0 : gpuSum / benchGpuMs.size();
        double gpuP99 = gsort.empty() ? 0 : gsort[std::min(gsort.size() - 1, (size_t)(gsort.size() * 0.99))];
        double gpuMin = gsort.empty() ? 0 : gsort.front();
        double gpuFps = gpuAvg > 0 ? 1000.0 / gpuAvg : 0;
        double crSum = 0; for (float f : benchCpuRecMs) crSum += f;
        std::vector<float> crsort = benchCpuRecMs; std::sort(crsort.begin(), crsort.end());
        double cpuRecAvg = benchCpuRecMs.empty() ? 0 : crSum / benchCpuRecMs.size();
        double cpuRecP99 = crsort.empty() ? 0 : crsort[std::min(crsort.size() - 1, (size_t)(crsort.size() * 0.99))];
        double poSum = 0; for (float f : benchPoseMs) poSum += f;
        double poseAvg = benchPoseMs.empty() ? 0 : poSum / benchPoseMs.size();
        std::string ver = ups->versionString(), fg = ups->fgStatus(), extra = ups->extraStatus();
        uint64_t presentedN = ups->hasPresentedCounter() ? benchPresented : (uint64_t)benchFrames.size();
        auto esc = [](std::string s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') o.push_back('\\'); o.push_back(c); } return o; };
        // Report block: what was actually loaded + drawn, and how the pacing behaved
        std::string modelsJson = "[";
        size_t joints = 0, clips = 0;
        for (size_t i = 0; i < models.size(); ++i) {
            const CharModel& cm = models[i];
            joints += cm.data.joints.size(); clips += cm.data.clips.size();
            int texOk = 0; for (const auto& v : cm.variants) if (v.ok) ++texOk;
            char mb[512];
            snprintf(mb, sizeof(mb), "%s{\"name\":\"%s\",\"verts\":%zu,\"tris\":%zu,\"submeshes\":%zu,\"joints\":%zu,\"clips\":%zu,\"atlasUV\":%s,\"variants\":%d,\"scale\":%.3f}",
                i ? "," : "", esc(cm.data.name).c_str(), cm.data.verts.size(), cm.data.indices.size() / 3, cm.data.subs.size(), cm.data.joints.size(), cm.data.clips.size(),
                cm.data.atlasUV ? "true" : "false", texOk, cm.scale);
            modelsJson += mb;
        }
        modelsJson += "]";
        char m6[2048];
        snprintf(m6, sizeof(m6),
            "\"inflight\":%d,\"stalls\":%u,\"stallMs\":%.2f,\"models\":%s,\"joints\":%zu,\"clips\":%zu,\"textures\":%d,\"textureMB\":%.1f,"
            "\"texturesFromCache\":%d,\"texRes\":%u,\"modelLoadMs\":%.1f,\"textureLoadMs\":%.1f,\"skinned\":%u,\"charsWhy\":\"%s\","
            "\"fur\":%s,\"furShells\":%u,\"furShellsNear\":%d,\"furShellsMid\":%d,\"instMax\":%u,\"instDropped\":%llu,\"instCap\":%u,\"skinCap\":%u,\"crowdParsed\":%d,\"draws\":%u,\"instEntries\":%u,\"culled\":%u,\"lod\":%u,\"instancingOn\":%s",
            inflight, stallCount, stallMs, modelsJson.c_str(), joints, clips, texturesLoaded, (double)textureBytes / (1024.0 * 1024.0),
            texturesFromCache, texRes, modelLoadMs, textureLoadMs, skinnedDrawnLast, esc(charsWhy).c_str(),
            noFur ? "false" : "true", furShellsLast, STARLIGHT_FUR_SHELLSNEAR, STARLIGHT_FUR_SHELLSMID,
            benchInstMax, (unsigned long long)instDropped, kMaxInstances, kMaxSkinned, lastCrowd, drawCallLast, instEntriesLast, culledLast, lodDrawnLast, instancingOn ? "true" : "false");
        char buf[4608];
        snprintf(buf, sizeof(buf),
            "{\"instanceMemory\":\"%s\",\"upscaler\":\"%s\",\"requested\":\"%s\",\"reason\":\"%s\",\"version\":\"%s\",\"extra\":\"%s\","
            "\"mode\":\"%s\",\"renderW\":%u,\"renderH\":%u,\"outW\":%u,\"outH\":%u,"
            "\"fgRequested\":%s,\"fgEnabled\":%s,\"fgStatus\":\"%s\",\"vsync\":%s,"
            "\"latencyRequested\":\"%s\",\"latency\":\"%s\",\"latencyStatus\":\"%s\",\"latencyDetail\":\"%s\","
            "\"frames\":%zu,\"presented\":%llu,\"presentedFps\":%.1f,\"seconds\":%.3f,\"fps\":%.1f,\"msAvg\":%.3f,\"msP99\":%.3f,"
            "\"gpuMsAvg\":%.3f,\"gpuMsMin\":%.3f,\"gpuMsP99\":%.3f,\"gpuFps\":%.1f,"
            "\"cpuRecMsAvg\":%.3f,\"cpuRecMsP99\":%.3f,\"poseMsAvg\":%.3f,\"gatherMsAvg\":%.2f,\"emitMsAvg\":%.2f,\"mergeMsAvg\":%.2f,"
            "%s,"
            "\"adapter\":\"%s\",\"vendor\":\"0x%04x\",\"server\":%s}\n",
            instanceGpuUpload ? "gpu-upload" : "system-upload", up::pathName(ups->path()), esc(upsRequest).c_str(), esc(upsReason).c_str(), esc(ver).c_str(), esc(extra).c_str(),
            ups->modeName(ups->mode()), rw, rh, w, h,
            fgRequest ? "true" : "false", ups->fgEnabled() ? "true" : "false", esc(fg).c_str(), vsync ? "true" : "false",
            esc(latencyRequest).c_str(), esc(latencyChoice).c_str(), esc(latencyStatus()).c_str(), esc(latencyWhy).c_str(),
            benchFrames.size(), (unsigned long long)presentedN, sum > 0 ? presentedN / sum : 0.0, sum, fps, avgMs, p99Ms,
            gpuAvg, gpuMin, gpuP99, gpuFps,
            cpuRecAvg, cpuRecP99, poseAvg, profileSamples ? profileGatherSum / profileSamples : 0.0, profileSamples ? profileEmitSum / profileSamples : 0.0, profileSamples ? profileMergeSum / profileSamples : 0.0,
            m6,
            esc(adapterName).c_str(), adapterVendor, hasServer ? "true" : "false");
        FILE* f = nullptr;
        if (fopen_s(&f, reportPath.c_str(), "w") == 0 && f) { fputs(buf, f); fclose(f); logLine("report written: " + reportPath); }
        else logLine("report: cannot write " + reportPath);
    }
    std::function<void()> editorDraw;
    float editorCameraDistance = 0;
    XMFLOAT3 editorEye{0,9.36f,13},editorTarget{0,0,0};
    XMFLOAT3 editorUp{0,1,0};
    bool hostCamera=false;XMFLOAT3 hostEye{0,3,8},hostTarget{0,0,0};float hostFovDegrees=50.0f,hostNear=0.1f,hostFar=400.0f;
    double stageMarks[10]{};double inflightWaitMs=0,signalMs=0;uint32_t passFrameId[3]{};   // diagnostic: stage timestamps, tail timers, and the frame each GPU pass slot was submitted for   // diagnostic: timingNow() at the main stages of the last frame
    void renderEmpty(HWND hwnd) {
        (void)hwnd;stageMarks[0]=timingNow();
        struct FrameTail{RenderDevice* d;uint32_t id;~FrameTail(){d->stageMarks[9]=timingNow();if(starlight_diag::events_enabled()){starlight_diag::evlog("rf.end",id,1000*(d->stageMarks[9]-d->stageMarks[0]),1000*(d->stageMarks[9]-d->stageMarks[7]));starlight_diag::evlog("rf.tail",d->signalMs,d->inflightWaitMs,1000*(d->stageMarks[9]-d->stageMarks[8]));}}} frameTail{this,frameIndex};
        starlight_diag::evlog("rf.begin",frameIndex,slot);
        if (frameLatencyWaitable && !auditOffscreen){double started=timingNow();WaitForSingleObject(frameLatencyWaitable,1000);measuredPresentWaitMs+=(timingNow()-started)*1000;}stageMarks[1]=timingNow();
        paceFrame();stageMarks[2]=timingNow();
        uint64_t now = GetTickCount64();
        float dt = lastTick ? (now-lastTick)*0.001f : 0.016f; lastTick=now;
        const float nearZ=hostCamera?hostNear:0.1f, farZ=hostCamera?hostFar:400.0f, fovY=XMConvertToRadians(hostCamera?hostFovDegrees:50.0f);
        XMVECTOR eye=editorCameraDistance>0?XMLoadFloat3(&editorEye):(hostCamera?XMLoadFloat3(&hostEye):XMVectorSet(0,3,8,0)), ctr=editorCameraDistance>0?XMLoadFloat3(&editorTarget):(hostCamera?XMLoadFloat3(&hostTarget):XMVectorZero());
        XMMATRIX view=editorCameraDistance>0?XMMatrixLookAtRH(eye, ctr, XMLoadFloat3(&editorUp)):XMMatrixLookAtLH(eye, ctr, XMVectorSet(0,1,0,0));
        float cameraFov=editorCameraDistance>0?2*atanf(tanf(XMConvertToRadians(48.0f)*.5f)*(float)rh/rw):fovY;
        XMMATRIX proj=editorCameraDistance>0?XMMatrixPerspectiveFovRH(cameraFov,(float)rw/rh,nearZ,farZ):XMMatrixPerspectiveFovLH(cameraFov,(float)rw/rh,nearZ,farZ);
        viewProjNoJit=XMMatrixMultiply(view,proj); viewProj=viewProjNoJit;
        XMStoreFloat3(&camWorld,eye); jitterX=jitterY=0;
        if(curModel.size()!=kMaxInstances){curModel.resize(kMaxInstances);prevModel.resize(kMaxInstances);}
        ups->beginFrame(frameIndex);
        const float clear[4]={0.025f,0.03f,0.045f,1};
        // ---- acquire this frame's slot. The CPU only waits when the slot's previous
        // submission (kFrameCount frames ago) has not retired — with --inflight 3 that is the
        // GPU-bound case, without flushing every frame.
        ups->renderSubmitStart();
        slot = frameIndex % kFrameCount;
        if (fence->GetCompletedValue() < slotFence[slot]) {
            auto w0 = std::chrono::steady_clock::now();
            waitFence(slotFence[slot]);
            stallMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0).count(); ++stallCount;
        }
        alloc = allocs[slot];
        mustHr(alloc->Reset(), "reset frame command allocator");
        mustHr(cmd->Reset(alloc.Get(), pso.Get()), "reset frame command list");
        if (tsHeap) {   // GPU timer: the slot fence was waited above, so this slot's PREVIOUS resolve is ready to read
            if (tsFreq) {
                void* p = nullptr; D3D12_RANGE rr{ (SIZE_T)(sizeof(UINT64) * 2 * slot), (SIZE_T)(sizeof(UINT64) * (2 * slot + 2)) };
                if (SUCCEEDED(tsReadback->Map(0, &rr, &p)) && p) {
                    const UINT64* ts = reinterpret_cast<const UINT64*>(static_cast<const char*>(p) + sizeof(UINT64) * 2 * slot);
                    if (ts[1] > ts[0]) { lastGpuMs = (float)((double)(ts[1] - ts[0]) * 1000.0 / (double)tsFreq); gpuMsNow = lastGpuMs; }
                    D3D12_RANGE wr{ 0, 0 }; tsReadback->Unmap(0, &wr);
                }
            }
            cmd->EndQuery(tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot);   // GPU-work start
            if (passHeap && tsFreq) {
                void* q = nullptr; D3D12_RANGE pr{ (SIZE_T)(sizeof(UINT64) * 16 * slot), (SIZE_T)(sizeof(UINT64) * (16 * slot + 16)) };
                if (SUCCEEDED(passReadback->Map(0, &pr, &q)) && q) {
                    const UINT64* pt = reinterpret_cast<const UINT64*>(static_cast<const char*>(q) + sizeof(UINT64) * 16 * slot);
                    for (int i = 0; i < 4; ++i) if (pt[i + 1] >= pt[i] && pt[i] != 0) lastPassMs[i] = (float)((double)(pt[i + 1] - pt[i]) * 1000.0 / (double)tsFreq);
                    if(pt[1]&&pt[7]&&pt[8]&&pt[9]&&pt[10]&&pt[2]&&pt[1]<=pt[7]&&pt[7]<=pt[8]&&pt[8]<=pt[9]&&pt[9]<=pt[10]&&pt[10]<=pt[2]){const double toMs=1000.0/double(tsFreq);starlight_diag::evlog("gpu4",double(pt[7]-pt[1])*toMs,double(pt[8]-pt[7])*toMs,double(pt[9]-pt[8])*toMs);starlight_diag::evlog("gpu5",double(pt[10]-pt[9])*toMs,double(pt[2]-pt[10])*toMs,0);}starlight_diag::evlog("gpu",passFrameId[slot],lastPassMs[0],lastPassMs[1]);starlight_diag::evlog("gpu2",passFrameId[slot],lastPassMs[2],lastPassMs[3]);if(pt[5]>=pt[0]&&pt[6]>=pt[5]&&pt[1]>=pt[6]&&pt[5]&&pt[6])starlight_diag::evlog("gpu3",double(pt[5]-pt[0])*1000.0/double(tsFreq),double(pt[6]-pt[5])*1000.0/double(tsFreq),double(pt[1]-pt[6])*1000.0/double(tsFreq));{static UINT64 previousStart=0;static UINT64 previousEnd=0;if(starlight_diag::events_enabled()&&pt[0]&&pt[4]>=pt[0]){starlight_diag::evlog("gpuT",previousStart?double(pt[0]-previousStart)*1000.0/double(tsFreq):0,double(pt[4]-pt[0])*1000.0/double(tsFreq),previousEnd&&pt[0]>=previousEnd?double(pt[0]-previousEnd)*1000.0/double(tsFreq):-1);previousStart=pt[0];previousEnd=pt[4];}}
                    D3D12_RANGE wr{ 0, 0 }; passReadback->Unmap(0, &wr);
                }
                passFrameId[slot]=frameIndex;gpuMark(0);
            }
        }
        stageMarks[3]=timingNow();auto _recT0 = std::chrono::steady_clock::now();   // CPU record-timer start (single-thread draw submission cost)
        poseAccumMs = 0;                                  // per-frame evalPose accumulator (parallelizable portion)
        instEntryCursor = 0; drawCallCount = 0; culled = 0; lodDrawn = 0; curModelByKey.clear(); items.clear();   // instancing: reset per-frame collect state
        instCursor = 0; skinCursor = (UINT)(models.size() * kFarPoses); skinnedDrawn = 0; furShellsDrawn = 0;   // reserve [0..F) palette slots for the shared far-pose pool
        if (srvHeap) { ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() }; cmd->SetDescriptorHeaps(1, heaps); }

        // scene pass -> render-size color + mvec + depth (resting state = shader resource)
        if (msaa <= 1) barrier(sceneColor.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (msaa <= 1) barrier(sceneMvec.Get(),  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (msaa <= 1) barrier(depth.Get(),      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
        rtvs[0] = starlight_cpu_heap_start(rtvHeap); rtvs[0].ptr += (SIZE_T)kFrameCount * rtvSize;
        rtvs[1] = rtvs[0]; rtvs[1].ptr += rtvSize;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = starlight_cpu_heap_start(dsvHeap);
        cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        cmd->OMSetRenderTargets(2, rtvs, FALSE, &dsv);

        D3D12_VIEWPORT vp{ 0, 0, (float)rw, (float)rh, 0.0f, 1.0f };
        D3D12_RECT sr{ 0, 0, (LONG)rw, (LONG)rh };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sr);
        cmd->SetGraphicsRootSignature(rootSig.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        FrameCB fc{}; fc.fog=XMFLOAT4(clear[0],clear[1],clear[2],0); writeFrameCB(fc);
        if (instCursor < kMaxInstances) {   // background fill (see psoBg; reads Frame.fog)
            UINT bgInst = instCursor++;
            writeInstance(bgInst, XMMatrixIdentity(), XMFLOAT3(clear[0], clear[1], clear[2]), 0.0f, 1.0f, 0.0f, false);
            cmd->SetGraphicsRootConstantBufferView(0, cbSlotGpu() + (UINT64)bgInst * kCbStride);
            cmd->SetPipelineState(psoBg.Get());
            cmd->DrawInstanced(3, 1, 0, 0);
            cmd->SetPipelineState(pso.Get());
        }

        if(editorDraw){try{editorDraw();}catch(...){(void)cmd->Close();throw;}}   // an exception must not leave the frame command list open for every later frame
        stageMarks[4]=timingNow();gpuMark(2);
        // ---- reconstruct render-size scene -> output-size upOutput, copy into the backbuffer ----
        if (msaa > 1) resolveScene();
        if (msaa <= 1) barrier(sceneColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (msaa <= 1) barrier(sceneMvec.Get(),  D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        if (msaa <= 1) barrier(depth.Get(),      D3D12_RESOURCE_STATE_DEPTH_WRITE,   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        up::FrameIn fi;
        fi.cmd = cmd.Get(); fi.color = sceneColor.Get(); fi.depth = depth.Get(); fi.mvec = sceneMvec.Get();
        auto outputTarget = auditOffscreen ? auditOutput.Get() : targets[frame].Get();
        fi.exposure = exposureTex.Get(); fi.output = upOutput.Get(); fi.backbuffer = outputTarget;
        fi.renderW = rw; fi.renderH = rh; fi.outW = w; fi.outH = h;
        fi.jitterX = jitterX; fi.jitterY = jitterY; fi.dtMs = dt * 1000.0f; fi.reset = resetHistory;
        fi.frameIndex = frameIndex; fi.backbufferIndex = frame;
        XMStoreFloat4x4(&fi.view, view); XMStoreFloat4x4(&fi.proj, proj);
        XMStoreFloat4x4(&fi.viewProj, viewProjNoJit); XMStoreFloat4x4(&fi.prevViewProj, prevViewProjNoJit);
        fi.camPos = camWorld;
        { XMFLOAT3 f3; XMStoreFloat3(&f3, XMVector3Normalize(XMVectorSubtract(ctr, eye))); fi.camFwd = f3;
          XMStoreFloat3(&f3, XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), XMVectorSubtract(ctr, eye)))); fi.camRight = f3;
          XMStoreFloat3(&f3, XMVector3Cross(XMLoadFloat3(&fi.camFwd), XMLoadFloat3(&fi.camRight))); fi.camUp = f3; }
        fi.nearZ = nearZ; fi.farZ = farZ; fi.fovY = fovY; fi.aspect = (float)w / (float)h;
        if(!linearHdrScene)ups->evaluate(fi);stageMarks[5]=timingNow();gpuMark(3);

        barrier(upOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(outputTarget, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(outputTarget, upOutput.Get());
        // ---- --shot: grab this frame's reconstructed output on the way to PRESENT ----
        bool shotNow = !shotPath.empty() && !shotDone && (now - startTick) >= shotAtMs;
        if (shotNow) captureBackbuffer(upOutput.Get());
        barrier(upOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(outputTarget, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
        fi.backbufferState = D3D12_RESOURCE_STATE_PRESENT;
        if (!auditOffscreen) ups->beforePresent(fi);
        if (tsHeap) {   // GPU-work end + resolve this slot's pair to the readback buffer
            cmd->EndQuery(tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot + 1);
            if (passHeap) { gpuMark(4); cmd->ResolveQueryData(passHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 16 * slot, 11, passReadback.Get(), sizeof(UINT64) * 16 * slot); }
            cmd->ResolveQueryData(tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot, 2, tsReadback.Get(), sizeof(UINT64) * 2 * slot);
        }
        lastCpuRecMs = (float)std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _recT0).count();
        lastPoseMs = (float)poseAccumMs;
        mustHr(cmd->Close(), "close command list");
        ID3D12CommandList* lists[] = { cmd.Get() };
        queue->ExecuteCommandLists(1, lists);
        antilag.markEndOfFrameRendering();stageMarks[6]=timingNow();
        antilag.setFrameGenFrameType(false);
        if (!auditOffscreen) ups->present(swap.Get(), vsync ? 1 : 0, (!vsync && tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0);
        if (!auditOffscreen) ups->afterPresent();stageMarks[7]=timingNow();
        // ---- retire by slot instead of flushing. The fence value signalled here is what the
        // slot waits on kFrameCount frames from now; --inflight < 3 waits earlier (1 = old behaviour).
        slotFence[slot] = ++fenceVal;
        {const double s0=timingNow();mustHr(queue->Signal(fence.Get(), fenceVal), "signal frame fence");signalMs=(timingNow()-s0)*1000;}
        {const double w0=timingNow();if (inflight < (int)kFrameCount) waitFence(fenceVal - (UINT64)(inflight - 1));inflightWaitMs=(timingNow()-w0)*1000;}
        if (shotNow) { flush(); writeShotBmp(); shotDone = true; }
        stageMarks[8]=timingNow();
        if (d3dDebug) drainDebug();
        skinnedDrawnLast = skinnedDrawn; furShellsLast = furShellsDrawn;
        frame = swap->GetCurrentBackBufferIndex();
        if (frame >= kFrameCount) {   // Streamline proxy swapchain (DLSS-G loaded) can report an index beyond our BufferCount (issue #35) -> targets[frame] OOB = intermittent crash. Guard it.
            if (!warnedBbIndex) { warnedBbIndex = true; logLine("WARN: swapchain backbuffer index " + std::to_string(frame) + " >= kFrameCount " + std::to_string(kFrameCount) + " (Streamline proxy) - clamping (evidence of the #35 crash vector)"); }
            frame %= kFrameCount;
        }

        // history for next frame's motion vectors
        prevViewProjNoJit = viewProjNoJit;
        std::swap(curModel, prevModel);
        resetHistory = (instCursor != prevInstCount);
        prevInstCount = instCursor;
        instEntriesLast = instEntryCursor; drawCallLast = drawCallCount; culledLast = culled; lodDrawnLast = lodDrawn;   // for the live HUD / report
        prevModelByKey.swap(curModelByKey);                                // this frame's transforms become next frame's history (batching-safe)
        cPrev.swap(cCur);                                                  // crowd per-index history: this frame -> next frame's prev
        ++frameIndex;

        // fps (title) + bench window
        ++fpsFrames;
        if (now - fpsTick >= 500) { fpsNow = fpsTick ? fpsFrames * 1000.0f / (float)(now - fpsTick) : 0; fpsTick = now; fpsFrames = 0; }
        if (benchMs) {
            if (!benchT0) benchT0 = now;
            else if (now - benchT0 >= 1500) { benchFrames.push_back(dt); benchGpuMs.push_back(lastGpuMs); benchCpuRecMs.push_back(lastCpuRecMs); benchPoseMs.push_back(lastPoseMs); profileGatherSum += lastGatherMs; profileEmitSum += lastEmitMs; profileMergeSum += lastMergeMs; ++profileSamples; benchPresented += ups->takePresentedCount(); if (instCursor > benchInstMax) benchInstMax = instCursor; }
            else ups->takePresentedCount();
            if (now - benchT0 >= 1500 + benchMs) { writeReport(); PostQuitMessage(0); benchMs = 0; }
        }
    }

    // --report <json>: what actually ran (path, exact version, mode, sizes, FG) + the numbers.

};
