#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
// net module (standalone): WebSocket transport + protocol structs / parsers. json.hpp is large;
// scope-suppress its third-party warnings only. NOMINMAX above keeps json.hpp's std::max/min clean.
#pragma warning(push)
#pragma warning(disable: 4127 4244 4267 4996 5054 4996)
#include "net/world_state.h"
#include "net/ws_client.h"
#pragma warning(pop)

#include "render_device.h"
#include "gen/gen_flags.h"
static XMFLOAT3 ColorwayRGB(const std::string& c, const XMFLOAT3& fallback) {
    if (c == "pink")    return XMFLOAT3(1.00f, 0.45f, 0.75f);
    if (c == "magenta") return XMFLOAT3(1.00f, 0.10f, 0.80f);
    if (c == "white")   return XMFLOAT3(1.00f, 1.00f, 1.00f);
    if (c == "silver")  return XMFLOAT3(0.78f, 0.80f, 0.86f);
    if (c == "blue")    return XMFLOAT3(0.25f, 0.45f, 1.00f);
    if (c == "violet")  return XMFLOAT3(0.60f, 0.30f, 0.95f);
    if (c == "black")   return XMFLOAT3(0.06f, 0.06f, 0.08f);
    if (c == "gold")    return XMFLOAT3(1.00f, 0.78f, 0.25f);
    if (c.size() == 7 && c[0] == '#') {
        unsigned v = 0; bool ok = true;
        for (size_t i = 1; i < 7; ++i) {
            char ch = c[i]; unsigned d;
            if (ch >= '0' && ch <= '9') d = (unsigned)(ch - '0');
            else if (ch >= 'a' && ch <= 'f') d = (unsigned)(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') d = (unsigned)(ch - 'A' + 10);
            else { ok = false; break; }
            v = (v << 4) | d;
        }
        if (ok) return XMFLOAT3(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f);
    }
    return fallback;
}
static const FurProfile kFurFemale = { 0.012f, 0.85f, 0.25f, 0.80f, 0.50f, 1.00f };   // sleek velvet coat (spec §9)
static const FurProfile kFurMale   = { 0.016f, 0.45f, 0.80f, 0.45f, 0.30f, 0.90f };
struct Renderer : RenderDevice {
    net::WorldState cachedSnap_;  // render-thread cache of the latest snapshot; re-copied ONLY when the seq changes
    uint64_t cachedSeq_ = ~0ull;  // (snapshots arrive ~20-30Hz; rendering is far faster — don't re-copy 100k crowd every frame)

    net::LatestState* state = nullptr;
    net::WsClient*    ws = nullptr;                  // for input sends (null in pure-mock runs)
    std::string       myId;
    // room constants (from welcome; fallbacks match server.mjs HALF / BAR / roomAmbiance DANCEFLOOR)
    float roomHalf = 30.0f, barX = 0.0f, barZ = -22.0f;
    const float kCeilY = 7.5f, kStageR = 9.3f, kStageH = 0.1f;

    // follow-spotlights: three rigs that ride the ceiling ABOVE the player (offset per rig, trailing
    // with their own lag) and aim at a smoothed floor target with a slow wobble — so the beams tilt
    // and sweep while she moves, then settle near-vertical over her when she stops.
    struct SpotRig { XMFLOAT3 offset; XMFLOAT3 color; float lag, wobble, radius; float tx = 0, tz = 0, ax = 0, az = 0; bool init = false; };
    SpotRig spots[3] = {
        { { -4.5f, 0, -2.5f }, { 1.00f, 0.12f, 0.80f }, 3.2f, 0.9f, 1.6f },   // magenta
        { {  4.5f, 0, -2.5f }, { 0.25f, 0.45f, 1.00f }, 2.3f, 0.7f, 1.9f },   // blue
        { {  0.0f, 0,  4.5f }, { 1.00f, 1.00f, 1.00f }, 4.5f, 1.2f, 1.3f },   // white
    };

    // input (server-authoritative; we only SEND intents) + camera yaw
    struct Input {
        uint32_t seq = 0; uint64_t lastActive = 0; uint32_t sent = 0;
        std::chrono::steady_clock::time_point lastSendTp{};
        bool prevJump = false, prevDodge = false, prevFire = false, prevCharge = false;
        float yaw = 0.0f;          // last facing sent (server aims with it)
        float camYaw = 0.0f;       // orbit angle; auto-orbits when idle, arrows steer it
        bool  autoWalk = false;    // --auto: walk to the bar hands-free (dev/verification)
    } in;

    // counts for the title bar + phase-change log

    net::Encounter lastPhase = net::Encounter::Unknown;

    void init(HWND hwnd, UINT w, UINT h, const Mesh& forge, net::LatestState* st, net::WsClient* wsc, const std::string& id) {
        state=st; ws=wsc; myId=id; hasServer=wsc!=nullptr; RenderDevice::init(hwnd,w,h,forge);
    }
    void loadCharacters() override {
        if (noChars) { charsWhy = "--nochars"; return; }
        {   // defaults = the repo's asset build dir: native-dx12\bin\..\..\build\{models,textures\bakes-v1}
            char exeP[MAX_PATH]; GetModuleFileNameA(nullptr, exeP, MAX_PATH);
            std::string root(exeP); size_t sl = root.find_last_of("\\/"); root = (sl == std::string::npos) ? "." : root.substr(0, sl);
            root += "\\..\\..\\build";
            if (modelsDir.empty()) modelsDir = root + "\\models";
            if (texturesDir.empty()) texturesDir = root + "\\textures\\bakes-v1";
        }
        struct Want { const char* name; };
        const Want want[] = { { "fox-lead" }, { "fox" }, { "wolf" }, { "snowleopard" } };
        auto t0 = std::chrono::steady_clock::now();
        for (const Want& wnt : want) {
            CharModel cm; std::string err;
            std::string glb = modelsDir + "\\" + wnt.name + ".glb";
            std::string texDir = texturesDir + "\\" + wnt.name;
            std::string uvBin = texDir + "\\" + wnt.name + "_atlas_uv.bin";
            if (!model::loadSkinned(glb, uvBin, cm.data, err)) { logLine(std::string("model ") + wnt.name + ": " + err); continue; }
            if (!err.empty()) logLine(std::string("model ") + wnt.name + ": " + err);
            if (cm.data.joints.size() > kMaxJoints) { logLine(std::string("model ") + wnt.name + ": too many joints (" + std::to_string(cm.data.joints.size()) + ")"); continue; }
            for(auto& sub : cm.data.subs) if(sub.node.rfind("gear:",0)==0) sub.gearSlot=sub.node.substr(5);
            const model::SkinnedModel& d = cm.data;
            cm.clipIdle = d.clipIndex("idle"); cm.clipWalk = d.clipIndex("walk"); cm.clipRun = d.clipIndex("run"); cm.clipJump = d.clipIndex("jump");
            cm.clipDodge = d.clipIndex("dodge_roll_f"); cm.clipCharge = d.clipIndex("bow_charge"); cm.clipFire = d.clipIndex("bow_release"); cm.clipSway = d.clipIndex("tail_sway");
            cm.jHead = d.jointIndex("head"); cm.jShoulderL = d.jointIndex("shoulder_l"); cm.jShoulderR = d.jointIndex("shoulder_r");
            cm.jHandL = d.jointIndex("hand_l"); cm.jHandR = d.jointIndex("hand_r"); cm.jFootL = d.jointIndex("foot_l"); cm.jFootR = d.jointIndex("foot_r");
            float ht = std::max(0.001f, d.bbMax.y - d.bbMin.y);
            cm.scale = (ht > 1.4f && ht < 2.4f) ? 1.0f : 1.8f / ht;   // charfactory rigs are already ~1.8 m
            logLine(std::string("model ") + wnt.name + ": " + std::to_string(d.verts.size()) + " verts, " + std::to_string(d.indices.size() / 3) + " tris, "
                + std::to_string(d.joints.size()) + " joints, " + std::to_string(d.clips.size()) + " clips, " + std::to_string(d.subs.size()) + " submeshes, height "
                + std::to_string(ht) + (d.atlasUV ? ", atlas uv" : ", glb uv"));
            modelByName[wnt.name] = (int)models.size();
            models.push_back(std::move(cm));
        }
        modelLoadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (models.empty()) { charsWhy = "no models loaded from " + modelsDir; logLine("characters OFF: " + charsWhy); return; }

        // descriptor heap: kTexPerSet SRVs per texture set (basecolor, normal, roughness, ao, furflow, sparkle)
        D3D12_DESCRIPTOR_HEAP_DESC hd{}; hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = kTexPerSet * 4 * (UINT)models.size() + kTexPerSet;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        must(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&srvHeap)), "CreateDescriptorHeap SRV");
        srvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        alloc->Reset(); cmd->Reset(alloc.Get(), nullptr);
        t0 = std::chrono::steady_clock::now();
        std::string cacheDir = texturesDir + "\\.mipcache";
        for (CharModel& cm : models) {
            cm.gpu = uploadMesh(cm.data.verts.data(), cm.data.verts.size() * sizeof(model::SkinVertex), sizeof(model::SkinVertex), cm.data.indices);
            cm.lodData = decimateSkinned(cm.data, 18);   // far-LOD (skin-aware); only used when a char is tiny on screen
            if (cm.lodData.verts.size() >= 8 && cm.lodData.indices.size() >= 3) {
                cm.lodGpu = uploadMesh(cm.lodData.verts.data(), cm.lodData.verts.size() * sizeof(model::SkinVertex), sizeof(model::SkinVertex), cm.lodData.indices);
                cm.hasLod = true; cm.lodVerts = cm.lodData.verts.size();
                logLine("model " + cm.data.name + ": far-LOD " + std::to_string(cm.lodVerts) + " verts / " + std::to_string(cm.lodData.indices.size() / 3) + " tris (from " + std::to_string(cm.data.verts.size()) + " verts)");
            }
            cm.lodData2 = decimateSkinned(cm.data, 9);   // TIER-2 ultra-coarse: only ever drawn for sub-~15px specks
            if (cm.lodData2.verts.size() >= 6 && cm.lodData2.indices.size() >= 3) {
                cm.lodGpu2 = uploadMesh(cm.lodData2.verts.data(), cm.lodData2.verts.size() * sizeof(model::SkinVertex), sizeof(model::SkinVertex), cm.lodData2.indices);
                cm.hasLod2 = true; cm.lodVerts2 = cm.lodData2.verts.size();
                logLine("model " + cm.data.name + ": tier-2 LOD " + std::to_string(cm.lodVerts2) + " verts / " + std::to_string(cm.lodData2.indices.size() / 3) + " tris");
            }
            std::string texDir = texturesDir + "\\" + cm.data.name + "\\" + cm.data.name;
            ComPtr<ID3D12Resource> nrm = loadTex(texDir + "_normal.png", cacheDir), ao = loadTex(texDir + "_ao.png", cacheDir);
            const int variants = (&cm == &models[0]) ? 1 : 4;   // the lead wears her own look; crowd models get all four colorways
            for (int v = 0; v < variants; ++v) {
                TexSet ts; ts.normal = nrm; ts.ao = ao;
                ts.basecolor = loadTex(texDir + "_v" + std::to_string(v) + "_basecolor.png", cacheDir);
                ts.roughness = loadTex(texDir + "_v" + std::to_string(v) + "_roughness.png", cacheDir);
                ts.ok = ts.basecolor && ts.normal && ts.roughness && ts.ao;
                if (ts.ok && !noFur) {
                    ts.furflow = loadTex(texDir + "_v" + std::to_string(v) + "_furflow.png", cacheDir);
                    ts.sparkle = loadTex(texDir + "_v" + std::to_string(v) + "_sparkle.png", cacheDir);
                    ts.fur = ts.furflow && ts.sparkle;
                    if (!ts.fur) logLine("fur OFF for " + cm.data.name + " v" + std::to_string(v) + ": furflow/sparkle map missing");
                }
                if (ts.ok) {
                    ts.table = srvCount; srvCount += kTexPerSet;
                    D3D12_CPU_DESCRIPTOR_HANDLE dh = srvHeap->GetCPUDescriptorHandleForHeapStart(); dh.ptr += (SIZE_T)ts.table * srvSize;
                    ID3D12Resource* texs[kTexPerSet] = { ts.basecolor.Get(), ts.normal.Get(), ts.roughness.Get(), ts.ao.Get(),
                                                         ts.fur ? ts.furflow.Get() : ts.ao.Get(), ts.fur ? ts.sparkle.Get() : ts.ao.Get() };   // the two fur slots stay valid (ao) when fur is off
                    for (int i = 0; i < kTexPerSet; ++i) {
                        const auto format = texs[i]->GetDesc().Format;
                        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
                        sv.Format = format == DXGI_FORMAT_BC5_TYPELESS ? DXGI_FORMAT_BC5_UNORM
                                  : format == DXGI_FORMAT_BC7_TYPELESS ? (i == 0 ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC7_UNORM)
                                  : (i == 0 ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM);
                        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                        sv.Texture2D.MipLevels = texs[i]->GetDesc().MipLevels;
                        device->CreateShaderResourceView(texs[i], &sv, dh); dh.ptr += srvSize;
                    }
                }
                cm.variants.push_back(ts);
            }
        }
        cmd->Close(); ID3D12CommandList* l[] = { cmd.Get() }; queue->ExecuteCommandLists(1, l); flush();
        uploadKeepAlive.clear();
        textureLoadMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        logLine("textures: " + std::to_string(texturesLoaded) + " loaded (" + std::to_string(texturesFromCache) + " from mip cache), "
            + std::to_string(textureBytes / (1024 * 1024)) + " MB at <=" + std::to_string(texRes) + ", " + std::to_string((int)textureLoadMs) + " ms; models " + std::to_string((int)modelLoadMs) + " ms");
    }

    ComPtr<ID3D12Resource> loadTex(const std::string& png, const std::string& cacheDir) {
        model::TexImage img; std::string err;
        if (!model::loadTextureRGBA(png, texRes, cacheDir, img, err)) { logLine("texture " + png + ": " + err); return nullptr; }
        std::vector<ComPtr<ID3D12Resource>> textureUploads;
        ComPtr<ID3D12Resource> r = model::uploadTexture(device.Get(), cmd.Get(), img, textureUploads, err);
        for (auto& upload : textureUploads) uploadKeepAlive.push_back({std::move(upload), nullptr});
        if (!r) { logLine("texture " + png + ": " + err); return nullptr; }
        ++texturesLoaded; if (img.fromCache) ++texturesFromCache; textureBytes += img.bytes();
        return r;
    }

    struct CharDraw { const net::LookState* look = nullptr; int variant = 0; bool local = false; };
    // UNIFIED instancing: collect one InstData per shown submesh into (model,variant,submesh) buckets.
    // No immediate draws here — flushInstances() issues the batched draws. Fur SHELL extrusion deferred to v2
    // (velvet shading still applies via per-instance fur0). Motion-vector history is per-character (key), batching-safe.
    // PHASE A (serial): record a DrawItem — assign palette slot, compute transforms (+ mvec history). No pose, no emit.
    bool gatherCharacter(CharModel& cm, const AnimState& an, const XMMATRIX& body, const CharDraw& cd, uint64_t key) {
        if (cullOn) {   // frustum cull: characters the camera can't see cost nothing (no pose, no emit, no draw)
            XMFLOAT3 bp; XMStoreFloat3(&bp, body.r[3]);
            if (!sphereVisible(bp.x, bp.y + 0.9f, bp.z, 1.35f)) { ++culled; return true; }
        }
        if (skinCursor >= kMaxSkinned) return false;
        DrawItem it;
        it.cm = &cm; it.pslot = skinCursor++;
        it.clip = an.clip; it.t = an.t; it.prevClip = an.prevClip; it.prevT = an.prevT; it.blend = an.blend;
        it.variant = (int)std::max(0, std::min((int)cm.variants.size() - 1, cd.variant));
        it.look = cd.look;
        const XMMATRIX model = XMMatrixMultiply(XMMatrixScaling(cm.scale, cm.scale, cm.scale), body);
        XMStoreFloat4x4(&it.bodyF, body);
        XMStoreFloat4x4(&it.base.mvp, XMMatrixMultiply(model, viewProj));
        XMStoreFloat4x4(&it.base.model, model);
        XMStoreFloat4x4(&it.base.mvpNoJit, XMMatrixMultiply(model, viewProjNoJit));
        XMFLOAT4X4 mf; XMStoreFloat4x4(&mf, model);
        const uint64_t kh = key;
        { auto pit = prevModelByKey.find(kh);
          if (!resetHistory && pit != prevModelByKey.end()) XMStoreFloat4x4(&it.base.prevMvp, XMMatrixMultiply(XMLoadFloat4x4(&pit->second), prevViewProjNoJit));
          else it.base.prevMvp = it.base.mvpNoJit; }
        curModelByKey[kh] = mf;
        it.base.mvScale = XMFLOAT4(0.5f * (float)rw, -0.5f * (float)rh, 0, 0);
        if (lodOn && cm.hasLod) {   // far-LOD: only when the char is TINY on screen (imperceptible). ~char height in pixels.
            XMFLOAT3 bp; XMStoreFloat3(&bp, body.r[3]);
            float dx = bp.x - camWorld.x, dy = bp.y - camWorld.y, dz = bp.z - camWorld.z; float d = sqrtf(dx*dx + dy*dy + dz*dz);
            float projPx = 1.9f * ((float)rh / 0.9326f) / (d > 0.5f ? d : 0.5f);   // fovY 50deg -> 2*tan(25)=0.9326
            it.lod = (projPx < 65.0f);
        }
        if (static_cast<const net::LookState*>(it.look) && static_cast<const net::LookState*>(it.look)->present) it.joints.resize(kMaxJoints);
        items.push_back(std::move(it));
        ++skinnedDrawn;
        return true;
    }
    // PHASE C (serial): emit one InstData per shown submesh into (model,variant,submesh) buckets. Pose already in palette[pslot].
    void emitItemTo(const DrawItem& di, std::vector<std::vector<InstData>>& bkt) {
        CharModel& cm = *di.cm;
        const model::SkinnedModel& d = cm.data;
        const CharDraw cd{ static_cast<const net::LookState*>(di.look), di.variant, false };
        const UINT variant = (UINT)di.variant;
        const TexSet& ts = cm.variants[variant];
        const UINT cmIdx = (UINT)(&cm - models.data());
        const SceneCB& base = di.base;
        const UINT pslot = di.pslot;
        if (di.lod && cm.hasLod) {   // FAR-LOD: one instance of the decimated mesh (skinned, textured basecolor, no fur/gear — tiny on screen)
            InstData id{}; id.s = base;
            id.s.baseColor = XMFLOAT4(1, 1, 1, 0.0f);
            id.s.params = XMFLOAT4(0.7f, ts.ok ? 1.0f : 0.0f, 0.0f, 0.0f);
            id.paletteBase = pslot * kMaxJoints;
            // tier-2 (ultra-coarse) for sub-~15px specks -> reserved slot 30; tier-1 -> reserved slot 31. One draw per (model,variant,tier).
            const UINT lodSub = (di.lod2 && cm.hasLod2) ? (kBktMaxSubs - 2) : (kBktMaxSubs - 1);
            bkt[bktIndex(cmIdx, variant, lodSub)].push_back(id);
            return;
        }
        const FurProfile& fur = (cd.look && cd.look->present && cd.look->sex == "m") ? kFurMale : kFurFemale;
        UINT subIdx = 0;
        for (const model::Submesh& sm : d.subs) {
            const UINT thisSub = subIdx++;
            XMFLOAT3 tint(1, 1, 1); float emissive = sm.emissive ? 1.0f : 0.0f; float rough = 0.7f, metal = 0.0f;
            if (!sm.gearSlot.empty()) {
                bool show = true; std::string color;
                if (cd.look && cd.look->present) {
                    show = false;
                    for (const auto& gs : cd.look->gear) if (gs.slot == sm.gearSlot) { show = gs.show; color = gs.color; break; }
                }
                if (!show) continue;
                tint = ColorwayRGB(color, XMFLOAT3(1, 1, 1));
                if (sm.material == "gear_metal") { metal = 0.9f; rough = 0.35f; }
            } else if (sm.node == "tattoo") {
                if (cd.look && cd.look->present) { if (!cd.look->tattoo.on) continue; tint = ColorwayRGB(cd.look->tattoo.color, XMFLOAT3(1.0f, 0.10f, 0.80f)); }
                emissive = 0.85f + 0.15f * sinf((float)(lastTick % 100000) * 0.004f);
            }
            if (!ts.ok) tint = XMFLOAT3(tint.x * sm.baseColor.x, tint.y * sm.baseColor.y, tint.z * sm.baseColor.z);
            const bool furSkin = ts.ok && ts.fur && !noFur && (sm.material == "fur" || sm.material == "furshell");
            InstData id{}; id.s = base;
            id.s.baseColor = XMFLOAT4(tint.x, tint.y, tint.z, emissive);
            id.s.params = XMFLOAT4(rough, ts.ok ? 1.0f : 0.0f, metal, 0.0f);
            if (furSkin) id.s.fur0 = XMFLOAT4(fur.length, fur.softness, fur.stiffness, fur.sheen);
            id.paletteBase = pslot * kMaxJoints;
            bkt[bktIndex(cmIdx, variant, thisSub)].push_back(id);
        }
    }

    float floorY(float x, float z) const { return (x * x + z * z < kStageR * kStageR) ? kStageH : 0.0f; }

    // ---- input: poll keys (only while our window is foreground), send intents at the server's
    // 30 Hz in exactly server.mjs's `input` shape. Movement is CAMERA-RELATIVE (W = away from the
    // camera). yaw sent = the movement heading (so the server aims/faces where you walk), else the
    // last heading. Edge-triggered: SPACE jump (one input carries jump:true), Q dodge, F fire,
    // C hold = chargeStart / release = chargeRelease. Arrows orbit the camera; it auto-orbits when idle.
    void pollInput(HWND hwnd, uint64_t now, float dt, const net::Player* me) {
        auto key = [](int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; };
        bool focus = (GetForegroundWindow() == hwnd);
        float f = 0, s = 0; bool sprint = false, jump = false, dodge = false, fire = false, charge = false;
        if (focus) {
            f = (key('W') ? 1.0f : 0.0f) - (key('S') ? 1.0f : 0.0f);
            s = (key('D') ? 1.0f : 0.0f) - (key('A') ? 1.0f : 0.0f);
            sprint = key(VK_SHIFT); jump = key(VK_SPACE); dodge = key('Q'); fire = key('F'); charge = key('C');
            if (key(VK_LEFT))  { in.camYaw -= dt * 1.6f; in.lastActive = now; }
            if (key(VK_RIGHT)) { in.camYaw += dt * 1.6f; in.lastActive = now; }
        }
        // camera basis on the floor plane (LH: right = up x forward)
        float fx = -sinf(in.camYaw), fz = -cosf(in.camYaw);
        float rx = fz, rz = -fx;
        float dx = fx * f + rx * s, dz = fz * f + rz * s;
        // --auto: walk toward the bar until close enough to trigger the encounter (server: <3.6m)
        if (in.autoWalk && me) {
            float ax = barX - me->x, az = barZ - me->z; float L = sqrtf(ax * ax + az * az);
            if (L > 2.8f) { dx = ax / L; dz = az / L; } else { dx = dz = 0; }
        }
        float len = sqrtf(dx * dx + dz * dz);
        if (len > 1.0f) { dx /= len; dz /= len; }
        if (len > 0.01f) { in.yaw = atan2f(dx, dz); in.lastActive = now; }
        if (f != 0 || s != 0 || jump || dodge || fire || charge) in.lastActive = now;
        // idle auto-orbit keeps the room moving (and the spotlights sweeping) with no hands on it
        if (now - in.lastActive > 4000) in.camYaw += dt * (XM_2PI / 32.0f);

        if (!ws || !ws->isOpen()) return;
        bool jumpEdge = jump && !in.prevJump, dodgeEdge = dodge && !in.prevDodge, fireEdge = fire && !in.prevFire;
        bool chargeDown = charge && !in.prevCharge, chargeUp = !charge && in.prevCharge;
        in.prevJump = jump; in.prevDodge = dodge; in.prevFire = fire; in.prevCharge = charge;
        if (dodgeEdge)  ws->sendText("{\"t\":\"dodge\",\"dx\":" + std::to_string(dx) + ",\"dz\":" + std::to_string(dz) + "}");
        if (fireEdge)   ws->sendText("{\"t\":\"fire\"}");
        if (chargeDown) ws->sendText("{\"t\":\"chargeStart\"}");
        if (chargeUp)   ws->sendText("{\"t\":\"chargeRelease\"}");
        // 30 Hz cadence on a high-resolution clock (GetTickCount64 is ~15.6ms-grained, which
        // under-sends); dt = the real elapsed time this input covers, capped like the server does.
        auto tnow = std::chrono::steady_clock::now();
        double sinceSend = std::chrono::duration<double>(tnow - in.lastSendTp).count();
        if (sinceSend >= (1.0 / 30.0) - 0.002 || jumpEdge) {
            float sendDt = (in.sent == 0) ? (1.0f / 30.0f) : std::min(0.1f, (float)sinceSend);
            in.lastSendTp = tnow;
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "{\"t\":\"input\",\"seq\":%u,\"dx\":%.3f,\"dz\":%.3f,\"jump\":%s,\"sprint\":%s,\"yaw\":%.3f,\"pitch\":0,\"dt\":%.4f}",
                ++in.seq, dx, dz, jumpEdge ? "true" : "false", sprint ? "true" : "false", in.yaw, sendDt);
            ws->sendText(buf);
            if ((++in.sent % 150) == 0) logLine("input sent x" + std::to_string(in.sent) + " (seq " + std::to_string(in.seq) + ")");
        }
    }

    // ---- --shot: copy the finished backbuffer into a READBACK buffer and write a 24-bit BMP ----
    void render(HWND hwnd) {
        // Pace at the frame TOP on the swapchain's frame-latency waitable: blocks only until a backbuffer is
        // free, letting the PREVIOUS frame's present() overlap with this frame's CPU record (true CPU||GPU pipelining).
        if (frameLatencyWaitable) WaitForSingleObject(frameLatencyWaitable, 1000);
        paceFrame();
        // Snapshot cache: only pay the (100k-crowd) copy when the network/mock thread has actually published a new
        // snapshot (seq bump). Rendering runs far faster than the ~20-30Hz snapshot rate, so most frames reuse the cache.
        if (state) { uint64_t s = state->seq(); if (s != cachedSeq_) { cachedSnap_ = state->getLatest(); cachedSeq_ = s; } }
        net::WorldState& snap = cachedSnap_;
        net::Welcome wc = state ? state->getWelcome() : net::Welcome{};
        if (wc.valid) {
            if (wc.worldHalf > 1.0f) roomHalf = wc.worldHalf;
            if (wc.barValid) { barX = wc.barX; barZ = wc.barZ; }
        }

        uint64_t now = GetTickCount64();
        float dt = lastTick ? (now - lastTick) / 1000.0f : 0.016f;
        lastTick = now;
        dt = std::min(dt, 0.1f);
        float a = std::min(1.0f, dt * 8.0f);   // smoothing rate

        // ---- resolve local player (server-flagged id, else first) ----
        const net::Player* me = nullptr;
        if (!snap.players.empty()) {
            for (const auto& p : snap.players) if (p.id == myId) { me = &p; break; }
            if (!me) me = &snap.players.front();
        }
        lastPlayers = (int)snap.players.size(); lastCrowd = (int)snap.crowd.size();
        if (snap.valid && snap.encounter != lastPhase) {
            lastPhase = snap.encounter;
            logLine(std::string("phase -> ") + net::encounterName(snap.encounter) + " (darkness " + std::to_string(snap.darkness) + ")");
        }

        // ---- input (sends intents; movement stays server-authoritative) ----
        antilag.update();
        pollInput(hwnd, now, dt, me);

        // ---- camera: follow the local player; yaw from input (auto-orbits when idle) ----
        float camPx = 0, camPz = 0, camPy = 0;
        if (me) { SmoothXf& s = track(eid('m', me->id), me->x, me->z, me->y, me->yaw, a); camPx = s.x; camPz = s.z; camPy = s.y; }
        float orbit = in.camYaw;
        XMVECTOR ctr = XMVectorSet(camPx, 1.1f + camPy * 0.5f, camPz, 0);
        XMVECTOR eye = XMVectorAdd(ctr, XMVectorSet(sinf(orbit) * camDist, camHigh, cosf(orbit) * camDist, 0));
        XMStoreFloat3(&camWorld, eye);
        // ---- M5: pending hotkeys (F2 quality / F3 FG / F4 DLSS-G multiplier) applied between frames ----
        if (pendingMode >= 0) { int m = pendingMode; pendingMode = -1; applyMode(m); logLine(std::string("mode -> ") + ups->modeName(ups->mode()) + " " + std::to_string(rw) + "x" + std::to_string(rh)); }
        if (pendingFG >= 0) { ups->setFG(pendingFG == 1); pendingFG = -1; logLine("fg -> " + ups->fgStatus()); }
        if (pendingMult) { ups->cycleMultiplier(); pendingMult = false; logLine("fg mult -> " + ups->fgStatus()); }
        if (curModel.size() != kMaxInstances) { curModel.resize(kMaxInstances); prevModel.resize(kMaxInstances); }
        ups->beginFrame(frameIndex);

        XMMATRIX view = XMMatrixLookAtLH(eye, ctr, XMVectorSet(0, 1, 0, 0));
        // Far plane: 400 for normal play (small camDist, best depth precision). When the camera is pulled
        // way back for a crowd stress view, extend it so the FAR side of the crowd isn't clipped to black.
        const float nearZ = 0.1f, fovY = XMConvertToRadians(50.0f);
        const float farZ = (camDist > 30.0f) ? (camDist * 4.0f + 800.0f) : 400.0f;
        XMMATRIX proj = XMMatrixPerspectiveFovLH(fovY, (float)w / (float)h, nearZ, farZ);
        viewProjNoJit = XMMatrixMultiply(view, proj);
        // Halton(2,3) sub-pixel jitter; phase count = ceil(8 * (out/render)^2) (the SDKs' rule).
        // Applied as an NDC offset scaled by clip.w = row 2 of a LH projection (w == view z).
        jitterX = jitterY = 0;
        if (ups->wantsJitter()) {
            uint32_t phases = (uint32_t)ceilf(8.0f * ((float)w / (float)rw) * ((float)w / (float)rw));
            uint32_t k = (frameIndex % phases) + 1;
            jitterX = halton(k, 2) - 0.5f; jitterY = halton(k, 3) - 0.5f;
        }
        XMMATRIX projJ = proj;
        projJ.r[2] = XMVectorAdd(projJ.r[2], XMVectorSet(2.0f * jitterX / (float)rw, -2.0f * jitterY / (float)rh, 0, 0));
        viewProj = XMMatrixMultiply(view, projJ);
        computeFrustum(viewProjNoJit);   // camera frustum for CPU-side character culling this frame

        // ---- atmosphere: mood per encounter phase, darkened by WorldState.darkness ----
        XMFLOAT3 mood;
        switch (snap.encounter) {
            case net::Encounter::Intro:   mood = XMFLOAT3(0.10f, 0.09f, 0.20f); break;
            case net::Encounter::Attack:  mood = XMFLOAT3(0.12f, 0.06f, 0.14f); break;
            case net::Encounter::Combat:  mood = XMFLOAT3(0.10f, 0.04f, 0.12f); break;
            case net::Encounter::Boss:    mood = XMFLOAT3(0.06f, 0.02f, 0.06f); break;
            case net::Encounter::Cleared: mood = XMFLOAT3(0.09f, 0.10f, 0.18f); break;
            default:                      mood = XMFLOAT3(0.043f, 0.031f, 0.090f); break;
        }
        float dk = std::min(1.0f, std::max(0.0f, snap.darkness));
        float dim = 1.0f - 0.65f * dk;
        const float clear[4] = { mood.x * dim, mood.y * dim, mood.z * dim, 1.0f };
        XMFLOAT4 fog = XMFLOAT4(clear[0], clear[1], clear[2], 0.35f + 0.5f * dk);

        // ---- M6: acquire this frame's slot. The CPU only waits when the slot's previous
        // submission (kFrameCount frames ago) has not retired — with --inflight 3 that is the
        // GPU-bound case, never the sync-every-frame flush of M1..M5.
        ups->renderSubmitStart();
        slot = frameIndex % kFrameCount;
        if (fence->GetCompletedValue() < slotFence[slot]) {
            auto w0 = std::chrono::steady_clock::now();
            waitFence(slotFence[slot]);
            stallMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - w0).count(); ++stallCount;
        }
        alloc = allocs[slot];
        alloc->Reset();
        cmd->Reset(alloc.Get(), pso.Get());
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
        }
        auto _recT0 = std::chrono::steady_clock::now();   // CPU record-timer start (single-thread draw submission cost)
        poseAccumMs = 0;                                  // per-frame evalPose accumulator (parallelizable portion)
        instEntryCursor = 0; drawCallCount = 0; culled = 0; lodDrawn = 0; curModelByKey.clear(); items.clear();   // instancing: reset per-frame collect state
        instCursor = 0; skinCursor = (UINT)(models.size() * kFarPoses); skinnedDrawn = 0; furShellsDrawn = 0;   // reserve [0..F) palette slots for the shared far-pose pool
        if (srvHeap) { ID3D12DescriptorHeap* heaps[] = { srvHeap.Get() }; cmd->SetDescriptorHeaps(1, heaps); }

        // scene pass -> render-size color + mvec + depth (resting state = shader resource)
        barrier(sceneColor.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        barrier(sceneMvec.Get(),  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        barrier(depth.Get(),      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2];
        rtvs[0] = rtvHeap->GetCPUDescriptorHandleForHeapStart(); rtvs[0].ptr += (SIZE_T)kFrameCount * rtvSize;
        rtvs[1] = rtvs[0]; rtvs[1].ptr += rtvSize;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = dsvHeap->GetCPUDescriptorHandleForHeapStart();
        cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        cmd->OMSetRenderTargets(2, rtvs, FALSE, &dsv);

        D3D12_VIEWPORT vp{ 0, 0, (float)rw, (float)rh, 0.0f, 1.0f };
        D3D12_RECT sr{ 0, 0, (LONG)rw, (LONG)rh };
        cmd->RSSetViewports(1, &vp);
        cmd->RSSetScissorRects(1, &sr);
        cmd->SetGraphicsRootSignature(rootSig.Get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        auto charModel = [&](float x, float y, float z, float yaw) {
            return XMMatrixMultiply(XMMatrixMultiply(charNorm, XMMatrixRotationY(yaw)), XMMatrixTranslation(x, y, z));
        };
        auto coneModel = [&](float x, float y, float z, float r, float hgt, float yaw) {
            return XMMatrixMultiply(XMMatrixMultiply(XMMatrixScaling(r, hgt, r), XMMatrixRotationY(yaw)), XMMatrixTranslation(x, y, z));
        };

        // ---- follow-spotlights: advance each rig's smoothed floor target toward the local player
        // (dancefloor center when nobody is in) with its own lag + a slow wobble so the three beams
        // visibly sweep and cross as she moves. Computed BEFORE entities so their pools can light them.
        float tgtX = me ? camPx : 0.0f, tgtZ = me ? camPz : 0.0f;
        XMFLOAT3 spotHit[3], spotTop[3];
        for (int i = 0; i < 3; ++i) {
            SpotRig& r = spots[i];
            float ph = (float)(now % 100000) / 1000.0f * r.wobble + (float)i * 2.1f;
            float wx = tgtX + sinf(ph) * 0.9f, wz = tgtZ + cosf(ph * 0.8f) * 0.9f;
            float axw = tgtX + r.offset.x, azw = tgtZ + r.offset.z;     // rig rides the ceiling above her
            if (!r.init) { r.tx = wx; r.tz = wz; r.ax = axw; r.az = azw; r.init = true; }
            float k = std::min(1.0f, dt * r.lag), ka = std::min(1.0f, dt * r.lag * 0.45f);
            r.tx += (wx - r.tx) * k; r.tz += (wz - r.tz) * k;
            r.ax += (axw - r.ax) * ka; r.az += (azw - r.az) * ka;       // slower -> beams tilt while sweeping
            float clampA = roomHalf - 1.0f;
            r.ax = std::max(-clampA, std::min(clampA, r.ax)); r.az = std::max(-clampA, std::min(clampA, r.az));
            spotHit[i] = XMFLOAT3(r.tx, floorY(r.tx, r.tz) + 0.02f, r.tz);
            spotTop[i] = XMFLOAT3(r.ax, kCeilY, r.az);
        }
        float spotDim = 1.0f - 0.45f * dk;

        // ---- M6: per-frame lights (b1). Key light + hemisphere ambient dim with `darkness`; the
        // three follow-spots are cone lights aimed from the rig at the tracked floor point.
        {
            FrameCB fc{};
            fc.lightDir = XMFLOAT4(-0.4f, -0.85f, -0.35f, 3.0f * dim);   // linear-space intensities (albedo/pi BRDF)
            fc.ambient  = XMFLOAT4(0.46f * dim, 0.42f * dim, 0.62f * dim, 0.45f);
            if (flatLight) { fc.lightDir = XMFLOAT4(-0.3f, -0.7f, -0.65f, 3.5f); fc.ambient = XMFLOAT4(0.55f, 0.55f, 0.55f, 0.6f); }   // --flatlight: colour QA
            fc.camPos   = XMFLOAT4(camWorld.x, camWorld.y, camWorld.z, 0.0f);
            fc.fog      = fog;
            for (int i = 0; i < 3; ++i) {
                const SpotRig& r = spots[i];
                XMVECTOR d = XMVectorSubtract(XMLoadFloat3(&spotHit[i]), XMLoadFloat3(&spotTop[i]));
                float len = XMVectorGetX(XMVector3Length(d));
                XMFLOAT3 dn; XMStoreFloat3(&dn, XMVector3Normalize(d));
                float cosOuter = len / sqrtf(len * len + r.radius * r.radius);                       // pool radius at the floor
                float cosInner = len / sqrtf(len * len + r.radius * r.radius * 0.36f);
                fc.spots[i].pos   = XMFLOAT4(spotTop[i].x, spotTop[i].y, spotTop[i].z, len * 1.6f);
                fc.spots[i].dir   = XMFLOAT4(dn.x, dn.y, dn.z, cosOuter);
                fc.spots[i].color = XMFLOAT4(r.color.x * spotDim * 22.0f, r.color.y * spotDim * 22.0f, r.color.z * spotDim * 22.0f, cosInner);
                if (flatLight) fc.spots[i].color = XMFLOAT4(0, 0, 0, cosInner);
            }
            writeFrameCB(fc);
        }
        if (instCursor < kMaxInstances) {   // background fill (see psoBg; reads Frame.fog)
            UINT bgInst = instCursor++;
            writeInstance(bgInst, XMMatrixIdentity(), XMFLOAT3(clear[0], clear[1], clear[2]), 0.0f, 1.0f, 0.0f, false);
            cmd->SetGraphicsRootConstantBufferView(0, cbSlotGpu() + (UINT64)bgInst * kCbStride);
            cmd->SetPipelineState(psoBg.Get());
            cmd->DrawInstanced(3, 1, 0, 0);
            cmd->SetPipelineState(pso.Get());
        }

        // ---- THE ROOM (greybox, lit + fogged like everything else). Arena = server half-size.
        // Neon strips are emissive and dim with `darkness` ("the lights die" when the ShadowEnemy come).
        // Skipped entirely under --stress: the closed box would occlude the pulled-back crowd view.
        if (!hideRoom) {
            const float H = roomHalf, T = 0.5f, WH = kCeilY;
            float neon = 1.0f - 0.7f * dk;
            XMFLOAT3 magenta(1.0f * neon, 0.10f * neon, 0.80f * neon), blue(0.25f * neon, 0.45f * neon, 1.0f * neon), white(0.95f * neon, 0.95f * neon, 1.0f * neon);
            XMFLOAT3 floorC(0.10f, 0.09f, 0.14f), wallC(0.13f, 0.11f, 0.18f), darkC(0.07f, 0.06f, 0.10f);
            drawMesh(boxMesh, BoxAt(0, -0.05f, 0, 2 * H, 0.1f, 2 * H), floorC, 0.0f);                     // floor
            drawMesh(boxMesh, BoxAt(0, WH + 0.15f, 0, 2 * H + 2 * T, 0.3f, 2 * H + 2 * T), darkC, 0.0f);   // ceiling
            drawMesh(boxMesh, BoxAt( H + T * 0.5f, WH * 0.5f, 0, T, WH, 2 * H + 2 * T), wallC, 0.0f);        // walls
            drawMesh(boxMesh, BoxAt(-H - T * 0.5f, WH * 0.5f, 0, T, WH, 2 * H + 2 * T), wallC, 0.0f);
            drawMesh(boxMesh, BoxAt(0, WH * 0.5f,  H + T * 0.5f, 2 * H, WH, T), wallC, 0.0f);
            drawMesh(boxMesh, BoxAt(0, WH * 0.5f, -H - T * 0.5f, 2 * H, WH, T), wallC, 0.0f);
            // neon strips: magenta on the ±z walls, blue on the ±x walls (y=3), white on all four (y=6)
            drawMesh(boxMesh, BoxAt(0, 3.0f,  H - 0.08f, 2 * H, 0.12f, 0.12f), magenta, 1.0f);
            drawMesh(boxMesh, BoxAt(0, 3.0f, -H + 0.08f, 2 * H, 0.12f, 0.12f), magenta, 1.0f);
            drawMesh(boxMesh, BoxAt( H - 0.08f, 3.0f, 0, 0.12f, 0.12f, 2 * H), blue, 1.0f);
            drawMesh(boxMesh, BoxAt(-H + 0.08f, 3.0f, 0, 0.12f, 0.12f, 2 * H), blue, 1.0f);
            drawMesh(boxMesh, BoxAt(0, 6.0f,  H - 0.06f, 2 * H, 0.08f, 0.08f), white, 1.0f);
            drawMesh(boxMesh, BoxAt(0, 6.0f, -H + 0.06f, 2 * H, 0.08f, 0.08f), white, 1.0f);
            drawMesh(boxMesh, BoxAt( H - 0.06f, 6.0f, 0, 0.08f, 0.08f, 2 * H), white, 1.0f);
            drawMesh(boxMesh, BoxAt(-H + 0.06f, 6.0f, 0, 0.08f, 0.08f, 2 * H), white, 1.0f);
            // four pillars with a vertical neon edge (magenta / blue alternating)
            for (int i = 0; i < 4; ++i) {
                float px = (i & 1) ? 18.0f : -18.0f, pz = (i & 2) ? 18.0f : -18.0f;
                drawMesh(boxMesh, BoxAt(px, WH * 0.5f, pz, 0.9f, WH, 0.9f), wallC, 0.0f);
                drawMesh(boxMesh, BoxAt(px - (px > 0 ? 0.5f : -0.5f), WH * 0.5f, pz, 0.1f, WH - 0.4f, 0.1f), (i & 1) ? blue : magenta, 1.0f);
            }
            // the bar (welcome.bar): counter block, white neon lip, back-bar wall with a white strip
            drawMesh(boxMesh, BoxAt(barX, 0.55f, barZ - 0.2f, 9.0f, 1.1f, 1.1f), XMFLOAT3(0.16f, 0.12f, 0.20f), 0.0f);
            drawMesh(boxMesh, BoxAt(barX, 1.12f, barZ + 0.32f, 9.0f, 0.05f, 0.08f), white, 1.0f);
            drawMesh(boxMesh, BoxAt(barX, 1.6f, barZ - 3.5f, 12.0f, 3.2f, 0.4f), wallC, 0.0f);
            drawMesh(boxMesh, BoxAt(barX, 3.25f, barZ - 3.25f, 12.0f, 0.1f, 0.1f), white, 1.0f);
            drawMesh(boxMesh, BoxAt(barX, 2.2f, barZ - 3.25f, 11.0f, 0.06f, 0.06f), magenta, 1.0f);
            // raised dancefloor slab (roomAmbiance DANCEFLOOR: center, r=9) with a thin blue neon edge
            drawMesh(cylMesh, XMMatrixMultiply(XMMatrixScaling(kStageR, kStageH, kStageR), XMMatrixTranslation(0, 0, 0)), XMFLOAT3(0.12f, 0.10f, 0.20f), 0.0f);
            drawMesh(ringThinMesh, XMMatrixMultiply(XMMatrixScaling(kStageR + 0.05f, 1, kStageR + 0.05f), XMMatrixTranslation(0, kStageH + 0.01f, 0)), blue, 1.0f);
            drawMesh(ringThinMesh, XMMatrixMultiply(XMMatrixScaling(4.5f, 1, 4.5f), XMMatrixTranslation(0, kStageH + 0.01f, 0)), magenta, 1.0f);
            // spotlight rigs on the ceiling
            for (int i = 0; i < 3; ++i) drawMesh(boxMesh, BoxAt(spotTop[i].x, spotTop[i].y - 0.2f, spotTop[i].z, 0.6f, 0.4f, 0.6f), darkC, 0.0f);
        }

        // ---- gear + tattoo extras (CONTRACT-3D §3 v1.2). With a skinned model the chest/pants/hip
        // pieces and the tattoo are submeshes of the character itself (drawCharacter); slots the
        // model has no mesh for (head/shoulders/gloves/feet/weapon) attach placeholder boxes to the
        // posed joints. Without models: the M4 body offsets on the 1.8 m forge mesh (+Z = front).
        auto drawLook = [&](const net::LookState& look, const XMMATRIX& body, const CharModel* cm, const XMFLOAT4X4* joints) {
            auto attach = [&](const GpuMesh& mesh, const XMMATRIX& local, const XMFLOAT3& col, float em) {
                drawMesh(mesh, XMMatrixMultiply(local, body), col, em, 0.45f, 0.35f);
            };
            auto atJoint = [&](const GpuMesh& mesh, int j, const XMMATRIX& local, const XMFLOAT3& col, float em) {
                if (j < 0 || !joints) return;
                XMMATRIX m = XMMatrixMultiply(XMMatrixMultiply(local, XMLoadFloat4x4(&joints[j])), XMMatrixScaling(cm->scale, cm->scale, cm->scale));
                drawMesh(mesh, XMMatrixMultiply(m, body), col, em, 0.45f, 0.35f);
            };
            const XMFLOAT3 neutral(0.72f, 0.72f, 0.78f);
            for (const auto& g : look.gear) {
                if (!g.show) continue;
                XMFLOAT3 col = ColorwayRGB(g.color, neutral);
                if (cm) {
                    bool inMesh = false;
                    for (const auto& sm : cm->data.subs) if (sm.gearSlot == g.slot) { inMesh = true; break; }
                    if (inMesh) continue;
                    if      (g.slot == "head")      atJoint(boxMesh, cm->jHead, BoxAt(0, 0.10f, 0, 0.30f, 0.16f, 0.30f), col, 0.1f);
                    else if (g.slot == "shoulders") { atJoint(boxMesh, cm->jShoulderL, BoxAt(0, 0.04f, 0, 0.20f, 0.12f, 0.22f), col, 0.1f);
                                                      atJoint(boxMesh, cm->jShoulderR, BoxAt(0, 0.04f, 0, 0.20f, 0.12f, 0.22f), col, 0.1f); }
                    else if (g.slot == "gloves")    { atJoint(boxMesh, cm->jHandL, BoxAt(0, 0, 0, 0.14f, 0.14f, 0.14f), col, 0.1f);
                                                      atJoint(boxMesh, cm->jHandR, BoxAt(0, 0, 0, 0.14f, 0.14f, 0.14f), col, 0.1f); }
                    else if (g.slot == "feet")      { atJoint(boxMesh, cm->jFootL, BoxAt(0, -0.03f, 0.05f, 0.14f, 0.10f, 0.28f), col, 0.1f);
                                                      atJoint(boxMesh, cm->jFootR, BoxAt(0, -0.03f, 0.05f, 0.14f, 0.10f, 0.28f), col, 0.1f); }
                    else if (g.slot == "weapon")    atJoint(coneMesh, cm->jHandR, XMMatrixMultiply(XMMatrixScaling(0.14f, 0.95f, 0.14f), XMMatrixTranslation(0.06f, -0.40f, 0.10f)), col, 0.35f);
                    else                            attach(boxMesh, BoxAt(0, 1.25f, -0.26f, 0.22f, 0.22f, 0.12f), col, 0.1f);   // unknown slot: on the back
                    continue;
                }
                if      (g.slot == "head")      attach(boxMesh, BoxAt(0, 1.70f, 0, 0.34f, 0.26f, 0.34f), col, 0.1f);
                else if (g.slot == "shoulders") { attach(boxMesh, BoxAt( 0.30f, 1.46f, 0, 0.22f, 0.14f, 0.24f), col, 0.1f);
                                                  attach(boxMesh, BoxAt(-0.30f, 1.46f, 0, 0.22f, 0.14f, 0.24f), col, 0.1f); }
                else if (g.slot == "chest")     attach(boxMesh, BoxAt(0, 1.22f, 0, 0.50f, 0.42f, 0.34f), col, 0.1f);
                else if (g.slot == "gloves")    { attach(boxMesh, BoxAt( 0.37f, 0.86f, 0.06f, 0.16f, 0.16f, 0.16f), col, 0.1f);
                                                  attach(boxMesh, BoxAt(-0.37f, 0.86f, 0.06f, 0.16f, 0.16f, 0.16f), col, 0.1f); }
                else if (g.slot == "pants")     attach(boxMesh, BoxAt(0, 0.70f, 0, 0.44f, 0.52f, 0.30f), col, 0.1f);
                else if (g.slot == "hip")       attach(boxMesh, BoxAt(0.31f, 0.92f, 0.02f, 0.16f, 0.14f, 0.12f), col, 0.1f);
                else if (g.slot == "feet")      { attach(boxMesh, BoxAt( 0.13f, 0.07f, 0.05f, 0.16f, 0.14f, 0.32f), col, 0.1f);
                                                  attach(boxMesh, BoxAt(-0.13f, 0.07f, 0.05f, 0.16f, 0.14f, 0.32f), col, 0.1f); }
                else if (g.slot == "weapon")    attach(coneMesh, XMMatrixMultiply(XMMatrixScaling(0.16f, 1.05f, 0.16f), XMMatrixTranslation(0.42f, 0.45f, 0.14f)), col, 0.35f);
                else                            attach(boxMesh, BoxAt(0, 1.25f, -0.26f, 0.22f, 0.22f, 0.12f), col, 0.1f);   // unknown slot: on the back
            }
            if (!cm && look.tattoo.present && look.tattoo.on) {
                XMFLOAT3 tc = ColorwayRGB(look.tattoo.color, XMFLOAT3(1.0f, 0.10f, 0.80f));
                float pulse = 0.85f + 0.15f * sinf((float)(now % 100000) * 0.004f);
                XMFLOAT3 glowC(tc.x * pulse, tc.y * pulse, tc.z * pulse);
                const GpuMesh& tm = (look.tattoo.design == 0) ? heartMesh : discMesh;
                // flat mesh (normal +Y, lobes +Z) rotated to stand on the front of the lower stomach
                XMMATRIX local = XMMatrixMultiply(XMMatrixMultiply(XMMatrixScaling(0.11f, 1, 0.11f), XMMatrixRotationX(-XM_PIDIV2)), XMMatrixTranslation(0, 0.98f, 0.17f));
                attach(tm, local, glowC, 1.0f);
            }
        };

        // ---- M6: model + clip choice. Players: their species' model when loaded, else the lead.
        // Crowd/NPCs rotate the non-lead models by id hash (variant = hash % 4). Clip from the wire
        // anim enum (server ANIM: idle walk run jump dodge charge fire; crowd: idle walk dance).
        auto hashId = [](const std::string& id) { uint32_t hsh = 2166136261u; for (unsigned char ch : id) { hsh ^= ch; hsh *= 16777619u; } return hsh; };
        auto pickModel = [&](const std::string& species, uint32_t hsh, bool player) -> CharModel* {
            if (models.empty()) return nullptr;
            if (player) { auto it = modelByName.find(species); return (it != modelByName.end() && it->second > 0) ? &models[(size_t)it->second] : &models[0]; }
            if (models.size() == 1) return &models[0];
            return &models[1 + hsh % (models.size() - 1)];
        };
        auto clipFor = [&](const CharModel& cm, int anim, bool crowd) {
            int c = cm.clipIdle;
            if (crowd) { if (anim == 1) c = cm.clipWalk; else if (anim == 2) c = cm.clipSway; }
            else switch (anim) {
                case 1: c = cm.clipWalk; break; case 2: c = cm.clipRun; break; case 3: c = cm.clipJump; break;
                case 4: c = cm.clipDodge; break; case 5: c = cm.clipCharge; break; case 6: c = cm.clipFire; break; default: break;
            }
            return c >= 0 ? c : (cm.clipIdle >= 0 ? cm.clipIdle : 0);
        };
        static std::vector<XMFLOAT4X4> jointsTmp; jointsTmp.resize(kMaxJoints);
        // one character: skinned when a model is loaded, else the M5 primitive + old placeholders
        auto drawEntity = [&](uint64_t key, const net::LookState* look, const std::string& species, int anim, bool crowd, bool local,
                              float x, float y, float z, float yaw, const XMFLOAT3& fallbackCol, float fallbackEm) {
            CharModel* cm = pickModel(species, (uint32_t)key, !crowd);
            if (cm && !cm->data.joints.empty()) {
                AnimState& an = anims[key]; an.last = now;
                int clip = clipFor(*cm, anim, crowd);
                advanceAnim(an, clip, dt, 1.0f);
                float bob = (crowd && anim == 2) ? 0.06f * fabsf(sinf(an.t * 6.0f)) : 0.0f;   // dance: procedural bob on the sway clip
                XMMATRIX body = XMMatrixMultiply(XMMatrixRotationY(yaw), XMMatrixTranslation(x, y + bob, z));
                CharDraw cd; cd.look = look; cd.variant = (int)((key * 2654435761ull >> 13) % 4u); cd.local = local;
                if (gatherCharacter(*cm, an, body, cd, key)) return;   // record item; pose (parallel) + emit + drawLook happen after the walk
            }
            drawMesh(charMesh, charModel(x, y, z, yaw), fallbackCol, fallbackEm, 0.6f, 0.0f);
            if (look && look->present) drawLook(*look, XMMatrixMultiply(XMMatrixRotationY(yaw), XMMatrixTranslation(x, y, z)), nullptr, nullptr);
        };

        // players (local player = the lead model; fallback tint white / cool blue) + their gear/tattoo
        auto _gatherT0 = std::chrono::steady_clock::now();
        for (const auto& p : snap.players) {
            const uint64_t k = eid('p', p.id);
            SmoothXf& s = track(k, p.x, p.z, p.y, p.yaw, a);
            bool local = (me && p.id == me->id);
            XMFLOAT3 col = local ? XMFLOAT3(0.95f, 0.97f, 1.0f) : XMFLOAT3(0.45f, 0.62f, 1.0f);
            float y = s.y + floorY(s.x, s.z);
            drawEntity(k, p.look.present ? &p.look : nullptr, local ? std::string("fox-lead") : p.look.species, p.anim, false, local, s.x, y, s.z, s.yaw, col, 0.05f);
        }
        // crowd: DATA-ORIENTED PARALLEL gather — per-index SoA state arrays (no shared maps => lock-free), spread across all cores
        {
            const int N = (int)snap.crowd.size();
            if ((int)cSmooth.size() < N) { cSmooth.resize(N); cAnim.resize(N); cPrev.resize(N); cCur.resize(N); cTier.resize(N, 0); }
            const UINT crowdBase = (UINT)items.size();
            const UINT skinBase = skinCursor;
            items.resize((size_t)crowdBase + (size_t)N);
            const unsigned pt = parallelGather ? poolThreads : 1;
            parallelFor(N, pt, [&](int lo, int hi, int) {
                for (int i = lo; i < hi; ++i) {
                    const auto& c = snap.crowd[(size_t)i];
                    DrawItem& it = items[(size_t)crowdBase + (size_t)i]; it.valid = false; it.look = nullptr; it.lod = false; it.lod2 = false;
                    SmoothXf& s = cSmooth[(size_t)i];
                    if (!s.init) { s.x = c.x; s.z = c.z; s.y = 0; s.yaw = c.yaw; s.init = true; }
                    else { s.x += (c.x - s.x) * a; s.z += (c.z - s.z) * a; float dy = c.yaw - s.yaw; while (dy > XM_PI) dy -= XM_2PI; while (dy < -XM_PI) dy += XM_2PI; s.yaw += dy * a; }
                    const float wx = s.x, wz = s.z, wyaw = s.yaw, wy = floorY(s.x, s.z);
                    if (cullOn && !sphereVisible(wx, wy + 0.9f, wz, 1.35f)) continue;
                    const uint64_t key = eid('c', c.id);
                    CharModel* cm = pickModel("", (uint32_t)key, false);
                    if (!cm || cm->data.joints.empty()) continue;
                    AnimState& an = cAnim[(size_t)i]; an.last = now;
                    advanceAnim(an, clipFor(*cm, c.anim, true), dt, 1.0f);
                    const float bob = (c.anim == 2) ? 0.06f * fabsf(sinf(an.t * 6.0f)) : 0.0f;
                    const XMMATRIX body = XMMatrixMultiply(XMMatrixRotationY(wyaw), XMMatrixTranslation(wx, wy + bob, wz));
                    const XMMATRIX model = XMMatrixMultiply(XMMatrixScaling(cm->scale, cm->scale, cm->scale), body);
                    if (lodOn && cm->hasLod) {
                        float dx = wx - camWorld.x, dyy = wy - camWorld.y, dz = wz - camWorld.z; float d = sqrtf(dx * dx + dyy * dyy + dz * dz);
                        float projPx = 1.9f * ((float)rh / 0.9326f) / (d > 0.5f ? d : 0.5f);
                        // 3-tier LOD with HYSTERESIS: enter thresholds tighter than exit (gap) so a char sitting near a
                        // boundary doesn't flip tier every frame as the camera jitters. tier2 only for sub-15px specks.
                        uint8_t tier = cTier[(size_t)i];
                        if      (tier == 0) { if (projPx < 65.0f) tier = 1; }
                        else if (tier == 1) { if (projPx > 78.0f) tier = 0; else if (projPx < 15.0f && cm->hasLod2) tier = 2; }
                        else                { if (projPx > 19.0f) tier = 1; }
                        cTier[(size_t)i] = tier;
                        it.lod = (tier >= 1); it.lod2 = (tier == 2);
                    }
                    const UINT cmIdx = (UINT)(cm - models.data());
                    if (it.lod) it.pslot = cmIdx * kFarPoses + (UINT)((size_t)i % (size_t)kFarPoses);   // shared far-pose pool (per model) — pose-LOD
                    else { if (skinBase + (UINT)i >= kMaxSkinned) continue; it.pslot = skinBase + (UINT)i; }   // individual near pose
                    it.cm = cm;
                    it.clip = an.clip; it.t = an.t; it.prevClip = an.prevClip; it.prevT = an.prevT; it.blend = an.blend;
                    it.variant = (int)((key * 2654435761ull >> 13) % 4u);
                    XMStoreFloat4x4(&it.bodyF, body);
                    XMStoreFloat4x4(&it.base.mvp, XMMatrixMultiply(model, viewProj));
                    XMStoreFloat4x4(&it.base.model, model);
                    XMStoreFloat4x4(&it.base.mvpNoJit, XMMatrixMultiply(model, viewProjNoJit));
                    XMFLOAT4X4 mf; XMStoreFloat4x4(&mf, model);
                    if (!resetHistory) XMStoreFloat4x4(&it.base.prevMvp, XMMatrixMultiply(XMLoadFloat4x4(&cPrev[(size_t)i]), prevViewProjNoJit));
                    else it.base.prevMvp = it.base.mvpNoJit;
                    cCur[(size_t)i] = mf;
                    it.base.mvScale = XMFLOAT4(0.5f * (float)rw, -0.5f * (float)rh, 0, 0);
                    it.valid = true;
                }
            });
            if (skinBase < kMaxSkinned) skinCursor = skinBase + (UINT)std::min((UINT)N, kMaxSkinned - skinBase);
            for (UINT i = crowdBase; i < (UINT)items.size(); ++i) { if (items[(size_t)i].valid) ++skinnedDrawn; else ++culled; }
        }
        // npcs (from welcome — idle; fallback steady blue)
        for (const auto& n : wc.npcs) {
            const uint64_t k = eid('n', n.id);
            SmoothXf& s = track(k, n.x, n.z, 0, n.yaw, 1.0f);
            drawEntity(k, nullptr, "", 0, true, false, s.x, floorY(s.x, s.z), s.z, s.yaw, XMFLOAT3(0.35f, 0.55f, 0.95f), 0.10f);
        }
        if ((frameIndex % 600) == 0) for (auto it = anims.begin(); it != anims.end();) { if (now - it->second.last > 5000) it = anims.erase(it); else ++it; }
        lastGatherMs = (float)std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _gatherT0).count();
        // --- PHASE B: parallel per-character pose eval into disjoint palette slots (the multicore win) ---
        {
            auto _pb = std::chrono::steady_clock::now();
            // shared far-pose pool: kFarPoses phase-offset poses PER MODEL; far-LOD crowd samples these (pose-LOD: ~90k solves -> ~96)
            for (size_t m = 0; m < models.size(); ++m) {
                const CharModel& cm = models[m]; if (cm.data.clips.empty()) continue;
                int clip = cm.clipWalk >= 0 ? cm.clipWalk : (cm.clipIdle >= 0 ? cm.clipIdle : 0);
                float base = (float)(now % 100000) * 0.001f;
                for (UINT k = 0; k < kFarPoses; ++k) {
                    XMFLOAT4X4* pd = reinterpret_cast<XMFLOAT4X4*>(paletteBase + ((size_t)slot * kMaxSkinned + ((UINT)m * kFarPoses + k)) * kPaletteBytes);
                    model::evalPose(cm.data, clip, base + (float)k * 0.17f, -1, 0.0f, 0.0f, pd, nullptr);
                }
            }
            unsigned pt = parallelPose ? poolThreads : 1;
            parallelFor((int)items.size(), pt, [&](int lo, int hi, int) {
                for (int i = lo; i < hi; ++i) {
                    DrawItem& di = items[(size_t)i];
                    if (!di.valid || di.lod) continue;   // culled, or far-LOD (uses the shared far-pose pool, no individual solve)
                    // direct-to-mapped-palette: evalPose writes straight into this char's disjoint palette slot (no scratch, no memcpy)
                    XMFLOAT4X4* pdst = reinterpret_cast<XMFLOAT4X4*>(paletteBase + ((size_t)slot * kMaxSkinned + di.pslot) * kPaletteBytes);
                    model::evalPose(di.cm->data, di.clip, di.t, di.prevClip, di.prevT, di.blend, pdst,
                                    (static_cast<const net::LookState*>(di.look) && static_cast<const net::LookState*>(di.look)->present) ? di.joints.data() : nullptr);
                }
            });
            poseAccumMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _pb).count();
        }
        auto _emitT0 = std::chrono::steady_clock::now();
        // --- PHASE C: emit submesh instances into per-CHUNK bucket sets, IN PARALLEL (no contention, no locks) ---
        {
            unsigned pt = parallelPose ? poolThreads : 1;
            if (threadBuckets.size() < pt) threadBuckets.resize(pt);
            const UINT nb = bktCount();
            for (auto& tb : threadBuckets) { if (tb.size() < nb) tb.resize(nb); for (auto& v : tb) v.clear(); }   // reuse capacity — no per-frame realloc churn
            parallelFor((int)items.size(), pt, [&](int lo, int hi, int idx) {
                auto& tb = threadBuckets[(size_t)idx];
                for (int i = lo; i < hi; ++i) if (items[(size_t)i].valid) emitItemTo(items[(size_t)i], tb);
            });
        }
        // look-gear (immediate D3D drawMesh — NOT thread-safe) runs serially; only look-items (players), few
        for (const DrawItem& di : items) if (di.valid && static_cast<const net::LookState*>(di.look) && static_cast<const net::LookState*>(di.look)->present) drawLook(*static_cast<const net::LookState*>(di.look), XMLoadFloat4x4(&di.bodyF), di.cm, di.joints.data());
        lastEmitMs = (float)std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _emitT0).count();
        auto _mergeT0 = std::chrono::steady_clock::now();
        flushInstances();   // merge the per-chunk buckets -> batched instanced draws (the crowd win)
        lastMergeMs = (float)std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _mergeT0).count();
        // dummies (grey cones)
        for (const auto& d : snap.dummies) {
            if (d.dead) continue;
            SmoothXf& s = track(eid('d', d.id), d.x, d.z, 0, 0, a);
            drawMesh(coneMesh, coneModel(s.x, 0, s.z, 0.7f, 1.7f, 0), XMFLOAT3(0.5f, 0.5f, 0.55f), 0.0f);
        }
        // shadowenemy shades (magenta cones)
        for (const auto& c : snap.shadowenemy) {
            if (c.dead) continue;
            SmoothXf& s = track(eid('x', c.id), c.x, c.z, 0, c.yaw, a);
            drawMesh(coneMesh, coneModel(s.x, 0, s.z, 0.6f, 1.9f, s.yaw), XMFLOAT3(0.90f, 0.20f, 0.72f), 0.25f);
        }
        // fires (small orange glowing cones)
        for (const auto& f : snap.fires) {
            if (f.out) continue;
            SmoothXf& s = track(eid('f', f.id), f.x, f.z, 0, 0, 1.0f);
            drawMesh(coneMesh, coneModel(s.x, 0, s.z, 0.45f, 0.9f, 0), XMFLOAT3(1.0f, 0.5f, 0.12f), 0.75f);
        }
        // projectiles (cyan bolt / magenta charged — small floating cones)
        for (const auto& pr : snap.proj) {
            SmoothXf& s = track(eid('j', pr.id), pr.x, pr.z, pr.y, 0, a);
            XMFLOAT3 col = (pr.kind == "charged") ? XMFLOAT3(0.95f, 0.35f, 0.95f) : XMFLOAT3(0.40f, 0.90f, 1.0f);
            float sz = (pr.kind == "charged") ? 0.45f : 0.25f;
            drawMesh(coneMesh, coneModel(s.x, s.y, s.z, sz, sz * 1.6f, 0), col, 0.85f);
        }
        // boss (big magenta cone) + telegraph ring on the floor
        if (snap.boss.present && !snap.boss.dead) {
            SmoothXf& s = track(eid('b', snap.boss.id), snap.boss.x, snap.boss.z, 0, snap.boss.yaw, a);
            drawMesh(coneMesh, coneModel(s.x, 0, s.z, 1.8f, 4.2f, s.yaw), XMFLOAT3(0.95f, 0.15f, 0.35f), 0.20f);
            if (snap.boss.telegraph) {
                float pulse = 5.5f + 0.6f * sinf((float)now * 0.006f);
                XMMATRIX ring = XMMatrixMultiply(XMMatrixScaling(pulse, 1.0f, pulse), XMMatrixTranslation(s.x, 0.03f, s.z));
                drawMesh(ringMesh, ring, XMFLOAT3(1.0f, 0.12f, 0.28f), 1.0f);
            }
        }

        // ---- follow-spotlights (additive pass, after all opaque geometry): translucent beam cone
        // from the ceiling rig down to the tracked floor point + a bright pool disc where it lands.
        // Fog for additive draws blends toward BLACK (fading the light), never toward the fog color.
        {
            cmd->SetPipelineState(psoAdd.Get());   // PSAdd: unlit, fog toward black
            for (int i = 0; i < 3 && !flatLight; ++i) {
                const SpotRig& r = spots[i];
                float k = 0.07f * spotDim;   // haze, not a wash: the additive beam must never hide the fur/skin it lands on
                XMFLOAT3 beam(r.color.x * k, r.color.y * k, r.color.z * k);
                drawMesh(coneMesh, BeamXf(spotTop[i], spotHit[i], r.radius), beam, 1.0f);
                float pk = 0.30f * spotDim;
                XMFLOAT3 pool(r.color.x * pk, r.color.y * pk, r.color.z * pk);
                drawMesh(discMesh, XMMatrixMultiply(XMMatrixScaling(r.radius, 1, r.radius), XMMatrixTranslation(spotHit[i].x, spotHit[i].y, spotHit[i].z)), pool, 1.0f);
                drawMesh(discMesh, XMMatrixMultiply(XMMatrixScaling(r.radius * 0.45f, 1, r.radius * 0.45f), XMMatrixTranslation(spotHit[i].x, spotHit[i].y + 0.005f, spotHit[i].z)), pool, 1.0f);
            }
        }

        // ---- M5: reconstruct render-size scene -> output-size upOutput, copy into the backbuffer ----
        barrier(sceneColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(sceneMvec.Get(),  D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barrier(depth.Get(),      D3D12_RESOURCE_STATE_DEPTH_WRITE,   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        up::FrameIn fi;
        fi.cmd = cmd.Get(); fi.color = sceneColor.Get(); fi.depth = depth.Get(); fi.mvec = sceneMvec.Get();
        fi.exposure = exposureTex.Get(); fi.output = upOutput.Get(); fi.backbuffer = targets[frame].Get();
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
        ups->evaluate(fi);

        barrier(upOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        barrier(targets[frame].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(targets[frame].Get(), upOutput.Get());
        // ---- --shot: grab this frame's reconstructed output on the way to PRESENT ----
        bool shotNow = !shotPath.empty() && !shotDone && (now - startTick) >= shotAtMs;
        if (shotNow) captureBackbuffer(upOutput.Get());
        barrier(upOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barrier(targets[frame].Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        fi.backbufferState = D3D12_RESOURCE_STATE_PRESENT;
        ups->beforePresent(fi);
        if (tsHeap) {   // GPU-work end + resolve this slot's pair to the readback buffer
            cmd->EndQuery(tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot + 1);
            cmd->ResolveQueryData(tsHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 2 * slot, 2, tsReadback.Get(), sizeof(UINT64) * 2 * slot);
        }
        lastCpuRecMs = (float)std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - _recT0).count();
        lastPoseMs = (float)poseAccumMs;
        cmd->Close();
        ID3D12CommandList* lists[] = { cmd.Get() };
        queue->ExecuteCommandLists(1, lists);
        antilag.markEndOfFrameRendering();
        antilag.setFrameGenFrameType(false);
        ups->present(swap.Get(), vsync ? 1 : 0, (!vsync && tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0);
        ups->afterPresent();
        // ---- M6: retire by slot instead of flushing. The fence value signalled here is what the
        // slot waits on kFrameCount frames from now; --inflight < 3 waits earlier (1 = old behaviour).
        slotFence[slot] = ++fenceVal;
        queue->Signal(fence.Get(), fenceVal);
        if (inflight < (int)kFrameCount) waitFence(fenceVal - (UINT64)(inflight - 1));
        if (shotNow) { flush(); writeShotBmp(); shotDone = true; }
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
static Renderer& g = *new Renderer;

// <gen:wndproc> generated by tools/buildctl.py - edit build/flags.json, not this block
LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (m == WM_KEYDOWN && !(l & (1 << 30))) {
        switch (w) {
        case VK_F2: if (g.ups) { g.pendingMode = ((g.pendingMode < 0 ? g.ups->mode() : g.pendingMode) + 1) % g.ups->modeCount(); } return 0;   // cycle quality mode
        case VK_F3: if (g.ups) { g.pendingFG = g.ups->fgEnabled() ? 0 : 1; } return 0;   // toggle frame generation
        case VK_F4: { g.pendingMult = true; } return 0;   // cycle FG multiplier
        case VK_F5: { g.resetHistory = true; } return 0;   // reset upscaler history
        case VK_ESCAPE: { PostQuitMessage(0); } return 0;   // quit
        case 'I': { g.instancingOn = !g.instancingOn; } return 0;   // toggle cross-character instancing (batched vs per-char) — live A/B
        case 'P': { g.parallelPose = !g.parallelPose; } return 0;   // toggle parallel per-char pose (multicore vs serial) — live A/B
        case 'T': { g.perfHud = !g.perfHud; } return 0;   // toggle live perf HUD vs full client title
        case 'C': { g.cullOn = !g.cullOn; } return 0;   // toggle frustum culling (skip off-screen characters) — live A/B
        case 'L': { g.lodOn = !g.lodOn; } return 0;   // toggle far-LOD meshes (decimated distant characters) — live A/B
        case 'G': { g.parallelGather = !g.parallelGather; } return 0;   // toggle parallel crowd gather (multicore) vs serial — live A/B
        default: break;
        }
    }
    return DefWindowProc(h, m, w, l);
}
// </gen:wndproc>

// <gen:title> generated by tools/buildctl.py - edit build/flags.json, not this block
static void applyClientTitle(HWND hwnd, bool srv, bool connected) {
    wchar_t t[1024];
    swprintf(t, 1024, L"%hs - DX12 client (%hs %hs | %hs %hs | %hs %ux%u->%ux%u | %hs | %hs | %.0f fps | inflight %d | %ls, %hs, players %d, crowd %d, skinned %u)",
        STARLIGHT_PRODUCT,
        STARLIGHT_MILESTONE,
        STARLIGHT_BUILD_TAG,
        g.ups ? up::pathName(g.ups->path()) : "-",
        g.ups ? g.ups->versionString().c_str() : "",
        g.ups ? g.ups->modeName(g.ups->mode()) : "-",
        g.rw,
        g.rh,
        g.w,
        g.h,
        g.ups ? g.ups->fgStatus().c_str() : "FG -",
        g.latencyTitle().c_str(),
        g.fpsNow,
        g.inflight,
        srv ? L"server" : (connected ? L"server (no state yet)" : L"mock"),
        net::encounterName(g.lastPhase),
        g.lastPlayers,
        g.lastCrowd,
        g.skinnedDrawnLast);
    SetWindowTextW(hwnd, t);
}
static const uint64_t kTitleRefreshMs = 500;
// </gen:title>
// live perf HUD in the title bar — the real-time readout: press I to toggle instancing and watch draws + CPU cost move.
static void applyPerfHud(HWND hwnd) {
    float gms = g.gpuMsNow > 0 ? g.gpuMsNow : 0.0f;
    wchar_t t[512];
    swprintf(t, 512,
        L"SampleGame  |  INST %ls[I]  POOL %ls %uc[P]  CULL %ls[C]  |  %u drawn / %u culled  |  %u draws  |  GPU %.2f ms (%.0f fps)  |  CPU %.2f ms (pose %.2f)  |  %ux%u",
        g.instancingOn ? L"ON" : L"OFF",
        g.parallelPose ? L"ON" : L"OFF", g.poolThreads,
        g.cullOn ? L"ON" : L"OFF",
        g.skinnedDrawnLast, g.culledLast, g.drawCallLast,
        gms, gms > 0 ? 1000.0f / gms : 0.0f,
        g.lastCpuRecMs, g.lastPoseMs,
        g.rw, g.rh);
    SetWindowTextW(hwnd, t);
}
// Beat Windows' foreground-steal LOCK so the window becomes foreground on launch WITHOUT a manual click.
// A non-foreground flip-model window is DWM present-throttled to ~20fps; foreground => independent flip => real fps.
// Technique: attach to the current foreground thread's input queue (so we share its input state) + drop the
// foreground-lock timeout, then SetForegroundWindow, then restore. This is the standard way to bypass the lock.
static std::wstring DefaultModelPath() {
    wchar_t buf[MAX_PATH]; GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t bin = p.find_last_of(L"\\/");
    if (bin != std::wstring::npos) p = p.substr(0, bin);
    size_t nd = p.find_last_of(L"\\/");
    if (nd != std::wstring::npos) p = p.substr(0, nd);
    size_t sr = p.find_last_of(L"\\/");
    if (sr != std::wstring::npos) p = p.substr(0, sr);
    return p + L"\\build\\models\\fox-lead.glb";
}

// ---- built-in MOCK snapshot loop (mirrors client/room/game.js startMock + net/test_net.cpp) ----
static std::string mockWelcome() {
    return R"JSON({
      "t":"welcome","id":"me","world":{"half":30},"tick":30,
      "consts":{"WALK":5.5,"SPRINT":9.5,"JUMP_V":7.5,"DODGE_SPEED":16,"STAM_MAX":100,"DODGE_COST":33,"CHARGE_FULL":1.15},
      "bar":{"x":0,"z":-22},
      "catalog":{"tattooDesigns":[{"id":0,"name":"heart","label":"Magenta heart","defaultColor":"magenta","lore":"Those who choose the magenta heart do so to signify that they are fertile."}]},
      "npcs":[
        {"id":"n1","role":"bartender","x":-14,"z":0,"yaw":1.6,"line":"What will it be, stranger?"},
        {"id":"n2","role":"friend","x":-3,"z":4,"yaw":0,"line":"Grab a seat, show is about to start."},
        {"id":"n3","role":"ally","x":3,"z":-3,"yaw":3.1,"line":"Stick close if things go bad."}
      ],
      "crowd":[
        {"id":"cd0","x":3,"z":0,"yaw":0.2,"kind":"dancer","anim":2},
        {"id":"cd1","x":4.5,"z":1,"yaw":0.4,"kind":"dancer","anim":1},
        {"id":"cp0","x":-12.5,"z":-8.5,"yaw":0,"kind":"patron","anim":0}
      ]
    })JSON";
}
static std::string mockSnapshot(const std::string& phase, double darkness, double t, int stress = 0) {
    bool combatish = (phase == "attack" || phase == "combat" || phase == "boss");
    char pl[1024];   // the literal below is ~600 bytes; a 256 buffer truncated it into broken JSON (mock thread threw -> terminate)
    // drift the local player a little so motion + follow-cam are visible
    int pn = std::snprintf(pl, sizeof(pl),
        R"("players":[{"id":"me","name":"Chaos","x":%.2f,"y":0,"z":%.2f,"yaw":%.2f,"pitch":0,"hp":100,"stam":90,"state":0,"anim":1,"charge":0,"lastSeq":7,)"
        R"("look":{"species":"fox","sex":"f","tattoo":{"on":true,"design":0,"color":"magenta"},"gear":{"head":{"piece":"hood","color":null,"show":false},"shoulders":null,"chest":{"piece":"crop","color":"magenta","show":true},"gloves":{"piece":"wraps","color":"white","show":false},"pants":{"piece":"cargo","color":"blue","show":true},"hip":null,"feet":{"piece":"boots","color":"black","show":false},"weapon":{"piece":"elembow","color":"white","show":true}}}}],)",
        2.0 * sin(t * 0.6), 2.0 * cos(t * 0.6), t * 0.6);
    if (pn < 0 || pn >= (int)sizeof(pl)) { logLine("mockSnapshot: player literal truncated"); pl[0] = 0; }
    std::string boss = (phase == "boss")
        ? R"("boss":{"id":"boss","x":0,"z":-10,"yaw":0.4,"hp":620,"maxhp":800,"active":true,"dead":false,"telegraph":true,"kind":"warden"},)"
        : R"("boss":null,)";
    std::string shadowenemy = combatish
        ? R"("shadowenemy":[{"id":"c1","x":7,"z":-5,"yaw":-1.2,"hp":40,"maxhp":60,"kind":"shadowenemy","dead":false},{"id":"c2","x":8,"z":6,"yaw":0.8,"hp":55,"maxhp":60,"kind":"shadowenemy","dead":false}],)"
        : R"("shadowenemy":[],)";
    std::string proj = combatish
        ? R"("proj":[{"id":"b1","x":6,"y":1.2,"z":-3,"kind":"bolt"},{"id":"b2","x":-5,"y":1.4,"z":4,"kind":"charged"}],)"
        : R"("proj":[],)";
    std::string fires = combatish
        ? std::string(R"("fires":[{"id":"f1","x":-6,"z":-6,"hp":30,"out":false},{"id":"f3","x":4,"z":8,"hp":20,"out":)")
            + (phase == "cleared" ? "true" : "false") + "}],"
        : R"("fires":[],)";
    // base ambient crowd, plus --stress N synthetic dancers (grid around the stage) to load-test the
    // per-entity draw + skinning path — the boss-fight-with-a-full-bar case. Capped downstream at maxSkinned.
    std::string crowd = R"("crowd":[{"id":"cd0","x":3.1,"z":0.2,"yaw":0.9,"kind":"dancer","anim":2},{"id":"cd1","x":4.6,"z":1.1,"yaw":0.5,"kind":"dancer","anim":1},{"id":"cp0","x":-12.5,"z":-8.5,"yaw":0.1,"kind":"patron","anim":0},{"id":"cl0","x":9,"z":0,"yaw":0.3,"kind":"patron","anim":1})";
    for (int i = 0; i < stress; ++i) {
        double ang = (double)i * 2.39996323;                  // golden angle -> sunflower/phyllotaxis packing = a dense disc of dancers AROUND the stage
        double r = 1.15 * sqrt((double)(i + 1));
        double sx = r * cos(ang), sz = r * sin(ang);
        double syaw = 0.9 * sin(t + i * 0.7);                 // gently turning so skinning + follow varies
        int anim = 1 + (i & 1);                               // walk/dance -> exercises the skinned palette path
        char b[192];
        std::snprintf(b, sizeof(b), R"(,{"id":"s%d","x":%.2f,"z":%.2f,"yaw":%.2f,"kind":"dancer","anim":%d})", i, sx, sz, syaw, anim);
        crowd += b;
    }
    crowd += "],";
    return std::string("{\"t\":\"state\",") + pl + proj
        + R"("dummies":[{"id":"d0","x":-10,"z":10,"hp":60,"maxhp":60,"dead":false},{"id":"d1","x":10,"z":10,"hp":30,"maxhp":60,"dead":false}],)"
        + shadowenemy + fires
        + crowd
        + boss
        + "\"encounter\":\"" + phase + "\",\"darkness\":" + std::to_string(darkness) + "}";
}

static int WINAPI RunProjectBody(HINSTANCE hInst, LPWSTR lpCmdLine, int nShow) {
    // <gen:args> generated by tools/buildctl.py - edit build/flags.json, not this block
    ClientArgs args;
    {
        std::wstring command=L"project "+std::wstring(lpCmdLine ? lpCmdLine : L"");
        int count=0; LPWSTR* argv=CommandLineToArgvW(command.c_str(),&count);
        std::vector<std::string> toks;
        for(int i=1;i<count;++i) { int n=WideCharToMultiByte(CP_UTF8,0,argv[i],-1,nullptr,0,nullptr,nullptr); std::string t(n,'\0'); WideCharToMultiByte(CP_UTF8,0,argv[i],-1,t.data(),n,nullptr,nullptr); t.pop_back(); toks.push_back(t); }
        LocalFree(argv);
        std::string perr;
        if (!parseClientArgs(toks, args, perr)) {   // no dialog: write the reason next to the exe and exit 3
            wchar_t exeP[MAX_PATH]; GetModuleFileNameW(nullptr, exeP, MAX_PATH); std::wstring ep(exeP); size_t sl = ep.find_last_of(L"\\/");
            ep = (sl == std::wstring::npos ? std::wstring(L".") : ep.substr(0, sl)) + L"\\SampleGameDX12.args.log";
            FILE* ef = nullptr; if (_wfopen_s(&ef, ep.c_str(), L"w") == 0 && ef) { fprintf(ef, "%s\n\n%s", perr.c_str(), clientUsage()); fclose(ef); }
            return 3;
        }
    }
    const UINT W = args.resW, H = args.resH;
    std::string host = args.host;
    uint16_t port = args.port;
    std::string logPath = args.logPath;
    std::string shotPath = args.shotPath;
    uint64_t shotAtMs = args.shotAtMs;
    bool autoWalk = args.autoWalk;
    std::string display = args.display;
    int stress = args.stress;
    int mtproof = args.mtproof;
    int noinstancing = args.noinstancing;
    int poolthreads = args.poolthreads;
    int nocull = args.nocull;
    int nolod = args.nolod;
    int nopargather = args.nopargather;
    // </gen:args>
    if (!logPath.empty()) { g_logT0 = GetTickCount64(); FILE* lf = nullptr; if (fopen_s(&lf, logPath.c_str(), "a") == 0) g_logFile = lf; }
    logLine("start host=" + host + " port=" + std::to_string(port) + (autoWalk ? " auto" : ""));

    Mesh forge; std::string err;
    if (!LoadGLB(std::wstring(args.modelsDir.begin(), args.modelsDir.end()) + L"\\fox-lead.glb", forge, err)) {
        logLine("glTF load FAILED: " + err);
        return 2;
    }

    // ---- net: connect, feed LatestState; fall back to MOCK if no live snapshot in ~2s ----
    static net::LatestState store;
    static net::WsClient   ws;
    static std::atomic<bool> live{ false };
    static std::string      myId = "me";

    static std::atomic<int> stateCount{ 0 };
    ws.onMessage([&](const std::string& text) {
        auto m = nlohmann::json::parse(text, nullptr, false);
        if (m.is_discarded()) return;
        std::string t = m.value("t", "");
        if (t == "welcome") {
            net::Welcome wc = net::parseWelcome(m);
            if (!wc.myId.empty()) myId = wc.myId;
            store.setWelcome(wc);
            std::string cat;
            for (const auto& d : wc.tattooDesigns) cat += " [" + std::to_string(d.id) + " " + d.name + "/" + d.label + "/" + d.defaultColor + "]";
            logLine("welcome id=" + wc.myId + " half=" + std::to_string(wc.worldHalf) + " tick=" + std::to_string(wc.tick)
                + " bar=" + (wc.barValid ? "(" + std::to_string(wc.barX) + "," + std::to_string(wc.barZ) + ")" : "MISSING")
                + " npcs=" + std::to_string(wc.npcs.size()) + " crowd=" + std::to_string(wc.crowd.size())
                + " phase=" + net::encounterName(wc.encounter) + " tattooDesigns=" + std::to_string(wc.tattooDesigns.size()) + cat);
        } else if (t == "state") {
            net::WorldState st = net::parseSnapshot(m);
            if (stateCount.fetch_add(1) == 0) {
                // first snapshot: log the local player's normalized look as the server echoes it
                for (const auto& p : st.players) {
                    if (p.id != myId) continue;
                    std::string gearDesc;
                    for (const auto& s : p.look.gear) gearDesc += " " + s.slot + "=" + s.piece + "/" + (s.color.empty() ? "null" : s.color) + "/" + (s.show ? "SHOW" : "hide");
                    logLine("first state: me=" + p.id + " name=" + p.name + " species=" + p.look.species + " sex=" + p.look.sex
                        + " tattoo{on=" + (p.look.tattoo.on ? "true" : "false") + " design=" + std::to_string(p.look.tattoo.design) + " color=" + p.look.tattoo.color + "}"
                        + " gear:" + gearDesc + " | players=" + std::to_string(st.players.size()) + " crowd=" + std::to_string(st.crowd.size()));
                }
            }
            store.set(st);
            live.store(true);
        } else if (t == "encounter" || t == "kill" || t == "bye") {
            logLine("msg " + text.substr(0, 160));
        }
    });
    bool connected = ws.connect(host, port, "/");
    logLine(std::string("connect ") + (connected ? "OK" : "FAILED (mock fallback)"));
    if (connected) {
        // join exactly as server.mjs expects: {t:"join", name, look}. The look exercises the gear
        // contract on purpose: sex f => only chest shows by default; pants + weapon are explicitly
        // toggled show:true; head/shoulders/gloves/hip/feet stay hidden (equipped, not drawn);
        // tattoo left to the server default (f => on, heart, magenta).
        nlohmann::json look = {
            { "species", "fox" }, { "sex", "f" },
            { "gear", {
                { "head", "hood" }, { "shoulders", "pads" },
                { "chest", { { "piece", "crop" }, { "color", "magenta" } } },
                { "gloves", "wraps" },
                { "pants", { { "piece", "cargo" }, { "color", "blue" }, { "show", true } } },
                { "hip", "sash" }, { "feet", "boots" },
                { "weapon", { { "piece", "elembow" }, { "color", "white" }, { "show", true } } } } }
        };
        nlohmann::json join = { { "t", "join" }, { "name", "SampleGameDX12" }, { "look", look } };
        std::string js = join.dump();
        ws.sendText(js);
        logLine("sent join: " + js);
    }

    // MOCK feeder: if no live snapshot arrives within 2s, cycle all 5 encounter phases forever.
    std::atomic<bool> stopMock{ false };
    std::thread mockThread([&] { try {
        for (int i = 0; i < 20 && !live.load() && !stopMock.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (live.load() || stopMock.load()) return;
        store.setWelcome(net::parseWelcome(nlohmann::json::parse(mockWelcome())));
        const char* phases[] = { "intro", "attack", "combat", "boss", "cleared" };
        const double darks[] = { 0.10, 0.45, 0.70, 1.00, 0.15 };
        double t = 0; int ph = 0; uint64_t phaseStart = GetTickCount64();
        while (!stopMock.load() && !live.load()) {
            uint64_t now = GetTickCount64();
            if (now - phaseStart > 3500) { ph = (ph + 1) % 5; phaseStart = now; }
            t += 0.05;
            store.set(net::parseSnapshot(nlohmann::json::parse(mockSnapshot(phases[ph], darks[ph], t, stress))));
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // ~20 Hz mock snap
        }
    } catch (const std::exception& e) { logLine(std::string("mock feeder died: ") + e.what()); } });

    // DPI awareness FIRST — before any monitor query / window create. Without it, GetSystemMetrics
    // and monitor rects return LOGICAL (scaled-down) pixels on a scaled display, so "fullscreen"
    // comes out as a smaller window with desktop showing around it. Runtime-resolved => builds on any SDK.
    {
        if (HMODULE u32 = GetModuleHandleW(L"user32.dll")) {
            typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
            auto pCtx = (SetCtxFn)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
            // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
            if (!pCtx || !pCtx((HANDLE)(INT_PTR)-4)) {
                typedef BOOL (WINAPI *OldAwareFn)(void);
                if (auto pOld = (OldAwareFn)GetProcAddress(u32, "SetProcessDPIAware")) pOld();
            }
        }
    }
    WNDCLASSEX wc{}; wc.cbSize = sizeof(wc); wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"SampleGameDX12"; RegisterClassEx(&wc);
    // --display: windowed = bordered window at --res; borderless = borderless popup covering the
    // primary monitor (gets DXGI independent-flip -> no compositor/occlusion throttle); fullscreen =
    // the same monitor-covering popup, plus a best-effort exclusive-fullscreen transition after init.
    const std::string disp = args.display;
    const bool coverScreen = (disp == "borderless" || disp == "fullscreen");
    const bool topmost     = (disp == "fullscreen");   // fullscreen covers the taskbar (topmost); borderless doesn't
    UINT ew = W, eh = H;                       // effective client (swapchain) size
    DWORD wstyle, exstyle = 0; int px, py, wpix, hpix;
    if (coverScreen) {
        // TRUE physical monitor rect (we're DPI-aware now) so the popup covers the WHOLE panel edge-to-edge
        MONITORINFO mi{ sizeof(mi) };
        HMONITOR hm = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        if (GetMonitorInfo(hm, &mi)) {
            px = mi.rcMonitor.left; py = mi.rcMonitor.top;
            wpix = mi.rcMonitor.right - mi.rcMonitor.left; hpix = mi.rcMonitor.bottom - mi.rcMonitor.top;
        } else {
            px = 0; py = 0; wpix = GetSystemMetrics(SM_CXSCREEN); hpix = GetSystemMetrics(SM_CYSCREEN);
        }
        ew = (UINT)wpix; eh = (UINT)hpix;
        wstyle = WS_POPUP | WS_VISIBLE;
        if (topmost) exstyle = WS_EX_TOPMOST;
    } else {
        RECT r{ 0, 0, (LONG)W, (LONG)H }; AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        wstyle = WS_OVERLAPPEDWINDOW; px = CW_USEDEFAULT; py = CW_USEDEFAULT; wpix = r.right - r.left; hpix = r.bottom - r.top;
    }
    HWND hwnd = CreateWindowEx(exstyle, wc.lpszClassName, L"SampleGame", wstyle,
        px, py, wpix, hpix, nullptr, nullptr, hInst, nullptr);
    logLine("display: " + disp + " " + std::to_string(ew) + "x" + std::to_string(eh)
            + (topmost ? " topmost" : "") + " @(" + std::to_string(px) + "," + std::to_string(py) + ")");

    // <gen:apply-args> generated by tools/buildctl.py - edit build/flags.json, not this block
    g.upsRequest = args.upsRequest;
    g.modeRequest = args.modeRequest;
    g.fgRequest = args.fgRequest;
    g.latencyRequest = args.latencyRequest;
    g.vsync = !args.noVsync;
    g.benchMs = args.benchMs;
    g.reportPath = args.reportPath;
    g.d3dDebug = args.d3dDebug;
    g.inflight = args.inflight;
    g.texRes = (UINT)std::max(64, args.texRes);
    g.modelsDir = args.modelsDir;
    g.texturesDir = args.texturesDir;
    g.noChars = args.noChars;
    g.noFur = args.noFur;
    g.flatLight = args.flatLight;
    g.camDist = args.camDist;
    g.camHigh = args.camHigh;
    // </gen:apply-args>
    g.instancingOn = (noinstancing == 0);   // --noinstancing starts in per-character-draw mode for the A/B (toggle live with I)
    g.poolThreads = poolthreads > 0 ? (unsigned)poolthreads : physicalCoreCount();   // parallel per-char pose pool (0=auto=physical cores; 1=serial)
    g.hideRoom = (stress > 0);   // stress crowd spills far beyond the room; hide the box so it can't occlude the view
    g.cullOn = (nocull == 0);   // --nocull starts with frustum culling off for the A/B (toggle live with C)
    g.lodOn = (nolod == 0);     // --nolod starts with far-LOD off for the A/B (toggle live with L)
    g.parallelGather = (nopargather == 0);   // --nopargather = serial crowd gather for the A/B (toggle live with G)
    try { g.init(hwnd, ew, eh, forge, &store, connected ? &ws : nullptr, myId); }
    catch (const std::exception& e) {
        stopMock.store(true); if (mockThread.joinable()) mockThread.join(); ws.close();
        logLine(std::string("DX12 init FAILED: ") + e.what()); return 2;   // no dialog: the reason is in --log
    }
    if (mtproof > 0) {   // multicore proof run: measure evalPose scaling across cores, write table, exit
        g.mtProof(mtproof);
        stopMock.store(true); if (mockThread.joinable()) mockThread.join(); ws.close();
        return 0;
    }
    // NOTE: exclusive fullscreen (SetFullscreenState) is unsupported on this flip-model + FG-wrapped
    // swapchain — the failed transition black-screens it and stops FG doubling. So "fullscreen" is a
    // borderless-fullscreen popup (identical present path to borderless; the modern standard). Do NOT
    // call g.enterFullscreen() here.
    g.in.autoWalk = autoWalk;
    if (!shotPath.empty()) { std::wstring wp; for (char c : shotPath) wp.push_back((wchar_t)(unsigned char)c); g.shotPath = wp; g.shotAtMs = shotAtMs; }

    ShowWindow(hwnd, nShow);
    if (coverScreen) {
        // snap to the exact monitor rect; fullscreen goes HWND_TOPMOST so it sits over the taskbar,
        // borderless stays HWND_TOP (composited, alt-tab friendly). This is what makes fullscreen
        // actually cover the panel instead of reading as "just a large window".
        SetWindowPos(hwnd, topmost ? HWND_TOPMOST : HWND_TOP, px, py, wpix, hpix, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    // Foreground activation is requested once at project launch.
    logLine(RenderDevice::activateProjectWindow(hwnd) ? "foreground acquired" : "foreground request denied"); //  // grab foreground on launch WITHOUT a manual click (beats the foreground-steal lock) -> independent flip -> real fps, not the 20fps DWM throttle
    MSG msg{};
    uint64_t lastTitle = 0;
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        else {
            g.myId = myId; g.render(hwnd);
            uint64_t now = GetTickCount64();
            if (now - lastTitle > kTitleRefreshMs) { lastTitle = now; if (g.perfHud) applyPerfHud(hwnd); else applyClientTitle(hwnd, connected && live.load(), connected); }
        }
    }

    stopMock.store(true); if (mockThread.joinable()) mockThread.join();
    ws.close();
    // orderly teardown: GPU idle, then the vendor backend (its contexts/threads/DLL) BEFORE the device
    // goes away at static destruction — otherwise the SDK DLL unloads under its own worker threads.
    g.teardown();
    logLine("exit");
    if (g_logFile) { std::lock_guard<std::mutex> lk(g_logMu); fclose(g_logFile); g_logFile = nullptr; }
    return 0;
}

extern "C" __declspec(dllexport) int WINAPI StarlightRunProject(HINSTANCE i, LPWSTR c, int s) { int result=RunProjectBody(i,c,s); delete &g; return result; }

