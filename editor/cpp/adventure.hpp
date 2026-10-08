#pragma once
#include "event_log.h"
#include "repair_switches.hpp"
#include "object_cache.hpp"
#include "spatial_index.hpp"
#include "environment_events.hpp"
#include "runtime_timing.hpp"
#include "particle_physics.hpp"
#include "terrain_contact.hpp"
#include "fracture.hpp"
#include "rock_fracture.hpp"
#include "destruction_dust.hpp"
#include "object_destruction.hpp"
#include "terrain_destruction.hpp"
#include "projectile_physics.hpp"
#include "lightning_physics.hpp"
#include "interactions.hpp"
#include "gameplay_rules.hpp"
#include "movement_resources.hpp"
#include "grapple_motion.hpp"
#include "basic_attack.hpp"
#include "ability_slots.hpp"
#include "ground_navigation.hpp"
#include "combat_control.hpp"
#ifdef STARLIGHT_CHARACTER_CONTROLLER
#include "adventure_controller.hpp"
#include <memory>
#endif
#include <unordered_set>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#ifdef _WIN32
#include <windows.h>
#endif
namespace starlight::editor {

// Play state is separate from the authored scene. Collision bounds are cached
// at session start; static geometry uses conservative world-space boxes.
// Breaking a rock is a pure function of its mesh, pose and seed. It is computed here so a background worker can
// finish it before the player ever hits the object (see AdventurePlayer::schedule_prefracture).
// `rotation` is the turn applied to every piece when the result was prepared for an earlier pose of the object (see adapt_fracture).
struct FractureResult {bool supported=false;std::vector<FracturePiece> pieces;uint32_t seed=0;std::array<double,4> rotation{0,0,0,1};};
// The rigid motion that takes an object from the pose a fracture was prepared at to its pose now: pieces move with it, and the turn is given to
// each piece as its starting orientation, so a prepared break stays valid however far the rock has been pushed or turned since.
template<class P,class N> inline void adapt_fracture(FractureResult& result,const P& prepared,const N& now){
    const auto o0=world_point(prepared,{}),o1=world_point(now,{});
    Vec3 a0[3]={world_point(prepared,{1,0,0})-o0,world_point(prepared,{0,1,0})-o0,world_point(prepared,{0,0,1})-o0};
    Vec3 a1[3]={world_point(now,{1,0,0})-o1,world_point(now,{0,1,0})-o1,world_point(now,{0,0,1})-o1};
    // The rotation that takes the prepared axis directions to the current ones (the scale is the same in both poses, so only the turn differs).
    std::array<std::array<double,3>,3> r{};
    Vec3 u0=a0[0].unit(),v0=a0[1].unit(),w0=u0.cross(v0).unit();
    Vec3 u1=a1[0].unit(),v1=a1[1].unit(),w1=u1.cross(v1).unit();
    // R = [u1 v1 w1] * [u0 v0 w0]^T
    const Vec3 c0[3]={u0,v0,w0},c1[3]={u1,v1,w1};
    auto comp=[&](const Vec3& v,int axis){return axis==0?v.x:axis==1?v.y:v.z;};
    for(int row=0;row<3;++row)for(int col=0;col<3;++col){double s=0;for(int k=0;k<3;++k)s+=comp(c1[k],row)*comp(c0[k],col);r[row][col]=s;}
    const double trace=r[0][0]+r[1][1]+r[2][2];
    double qx,qy,qz,qw;
    if(trace>0){double s=std::sqrt(trace+1.)*2;qw=.25*s;qx=(r[2][1]-r[1][2])/s;qy=(r[0][2]-r[2][0])/s;qz=(r[1][0]-r[0][1])/s;}
    else if(r[0][0]>r[1][1]&&r[0][0]>r[2][2]){double s=std::sqrt(1.+r[0][0]-r[1][1]-r[2][2])*2;qw=(r[2][1]-r[1][2])/s;qx=.25*s;qy=(r[0][1]+r[1][0])/s;qz=(r[0][2]+r[2][0])/s;}
    else if(r[1][1]>r[2][2]){double s=std::sqrt(1.+r[1][1]-r[0][0]-r[2][2])*2;qw=(r[0][2]-r[2][0])/s;qx=(r[0][1]+r[1][0])/s;qy=.25*s;qz=(r[1][2]+r[2][1])/s;}
    else{double s=std::sqrt(1.+r[2][2]-r[0][0]-r[1][1])*2;qw=(r[1][0]-r[0][1])/s;qx=(r[0][2]+r[2][0])/s;qy=(r[1][2]+r[2][1])/s;qz=.25*s;}
    const double norm=std::sqrt(qx*qx+qy*qy+qz*qz+qw*qw);
    result.rotation={qx/norm,qy/norm,qz/norm,qw/norm};
    for(auto& piece:result.pieces){
        const Vec3 d=piece.center-o0;
        piece.center=o1+Vec3{r[0][0]*d.x+r[0][1]*d.y+r[0][2]*d.z,r[1][0]*d.x+r[1][1]*d.y+r[1][2]*d.z,r[2][0]*d.x+r[2][1]*d.y+r[2][2]*d.z};
    }
}
template<class P> inline FractureResult compute_fracture(const Character& c,const P& moved,uint32_t serial,std::stop_token stop={}){
    FractureResult result;
    auto fractureParts=object_parts(c);
    result.supported=!fractureParts.empty();
    const bool geological=!c.Sculpt&&(c.Shape=="rock"||c.Shape=="boulder"||c.Shape=="pebbles"||c.Shape=="chaos-rock");
    auto origin=world_point(moved,{}),bx=world_point(moved,{1,0,0})-origin,by=world_point(moved,{0,1,0})-origin,bz=world_point(moved,{0,0,1})-origin;
    uint32_t fractureSeed=serial*2654435761u;for(unsigned char byte:c.Id)fractureSeed=(fractureSeed^byte)*16777619u;
    for(auto part:fractureParts){foliage_fragment_fallback(part.mesh,c);for(auto& v:part.mesh.vertices){v.position=world_point(moved,v.position);v.normal=(by.cross(bz)*v.normal.x+bz.cross(bx)*v.normal.y+bx.cross(by)*v.normal.z).unit();}auto pieces=geological?fracture_rock(part.mesh,part.color,part.emissive,fractureSeed++,nullptr,stop):std::vector<FracturePiece>{};if(pieces.empty()&&stop.stop_requested()){result.supported=false;break;}if(pieces.empty())pieces=fracture_mesh(std::move(part.mesh),part.color,part.emissive);if(pieces.empty()){result.supported=false;break;}result.pieces.insert(result.pieces.end(),std::make_move_iterator(pieces.begin()),std::make_move_iterator(pieces.end()));}
    result.seed=fractureSeed;
    return result;
}
class AdventurePlayer {
  public:
    AdventurePlayer()=default;
    ~AdventurePlayer(){release_pins();}
  private:
    GameSettings settings_;
    std::vector<std::pair<std::string,Bounds>> solids_;
    std::unordered_map<std::string,Bounds> interactionBounds_;
    SceneData playScene_;
    GameplayRulesRuntime rules_;
    bool rulesEnabled_=true;
    bool jumpRequested_=false;
    bool sprintHeld_=false;
    MovementResources movement_;
    GrappleMotion grapple_;
    double grappleBlockedSeconds_=0;
    std::string grappleTarget_;
    Vec3 grappleOffset_{},grappleLocalNormal_{};
    Mesh grappleTerrain_;
    double mantleRemaining_=0;
    bool grappleFalling_=false;
    Vec3 presentationVelocity_{};
    BasicAttack attack_;
    uint64_t presentationEpoch_=0,shotSerial_=0,shatterSerial_=0,impactSerial_=0;
    double lastShotTime_=-1;
    AbilitySlots abilitySlots_;
    struct HealingCast {std::string caster;BasicAttack cycle;Vec3 origin{},aim{};int team=1;};
    std::vector<HealingCast> healingCasts_;
    std::unordered_map<std::string,Character> actorRoutes_;
    std::unordered_map<std::string,double> actorRouteTimes_;
    CombatControl control_;
    HitChance hitChance_;
    std::optional<ControlEffect> lastDisplayControl_;
    std::unordered_set<std::string> lastVisibleIds_;
    double health_=100;
    std::unordered_map<std::string,int64_t> lightningCycles_;
    uint64_t lightningSerial_=0;
    std::vector<LightningSoundEvent> lightningSounds_;
    Vec3 controlOrigin_{},controlVelocity_{};
    bool launchAirborne_=false;
    double tetherPull_=0,tetherCooldown_=0;
    std::string selectedTarget_;
    struct Fragment : PhysicalParticle {Bounds localBounds;std::shared_ptr<const MeshPart> part;std::array<double,4> rotation{0,0,0,1};uint32_t body=UINT32_MAX;uint64_t serial=0;};
    std::vector<Fragment> fragments_;
    DestructionDust dust_;
    ObjectDestruction destruction_;
    TerrainDestruction terrainDestruction_;
    uint64_t fragmentSerial_=0;
    // Independent of playback/reset counters: replaying a break generates a
    // fresh pattern. Explicit fracture seeds still permit diagnostic replay.
    int breakOrdinal_=0;
    std::string lastPursuitBlocker_;uint64_t pursuitBlocked_=0;uint64_t turnApplied_=0,turnBlocked_=0,astarCalls_=0,astarFails_=0;double astarMs_=0;std::string lastTurnBlocker_; // boss diagnostics
    uint32_t fracturePatternSerial_=[](){uint32_t value=2166136261u;for(unsigned char byte:new_id())value=(value^byte)*16777619u;return value;}();
    ProjectilePhysics projectiles_;
    std::unordered_map<std::string,std::pair<Bounds,uint64_t>> projectileBounds_;uint64_t projectileBoundsPass_=0;
    std::shared_ptr<const LiquidSnapshot> liquidSnapshot_;
    std::unordered_map<std::string,CombatActor> combatActors_;
    double dodgeRemaining_=0;
    double dodgeRollSign_=1;
    Vec3 dodgeVelocity_{};
    bool vHeld_=false,vConsumed_=false,lunging_=false;
    double vTime_=0,vRight_=0,vForward_=1;
    int posture_=0;
    bool controlHeld_=false,controlConsumed_=false;
    double controlTime_=0,slideRemaining_=0;
    Vec3 slideVelocity_{};
    std::array<double,4> lastMovementPress_{{-1,-1,-1,-1}};
    double seconds_=0;
    ObjectCache::PinOwner pinOwner_=0;
  public:
    struct GeometryReport {size_t objects=0,distinctGeometries=0,rayTrees=0,rayTreeBytes=0,geometryBytes=0,pinnedGeometries=0,pinnedBytes=0,treeBytesBuiltHere=0;bool overBudget=false;};
  private:
    GeometryReport geometryReport_;
    void release_pins(){if(pinOwner_){ObjectCache::shared().unpin(pinOwner_);pinOwner_=0;}}
    struct SpatialState {
        SpatialIndex tree;
        std::unordered_map<std::string,uint32_t> slotOf;
        std::vector<const Character*> object;
        std::vector<uint32_t> movers;
        size_t characters=0,removedCount=0;
        bool built=false,fresh=false;
        std::vector<uint32_t> stamp;std::vector<Bounds> pursuit;uint32_t epoch=0;
    };
    mutable SpatialState spatial_;
    mutable SpatialIndex solidIndex_;
    std::unordered_map<std::string,uint32_t> solidSlot_;
    bool spatialEnabled_=repair_switch("STARLIGHT_FIX_SPATIAL");
#ifdef STARLIGHT_CHARACTER_CONTROLLER
    std::unique_ptr<AdventureController> controller_;
#endif
    std::optional<TerrainData> terrain_;
    // Background break-ahead: static breakables are fractured on one low-priority worker after the session starts.
    // States: 0 queued, 1 running, 2 prepared (a valid result), 3 abandoned (taken for synchronous work or superseded), 4 failed (reason says why).
    struct PreFracture {Character source;ObjectPose pose;uint64_t key=0;uint32_t serial=0;int state=0;std::string reason;std::optional<FractureResult> result;};
    std::unordered_map<std::string,std::shared_ptr<PreFracture>> preFractures_;
    std::deque<std::shared_ptr<PreFracture>> preQueue_;
    std::mutex preMutex_;
    std::condition_variable_any preCv_;
    std::atomic<uint64_t> preVersion_{0};
    uint64_t preTaken_=0;
    std::unordered_set<std::string> preTried_;double preTopUpAt_=-1;std::atomic<int> preActive_{0};
    std::vector<std::jthread> preWorkers_; // up to four (one per prepared breakable), so the break-ahead results are ready sooner after the session starts
    // Same object with the same geometry, scale and look; where it stands and how it is turned may differ (adapt_fracture follows it).
    static bool same_fracture_source(const PreFracture& entry,const Character& b){
        const Character& a=entry.source;
        return a.Id==b.Id&&a.Shape==b.Shape&&a.Asset==b.Asset&&a.Size==b.Size&&a.Width==b.Width&&a.Height==b.Height&&a.Depth==b.Depth
            &&a.Surface==b.Surface&&a.Color==b.Color&&a.BranchSpread==b.BranchSpread&&a.TargetX==b.TargetX&&a.TargetY==b.TargetY&&a.TargetZ==b.TargetZ&&a.Foliage==b.Foliage
            &&a.Sculpt.has_value()==b.Sculpt.has_value()&&(!a.Sculpt||a.Sculpt->content_revision()==b.Sculpt->content_revision())
            &&entry.key==fracture_source_key(b);
    }
  public:
    // Meshes of finished break-ahead results that have not been used yet, so a renderer can have them on the GPU
    // before the object is hit. Returns false when nothing changed since the previous call.
    bool take_prefractured_parts(std::vector<std::shared_ptr<const MeshPart>>& out){
        const uint64_t version=preVersion_.load();
        if(version==preTaken_)return false;
        preTaken_=version;out.clear();
        std::lock_guard lock(preMutex_);
        for(const auto& [id,entry]:preFractures_)
            if(entry->state==2&&entry->result)for(const auto& piece:entry->result->pieces)if(piece.part)out.push_back(piece.part);
        return true;
    }
  private:
    void stop_prefracture(){
        for(auto& worker:preWorkers_)worker.request_stop();for(auto& worker:preWorkers_)if(worker.joinable())worker.join();preWorkers_.clear();preActive_=0;
        std::lock_guard lock(preMutex_);preQueue_.clear();preFractures_.clear();
    }
    void schedule_prefracture(){
        stop_prefracture();
        preTried_.clear();preTopUpAt_=-1;
        top_up_prefracture(true);
    }
    // Rolling break-ahead. Up to STARLIGHT_PREFRACTURE_MAX (default 256) breakables are fractured on background workers at any time:
    // the ones inside the player's attack cone first, then the nearest to the player. As results are used, further breakables
    // are queued, so a rock hit later in a long fight is still ready and the gameplay thread does not stall for a fracture.
    void top_up_prefracture(bool initial=false){
        if(!rulesEnabled_)return;
        const bool rolling=repair_prefracture();
        if(!rolling&&!initial)return; // previous behaviour: one batch when the session starts
        static const size_t limit=[]{wchar_t v[16]{};if(GetEnvironmentVariableW(L"STARLIGHT_PREFRACTURE",v,16)&&v[0]==L'0')return size_t(0);wchar_t m[16]{};if(GetEnvironmentVariableW(L"STARLIGHT_PREFRACTURE_MAX",m,16)){int n=_wtoi(m);if(n>=0)return size_t(n);}return size_t(256);}(); // up to 256 breakables are prepared while the scene loads (every breakable of an ordinary scene); in a larger scene the rest follow as these are used, so memory does not grow with the breakable count
        if(limit==0)return;
        const size_t cap=rolling?limit:std::min<size_t>(limit,4);
        const size_t held=prefracture_live();
        if(held>=cap)return;
        // Every breakable, non-actor object that can turn into rubble (the Breakable flag, not only the explicit gameplay role).
        std::vector<const Character*> candidates;
        for(const auto& c:playScene_.Characters)
            if(c.Breakable&&!preTried_.contains(c.Id)&&!rules_.removed.contains(c.Id)&&destruction_has_rubble(c))candidates.push_back(&c); // Dynamic rocks, route platforms and combat actors are prepared too; the result follows the object to its pose when it breaks
        if(candidates.empty())return;
        const Vec3 feet=rolling?position-Vec3{0,1.7,0}:Vec3{settings_.SpawnX,settings_.SpawnY,settings_.SpawnZ};const Vec3 aim=attack_direction();
        auto inCone=[&](const Character* c){if(!rolling)return false;auto b=posed_bounds(*c);return inside_basic_telegraph(feet,aim,(b.low+b.high)*.5,basicAttackRange,basicAttackHalfAngle+.08);};
        std::vector<std::pair<std::pair<int,double>,const Character*>> ranked;ranked.reserve(candidates.size());
        for(const auto* c:candidates)ranked.push_back({{inCone(c)?0:1,(Vec3{c->X,c->Y,c->Z}-feet).length()},c});
        std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        const size_t room=cap-held;
        {
            std::lock_guard lock(preMutex_);
            for(size_t k=0;k<ranked.size()&&k<room;++k){
                const auto* c=ranked[k].second;
                auto entry=std::make_shared<PreFracture>();entry->source=*c;entry->pose=route_pose_of(*c,seconds_);entry->key=fracture_source_key(*c);entry->serial=uint32_t(++fracturePatternSerial_);
                preFractures_.emplace(c->Id,entry);preQueue_.push_back(entry);preTried_.insert(c->Id);
            }
        }
        if(preActive_.load()==0){for(auto& worker:preWorkers_)if(worker.joinable())worker.join();preWorkers_.clear();}
        const bool buildHulls=settings_.Collision;
        size_t queued=0;{std::lock_guard lock(preMutex_);queued=preQueue_.size();}
        const int wanted=int(std::min<size_t>(4,queued));
        while(preActive_.load()<wanted){
            ++preActive_;
            preWorkers_.emplace_back([this,buildHulls](std::stop_token stop){
                (void)buildHulls;
#ifdef _WIN32
                SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
                for(;;){
                    std::shared_ptr<PreFracture> entry;
                    {
                        std::unique_lock lock(preMutex_);
                        while(!preQueue_.empty()&&preQueue_.front()->state!=0)preQueue_.pop_front();
                        if(preQueue_.empty()||stop.stop_requested()){--preActive_;return;}
                        entry=preQueue_.front();preQueue_.pop_front();entry->state=1;
                    }
                    FractureResult result;std::string failure;
                    const double preStart=starlight_diag::events_enabled()?starlight_diag::evnow():0;
                    try{result=compute_fracture(entry->source,entry->pose,entry->serial,stop);}catch(const std::exception& error){result={};failure=std::string("fracture computation threw: ")+error.what();}catch(...){result={};failure="fracture computation threw an unknown error";}
                    if(failure.empty()&&(!result.supported||result.pieces.empty()))failure="the object's geometry produced no fracture pieces";
                    const double preFractured=preStart>0?starlight_diag::evnow():0;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
                    // Collision hulls are the slowest part of attaching debris to the physics world; build them here so the
                    // gameplay thread only attaches finished shapes. A piece without a hull makes the entry Failed (it can still be used, with the hull built at the break and counted).
                    if(buildHulls&&failure.empty()){
                        size_t missing=0;
                        try{for(auto& piece:result.pieces)if(piece.part){if(stop.stop_requested()){failure="preparation was stopped";break;}piece.hull=build_debris_hull(piece.part->mesh,entry->source.Density);if(piece.hull)piece.hullDensity=entry->source.Density;else ++missing;}}
                        catch(const std::exception& error){for(auto& piece:result.pieces)piece.hull.reset();failure=std::string("collision hull construction threw: ")+error.what();}
                        catch(...){for(auto& piece:result.pieces)piece.hull.reset();failure="collision hull construction threw an unknown error";}
                        if(failure.empty()&&missing)failure="a collision hull could not be built for "+std::to_string(missing)+" of "+std::to_string(result.pieces.size())+" pieces";
                    }
#endif
                    if(preStart>0)starlight_diag::evlog("pre.done",preFractured-preStart,starlight_diag::evnow()-preFractured,double(result.pieces.size()));
                    {
                        std::lock_guard lock(preMutex_);
                        entry->result=std::move(result);entry->reason=failure;entry->state=entry->state==3?3:failure.empty()?2:4;
                        ++preVersion_;
                    }
                    preCv_.notify_all();
                }
            });
        }
    }
    // Returns the finished fracture of this exact, unchanged object, waiting for the worker if it is mid-way, or
    // nothing (caller computes synchronously) when there is none, it was not started, or the object changed.
    std::optional<FractureResult> obtain_prefracture(const Character& c,const ObjectPose& now){
        std::shared_ptr<PreFracture> entry;
        {
            std::lock_guard lock(preMutex_);
            auto found=preFractures_.find(c.Id);if(found==preFractures_.end()){++runtime_bypass().fractureNoEntry;std::fprintf(stderr,"prefracture: none held for %s\n",c.Name.c_str());return {};}
            entry=found->second;preFractures_.erase(found);++preVersion_;
        }
        if(!same_fracture_source(*entry,c)){
            ++runtime_bypass().fractureStaleSource;
            std::fprintf(stderr,"prefracture: source changed for %s (state %d)\n",c.Name.c_str(),entry->state);
            std::lock_guard lock(preMutex_);if(entry->state==0)entry->state=3;return {};
        }
        std::unique_lock lock(preMutex_);
        if(entry->state==0){++runtime_bypass().fractureNotStarted;std::fprintf(stderr,"prefracture: not started for %s\n",c.Name.c_str());entry->state=3;lock.unlock();return compute_fracture(c,now,entry->serial);}
        if(entry->state==1){++runtime_bypass().fractureWaited;std::fprintf(stderr,"prefracture: still running for %s, waiting\n",c.Name.c_str());}
        preCv_.wait(lock,[&]{return entry->state==2||entry->state==3||entry->state==4;});
        if(entry->state==4){
            ++runtime_bypass().fractureFailedEntry;
            std::fprintf(stderr,"prefracture: preparation failed for %s: %s\n",c.Name.c_str(),entry->reason.c_str());
            if(!entry->result||!entry->result->supported||entry->result->pieces.empty())return {};
        }
        if(!entry->result||!entry->result->supported&&entry->result->pieces.empty())return {};
        auto ready=std::move(entry->result);
        lock.unlock();
        if(ready)adapt_fracture(*ready,entry->pose,now);
        return ready;
    }
    bool blocked(Vec3 p) const {return blocked_with(p,spatialEnabled_);}
    bool blocked_with(Vec3 p,bool indexed) const {
        if(!settings_.Collision)return false;
        const double feet=p.y-1.7;
        auto test=[&](const std::pair<std::string,Bounds>& solid){
            if(rules_.removed.contains(solid.first))return false;const auto& b=solid.second;
            if(b.high.y<=feet+.05 || b.low.y>=p.y+.1)return false;
            double x=std::clamp(p.x,b.low.x,b.high.x),z=std::clamp(p.z,b.low.z,b.high.z);
            return std::hypot(p.x-x,p.z-z)<settings_.PlayerRadius;
        };
        if(indexed&&solidIndex_.size()==solids_.size()){
            SpatialBox query;const double reach=settings_.PlayerRadius+1e-6;
            query.lo[0]=p.x-reach;query.hi[0]=p.x+reach;query.lo[1]=-1e300;query.hi[1]=1e300;query.lo[2]=p.z-reach;query.hi[2]=p.z+reach;
            std::vector<uint32_t> slots;solidIndex_.overlap(query,slots);
            for(uint32_t slot:slots)if(test(solids_[slot]))return true;
            return false;
        }
        for(const auto& solid:solids_)if(test(solid))return true;
        return false;
    }
  public:
    // Diagnostics: cumulative milliseconds spent breaking objects (obtaining/computing the fracture, creating collision hulls).
    struct DestructionTiming {double fractureMs=0,hullMs=0;} lastDestruction;
    RuntimeTiming lastTiming;
    Vec3 position{};
    bool finished=false;
    const SceneData& scene() const {return playScene_;}
    std::vector<Character> dynamic_objects()const{
        std::vector<Character> result;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        // The render session only reads the pose of these objects, so each entry carries the pose and nothing else: copying a sculpted
        // model's vertex and index arrays for every resting rock on every update was the largest cost of a scene with sculpted statues.
        if(controller_)for(const auto& c:playScene_.Characters)if(c.Dynamic){
            Character pose;pose.Id=c.Id;pose.Dynamic=true;pose.X=c.X;pose.Y=c.Y;pose.Z=c.Z;pose.PhysicsRotation=c.PhysicsRotation;pose.JointRuntimePoints=c.JointRuntimePoints;
            result.push_back(std::move(pose));
        }
#endif
        return result;
    }
    double sight_range()const{return settings_.SightRange*(control_.active==ControlEffect::Blind?.5:1.);}
    // Bounds of an object at the current time. Objects that never move, turn or get driven by physics keep their bounds,
    // so sculpted models (tens of thousands of vertices) are not re-measured on every update.
    Bounds posed_bounds(const Character& c)const{
        if(!repair_bounds())return object_bounds(route_pose(c,seconds_));
        if(bounds_cacheable(c)&&!combatActors_.contains(c.Id))return ObjectCache::shared().bounds(c); // the engine-wide cache; refreshes itself when the object changes
        return ObjectCache::shared().bounds_at(c,route_pose_of(c,seconds_)); // routes and actors: shared geometry, this moment's pose, no Character copy
    }
    // Exact triangle hit against an object. Scenery that does not move on its own uses the engine-wide prepared collision mesh;
    // everything else (routes, physics, actors) builds its mesh as before.
    std::optional<MeshHit> object_hit(const Character& object,const Character& posed,Vec3 origin,Vec3 direction)const{
        // The shape of an object does not change when it moves, so routed platforms and combat actors share the same geometry and tree.
        if(repair_bounds())return ObjectCache::shared().raycast(object,posed,origin,direction);
        return mesh_hit(object_mesh(posed),posed,origin,direction);
    }
    std::optional<MeshHit> object_hit(const Character& object,const ObjectPose& posed,Vec3 origin,Vec3 direction)const{
        if(repair_bounds())return ObjectCache::shared().raycast(object,posed,origin,direction);
        auto legacy=route_pose(object,seconds_);
        return mesh_hit(object_mesh(legacy),legacy,origin,direction);
    }
    static SpatialBox spatial_box(const Bounds& b){SpatialBox s;s.lo[0]=b.low.x;s.lo[1]=b.low.y;s.lo[2]=b.low.z;s.hi[0]=b.high.x;s.hi[1]=b.high.y;s.hi[2]=b.high.z;return s;}
    bool spatial_active()const{return spatialEnabled_&&repair_bounds();}
    // Test hook: switch the shared candidate index off to compare against the full scan it replaces.
    void use_spatial_index(bool on){spatialEnabled_=on;}
    bool spatial_mover(const Character& c)const{return c.Dynamic||!c.Route.empty()||c.CombatHealth>0||c.CombatPursuit||combatActors_.contains(c.Id);}
    // The obstacle bounds pursuit has always used: cached for scenery, this moment's route position (without its turn) for everything else.
    Bounds pursuit_bounds(const Character& other)const{
        if(bounds_cacheable(other)&&!combatActors_.contains(other.Id))return ObjectCache::shared().bounds(other);
        auto pose=object_pose(other);if(!other.Route.empty()){auto at=other.at(seconds_);pose.X=at.X;pose.Z=at.Z;}
        return ObjectCache::shared().bounds_at(other,pose,false);
    }
    // Conservative box of an object for the candidate index: covers every bounds any caller measures for it at this moment.
    SpatialBox spatial_box_of(const Character& c)const{
        auto box=spatial_box(posed_bounds(c));
        if(!c.Route.empty())box.include(spatial_box(pursuit_bounds(c)));
        return box;
    }
    void sync_spatial(bool preparing=false)const{
        auto& s=spatial_;
        const size_t count=playScene_.Characters.size(),removed=rules_.removed.size();
        if(s.built&&s.characters==count&&s.removedCount==removed)return;
        if(!s.built&&!preparing)++runtime_bypass().spatialIndexBuiltLate;
        const bool first=!s.built;
        std::vector<uint8_t> seen(s.object.size(),0);
        if(first)s.tree.begin_bulk();
        for(const auto& c:playScene_.Characters){
            auto found=s.slotOf.find(c.Id);uint32_t slot=0;bool fresh=false;
            if(found==s.slotOf.end()){slot=uint32_t(s.object.size());s.slotOf.emplace(c.Id,slot);s.object.push_back(nullptr);seen.push_back(0);fresh=true;}else slot=found->second;
            s.object[slot]=&c;seen[slot]=1;
            if(fresh)s.tree.set(slot,spatial_box_of(c));
        }
        for(uint32_t slot=0;slot<s.object.size();++slot)if(!seen[slot]){s.object[slot]=nullptr;s.tree.erase(slot);}
        if(first)s.tree.end_bulk();
        s.movers.clear();
        for(uint32_t slot=0;slot<s.object.size();++slot)if(s.object[slot]&&spatial_mover(*s.object[slot]))s.movers.push_back(slot);
        s.stamp.resize(s.object.size(),0);s.pursuit.resize(s.object.size());
        s.characters=count;s.removedCount=removed;s.built=true;s.fresh=false;
    }
    // Brings the boxes of everything that can move up to date (once per change of pose, not once per query).
    void refresh_spatial(bool preparing=false)const{
        sync_spatial(preparing);
        auto& s=spatial_;if(s.fresh)return;
        for(uint32_t slot:s.movers)s.tree.set(slot,spatial_box_of(*s.object[slot]));
        s.fresh=true;
    }
    // One object moved or turned: keep its own box exact without re-measuring every mover.
    void touch_spatial(const Character& c)const{
        auto& s=spatial_;if(!s.built)return;
        auto found=s.slotOf.find(c.Id);
        if(found!=s.slotOf.end()&&s.object[found->second])s.tree.set(found->second,spatial_box_of(c));
    }
    static bool spatial_verify(){static const bool on=[]{char v[8]{};return GetEnvironmentVariableA("STARLIGHT_SPATIAL_VERIFY",v,8)&&v[0]=='1';}();return on;}
    // Calls visit(object) in scene order for every object a segment could touch (a superset of the objects whose bounds the segment reaches).
    template<class F> void each_ray_candidate(Vec3 origin,Vec3 direction,double length,F&& visit)const{
        if(!spatial_active()){for(const auto& c:playScene_.Characters)visit(c);return;}
        refresh_spatial();
        const double o[3]={origin.x,origin.y,origin.z},d[3]={direction.x,direction.y,direction.z};
        std::vector<uint32_t> slots;spatial_.tree.segment(o,d,length,.05,slots);
        if(spatial_verify())for(const auto& c:playScene_.Characters)if(ray_reaches_bounds(origin,direction,length,posed_bounds(c))&&!std::binary_search(slots.begin(),slots.end(),spatial_.slotOf.at(c.Id)))throw std::runtime_error("Spatial index missed "+c.Id);
        for(uint32_t slot:slots)if(const Character* c=spatial_.object[slot])visit(*c);
    }
    // Calls visit(object) in scene order for every object whose box overlaps the given one.
    template<class F> void each_box_candidate(Vec3 low,Vec3 high,F&& visit)const{
        if(!spatial_active()){for(const auto& c:playScene_.Characters)visit(c);return;}
        refresh_spatial();
        SpatialBox query;query.lo[0]=low.x-1e-6;query.lo[1]=low.y-1e-6;query.lo[2]=low.z-1e-6;query.hi[0]=high.x+1e-6;query.hi[1]=high.y+1e-6;query.hi[2]=high.z+1e-6;
        std::vector<uint32_t> slots;spatial_.tree.overlap(query,slots);
        for(uint32_t slot:slots)if(const Character* c=spatial_.object[slot])visit(*c);
    }
    static bool ray_reaches_bounds(Vec3 origin,Vec3 direction,double distance,const Bounds& b){
        double tNear=0,tFar=distance;
        const double o[3]={origin.x,origin.y,origin.z},d[3]={direction.x,direction.y,direction.z},lo[3]={b.low.x-.02,b.low.y-.02,b.low.z-.02},hi[3]={b.high.x+.02,b.high.y+.02,b.high.z+.02};
        for(int axis=0;axis<3;++axis){
            if(std::abs(d[axis])<1e-12){if(o[axis]<lo[axis]||o[axis]>hi[axis])return false;continue;}
            double t1=(lo[axis]-o[axis])/d[axis],t2=(hi[axis]-o[axis])/d[axis];if(t1>t2)std::swap(t1,t2);
            tNear=std::max(tNear,t1);tFar=std::min(tFar,t2);if(tNear>tFar)return false;
        }
        return true;
    }
    bool perceived(const Character& c)const{
        auto it=combatActors_.find(c.Id);
        if(control_.active==ControlEffect::Taunt && it!=combatActors_.end() && it->second.hostile_to(1) && combat_identity(c.Id)!=control_.source.id)return false;
        auto at=c.at(seconds_);auto delta=Vec3{at.X,c.Y,at.Z}-position;
        return delta.length()<=sight_range();
    }
    SceneData visible_scene()const{
        auto copy=playScene_;
        std::erase_if(copy.Characters,[&](const Character& c){return !perceived(c);});
        if(control_.active==ControlEffect::Blind)copy.FogDensity=std::min(.3,std::max(.018,copy.FogDensity)*2);
        return copy;
    }
    const BasicAttack& basic_attack()const{return attack_;}
    const CombatControl& crowd_control()const{return control_;}
    double health()const{return health_;}
    Bounds body_bounds()const{auto feet=position-Vec3{0,1.7,0};double height=posture_==0?1.8:posture_==2?.5:1.;return {feet-Vec3{settings_.PlayerRadius,0,settings_.PlayerRadius},feet+Vec3{settings_.PlayerRadius,height,settings_.PlayerRadius}};}
    struct CombatDisplay {
        double health=100,targetHealth=0,targetMaximum=0,breakStrength=0,breakMaximum=1000,effectSeconds=0,immunitySeconds=0,phaseSeconds=0;
        bool hasTarget=false,boss=false,waking=false;
        std::optional<ControlEffect> effect;
        BossBreakBar::Phase phase=BossBreakBar::Phase::Ready;
    };
    CombatDisplay combat_display()const{
        CombatDisplay d;d.health=health_;d.effect=control_.active;d.effectSeconds=control_.remaining;d.immunitySeconds=control_.immunity;d.waking=control_.waking;
        if(auto it=combatActors_.find(selectedTarget_);it!=combatActors_.end() && it->second.health>0){
            const auto& a=it->second;d.hasTarget=true;d.targetHealth=a.health;d.targetMaximum=a.maximum;d.boss=a.boss;
            d.breakStrength=a.bar.strength;d.breakMaximum=a.bar.capacity;d.phase=a.bar.phase;d.phaseSeconds=a.bar.remaining;
        }
        return d;
    }

