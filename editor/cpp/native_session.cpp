#include "event_log.h"
#include <numbers>
#include "motion_trace.hpp"
#include "native_session.hpp"
#include "interactions.hpp"
#include "physics.hpp"
#include "pacing_log.hpp"
#include "environment_effects.hpp"
#include "joint_geometry.hpp"
#include "object_cache.hpp"
#include <chrono>
#include <deque>
namespace {
extern "C" {
__declspec(dllimport) void *sl_create(HWND, int, int);
__declspec(dllimport) int sl_resize(void*, int, int);
__declspec(dllimport) int sl_scene(void *, const unsigned char *, int);
__declspec(dllimport) int sl_scene_base(void *, const unsigned char *, int, unsigned long long);
__declspec(dllimport) int sl_scene_delta(void *, const unsigned char *, int, unsigned long long);
__declspec(dllimport) int sl_scene_stats(void *, double *, int);
__declspec(dllimport) int sl_scene_remove(void *, const unsigned long long *, int);
__declspec(dllimport) int sl_resident_mesh(void *, const unsigned char *, int);
__declspec(dllimport) int sl_resident_release(void *, const unsigned long long *, int);
__declspec(dllimport) int sl_instances(void *, const unsigned char *, int);
__declspec(dllimport) void sl_telegraph_stats(void*,unsigned long long*,unsigned long long*);
__declspec(dllimport) int sl_telegraph_ids(void*,const uint64_t*,int);
__declspec(dllimport) int sl_telegraphs(void*,const float*,int);
__declspec(dllimport) int sl_frame(void *, double, float);
__declspec(dllimport) int sl_wait_gpu(void *);
__declspec(dllimport) void sl_camera(void *, float, float, float, float, float, float);
__declspec(dllimport) void sl_camera_up(void *,float,float,float);
__declspec(dllimport) void sl_destroy(void *);
__declspec(dllimport) int sl_fog_readback(void*,double*);
__declspec(dllimport) int sl_motion_readback(void*,double*);
__declspec(dllimport) int sl_environment_lights(void*,const float*,int);
__declspec(dllimport) int sl_effect_clock(void*,double);
__declspec(dllimport) int sl_surface_weather(void*,float,float,float,float,const float*,int);
__declspec(dllimport) int sl_hdr_post_settings(void*,int,float);
__declspec(dllimport) int sl_shadow_settings(void*,int);
__declspec(dllimport) int sl_hdr_post_stats(void*,uint64_t*);
__declspec(dllimport) int sl_sun_shadow_stats(void*,uint64_t*);
__declspec(dllimport) int sl_local_shadow_stats(void*,uint64_t*);
__declspec(dllimport) int sl_fog_sources(void*,const float*,int);
__declspec(dllimport) int sl_fog_source_types(void*,const float*,int);
__declspec(dllimport) int sl_fog_terrain(void*,const float*);
__declspec(dllimport) int sl_fog_obstacles(void*,const float*,int);
__declspec(dllimport) void sl_environment_reset(void *);
__declspec(dllimport) int sl_grass_contacts(void*,const float*,int);
__declspec(dllimport) int sl_grass_stats(void*,uint64_t*);
__declspec(dllimport) int sl_lifetime_stats(void*,uint64_t*,int);
__declspec(dllimport) float sl_effect_time(void *);
__declspec(dllimport) void sl_shot(void *, const wchar_t *);
__declspec(dllimport) void sl_scene_resources(void *, unsigned long long *, unsigned long long *, unsigned long long *);
__declspec(dllimport) void sl_upload_pool_stats(void *, unsigned long long *, unsigned long long *, unsigned long long *, unsigned long long *);
__declspec(dllimport) const char *sl_error();
__declspec(dllimport) void sl_multicore_begin(void*,int);
__declspec(dllimport) int sl_multicore_read(void*,double*,int);
__declspec(dllimport) void sl_wait_timings(void*,double*,double*,double*);
__declspec(dllimport) double sl_gpu_ms(void *);
__declspec(dllimport) double sl_ray_ms(void *);
__declspec(dllimport) double sl_denoise_ms(void *);
__declspec(dllimport) int sl_render_width(void *);
__declspec(dllimport) int sl_rt_active(void *);
__declspec(dllimport) int sl_rr_active(void *);
}
// Deep equality of two characters without turning their (large) sculpt meshes into text.
bool same_character(starlight::editor::Character& a,starlight::editor::Character& b){
    if(a.Sculpt.has_value()!=b.Sculpt.has_value())return false;
    if(a.Sculpt&&(a.Sculpt->Positions!=b.Sculpt->Positions||a.Sculpt->Indices!=b.Sculpt->Indices||a.Sculpt->MaterialCoordinates!=b.Sculpt->MaterialCoordinates))return false;
    auto sculptA=std::move(a.Sculpt);auto sculptB=std::move(b.Sculpt);a.Sculpt.reset();b.Sculpt.reset();
    const bool same=starlight::editor::json(a)==starlight::editor::json(b);
    a.Sculpt=std::move(sculptA);b.Sculpt=std::move(sculptB);
    return same;
}
struct ViewDeleter {
    void operator()(void *p) const { sl_destroy(p); }
};
} // namespace
namespace starlight::editor {
NativeSession::NativeSession(HWND window, int w, int h)
    : window_(window), width_(std::max(1, w)), height_(std::max(1, h)) {
    thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
}
NativeSession::~NativeSession() {
    thread_.request_stop();
    condition_.notify_all();
    if (thread_.joinable())
        thread_.join();
}
uint64_t NativeSession::scene(SceneData s,bool runtimeUpdate) {
    s.validate();
    uint64_t generation=0;
    {
        std::lock_guard lock(mutex_);
        pending_ = std::move(s);
        generation=++sceneGeneration_;
        pendingRuntime_=runtimeUpdate;
        if(!runtimeUpdate){playerBody_.reset();liquidSnapshot_.reset();debrisPoses_.clear();collisionObjects_.clear();runtimeObjects_.clear();runtimeObjectsDirty_=false;}
    }
    condition_.notify_one();
    return generation;
}
void NativeSession::size(int w, int h) {
    std::lock_guard lock(mutex_);
    width_ = std::max(1, w);
    height_ = std::max(1, h);
}
void NativeSession::camera(CameraState c) {
    std::lock_guard lock(mutex_);
    camera_ = c;
}
void NativeSession::playback(bool play, bool reset) {
    std::lock_guard lock(mutex_);
    if (playing_ != play)
        tools_dirty_ = true;
    playing_ = play;
    reset_ |= reset;
}
void NativeSession::gameplay_overlays(std::vector<MeshPart> parts,bool probeMotion){
    std::vector<GpuTelegraph> gpu;
    for(const auto& part:parts)if(part.gpuTelegraph)gpu.push_back(*part.gpuTelegraph);
    std::erase_if(parts,[](const auto& part){return part.gpuTelegraph.has_value();});
    std::lock_guard lock(mutex_);motionProbe_=motionProbe_||probeMotion;gpuTelegraphs_=std::move(gpu);
    auto sameVector=[](Vec3 a,Vec3 b){return a.x==b.x && a.y==b.y && a.z==b.z;};
    const auto& previousOverlays=*gameplayOverlays_;
    bool same=parts.size()==previousOverlays.size();
    for(size_t i=0;same && i<parts.size();++i){
        const auto& a=parts[i];const auto& b=previousOverlays[i];
        same=sameVector(a.color,b.color) && a.emissive==b.emissive && a.mesh.indices==b.mesh.indices && a.mesh.vertices.size()==b.mesh.vertices.size();
        for(size_t j=0;same && j<a.mesh.vertices.size();++j){
            const auto& x=a.mesh.vertices[j];const auto& y=b.mesh.vertices[j];
            same=sameVector(x.position,y.position) && sameVector(x.normal,y.normal) && sameVector(x.color,y.color) && x.kind==y.kind;
        }
    }
    if(same)return;
    gameplayOverlays_=std::make_shared<const std::vector<MeshPart>>(std::move(parts));gameplayOverlaysDirty_=true;
}
void NativeSession::rubble_instances(std::vector<RubbleInstance> instances){
    std::lock_guard lock(mutex_);
    rubble_=std::make_shared<const std::vector<RubbleInstance>>(std::move(instances));rubbleDirty_=true;
}
void NativeSession::pin_preload(bool pinned){
    std::lock_guard lock(mutex_);
    if(pinned!=preloadPinned_||!pinned){preloadPinnedList_.clear();status_.preload_pinned_parts=0;}
    preloadPinned_=pinned;
}
uint64_t NativeSession::request_gpu_fence(){std::lock_guard lock(mutex_);return ++fenceRequested_;}
uint64_t NativeSession::rubble_preload(std::vector<std::shared_ptr<const MeshPart>> parts,bool bulk){
    std::lock_guard lock(mutex_);
    const uint64_t generation=++preloadGeneration_;
    if(preloadPinned_){
        if(parts.empty()&&!preloadPinnedList_.empty())++status_.preload_pin_holds;
        std::unordered_set<const MeshPart*> have;for(const auto& held:preloadPinnedList_)have.insert(held.get());
        for(auto& added:parts)if(added&&have.insert(added.get()).second)preloadPinnedList_.push_back(std::move(added));
        parts=preloadPinnedList_;status_.preload_pinned_parts=parts.size();
    }else if(rubblePreloadDirty_&&rubblePreload_&&!parts.empty()){auto merged=*rubblePreload_;merged.insert(merged.end(),std::make_move_iterator(parts.begin()),std::make_move_iterator(parts.end()));parts=std::move(merged);}
    rubblePreload_=std::make_shared<const std::vector<std::shared_ptr<const MeshPart>>>(std::move(parts));rubblePreloadDirty_=true;
    if(bulk){rubblePreloadBulk_=true;status_.preload_pending=true;}
    status_.preload_generation_requested=generation;
    return generation;
}
void NativeSession::gameplay_time(std::optional<double> time) {
    std::lock_guard lock(mutex_);gameplayTime_=time;
}
void NativeSession::runtime_objects(std::vector<Character> objects){
    std::lock_guard lock(mutex_);
    // Resting physics objects are published every update with the same pose; only a change is worth waking the render thread for
    // (it rebuilds the scene whenever it receives poses), so an unchanged list is dropped here.
    const bool same=objects.size()==runtimeObjects_.size()&&std::equal(objects.begin(),objects.end(),runtimeObjects_.begin(),[](const Character& a,const Character& b){
        return a.Id==b.Id&&a.X==b.X&&a.Y==b.Y&&a.Z==b.Z&&a.PhysicsRotation==b.PhysicsRotation&&a.JointRuntimePoints==b.JointRuntimePoints;});
    if(same)return;
    runtimeObjects_=std::move(objects);runtimeObjectsDirty_=!runtimeObjects_.empty();
}
void NativeSession::playback_options(bool repeat, std::string follow) {
    std::lock_guard lock(mutex_);
    repeat_ = repeat;
    follow_ = std::move(follow);
}
void NativeSession::tools(std::string selected, std::string mode) {
    std::lock_guard lock(mutex_);
    if (selected_ != selected || tool_ != mode)
        tools_dirty_ = true;
    selected_ = std::move(selected);
    tool_ = std::move(mode);
}
void NativeSession::collision_objects(std::vector<Character> objects){std::lock_guard lock(mutex_);collisionObjects_=std::move(objects);}
void NativeSession::player_body(std::optional<Bounds> body){std::lock_guard lock(mutex_);playerBody_=body;}
void NativeSession::probe_fog(){std::lock_guard lock(mutex_);fogProbe_=true;}
void NativeSession::probe_motion(){std::lock_guard lock(mutex_);motionProbe_=true;}
void NativeSession::capture(fs::path p) {
    std::lock_guard lock(mutex_);
    captureQueue_.push_back(std::move(p));
}
RenderStatus NativeSession::status() {
    std::lock_guard lock(mutex_);
    return status_;
}
void NativeSession::pacing_enabled(bool enabled){std::lock_guard lock(mutex_);pacingEnabled_=enabled;if(!enabled){pacingSnapshot_={};timingEvents_.clear();}}
void NativeSession::frame_updates(HWND window){std::lock_guard lock(mutex_);updateWindow_=window;}
void NativeSession::runtime_timing(RuntimeTiming timing,int count){std::lock_guard lock(mutex_);runtimeTiming_=timing;++runtimeSequence_;if(count!=attackCount_){if(pacingEnabled_)timingEvents_.push_back("attack-area-count:"+std::to_string(count));attackCount_=count;}}
void NativeSession::input_event(){std::lock_guard lock(mutex_);inputEventTime_=timing_seconds();++inputSequence_;}
void NativeSession::frame_update_handled(){std::lock_guard lock(mutex_);updatePosted_=false;}
void NativeSession::pacing_debris(std::vector<std::pair<std::string,std::array<double,7>>> poses){std::lock_guard lock(mutex_);debrisPoses_=std::move(poses);}
void NativeSession::pacing_player(Vec3 position,std::vector<double> pose){std::lock_guard lock(mutex_);playerPosition_=position;playerPose_=std::move(pose);}
std::shared_ptr<const LiquidSnapshot> NativeSession::liquid_snapshot(){std::lock_guard lock(mutex_);return liquidSnapshot_;}
PacingSnapshot NativeSession::pacing_snapshot(){std::lock_guard lock(mutex_);return pacingSnapshot_;}
void NativeSession::suspend_rendering(bool suspended) {
    { std::lock_guard lock(mutex_); suspended_ = suspended; }
    condition_.notify_all();
}
void NativeSession::run(std::stop_token stop) {
    {wchar_t value[16]{};if(GetEnvironmentVariableW(L"STARLIGHT_RENDER_PRIORITY",value,16)){const int level=_wtoi(value);if(level>=-1&&level<=2)SetThreadPriority(GetCurrentThread(),level);}} // measurement switch: -1 below normal, 1 above normal, 2 highest
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct ComScope {
        ~ComScope() { CoUninitialize(); }
    } com;
    try {
        std::unique_ptr<void, ViewDeleter> viewport;
        std::unique_ptr<PhysicsSession> physics;
        std::unique_ptr<TerrainFlow> flow;
        EnvironmentEffects environment;
        std::unordered_map<std::string,Vec3> fogCenters;
        SceneData authored, simulation;
        RenderSceneCache sceneCache;
        int w = 0, h = 0;
        double seconds = 0;
        bool physicsNeeded = false,fogTerrainDirty=true;
        auto previous = std::chrono::steady_clock::now();
        std::vector<uint8_t> packet;
        FramePacingMonitor pacing;
        bool wasPacing=false;double lastPacingPublish=0;
        std::unique_ptr<PacingLog> pacingLog;
        std::vector<PacingFrameRecord> pacingFrames;
        double logStart=0,lastLoggedFrame=0;
        uint64_t logFrame=0;double priorFence=0,priorPacing=0,priorPresent=0;
        // Static objects never move inside one simulation copy. Their exact mesh bounds are computed once per
        // simulation epoch (every assignment of `simulation` bumps it) instead of regenerating each mesh per frame.
        uint64_t simulationEpoch=0,boundsEpoch=~0ull;
        std::vector<std::optional<Bounds>> staticBounds;
        std::optional<json> authoredRest; // json of the installed scene without characters, rules and quests (valid after a removal-only update) // json of the installed scene without characters, rules and quests (valid after a removal-only update)
        auto reset = [&] {
            sceneCache.clear();
            simulation = authored;++simulationEpoch;
            seconds = 0;
            physics.reset();
            environment.reset();fogCenters.clear();
            if(viewport)sl_environment_reset(viewport.get());
            {std::lock_guard lock(mutex_);status_.environment_epoch=environment.epoch();status_.environment_lightning=0;status_.environment_sounds.clear();status_.effect_sparks=0;status_.fire_particles=0;status_.liquid_splashes=0;status_.rain_particles=0;status_.rain_impacts=0;status_.liquid_volume=0;status_.liquid_wave_energy=0;}
            physicsNeeded = std::any_of(simulation.Characters.begin(), simulation.Characters.end(),
                                        [](const Character &c) { return c.Dynamic; }) && !(simulation.Game && simulation.Game->Collision);
            flow = simulation.LiquidSources.empty() && !(simulation.Terrain && simulation.Terrain->WaterEnabled) &&
                       std::none_of(simulation.Characters.begin(),simulation.Characters.end(),[](const auto& c){return c.Shape=="lava";})
                       ? nullptr
                       : std::make_unique<TerrainFlow>(simulation.Terrain.value_or(TerrainData{}),
                                                       simulation.LiquidSources,simulation.Characters);
            if(flow)flow->wind(simulation.WaterWindX+simulation.RainWindX*simulation.RainIntensity,simulation.WaterWindZ+simulation.RainWindZ*simulation.RainIntensity,simulation.WaterWaveStrength);
            if(flow)flow->lava_settings(simulation.LavaInitialHeat,simulation.LavaCoolingRate,simulation.LavaCrustDepth,simulation.LavaLightStrength,true);
        };
        bool hadLiquidDebris=false,hadGpuGrass=false;
        wchar_t grassSetting[8]{};DWORD grassLength=GetEnvironmentVariableW(L"STARLIGHT_GPU_GRASS",grassSetting,8);
        if(grassLength&&(grassLength>=8||(wcscmp(grassSetting,L"0")&&wcscmp(grassSetting,L"1"))))throw std::runtime_error("GPU grass setting must be 0 or 1");
        const bool gpuGrassRequested=!grassLength||grassSetting[0]!=L'0';
        // Resident static scene: install the static packet once, then send only changing draws.
        // STARLIGHT_SCENE_DELTA=0 restores whole-packet publication; STARLIGHT_SCENE_DELTA_VERIFY=1 also builds the
        // whole packet each update and requires it to equal the resident base plus the delta, byte for byte.
        wchar_t deltaSetting[8]{};
        const bool deltaScene=!(GetEnvironmentVariableW(L"STARLIGHT_SCENE_DELTA",deltaSetting,8)&&deltaSetting[0]==L'0');
        wchar_t verifySetting[8]{};
        const bool verifyDelta=GetEnvironmentVariableW(L"STARLIGHT_SCENE_DELTA_VERIFY",verifySetting,8)&&verifySetting[0]==L'1';
        uint64_t installedBase=0;bool sceneInstalled=false;
        // Liquid surface generation for the next frame runs on a background thread while this frame is built and drawn;
        // STARLIGHT_LIQUID_ASYNC=0 turns it off (the delta-verification mode also turns it off so both packets see one geometry).
        const bool liquidAsync=[]{char value[8]{};const bool off=GetEnvironmentVariableA("STARLIGHT_LIQUID_ASYNC",value,8)&&value[0]=='0';const bool verify=GetEnvironmentVariableA("STARLIGHT_SCENE_DELTA_VERIFY",value,8)&&value[0]=='1';return !off&&!verify;}();
        CameraState lastCamera;bool haveLastCamera=false;uint64_t cameraRun=0,cameraRepeats=0,cameraLongest=0,cameraChanges=0;double cameraGapMax=0,cameraGapSum=0;std::chrono::steady_clock::time_point cameraChangedAt{};
        // Resident rubble: mesh -> viewport key. The shared_ptr keeps the immutable mesh alive while its address is a key.
        struct ResidentId{uint64_t content=0;const MeshPart* pointer=nullptr;ResidentId()=default;ResidentId(const MeshPart* p):pointer(p){}ResidentId(uint64_t c,const MeshPart* p):content(c),pointer(p){}bool operator==(const ResidentId& o)const{return content?content==o.content:(!o.content&&pointer==o.pointer);}};struct ResidentIdHash{size_t operator()(const ResidentId& id)const{return id.content?size_t(id.content):size_t(reinterpret_cast<uintptr_t>(id.pointer));}};using ResidentMap=std::unordered_map<ResidentId,std::pair<uint64_t,std::shared_ptr<const MeshPart>>,ResidentIdHash>;ResidentMap residentKeys;
        uint64_t nextResidentKey=1;
        std::shared_ptr<const std::vector<std::shared_ptr<const MeshPart>>> preloadList;bool preloadPending=false,preloadBulkNow=false;uint64_t preloadGenerationNow=0;
        std::shared_ptr<const std::vector<RubbleInstance>> currentRubble=std::make_shared<const std::vector<RubbleInstance>>();
        using StageClock=std::chrono::steady_clock;
        auto stageMs=[](StageClock::time_point a,StageClock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();};
        double stageSim=0,stagePrep=0,stageOverlay=0,stagePacket=0,stageInstall=0,stageFrame=0;uint64_t iterationCount=0;
        // CPU meshes of released rubble are freed a few per frame instead of all in the frame the fragments disappear.
        std::vector<std::shared_ptr<const MeshPart>> meshGraveyard;
        uint64_t deltaUpdates=0,baseInstalls=0,fullUpdates=0;
        // The liquid obstacle mask is built from every solid object, which costs tens of milliseconds on a scene with detailed
        // sculpted scenery. Objects that move on their own (combat actors, debris) only matter to the liquid while they touch
        // it, so they join the mask only then, and the mask is rebuilt only when the set of objects it holds, or any held pose,
        // actually changed (STARLIGHT_FIX_DELTA=0 restores the old rebuild on every call).
        uint64_t obstacleHeavySig=0,obstacleLightSig=0,obstacleMobileSig=0;bool obstacleMarkValid=false;FlowObstacleMask::Mark obstacleMark;
        struct LightEntry {uint64_t idHash,objectSig;bool alive;};
        std::vector<LightEntry> obstacleLightEntries; // the light layer's objects in the order they were added (its object numbers), with the ones taken out since marked dead
        double lastObstacleCheck=-1; // re-evaluates which mobile objects touch liquid even when no scene update arrives
        auto obstacles = [&](std::span<const Character> debris=std::span<const Character>{}) {
            if (!flow)
                return;
            const bool reuse=repair_delta();
            std::vector<double> liquidTop;if(reuse)liquidTop=flow->liquid_top_cells(1);
            auto touchesWet=[&](const Bounds& b){
                if(!reuse)return true;
                auto cell=[](double v){return std::clamp(int(std::floor((v+12)/.5)),0,TerrainFlow::N-1);};
                for(int z=cell(b.low.z);z<=cell(b.high.z);++z)for(int x=cell(b.low.x);x<=cell(b.high.x);++x)if(b.low.y<=liquidTop[z*TerrainFlow::N+x]+.15)return true; // reaches the liquid surface
                return false;
            };
            // Three layers: heavy immutable scenery (sculpted or imported models), light immutable scenery (rocks, stones, shapes) and the movers.
            // Removing or adding a rock only replaces the light layer; the heavy models are registered once.
            uint64_t heavySig=1469598103934665603ull,lightSig=1469598103934665603ull,mobileSig=1469598103934665603ull;
            struct Held {const Character* object;int layer;uint64_t idHash,objectSig;}; // 0 heavy, 1 light, 2 mobile
            std::vector<Held> held;
            std::deque<Character> posed; // stable addresses
            auto consider=[&](const Character& source,bool mobile){
                auto k = source.Shape.value_or("");
                if (k == "grass" || k == "fire" || k == "light" || k == "lava" || k == "chaos-lightning" || k == "light-shard" || k == "craxis-smoke")
                    return;
                const Character* use=&source;
                if(mobile&&reuse&&!touchesWet(ObjectCache::shared().bounds_at(source,route_pose_of(source,seconds))))return;
                if(!source.Route.empty()&&!source.PhysicsRotation){posed.push_back(route_pose(source,seconds));use=&posed.back();}
                const int layer=mobile?2:(source.Sculpt||!source.Asset.empty())?0:1;
                uint64_t& sig=layer==0?heavySig:layer==1?lightSig:mobileSig;
                uint64_t own=1469598103934665603ull;
                auto mix=[&](uint64_t v){sig^=v;sig*=1099511628211ull;own^=v;own*=1099511628211ull;};
                const uint64_t idHash=std::hash<std::string>{}(source.Id);
                mix(idHash);
                for(double v:{use->X,use->Y,use->Z,use->RotationX,use->RotationY,use->RotationZ,use->Size,use->Width,use->Height,use->Depth}){uint64_t bits;std::memcpy(&bits,&v,8);mix(bits);}
                if(use->PhysicsRotation)for(double v:*use->PhysicsRotation){uint64_t bits;std::memcpy(&bits,&v,8);mix(bits);}else mix(0x9e3779b97f4a7c15ull);
                mix(std::hash<std::string>{}(use->Asset));mix(std::hash<std::string>{}(use->Shape.value_or("")));
                held.push_back({use,layer,idHash,own});
            };
            for (const auto& c : simulation.Characters)consider(c,render_dynamic(c,&sceneCache));
            for(const auto& body:debris)consider(body,true);
            const double ob0=starlight_diag::events_enabled()?starlight_diag::evnow():0;
            auto addTo=[&](FlowObstacleMask& layer,const Character& object,int which){
                if(which!=2&&bounds_cacheable(object))layer.add(*ObjectCache::shared().mesh(object),object);else layer.add(object_mesh(object),object);
            };
            auto buildLayer=[&](int which)->std::shared_ptr<const FlowObstacleMask> {
                const double t0=ob0>0?starlight_diag::evnow():0;
                auto layer=std::make_shared<FlowObstacleMask>();
                size_t objects=0,triangleCount=0;
                for(const auto& entry:held)if(entry.layer==which&&which!=2&&bounds_cacheable(*entry.object))triangleCount+=ObjectCache::shared().mesh(*entry.object)->indices.size()/3;
                layer->triangles.reserve(triangleCount); // one allocation instead of repeated regrowth of 128-byte records
                for(const auto& entry:held)if(entry.layer==which){addTo(*layer,*entry.object,which);++objects;}
                if(layer->triangles.empty())return nullptr;
                const double t1=ob0>0?starlight_diag::evnow():0;
                layer->finish();
                if(ob0>0)starlight_diag::evlog("obstL",t1-t0,starlight_diag::evnow()-t1,double(which)*1e6+double(objects));
                return layer;
            };
            // The light layer: when the only change is that some of its objects are gone (a rock broke), they are taken out of the
            // existing layer instead of registering every remaining rock again; anything else rebuilds it.
            auto lightLayer=[&](const std::shared_ptr<const FlowObstacleMask>& current)->std::shared_ptr<const FlowObstacleMask> {
                std::vector<LightEntry> now;
                for(const auto& entry:held)if(entry.layer==1)now.push_back({entry.idHash,entry.objectSig,true});
                if(current&&obstacleLightEntries.size()==current->objectCount){
                    std::vector<unsigned> gone;size_t matched=0;
                    for(unsigned i=0;i<obstacleLightEntries.size();++i){
                        const auto& old=obstacleLightEntries[i];
                        if(!old.alive)continue;
                        if(matched<now.size()&&old.idHash==now[matched].idHash&&old.objectSig==now[matched].objectSig)++matched;else gone.push_back(i);
                    }
                    if(matched==now.size()&&!gone.empty()){
                        if(now.empty()){obstacleLightEntries.clear();return nullptr;}
                        const double r0=ob0>0?starlight_diag::evnow():0;
                        const_cast<FlowObstacleMask&>(*current).remove_objects(gone);
                        for(unsigned i:gone)obstacleLightEntries[i].alive=false;
                        if(ob0>0)starlight_diag::evlog("obstR",starlight_diag::evnow()-r0,double(gone.size()),double(now.size()));
                        return current;
                    }
                }
                auto built=buildLayer(1);
                if(built)obstacleLightEntries=std::move(now);else obstacleLightEntries.clear();
                return built;
            };
            if(reuse&&flow->has_obstacles()&&obstacleMarkValid&&heavySig==obstacleHeavySig){
                if(lightSig==obstacleLightSig&&mobileSig==obstacleMobileSig)return;
                flow->amend_obstacles([&](FlowObstacleMask& mask){
                    auto light=lightSig==obstacleLightSig?mask.lightStatic:lightLayer(mask.lightStatic);
                    auto movers=mobileSig==obstacleMobileSig?mask.mobile:buildLayer(2);
                    mask.set_layers(light,movers,obstacleMark.low,obstacleMark.high);
                });
                const bool lightChanged=lightSig!=obstacleLightSig;
                obstacleLightSig=lightSig;obstacleMobileSig=mobileSig;
                {static const bool verify=[]{char v[8]{};return GetEnvironmentVariableA("STARLIGHT_OBSTACLE_MASK_VERIFY",v,8)&&v[0]=='1';}();
                 if(verify){
                    // Compare against a mask built from every object in one piece: same column extents and the same answers to the queries the liquid makes.
                    FlowObstacleMask reference;
                    for(const auto& entry:held)reference.add(object_mesh(*entry.object),*entry.object);
                    reference.finish();
                    bool same=false;
                    flow->amend_obstacles([&](FlowObstacleMask& mask){
                        same=mask.coverage==reference.coverage&&mask.bottom==reference.bottom&&mask.top==reference.top;
                        uint32_t seed=12345;auto random=[&]{seed=seed*1664525u+1013904223u;return double(seed>>8)/double(1<<24);};
                        for(int k=0;k<3000&&same;++k){
                            const Vec3 p{-12+24*random(),-1+4*random(),-12+24*random()};
                            const double a=mask.clearance(p),b=reference.clearance(p);
                            if(std::isfinite(a)!=std::isfinite(b)||(std::isfinite(a)&&std::abs(a-b)>1e-6))same=false;
                            const Vec3 q=p+Vec3{random()*.3,random()*.3,random()*.3},r=p+Vec3{random()*.3,-random()*.2,random()*.3};
                            if(mask.clear_triangle(p,q,r)!=reference.clear_triangle(p,q,r))same=false;
                            if(mask.region_empty(p.x-.2,p.x+.2,p.z-.2,p.z+.2)&&!reference.region_empty(p.x-.2,p.x+.2,p.z-.2,p.z+.2))same=false;
                        }
                    });
                    if(!same)throw std::runtime_error("Layered liquid obstacle mask differs from a full rebuild");
                    static int verified=0;if(++verified==1)std::fprintf(stderr,"obstacle mask verify active%c",10);
                 }}
                if(ob0>0)starlight_diag::evlog("obst2",0.,starlight_diag::evnow()-ob0,double(held.size()));
                if(ob0>0&&lightChanged)starlight_diag::evlog("obst4",starlight_diag::evnow()-ob0,double(held.size()),0.);
                return;
            }
            FlowObstacleMask mask;
            for(const auto& entry:held)if(entry.layer==0)addTo(mask,*entry.object,0);
            mask.finish();
            obstacleMark=mask.mark();
            mask.set_layers(lightLayer(nullptr),buildLayer(2),obstacleMark.low,obstacleMark.high);
            flow->set_obstacles(std::move(mask));
            obstacleHeavySig=heavySig;obstacleLightSig=lightSig;obstacleMobileSig=mobileSig;obstacleMarkValid=true;
            if(ob0>0)starlight_diag::evlog("obst2",starlight_diag::evnow()-ob0,0.,double(held.size()));
        };
        std::vector<Character> lastRuntimeObjects;std::unordered_map<std::string,std::array<Vec3,4>> settlePoses;double lastBodyMotion=0;uint64_t fenceHandled=0,lastTakenScene=0;
        std::vector<RubbleInstance> bodyInstances;uint64_t bodiesSignature=0,lastLightSignature=0,lastEmitterSignature=0;
        while (!stop.stop_requested()) {
            auto begin = std::chrono::steady_clock::now();++iterationCount;
            for(int freed=0;freed<6&&!meshGraveyard.empty();++freed)meshGraveyard.pop_back();const double b_sim=stageSim,b_prep=stagePrep,b_ov=stageOverlay,b_pk=stagePacket,b_in=stageInstall,b_fr=stageFrame;double lockWaitMs=0,lockHeldMs=0;
            double dt = std::clamp(std::chrono::duration<double>(begin - previous).count(), 0., .05);
            previous = begin;
            CameraState camera;std::optional<Bounds> playerBody;std::vector<Character> collisionObjects;
            std::shared_ptr<const std::vector<MeshPart>> gameplayOverlays;
            std::vector<GpuTelegraph> gpuTelegraphs;
            std::vector<Character> runtimeObjects;std::shared_ptr<const std::vector<RubbleInstance>> rubble;bool rubbleDirty=false;std::shared_ptr<const std::vector<std::shared_ptr<const MeshPart>>> preload;bool preloadDirty=false;
            bool play, doReset, repeat, toolsDirty;
            std::string follow, selected, tool;
            int requestedW, requestedH;
            std::optional<SceneData> next;
            bool runtimeUpdate=false;
            std::optional<double> gameplayTime;
            bool motionProbe=false,fogProbe=false;
            std::optional<fs::path> shot;
            std::vector<std::pair<std::string,std::array<double,7>>> debrisPoses;
            bool measurePacing=false;Vec3 playerPosition;std::vector<double> playerPose;RuntimeTiming timing;uint64_t updateSequence=0,inputSequence=0;double inputEvent=0;std::vector<std::string> events;
            {
                std::unique_lock lock(mutex_);lockWaitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
                if (suspended_) {
                    status_.suspended = true;
                    condition_.wait(lock, [&] { return stop.stop_requested() || !suspended_; });
                    status_.suspended = false;
                    if (stop.stop_requested()) break;
                    previous = std::chrono::steady_clock::now();
                    dt = 0;
                }
                camera = camera_;playerBody=playerBody_;collisionObjects=collisionObjects_;
                measurePacing=pacingEnabled_;if(measurePacing){debrisPoses=debrisPoses_;playerPosition=playerPosition_;playerPose=playerPose_;timing=runtimeTiming_;updateSequence=runtimeSequence_;inputSequence=inputSequence_;inputEvent=inputEventTime_;events.swap(timingEvents_);}
                gameplayTime=gameplayTime_;
                play = playing_;
                repeat = repeat_;
                follow = follow_;
                selected = selected_;
                tool = tool_;
                toolsDirty = tools_dirty_;
                toolsDirty |= gameplayOverlaysDirty_;gameplayOverlaysDirty_=false;
                gameplayOverlays=gameplayOverlays_;gpuTelegraphs=gpuTelegraphs_;rubble=rubble_;rubbleDirty=rubbleDirty_;rubbleDirty_=false;if(rubblePreloadDirty_){preload=rubblePreload_;rubblePreloadDirty_=false;preloadDirty=true;preloadGenerationNow=preloadGeneration_;if(rubblePreloadBulk_){rubblePreloadBulk_=false;preloadBulkNow=true;}}
                if(runtimeObjectsDirty_){runtimeObjects=runtimeObjects_;runtimeObjectsDirty_=false;lastRuntimeObjects=runtimeObjects;}
                tools_dirty_ = false;
                doReset = reset_;
                reset_ = false;
                requestedW = width_;
                requestedH = height_;
                next = std::move(pending_);if(next)lastTakenScene=sceneGeneration_;
                runtimeUpdate=pendingRuntime_;pendingRuntime_=false;
                pending_.reset();
                motionProbe=motionProbe_;motionProbe_=false;fogProbe=fogProbe_;fogProbe_=false;
                if(!captureQueue_.empty()){shot=std::move(captureQueue_.front());captureQueue_.pop_front();}
                capture_.reset();
            }
            double sceneCostMs=0;const double mLock=starlight_diag::events_enabled()?std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count():0;const bool mkOn=starlight_diag::events_enabled();auto mkNow=[&]{return mkOn?std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count():0.;};
            bool rebuild = toolsDirty;bool bodiesChanged=false;
            if (next) {
                double rmT=starlight_diag::evnow();auto rmMark=[&](const char* tag){const double n=starlight_diag::evnow();starlight_diag::evlog(tag,n-rmT);rmT=n;};starlight_diag::evlog("rm.begin",double(authored.Characters.size()),double(next->Characters.size()));
                fogTerrainDirty=true;
                if(measurePacing){
                    for(const auto& c:next->Characters)if(std::none_of(authored.Characters.begin(),authored.Characters.end(),[&](const auto& old){return old.Id==c.Id;}))events.push_back("object-added:"+c.Id);
                    for(const auto& c:authored.Characters)if(std::none_of(next->Characters.begin(),next->Characters.end(),[&](const auto& fresh){return fresh.Id==c.Id;}))events.push_back("object-removed:"+c.Id);
                }
                rmMark("rm.detect");bool resetFog=authored.FogDensity!=next->FogDensity;
                bool sameBodies=!physics||json(authored.Characters)==json(next->Characters); // only the rigid-body session uses this
                // When only objects disappeared (a rock broke, a key was taken), drop just their parts from the resident static
                // scene instead of rebuilding and re-installing everything.
                rmMark("rm.bodies");std::vector<unsigned long long> removedKeys;bool removalOnly=false;
                if(runtimeUpdate&&!doReset&&deltaScene&&sceneInstalled&&sceneCache.token!=0&&installedBase==sceneCache.token&&!sceneCache.packet.empty()&&next->Characters.size()<=authored.Characters.size()){
                    // Gameplay rules and quests change as objects break but never affect rendering, so they do not count as a scene change.
                    std::vector<Character> oldCharacters=std::move(authored.Characters),newCharacters=std::move(next->Characters);
                    auto oldRules=std::move(authored.Rules);auto newRules=std::move(next->Rules);auto oldQuests=std::move(authored.Quests);auto newQuests=std::move(next->Quests);
                    rmMark("rm.pre");json nextRest(*next);const bool restEqual=(authoredRest?*authoredRest:json(authored))==nextRest;if(restEqual)authoredRest=std::move(nextRest);else authoredRest.reset();rmMark("rm.rest");
                    authored.Characters=std::move(oldCharacters);next->Characters=std::move(newCharacters);authored.Rules=std::move(oldRules);next->Rules=std::move(newRules);authored.Quests=std::move(oldQuests);next->Quests=std::move(newQuests);
                    
                    if(restEqual){
                        std::unordered_map<std::string,Character*> oldById;for(auto& c:authored.Characters)oldById.emplace(c.Id,&c);
                        bool onlyRemoved=true;
                        for(auto& c:next->Characters){
                            auto it=oldById.find(c.Id);
                            // Member-wise comparison (Character::operator==): exact and far cheaper than serializing every survivor.
                            if(render_dynamic(c,&sceneCache)||(c.Dynamic&&it!=oldById.end()&&sceneCache.resting.count(c.Id)&&!same_pose(pose_probe(*it->second),pose_probe(c)))){if(it!=oldById.end())oldById.erase(it);continue;} // drawn in the delta (or about to be moved there by promote_moved); its changes never touch the resident base
                            if(it==oldById.end()||!(*it->second==c)){onlyRemoved=false;break;}
                            oldById.erase(it);
                        }
                        rmMark("rm.surv");
                        
                        if(onlyRemoved){
                            removalOnly=true;
                            for(const auto& [id,old]:oldById){if(render_dynamic(*old,&sceneCache))continue;sceneCache.resting.erase(id);const auto known=sceneCache.partCounts.find(id);const size_t partCount=known!=sceneCache.partCounts.end()?size_t(known->second):object_parts(*old).size();for(size_t part=0;part<partCount;++part)removedKeys.push_back(render_identity(id,uint32_t(part)));}
                        }
                    }
                }
                rmMark("rm.keys");authored = std::move(*next);if(!removalOnly)authoredRest.reset();
                if(runtimeUpdate && !doReset){
                    std::vector<std::optional<Bounds>> keptBounds;bool keepBounds=false;
                    if(removalOnly&&boundsEpoch==simulationEpoch&&staticBounds.size()==simulation.Characters.size()){
                        // Only objects disappeared: the others are unchanged, so their cached bounds stay valid.
                        keepBounds=true;size_t kept=0;
                        for(size_t i=0;i<simulation.Characters.size()&&keepBounds;++i){
                            if(kept<authored.Characters.size()&&simulation.Characters[i].Id==authored.Characters[kept].Id){keptBounds.push_back(staticBounds[i]);++kept;}
                        }
                        keepBounds=keepBounds&&kept==authored.Characters.size();
                    }
                    rmMark("rm.assign");simulation=authored;rmMark("rm.simcopy");++simulationEpoch;if(keepBounds){staticBounds=std::move(keptBounds);boundsEpoch=simulationEpoch;}
                    if(removalOnly){
                        if(!removedKeys.empty()){
                            if(!sl_scene_remove(viewport.get(),removedKeys.data(),int(removedKeys.size())))throw std::runtime_error(sl_error());
                            sceneCache.removedIdentities.insert(sceneCache.removedIdentities.end(),removedKeys.begin(),removedKeys.end());
                        }
                    }else sceneCache.clear();
                    rmMark("rm.remove");if(flow)flow->terrain(simulation.Terrain.value_or(TerrainData{}));
                    if(physics){if(sameBodies)physics->terrain(simulation.Terrain.value_or(TerrainData{}));else physics->reset(simulation);}
                    rmMark("rm.terrain");if(resetFog && viewport)sl_environment_reset(viewport.get());
                }else reset();
                obstacles();rmMark("rm.obst");
                rebuild = true;
            } else if (doReset) {
                reset();
                obstacles();
                rebuild = true;
            }
            // The poses are no longer re-sent while they do not change, so a scene that was just replaced or reset gets the last ones again.
            if(!runtimeUpdate&&next)lastRuntimeObjects.clear();
            if((next&&!runtimeUpdate)||doReset){sceneCache.allowResting=false;settlePoses.clear();lastBodyMotion=seconds;}
            if(runtimeObjects.empty()&&(next||doReset)&&!lastRuntimeObjects.empty())runtimeObjects=lastRuntimeObjects;
            if(!runtimeObjects.empty()){
                for(const auto& pose:runtimeObjects)for(auto& c:simulation.Characters)if(c.Id==pose.Id && c.Dynamic){c.X=pose.X;c.Y=pose.Y;c.Z=pose.Z;c.PhysicsRotation=pose.PhysicsRotation;c.JointRuntimePoints=pose.JointRuntimePoints;c.Route.clear();break;}
                rebuild=true;obstacles();
            }
            {
                const bool same=haveLastCamera&&camera.eye.x==lastCamera.eye.x&&camera.eye.y==lastCamera.eye.y&&camera.eye.z==lastCamera.eye.z&&camera.target.x==lastCamera.target.x&&camera.target.y==lastCamera.target.y&&camera.target.z==lastCamera.target.z;
                const auto nowTime=StageClock::now();
                if(same){++cameraRepeats;++cameraRun;cameraLongest=std::max(cameraLongest,cameraRun);}
                else{if(haveLastCamera){const double gap=stageMs(cameraChangedAt,nowTime);cameraGapMax=std::max(cameraGapMax,gap);cameraGapSum+=gap;++cameraChanges;}cameraRun=0;cameraChangedAt=nowTime;lastCamera=camera;haveLastCamera=true;}
                {static FILE* cameraLog=[]()->FILE*{char path[512];if(!GetEnvironmentVariableA("STARLIGHT_CAMERA_LOG",path,512))return nullptr;FILE* file=nullptr;fopen_s(&file,path,"w");return file;}();
                static uint64_t cameraLogLines=0;
                if(cameraLog){fprintf(cameraLog,"%.4f %.6f %.6f %.6f %.6f %.6f %.6f\n",std::chrono::duration<double,std::milli>(nowTime.time_since_epoch()).count(),camera.eye.x,camera.eye.y,camera.eye.z,camera.target.x,camera.target.y,camera.target.z);if((++cameraLogLines&511)==0)fflush(cameraLog);}}
            }
            const double mA=mkNow();const auto simStart=StageClock::now();
            if (play) {
                if (physicsNeeded && !physics) {
                    physics = std::make_unique<PhysicsSession>();
                    physics->reset(simulation);
                }
                if (!physics || physics->ready()) {
                    seconds += dt;const bool secOn=starlight_diag::events_enabled();double secT=secOn?starlight_diag::evnow():0,secObstacles1=0,secFlow=0,secPhysics=0,secObstacles2=0,secEnvironment=0,secLiquid=0,secCopy=0,secStep=0;
                    if(flow && (!collisionObjects.empty()||hadLiquidDebris||std::any_of(simulation.Characters.begin(),simulation.Characters.end(),[](const auto& c){return !c.Route.empty();})||(repair_delta()&&seconds-lastObstacleCheck>=.2))){lastObstacleCheck=seconds;obstacles(collisionObjects);}hadLiquidDebris=!collisionObjects.empty();
                    if(secOn){const double n=starlight_diag::evnow();secObstacles1=n-secT;secT=n;}
                    if (flow){flow->wind(simulation.WaterWindX+simulation.RainWindX*simulation.RainIntensity,simulation.WaterWindZ+simulation.RainWindZ*simulation.RainIntensity,simulation.WaterWaveStrength);
                        flow->lava_settings(simulation.LavaInitialHeat,simulation.LavaCoolingRate,simulation.LavaCrustDepth,simulation.LavaLightStrength);
                        flow->step(std::max(1e-6, dt));
                    }
                    if(secOn){const double n=starlight_diag::evnow();secFlow=n-secT;secT=n;}
                    bool physicsChanged=false;
                    if (physics && (physicsChanged=physics->step(simulation, dt, flow.get())))
                        { if(secOn){const double n=starlight_diag::evnow();secPhysics=n-secT;secT=n;} obstacles(collisionObjects); if(secOn){const double n=starlight_diag::evnow();secObstacles2=n-secT;secT=n;} }
                    else if(secOn){const double n=starlight_diag::evnow();secPhysics=n-secT;secT=n;}
                    // Publish the current simulation on this rendered frame;
                    // do not hold moving objects behind a 20 Hz visual throttle.
                    if (flow || physics) rebuild = true;
                    if(flow){auto participants=simulation.Characters;participants.insert(participants.end(),collisionObjects.begin(),collisionObjects.end());if(playerBody){Character player;player.Id=lightning_player_id;player.Shape="cube";player.Dynamic=true;auto center=(playerBody->low+playerBody->high)*.5;player.X=center.x;player.Z=center.z;player.Y=playerBody->low.y;player.Width=playerBody->high.x-playerBody->low.x;player.Height=playerBody->high.y-playerBody->low.y;player.Depth=playerBody->high.z-playerBody->low.z;participants.push_back(player);}flow->interact(participants,dt,gameplayTime.value_or(seconds));for(const auto& splash:flow->splashes())environment.splash(splash);}
                    if(secOn){const double n=starlight_diag::evnow();secLiquid=n-secT;secT=n;}
                    const bool gpuGrass=gpuGrassRequested&&!simulation.RaytracingEnabled;
                    if(environment.active(simulation,gpuGrass)){
                        auto effectScene=simulation;effectScene.Characters.insert(effectScene.Characters.end(),collisionObjects.begin(),collisionObjects.end());if(secOn){const double n=starlight_diag::evnow();secCopy=n-secT;secT=n;}environment.step(effectScene,dt,gameplayTime.value_or(seconds),flow.get(),playerBody,gpuGrass);
                        if(secOn){const double n=starlight_diag::evnow();secStep=n-secT;secT=n;}
                        rebuild=true;
                    }
                    if(secOn){const double n=starlight_diag::evnow();secEnvironment=n-secT;secT=n;starlight_diag::evlog("sim.sec",secObstacles1+secFlow,secPhysics,secObstacles2);starlight_diag::evlog("sim.sec2",secEnvironment,physicsChanged?1.:0.,0);starlight_diag::evlog("sim.sec3",secLiquid,secCopy,secStep);}
                    if(flow&&liquidAsync)flow->geometry_begin_async();
                    auto liquidState=flow?std::make_shared<const LiquidSnapshot>(flow->snapshot()):nullptr;
                    {std::lock_guard lock(mutex_);liquidSnapshot_=std::move(liquidState);status_.environment_epoch=environment.epoch();status_.environment_lightning=environment.lightning_serial();status_.environment_sounds=environment.lightning_sounds();status_.effect_sparks=environment.spark_count();status_.fire_particles=environment.flame_count();status_.liquid_splashes=environment.splash_count();status_.rain_particles=environment.rain().particles().size();status_.rain_impacts=environment.rain().impact_count();status_.liquid_volume=flow?flow->volume():0;status_.liquid_wave_energy=flow?flow->wave_energy():0;}
                }
            }
            stageSim+=stageMs(simStart,StageClock::now());
            if (viewport && (w != requestedW || h != requestedH)) {
                if (!sl_resize(viewport.get(), requestedW, requestedH))
                    throw std::runtime_error(sl_error());
                w=requestedW; h=requestedH;
            }
            if (!viewport) {
                w = requestedW;
                h = requestedH;
                viewport.reset(sl_create(window_, w, h));installedBase=0;sceneInstalled=false;residentKeys.clear();preloadPending=preloadList!=nullptr;
                if (!viewport)
                    throw std::runtime_error(sl_error());
                rebuild = true;
            }
            const double mB=mkNow();const auto prepStart=StageClock::now();
            if(fogTerrainDirty||(flow&&play)){std::array<float,4096> heights{};for(int z=0;z<64;++z)for(int x=0;x<64;++x)heights[z*64+x]=float(atmosphere_surface(simulation.Terrain?&*simulation.Terrain:nullptr,flow.get(),-16+(x+.5)*.5,-16+(z+.5)*.5));if(!sl_fog_terrain(viewport.get(),heights.data()))throw std::runtime_error(sl_error());fogTerrainDirty=false;}
            size_t movingFogBodies=0;std::vector<float> fogBodies;std::unordered_map<std::string,Vec3> nextFogCenters;
            if(playerBody){auto center=(playerBody->low+playerBody->high)*.5;Vec3 velocity{};if(play&&dt>0)if(auto old=fogCenters.find(lightning_player_id);old!=fogCenters.end()){velocity=(center-old->second)/dt;double speed=velocity.length();if(speed>30)velocity=velocity*(30/speed);}nextFogCenters[lightning_player_id]=center;for(double value:{playerBody->low.x,playerBody->low.y,playerBody->low.z,playerBody->high.x,playerBody->high.y,playerBody->high.z,velocity.x,velocity.y,velocity.z})fogBodies.push_back(float(value));if(velocity.length()>.05)++movingFogBodies;}
            struct SolidBox{const std::string* id;Bounds box;};
            std::vector<SolidBox> solidBoxes;
            {
                if(boundsEpoch!=simulationEpoch||staticBounds.size()!=simulation.Characters.size()){staticBounds.assign(simulation.Characters.size(),std::nullopt);boundsEpoch=simulationEpoch;}
                const double poseTime=gameplayTime.value_or(seconds);
                auto consider=[&](const Character& source,std::optional<Bounds>* cached){
                    if(!lightning_solid(source))return;
                    if(bounds_cacheable(source)&&!render_dynamic(source,&sceneCache)){solidBoxes.push_back({&source.Id,ObjectCache::shared().bounds(source)});return;} // engine-wide cache
                    if(cached&&*cached){solidBoxes.push_back({&source.Id,**cached});return;}
                    const Bounds box=(play&&!source.Route.empty()&&!source.PhysicsRotation)?ObjectCache::shared().bounds_at(source,route_pose_of(source,poseTime)):object_bounds(source);
                    if(cached)*cached=box;
                    solidBoxes.push_back({&source.Id,box});
                };
                for(size_t i=0;i<simulation.Characters.size();++i){const auto& c=simulation.Characters[i];consider(c,(c.Route.empty()&&!render_dynamic(c,&sceneCache))?&staticBounds[i]:nullptr);}
                for(const auto& c:collisionObjects)consider(c,nullptr);
            }
            for(const auto& solid:solidBoxes){
                const auto& box=solid.box;auto center=(box.low+box.high)*.5;Vec3 velocity{};
                if(play&&dt>0)if(auto old=fogCenters.find(*solid.id);old!=fogCenters.end()){velocity=(center-old->second)/dt;double speed=velocity.length();if(speed>30)velocity=velocity*(30/speed);}
                nextFogCenters[*solid.id]=center;
                if(box.high.x< -16||box.low.x>16||box.high.z< -16||box.low.z>16||box.high.y< -2||box.low.y>14)continue;
                if(fogBodies.size()<128*9&&velocity.length()>.05)++movingFogBodies;
                if(fogBodies.size()<128*9)for(double value:{box.low.x,box.low.y,box.low.z,box.high.x,box.high.y,box.high.z,velocity.x,velocity.y,velocity.z})fogBodies.push_back(float(value));
            }
            std::vector<float> grassBodies;
            auto grassBody=[&](const Bounds& box){for(double value:{box.low.x,box.low.y,box.low.z,box.high.x,box.high.y,box.high.z})grassBodies.push_back(float(value));};
            if(playerBody)grassBody(*playerBody);
            for(const auto& solid:solidBoxes)grassBody(solid.box);
            if(!sl_grass_contacts(viewport.get(),grassBodies.data(),int(grassBodies.size()/6)))throw std::runtime_error(sl_error());
            fogCenters=std::move(nextFogCenters);
            {std::lock_guard lock(mutex_);status_.fog_bodies=fogBodies.size()/9;status_.fog_moving_bodies=movingFogBodies;}
            {static const bool noFogObstacles=std::getenv("STARLIGHT_DIAG_NO_FOG_OBSTACLES")!=nullptr; // diagnostic: attribute GPU cost of the fog obstacle list
                if(!sl_fog_obstacles(viewport.get(),fogBodies.data(),noFogObstacles?0:int(fogBodies.size()/9)))throw std::runtime_error(sl_error());}
            std::vector<float> weatherBoxes;weatherBoxes.reserve(64*6);
            for(size_t i=0;i<fogBodies.size()/9&&i<64;++i)weatherBoxes.insert(weatherBoxes.end(),fogBodies.begin()+i*9,fogBodies.begin()+i*9+6);
            if(!sl_surface_weather(viewport.get(),float(environment.surface_moisture()),float(simulation.RainIntensity),float(simulation.RainWindX),float(simulation.RainWindZ),weatherBoxes.data(),int(weatherBoxes.size()/6)))throw std::runtime_error(sl_error());
            if(!sl_hdr_post_settings(viewport.get(),simulation.BloomQuality,float(simulation.BloomStrength)))throw std::runtime_error(sl_error());
            if(!sl_shadow_settings(viewport.get(),simulation.ShadowQuality))throw std::runtime_error(sl_error());
            sl_multicore_begin(viewport.get(),measurePacing?1:0);
            const bool gpuGrass=gpuGrassRequested&&play&&!simulation.RaytracingEnabled;
            if(gpuGrass!=hadGpuGrass){rebuild=true;hadGpuGrass=gpuGrass;}
            stagePrep+=stageMs(prepStart,StageClock::now());const double mC=mkNow();
            if (rebuild || (packet.empty() && !sceneInstalled)) {
                auto uploadStart=std::chrono::steady_clock::now();
                auto overlays = editor_overlays(simulation, selected, tool, play);
                const std::vector<MeshPart>* trailingOverlays=gameplayOverlays.get();
                if(play){auto effects=environment.geometry(simulation,gpuGrass);overlays.insert(overlays.end(),std::make_move_iterator(effects.begin()),std::make_move_iterator(effects.end()));}
                auto joints=joint_geometry(simulation,authored);overlays.insert(overlays.end(),std::make_move_iterator(joints.begin()),std::make_move_iterator(joints.end()));
                const auto overlaysDone=StageClock::now();
                stageOverlay+=stageMs(uploadStart,overlaysDone);
                std::vector<uint8_t> delta;
                bool installedDelta=false;
                // Physics bodies start out settling (contact offsets, overlapping neighbours pushing apart), so they are drawn per update until
                // none has moved for a short while; then the static scene is rebuilt once with the still ones inside it.
                if(play&&repair_delta()){
                    bool anyEligible=false,anyMoved=false;
                    for(const auto& c:simulation.Characters){
                        if(!resting_eligible(c))continue;
                        anyEligible=true;const auto probe=pose_probe(c);
                        auto seen=settlePoses.find(c.Id);
                        if(seen==settlePoses.end())settlePoses.emplace(c.Id,probe),anyMoved=true;
                        else if(!same_pose(seen->second,probe)){seen->second=probe;anyMoved=true;}
                    }
                    if(anyMoved)lastBodyMotion=seconds;
                    if(!sceneCache.allowResting&&anyEligible&&seconds-lastBodyMotion>.75){
                        sceneCache.allowResting=true;sceneCache.clear();starlight_diag::evlog("rest.arm",seconds,0,0);
                    }
                }
                // Resting bodies live in the resident base; any that has moved leaves it now and is drawn in this and later deltas.
                {static uint64_t restLog=0;if((restLog++%240)==0)starlight_diag::evlog("rest.state",double(sceneCache.resting.size()),double(sceneCache.packet.size()),double(simulation.Characters.size()));}
                if(!sceneCache.resting.empty()&&!sceneCache.packet.empty()){
                    std::vector<unsigned long long> promoted;std::vector<std::string> ids;
                    for(const auto& c:simulation.Characters){
                        const auto found=sceneCache.resting.find(c.Id);
                        if(found==sceneCache.resting.end()||same_pose(found->second,pose_probe(c)))continue;
                        ids.push_back(c.Id);
                        {const auto now=pose_probe(c);double worst=0;int which=0;for(int k=0;k<4;++k){double d=std::max({std::abs(now[k].x-found->second[k].x),std::abs(now[k].y-found->second[k].y),std::abs(now[k].z-found->second[k].z)});if(d>worst){worst=d;which=k;}}std::fprintf(stderr,"promoted %s: probe %d moved by %.6f (y %.6f -> %.6f) phys=%d%c",c.Name.c_str(),which,worst,found->second[0].y,now[0].y,int(c.PhysicsRotation.has_value()),10);}
                        const auto known=sceneCache.partCounts.find(c.Id);
                        const size_t partCount=known!=sceneCache.partCounts.end()?size_t(known->second):object_parts(c).size();
                        for(size_t part=0;part<partCount;++part)promoted.push_back(render_identity(c.Id,uint32_t(part)));
                    }
                    if(!ids.empty())starlight_diag::evlog("rest.promote",double(ids.size()),double(sceneCache.resting.size()),double(promoted.size()));
                    for(const auto& id:ids)sceneCache.resting.erase(id);
                    if(!promoted.empty()){
                        if(installedBase==sceneCache.token&&sceneCache.token!=0&&!sl_scene_remove(viewport.get(),promoted.data(),int(promoted.size())))throw std::runtime_error(sl_error());
                        sceneCache.removedIdentities.insert(sceneCache.removedIdentities.end(),promoted.begin(),promoted.end());
                    }
                }
                sceneCache.bodiesAsInstances=repair_delta()&&repair_instances()&&!simulation.RaytracingEnabled;
                if(deltaScene && render_scene_delta(simulation, flow.get(), overlays, sceneCache, play, gpuGrass, delta, trailingOverlays)){
                    const auto packetDone=StageClock::now();
                    stagePacket+=stageMs(overlaysDone,packetDone);
                    if(sceneCache.packet.size()>INT_MAX || delta.size()>INT_MAX)
                        throw std::runtime_error("Scene packet exceeds the native installation limit");
                    if(installedBase!=sceneCache.token){
                        if(!sl_scene_base(viewport.get(), sceneCache.packet.data(), int(sceneCache.packet.size()), sceneCache.token))
                            throw std::runtime_error(sl_error());
                        installedBase=sceneCache.token;++baseInstalls;
                        if(!sceneCache.removedIdentities.empty()&&!sl_scene_remove(viewport.get(),reinterpret_cast<const unsigned long long*>(sceneCache.removedIdentities.data()),int(sceneCache.removedIdentities.size())))throw std::runtime_error(sl_error());
                    }
                    if(!sl_scene_delta(viewport.get(), delta.data(), int(delta.size()), sceneCache.token))
                        throw std::runtime_error(sl_error());
                    stageInstall+=stageMs(packetDone,StageClock::now());
                    installedDelta=true;sceneInstalled=true;++deltaUpdates;packet.clear();
                    if(verifyDelta){
                        // Contract: whole packet == resident base (draw count patched) + delta draws.
                        auto whole=render_scene(simulation, flow.get(), overlays, &sceneCache, play, gpuGrass, trailingOverlays);
                        const auto& base=sceneCache.packet;
                        uint32_t baseCount=0,deltaCount=0,wholeCount=0;
                        std::memcpy(&baseCount,base.data()+4,4);std::memcpy(&deltaCount,delta.data()+12,4);std::memcpy(&wholeCount,whole.data()+4,4);
                        const bool same=whole.size()==base.size()+delta.size()-16 && wholeCount==baseCount+deltaCount
                            && std::memcmp(whole.data()+8,base.data()+8,base.size()-8)==0
                            && std::memcmp(whole.data()+base.size(),delta.data()+16,delta.size()-16)==0;
                        if(!same)throw std::runtime_error("Resident scene delta differs from the whole packet");
                    }
                }
                if(!installedDelta){if(std::any_of(simulation.Characters.begin(),simulation.Characters.end(),[](const Character& c){return drives_effect_channels(c);}))bypass_counters().effectRepublish.fetch_add(1,std::memory_order_relaxed);
                    if(!sceneCache.removedIdentities.empty())sceneCache.clear();
                    packet = render_scene(simulation, flow.get(), overlays, &sceneCache,play,gpuGrass,trailingOverlays);
                    const auto packetDone=StageClock::now();
                    stagePacket+=stageMs(overlaysDone,packetDone);
                    if (packet.size() > INT_MAX || !sl_scene(viewport.get(), packet.data(), int(packet.size())))
                        throw std::runtime_error(sl_error());
                    stageInstall+=stageMs(packetDone,StageClock::now());
                    installedBase=0;sceneInstalled=true;++fullUpdates;
                }
                {
                    // Moving physics bodies are sent as resident instances: their geometry reaches the graphics card once and each update carries only
                    // where the body is and how it is turned. (A frame that fell back to the whole-scene packet draws them inside it instead.)
                    std::vector<RubbleInstance> next;uint64_t signature=1469598103934665603ull;
                    auto mixBits=[&](double v){uint64_t bits;std::memcpy(&bits,&v,8);signature^=bits;signature*=1099511628211ull;};
                    if(installedDelta&&sceneCache.bodiesAsInstances){
                        for(const auto& c:simulation.Characters){
                            if(!instance_body(c)||sceneCache.resting.count(c.Id))continue;
                            const auto body=resident_body(c);
                            const auto& bodyList=body->parts;
                            std::array<double,4> q{0,0,0,1};
                            if(c.PhysicsRotation)q=*c.PhysicsRotation;
                            else{ // the same turn world_point applies: about X, then Y, then Z
                                constexpr double rad=std::numbers::pi/180;
                                auto axis=[&](int a,double deg){const double h=deg*rad*.5,s=std::sin(h);return std::array<double,4>{a==0?s:0,a==1?s:0,a==2?s:0,std::cos(h)};};
                                auto multiply=[](const std::array<double,4>& a,const std::array<double,4>& b){return std::array<double,4>{a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1],a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0],a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3],a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2]};};
                                q=multiply(axis(2,c.RotationZ),multiply(axis(1,c.RotationY),axis(0,c.RotationX)));
                            }
                            const uint64_t identity=std::hash<std::string>{}(c.Id);
                            for(size_t part=0;part<bodyList.size();++part){
                                next.push_back({bodyList[part],Vec3{c.X,c.Y,c.Z},q,identity+part,body->part_identity(part)});
                                mixBits(c.X);mixBits(c.Y);mixBits(c.Z);for(double v:q)mixBits(v);mixBits(double(body->part_identity(part)>>12));
                            }
                        }
                    }
                    if(signature!=bodiesSignature||next.size()!=bodyInstances.size()){bodiesChanged=true;bodiesSignature=signature;}
                    bodyInstances=std::move(next);
                }
                sceneCostMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count();
                {std::lock_guard lock(mutex_);status_.scene_cpu_total_ms+=sceneCostMs;++status_.scene_updates;status_.scene_generation_installed=lastTakenScene;}
            }
            {static MotionTrace trace;if(play)trace.sample(simulation,"mot.render");}
            const double mD=mkNow();
            if(preloadDirty){preloadList=preload;preloadPending=true;}
            if(rubble&&rubbleDirty)currentRubble=rubble;
            if(viewport&&((rubble&&rubbleDirty)||preloadPending||bodiesChanged)){
                const auto rubbleStart=StageClock::now();double rbT=starlight_diag::evnow();auto rbMark=[&](const char* tag,double a=0,double b=0){const double n=starlight_diag::evnow();starlight_diag::evlog(tag,n-rbT,a,b);rbT=n;};double rubbleSerialize=0,rubbleUpload=0;
                struct Wire{uint64_t mesh,motion;float px,py,pz,qx,qy,qz,qw,r,g,b,emissive,reserved;};
                static_assert(sizeof(Wire)==64);
                std::vector<RubbleInstance> allInstances(*currentRubble);allInstances.insert(allInstances.end(),bodyInstances.begin(),bodyInstances.end());
                std::vector<Wire> wire;wire.reserve(allInstances.size());
                std::unordered_set<ResidentId,ResidentIdHash> live;
                std::vector<uint8_t> uploads(4,0);uint32_t uploadCount=0;
                auto queueUpload=[&](const std::shared_ptr<const MeshPart>& part,ResidentId id=ResidentId{})->ResidentMap::iterator{if(!id.content&&!id.pointer)id=ResidentId(part.get());
                    const uint64_t key=nextResidentKey++;
                    const auto serializeStart=StageClock::now();
                    auto bytes=serialize_resident_mesh(*part,key);
                    rubbleSerialize+=stageMs(serializeStart,StageClock::now());
                    const uint32_t size=uint32_t(bytes.size());
                    uploads.insert(uploads.end(),reinterpret_cast<const uint8_t*>(&size),reinterpret_cast<const uint8_t*>(&size)+4);
                    uploads.insert(uploads.end(),bytes.begin(),bytes.end());++uploadCount;
                    return residentKeys.emplace(id,std::pair<uint64_t,std::shared_ptr<const MeshPart>>{key,part}).first;
                };
                // Meshes that are drawn now must be resident this frame.
                for(const auto& instance:allInstances){
                    if(!instance.part||instance.part->mesh.indices.empty())continue;
                    const ResidentId mesh=instance.contentKey?ResidentId(instance.contentKey,instance.part.get()):ResidentId(instance.part.get());live.insert(mesh);
                    auto known=residentKeys.find(mesh);
                    if(known==residentKeys.end())known=queueUpload(instance.part,mesh);
                    const auto& q=instance.rotation;
                    wire.push_back({known->second.first,vertex_motion_identity(3,instance.serial,UINT64_MAX),float(instance.position.x),float(instance.position.y),float(instance.position.z),float(q[0]),float(q[1]),float(q[2]),float(q[3]),float(instance.part->color.x),float(instance.part->color.y),float(instance.part->color.z),instance.part->emissive?1.f:0.f,0.f});
                }
                // Meshes that will probably be drawn soon trickle in a few per frame.
                preloadPending=false;int budget=preloadBulkNow?INT_MAX:3;
                if(preloadList)for(const auto& part:*preloadList){
                    if(!part||part->mesh.indices.empty())continue;
                    live.insert(part.get());
                    if(residentKeys.contains(part.get()))continue;
                    if(budget>0){--budget;queueUpload(part);}else preloadPending=true;
                }
                rbMark("rb.build",double(wire.size()),double(uploadCount));if(uploadCount){
                    std::memcpy(uploads.data(),&uploadCount,4);const auto uploadStart=StageClock::now();
                    if(uploads.size()>INT_MAX||!sl_resident_mesh(viewport.get(),uploads.data(),int(uploads.size())))throw std::runtime_error(sl_error());
                    rubbleUpload+=stageMs(uploadStart,StageClock::now());
                }
                rbMark("rb.upload");if(!preloadPending){std::lock_guard lock(mutex_);status_.preload_generation_submitted=std::max(status_.preload_generation_submitted,preloadGenerationNow);if(preloadBulkNow){preloadBulkNow=false;status_.preload_pending=false;}}
                if(!sl_instances(viewport.get(),reinterpret_cast<const unsigned char*>(wire.data()),int(wire.size())))throw std::runtime_error(sl_error());
                rbMark("rb.inst",double(wire.size()));std::vector<unsigned long long> released;
                for(auto it=residentKeys.begin();it!=residentKeys.end();){
                    if(live.contains(it->first)){++it;continue;}
                    released.push_back(it->second.first);meshGraveyard.push_back(std::move(it->second.second));it=residentKeys.erase(it);
                }
                rbMark("rb.erase",double(released.size()));if(!released.empty()&&!sl_resident_release(viewport.get(),released.data(),int(released.size())))throw std::runtime_error(sl_error());rbMark("rb.rel",double(released.size()));
                {const double total=stageMs(rubbleStart,StageClock::now());std::lock_guard lock(mutex_);status_.rubble_serialize_ms+=rubbleSerialize;status_.rubble_upload_ms+=rubbleUpload;status_.rubble_instances_ms+=total-rubbleSerialize-rubbleUpload;status_.rubble_worst_publish_ms=std::max(status_.rubble_worst_publish_ms,total);status_.rubble_uploaded_meshes+=uploadCount;}
            }
            const double mE=mkNow();std::vector<float> telegraphData;std::vector<uint64_t> telegraphIds;telegraphData.reserve(gpuTelegraphs.size()*11);
            for(const auto& effect:gpuTelegraphs){const auto* data=reinterpret_cast<const float*>(&effect);telegraphData.insert(telegraphData.end(),data,data+11);telegraphIds.push_back(effect.identity);}
            if(!sl_telegraphs(viewport.get(),telegraphData.data(),int(gpuTelegraphs.size()))||!sl_telegraph_ids(viewport.get(),telegraphIds.data(),int(telegraphIds.size())))throw std::runtime_error(sl_error());
            const double mk0=mkNow();
            double duration = 0;
            for (const auto &c : authored.Characters) {
                WalkPoint p{c.X, c.Z};
                double length = 0;
                for (auto q : c.Route) {
                    length += std::hypot(q.X - p.X, q.Z - p.Z);
                    p = q;
                }
                duration = std::max(duration, length / c.Speed);
            }
            if (play && !authored.Game && duration > 0 && !physicsNeeded && !flow && !repeat && seconds >= duration) {
                seconds = duration;
                play = false;
                std::lock_guard lock(mutex_);
                playing_ = false;
            }
            double routeTime = repeat && duration > 0 ? std::fmod(seconds, duration) : seconds;
            if(gameplayTime)routeTime=*gameplayTime;
            if (!follow.empty())
                for (const auto &c : simulation.Characters)
                    if (c.Id == follow) {
                        auto p = c.at(routeTime);
                        Vec3 target{p.X, c.Y + .7 * c.Size, p.Z};
                        camera.eye = camera.eye + (target - camera.target);
                        camera.target = target;
                        break;
                    }
            const double mk1=mkNow();
            sl_camera(viewport.get(), float(camera.eye.x), float(camera.eye.y), float(camera.eye.z),
                      float(camera.target.x), float(camera.target.y), float(camera.target.z));
            sl_camera_up(viewport.get(),float(camera.up.x),float(camera.up.y),float(camera.up.z));
            if (shot)
                sl_shot(viewport.get(), shot->c_str());
            if(!sl_effect_clock(viewport.get(),routeTime))throw std::runtime_error("Invalid environment simulation clock");
            const double mk2=mkNow();auto environmentLights=environment_lights(simulation,routeTime,flow.get());{const uint64_t signature=channel_signature(environmentLights.data(),environmentLights.size()*sizeof(environmentLights[0]));bypass_counters().lightChannelFrames.fetch_add(1,std::memory_order_relaxed);if(signature!=lastLightSignature){lastLightSignature=signature;bypass_counters().lightChannelChanges.fetch_add(1,std::memory_order_relaxed);}}if(!sl_environment_lights(viewport.get(),environmentLights.empty()?nullptr:environmentLights[0].data(),int(environmentLights.size())))throw std::runtime_error(sl_error());
            const double mk3=mkNow();auto smokeSources=smoke_sources(simulation,routeTime,flow.get());{const uint64_t signature=channel_signature(smokeSources.data(),smokeSources.size()*sizeof(smokeSources[0]));if(signature!=lastEmitterSignature){lastEmitterSignature=signature;bypass_counters().emitterChannelChanges.fetch_add(1,std::memory_order_relaxed);}}if(!sl_fog_sources(viewport.get(),smokeSources.empty()?nullptr:smokeSources[0].data(),int(smokeSources.size())))throw std::runtime_error(sl_error());
            const double mk4=mkNow();std::vector<float> fogTypes;for(auto emitter:fog_emitters(simulation,routeTime,flow.get()))fogTypes.push_back(emitter[4]);if(!sl_fog_source_types(viewport.get(),fogTypes.data(),int(fogTypes.size())))throw std::runtime_error(sl_error());
            auto frameStart=std::chrono::steady_clock::now();const double mPre=starlight_diag::events_enabled()?std::chrono::duration<double,std::milli>(frameStart-begin).count():0;
            if (!sl_frame(viewport.get(), routeTime, float(camera.distance)))
                throw std::runtime_error(sl_error());
            const double frameCallMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart).count();
            {   // A caller asked to know when the graphics card has finished everything submitted so far (loading uses this as its GPU readiness barrier).
                uint64_t wanted;{std::lock_guard lock(mutex_);wanted=fenceRequested_;}
                if(wanted>fenceHandled){
                    if(!sl_wait_gpu(viewport.get()))throw std::runtime_error(sl_error());
                    fenceHandled=wanted;std::lock_guard lock(mutex_);status_.gpu_fence_done=wanted;status_.gpu_fence_preload_generation=status_.preload_generation_submitted;status_.gpu_fence_scene_generation=status_.scene_generation_installed;
                }
            }
            stageFrame+=frameCallMs;
            if(starlight_diag::events_enabled()){const double total=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();starlight_diag::evlog("it",double(iterationCount),total,lockWaitMs);starlight_diag::evlog("it2",stageSim-b_sim,stagePrep-b_prep,stageOverlay-b_ov);starlight_diag::evlog("it3",stagePacket-b_pk,stageInstall-b_in,stageFrame-b_fr);starlight_diag::evlog("it4",mLock,mPre,0);starlight_diag::evlog("it5",mk0,mk1,mk2);starlight_diag::evlog("it6",mk3,mk4,mPre);starlight_diag::evlog("it7",mA,mB,mC);starlight_diag::evlog("it8",mD,mE,0);}
            const double postT0=starlight_diag::events_enabled()?starlight_diag::evnow():0;double postMid=0;
            {static FILE* stallLog=[]()->FILE*{char path[512];if(!GetEnvironmentVariableA("STARLIGHT_STALL_LOG",path,512))return nullptr;FILE* file=nullptr;fopen_s(&file,path,"w");return file;}();
             if(stallLog){const double total=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();if(total>12){fprintf(stallLog,"iteration %.1f ms: lockWait %.1f sim %.1f prep %.1f overlay %.1f packet %.1f install %.1f frame %.1f\n",total,lockWaitMs,stageSim-b_sim,stagePrep-b_prep,stageOverlay-b_ov,stagePacket-b_pk,stageInstall-b_in,stageFrame-b_fr);fflush(stallLog);}}}
            if(fogProbe){std::array<double,7> sample{};if(!sl_fog_readback(viewport.get(),sample.data()))throw std::runtime_error(sl_error());std::lock_guard lock(mutex_);status_.fog_sample=sample;++status_.fog_probes;}
            if(motionProbe){std::array<double,3> sample{};if(!sl_motion_readback(viewport.get(),sample.data()))throw std::runtime_error(sl_error());std::lock_guard lock(mutex_);status_.motion_sample=sample;++status_.motion_probes;}
            if(measurePacing){
                if(!wasPacing){pacing={};lastPacingPublish=0;}
                double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                if(!pacingLog){
                    wchar_t requested[32768]{};DWORD count=GetEnvironmentVariableW(L"STARLIGHT_PACING_LOG",requested,32768);
                    fs::path path=count && count<32768?fs::path(requested):application_data()/L"Diagnostics/FramePacing"/(L"frame-pacing-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(reinterpret_cast<uintptr_t>(window_))+L".jsonl");
                    pacingLog=std::make_unique<PacingLog>(path);logStart=now;
                }
                double fence=0,pacingWait=0,present=0;sl_wait_timings(viewport.get(),&fence,&pacingWait,&present);
                unsigned long long gpuCount=0,gpuBytes=0;sl_telegraph_stats(viewport.get(),&gpuCount,&gpuBytes);
                if(wasPacing && lastLoggedFrame>0){
                    PacingFrameRecord record{++logFrame,now-logStart,(now-lastLoggedFrame)*1000,sceneCostMs,frameCallMs,sl_gpu_ms(viewport.get())};
                    record.gpuEffectCount=gpuCount;record.gpuEffectBytes=gpuBytes;record.battleWorkers=timing.battleWorkers;record.simulationMs=timing.simulationMs;record.combatAiMs=timing.combatAiMs;record.physicsMs=timing.physicsMs;record.presentationMs=timing.presentationMs;
                    record.fenceWaitMs=fence-priorFence;record.pacingWaitMs=pacingWait-priorPacing;record.presentWaitMs=present-priorPresent;
                    DWORD foregroundProcess=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundProcess);
                    record.applicationForeground=foregroundProcess==GetCurrentProcessId();
                    record.windowMinimized=IsIconic(GetAncestor(window_,GA_ROOT))!=FALSE;
                    record.windowVisible=IsWindowVisible(window_)!=FALSE;
                    record.inputSampleAgeMs=timing.inputSampleTime>0?(now-timing.inputSampleTime)*1000:-1;record.inputEventAgeMs=inputEvent>0?(now-inputEvent)*1000:-1;
                    record.updateSequence=updateSequence;record.inputSequence=inputSequence;record.events=std::move(events);pacingFrames.push_back(std::move(record));
                }
                std::vector<double> workerValues(sl_multicore_read(viewport.get(),nullptr,0));
                sl_multicore_read(viewport.get(),workerValues.data(),int(workerValues.size()));
                ParallelWorkReading work;
                work.parallelBatches=unsigned(workerValues[0]);work.serialBatches=unsigned(workerValues[1]);work.participants=unsigned(workerValues[2]);work.items=static_cast<unsigned long long>(workerValues[3]);work.wallMs=workerValues[4];work.callerWaitMs=workerValues[5];work.chunkMs.assign(workerValues.begin()+6,workerValues.end());
                if(wasPacing && !pacingFrames.empty())pacingFrames.back().multicore=work;
                priorFence=fence;priorPacing=pacingWait;priorPresent=present;
                lastLoggedFrame=now;
                pacing.frame(now);
                pacing.pose("camera","Camera",camera.eye,{camera.eye.x,camera.eye.y,camera.eye.z,camera.target.x,camera.target.y,camera.target.z,camera.up.x,camera.up.y,camera.up.z},now);
                for(const auto& [id,pose]:debrisPoses)pacing.pose(id,id.starts_with("projectile-")?"Projectile":"Physical debris",{pose[0],pose[1],pose[2]},std::vector<double>(pose.begin(),pose.end()),now);
                if(!playerPose.empty())pacing.pose("player","Player",playerPosition,playerPose,now);
                for(const auto& c:simulation.Characters){auto at=c.at(routeTime);
                    std::vector<double> pose{at.X,c.Y,at.Z,c.RotationX,c.RotationY,c.RotationZ,c.Size,c.Width,c.Height,c.Depth};
                    if(c.PhysicsRotation)pose.insert(pose.end(),c.PhysicsRotation->begin(),c.PhysicsRotation->end());
                    for(const auto& point:c.JointRuntimePoints)pose.insert(pose.end(),point.begin(),point.end());
                    pacing.pose(c.Id,c.Name,{at.X,c.Y+c.Size*c.Height,at.Z},std::move(pose),now);
                    auto kind=c.Shape.value_or("");
                    if(kind=="grass" || kind=="chaos-lightning" || kind=="lava" || kind=="fire" || kind=="craxis-smoke")
                        pacing.effect(c.Id,{sl_effect_time(viewport.get())},"rendered-effect-time",now);
                }
                if(now-lastPacingPublish>=.1){auto snapshot=pacing.snapshot(now);snapshot.gpuEffectCount=gpuCount;snapshot.gpuEffectBytes=gpuBytes;snapshot.multicore=work;snapshot.battleWorkers=timing.battleWorkers;snapshot.logFailed=pacingLog->failed();snapshot.droppedLogRecords=pacingLog->dropped();pacingLog->push(snapshot,std::move(pacingFrames),now-logStart);pacingFrames.clear();std::lock_guard lock(mutex_);pacingSnapshot_=std::move(snapshot);lastPacingPublish=now;}
            }
            if(postT0>0)postMid=starlight_diag::evnow()-postT0;
            wasPacing=measurePacing;
            {
                std::lock_guard lock(mutex_);
                status_.frames++;
                status_.stage_sim_ms=stageSim;status_.stage_prep_ms=stagePrep;status_.stage_overlay_ms=stageOverlay;status_.stage_packet_ms=stagePacket;status_.stage_install_ms=stageInstall;status_.stage_frame_ms=stageFrame;
                status_.scene_delta_updates=deltaUpdates;status_.scene_base_installs=baseInstalls;status_.scene_full_updates=fullUpdates;
                status_.camera_repeat_frames=cameraRepeats;status_.camera_longest_repeat=cameraLongest;status_.camera_changes=cameraChanges;status_.camera_change_gap_max_ms=cameraGapMax;status_.camera_change_gap_sum_ms=cameraGapSum;
                {const auto flowProfile=flow_geometry_profile();status_.flow_setup_ms=flowProfile.setupMs;status_.flow_grid_ms=flowProfile.gridMs;status_.flow_refine_ms=flowProfile.refineMs;status_.flow_clip_ms=flowProfile.clipMs;status_.flow_vertex_ms=flowProfile.vertexMs;status_.flow_obstacle_ms=flowProfile.obstacleMs;status_.flow_normals_ms=flowProfile.normalsMs;status_.flow_geometry_calls=flowProfile.calls;status_.flow_vertices=flowProfile.vertices;status_.flow_triangles=flowProfile.triangles;status_.flow_clip_topology_ms=flowProfile.clipTopologyMs;status_.flow_clip_crossings_ms=flowProfile.clipCrossingsMs;status_.flow_clip_clearance_ms=flowProfile.clipClearanceMs;}
                {const auto profile=render_scene_profile();status_.packet_flow_geometry_ms=profile.flowGeometryMs;status_.packet_parts_ms=profile.partsMs;status_.packet_serialize_ms=profile.serializeMs;status_.packet_base_ms=profile.baseMs;status_.packet_bytes=profile.bytes;status_.effect_delta_frames=profile.effectDeltaFrames;status_.effect_delta_actors=profile.effectDeltaActors;status_.effect_whole_republish=bypass_counters().effectRepublish.load();status_.light_channel_frames=bypass_counters().lightChannelFrames.load();status_.light_channel_changes=bypass_counters().lightChannelChanges.load();status_.emitter_channel_changes=bypass_counters().emitterChannelChanges.load();status_.body_cache_hits=profile.geometry.bodyHits;status_.body_cache_misses=profile.geometry.bodyMisses;status_.body_cache_evicted=profile.geometry.bodyEvicted;status_.body_cache_pinned_skips=profile.geometry.bodyPinnedSkips;status_.body_cache_entries=profile.geometry.bodyEntries;status_.body_cache_bytes=profile.geometry.bodyBytes;status_.parts_cache_hits=profile.geometry.partHits;status_.parts_cache_misses=profile.geometry.partMisses;status_.parts_cache_evicted=profile.geometry.partEvicted;status_.parts_cache_entries=profile.geometry.partEntries;status_.parts_cache_bytes=profile.geometry.partBytes;}
                {double stats[10]{};if(sl_scene_stats(viewport.get(),stats,10)){status_.install_parse_ms=stats[0];status_.install_hash_ms=stats[1];status_.install_bounds_ms=stats[2];status_.install_upload_ms=stats[3];status_.install_tail_ms=stats[4];status_.install_total_ms=stats[5];status_.install_calls=uint64_t(stats[6]);status_.install_delta_calls=uint64_t(stats[7]);status_.install_bytes=uint64_t(stats[8]);status_.install_vertices=uint64_t(stats[9]);}}
                status_.frame_cpu_total_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart).count();
                if (shot) ++status_.captures;
                if (next) ++status_.scene_revision;
                sl_scene_resources(viewport.get(), &status_.scene_parts, &status_.scene_blas, &status_.pending_uploads);
                sl_upload_pool_stats(viewport.get(), &status_.upload_pool_count, &status_.upload_pool_bytes, &status_.upload_created, &status_.upload_reused);
                uint64_t grassState[4]{};if(!sl_grass_stats(viewport.get(),grassState))throw std::runtime_error("Grass statistics unavailable");status_.grass_instances=grassState[0];status_.grass_contacts=grassState[1];status_.grass_dispatches=grassState[2];status_.grass_bytes=grassState[3];
                uint64_t life[16]{};if(!sl_lifetime_stats(viewport.get(),life,16))throw std::runtime_error("Lifetime statistics unavailable");status_.lifetime_rt_pending=life[0];status_.lifetime_rt_drained=life[1];status_.lifetime_rt_max_pending=life[2];status_.lifetime_rt_forced_stalls=life[3];status_.lifetime_grass_pending=life[4];status_.lifetime_grass_drained=life[5];status_.lifetime_grass_max_pending=life[6];status_.lifetime_grass_forced_stalls=life[7];status_.lifetime_resident_pending=life[8];status_.lifetime_resident_drained=life[9];status_.lifetime_resident_max_pending=life[10];status_.lifetime_resident_forced_stalls=life[11];status_.lifetime_rt_pending_bytes=life[12];status_.lifetime_grass_pending_bytes=life[13];status_.lifetime_forced_stall_us=life[14];status_.lifetime_side_staging_pending=life[15];
                status_.seconds = seconds;
                status_.playing = play;
                status_.gpu_ms = sl_gpu_ms(viewport.get());
                uint64_t post[4]{};if(!sl_hdr_post_stats(viewport.get(),post))throw std::runtime_error("Glow statistics unavailable");status_.bloom_levels=post[0];status_.hdr_post_bytes=post[1];
                uint64_t sun[4]{},local[6]{};if(!sl_sun_shadow_stats(viewport.get(),sun)||!sl_local_shadow_stats(viewport.get(),local))throw std::runtime_error("Shadow statistics unavailable");status_.sun_shadow_resolution=sun[0];status_.local_shadow_resolution=local[0];status_.local_shadow_capacity=local[2];status_.shadow_bytes=sun[1]+local[1];
                status_.ray_ms = sl_ray_ms(viewport.get());
                status_.denoise_ms = sl_denoise_ms(viewport.get());
                status_.tier = sl_rt_active(viewport.get());
                status_.rr = sl_rr_active(viewport.get()) != 0;
                status_.width = sl_render_width(viewport.get());
                // Coalesce to one UI update. Simulation remains on its owning
                // thread, but is no longer held behind a 16 ms window timer.
                if(updateWindow_ && !updatePosted_)updatePosted_=PostMessageW(updateWindow_,frameUpdateMessage,0,0)!=0;
            }
            if(postT0>0)starlight_diag::evlog("post",starlight_diag::evnow()-postT0,postMid,0);
            // Match the reference editor's continuous native render loop.
            // Suspension and stop handling remain at the top of the loop;
            // GPU fences/presentation provide rendering backpressure.
        }
        if(pacingLog && !pacingFrames.empty())pacingLog->push(pacing.snapshot(lastLoggedFrame),std::move(pacingFrames),lastLoggedFrame-logStart);
        auto report = status();
        atomic_write(application_data() / L"Diagnostics/NativeEditor" /
                         (L"session-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(reinterpret_cast<uintptr_t>(window_)) + L".json"),
                     json{{"frames", report.frames}, {"renderWidth", report.width},
                          {"gpuMs", report.gpu_ms}, {"rayMs", report.ray_ms},
                          {"denoiseMs", report.denoise_ms}, {"activeTier", report.tier},
                          {"rrActive", report.rr}, {"upscaling", false},
                          {"frameGeneration", false}}.dump(2));
    } catch (const std::exception &e) {
        diagnostic("native renderer worker", e.what());
        std::lock_guard lock(mutex_);
        status_.error = e.what();
    }
}
} // namespace starlight::editor
