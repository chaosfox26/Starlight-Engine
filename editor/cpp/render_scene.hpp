#pragma once
#include "assets.hpp"
#include "flow.hpp"
#include "repair_switches.hpp"
#include <atomic>
#include <intrin.h>
#include <cstdint>
namespace starlight::editor {
inline uint64_t render_identity(std::string_view id,uint32_t part){uint64_t hash=1469598103934665603ull;for(unsigned char c:id){hash^=c;hash*=1099511628211ull;}for(int i=0;i<4;++i){hash^=(part>>(i*8))&255;hash*=1099511628211ull;}return hash?hash:1;}
enum EffectChannel : unsigned {EffectGeometry=1,EffectLight=2,EffectEmitter=4,EffectShadow=8,EffectEmissive=16,EffectRuntime=32,EffectPassable=64};
inline unsigned effect_channels(const std::optional<std::string>& shape){
    if(!shape)return 0;
    const std::string& k=*shape;
    if(k=="fire")return EffectGeometry|EffectLight|EffectEmitter|EffectRuntime|EffectPassable;
    if(k=="light")return EffectGeometry|EffectLight|EffectEmissive|EffectPassable;
    if(k=="chaos-rock")return EffectGeometry|EffectLight|EffectShadow;
    if(k=="light-shard")return EffectGeometry|EffectLight|EffectShadow|EffectPassable;
    if(k=="chaos-lightning")return EffectGeometry|EffectLight|EffectEmissive|EffectRuntime|EffectPassable;
    if(k=="craxis-smoke")return EffectGeometry|EffectEmitter|EffectPassable;
    if(k=="lava")return EffectGeometry|EffectPassable;
    if(k=="grass")return EffectGeometry|EffectShadow|EffectRuntime|EffectPassable;
    return 0;
}
inline bool is_effect_shape(const std::optional<std::string>& shape){return effect_channels(shape)!=0;}
inline bool drives_effect_channels(const Character& c){return (effect_channels(c.Shape)&(EffectLight|EffectEmitter|EffectRuntime))!=0;}
inline bool feeds_light_or_emitter_channel(const Character& c){return (effect_channels(c.Shape)&(EffectLight|EffectEmitter))!=0;}
inline uint64_t channel_signature(const void* data,size_t bytes){uint64_t hash=1469598103934665603ull;auto at=static_cast<const unsigned char*>(data);for(size_t i=0;i<bytes;++i){hash^=at[i];hash*=1099511628211ull;}return hash;}
inline uint64_t resident_part_identity(uint64_t bodyKey,size_t index){uint64_t hash=bodyKey^((uint64_t(index)+1)*0x9E3779B97F4A7C15ull);hash*=1099511628211ull;hash^=hash>>29;return hash|(1ull<<63);}
// Objects that move on their own every update (physics bodies, combat actors) are drawn in the per-update delta, never in the
// resident static base, so their motion or turning does not rebuild the static scene.
// Bypass detector: counts how often an object that does not move on its own (and is not a trivial cube) has its bounds or collision mesh
// computed OUTSIDE the shared ObjectCache. A steady-state rate above zero means some path re-does work the cache already holds.
struct BypassCounters {
    std::atomic<uint64_t> staticBounds{0},staticMeshes{0};
    std::atomic<uint64_t> effectDeltaFrames{0},effectDeltaActors{0},effectRepublish{0},lightChannelFrames{0},lightChannelChanges{0},emitterChannelChanges{0};
    // Optional attribution (STARLIGHT_BYPASS_TRACE=1): how often each caller (return address) bypassed the cache.
    struct Site {std::atomic<uintptr_t> address{0};std::atomic<uint64_t> count{0};std::atomic<int> kind{0};};
    Site sites[256];
    void record(const void* caller,int kind){
        uintptr_t a=reinterpret_cast<uintptr_t>(caller);size_t i=(a>>4)*2654435761u%256;
        for(size_t probe=0;probe<256;++probe,i=(i+1)%256){
            uintptr_t expected=0;
            if(sites[i].address.load(std::memory_order_relaxed)==a||sites[i].address.compare_exchange_strong(expected,a)||expected==a){sites[i].count.fetch_add(1,std::memory_order_relaxed);sites[i].kind.store(kind,std::memory_order_relaxed);return;}
        }
    }
};
inline bool effect_split_enabled(){static const bool on=[]{char v[8]{};return !(GetEnvironmentVariableA("STARLIGHT_EFFECT_SPLIT",v,8)&&v[0]=='0');}();return on;}
inline bool bypass_trace(){static const bool on=[]{char v[8]{};return GetEnvironmentVariableA("STARLIGHT_BYPASS_TRACE",v,8)&&v[0]=='1';}();return on;}
inline BypassCounters& bypass_counters(){static BypassCounters counters;return counters;}
inline bool& in_object_cache(){thread_local bool inside=false;return inside;}
inline bool counts_as_static_bypass(const Character& c){return c.Route.empty()&&(c.Sculpt||!(c.Shape&&*c.Shape=="cube"));}
// A physics body that is not moving is drawn from the resident static scene like any other scenery: the editor marks every rock and boulder
// Dynamic by default, and re-sending the geometry of each resting one (sculpted models are tens of thousands of vertices) on every update
// made every such scene slow. `resting` holds the bodies currently in the resident base with the pose they were drawn at; the moment one
// is found elsewhere (see promote_moved in native_session.cpp) it leaves the base and is drawn in the per-update delta from then on.
inline bool resting_eligible(const Character& c){
    if(!c.Dynamic||c.JointKind!=0||!c.JointRuntimePoints.empty()||!c.Route.empty()||c.CombatHealth>0)return false;
    if(!c.Shape&&!c.Sculpt&&c.Asset.empty())return false;
    if(is_effect_shape(c.Shape))return false;
    return true;
}
// Where the object's local axes and origin are in the world; equal poses give equal values whatever way the rotation is stored.
std::array<Vec3,4> pose_probe(const Character& c);
// Two poses are the same only when every probe point is unchanged to floating-point noise (1e-7 m). There is no visual tolerance: a body that
// has moved by any visible amount is drawn where it really is.
inline bool same_pose(const std::array<Vec3,4>& a,const std::array<Vec3,4>& b,double tolerance=1e-7){
    for(int k=0;k<4;++k)if(std::abs(a[k].x-b[k].x)>tolerance||std::abs(a[k].y-b[k].y)>tolerance||std::abs(a[k].z-b[k].z)>tolerance)return false;
    return true;
}
// Owned by one render session; invalidate whenever its scene geometry changes.
struct RenderSceneCache { std::vector<uint8_t> packet; bool runtimeEffects=false,simulatedLiquids=false,gpuGrass=false; uint64_t token=0; bool bodiesAsInstances=false; /* moving physics bodies are drawn as resident instances (geometry once, pose per update), not as per-update meshes */ bool allowResting=false; /* set once physics bodies have come to rest; before that they are drawn per update */ void clear(){packet.clear();removedIdentities.clear();partCounts.clear();resting.clear();} std::unordered_map<std::string,std::array<Vec3,4>> resting; /* resting physics bodies in the resident base and the pose they were drawn at */ std::unordered_map<std::string,uint32_t> partCounts; /* parts each static character produced in the resident base */ std::vector<uint64_t> removedIdentities; /* parts removed from the resident base since it was built */ };
// A physics body that can be drawn as a resident instance: its geometry is uploaded once and only its pose is sent each update.
inline bool instance_body(const Character& c){return c.Dynamic&&resting_eligible(c);}
// The body's geometry with its scale applied, in its own space; shared by every body with the same geometry. Drawn with a position and a turn.
std::vector<std::shared_ptr<const MeshPart>> resident_body_parts(const Character&);
struct ResidentBody {uint64_t key=0;std::vector<std::shared_ptr<const MeshPart>> parts;uint64_t part_identity(size_t index)const{return resident_part_identity(key,index);}};
uint64_t render_geometry_key(const Character&);
std::shared_ptr<const ResidentBody> resident_body(const Character&);
std::shared_ptr<const std::vector<MeshPart>> shared_parts(const Character&);
struct RenderGeometryStats {uint64_t partHits=0,partMisses=0,partEvicted=0,partPinnedSkips=0,bodyHits=0,bodyMisses=0,bodyEvicted=0,bodyPinnedSkips=0;uint64_t partEntries=0,partBytes=0,bodyEntries=0,bodyBytes=0;};
RenderGeometryStats render_geometry_stats();
// Drawn in the per-update delta rather than the resident base: bodies that move, and combat actors.
inline bool render_dynamic(const Character& c,const RenderSceneCache* cache){
    if(repair_delta()&&c.CombatHealth>0)return true;
    if(!c.Dynamic)return false;
    return !(cache&&cache->resting.count(c.Id));
}
std::vector<uint8_t> render_scene(const SceneData &, const TerrainFlow *flow = nullptr,
                                  const std::vector<MeshPart> &overlays = {}, RenderSceneCache *cache = nullptr,
                                  bool runtimeEffects = false,bool gpuGrass=false,
                                  const std::vector<MeshPart> *trailing = nullptr);
// Builds only the draws that change between updates (dynamic bodies, liquid surfaces, overlays). The
// static scene stays in cache.packet, installed once under cache.token, and is not copied again.
// Returns false when the scene cannot keep a resident base; callers then use render_scene().
bool render_scene_delta(const SceneData &, const TerrainFlow *flow, const std::vector<MeshPart> &overlays,
                        RenderSceneCache &cache, bool runtimeEffects, bool gpuGrass, std::vector<uint8_t> &delta,
                        const std::vector<MeshPart> *trailing = nullptr);
struct RenderSceneProfile { double flowGeometryMs=0,partsMs=0,serializeMs=0,baseMs=0; uint64_t calls=0,deltaCalls=0,bytes=0,effectDeltaFrames=0,effectDeltaActors=0,effectRepublish=0; RenderGeometryStats geometry; };
RenderSceneProfile render_scene_profile();
// One rigid mesh for the viewport's resident-instance path: vertices are written exactly like scene-packet vertices
// (including the GPU kind tag), but the mesh is uploaded once and then drawn with a per-frame pose.
std::vector<uint8_t> serialize_resident_mesh(const MeshPart &, uint64_t key);
std::vector<MeshPart> object_parts(const Character &);
Mesh object_mesh(const Character &);
} // namespace starlight::editor