    const std::unordered_map<std::string,CombatActor>& combat_actors()const{return combatActors_;}
    bool control_actor(const std::string& id,ControlEffect effect,ControlSource source,int sourceTeam=1,uint64_t event=0){
        auto it=combatActors_.find(id);return it!=combatActors_.end() && it->second.apply(effect,source,sourceTeam,event);
    }
    double heal_actor(const std::string& id,double amount,int sourceTeam=1){auto it=combatActors_.find(id);return it==combatActors_.end()?0:it->second.heal(amount,sourceTeam);}
    double restore_health(double amount){
        if(health_<=0 || !std::isfinite(amount) || amount<=0)return 0;
        double restored=std::min(100-health_,amount);health_+=restored;return restored;
    }
    void healing_cast(const std::string& caster,bool held){
        if(!rulesEnabled_)return;
        for(auto& h:healingCasts_)if(h.caster==caster){h.cycle.press(held);return;}
        if(!held || (!caster.empty() && !combatActors_.contains(caster)))return;
        HealingCast h;h.caster=caster;h.cycle.cycleSeconds=1.5;h.cycle.press(true);healingCasts_.push_back(h);
    }
    bool unobstructed(Vec3 origin,Vec3 point,const std::string& caster,const std::string& target)const{
        auto delta=point-origin;double distance=delta.length();if(distance<.01)return true;
        const auto direction=delta.unit();
        bool clear=true;
        each_ray_candidate(origin,direction,distance,[&](const Character& c){
            if(!clear || c.Id==caster || c.Id==target || rules_.removed.contains(c.Id))return;
            if(repair_bounds()&&!ray_reaches_bounds(origin,direction,distance,posed_bounds(c)))return; // cannot be hit; skip building its mesh
            // Scenery that does not move on its own uses the engine-wide prepared collision mesh instead of rebuilding it per query.
            auto hit=object_hit(c,route_pose_of(c,seconds_),origin,direction);
            if(hit && hit->distance<distance-.01)clear=false;
        });
        if(!clear)return false;
        if(terrain_)for(double d=.1;d<distance;d+=.1){auto at=origin+delta.unit()*d;if(terrain_->Volumes.empty()?at.y<terrain_->height_at(at.x,at.z):terrain_->density_at(at.x,at.y,at.z)<0)return false;}
        return true;
    }
    void advance_healing(double dt){
        for(auto& h:healingCasts_){
            bool blocked=false;double rate=1;
            if(h.caster.empty()){h.origin=position-Vec3{0,1.7,0};h.aim=attack_direction();h.team=1;blocked=health_<=0 || dodging() || control_.blocks_skills();rate=control_.firing_factor();}
            else {
                auto it=combatActors_.find(h.caster);if(it==combatActors_.end()){h.cycle.clear();continue;}
                auto& actor=it->second;h.team=actor.team;blocked=actor.health<=0 || rules_.removed.contains(h.caster) || actor.control.blocks_skills() || (actor.boss && actor.bar.phase==BossBreakBar::Phase::Staggered);rate=actor.control.firing_factor();
                for(const auto& c:playScene_.Characters)if(c.Id==h.caster){auto b=posed_bounds(c);h.origin={(b.low.x+b.high.x)*.5,b.low.y,(b.low.z+b.high.z)*.5};}
                h.aim=actor.attackAim.length()>.1?actor.attackAim:Vec3{0,0,-1};
            }
            if(!h.cycle.advance(dt,blocked,rate))continue;
            auto feet=position-Vec3{0,1.7,0},playerContact=feet+Vec3{0,.85,0};
            if(h.team==1 && surface_telegraph_contact(terrain_?&*terrain_:nullptr,h.origin,h.aim,feet-Vec3{.3,0,.3},feet+Vec3{.3,posture_==0?1.8:posture_==2?.5:1.,.3},playerContact,3,.7853981633974483) && unobstructed(h.origin+Vec3{0,.85,0},playerContact,h.caster,""))restore_health(20);
            for(const auto& c:playScene_.Characters)if(auto it=combatActors_.find(c.Id);it!=combatActors_.end() && it->second.team==h.team){
                auto b=posed_bounds(c);auto point=(b.low+b.high)*.5;if(surface_telegraph_contact(terrain_?&*terrain_:nullptr,h.origin,h.aim,b.low,b.high,point,3,.7853981633974483) && unobstructed(h.origin+Vec3{0,.85,0},point,h.caster,c.Id))it->second.heal(20,h.team);
            }
        }
    }
    void defeat(){
        health_=0;cancel_mobility_input();control_={};control_.player=true;launchAirborne_=false;
        dodgeRemaining_=slideRemaining_=mantleRemaining_=0;grapple_.reset();grappleFalling_=false;selectedTarget_.clear();
        for(auto& h:healingCasts_)h.cycle.clear();for(auto& [id,a]:combatActors_)a.attack.clear();
    }
    double take_damage(double amount){
        if(!std::isfinite(amount) || amount<=0 || health_<=0 || dodging() || lunging())return 0;
        double applied=std::min(health_,amount);health_-=applied;control_.hit(applied);
        if(health_==0)defeat();
        return applied;
    }
    CombatControl::Result apply_control(ControlEffect effect,ControlSource source,bool hostile,bool self=false,Vec3 sourcePosition={}){
        if(health_<=0)return CombatControl::Result::Rejected;
        const auto result=control_.apply(effect,source,hostile,self);
        if(result==CombatControl::Result::Applied){
            launchAirborne_=effect==ControlEffect::Launch;tetherPull_=tetherCooldown_=0;
            controlOrigin_=sourcePosition;auto away=position-sourcePosition;away.y=0;
            away=away.length()>.01?away.unit():Vec3{0,0,1};
            controlVelocity_=effect==ControlEffect::Knockback?away*6:effect==ControlEffect::Launch?away*6+Vec3{0,6,0}:Vec3{};
            if(control_.blocks_basic())attack_.cancel();
            if(control_.blocks_mobility()){dodgeRemaining_=0;slideRemaining_=0;mantleRemaining_=0;jumpRequested_=false;vHeld_=false;vConsumed_=false;controlHeld_=false;controlConsumed_=true;}
            if(effect==ControlEffect::Root && grapple_.state==GrappleMotion::State::Pulling){
                grapple_.detach();grapple_.held=false;grapple_.velocity={0,grapple_.velocity.y,0};grappleFalling_=true;
            }
            if(effect==ControlEffect::Stun || effect==ControlEffect::Fear || effect==ControlEffect::Sleep || effect==ControlEffect::Knockback || effect==ControlEffect::Pull || effect==ControlEffect::Launch || effect==ControlEffect::Knockdown){
                if(grapple_.state!=GrappleMotion::State::Free){grapple_.detach();grapple_.held=false;grappleFalling_=true;}
            }
        }
        if(result==CombatControl::Result::CancelledBySource)launchAirborne_=false;
        return result;
    }
    void cleanse_control(){control_.cleanse();launchAirborne_=false;tetherPull_=0;}
    const std::string& selected_target()const{return selectedTarget_;}
    const AbilitySlots& ability_slots()const{return abilitySlots_;}
    void ability_button(unsigned key,bool down){if(rulesEnabled_ && abilitySlots_.action(key)==AbilityAction::BasicAttack)attack_.press(down);}
    void fire_button(bool down){ability_button('1',down);}
    void select_target(){
        std::vector<std::pair<double,std::string>> targets;
        for(const auto& c:playScene_.Characters)if((combatActors_.contains(c.Id) ? combatActors_.at(c.Id).hostile_to(1) : c.Breakable) && !rules_.removed.contains(c.Id) && perceived(c)){
            const auto b=posed_bounds(c);const auto delta=(b.low+b.high)*.5-view_position();
            if(delta.length()<=25 && delta.unit().dot(camera_direction())>0)targets.push_back({1-delta.unit().dot(camera_direction())+delta.length()*.0001,c.Id});
        }
        std::sort(targets.begin(),targets.end());
        if(targets.empty()){selectedTarget_.clear();return;}
        auto it=std::find_if(targets.begin(),targets.end(),[&](const auto& t){return t.second==selectedTarget_;});
        selectedTarget_=it==targets.end() || ++it==targets.end()?targets.front().second:it->second;
    }
    inline static double mvTarget_=0,mvTurn_=0,mvMoveLoop_=0;
    // Turns an actor toward `direction`. The actor's box is axis-aligned, so turning in place can make it wider and push it into a neighbour
    // (a square grows by about a fifth of its half-width). It therefore turns as far as it can without overlapping anything that blocks
    // movement, and never into an overlap: an actor inside an obstacle cannot move at all.
    void turn_actor(Character& c,Vec3 direction,double dt){
        if(c.PhysicsRotation || std::hypot(direction.x,direction.z)<1e-8)return;
        double wanted=std::atan2(-direction.x,-direction.z)*57.29577951308232;
        double difference=std::remainder(wanted-c.RotationY,360.);double change=std::clamp(difference,-360*dt,360*dt);
        if(std::abs(change)<1e-8)return;
        auto candidate=object_pose(c);
        auto boxAt=[&](double fraction){
            candidate.RotationY=std::remainder(c.RotationY+change*fraction,360.);
            if(repair_bounds())return ObjectCache::shared().bounds_at(c,candidate,false);
            Character legacy=c;legacy.RotationY=candidate.RotationY;return object_bounds(legacy);
        };
        const Bounds full=boxAt(1);
        const double reachX=(full.high.x-full.low.x)+1,reachZ=(full.high.z-full.low.z)+1;
        std::vector<std::pair<const Character*,Bounds>> neighbours;
        each_box_candidate({full.low.x-reachX,-1e300,full.low.z-reachZ},{full.high.x+reachX,1e300,full.high.z+reachZ},[&](const Character& other){
            if(!(other.Id!=c.Id && !rules_.removed.contains(other.Id) && (!repair_solids()||blocks_movement(other))))return;
            auto b=posed_bounds(other);
            if(b.high.x<full.low.x-reachX||b.low.x>full.high.x+reachX||b.high.z<full.low.z-reachZ||b.low.z>full.high.z+reachZ)return;
            neighbours.emplace_back(&other,b);
        });
        const Character* blocker=nullptr;
        auto overlaps=[&](const Bounds& box){
            for(const auto& [other,b]:neighbours)
                if(box.low.x<b.high.x && box.high.x>b.low.x && box.low.z<b.high.z && box.high.z>b.low.z && box.low.y<b.high.y-1e-5 && box.high.y>b.low.y+1e-5){blocker=other;return true;}
            return false;
        };
        Bounds proposed=full;
        if(overlaps(full)){
            if(!repair_turn()){++turnBlocked_;lastTurnBlocker_=blocker->Id;return;}
            double clear=0,blocked=1;
            for(int refinement=0;refinement<8;++refinement){
                const double mid=(clear+blocked)*.5;
                if(overlaps(boxAt(mid)))blocked=mid;else clear=mid;
            }
            if(clear<1e-3){++turnBlocked_;if(blocker)lastTurnBlocker_=blocker->Id;return;}
            proposed=boxAt(clear); // candidate now holds the largest turn that stays clear
        }
        ++turnApplied_;
        c.RotationY=candidate.RotationY;interactionBounds_[c.Id]=proposed;++rules_.revision;touch_spatial(c);
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        if(controller_)controller_->move_actor(c.Id,{c.X,c.Y,c.Z},dt,c.RotationY);
#endif
    }
    struct ActorTarget {std::string id;Vec3 feet;};
    std::optional<ActorTarget> actor_target(const Character& caster,const CombatActor& actor)const{
        std::optional<ActorTarget> result;double nearest=caster.CombatSightRange*(actor.control.active==ControlEffect::Blind?.5:1.);
        auto at=caster.at(seconds_);Vec3 origin{at.X,caster.Y,at.Z};
        auto consider=[&](const std::string& id,Vec3 feet,std::uint64_t identity){
            if(actor.control.active==ControlEffect::Taunt && actor.control.source.id!=identity)return;
            auto delta=feet-origin;delta.y=0;double distance=delta.length();
            if(distance<=nearest){nearest=distance;result=ActorTarget{id,feet};}
        };
        if(health_>0 && actor.hostile_to(1))consider("",position-Vec3{0,1.7,0},1);
        for(const auto& c:playScene_.Characters){
            auto it=combatActors_.find(c.Id);
            if(c.Id==caster.Id || it==combatActors_.end() || it->second.health<=0 || !actor.hostile_to(it->second.team) || rules_.removed.contains(c.Id))continue;
            auto b=posed_bounds(c);consider(c.Id,{(b.low.x+b.high.x)*.5,b.low.y,(b.low.z+b.high.z)*.5},combat_identity(c.Id));
        }
        return result;
    }
    void advance_pursuit(double dt){
        if(!rulesEnabled_ || !std::isfinite(dt) || dt<=0)return;
        dt=std::min(dt,.05);
        // Obstacle bounds for the pathing checks below are gathered once per update. Recomputing every object's bounds (and
        // copying the object) inside each sample of each search edge made one failed search cost seconds on a populated scene.
        std::vector<std::pair<const Character*,Bounds>> pursuitObstacles;bool pursuitObstaclesReady=false;
        auto ensurePursuitObstacles=[&]{
            if(pursuitObstaclesReady)return;pursuitObstaclesReady=true;pursuitObstacles.reserve(playScene_.Characters.size());
            for(const auto& other:playScene_.Characters){
                if(rules_.removed.contains(other.Id))continue;
                if(repair_solids()&&!blocks_movement(other))continue; // grass, lights, fire, keys never stop an enemy
                if(bounds_cacheable(other)&&!combatActors_.contains(other.Id)){
                    pursuitObstacles.emplace_back(&other,ObjectCache::shared().bounds(other));
                }else{
                    auto obstacle=other;if(!other.Route.empty()){auto at=other.at(seconds_);obstacle.X=at.X;obstacle.Z=at.Z;}
                    pursuitObstacles.emplace_back(&other,object_bounds(obstacle));
                }
            }
        };
        const bool indexedPursuit=spatial_active();
        if(indexedPursuit){refresh_spatial();++spatial_.epoch;}
        auto lazyObstacle=[&](uint32_t slot,const Character& other)->const Bounds&{
            if(spatial_.stamp[slot]!=spatial_.epoch){spatial_.pursuit[slot]=pursuit_bounds(other);spatial_.stamp[slot]=spatial_.epoch;}
            return spatial_.pursuit[slot];
        };
        auto anyObstacle=[&](const Character& self,Vec3 low,Vec3 high,double pad,auto&& blocks)->bool{
            if(!indexedPursuit){
                ensurePursuitObstacles();
                for(const auto& [other,b]:pursuitObstacles)if(other!=&self&&blocks(*other,b))return true;
                return false;
            }
            SpatialBox query;query.lo[0]=low.x-pad-1e-6;query.lo[1]=low.y-pad-1e-6;query.lo[2]=low.z-pad-1e-6;query.hi[0]=high.x+pad+1e-6;query.hi[1]=high.y+pad+1e-6;query.hi[2]=high.z+pad+1e-6;
            std::vector<uint32_t> slots;spatial_.tree.overlap(query,slots);
            for(uint32_t slot:slots){
                const Character* other=spatial_.object[slot];
                if(!other||other==&self||rules_.removed.contains(other->Id)||(repair_solids()&&!blocks_movement(*other)))continue;
                if(blocks(*other,lazyObstacle(slot,*other)))return true;
            }
            return false;
        };
        for(auto& c:playScene_.Characters){
            auto found=combatActors_.find(c.Id);if(found==combatActors_.end())continue;
            const bool hasRoute=actorRoutes_.contains(c.Id);
            const bool fleeing=found->second.control.active==ControlEffect::Fear;
            const bool displaced=found->second.control.active==ControlEffect::Knockback || found->second.control.active==ControlEffect::Pull;
            const bool launched=found->second.launchAirborne;
            const bool tethered=found->second.control.active==ControlEffect::Tether;
            if(!c.CombatPursuit && !hasRoute && !fleeing && !displaced && !launched && !tethered)continue;
            auto& actor=found->second;const double tgA=starlight_diag::evnow();auto victim=actor_target(c,actor);mvTarget_+=starlight_diag::evnow()-tgA;Vec3 origin{c.X,c.Y,c.Z};
            bool followingRoute=hasRoute && (!c.CombatPursuit || !victim);
            if(followingRoute){
                if(!actor.control.blocks_mobility() && !(actor.boss && actor.bar.phase==BossBreakBar::Phase::Staggered))actorRouteTimes_[c.Id]+=dt*actor.control.movement_factor();
                auto at=actorRoutes_.at(c.Id).at(actorRouteTimes_[c.Id]);victim=ActorTarget{"",{at.X,c.Y,at.Z}};
            }
            Vec3 effectSource=position;
            if(fleeing || displaced || launched || tethered){
                Vec3 source=position;
                for(const auto& other:playScene_.Characters)if(combat_identity(other.Id)==actor.control.source.id){auto at=other.at(seconds_);source={at.X,other.Y,at.Z};break;}
                effectSource=source;
                auto away=origin-source;away.y=0;if(away.length()<.01)away={0,0,1};
                if(!tethered)victim=ActorTarget{"",origin+away.unit()*10};
                else if(!victim)victim=ActorTarget{"",origin};
            }
            if(!victim){actor.steering.stop();continue;}auto target=victim->feet;target.y=c.Y;
            if(actor.health<=0 || (actor.control.blocks_mobility() && !fleeing && !displaced && !launched) || (actor.boss && actor.bar.phase==BossBreakBar::Phase::Staggered) || (target-origin).length()>20){actor.steering.stop();continue;}
            auto bounds=object_bounds(c);double radius=std::max(bounds.high.x-bounds.low.x,bounds.high.z-bounds.low.z)*.5;
            double spacing=std::max(2.,radius+settings_.PlayerRadius+.2);
            auto routeClear=[&](Vec3 from,Vec3 to){
                auto span=to-from;int samples=std::max(1,int(std::ceil(span.length()/.2)));
                double previousFeet=bounds.low.y+from.y-origin.y;
                for(int i=1;i<=samples;i++){
                    auto at=from+span*(double(i)/samples);auto offset=at-origin;
                    if(terrain_){
                        if(!terrain_->Volumes.empty()){
                            auto floor=terrain_body_support(*terrain_,bounds,offset,previousFeet);
                            if(!floor||std::abs(*floor-previousFeet)>std::hypot(span.x,span.z)/samples+.05)return false;
                            offset.y=*floor-bounds.low.y;previousFeet=*floor;
                        }else{double oldHeight=terrain_->height_at(from.x,from.z),height=terrain_->height_at(at.x,at.z);if(std::abs(height-oldHeight)>std::hypot(at.x-from.x,at.z-from.z)+.05)return false;offset.y=height-terrain_->height_at(origin.x,origin.z);}
                    }
                    auto low=bounds.low+offset,high=bounds.high+offset;
                    if(anyObstacle(c,low,high,.06,[&](const Character&,const Bounds& b){
                        return low.x<b.high.x+.03 && high.x>b.low.x-.03 && low.z<b.high.z+.03 && high.z>b.low.z-.03 && low.y<b.high.y-.05 && high.y>b.low.y+.05;
                    }))return false;
                }return true;
            };
            auto steeringTarget=target;double steeringSpacing=followingRoute?.001:spacing;
            actor.navigationRetry=std::max(0.,actor.navigationRetry-dt);
            if(!fleeing && !displaced && !launched && actor.tetherYank<=0){
                auto toward=target-origin;double length=std::hypot(toward.x,toward.z);auto goal=length>steeringSpacing?target-toward.unit()*steeringSpacing:origin;
                if(routeClear(origin,goal)){actor.navigationPath.clear();}
                else {
                    if((actor.navigationGoal-goal).length()>1){actor.navigationPath.clear();actor.navigationBackoff=.5;}
                    while(!actor.navigationPath.empty() && (actor.navigationPath.front()-origin).length()<.2)actor.navigationPath.erase(actor.navigationPath.begin());
                    if(!actor.navigationPath.empty() && !routeClear(origin,actor.navigationPath.front()))actor.navigationPath.clear();
                    if(actor.navigationPath.empty() && actor.navigationRetry<=0){{const auto astarT0=std::chrono::steady_clock::now();actor.navigationPath=ground_path(origin,goal,routeClear);++astarCalls_;if(actor.navigationPath.empty())++astarFails_;astarMs_+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-astarT0).count();}actor.navigationGoal=goal;actor.navigationBackoff=actor.navigationPath.empty()?std::min(4.,actor.navigationBackoff*2):.5;actor.navigationRetry=actor.navigationBackoff;}
                    while(!actor.navigationPath.empty() && (actor.navigationPath.front()-origin).length()<.2)actor.navigationPath.erase(actor.navigationPath.begin());
                    if(!actor.navigationPath.empty()){steeringTarget=actor.navigationPath.front();steeringSpacing=0;}
                }
            }
            auto delta=actor.steering.step(origin,steeringTarget,dt,(followingRoute?c.Speed:2.5)*actor.control.movement_factor(),steeringSpacing);
            if(displaced){
                auto direction=actor.control.active==ControlEffect::Pull?effectSource-origin:origin-effectSource;direction.y=0;
                double speed=actor.control.active==ControlEffect::Pull?std::min(16.,std::max(0.,direction.length()-1)/dt):6.;
                if(direction.length()<.01)direction={0,0,1};delta=direction.unit()*speed*dt;actor.steering.stop();
            }
            if(launched){
                if(actor.launchPending){auto away=origin-effectSource;away.y=0;if(away.length()<.01)away={0,0,1};actor.launchVelocity=away.unit()*6+Vec3{0,6,0};actor.launchPending=false;}
                actor.launchVelocity.y-=9.81*dt;delta=actor.launchVelocity*dt;actor.steering.stop();
            }
            actor.tetherGuard=std::max(0.,actor.tetherGuard-dt);
            if(tethered){auto offset=origin-effectSource;offset.y=0;
                if(offset.length()>6 && actor.tetherYank<=0 && actor.tetherGuard<=0){actor.tetherYank=.5;actor.tetherGuard=.5;}
                if(actor.tetherYank>0){actor.tetherYank=std::max(0.,actor.tetherYank-dt);delta=offset.unit()*-std::min(16.*dt,std::max(0.,offset.length()-5.5));actor.steering.stop();}
            }else actor.tetherYank=0;
            bool volumeTerrain=terrain_&&!terrain_->Volumes.empty();
            if(!launched && terrain_){
                if(volumeTerrain){auto floor=terrain_body_support(*terrain_,bounds,delta,bounds.low.y);if(floor)delta.y=*floor-bounds.low.y;else{delta={};actor.steering.stop();}}
                else delta.y=terrain_->height_at(c.X+delta.x,c.Z+delta.z)-terrain_->height_at(c.X,c.Z);
            }
            auto obstructed=[&](Vec3 offset){
                auto low=bounds.low+offset,high=bounds.high+offset;
                return anyObstacle(c,low,high,.001,[&](const Character& other,const Bounds& b){
                    if(low.x<b.high.x && high.x>b.low.x && low.z<b.high.z && high.z>b.low.z && low.y<b.high.y-1e-5 && high.y>b.low.y+1e-5){lastPursuitBlocker_=other.Id;++pursuitBlocked_;return true;}
                    return false;
                });
            };
            Vec3 allowed{};bool landed=false;const double mlA=starlight_diag::evnow();
            for(int axis=0;axis<3;axis++){
                Vec3 portion=axis==0?Vec3{delta.x,0,0}:axis==1?Vec3{0,0,delta.z}:Vec3{0,volumeTerrain&&!launched?delta.y-allowed.y:delta.y,0};
                int steps=std::max(1,int(std::ceil(portion.length()/.025)));auto increment=portion*(1./steps);
                for(int i=0;i<steps;i++){
                    auto next=allowed+increment;double floor=terrain_?terrain_->height_at(c.X+next.x,c.Z+next.z):0;
                    if(volumeTerrain){
                        if(!launched&&axis!=2){auto support=terrain_body_support(*terrain_,bounds,next,bounds.low.y+allowed.y);if(!support||std::abs(*support-(bounds.low.y+allowed.y))>increment.length()+.05){actor.steering.stop();break;}next.y=*support-bounds.low.y;}
                        if(!terrain_body_clear(*terrain_,bounds,next)){
                            if(launched){double low=0,high=1;for(int refinement=0;refinement<12;++refinement){double mid=(low+high)*.5;if(terrain_body_clear(*terrain_,bounds,allowed+increment*mid))low=mid;else high=mid;}allowed=allowed+increment*low;if(axis==2){landed=increment.y<0;actor.launchVelocity.y=0;}}
                            actor.steering.stop();break;
                        }
                    }else{
                        if(launched && axis==2 && increment.y<0 && bounds.low.y+next.y<=floor){allowed.y=floor-bounds.low.y;landed=true;break;}
                        if(!launched && terrain_ && std::abs(floor-terrain_->height_at(c.X,c.Z))>.05){actor.steering.stop();break;}
                    }
                    if(obstructed(next)){actor.steering.stop();if(launched && axis==2){landed=increment.y<0;actor.launchVelocity.y=0;}break;}
                    allowed=next;
                }
            }
            mvMoveLoop_+=starlight_diag::evnow()-mlA;delta=allowed;
            if(landed){actor.launchAirborne=false;actor.launchVelocity={};actor.control.remaining=1;}
            if(delta.length()<1e-9)continue;
            c.X+=delta.x;c.Y+=delta.y;c.Z+=delta.z;
            if(!c.CombatAutoAttack && !fleeing && !displaced && !launched){const double tuA=starlight_diag::evnow();turn_actor(c,delta,dt);mvTurn_+=starlight_diag::evnow()-tuA;}
            interactionBounds_[c.Id]=object_bounds(c);++rules_.revision;
            if(pursuitObstaclesReady)for(auto& entry:pursuitObstacles)if(entry.first==&c){entry.second=interactionBounds_[c.Id];break;}
            if(indexedPursuit){
                if(auto slot=spatial_.slotOf.find(c.Id);slot!=spatial_.slotOf.end()){spatial_.pursuit[slot->second]=interactionBounds_[c.Id];spatial_.stamp[slot->second]=spatial_.epoch;}
                touch_spatial(c);
            }
#ifdef STARLIGHT_CHARACTER_CONTROLLER
            if(controller_)controller_->move_actor(c.Id,{c.X,c.Y,c.Z},dt,c.RotationY);
#endif
        }
    }
    void advance_enemy_attacks(double dt){
        if(!rulesEnabled_)return;
        std::vector<std::string> defeated;
        for(auto& c:playScene_.Characters){
            auto it=combatActors_.find(c.Id);if(it==combatActors_.end())continue;
            auto& actor=it->second;
            auto bounds=posed_bounds(c);
            Vec3 origin{(bounds.low.x+bounds.high.x)*.5,bounds.low.y,(bounds.low.z+bounds.high.z)*.5};
            const double tgB=starlight_diag::evnow();auto victim=actor_target(c,actor);mvTarget_+=starlight_diag::evnow()-tgB;
            auto delta=victim?victim->feet-origin:Vec3{};delta.y=0;
            bool eligible=c.CombatAutoAttack && victim.has_value() && actor.health>0 && !rules_.removed.contains(c.Id);
            bool blocked=!eligible || actor.tetherYank>0 || actor.control.blocks_basic() || (actor.boss && actor.bar.phase==BossBreakBar::Phase::Staggered);
            if(eligible && !actor.control.blocks_basic()){const double tuB=starlight_diag::evnow();turn_actor(c,delta,dt);mvTurn_+=starlight_diag::evnow()-tuB;}
            double facing=c.RotationY*.0174532925199433;
            actor.attackOrigin=origin;actor.attackAim={-std::sin(facing),0,-std::cos(facing)}; // Cone and hit share current bounded actor facing.
            actor.attack.press(eligible && delta.length()<=(c.CombatProjectileSpeed>0?20:3));
            if(!actor.attack.advance(dt,blocked,actor.control.firing_factor()))continue;
            auto point=victim->feet+Vec3{0,.85,0};
            Vec3 targetLow=victim->feet-Vec3{.3,0,.3},targetHigh=victim->feet+Vec3{.3,posture_==0?1.8:posture_==2?.5:1.,.3};
            if(!victim->id.empty())for(const auto& target:playScene_.Characters)if(target.Id==victim->id){
                auto targetBounds=posed_bounds(target);targetLow=targetBounds.low;targetHigh=targetBounds.high;break;
            }
            if(c.CombatProjectileSpeed>0){auto eye=origin+Vec3{0,1,0};auto aim=actor.attackAim*std::max(.1,delta.length());aim.y=point.y-eye.y;projectiles_.fire(eye,aim.unit(),c.CombatProjectileSpeed,c.CombatProjectileGravity,c.Id,actor.team,c.CombatProjectileMass,c.CombatControlEffect,actor.control.hit_factor());++shotSerial_;continue;}
            if(!surface_telegraph_contact(terrain_?&*terrain_:nullptr,actor.attackOrigin,actor.attackAim,targetLow,targetHigh,point,3,.7853981633974483))continue;
            auto eye=actor.attackOrigin+Vec3{0,1,0};
            if(unobstructed(eye,point,c.Id,victim->id) && hitChance_.roll(actor.control.hit_factor())){
                if(victim->id.empty()){
                    take_damage(10);
                    if(c.CombatControlEffect>=0 && c.CombatControlEffect<14)apply_control(static_cast<ControlEffect>(c.CombatControlEffect),{combat_identity(c.Id),false},true,false,origin);
                }else if(auto target=combatActors_.find(victim->id);target!=combatActors_.end()){
                    target->second.damage(10,actor.team);
                    if(c.CombatControlEffect>=0 && c.CombatControlEffect<14)target->second.apply(static_cast<ControlEffect>(c.CombatControlEffect),{combat_identity(c.Id),false},actor.team);
                    if(target->second.health<=0){target->second.attack.clear();target->second.steering.stop();defeated.push_back(victim->id);}
                }
            }
        }
        // Complete removal after the scene iteration; fracture and rule actions
        // may change the character list. All damage producers share this finish.
        for(const auto& id:defeated)finish_destruction(id);
        finished=rules_.finished;
    }
    void advance_combat(double dt){
        for(auto& [id,actor]:combatActors_){
            actor.advance(dt);
            if(actor.control.active==ControlEffect::Taunt){
                bool valid=actor.control.source.id==1 && health_>0 && actor.hostile_to(1);
                for(const auto& [otherId,other]:combatActors_)if(combat_identity(otherId)==actor.control.source.id && other.health>0 && actor.hostile_to(other.team) && !rules_.removed.contains(otherId))valid=true;
                if(!valid)actor.control.end();
            }
        }
        const double mvT0=starlight_diag::events_enabled()?starlight_diag::evnow():0;
        advance_pursuit(dt);const double mvT1=mvT0>0?starlight_diag::evnow():0;
        advance_enemy_attacks(dt);const double mvT2=mvT0>0?starlight_diag::evnow():0;
        advance_healing(dt);
        advance_projectiles(dt);
        advance_lightning();const double mvT3=mvT0>0?starlight_diag::evnow():0;
        if(mvT0>0){starlight_diag::evlog("mv1",mvT1-mvT0,mvT2-mvT1,mvT3-mvT2);starlight_diag::evlog("mv2",mvTarget_,mvTurn_,mvMoveLoop_);mvTarget_=mvTurn_=mvMoveLoop_=0;}
        if(!fragments_.empty()||!dust_.puffs().empty()||!dust_.grains().empty()){
            // Obstacles are only read by free-flying particles and dust; rigid-body fragments are driven by the physics world,
            // so a field of finished rock fragments must not pay for rebuilding every object's bounds each update.
            bool needObstacles=!dust_.puffs().empty()||!dust_.grains().empty();
#ifdef STARLIGHT_CHARACTER_CONTROLLER
            for(const auto& f:fragments_)if(!controller_||f.body==UINT32_MAX){needObstacles=true;break;}
#else
            needObstacles=true;
#endif
            std::vector<Bounds> obstacles;
            const double obstacleStart=starlight_diag::events_enabled()?starlight_diag::evnow():0;size_t obstacleMisses=0;double missMs=0,environmentMs=0;
            if(needObstacles){
                obstacles.reserve(playScene_.Characters.size()+fragments_.size()+1);
                for(const auto& object:playScene_.Characters)if(lightning_solid(object)&&!rules_.removed.contains(object.Id)&&object.GameplayRole!="key"&&object.GameplayRole!="exit"){
                    // Objects that never move keep their bounds; everything else is posed as before.
                    if(bounds_cacheable(object)){
                        const double m0=obstacleStart>0?starlight_diag::evnow():0;const auto before=obstacleStart>0?ObjectCache::shared().stats().misses:0;
                        const Bounds cachedBounds=ObjectCache::shared().bounds(object);
                        if(obstacleStart>0&&ObjectCache::shared().stats().misses!=before){missMs+=starlight_diag::evnow()-m0;++obstacleMisses;}
                        {static const bool verify=[]{char value[8]{};return GetEnvironmentVariableA("STARLIGHT_OBSTACLE_VERIFY",value,8)&&value[0]=='1';}();
                         if(verify){const auto fresh=object_bounds(route_pose(object,seconds_));if(std::memcmp(&fresh,&cachedBounds,sizeof(Bounds))!=0)throw std::runtime_error("Cached obstacle bounds differ from the posed bounds of "+object.Id);}}
                        obstacles.push_back(cachedBounds);
                    }else obstacles.push_back(posed_bounds(object));
                }
                const double e0=obstacleStart>0?starlight_diag::evnow():0;
                for(const auto& body:environment_objects())obstacles.push_back(object_bounds(body));
                obstacles.push_back(body_bounds());
                if(obstacleStart>0)environmentMs=starlight_diag::evnow()-e0;
            }
            if(obstacleStart>0)starlight_diag::evlog("obst",double(obstacleMisses),missMs,environmentMs);
            for(auto& f:fragments_){
#ifdef STARLIGHT_CHARACTER_CONTROLLER
                if(controller_ && f.body!=UINT32_MAX){f.age+=dt;controller_->debris_pose(f.body,f.position,f.rotation);if(f.age>=f.lifetime)controller_->remove_debris(f.body);continue;}
#endif
                step_particle(f,dt,terrain_?&*terrain_:nullptr,obstacles);
            }
            {const size_t before=fragments_.size();std::erase_if(fragments_,[](const auto& f){return f.age>=f.lifetime;});if(starlight_diag::events_enabled()&&fragments_.size()!=before)starlight_diag::evlog("frag.erase",double(before-fragments_.size()),double(fragments_.size()));}
            {const double d0=obstacleStart>0?starlight_diag::evnow():0;
            dust_.advance(dt,terrain_?&*terrain_:nullptr,obstacles,{.3+playScene_.RainWindX*.15,0,.15+playScene_.RainWindZ*.15});
            if(obstacleStart>0)starlight_diag::evlog("dust",starlight_diag::evnow()-d0,double(obstacles.size()),double(fragments_.size()));}
        }
        if(!launchAirborne_)control_.advance(dt);
        if(control_.active==ControlEffect::Taunt){
            bool valid=false;for(const auto& [id,a]:combatActors_)if(combat_identity(id)==control_.source.id && a.health>0 && a.hostile_to(1))valid=true;
            if(!valid)control_.end();
        }
        if(!selectedTarget_.empty()){
            bool valid=false;for(const auto& c:playScene_.Characters)if(c.Id==selectedTarget_ && !rules_.removed.contains(c.Id) && perceived(c))valid=true;
            if(!valid)selectedTarget_.clear();
        }
        if(lastDisplayControl_!=control_.active){lastDisplayControl_=control_.active;++rules_.revision;}
        std::unordered_set<std::string> visibleIds;
        for(const auto& c:playScene_.Characters)if(!rules_.removed.contains(c.Id) && perceived(c))visibleIds.insert(c.Id);
        if(visibleIds!=lastVisibleIds_){lastVisibleIds_=std::move(visibleIds);++rules_.revision;}
        if(!rulesEnabled_ || !attack_.advance(dt,health_<=0 || dodging() || tetherPull_>0 || control_.blocks_basic(),control_.firing_factor()))return;
        ++shotSerial_;lastShotTime_=seconds_;
        if(settings_.ProjectileSpeed>0){projectiles_.fire(position-Vec3{0,.7,0},attack_direction(),settings_.ProjectileSpeed,settings_.ProjectileGravity,projectile_player_id,1,settings_.ProjectileMass,-1,control_.hit_factor());advance_projectiles(0);return;}
        std::vector<std::string> hit;
        for(const auto& c:playScene_.Characters)if((combatActors_.contains(c.Id) ? combatActors_.at(c.Id).hostile_to(1) : c.Breakable) && !rules_.removed.contains(c.Id) && perceived(c)){
            auto b=posed_bounds(c);auto point=(b.low+b.high)*.5;
            if(!surface_telegraph_contact(terrain_?&*terrain_:nullptr,position-Vec3{0,1.7,0},attack_direction(),b.low,b.high,point))continue;
            if(unobstructed(position-Vec3{0,.7,0},point,"",c.Id) && hitChance_.roll(control_.hit_factor()))hit.push_back(c.Id);
        }
        apply_attack_hits(hit);
        finished=rules_.finished;
    }
    void advance_lightning(){
        if(!rulesEnabled_)return;
        if(std::none_of(playScene_.Characters.begin(),playScene_.Characters.end(),[](const auto& c){return c.Shape=="chaos-lightning";}))return;
        int64_t cycle=int64_t(std::floor((seconds_-.025)/.22));if(cycle<0)return;
        const bool due=std::any_of(playScene_.Characters.begin(),playScene_.Characters.end(),[&](const auto& c){if(c.Shape!="chaos-lightning"||rules_.removed.contains(c.Id))return false;const auto seen=lightningCycles_.find(c.Id);return seen==lightningCycles_.end()||seen->second<cycle;});
        if(!due)return;
        auto scene=playScene_;std::erase_if(scene.Characters,[&](const auto& c){return rules_.removed.contains(c.Id);});
        auto debris=environment_objects();scene.Characters.insert(scene.Characters.end(),debris.begin(),debris.end());
        for(const auto& emitter:scene.Characters)if(emitter.Shape=="chaos-lightning"){
            auto [it,fresh]=lightningCycles_.try_emplace(emitter.Id,-1);if(it->second>=cycle)continue;it->second=cycle;
            auto playerBox=body_bounds();
            auto arcs=trace_lightning(emitter,scene,seconds_,health_>0?std::optional<Bounds>{playerBox}:std::nullopt);if(arcs.empty())continue;++lightningSerial_;lightningSounds_.push_back({lightningSerial_,arcs.front().target,seconds_});if(lightningSounds_.size()>128)lightningSounds_.erase(lightningSounds_.begin());
            std::unordered_set<std::string> dustContacts;
            for(const auto& arc:arcs)if(arc.contact&&arc.hit!=lightning_player_id&&!combatActors_.contains(arc.hit)){
                auto key=arc.hit.empty()?std::to_string(int(arc.target.x*4))+":"+std::to_string(int(arc.target.y*4))+":"+std::to_string(int(arc.target.z*4)):arc.hit;
                if(!dustContacts.insert(key).second)continue;
                Vec3 tint{.4,.36,.3};bool mineralContact=true;for(const auto& body:scene.Characters)if(body.Id==arc.hit){tint=rgb(body.Color);mineralContact=destruction_has_rubble(body);break;}if(!mineralContact)continue;
                dust_.burst(arc.target,arc.normal,tint,.22,uint32_t(lightningSerial_)*2654435761u,false);
            }
            if(emitter.LightningDamage<=0)continue;
            for(const auto& arc:arcs)if(arc.contact&&arc.hit.empty()&&!arc.branch){damage_terrain(arc.target,arc.normal,emitter.LightningDamage);break;}
            std::unordered_set<std::string> contacted;for(const auto& arc:arcs)if(!arc.hit.empty()&&contacted.insert(arc.hit).second){if(arc.hit==lightning_player_id){if(emitter.CombatTeam!=1&&emitter.CombatTeam!=0)take_damage(emitter.LightningDamage);continue;}for(const auto& body:scene.Characters)if(body.Id==arc.hit&&(body.Breakable||combatActors_.contains(body.Id))){apply_attack_hits({body.Id},emitter.LightningDamage,emitter.CombatTeam);break;}}
        }
    }
    bool damage_terrain(Vec3 point,Vec3 normal,double damage){
        if(!rulesEnabled_||!terrain_)return false;
        auto candidate=*terrain_;auto damageState=terrainDestruction_;if(!damageState.damage(candidate,point,normal,damage)){terrainDestruction_=std::move(damageState);return false;}
        Mesh grappleMesh;
        for(const auto& part:terrain_geometry(candidate)){int offset=int(grappleMesh.vertices.size());grappleMesh.vertices.insert(grappleMesh.vertices.end(),part.mesh.vertices.begin(),part.mesh.vertices.end());for(int index:part.mesh.indices)grappleMesh.indices.push_back(index+offset);}
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        if(controller_)controller_->terrain(candidate);
#endif
        terrain_=std::move(candidate);terrainDestruction_=std::move(damageState);playScene_.Terrain=terrain_;grappleTerrain_=std::move(grappleMesh);
        if(grappleTarget_=="@terrain"&&terrain_->density_at(grapple_.anchor.x,grapple_.anchor.y,grapple_.anchor.z)>.05){grapple_.detach();grappleFalling_=true;}
        dust_.burst(point,normal,{.4,.36,.3},terrain_->DestructionRadius,uint32_t(++impactSerial_)*2654435761u,true);
        ++rules_.revision;return true;
    }
    double destruction_health(const std::string& id)const{
        auto object=std::find_if(playScene_.Characters.begin(),playScene_.Characters.end(),[&](const auto& c){return c.Id==id;});
        if(object==playScene_.Characters.end())return 0;
        if(auto actor=combatActors_.find(id);actor!=combatActors_.end())return actor->second.health;
        return destruction_.remaining(*object);
    }
  private:
    bool finish_destruction(const std::string& id){
        // Idempotence prevents duplicate rubble or quest events if several
        // attacks finish the same actor in one simulation step.
        if(rules_.removed.contains(id)||std::none_of(playScene_.Characters.begin(),playScene_.Characters.end(),[&](const auto& c){return c.Id==id;}))return false;
        for(const auto& c:playScene_.Characters)if(c.Id==id&&c.Breakable&&destruction_has_rubble(c)){auto b=posed_bounds(c);auto center=(b.low+b.high)*.5;
            const auto destructionStart=std::chrono::steady_clock::now();
            auto moved=route_pose_of(c,seconds_);
            FractureResult fracture;
            if(auto ready=obtain_prefracture(c,moved))fracture=std::move(*ready);
            else{++runtime_bypass().fractureComputedAtBreak;fracture=compute_fracture(c,moved,uint32_t(++fracturePatternSerial_));}
            auto physicalPieces=std::move(fracture.pieces);const bool fractureSupported=fracture.supported;uint32_t fractureSeed=fracture.seed;
            lastDestruction.fractureMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-destructionStart).count();
            if(fractureSupported){
                {
                    bool bodyFailureReported=false; // a full physics world must not leave the break half done: the piece still drops, without a body
                    for(auto piece:physicalPieces){
                        Fragment f;f.position=piece.center;f.rotation=fracture.rotation;auto outward=(piece.center-center).unit();f.velocity=outward*2+Vec3{0,3,0};f.radius=piece.radius;f.restitution=c.Restitution;f.part=std::move(piece.part);for(const auto& vertex:f.part->mesh.vertices){auto p=vertex.position;f.localBounds.low={std::min(f.localBounds.low.x,p.x),std::min(f.localBounds.low.y,p.y),std::min(f.localBounds.low.z,p.z)};f.localBounds.high={std::max(f.localBounds.high.x,p.x),std::max(f.localBounds.high.y,p.y),std::max(f.localBounds.high.z,p.z)};}
#ifdef STARLIGHT_CHARACTER_CONTROLLER
                        if(controller_){if(!(piece.hull&&piece.hullDensity==c.Density))++runtime_bypass().hullBuiltAtBreak;const auto hullStart=std::chrono::steady_clock::now();auto motion=controller_->body_motion(c.Id,piece.center);f.velocity=f.velocity+motion.first;try{f.body=controller_->add_debris(f.part->mesh,f.position,f.velocity,c.Density,c.Friction,c.Restitution,motion.second+Vec3{outward.z,1,-outward.x},piece.hull&&piece.hullDensity==c.Density?piece.hull:DebrisHull{},fracture.rotation);}catch(const std::exception& error){f.body=UINT32_MAX;if(!bodyFailureReported){bodyFailureReported=true;std::fprintf(stderr,"break: a fragment of %s has no physics body and falls as a plain particle: %s\n",c.Name.c_str(),error.what());}}lastDestruction.hullMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-hullStart).count();}
#endif
                        f.serial=++fragmentSerial_;fragments_.push_back(std::move(f));
                    }
                }
            }else for(int i=0;i<16;i++){double angle=i*2.3999632297;Fragment f;f.position=center;f.velocity={std::cos(angle)*(1+i%3),2.+i%4,std::sin(angle)*(1+i%3)};f.serial=++fragmentSerial_;fragments_.push_back(std::move(f));}
            dust_.burst(center,{0,1,0},rgb(c.Color),std::clamp((b.high-b.low).length()*.35,.15,3.),fractureSeed,true);
            starlight_diag::evlog("brk.fin",lastDestruction.fractureMs,lastDestruction.hullMs,double(fragments_.size()));
            if(fragments_.size()>256){auto excess=fragments_.size()-256;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
                if(controller_)for(size_t i=0;i<excess;++i)if(fragments_[i].body!=UINT32_MAX)controller_->remove_debris(fragments_[i].body);
#endif
                fragments_.erase(fragments_.begin(),fragments_.begin()+excess);
            }break;
        }
        if(auto actor=combatActors_.find(id);actor!=combatActors_.end()){
            actor->second.attack.clear();actor->second.steering.stop();
        }
        rules_.remove_object(playScene_,id);rules_.emit("object-broken",id,playScene_);if(selectedTarget_==id)selectedTarget_.clear();
        ++shatterSerial_;
        return true;
    }
  public:
    struct BossDebug{double rotationY=0,distance=0,facingErrorDeg=0,windup=0;bool winding=false;size_t pathSize=0;std::string pursuitBlocker;uint64_t pursuitBlocked=0;uint64_t turnApplied=0,turnBlocked=0,astarCalls=0,astarFails=0;double astarMs=0;std::string turnBlocker;bool found=false;};
    BossDebug boss_debug(const std::string& id)const{
        BossDebug d;d.turnApplied=turnApplied_;d.turnBlocked=turnBlocked_;d.astarCalls=astarCalls_;d.astarFails=astarFails_;d.astarMs=astarMs_;d.turnBlocker=lastTurnBlocker_;d.pursuitBlocker=lastPursuitBlocker_;d.pursuitBlocked=pursuitBlocked_;
        auto actor=combatActors_.find(id);if(actor==combatActors_.end())return d;
        for(const auto& c:playScene_.Characters)if(c.Id==id){
            d.found=true;d.rotationY=c.RotationY;auto toPlayer=position-Vec3{c.X,position.y,c.Z};toPlayer.y=0;d.distance=toPlayer.length();
            const double f=c.RotationY*.0174532925199433;const Vec3 aim{-std::sin(f),0,-std::cos(f)};
            if(d.distance>1e-6){double dot=std::clamp(aim.dot(toPlayer.unit()),-1.,1.);d.facingErrorDeg=std::acos(dot)*57.29577951308232;}
            break;
        }
        d.windup=actor->second.attack.windup;d.winding=actor->second.attack.winding;d.pathSize=actor->second.navigationPath.size();
        return d;
    }
    // ---- Scene preparation (shared loading path). The host (editor Play, player) calls these before gameplay starts, while it shows the
    // loading screen; each reports real completed work through `fraction`. Everything they build goes into the engine-wide caches, so a
    // scene that is started again, or an object added later, reuses or lazily refreshes the same data.
    // Warms the shared object cache for every object in the scene: world bounds, the collision mesh and the ray tree of each distinct geometry (statics, route
    // platforms, actors and plain boxes alike), pins them so eviction cannot discard them, and builds the shared candidate index. The ray-tree memory is part of
    // the cache's geometry budget; geometry_report() lists what was prepared.
    GeometryReport geometry_report()const{return geometryReport_;}
    void prepare_geometry(std::atomic<double>& fraction){
        auto& cache=ObjectCache::shared();
        cache.begin_preparation();
        release_pins();pinOwner_=cache.new_pin_owner();
        struct Item {const Character* object;bool bounds;};
        std::vector<Item> work;work.reserve(playScene_.Characters.size());
        for(const auto& c:playScene_.Characters)work.push_back({&c,bounds_cacheable(c)&&!combatActors_.contains(c.Id)&&(c.Sculpt||!(c.Shape&&*c.Shape=="cube"))});
        geometryReport_={};geometryReport_.objects=work.size();
        fraction=work.empty()?1.:0.;
        if(work.empty())return;
        std::atomic<size_t> next{0},done{0},treeBytes{0};
        std::exception_ptr failure;std::mutex failureLock;
        const unsigned threads=std::max(1u,std::min<unsigned>(8,std::thread::hardware_concurrency()?std::thread::hardware_concurrency()/2:2));
        std::vector<std::thread> pool;
        struct JoinAll{std::vector<std::thread>& threads;std::atomic<size_t>& next;size_t end;~JoinAll(){next=end;for(auto& worker:threads)if(worker.joinable())worker.join();}} joinAll{pool,next,work.size()};
        for(unsigned k=0;k<threads;++k)pool.emplace_back([&]{
            try{
                for(size_t i;(i=next.fetch_add(1))<work.size();){
                    const Character& c=*work[i].object;
                    cache.pin(pinOwner_,c,work[i].bounds);
                    if(work[i].bounds)(void)cache.bounds(c);
                    const auto warmed=cache.warm_query_geometry(c);
                    if(warmed.builtTree)treeBytes+=warmed.treeBytes;
                    fraction=double(done.fetch_add(1)+1)/double(work.size());
                }
            }catch(...){std::lock_guard guard(failureLock);if(!failure)failure=std::current_exception();next=work.size();}
        });
        for(auto& worker:pool)worker.join();
        if(failure)std::rethrow_exception(failure);
        for(const auto& item:work)if(!item.bounds)(void)posed_bounds(*item.object);
        if(spatial_active())refresh_spatial(true);
        const auto stats=cache.stats();
        geometryReport_.distinctGeometries=stats.geometries;geometryReport_.rayTrees=stats.rayTrees;geometryReport_.rayTreeBytes=stats.rayTreeBytes;geometryReport_.geometryBytes=stats.geometryBytes;
        geometryReport_.pinnedGeometries=stats.pinnedGeometries;geometryReport_.pinnedBytes=stats.pinnedBytes;geometryReport_.treeBytesBuiltHere=treeBytes.load();geometryReport_.overBudget=stats.overBudget;
        fraction=1.;
    }
    // Builds the collision world now instead of on the first movement update.
    void prepare_collision(std::atomic<double>& fraction){
        fraction=0.;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        if(settings_.Collision&&!controller_)controller_=std::make_unique<AdventureController>(playScene_,position);
#endif
        ObjectCache::shared().mark_prepared();
        fraction=1.;
    }
    // Break-ahead state of every declared destructible: prepared (a valid result), failed (with the reason), abandoned (taken for work at the break) or still
    // queued/running. `ready` counts valid prepared results only; the work is finished when settled() is true, and ok() says every one of them is valid.
    struct FractureProgress{
        size_t ready=0,total=0,prepared=0,failed=0,abandoned=0;
        std::vector<std::string> failures,pending;
        bool settled()const{return prepared+failed+abandoned>=total;}
        bool ok()const{return prepared==total;}
    };
    // Entries that still count against the break-ahead cap: queued, running or finished objects that are still in the scene. Failed or abandoned entries and entries of removed objects hold no result worth keeping a slot for.
    size_t prefracture_live(){
        std::lock_guard lock(preMutex_);size_t live=0;
        for(const auto& [id,entry]:preFractures_)if(entry->state<=2&&!rules_.removed.contains(id))++live;
        return live;
    }
    FractureProgress fracture_progress(){
        FractureProgress p;std::lock_guard lock(preMutex_);
        for(const auto& [id,entry]:preFractures_){
            ++p.total;
            if(entry->state==2)++p.prepared;
            else if(entry->state==0||entry->state==1)p.pending.push_back(entry->source.Id+(entry->state==1?" (running)":" (queued)"));
            else if(entry->state==3)++p.abandoned;
            else if(entry->state==4){++p.failed;p.failures.push_back(entry->source.Name+" ("+entry->source.Id+"): "+entry->reason);}
        }
        p.ready=p.prepared;
        return p;
    }

    size_t fragment_count()const{return fragments_.size();}
    // Pieces ever created; unlike fragment_count it does not stop growing when the resident list is full (256) and the oldest pieces are dropped.
    uint64_t fragments_created()const{return fragmentSerial_;}
    size_t dust_count()const{return dust_.puffs().size()+dust_.grains().size();}
    int break_count()const{return breakOrdinal_;}
    void apply_attack_hits(const std::vector<std::string>& hit,double damage=10,int sourceTeam=1){
        if(!rulesEnabled_||!std::isfinite(damage)||damage<=0)return;
        std::unordered_set<std::string> visited;
        for(const auto& id:hit){
            if(rules_.removed.contains(id)||!visited.insert(id).second)continue;
            auto object=std::find_if(playScene_.Characters.begin(),playScene_.Characters.end(),[&](const auto& c){return c.Id==id;});
            if(object==playScene_.Characters.end())continue;
            if(auto it=combatActors_.find(id);it!=combatActors_.end()){
                if(!it->second.hostile_to(sourceTeam))continue;
                it->second.damage(damage,sourceTeam);if(it->second.health>0)continue;
            }else if(!destruction_.damage(*object,damage))continue;
            starlight_diag::evlog("brk.hit",double(++breakOrdinal_));
            finish_destruction(id);
        }
        finished=rules_.finished;
    }
    void advance_projectiles(double dt){
        if(projectiles_.shots().empty()){projectileBounds_.clear();return;}
        std::vector<ProjectileTarget> targets;
        // Each id keeps its bounds from the previous update for the sweep; ids not seen this update (removed objects, expired debris) are dropped below.
        const uint64_t pass=++projectileBoundsPass_;
        auto track=[&](const std::string& id,const Bounds& after){auto [it,fresh]=projectileBounds_.try_emplace(id,after,pass);targets.push_back({id,fresh?after:it->second.first,after});it->second={after,pass};};
        for(const auto& object:playScene_.Characters){if(rules_.removed.contains(object.Id)||(!object.Breakable&&!lightning_solid(object)))continue;
            track(object.Id,posed_bounds(object));
        }
        for(const auto& c:environment_objects())track(c.Id,object_bounds(c));
        if(health_>0)track(projectile_player_id,body_bounds());
        std::erase_if(projectileBounds_,[&](const auto& entry){return entry.second.second!=pass;});
        for(const auto& impact:projectiles_.step(dt,terrain_?&*terrain_:nullptr,targets)){
#ifdef STARLIGHT_CHARACTER_CONTROLLER
            if(controller_&&!impact.target.empty()){controller_->impulse(impact.target,impact.position,impact.velocity*impact.mass);for(const auto& fragment:fragments_)if(fragment.body!=UINT32_MAX&&impact.target=="debris-"+std::to_string(fragment.serial))controller_->impulse_body(fragment.body,impact.position,impact.velocity*impact.mass);}
#endif
            ++impactSerial_;
            Vec3 impactTint{.4,.36,.3};bool mineralImpact=true;for(const auto& object:playScene_.Characters)if(object.Id==impact.target){impactTint=rgb(object.Color);mineralImpact=destruction_has_rubble(object);break;}
            if(mineralImpact&&impact.target!=projectile_player_id&&!combatActors_.contains(impact.target))dust_.burst(impact.position,impact.normal,impactTint,.18,uint32_t(impactSerial_)*2654435761u,false);
            static const auto sparkPart=[](){Character c;c.Shape="cube";auto part=std::make_shared<MeshPart>(shape_geometry(c).front());part->color={1,.65,.15};part->emissive=true;for(auto& v:part->mesh.vertices)v.position=(v.position-Vec3{0,.5,0})*.025;return part;}();
            for(int i=0;i<6&&fragments_.size()<256;++i){Fragment f;f.position=impact.position+impact.normal*.05;double angle=i*2.3999632297;f.velocity=impact.normal*1.5+Vec3{std::cos(angle),.5+double(i%3)*.5,std::sin(angle)};f.radius=.015;f.lifetime=.4;f.part=sparkPart;f.serial=++fragmentSerial_;fragments_.push_back(std::move(f));}
            if(impact.target.empty()){damage_terrain(impact.position,impact.normal,impact.damage);continue;}
            if(!hitChance_.roll(impact.accuracy))continue;
            if(impact.target==projectile_player_id){if(impact.team!=0&&impact.team!=1){take_damage(impact.damage);if(impact.control>=0)apply_control(static_cast<ControlEffect>(impact.control),{combat_identity(impact.source),false},true,false,impact.position);}continue;}
            for(const auto& object:playScene_.Characters)if(object.Id==impact.target&&(combatActors_.contains(object.Id)?combatActors_.at(object.Id).hostile_to(impact.team):object.Breakable)){if(impact.control>=0)if(auto actor=combatActors_.find(object.Id);actor!=combatActors_.end())actor->second.apply(static_cast<ControlEffect>(impact.control),{combat_identity(impact.source),false},impact.team);apply_attack_hits({object.Id},impact.damage,impact.team);break;}
        }
        finished=rules_.finished;
    }
    std::vector<Character> environment_objects()const{
        std::vector<Character> result;for(const auto& f:fragments_)if(f.part&&std::isfinite(f.localBounds.low.x)){
            Bounds box;Vec3 q{f.rotation[0],f.rotation[1],f.rotation[2]};for(double x:{f.localBounds.low.x,f.localBounds.high.x})for(double y:{f.localBounds.low.y,f.localBounds.high.y})for(double z:{f.localBounds.low.z,f.localBounds.high.z}){Vec3 p{x,y,z};p=p+q.cross(q.cross(p)+p*f.rotation[3])*2+f.position;box.low={std::min(box.low.x,p.x),std::min(box.low.y,p.y),std::min(box.low.z,p.z)};box.high={std::max(box.high.x,p.x),std::max(box.high.y,p.y),std::max(box.high.z,p.z)};}
            Character c;c.Id="debris-"+std::to_string(f.serial);c.Shape="cube";c.Dynamic=true;c.X=(box.low.x+box.high.x)*.5;c.Y=box.low.y;c.Z=(box.low.z+box.high.z)*.5;c.Width=std::max(.001,box.high.x-box.low.x);c.Height=std::max(.001,box.high.y-box.low.y);c.Depth=std::max(.001,box.high.z-box.low.z);result.push_back(std::move(c));
        }return result;
    }
    std::vector<std::pair<std::string,std::array<double,7>>> debris_poses()const{
        std::vector<std::pair<std::string,std::array<double,7>>> out;out.reserve(fragments_.size());
        for(const auto& f:fragments_)out.push_back({"debris-"+std::to_string(f.serial),{f.position.x,f.position.y,f.position.z,f.rotation[0],f.rotation[1],f.rotation[2],f.rotation[3]}});
        for(const auto& p:projectiles_.shots())out.push_back({"projectile-"+std::to_string(p.serial),{p.position.x,p.position.y,p.position.z,0,0,0,1}});
        return out;
    }
    void liquids(std::shared_ptr<const LiquidSnapshot> value){liquidSnapshot_=std::move(value);}
    double yaw=0,pitch=0;
    int posture() const {return posture_;} // 0 standing, 1 crouched, 2 prone, 3 sliding
    // Asset-independent animation input, derived from actual controller movement.
    // Reading presentation never changes simulation or the authored scene.
    struct Presentation {
        enum class Motion { Idle, Walk, Sprint, Jump, Fall, Crouch, Crawl, Slide, Dodge, Lunge, GrapplePull, WallHold, Climb, Swing };
        Motion motion=Motion::Idle;
        Vec3 feet{},velocity{},tetherAnchor{};
        double facing=0,bodyHeight=1.8;
        bool showBody=true,showTether=false;
        bool showWeapons=false,audioActive=false;
        Vec3 eye{},viewAim{0,0,-1},viewUp{0,1,0};
        uint64_t epoch=0,shots=0,shatters=0,impacts=0,lightning=0;
        double shotFlash=0;
        bool showTelegraph=false;
        const TerrainData* telegraphTerrain=nullptr;
        double channelProgress=0;
        double telegraphRange=basicAttackRange,telegraphHalfAngle=basicAttackHalfAngle;
        TelegraphKind telegraphKind=TelegraphKind::Friendly;
        Vec3 attackOrigin{},attackAim{};
        std::optional<Bounds> targetBounds;
        struct FragmentView {Vec3 position;std::shared_ptr<const MeshPart> part;std::array<double,4> rotation;uint64_t serial=0;};
        std::vector<FragmentView> fragments;
        std::vector<DustPuff> dust;
        std::vector<DustGrain> grit;
        std::vector<PhysicalProjectile> projectiles;
        std::vector<LightningSoundEvent> lightningSounds;
        struct ActorTelegraph {Vec3 origin,aim;double progress;TelegraphKind kind;std::string identity;};
        uint64_t telegraphIdentity=1;
        std::vector<ActorTelegraph> actorTelegraphs;
        std::optional<ControlEffect> control;
        double poseTime=0;
    };
    Presentation presentation() const {
        Presentation p;p.telegraphTerrain=terrain_?&*terrain_:nullptr;
        p.showWeapons=rulesEnabled_ && health_>0;p.audioActive=health_>0;
        p.eye=camera_eye();p.viewAim=camera_direction();p.viewUp=camera_up();
        p.epoch=presentationEpoch_;p.shots=shotSerial_;p.shatters=shatterSerial_;p.impacts=impactSerial_;p.lightning=lightningSerial_;
        p.shotFlash=health_>0 && lastShotTime_>=0?std::clamp(1-(seconds_-lastShotTime_)/.12,0.,1.):0;
        p.control=control_.active;p.poseTime=seconds_;
        auto casterVisible=[&](const std::string& id){if(id.empty())return true;for(const auto& c:playScene_.Characters)if(c.Id==id)return perceived(c);return false;};
        for(const auto& h:healingCasts_)if(h.cycle.winding && casterVisible(h.caster))p.actorTelegraphs.push_back({h.origin,h.aim,std::clamp(1-h.cycle.windup/h.cycle.cycleSeconds,0.,1.),telegraph_kind(h.team==1,true),h.caster+"-healing"});
        for(const auto& [id,actor]:combatActors_)if(casterVisible(id) && actor.attack.winding && actor.health>0 && !rules_.removed.contains(id))
            p.actorTelegraphs.push_back({actor.attackOrigin,actor.attackAim,std::clamp(1-actor.attack.windup/actor.attack.cycleSeconds,0.,1.),telegraph_kind(actor.team==1,false),id+"-attack"});
        p.feet=position-Vec3{0,1.7,0};p.velocity=presentationVelocity_;p.facing=yaw;
        p.bodyHeight=posture_==0?1.8:posture_==2?.5:1.;p.showBody=cameraDistance>.001;
        p.showTether=grapple_.state!=GrappleMotion::State::Free;p.tetherAnchor=grapple_.hook_position();
        p.showTelegraph=rulesEnabled_ && attack_.winding;p.attackOrigin=p.feet;p.attackAim=attack_direction();
        p.channelProgress=attack_.winding?std::clamp(1-attack_.windup/attack_.cycleSeconds,0.,1.):0;
        p.projectiles=projectiles_.shots();p.lightningSounds=lightningSounds_;
        for(const auto& f:fragments_)p.fragments.push_back({f.position,f.part,f.rotation,f.serial});
        p.dust=dust_.puffs();p.grit=dust_.grains();
        for(const auto& c:playScene_.Characters)if(c.Id==selectedTarget_ && perceived(c)){p.targetBounds=posed_bounds(c);break;}
        using M=Presentation::Motion;
        const double horizontal=std::hypot(p.velocity.x,p.velocity.z);
        if(dodging())p.motion=M::Dodge;
        else if(lunging())p.motion=M::Lunge;
        else if(grapple_.state==GrappleMotion::State::Pulling)p.motion=M::GrapplePull;
        else if(grapple_.state==GrappleMotion::State::Swing)p.motion=M::Swing;
        else if(grapple_.state==GrappleMotion::State::Wall)p.motion=std::abs(p.velocity.y)>.1?M::Climb:M::WallHold;
        else if(!grounded())p.motion=p.velocity.y>.1?M::Jump:M::Fall;
        else if(posture_==3)p.motion=M::Slide;
        else if(posture_==2)p.motion=M::Crawl;
        else if(posture_==1)p.motion=M::Crouch;
        else if(horizontal>.1)p.motion=horizontal>settings_.MoveSpeed*1.1?M::Sprint:M::Walk;
        return p;
    }
    bool set_posture(int value){
        if(value<0 || value>3)return false;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        if(settings_.Collision){
            if(!controller_)controller_=std::make_unique<AdventureController>(playScene_,position);
            if(!controller_->height(value==0?1.8:value==2?.5:1.))return false;
        }
#endif
        posture_=value;return true;
    }
    void posture_button(bool down,double right,double forward,double elapsed=0){
        if(health_<=0 || control_.blocks_mobility())return;
        if(down){
            if(!controlHeld_){
                controlHeld_=true;controlConsumed_=false;controlTime_=0;
                double length=std::hypot(right,forward);
                if(length>.1 && posture_==0 && grounded() && set_posture(3)){
                    slideVelocity_={(right*std::cos(yaw)+forward*std::sin(yaw))*8/length,0,(right*std::sin(yaw)-forward*std::cos(yaw))*8/length};
                    slideRemaining_=.6;controlConsumed_=true;
                }
            }
            if(std::isfinite(elapsed) && elapsed>0)controlTime_+=elapsed;
            if(!controlConsumed_ && controlTime_>=.35 && std::hypot(right,forward)<.1){set_posture(2);controlConsumed_=true;}
        }else if(controlHeld_){
            if(!controlConsumed_)set_posture(posture_==0?1:0);
            controlHeld_=false;controlConsumed_=false;controlTime_=0;
        }
    }
    Vec3 view_position() const {return position+Vec3{0,posture_==0?0:posture_==2?-1.3:-.8,0};}
    double cameraYaw=0,cameraPitch=0,cameraDistance=5;
    void zoom(double steps){if(std::isfinite(steps)){cameraDistance=std::clamp(cameraDistance-steps*.5,0.,12.);if(cameraDistance<=.15)cameraDistance=0;}}
    void orbit_camera(double horizontal,double vertical){
        if(!std::isfinite(horizontal)||!std::isfinite(vertical))return;
        const double factor=settings_.LookSensitivity*.0174532925199433;
        cameraYaw=std::remainder(cameraYaw+horizontal*factor,6.283185307179586);
        cameraPitch=std::clamp(cameraPitch-vertical*factor,-1.3962634015954636,1.3962634015954636);
    }
    Vec3 camera_direction() const {
        double reaction=0;
        if(cameraDistance<=.001 && !settings_.ReducedCameraMotion){
            if(control_.active==ControlEffect::Launch && !launchAirborne_)reaction=1.2*std::clamp(control_.remaining,0.,1.);
            else if(control_.active==ControlEffect::Knockback || control_.active==ControlEffect::Pull)reaction=.15*std::sin(control_.remaining*35)*std::clamp(control_.remaining*2,0.,1.);
            else if(control_.active==ControlEffect::Knockdown)reaction=-.35*std::sin(std::clamp(control_.remaining,0.,1.)*3.141592653589793);
        }
        const double angle=std::clamp(cameraPitch+reaction,-1.55,1.55);
        return {std::sin(cameraYaw)*std::cos(angle),std::sin(angle),-std::cos(cameraYaw)*std::cos(angle)};
    }
    Vec3 attack_direction()const{return grounded()?Vec3{std::sin(yaw),0,-std::cos(yaw)}:Vec3{std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};}
    Vec3 camera_up() const {
        auto forward=camera_direction();auto right=forward.cross({0,1,0}).unit();auto up=right.cross(forward);
        if(cameraDistance>.001 || !dodging() || settings_.ReducedCameraMotion)return up;
        const double phase=std::clamp(1.-dodgeRemaining_/.25,0.,1.);
        const double angle=dodgeRollSign_*6.283185307179586*(phase*phase*(3-2*phase));
        return up*std::cos(angle)+right*std::sin(angle);
    }
    Vec3 camera_eye() const {
        const double camT0=starlight_diag::events_enabled()?starlight_diag::evnow():0;
        struct CamLog{double t0;~CamLog(){if(t0>0){const double ms=starlight_diag::evnow()-t0;if(ms>.2)starlight_diag::evlog("cam",ms,0,0);}}} camLog{camT0};
        if(cameraDistance<=.001)return view_position();
        auto backward=camera_direction()*-1.;double distance=cameraDistance;
        each_ray_candidate(view_position(),backward,cameraDistance+.2,[&](const Character& object){
            if(repair_cameraboom()?!blocks_movement(object):(object.GameplayRole=="key" || object.GameplayRole=="exit"))return; // repaired: keys, exits, grass, lights, fire, lava, lightning and smoke never pull the boom in; switched off, only keys and exits are skipped
            // Session-start bounds come from every actual mesh vertex. Static
            // objects outside the finite boom cannot change its exact triangle
            // hit. Moving/routed/combat actors retain the original query path.
            if(repair_bounds()){
                // Same test for every object, moving or not: the shared cache returns current bounds (it re-measures an object only when it
                // has changed), so objects the editor marks Dynamic, such as every rock and boulder, are skipped when the boom cannot reach them.
                if(!ray_reaches_bounds(view_position(),backward,cameraDistance+.2,posed_bounds(object)))return;
            }else if(!object.Dynamic && object.Route.empty() && object.CombatHealth<=0){
                auto cached=interactionBounds_.find(object.Id);
                if(cached!=interactionBounds_.end()){
                    const auto& box=cached->second;auto origin=view_position();
                    double enter=0,leave=cameraDistance+.2;bool outside=false;
                    for(auto axis:std::array<std::array<double,4>,3>{{
                        {origin.x,backward.x,box.low.x,box.high.x},
                        {origin.y,backward.y,box.low.y,box.high.y},
                        {origin.z,backward.z,box.low.z,box.high.z}}}){
                        if(!std::isfinite(axis[2])||!std::isfinite(axis[3])){outside=false;break;}
                        double padding=1e-7*(1+std::max({std::abs(axis[0]),std::abs(axis[2]),std::abs(axis[3])}));
                        double low=axis[2]-padding,high=axis[3]+padding;
                        if(axis[1]==0){if(axis[0]<low||axis[0]>high){outside=true;break;}}
                        else{double a=(low-axis[0])/axis[1],b=(high-axis[0])/axis[1];if(a>b)std::swap(a,b);
                            enter=std::max(enter,a);leave=std::min(leave,b);if(enter>leave){outside=true;break;}}
                    }
                    if(outside)return;
                }
            }
            auto hit=object_hit(object,route_pose_of(object,seconds_),view_position(),backward);
            if(hit)distance=std::min(distance,std::max(0.,hit->distance-.2));
        });
        // Sample the short camera boom against terrain to avoid looking through hills.
        for(double d=.1;d<=distance;d+=.1){auto p=view_position()+backward*d;
            if(terrain_ && (terrain_->Volumes.empty()?p.y<terrain_->height_at(p.x,p.z)+.15:terrain_->density_at(p.x,p.y,p.z)<.15)){distance=std::max(0.,d-.1);break;}}
        return view_position()+backward*distance;
    }
    bool standing_inside_solid() const {return blocked(position);}
    bool solid_at(Vec3 p,bool indexed) const {return blocked_with(p,indexed);}
    void rebuild_for_authoring(const SceneData& scene){
        auto savedPosition=position;double savedYaw=yaw,savedPitch=pitch,savedCameraYaw=cameraYaw,savedCameraPitch=cameraPitch,savedDistance=cameraDistance;
        start(scene,false);position=savedPosition;yaw=savedYaw;pitch=savedPitch;cameraYaw=savedCameraYaw;cameraPitch=savedCameraPitch;cameraDistance=savedDistance;
    }
    const GameplayRulesRuntime& rules() const {return rules_;}
    double seconds() const {return seconds_;}
    void jump(){
        if(health_<=0 || control_.blocks_mobility())return;
        if(grapple_.state==GrappleMotion::State::Wall){grapple_.wall_jump();grappleFalling_=true;return;}
        if(posture_!=0 && !set_posture(0))return;
        jumpRequested_=true;
    }
    const GrappleMotion& grapple_motion() const {return grapple_;}
    void grapple_button(bool down){
        if(!down){auto before=grapple_.state;if(grapple_.flying())grapple_.velocity=presentationVelocity_;grapple_.release();if(before!=GrappleMotion::State::Free && grapple_.state==GrappleMotion::State::Free)grappleFalling_=true;return;}
        if(health_<=0 || control_.blocks_mobility())return;
        if(grapple_.held)return;
        std::optional<MeshHit> nearest;std::string id;Vec3 offset{},localNormal{};int grappleMode=0;
        each_ray_candidate(view_position(),camera_direction(),25,[&](const Character& object){
            if(rules_.removed.contains(object.Id) || (object.Shape && (*object.Shape=="light" || *object.Shape=="fire" || *object.Shape=="craxis-smoke")))return;
            auto moved=route_pose_of(object,seconds_);
            auto hit=object_hit(object,moved,view_position(),camera_direction());
            if(hit && hit->distance<=25 && (!nearest || hit->distance<nearest->distance)){nearest=hit;id=object.Id;grappleMode=object.GrappleMode;offset=local_point(moved,hit->point);localNormal=local_point(moved,hit->point+hit->normal)-offset;}
        });
        if(!grappleTerrain_.indices.empty()){
            Character ground;auto hit=mesh_hit(grappleTerrain_,ground,view_position(),camera_direction());
            if(hit && hit->distance<=25 && (!nearest || hit->distance<nearest->distance)){nearest=hit;id="@terrain";grappleMode=0;offset=hit->point;localNormal=hit->normal;}
        }
        if(grapple_.press(movement_,nearest.has_value(),position,nearest?nearest->point:Vec3{},nearest?nearest->normal:Vec3{0,1,0},nearest && (grappleMode==2 || (grappleMode==0 && nearest->normal.y<-.5)))){
            grapple_.velocity=presentationVelocity_;grappleBlockedSeconds_=0;grappleTarget_=id;grappleOffset_=offset;grappleLocalNormal_=localNormal;grappleFalling_=false;mantleRemaining_=0;
        }
    }
    void sprint(bool held){sprintHeld_=held;}
    const MovementResources& movement_resources() const {return movement_;}
    void mobility_button(bool down,double right,double forward,double elapsed=0){
        if(down){
            if(!vHeld_){vHeld_=true;vConsumed_=false;vTime_=0;vRight_=right;vForward_=forward;}
            if(std::isfinite(elapsed) && elapsed>0)vTime_+=elapsed;
            if(!vConsumed_ && vTime_>=.22){
                vConsumed_=true;
                if(std::hypot(right,forward)>.01)lunge();
            }
        }else if(vHeld_){
            if(!vConsumed_)dodge(vRight_,vForward_);
            vHeld_=false;vConsumed_=false;vTime_=0;
        }
    }
    void cancel_mobility_input(){for(auto& h:healingCasts_)if(h.caster.empty())h.cycle.clear();attack_.clear();grapple_.detach();grapple_.held=false;grappleFalling_=false;vHeld_=false;vConsumed_=false;vTime_=0;controlHeld_=false;controlConsumed_=false;controlTime_=0;sprintHeld_=false;jumpRequested_=false;lastMovementPress_.fill(-1);}
    bool lunge(){
        if(health_<=0 || !grounded() || control_.blocks_mobility())return false;
        if(dodgeRemaining_>0 || !movement_.spend_dodge(2))return false;
        dodgeVelocity_={std::sin(yaw)*12,0,-std::cos(yaw)*12};
        dodgeRemaining_=.5;lunging_=true;jumpRequested_=true;return true;
    }
    bool dodging() const {return dodgeRemaining_>0 && !lunging_;}
    bool lunging() const {return dodgeRemaining_>0 && lunging_;}
    bool dodge(double right,double forward){
        if(health_<=0 || control_.blocks_mobility())return false;
        if(dodgeRemaining_>0 || !std::isfinite(right) || !std::isfinite(forward))return false;
        double length=std::hypot(right,forward);
        if(length<.001){right=0;forward=1;length=1;}
        if(!movement_.spend_dodge())return false;
        grapple_.detach();grappleFalling_=false;
        // Initial tuning: three metres over 0.25 seconds, collision-resolved.
        dodgeVelocity_={(right*std::cos(yaw)+forward*std::sin(yaw))*12/length,0,
                        (right*std::sin(yaw)-forward*std::cos(yaw))*12/length};
        dodgeRemaining_=.25;dodgeRollSign_=right<0?-1:1;lunging_=false;return true;
    }
    void movement_press(int key,double now){
        int i=key=='W'?0:key=='A'?1:key=='S'?2:key=='D'?3:-1;
        if(i<0 || !std::isfinite(now))return;
        const double elapsed=now-lastMovementPress_[i];
        if(lastMovementPress_[i]>=0 && elapsed>=0 && elapsed<=.25){
            lastMovementPress_[i]=-1;
            dodge(i==1?-1:i==3?1:0,i==0?1:i==2?-1:0);
        }else lastMovementPress_[i]=now;
    }
    bool grounded() const {
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        return controller_ && controller_->grounded;
#else
        return false;
#endif
    }
    void update_rules(){if(rulesEnabled_){
        rules_.areas(position,playScene_);
        if(grounded()){
            std::vector<std::string> supports;
            for(const auto& object:playScene_.Characters){const auto& b=interactionBounds_.at(object.Id);
                if(position.x>=b.low.x && position.x<=b.high.x && position.z>=b.low.z && position.z<=b.high.z && std::abs(position.y-1.7-b.high.y)<.15)supports.push_back(object.Id);}
            for(const auto& id:supports)rules_.emit("stand-on-object",id,playScene_);
        }
        finished=rules_.finished;
    }}
    void start(const SceneData& scene,bool gameplayRules=true){
        if(!scene.Game)throw std::runtime_error("Choose an adventure preset first");
        ObjectCache::shared().begin_preparation();release_pins();spatial_=SpatialState{};solidIndex_.clear();solidSlot_.clear();geometryReport_={};
        lightningCycles_.clear();lightningSerial_=0;lightningSounds_.clear();
        ++presentationEpoch_;shotSerial_=shatterSerial_=impactSerial_=0;lastShotTime_=-1;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        controller_.reset();liquidSnapshot_.reset();
#endif
        projectiles_.clear();projectileBounds_.clear();grappleBlockedSeconds_=0;grapple_.reset();grappleTarget_.clear();grappleFalling_=false;
        grappleTerrain_={};mantleRemaining_=0;
        if(scene.Terrain)for(const auto& part:terrain_geometry(*scene.Terrain)){int offset=int(grappleTerrain_.vertices.size());grappleTerrain_.vertices.insert(grappleTerrain_.vertices.end(),part.mesh.vertices.begin(),part.mesh.vertices.end());for(int index:part.mesh.indices)grappleTerrain_.indices.push_back(index+offset);}
        seconds_=0;jumpRequested_=false;sprintHeld_=false;movement_={};
        dodgeRemaining_=0;dodgeVelocity_={};presentationVelocity_={};lastMovementPress_.fill(-1);
        cancel_mobility_input();attack_={};hitChance_={};lastDisplayControl_.reset();lastVisibleIds_.clear();control_={};control_.player=true;health_=100;controlOrigin_=controlVelocity_={};launchAirborne_=false;tetherPull_=tetherCooldown_=0;selectedTarget_.clear();fragments_.clear();dust_.clear();destruction_.reset();terrainDestruction_.reset();combatActors_.clear();healingCasts_.clear();lunging_=false;
        for(const auto& c:scene.Characters)if(c.CombatHealth>0){CombatActor actor;actor.health=actor.maximum=c.CombatHealth;actor.team=c.CombatTeam;actor.boss=c.CombatBoss;actor.attack.cycleSeconds=1.5;combatActors_.emplace(c.Id,actor);}
        posture_=0;controlHeld_=controlConsumed_=false;controlTime_=slideRemaining_=0;slideVelocity_={};
        scene.validate();playScene_=scene;actorRoutes_.clear();actorRouteTimes_.clear();
        if(gameplayRules)for(auto& c:playScene_.Characters)if(c.CombatHealth>0 && !c.Route.empty()){actorRoutes_.emplace(c.Id,c);actorRouteTimes_.emplace(c.Id,0);c.Route.clear();}
        rules_.reset(scene.Rules);rulesEnabled_=gameplayRules;finished=false;settings_=*scene.Game;terrain_=scene.Terrain;solids_.clear();interactionBounds_.clear();
        for(const auto& object:scene.Characters){
            auto bounds=object_bounds(object);
            interactionBounds_.emplace(object.Id,bounds);
            if(object.Shape && (*object.Shape=="light" || *object.Shape=="fire" || *object.Shape=="craxis-smoke"))continue;
            if(object.GameplayRole!="key" && object.GameplayRole!="exit")solids_.push_back({object.Id,bounds});
        }
        solidIndex_.begin_bulk();
        for(size_t i=0;i<solids_.size();++i){solidSlot_[solids_[i].first]=uint32_t(i);solidIndex_.set(uint32_t(i),spatial_box(solids_[i].second));}
        solidIndex_.end_bulk();
        position={settings_.SpawnX,settings_.SpawnY,settings_.SpawnZ};yaw=pitch=0;
        cameraYaw=cameraPitch=0;cameraDistance=5;
        // The first dust update needs the bounds of every immutable solid; computing them lazily cost about 15 ms in the middle of the
        // first break. Same objects, same calculation, done while the scene is being prepared.
        if(GetEnvironmentVariableA("STARLIGHT_DISABLE_PREWARM",nullptr,0)==0){const double w0=starlight_diag::events_enabled()?starlight_diag::evnow():0;
         for(const auto& object:playScene_.Characters)if(lightning_solid(object)&&bounds_cacheable(object)&&object.GameplayRole!="key"&&object.GameplayRole!="exit")(void)ObjectCache::shared().bounds(object);
         if(w0>0)starlight_diag::evlog("obst.warm",starlight_diag::evnow()-w0,double(ObjectCache::shared().stats().entries));}
        starlight_diag::evlog("adv.start");
        schedule_prefracture();
    }
    void look(double horizontal,double vertical){
        if(!std::isfinite(horizontal)||!std::isfinite(vertical))return;
        orbit_camera(horizontal,vertical);yaw=cameraYaw;pitch=cameraPitch;
    }
    void move(double right,double forward,double seconds){
        lastTiming={};lastTiming.inputSampleTime=timing_seconds();ScopedTiming simulationTimer{lastTiming.simulationMs};
        if(!std::isfinite(right)||!std::isfinite(forward)||!std::isfinite(seconds)||seconds<=0)return;
        right=std::clamp(right,-1.,1.);forward=std::clamp(forward,-1.,1.);
        double length=std::max(1.,std::hypot(right,forward));
        const double dt=std::min(seconds,.05);
        if(seconds_-preTopUpAt_>=.25){preTopUpAt_=seconds_;top_up_prefracture();}
        if(position.y<settings_.SpawnY-100 && health_>0)defeat();
        {ScopedTiming timer{lastTiming.combatAiMs};advance_combat(dt);}
        if(health_<=0 || control_.blocks_mobility()){right=forward=0;jumpRequested_=false;}
        const double sprintFraction=movement_.advance(dt,sprintHeld_,right!=0 || forward!=0);
        const double moveSpeed=settings_.MoveSpeed*(1.+.5*sprintFraction)*(posture_==2?.25:posture_==1?.5:1.)*control_.movement_factor();
        const double dodgeFraction=std::min(dt,dodgeRemaining_)/dt;
        dodgeRemaining_=std::max(0.,dodgeRemaining_-dt);if(dodgeRemaining_<1e-9)dodgeRemaining_=0;
        Vec3 velocity{(right*std::cos(yaw)+forward*std::sin(yaw))*moveSpeed/length,0,
                      (right*std::sin(yaw)-forward*std::cos(yaw))*moveSpeed/length};
        velocity=velocity*(1-dodgeFraction)+dodgeVelocity_*(dodgeFraction*control_.movement_factor());
        if(slideRemaining_>0){velocity=slideVelocity_*((.25+.75*slideRemaining_/.6)*control_.movement_factor());slideRemaining_=std::max(0.,slideRemaining_-dt);}
        else if(posture_==3)set_posture(1);
        if(control_.active==ControlEffect::Fear){auto away=position-controlOrigin_;away.y=0;velocity=away.unit()*settings_.MoveSpeed;}
        if(control_.active==ControlEffect::Knockback)velocity=controlVelocity_;
        if(control_.active==ControlEffect::Pull){auto toward=controlOrigin_-position;toward.y=0;velocity=toward.unit()*std::min(16.,std::max(0.,toward.length()-1)/dt);}
        tetherCooldown_=std::max(0.,tetherCooldown_-dt);
        if(control_.active==ControlEffect::Tether){
            auto offset=position-controlOrigin_;offset.y=0;
            if(offset.length()>6 && tetherPull_<=0 && tetherCooldown_<=0){tetherPull_=.5;tetherCooldown_=.5;grapple_.detach();grapple_.held=false;grappleFalling_=false;mantleRemaining_=0;}
            if(tetherPull_>0){tetherPull_=std::max(0.,tetherPull_-dt);velocity=offset.unit()*-std::min(16.,std::max(0.,offset.length()-5.5)/dt);}
        }else tetherPull_=0;
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        if(settings_.Collision){
            if(!controller_)controller_=std::make_unique<AdventureController>(playScene_,position);
            bool forced=false;
            if(launchAirborne_){controlVelocity_.y-=9.81*dt;velocity=controlVelocity_;forced=true;}
            if(grapple_.state!=GrappleMotion::State::Free){
                bool found=grappleTarget_=="@terrain";
                for(const auto& object:playScene_.Characters)if(object.Id==grappleTarget_ && !rules_.removed.contains(object.Id) && (!combatActors_.contains(object.Id) || combatActors_.at(object.Id).health>0)){
                    auto moved=route_pose_of(object,seconds_);
                    grapple_.move_anchor(world_point(moved,grappleOffset_),dt);grapple_.normal=(world_point(moved,grappleOffset_+grappleLocalNormal_)-world_point(moved,grappleOffset_)).unit();if(grapple_.state==GrappleMotion::State::Free)grappleFalling_=true;found=true;break;
                }
                if(!found){grapple_.detach();grappleFalling_=true;}
                if(grapple_.state==GrappleMotion::State::Wall && forward>0 && !control_.blocks_mobility()){
                    for(const auto& object:playScene_.Characters)if(object.Id==grappleTarget_){
                        auto bounds=posed_bounds(object);
                        if(position.y-1.7>=bounds.high.y+.04 && controller_->traverse(position,grapple_.normal*(-.8*control_.movement_factor())+Vec3{0,.2,0})){mantleRemaining_=.4;grapple_.velocity=grapple_.normal*(-2*control_.movement_factor())+Vec3{0,.5,0};grapple_.detach();grappleFalling_=true;break;}
                    }
                }

                if(grapple_.state!=GrappleMotion::State::Free){
                    Vec3 steer{right*std::cos(yaw),forward,right*std::sin(yaw)};
                    if(grapple_.state==GrappleMotion::State::Swing)steer=velocity*(1./std::max(1.,moveSpeed));
                    if(control_.blocks_mobility())steer={};
                    bool hookFlying=grapple_.flying();auto grappleVelocity=grapple_.step(position,steer,dt,control_.movement_factor());if(!hookFlying){velocity=grappleVelocity;forced=true;}
                    if(grapple_.state==GrappleMotion::State::Wall && grapple_.normal.y>.5){grapple_.detach();grappleFalling_=true;}
                }
            }
            if(grappleFalling_ && !forced){
                if(mantleRemaining_>0)mantleRemaining_=std::max(0.,mantleRemaining_-dt);else grapple_.velocity.y-=9.81*dt;velocity=grapple_.velocity;forced=true;
            }
            auto before=position;
            {ScopedTiming timer{lastTiming.physicsMs};controller_->liquids(liquidSnapshot_);position=controller_->step(position,velocity,dt,jumpRequested_,rulesEnabled_?seconds_:-1,rules_.removed,forced);}
            controller_->sync_dynamic(playScene_);
            spatial_.fresh=false;
            for(const auto& c:playScene_.Characters)if(c.Dynamic){auto bounds=object_bounds(c);interactionBounds_[c.Id]=bounds;if(auto slot=solidSlot_.find(c.Id);slot!=solidSlot_.end()){solids_[slot->second].second=bounds;solidIndex_.set(slot->second,spatial_box(bounds));}}
            presentationVelocity_=(position-before)*(1./dt);
            if(launchAirborne_ && controller_->grounded && controlVelocity_.y<=0){launchAirborne_=false;control_.remaining=1;controlVelocity_={};++impactSerial_;}
            if(grappleFalling_ && controller_->grounded && velocity.y<=0){grappleFalling_=false;grapple_.velocity={};}
            if(grapple_.state==GrappleMotion::State::Pulling && !grapple_.flying() && forced && velocity.length()>.1){
                // A tiny timer/render interval can fall below the collision solver's
                // movement precision. Release only after sustained obstruction.
                if((position-before).length()<velocity.length()*dt*.1)grappleBlockedSeconds_+=dt;else grappleBlockedSeconds_=0;
                if(grappleBlockedSeconds_>=.12){grapple_.detach();grappleFalling_=true;grappleBlockedSeconds_=0;}
            }else grappleBlockedSeconds_=0;

            jumpRequested_=false;if(rulesEnabled_)seconds_+=dt;
            spatial_.fresh=false;
            for(const auto& object:playScene_.Characters)if(!object.Route.empty())interactionBounds_[object.Id]=posed_bounds(object);
            update_rules();return;
        }
#endif
        jumpRequested_=false;if(rulesEnabled_)seconds_+=dt;
        spatial_.fresh=false;
        presentationVelocity_={};
        if(right==0 && forward==0 && dodgeFraction==0){update_rules();return;}
        const auto presentationStart=position;
        Vec3 delta=velocity*dt;
        // Bounded substeps prevent tunnelling through thin walls at high speed.
        int steps=std::max(1,int(std::ceil(delta.length()/std::max(.01,settings_.PlayerRadius*.5))));
        delta=delta*(1./steps);
        for(int i=0;i<steps;i++){
            auto next=position;next.x+=delta.x;if(!blocked(next))position.x=next.x;
            next=position;next.z+=delta.z;if(!blocked(next))position.z=next.z;
            if(terrain_)position.y=terrain_->height_at(position.x,position.z)+1.7;
        }
        presentationVelocity_=(position-presentationStart)*(1./dt);
        update_rules();
    }
    std::string interact(){
        if(!rulesEnabled_)return "Start Play to use gameplay interactions.";
        if(health_<=0 || control_.blocks_basic())return "Cannot interact right now.";
        if(finished)return "Adventure complete. Reset to play again.";
        size_t found=playScene_.Characters.size();double nearest=2.5;
        for(size_t i=0;i<playScene_.Characters.size();i++){
            const auto& object=playScene_.Characters[i];
            const auto& b=interactionBounds_.at(object.Id);
            // A ray/box test chooses the first visible surface, so walls block interaction.
            double entryDistance=0,exitDistance=2.5;auto ray=direction();
            auto axis=[&](double origin,double d,double low,double high){
                if(std::abs(d)<1e-9)return origin>=low && origin<=high;
                double a=(low-origin)/d,z=(high-origin)/d;if(a>z)std::swap(a,z);
                entryDistance=std::max(entryDistance,a);exitDistance=std::min(exitDistance,z);return entryDistance<=exitDistance;
            };
            if(axis(position.x,ray.x,b.low.x,b.high.x)&&axis(position.y,ray.y,b.low.y,b.high.y)&&
               axis(position.z,ray.z,b.low.z,b.high.z)&&entryDistance<nearest){nearest=entryDistance;found=i;}
        }
        if(found==playScene_.Characters.size())return "Look at a nearby object, then press F (or E).";
        auto object=playScene_.Characters[found];
        // Existing key/door/exit roles are compatibility adapters. Authored
        // events and legacy interactions share the same action/state owner.
        const bool activated=rulesEnabled_ && rules_.emit("interact",object.Id,playScene_);
        finished=rules_.finished;
        if(finished || rules_.removed.contains(object.Id))return rules_.message;
        if(object.GameplayRole=="none")return activated ? rules_.message : "This object has no interaction.";
        const std::string effect=object.GameplayRole=="key"?"item-collected":object.GameplayRole=="door"?"object-opened":object.GameplayRole=="breakable"?"object-broken":"";
        for(const auto& rule:playScene_.Rules)if(rule.Enabled && rule.Event==effect && (rule.Subject.empty() || rule.Subject==(effect=="item-collected"?object.GameplayKey:object.Id)) && !rules_.quest_rule_available(rule.Id,playScene_))return "Finish the current quest step before using this object.";
        if(object.GameplayRole=="key") {
            rules_.remove_object(playScene_,object.Id);
            rules_.collect(object.GameplayKey,playScene_);finished=rules_.finished;
            return rules_.message.empty() ? "Collected key: "+object.GameplayKey : rules_.message;
        }
        if(object.GameplayRole=="breakable"){
            if(!object.Breakable)return "This object cannot be broken.";
            apply_attack_hits({object.Id},object.CombatHealth>0?object.CombatHealth:object.DestructionHealth);
            if(!rules_.removed.contains(object.Id))return "This object cannot be broken by this interaction.";
            return rules_.message.empty()?"Broken: "+object.Name:rules_.message;
        }
        if(!object.GameplayKey.empty() && !rules_.items.contains(object.GameplayKey))return "Requires key: "+object.GameplayKey;
        rules_.apply({object.GameplayRole=="exit" ? "finish-game" : "open-door",object.Id},playScene_);
        if(object.GameplayRole=="door" && rules_.removed.contains(object.Id)){auto message=rules_.message;rules_.emit("object-opened",object.Id,playScene_);if(rules_.message.empty())rules_.message=message;}
        finished=rules_.finished;return rules_.message.empty()?"Opened: "+object.Name:rules_.message;
    }
    Vec3 direction() const {return {std::sin(yaw)*std::cos(pitch),std::sin(pitch),-std::cos(yaw)*std::cos(pitch)};}
};
inline std::vector<std::string> adventure_readiness(const SceneData& scene){
    std::vector<std::string> findings;
    if(!scene.Game){findings.push_back("Enable adventure controls to set up a player.");return findings;}
    std::unordered_set<std::string> keys;bool exit=false;
    for(const auto& object:scene.Characters)if(object.GameplayRole=="key")keys.insert(object.GameplayKey);
    for(const auto& object:scene.Characters){
        if(object.GameplayRole=="exit")exit=true;
        if((object.GameplayRole=="door" || object.GameplayRole=="exit") && !object.GameplayKey.empty() && !keys.contains(object.GameplayKey))
            findings.push_back(object.Name+": no collectible key named "+object.GameplayKey+".");

    }
    for(const auto& rule:scene.Rules)if(rule.Enabled) {
        if(rule.Event=="item-collected" && !rule.Subject.empty() && !keys.contains(rule.Subject))
            findings.push_back(rule.Name+": collection event expects an item key, but no collectible item is named "+rule.Subject+".");
        for(const auto& action:rule.Actions) {
            if(action.Type=="finish-game")exit=true;
            if(action.Type=="open-door" && std::none_of(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==action.Target && c.GameplayRole=="door";}))
                findings.push_back(rule.Name+": linked door is missing or is not a door.");
        }
        for(const auto& condition:rule.Conditions)if(condition.Type=="has-key" && !keys.contains(condition.Value))
            findings.push_back(rule.Name+": no collectible item named "+condition.Value+".");
        if(rule.Event=="interact" && !rule.Subject.empty() && std::none_of(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==rule.Subject;}))
            findings.push_back(rule.Name+": interaction object is missing.");
    }
    if(!exit)findings.push_back("No finish / exit object: players can explore, but cannot complete this adventure.");
    AdventurePlayer player;player.start(scene);
    if(player.standing_inside_solid())findings.push_back("The player starts inside a solid object. Move the start position or the object.");
    return findings;
}
}
