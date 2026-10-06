#include "render_scene.hpp"
#include "event_log.h"
#include "environment_effects.hpp"
#include "object_cache.hpp"
#include <algorithm>
#include <mutex>
#include <cfloat>
#include "platform.hpp"
#include <cstring>
#include <numbers>
#include <atomic>
#include <chrono>
namespace starlight::editor {
namespace {
using ProfileClock = std::chrono::steady_clock;
std::atomic<double> profileFlowMs{0}, profilePartsMs{0}, profileSerializeMs{0}, profileBaseMs{0};
std::atomic<uint64_t> profileCalls{0}, profileDeltaCalls{0}, profileBytes{0}, sceneTokens{0};
double profileMs(ProfileClock::time_point a, ProfileClock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
struct Writer {
    std::vector<uint8_t> bytes;
    template <class T> void put(T value) {
        auto p = reinterpret_cast<const uint8_t *>(&value);
        size_t at = bytes.size();
        bytes.resize(at + sizeof(T));
        std::memcpy(bytes.data() + at, p, sizeof(T));
    }
    void f(double x) {
        if (!std::isfinite(x) || std::abs(x) > FLT_MAX)
            throw std::runtime_error("Render value outside float range");
        put(float(x));
    }
    void vec(Vec3 p) {
        f(p.x);
        f(p.y);
        f(p.z);
    }
};
std::vector<MeshPart> generate_parts(const Character &c) {
    return c.Shape || c.Sculpt ? shape_geometry(c) : std::vector<MeshPart>(*load_model(c.Asset));
}
constexpr size_t kMaxPartEntries=4096,kMaxPartBytes=384ull<<20,kMaxBodyEntries=512,kMaxBodyBytes=256ull<<20;
size_t parts_bytes(const std::vector<MeshPart>& list){size_t total=0;for(const auto& part:list)total+=part.mesh.vertices.size()*sizeof(Vertex)+part.mesh.indices.size()*sizeof(int);return total;}
bool same_parts(const std::vector<MeshPart>& a,const std::vector<MeshPart>& b){
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i){
        const auto &x=a[i],&y=b[i];
        if(x.emissive!=y.emissive||x.color.x!=y.color.x||x.color.y!=y.color.y||x.color.z!=y.color.z||x.mesh.indices!=y.mesh.indices||x.mesh.vertices.size()!=y.mesh.vertices.size())return false;
        for(size_t k=0;k<x.mesh.vertices.size();++k){
            const auto &u=x.mesh.vertices[k],&v=y.mesh.vertices[k];
            if(std::memcmp(&u.position,&v.position,sizeof(Vec3))||std::memcmp(&u.normal,&v.normal,sizeof(Vec3))||std::memcmp(&u.color,&v.color,sizeof(Vec3))||u.kind!=v.kind||u.motionIdentity!=v.motionIdentity)return false;
        }
    }
    return true;
}
struct PartsEntry {std::shared_ptr<const std::vector<MeshPart>> parts;size_t bytes=0;uint64_t used=0;};
struct BodyEntry {std::shared_ptr<const ResidentBody> body;size_t bytes=0;uint64_t used=0;};
struct GeometryService {
    std::mutex lock;
    std::unordered_map<uint64_t,PartsEntry> parts;
    std::unordered_map<uint64_t,BodyEntry> bodies;
    size_t partBytes=0,bodyBytes=0;
    uint64_t clock=0;
    std::atomic<uint64_t> partHits{0},partMisses{0},partEvicted{0},partPinned{0},bodyHits{0},bodyMisses{0},bodyEvicted{0},bodyPinned{0};
    const bool verify=[]{char v[8]{};return GetEnvironmentVariableA("STARLIGHT_CACHE_VERIFY",v,8)&&v[0]=='1';}();
};
GeometryService& geometry_service(){static GeometryService service;return service;}
bool part_in_use(const std::shared_ptr<const std::vector<MeshPart>>& list){return list.use_count()>1;}
bool body_in_use(const std::shared_ptr<const ResidentBody>& body){
    if(body.use_count()>1)return true;
    for(const auto& part:body->parts)if(part.use_count()>1)return true;
    return false;
}
void evict_parts(GeometryService& s,uint64_t keep){
    std::vector<std::pair<uint64_t,uint64_t>> order;order.reserve(s.parts.size());
    for(const auto& [key,entry]:s.parts){if(key==keep)continue;if(part_in_use(entry.parts)){s.partPinned.fetch_add(1,std::memory_order_relaxed);continue;}order.emplace_back(entry.used,key);}
    std::sort(order.begin(),order.end());
    for(const auto& item:order){
        if(s.partBytes<=kMaxPartBytes/4*3&&s.parts.size()<=kMaxPartEntries/4*3)break;
        auto found=s.parts.find(item.second);s.partBytes-=found->second.bytes;s.parts.erase(found);s.partEvicted.fetch_add(1,std::memory_order_relaxed);
    }
}
void evict_bodies(GeometryService& s,uint64_t keep){
    std::vector<std::pair<uint64_t,uint64_t>> order;order.reserve(s.bodies.size());
    for(const auto& [key,entry]:s.bodies){if(key==keep)continue;if(body_in_use(entry.body)){s.bodyPinned.fetch_add(1,std::memory_order_relaxed);continue;}order.emplace_back(entry.used,key);}
    std::sort(order.begin(),order.end());
    for(const auto& item:order){
        if(s.bodyBytes<=kMaxBodyBytes/4*3&&s.bodies.size()<=kMaxBodyEntries/4*3)break;
        auto found=s.bodies.find(item.second);s.bodyBytes-=found->second.bytes;s.bodies.erase(found);s.bodyEvicted.fetch_add(1,std::memory_order_relaxed);
    }
}
std::vector<MeshPart> parts(const Character &c) {
    return *shared_parts(c);
}
} // namespace
uint64_t render_geometry_key(const Character& c){
    using namespace cache_detail;
    uint64_t h=geometry_key(c);
    if(c.Shape||c.Sculpt){
        h=mix(h,std::hash<std::string>{}(c.Color));
        if(c.Foliage){h=mix(h,1);for(double v:{c.Foliage->BarkScale,c.Foliage->BarkRelief,c.Foliage->BarkRoughness,c.Foliage->LeafTransmission,c.Foliage->LeafVeins,c.Foliage->LeafRoughness})h=mix(h,bits(v));}else h=mix(h,0);
    }
    return h?h:1;
}
namespace {
uint64_t generator_key(const Character& c){
    using namespace cache_detail;
    uint64_t h=1469598103934665603ull;
    for(double v:{c.BranchSpread,c.TargetX,c.TargetY,c.TargetZ})h=mix(h,bits(v));
    h=mix(h,std::hash<std::string>{}(c.Shape.value_or("")));h=mix(h,uint64_t(c.Surface));
    if(c.Glow>0)h=mix(h,0x676c6f77ull);
    if(c.Shape&&*c.Shape=="ring")h=mix(h,bits(c.RingInner));
    if(!c.Asset.empty())h=mix(h,std::hash<std::string>{}(c.Asset));
    if(c.Sculpt){h=mix(h,c.Sculpt->content_revision());h=mix(h,c.Sculpt->Positions.size());h=mix(h,c.Sculpt->Indices.size());}
    if(c.Shape||c.Sculpt){
        h=mix(h,std::hash<std::string>{}(c.Color));
        if(c.Foliage){h=mix(h,1);for(double v:{c.Foliage->BarkScale,c.Foliage->BarkRelief,c.Foliage->BarkRoughness,c.Foliage->LeafTransmission,c.Foliage->LeafVeins,c.Foliage->LeafRoughness})h=mix(h,bits(v));}else h=mix(h,0);
    }
    return h?h:1;
}
}
std::shared_ptr<const std::vector<MeshPart>> shared_parts(const Character& c){
    if(!c.Shape&&!c.Sculpt)return load_model(c.Asset);
    auto& s=geometry_service();
    if(c.Shape||c.Sculpt)c.validate();
    const uint64_t key=generator_key(c);
    {
        std::lock_guard guard(s.lock);
        auto found=s.parts.find(key);
        if(found!=s.parts.end()){
            found->second.used=++s.clock;s.partHits.fetch_add(1,std::memory_order_relaxed);
            auto hit=found->second.parts;
            if(s.verify&&(s.clock&63)==0&&!same_parts(*hit,generate_parts(c)))throw std::runtime_error("Render geometry cache returned stale parts for "+c.Id);
            return hit;
        }
    }
    s.partMisses.fetch_add(1,std::memory_order_relaxed);
    auto built=std::make_shared<const std::vector<MeshPart>>(generate_parts(c));
    std::lock_guard guard(s.lock);
    auto [slot,inserted]=s.parts.try_emplace(key);
    if(!inserted)return slot->second.parts;
    slot->second.parts=built;slot->second.bytes=parts_bytes(*built);slot->second.used=++s.clock;s.partBytes+=slot->second.bytes;
    if(s.partBytes>kMaxPartBytes||s.parts.size()>kMaxPartEntries)evict_parts(s,key);
    return built;
}
std::shared_ptr<const ResidentBody> resident_body(const Character& c){
    auto& s=geometry_service();
    const uint64_t key=render_geometry_key(c);
    {
        std::lock_guard guard(s.lock);
        auto found=s.bodies.find(key);
        if(found!=s.bodies.end()){found->second.used=++s.clock;s.bodyHits.fetch_add(1,std::memory_order_relaxed);return found->second.body;}
    }
    s.bodyMisses.fetch_add(1,std::memory_order_relaxed);
    auto body=std::make_shared<ResidentBody>();body->key=key;
    const double sx=c.Width*c.Size,sy=c.Height*c.Size,sz=c.Depth*c.Size;
    size_t bytes=0;
    const auto source=shared_parts(c);
    for(const auto& original:*source){
        if(original.mesh.indices.empty())continue;
        MeshPart part=original;
        for(auto& v:part.mesh.vertices){
            v.position={v.position.x*sx,v.position.y*sy,v.position.z*sz};
            const Vec3 n{v.normal.x/sx,v.normal.y/sy,v.normal.z/sz};const double length=n.length();if(length>1e-12)v.normal=n/length;
        }
        bytes+=part.mesh.vertices.size()*sizeof(Vertex)+part.mesh.indices.size()*sizeof(int);
        body->parts.push_back(std::make_shared<const MeshPart>(std::move(part)));
    }
    std::shared_ptr<const ResidentBody> shared=std::move(body);
    std::lock_guard guard(s.lock);
    auto [slot,inserted]=s.bodies.try_emplace(key);
    if(!inserted)return slot->second.body;
    slot->second.body=shared;slot->second.bytes=bytes;slot->second.used=++s.clock;s.bodyBytes+=bytes;
    if(s.bodyBytes>kMaxBodyBytes||s.bodies.size()>kMaxBodyEntries)evict_bodies(s,key);
    return shared;
}
RenderGeometryStats render_geometry_stats(){
    auto& s=geometry_service();
    std::lock_guard guard(s.lock);
    return {s.partHits.load(),s.partMisses.load(),s.partEvicted.load(),s.partPinned.load(),s.bodyHits.load(),s.bodyMisses.load(),s.bodyEvicted.load(),s.bodyPinned.load(),s.parts.size(),s.partBytes,s.bodies.size(),s.bodyBytes};
}
std::vector<MeshPart> object_parts(const Character& c){return *shared_parts(c);}
std::vector<std::shared_ptr<const MeshPart>> resident_body_parts(const Character& c){return resident_body(c)->parts;}
std::array<Vec3,4> pose_probe(const Character& c){return {world_point(c,{0,0,0}),world_point(c,{1,0,0}),world_point(c,{0,1,0}),world_point(c,{0,0,1})};}
Mesh object_mesh(const Character &c) {
    if(!in_object_cache()&&counts_as_static_bypass(c)){bypass_counters().staticMeshes.fetch_add(1,std::memory_order_relaxed);if(bypass_trace())bypass_counters().record(_ReturnAddress(),1);}
    Mesh m;
    const auto source=shared_parts(c);
    for (const auto &part : *source) {
        int offset = int(m.vertices.size());
        m.vertices.insert(m.vertices.end(), part.mesh.vertices.begin(), part.mesh.vertices.end());
        for (int i : part.mesh.indices)
            m.indices.push_back(i + offset);
    }
    return m;
}
// deltaOnly: write just the draws that change between updates (see render_scene_delta). The static scene
// stays in cache->packet and is neither rebuilt nor copied into the outgoing bytes.
static thread_local std::unordered_map<std::string,uint32_t>* partCountSink=nullptr; // set while the resident base is built
static thread_local const SceneData* headerScene=nullptr;
static thread_local const TerrainFlow* headerFlow=nullptr;
static std::vector<uint8_t> render_scene_impl(bool deltaOnly, bool &deltaUsable, const SceneData &scene, const TerrainFlow *flow,
                                  const std::vector<MeshPart> &overlays, RenderSceneCache *cache, bool runtimeEffects,bool gpuGrass,const std::vector<MeshPart> *trailing) {
    deltaUsable = false;
    // Paused/authored lava uses the same initial surface as playback, instead
    // of returning to the old flat fan when there is no supplied simulation.
    std::unique_ptr<TerrainFlow> previewLava;
    if(!flow&&((scene.Terrain&&scene.Terrain->WaterEnabled&&scene.Terrain->Lava)||std::any_of(scene.Characters.begin(),scene.Characters.end(),[](const auto& c){return c.Shape=="lava";}))){previewLava=std::make_unique<TerrainFlow>(scene.Terrain.value_or(TerrainData{}),scene.LiquidSources,scene.Characters);previewLava->lava_settings(scene.LavaInitialHeat,scene.LavaCoolingRate,scene.LavaCrustDepth,scene.LavaLightStrength,true);flow=previewLava.get();}
    // Keep terrain and authored static meshes resident while liquids and rigid
    // bodies change. Their motion must not rebuild every blade of grass.
    if(cache && !effect_split_enabled() && std::any_of(scene.Characters.begin(),scene.Characters.end(),[](const auto& c){return c.Dynamic && feeds_light_or_emitter_channel(c);}))cache=nullptr;
    if(cache && (cache->runtimeEffects!=runtimeEffects || cache->simulatedLiquids!=(flow!=nullptr)||cache->gpuGrass!=gpuGrass)){cache->clear();cache->runtimeEffects=runtimeEffects;cache->simulatedLiquids=flow!=nullptr;cache->gpuGrass=gpuGrass;}
    if(deltaOnly && !cache)return {};
    if(cache && cache->packet.empty()){
        auto base=scene;
        if(flow && base.Terrain)base.Terrain->WaterEnabled=false;
        cache->resting.clear();
        if(cache->allowResting&&repair_delta())for(const auto& c:scene.Characters)if(resting_eligible(c))cache->resting.emplace(c.Id,pose_probe(c));
        std::erase_if(base.Characters,[&](const auto& c){return render_dynamic(c,cache) || (flow && c.Shape=="lava");});
        const auto baseStart = ProfileClock::now();
        cache->partCounts.clear();partCountSink=&cache->partCounts;headerScene=&scene;headerFlow=flow;
        try{cache->packet=render_scene(base,nullptr,{},nullptr,runtimeEffects,gpuGrass);}catch(...){partCountSink=nullptr;headerScene=nullptr;headerFlow=nullptr;throw;}
        partCountSink=nullptr;headerScene=nullptr;headerFlow=nullptr;
        cache->token=++sceneTokens;
        profileBaseMs = profileBaseMs + profileMs(baseStart, ProfileClock::now());
    }
    deltaUsable = deltaOnly;
    const auto partsStart = ProfileClock::now();
    if(!cache)scene.validate();
    struct Draw {
        MeshPart part;
        const Character *actor;
        bool transient=false;
        uint64_t identity=0;
        bool terrainSurface=false;
        const MeshPart *shared=nullptr; // overlay geometry owned by the caller; not copied
    };
    std::vector<std::shared_ptr<const std::vector<MeshPart>>> holders;
    uint64_t effectActors=0;
    std::vector<Draw> draws;
    if (!cache) {
    if (scene.Terrain) {
        auto terrain=*scene.Terrain;if(flow)terrain.WaterEnabled=false;
        for (auto &p : terrain_geometry(terrain))
            draws.push_back({std::move(p), nullptr,false,0,true});
    } else {
        MeshPart ground;
        ground.color = rgb(scene.GroundColor);
        ground.mesh.vertices = {{{-12, 0, -12}, {0, 1, 0}},
                                {{-12, 0, 12}, {0, 1, 0}},
                                {{12, 0, 12}, {0, 1, 0}},
                                {{12, 0, -12}, {0, 1, 0}}};
        ground.mesh.indices = {0, 1, 2, 0, 2, 3};
        draws.push_back({std::move(ground), nullptr});
    }
    }
    for (const auto &c : scene.Characters)
        if((!cache || (render_dynamic(c,cache) && !(deltaOnly && cache->bodiesAsInstances && instance_body(c)))) && !(runtimeEffects && (c.Shape=="chaos-lightning" || c.Shape=="fire" || (c.Shape=="grass"&&!gpuGrass))) && !(flow && c.Shape=="lava"))
        {uint32_t partIndex=0;struct CountParts{uint32_t& index;const std::string& id;~CountParts(){if(partCountSink)(*partCountSink)[id]=index;}} countParts{partIndex,c.Id};
            if(cache&&drives_effect_channels(c))++effectActors;
            holders.push_back(shared_parts(c));
            for (const auto &p : *holders.back()){
                auto identity=render_identity(c.Id,partIndex++);
                if(p.mesh.indices.empty())continue;
                if(gpuGrass&&c.Shape=="grass"){MeshPart tagged=p;for(auto& vertex:tagged.mesh.vertices)vertex.kind=1.36f;draws.push_back({std::move(tagged), &c,false,identity});}
                else draws.push_back({MeshPart{}, &c,false,identity,false,&p});
            }}
    if(deltaOnly&&effectActors){bypass_counters().effectDeltaFrames.fetch_add(1,std::memory_order_relaxed);bypass_counters().effectDeltaActors.fetch_add(effectActors,std::memory_order_relaxed);}
    const auto flowStart = ProfileClock::now();
    if (flow)
        for (auto &p : flow->geometry_for_render())
            draws.push_back({std::move(p), nullptr, true});
    const auto flowEnd = ProfileClock::now();
    for (const auto &part : overlays)
        draws.push_back({MeshPart{}, nullptr, true, 0, false, &part});
    if (trailing)
        for (const auto &part : *trailing)
            draws.push_back({MeshPart{}, nullptr, true, 0, false, &part});
    uint32_t baseDraws=0;
    if(cache)std::memcpy(&baseDraws,cache->packet.data()+4,sizeof(baseDraws));
    if (baseDraws + draws.size() >= 8192)
        throw std::runtime_error("Native draw capacity exceeded");
    if(deltaOnly&&starlight_diag::events_enabled()){
        // Where the per-update vertices come from: scene objects, liquid geometry, overlays (effects, editor), gameplay overlays.
        double objectVerts=0,liquidVerts=0,sharedVerts=0,objectParts=0,biggestObject=0;std::string biggestName;
        for(const auto& draw:draws){
            const auto* mesh=&(draw.shared?draw.shared:&draw.part)->mesh;const double n=double(mesh->vertices.size());
            if(draw.actor){objectVerts+=n;++objectParts;if(n>biggestObject){biggestObject=n;biggestName=draw.actor->Name;}}else if(draw.shared)sharedVerts+=n;else liquidVerts+=n;
        }
        starlight_diag::evlog("dparts",objectVerts,liquidVerts,sharedVerts);
        static double lastBiggest=-1;if(biggestObject!=lastBiggest){lastBiggest=biggestObject;std::fprintf(stderr,"delta objects: %g parts, %g vertices, biggest %s (%g)%c",objectParts,objectVerts,biggestName.c_str(),biggestObject,10);}
    }
    const auto serializeStart = ProfileClock::now();
    Writer w;
    // Reserve once: changing attack meshes otherwise grow this byte buffer
    // repeatedly on the render thread. This is capacity only, not a format change.
    size_t packetCapacity = 256 + scene.Characters.size() * 64;
    for (const auto& draw : draws)
        packetCapacity += 160 + (draw.actor ? draw.actor->Route.size() * 8 : 0)
                        + (draw.shared ? draw.shared : &draw.part)->mesh.vertices.size() * 48
                        + (draw.shared ? draw.shared : &draw.part)->mesh.indices.size() * 4;
    w.bytes.reserve(packetCapacity + (cache && !deltaOnly ? cache->packet.size() : 0));
    if(deltaOnly){
        w.put(uint32_t(0x43564c53));
        w.put(uint64_t(cache->token));
        w.put(uint32_t(draws.size()));
    } else if(cache){
        w.bytes.insert(w.bytes.end(),cache->packet.begin(),cache->packet.end());
        uint32_t total=baseDraws+uint32_t(draws.size());
        std::memcpy(w.bytes.data()+4,&total,sizeof(total));
    } else {
    w.put(uint32_t(0x42564c53));
    w.put(uint32_t(draws.size()));
    // Use the same traced source positions and light budget as live playback.
    const SceneData& lightScene=headerScene?*headerScene:scene;
    const TerrainFlow* lightFlow=headerScene?headerFlow:flow;
    auto lights=environment_lights(lightScene,0,lightFlow);
    w.put(uint32_t(lights.size()));
    for(const auto& light:lights)for(float value:light)w.f(value);
    w.f(scene.Hour);
    w.f(scene.DayCycle ? 1 : 0);
    w.f(scene.DayLength);
    w.f(scene.FogDensity);
    auto emitters=fog_emitters(lightScene,0,lightFlow);
    w.put(uint32_t(emitters.size()));
    for(auto source:emitters)for(float value:source)w.f(value);
    w.put(int32_t(scene.RaytracingEnabled ? scene.RaytracingQuality : 0));
    w.put(int32_t(scene.PathBounces));
    w.put(int32_t(scene.FreezeRaytracingAnimation));
    w.put(int32_t(scene.RayReconstructionEnabled));
    }
    // world_point depends only on these fields. A scratch Character avoids copying the whole object (strings,
    // routes, sculpt meshes) and generating a fresh GUID for every draw; results are bit-identical.
    Character transform;
    for (const auto &draw : draws) {
        const auto *c = draw.actor;
        w.put(draw.identity);
        w.put(uint32_t(draw.transient));
        auto kind = c ? c->Shape.value_or("") : "";
        w.f((draw.shared ? draw.shared : &draw.part)->emissive ? 1 : 0);
        w.put(int32_t(kind == "craxis-smoke" ? 4 : kind == "fire" ? 1 : kind == "lava" ? 2 : 0));
        w.f(c ? c->Y : 0);
        w.vec((draw.shared ? draw.shared : &draw.part)->color);
        w.f(c ? c->X : 0);
        w.f(c ? c->Z : 0);
        w.f(c ? c->Size : 1);
        w.f(c ? c->Speed : 0);
        w.put(uint32_t(c && !c->Dynamic ? c->Route.size() : 0));
        if (c && !c->Dynamic)
            for (auto p : c->Route) {
                w.f(p.X);
                w.f(p.Z);
            }
        if (c) {
            transform.Width = c->Width; transform.Height = c->Height; transform.Depth = c->Depth;
            transform.PhysicsRotation = c->PhysicsRotation;
            transform.RotationX = c->RotationX; transform.RotationY = c->RotationY; transform.RotationZ = c->RotationZ;
            transform.X = transform.Z = 0;
            transform.Y = c->Y / c->Size;
            transform.Size = 1;
        } else {
            transform.Width = transform.Height = transform.Depth = 1;
            transform.PhysicsRotation.reset();
            transform.RotationX = transform.RotationY = transform.RotationZ = 0;
            transform.X = transform.Y = transform.Z = 0;
            transform.Size = 1;
        }
        auto origin = world_point(transform, {});
        for (auto basis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
            w.vec(world_point(transform, basis) - origin);
            w.f(0);
        }
        w.vec(origin);
        w.f(1);
        const auto &mesh = (draw.shared ? draw.shared : &draw.part)->mesh;
        mesh.validate();
        w.put(uint32_t(mesh.vertices.size()));
        {
            const bool rtSurface = c && scene.RaytracingEnabled && (c->Surface == 12 || c->Surface == 13);
            const size_t vertexBase = w.bytes.size();
            w.bytes.resize(vertexBase + mesh.vertices.size() * 48);
            uint8_t *cursor = w.bytes.data() + vertexBase;
            auto emit = [&](double value) {
                if (!std::isfinite(value) || std::abs(value) > FLT_MAX)
                    throw std::runtime_error("Render value outside float range");
                const float narrow = float(value);
                std::memcpy(cursor, &narrow, 4);
                cursor += 4;
            };
            for (const auto &v : mesh.vertices) {
                emit(v.position.x); emit(v.position.y); emit(v.position.z);
                const Vec3 normal = v.normal.dot(v.normal) > 1e-12 ? v.normal : Vec3{0, 1, 0};
                emit(normal.x); emit(normal.y); emit(normal.z);
                const Vec3 tint = rtSurface ? Vec3{1, 1, 1} : v.color;
                emit(tint.x); emit(tint.y); emit(tint.z);
                // Terrain paint is a continuous blend; it must never alias the
                // discrete grass-blade tags (1.35/1.36/1.37) during interpolation.
                // Keep the CPU mesh/scene format unchanged and tag only the GPU packet.
                emit(rtSurface ? 0.f : c && c->Surface == 12 && v.kind == 12 ? -40.f : c && c->Surface == 13 && v.kind == 13 ? -41.f : draw.terrainSurface && v.kind > .5f && v.kind < 6.8f ? v.kind - 32.f : v.kind);
                std::memcpy(cursor, &v.motionIdentity, 8);
                cursor += 8;
            }
        }
        w.put(uint32_t(mesh.indices.size()));
        {
            // Indices were range-checked by validate(), so int -> uint32_t is a straight copy.
            static_assert(sizeof(int) == sizeof(uint32_t));
            const size_t indexBase = w.bytes.size();
            w.bytes.resize(indexBase + mesh.indices.size() * 4);
            if (!mesh.indices.empty())
                std::memcpy(w.bytes.data() + indexBase, mesh.indices.data(), mesh.indices.size() * 4);
        }
    }
    const auto done = ProfileClock::now();
    profilePartsMs = profilePartsMs + profileMs(partsStart, flowStart);
    profileFlowMs = profileFlowMs + profileMs(flowStart, flowEnd);
    profileSerializeMs = profileSerializeMs + profileMs(serializeStart, done);
    ++profileCalls;
    if (deltaOnly) ++profileDeltaCalls;
    profileBytes += w.bytes.size();
    return std::move(w.bytes);
}
std::vector<uint8_t> render_scene(const SceneData &scene, const TerrainFlow *flow,
                                  const std::vector<MeshPart> &overlays, RenderSceneCache *cache, bool runtimeEffects,bool gpuGrass,const std::vector<MeshPart> *trailing) {
    bool usable = false;
    return render_scene_impl(false, usable, scene, flow, overlays, cache, runtimeEffects, gpuGrass, trailing);
}
bool render_scene_delta(const SceneData &scene, const TerrainFlow *flow, const std::vector<MeshPart> &overlays,
                        RenderSceneCache &cache, bool runtimeEffects, bool gpuGrass, std::vector<uint8_t> &delta,
                        const std::vector<MeshPart> *trailing) {
    bool usable = false;
    delta = render_scene_impl(true, usable, scene, flow, overlays, &cache, runtimeEffects, gpuGrass, trailing);
    return usable && cache.token != 0 && !cache.packet.empty();
}
std::vector<uint8_t> serialize_resident_mesh(const MeshPart &part, uint64_t key) {
    const auto &mesh = part.mesh;
    mesh.validate();
    Writer w;
    w.bytes.reserve(32 + mesh.vertices.size() * 48 + mesh.indices.size() * 4);
    w.put(uint32_t(0x44564c53));
    w.put(key);
    w.put(uint32_t(mesh.vertices.size()));
    const size_t base = w.bytes.size();
    w.bytes.resize(base + mesh.vertices.size() * 48);
    uint8_t *cursor = w.bytes.data() + base;
    auto emit = [&](double value) {
        if (!std::isfinite(value) || std::abs(value) > FLT_MAX)
            throw std::runtime_error("Render value outside float range");
        const float narrow = float(value);
        std::memcpy(cursor, &narrow, 4);
        cursor += 4;
    };
    for (const auto &v : mesh.vertices) {
        emit(v.position.x); emit(v.position.y); emit(v.position.z);
        const Vec3 normal = v.normal.dot(v.normal) > 1e-12 ? v.normal : Vec3{0, 1, 0};
        emit(normal.x); emit(normal.y); emit(normal.z);
        emit(v.color.x); emit(v.color.y); emit(v.color.z);
        emit(v.kind);
        const uint64_t none = 0; // rigid instances use model-matrix history, not per-vertex history
        std::memcpy(cursor, &none, 8);
        cursor += 8;
    }
    w.put(uint32_t(mesh.indices.size()));
    const size_t indexBase = w.bytes.size();
    w.bytes.resize(indexBase + mesh.indices.size() * 4);
    if (!mesh.indices.empty())
        std::memcpy(w.bytes.data() + indexBase, mesh.indices.data(), mesh.indices.size() * 4);
    return std::move(w.bytes);
}
RenderSceneProfile render_scene_profile() {
    const auto& counters=bypass_counters();
    return {profileFlowMs.load(), profilePartsMs.load(), profileSerializeMs.load(), profileBaseMs.load(),
            profileCalls.load(), profileDeltaCalls.load(), profileBytes.load(),
            counters.effectDeltaFrames.load(), counters.effectDeltaActors.load(), counters.effectRepublish.load(), render_geometry_stats()};
}
} // namespace starlight::editor
