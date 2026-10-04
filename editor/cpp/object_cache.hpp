#pragma once
// The engine's single shared cache of prepared per-object information, and its query interface.
//
// Anything that needs an object's world bounds or collision mesh asks here instead of recomputing them: gameplay (line of sight,
// turning, projectiles, pursuit, dust, camera), the environment effects, the render session (solid boxes, liquid obstacle mask) and the
// collision world. Scenes need no preparation: an entry is created on first use and every later lookup is a hash probe plus two cheap keys.
//
// Two things are cached separately, because they change for different reasons:
//   * GEOMETRY  - the object's own-space mesh. Keyed by what generates the geometry: shape or model asset (its content id) or
//                 sculpt (with its content revision), the generator parameters, and the scale. Shared by every object that has the same
//                 key, however many there are and wherever they stand. Moving, turning or physics never rebuilds it.
//   * INSTANCE  - one object's world bounds. Keyed by its id and checked against the geometry key and its transform key (position,
//                 rotation, physics rotation). A moved object recomputes its bounds from the shared geometry, not from scratch.
//
// Correctness: every lookup recomputes both keys, so edits, replaced models (a different content id), quest actions or any other change refresh the entry
// by themselves; nothing is invalidated by hand. Objects that move on their own (route) and combat actors are not remembered by bounds()
// (see bounds_cacheable); they are measured from the same shared geometry at their pose with bounds_at(), which copies nothing and rebuilds
// no mesh. The cache is shared by all threads; readers do not block each other.
//
// Memory: entries are evicted a little at a time, least recently used first, once the entry count or the geometry byte budget (meshes and
// ray trees together) is exceeded; handles that callers still hold stay valid. A scene pins what it prepared (pin/unpin) and pinned entries are
// never evicted. There is no wholesale purge.
//
// STARLIGHT_CACHE_VERIFY=1 re-measures a sample of cache hits from scratch and stops with an error if the cache ever returns a stale
// answer (this is how an edit path that forgets SculptMesh::touch() would be caught).
#include "interactions.hpp"
#include "render_scene.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace starlight::editor {
namespace cache_detail {
inline uint64_t mix(uint64_t h,uint64_t v){h^=v;h*=1099511628211ull;return h;}
inline uint64_t bits(double v){uint64_t b;std::memcpy(&b,&v,8);return b;}
}
// What generates the object's own-space geometry (including its scale).
inline uint64_t geometry_key(const Character& c){
    using namespace cache_detail;
    uint64_t h=1469598103934665603ull;
    for(double v:{c.Size,c.Width,c.Height,c.Depth,c.BranchSpread,c.TargetX,c.TargetY,c.TargetZ})h=mix(h,bits(v));
    h=mix(h,std::hash<std::string>{}(c.Shape.value_or("")));h=mix(h,uint64_t(c.Surface));
    if(c.Glow>0)h=mix(h,0x676c6f77ull);
    if(c.Shape&&*c.Shape=="ring")h=mix(h,bits(c.RingInner));
    if(!c.Asset.empty())h=mix(h,std::hash<std::string>{}(c.Asset));
    if(c.Sculpt){h=mix(h,c.Sculpt->content_revision());h=mix(h,c.Sculpt->Positions.size());h=mix(h,c.Sculpt->Indices.size());}
    return h;
}
// Where and how the object is turned in the world.
inline uint64_t transform_key(const Character& c){
    using namespace cache_detail;
    uint64_t h=1469598103934665603ull;
    for(double v:{c.X,c.Y,c.Z,c.RotationX,c.RotationY,c.RotationZ})h=mix(h,bits(v));
    if(c.PhysicsRotation){h=mix(h,1);for(double v:*c.PhysicsRotation)h=mix(h,bits(v));}else h=mix(h,0);
    return h;
}
inline uint64_t pose_signature(const Character& c){return cache_detail::mix(geometry_key(c),transform_key(c));}
inline uint64_t pose_transform_key(const ObjectPose& p){
    using namespace cache_detail;
    uint64_t h=1469598103934665603ull;
    for(double v:{p.X,p.Y,p.Z,p.RotationX,p.RotationY,p.RotationZ})h=mix(h,bits(v));
    if(p.PhysicsRotation){h=mix(h,1);for(double v:*p.PhysicsRotation)h=mix(h,bits(v));}else h=mix(h,0);
    return h;
}
// Everything that decides what a break of this object looks like, except where it stands: the geometry key (shape or model asset id,
// sculpt content, every generator parameter and the scale) plus the look the pieces inherit (colour, foliage material).
inline uint64_t fracture_source_key(const Character& c){
    using namespace cache_detail;
    uint64_t h=geometry_key(c);
    h=mix(h,std::hash<std::string>{}(c.Color));
    if(c.Foliage){h=mix(h,1);for(double v:{c.Foliage->BarkScale,c.Foliage->BarkRelief,c.Foliage->BarkRoughness,c.Foliage->LeafTransmission,c.Foliage->LeafVeins,c.Foliage->LeafRoughness})h=mix(h,bits(v));}else h=mix(h,0);
    return h;
}
// Work that was supposed to happen while the scene loaded but happened while it was running. Every counter should stay at zero in normal play; a
// non-zero value names the path that did the work late.
struct RuntimeBypass {
    std::atomic<uint64_t> fractureComputedAtBreak{0},fractureNoEntry{0},fractureStaleSource{0},fractureNotStarted{0},fractureWaited{0},fractureFailedEntry{0},hullBuiltAtBreak{0},spatialIndexBuiltLate{0};
    void reset(){fractureComputedAtBreak=fractureNoEntry=fractureStaleSource=fractureNotStarted=fractureWaited=fractureFailedEntry=hullBuiltAtBreak=spatialIndexBuiltLate=0;}
};
inline RuntimeBypass& runtime_bypass(){static RuntimeBypass counters;return counters;}
// Shared classification: does this object physically block movement? Grass, lights, fire, smoke, lightning, lava and collectable/exit
// markers never do (the player controller already ignores them); enemy movement, path search and turning must agree.
inline bool blocks_movement(const Character& c){
    if(c.GameplayRole=="key"||c.GameplayRole=="exit")return false;
    const auto& k=c.Shape;
    if(!k)return true;
    return !(*k=="grass"||*k=="fire"||*k=="light"||*k=="lava"||*k=="chaos-lightning"||*k=="light-shard"||*k=="craxis-smoke");
}
// Anything whose bounds are a pure function of its own pose may be cached, because every lookup checks that pose. That includes rocks and
// boulders the editor marks Dynamic: while they rest, nothing changes and they are served from the cache; the moment physics moves one, its
// transform key changes and the next lookup re-measures it from the shared geometry. Plain cubes that move are cheaper to compute than to look up.
inline bool bounds_cacheable(const Character& c){return c.Route.empty()&&(!(c.Dynamic||c.PhysicsRotation)||c.Sculpt||!(c.Shape&&*c.Shape=="cube"));}
class ObjectCache {
  public:
    static ObjectCache& shared(){static ObjectCache cache;return cache;}
    using PinOwner=uint64_t;
    // Posed world bounds of an object that does not move on its own (see bounds_cacheable).
    Bounds bounds(const Character& c){
        const uint64_t geometryKey=geometry_key(c),transform=transform_key(c);
        Bounds result;
        if(lookup(c,geometryKey,transform,result))return result;
        misses_.fetch_add(1,std::memory_order_relaxed);
        struct Scope{Scope(){in_object_cache()=true;}~Scope(){in_object_cache()=false;}} scope;
        if(c.Shape&&*c.Shape=="cube"&&!c.Sculpt)result=object_bounds_raw(c); // plain cubes have a closed-form fast path
        else{
            const auto geometry=geometry_for(c,geometryKey);
            for(const auto& v:geometry->mesh->vertices){
                auto p=world_point(c,v.position);
                result.low={std::min(result.low.x,p.x),std::min(result.low.y,p.y),std::min(result.low.z,p.z)};
                result.high={std::max(result.high.x,p.x),std::max(result.high.y,p.y),std::max(result.high.z,p.z)};
            }
        }
        store(c.Id,geometryKey,transform,result);
        return result;
    }
    // Bounds of object `c` standing at `pose` (a route position, an actor's current spot, a hypothetical turn). The geometry is the shared one for
    // c's geometry key, so no mesh is rebuilt and no Character is copied; the answer is the same as measuring c with that pose from scratch. With
    // `remember` the answer is kept under c's id like bounds(); a probe of a pose the object does not take (a trial turn) passes false.
    Bounds bounds_at(const Character& c,const ObjectPose& pose,bool remember=true){
        const uint64_t geometryKey=geometry_key(c),transform=pose_transform_key(pose);
        Bounds result;
        if(remember&&lookup(c,geometryKey,transform,result))return result;
        misses_.fetch_add(1,std::memory_order_relaxed);
        const auto geometry=geometry_for(c,geometryKey);
        for(const auto& v:geometry->mesh->vertices){
            auto p=world_point(pose,v.position);
            result.low={std::min(result.low.x,p.x),std::min(result.low.y,p.y),std::min(result.low.z,p.z)};
            result.high={std::max(result.high.x,p.x),std::max(result.high.y,p.y),std::max(result.high.z,p.z)};
        }
        if(remember)store(c.Id,geometryKey,transform,result);
        return result;
    }
    // Collision mesh in the object's own space (object_mesh) for an object that does not move on its own. Shared by every object with
    // the same geometry key.
    std::shared_ptr<const Mesh> mesh(const Character& c){
        return geometry_for(c,geometry_key(c))->mesh;
    }
    // Nearest hit of a ray against the object's actual surface (the object's own pose is `posed`; `c` identifies its geometry). The bounding-volume tree
    // is shared by every object of that shape. It is normally built during preparation (warm_query_geometry); a tree first built while a query is
    // waiting is counted as a late build.
    std::optional<MeshHit> raycast(const Character& c,const ObjectPose& posed,Vec3 origin,Vec3 direction){
        const uint64_t key=geometry_key(c);
        auto geometry=geometry_for(c,key);
        ensure_tree(geometry,key);
        return mesh_hit(*geometry->mesh,*geometry->accel,posed,origin,direction);
    }
    std::optional<MeshHit> raycast(const Character& c,const Character& posed,Vec3 origin,Vec3 direction){
        return raycast(c,object_pose(posed),origin,direction);
    }
    // Builds (once) the shared collision mesh and ray tree of c's geometry and returns what they cost. Safe on any thread.
    struct Warmed {size_t meshBytes=0,treeBytes=0;bool builtTree=false;};
    Warmed warm_query_geometry(const Character& c){
        const uint64_t key=geometry_key(c);
        auto geometry=geometry_for(c,key);
        Warmed warmed;
        warmed.builtTree=ensure_tree(geometry,key);
        warmed.meshBytes=geometry->meshBytes;
        warmed.treeBytes=geometry->treeBytes.load(std::memory_order_relaxed);
        return warmed;
    }
    // Pins keep a scene's prepared geometry (mesh, ray tree, bounds entries) out of eviction until the owner releases them, so memory pressure from
    // another scene can never push a prepared resource out and force a rebuild during play.
    PinOwner new_pin_owner(){return ++pinOwners_;}
    void pin(PinOwner owner,const Character& c,bool withBounds){
        const uint64_t key=geometry_key(c);
        std::unique_lock lock(mutex_);
        ++geometryPins_[key];ownerGeometry_[owner].push_back(key);
        if(withBounds){++instancePins_[c.Id];ownerInstances_[owner].push_back(c.Id);}
    }
    void unpin(PinOwner owner){
        std::unique_lock lock(mutex_);
        if(auto found=ownerGeometry_.find(owner);found!=ownerGeometry_.end()){
            for(uint64_t key:found->second)if(auto pin=geometryPins_.find(key);pin!=geometryPins_.end()&&--pin->second<=0)geometryPins_.erase(pin);
            ownerGeometry_.erase(found);
        }
        if(auto found=ownerInstances_.find(owner);found!=ownerInstances_.end()){
            for(const auto& id:found->second)if(auto pin=instancePins_.find(id);pin!=instancePins_.end()&&--pin->second<=0)instancePins_.erase(pin);
            ownerInstances_.erase(found);
        }
    }
    // Everything built after mark_prepared(), while play is running, bypassed preparation and is counted (Stats::lateGeometryBuilds / lateRayTreeBuilds).
    void begin_preparation(){prepared_.store(false);}
    void mark_prepared(){lateGeometry_.store(0);lateTrees_.store(0);prepared_.store(true);}
    struct Stats {uint64_t hits=0,misses=0,meshHits=0,meshMisses=0,evictedInstances=0,evictedGeometries=0;size_t entries=0,geometries=0,geometryBytes=0;
        uint64_t rayTreeBuilds=0,lateGeometryBuilds=0,lateRayTreeBuilds=0;size_t rayTrees=0,rayTreeBytes=0,pinnedGeometries=0,pinnedBytes=0;bool overBudget=false;};
    Stats stats()const{
        std::shared_lock lock(mutex_);
        Stats s{hits_.load(),misses_.load(),meshHits_.load(),meshMisses_.load(),evictedInstances_.load(),evictedGeometries_.load(),instances_.size(),geometries_.size(),geometryBytes_};
        s.rayTreeBuilds=treeBuilds_.load();s.lateGeometryBuilds=lateGeometry_.load();s.lateRayTreeBuilds=lateTrees_.load();
        for(const auto& [key,geometry]:geometries_){const size_t trees=geometry->treeBytes.load();if(trees){++s.rayTrees;s.rayTreeBytes+=trees;}}
        for(const auto& [key,count]:geometryPins_)if(auto found=geometries_.find(key);found!=geometries_.end()){++s.pinnedGeometries;s.pinnedBytes+=found->second->bytes;}
        s.overBudget=geometryBytes_>kMaxGeometryBytes;
        return s;
    }
    void clear(){std::unique_lock lock(mutex_);instances_.clear();geometries_.clear();geometryBytes_=0;geometryPins_.clear();instancePins_.clear();ownerGeometry_.clear();ownerInstances_.clear();}
  private:
    struct Geometry {std::shared_ptr<const Mesh> mesh;size_t bytes=0,meshBytes=0;std::atomic<size_t> treeBytes{0};std::atomic<uint64_t> used{0};std::shared_ptr<const MeshAccel> accel;std::once_flag accelFlag;};
    struct Instance {uint64_t geometry=0,transform=0;Bounds bounds{};std::atomic<uint64_t> used{0};};
    static constexpr size_t kMaxInstances=65536;
    static constexpr size_t kMaxGeometryBytes=768ull<<20;
    bool lookup(const Character& c,uint64_t geometryKey,uint64_t transform,Bounds& out){
        std::shared_lock lock(mutex_);
        auto found=instances_.find(c.Id);
        if(found==instances_.end()||found->second->geometry!=geometryKey||found->second->transform!=transform)return false;
        found->second->used.store(++clock_,std::memory_order_relaxed);
        hits_.fetch_add(1,std::memory_order_relaxed);
        out=found->second->bounds;
        lock.unlock();
        if(verify_&&(clock_.load(std::memory_order_relaxed)&63)==0)check_bounds(c,out);
        return true;
    }
    void store(const std::string& id,uint64_t geometryKey,uint64_t transform,const Bounds& result){
        std::unique_lock lock(mutex_);
        auto& slot=instances_[id];
        if(!slot)slot=std::make_unique<Instance>();
        slot->geometry=geometryKey;slot->transform=transform;slot->bounds=result;slot->used.store(++clock_,std::memory_order_relaxed);
        if(instances_.size()>kMaxInstances)evict_instances(id);
    }
    // Builds the geometry's ray tree on first need and charges its memory to the geometry budget. Returns true when this call built it.
    bool ensure_tree(const std::shared_ptr<Geometry>& geometry,uint64_t key){
        bool built=false;
        std::call_once(geometry->accelFlag,[&]{
            geometry->accel=build_mesh_accel(*geometry->mesh);
            geometry->treeBytes.store(mesh_accel_bytes(*geometry->accel),std::memory_order_relaxed);
            built=true;
        });
        if(!built)return false;
        treeBuilds_.fetch_add(1,std::memory_order_relaxed);
        if(prepared_.load(std::memory_order_relaxed))lateTrees_.fetch_add(1,std::memory_order_relaxed);
        std::unique_lock lock(mutex_);
        if(auto found=geometries_.find(key);found!=geometries_.end()&&found->second.get()==geometry.get()){
            const size_t trees=geometry->treeBytes.load(std::memory_order_relaxed);
            geometry->bytes+=trees;geometryBytes_+=trees;
            if(geometryBytes_>kMaxGeometryBytes)evict_geometries(key);
        }
        return true;
    }
    std::shared_ptr<Geometry> geometry_for(const Character& c,uint64_t key){
        {
            std::shared_lock lock(mutex_);
            auto found=geometries_.find(key);
            if(found!=geometries_.end()){
                found->second->used.store(++clock_,std::memory_order_relaxed);
                meshHits_.fetch_add(1,std::memory_order_relaxed);
                auto geometry=found->second;
                lock.unlock();
                if(verify_&&(clock_.load(std::memory_order_relaxed)&255)==0)check_geometry(c,*geometry);
                return geometry;
            }
        }
        meshMisses_.fetch_add(1,std::memory_order_relaxed);
        if(prepared_.load(std::memory_order_relaxed))lateGeometry_.fetch_add(1,std::memory_order_relaxed);
        struct Scope{Scope(){in_object_cache()=true;}~Scope(){in_object_cache()=false;}} scope;
        auto built=std::make_shared<Geometry>();
        built->mesh=std::make_shared<const Mesh>(object_mesh(c));
        built->meshBytes=built->bytes=built->mesh->vertices.size()*sizeof(built->mesh->vertices[0])+built->mesh->indices.size()*sizeof(int);
        std::unique_lock lock(mutex_);
        auto [slot,inserted]=geometries_.try_emplace(key,built);
        if(!inserted)return slot->second; // another thread built the same geometry first
        built->used.store(++clock_,std::memory_order_relaxed);geometryBytes_+=built->bytes;
        if(geometryBytes_>kMaxGeometryBytes)evict_geometries(key);
        return built;
    }
    // Drop the least recently used eighth of the instances (never the one just stored, never a pinned one).
    void evict_instances(const std::string& keep){
        std::vector<std::pair<uint64_t,const std::string*>> order;order.reserve(instances_.size());
        for(const auto& [id,instance]:instances_)if(id!=keep&&!instancePins_.contains(id))order.emplace_back(instance->used.load(std::memory_order_relaxed),&id);
        if(order.empty())return;
        const size_t drop=std::min(order.size(),std::max<size_t>(1,order.size()/8));
        std::nth_element(order.begin(),order.begin()+(drop-1),order.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        std::vector<std::string> doomed;doomed.reserve(drop);
        for(size_t i=0;i<drop;++i)doomed.push_back(*order[i].second);
        for(const auto& id:doomed)instances_.erase(id);
        evictedInstances_.fetch_add(drop,std::memory_order_relaxed);
    }
    // Drop least recently used geometry until the byte budget is back to three quarters (never the one just stored, never a pinned one). When the pinned
    // set alone is larger than the budget the cache stays over budget (reported in Stats::overBudget) instead of discarding prepared resources.
    void evict_geometries(uint64_t keep){
        std::vector<std::pair<uint64_t,uint64_t>> order;order.reserve(geometries_.size());
        for(const auto& [key,geometry]:geometries_)if(key!=keep&&!geometryPins_.contains(key))order.emplace_back(geometry->used.load(std::memory_order_relaxed),key);
        if(order.empty())return;
        std::sort(order.begin(),order.end());
        const size_t target=kMaxGeometryBytes/4*3;
        for(const auto& [used,key]:order){
            if(geometryBytes_<=target)break;
            auto found=geometries_.find(key);
            geometryBytes_-=found->second->bytes;geometries_.erase(found);
            evictedGeometries_.fetch_add(1,std::memory_order_relaxed);
        }
    }
    void check_bounds(const Character& c,const Bounds& cached){
        struct Scope{Scope(){in_object_cache()=true;}~Scope(){in_object_cache()=false;}} scope;
        const Bounds fresh=object_bounds_raw(c);
        if(std::memcmp(&fresh,&cached,sizeof(Bounds))!=0)throw std::runtime_error("Object cache returned stale bounds for "+c.Id);
    }
    void check_geometry(const Character& c,const Geometry& cached){
        struct Scope{Scope(){in_object_cache()=true;}~Scope(){in_object_cache()=false;}} scope;
        const Mesh fresh=object_mesh(c);
        bool same=fresh.vertices.size()==cached.mesh->vertices.size()&&fresh.indices==cached.mesh->indices;
        for(size_t i=0;same&&i<fresh.vertices.size();++i)same=std::memcmp(&fresh.vertices[i].position,&cached.mesh->vertices[i].position,sizeof(Vec3))==0;
        if(!same)throw std::runtime_error("Object cache returned stale geometry for "+c.Id);
    }
    const bool verify_=[]{char v[8]{};return GetEnvironmentVariableA("STARLIGHT_CACHE_VERIFY",v,8)&&v[0]=='1';}();
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string,std::unique_ptr<Instance>> instances_;
    std::unordered_map<uint64_t,std::shared_ptr<Geometry>> geometries_;
    std::unordered_map<uint64_t,int> geometryPins_;
    std::unordered_map<std::string,int> instancePins_;
    std::unordered_map<PinOwner,std::vector<uint64_t>> ownerGeometry_;
    std::unordered_map<PinOwner,std::vector<std::string>> ownerInstances_;
    size_t geometryBytes_=0;
    std::atomic<uint64_t> clock_{0},hits_{0},misses_{0},meshHits_{0},meshMisses_{0},evictedInstances_{0},evictedGeometries_{0},treeBuilds_{0},lateGeometry_{0},lateTrees_{0},pinOwners_{0};
    std::atomic<bool> prepared_{false};
};
}
