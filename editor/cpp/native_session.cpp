#include "native_session.hpp"
#include "interactions.hpp"
#include "physics.hpp"
#include "pacing_log.hpp"
#include "environment_effects.hpp"
#include "joint_geometry.hpp"
#include <chrono>
namespace {
extern "C" {
__declspec(dllimport) void *sl_create(HWND, int, int);
__declspec(dllimport) int sl_resize(void*, int, int);
__declspec(dllimport) int sl_scene(void *, const unsigned char *, int);
__declspec(dllimport) void sl_telegraph_stats(void*,unsigned long long*,unsigned long long*);
__declspec(dllimport) int sl_telegraph_ids(void*,const uint64_t*,int);
__declspec(dllimport) int sl_telegraphs(void*,const float*,int);
__declspec(dllimport) int sl_frame(void *, double, float);
__declspec(dllimport) void sl_camera(void *, float, float, float, float, float, float);
__declspec(dllimport) void sl_camera_up(void *,float,float,float);
__declspec(dllimport) void sl_destroy(void *);
__declspec(dllimport) int sl_fog_readback(void*,double*);
__declspec(dllimport) int sl_motion_readback(void*,double*);
__declspec(dllimport) int sl_environment_lights(void*,const float*,int);
__declspec(dllimport) int sl_effect_clock(void*,double);
__declspec(dllimport) int sl_fog_sources(void*,const float*,int);
__declspec(dllimport) int sl_fog_terrain(void*,const float*);
__declspec(dllimport) int sl_fog_obstacles(void*,const float*,int);
__declspec(dllimport) void sl_environment_reset(void *);
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
void NativeSession::scene(SceneData s,bool runtimeUpdate) {
    s.validate();
    {
        std::lock_guard lock(mutex_);
        pending_ = std::move(s);
        pendingRuntime_=runtimeUpdate;
        if(!runtimeUpdate){playerBody_.reset();liquidSnapshot_.reset();debrisPoses_.clear();collisionObjects_.clear();runtimeObjects_.clear();runtimeObjectsDirty_=false;}
    }
    condition_.notify_one();
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
    bool same=parts.size()==gameplayOverlays_.size();
    for(size_t i=0;same && i<parts.size();++i){
        const auto& a=parts[i];const auto& b=gameplayOverlays_[i];
        same=sameVector(a.color,b.color) && a.emissive==b.emissive && a.mesh.indices==b.mesh.indices && a.mesh.vertices.size()==b.mesh.vertices.size();
        for(size_t j=0;same && j<a.mesh.vertices.size();++j){
            const auto& x=a.mesh.vertices[j];const auto& y=b.mesh.vertices[j];
            same=sameVector(x.position,y.position) && sameVector(x.normal,y.normal) && sameVector(x.color,y.color) && x.kind==y.kind;
        }
    }
    if(same)return;
    gameplayOverlays_=std::move(parts);gameplayOverlaysDirty_=true;
}
void NativeSession::gameplay_time(std::optional<double> time) {
    std::lock_guard lock(mutex_);gameplayTime_=time;
}
void NativeSession::runtime_objects(std::vector<Character> objects){
    std::lock_guard lock(mutex_);runtimeObjects_=std::move(objects);runtimeObjectsDirty_=!runtimeObjects_.empty();
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
    capture_ = std::move(p);
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
        auto reset = [&] {
            sceneCache.clear();
            simulation = authored;
            seconds = 0;
            physics.reset();
            environment.reset();fogCenters.clear();
            if(viewport)sl_environment_reset(viewport.get());
            {std::lock_guard lock(mutex_);status_.environment_epoch=environment.epoch();status_.environment_lightning=0;status_.environment_sounds.clear();status_.effect_sparks=0;status_.fire_particles=0;status_.liquid_splashes=0;status_.liquid_volume=0;status_.liquid_wave_energy=0;}
            physicsNeeded = std::any_of(simulation.Characters.begin(), simulation.Characters.end(),
                                        [](const Character &c) { return c.Dynamic; }) && !(simulation.Game && simulation.Game->Collision);
            flow = simulation.LiquidSources.empty() && !(simulation.Terrain && simulation.Terrain->WaterEnabled) &&
                       std::none_of(simulation.Characters.begin(),simulation.Characters.end(),[](const auto& c){return c.Shape=="lava";})
                       ? nullptr
                       : std::make_unique<TerrainFlow>(simulation.Terrain.value_or(TerrainData{}),
                                                       simulation.LiquidSources,simulation.Characters);
            if(flow)flow->wind(.38,.16);
        };
        bool hadLiquidDebris=false;
        auto obstacles = [&](std::span<const Character> debris=std::span<const Character>{}) {
            if (!flow)
                return;
            FlowObstacleMask mask;
            for (auto c : simulation.Characters) {
                auto k = c.Shape.value_or("");
                if (k == "grass" || k == "fire" || k == "light" || k == "lava" ||
                    k == "chaos-lightning" || k == "light-shard" || k == "shadowenemy-smoke")
                    continue;
                c=route_pose(c,seconds);
                mask.add(object_mesh(c), c);
            }
            for(const auto& body:debris)mask.add(object_mesh(body),body);
            mask.finish();
            flow->set_obstacles(std::move(mask));
        };
        while (!stop.stop_requested()) {
            auto begin = std::chrono::steady_clock::now();
            double dt = std::clamp(std::chrono::duration<double>(begin - previous).count(), 0., .05);
            previous = begin;
            CameraState camera;std::optional<Bounds> playerBody;std::vector<Character> collisionObjects;
            std::vector<MeshPart> gameplayOverlays;
            std::vector<GpuTelegraph> gpuTelegraphs;
            std::vector<Character> runtimeObjects;
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
                std::unique_lock lock(mutex_);
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
                gameplayOverlays=gameplayOverlays_;gpuTelegraphs=gpuTelegraphs_;
                if(runtimeObjectsDirty_){runtimeObjects=runtimeObjects_;runtimeObjectsDirty_=false;}
                tools_dirty_ = false;
                doReset = reset_;
                reset_ = false;
                requestedW = width_;
                requestedH = height_;
                next = std::move(pending_);
                runtimeUpdate=pendingRuntime_;pendingRuntime_=false;
                pending_.reset();
                motionProbe=motionProbe_;motionProbe_=false;fogProbe=fogProbe_;fogProbe_=false;
                shot = std::move(capture_);
                capture_.reset();
            }
            double sceneCostMs=0;
            bool rebuild = toolsDirty;
            if (next) {
                fogTerrainDirty=true;
                if(measurePacing){
                    for(const auto& c:next->Characters)if(std::none_of(authored.Characters.begin(),authored.Characters.end(),[&](const auto& old){return old.Id==c.Id;}))events.push_back("object-added:"+c.Id);
                    for(const auto& c:authored.Characters)if(std::none_of(next->Characters.begin(),next->Characters.end(),[&](const auto& fresh){return fresh.Id==c.Id;}))events.push_back("object-removed:"+c.Id);
                }
                bool resetFog=authored.FogDensity!=next->FogDensity;
                authored = std::move(*next);
                if(runtimeUpdate && !doReset){
                    simulation=authored;sceneCache.clear();
                    if(physics)physics->reset(simulation);
                    if(resetFog && viewport)sl_environment_reset(viewport.get());
                }else reset();
                obstacles();
                rebuild = true;
            } else if (doReset) {
                reset();
                obstacles();
                rebuild = true;
            }
            if(!runtimeObjects.empty()){
                for(const auto& pose:runtimeObjects)for(auto& c:simulation.Characters)if(c.Id==pose.Id && c.Dynamic){c.X=pose.X;c.Y=pose.Y;c.Z=pose.Z;c.PhysicsRotation=pose.PhysicsRotation;c.JointRuntimePoints=pose.JointRuntimePoints;c.Route.clear();break;}
                rebuild=true;obstacles();
            }
            if (play) {
                if (physicsNeeded && !physics) {
                    physics = std::make_unique<PhysicsSession>();
                    physics->reset(simulation);
                }
                if (!physics || physics->ready()) {
                    seconds += dt;
                    if(flow && (!collisionObjects.empty()||hadLiquidDebris||std::any_of(simulation.Characters.begin(),simulation.Characters.end(),[](const auto& c){return !c.Route.empty();})))obstacles(collisionObjects);hadLiquidDebris=!collisionObjects.empty();
                    if (flow)
                        flow->step(std::max(1e-6, dt));
                    if (physics && physics->step(simulation, dt, flow.get()))
                        obstacles(collisionObjects);
                    // Publish the current simulation on this rendered frame;
                    // do not hold moving objects behind a 20 Hz visual throttle.
                    if (flow || physics) rebuild = true;
                    if(flow){auto participants=simulation.Characters;participants.insert(participants.end(),collisionObjects.begin(),collisionObjects.end());if(playerBody){Character player;player.Id=lightning_player_id;player.Shape="cube";player.Dynamic=true;auto center=(playerBody->low+playerBody->high)*.5;player.X=center.x;player.Z=center.z;player.Y=playerBody->low.y;player.Width=playerBody->high.x-playerBody->low.x;player.Height=playerBody->high.y-playerBody->low.y;player.Depth=playerBody->high.z-playerBody->low.z;participants.push_back(player);}flow->interact(participants,dt,gameplayTime.value_or(seconds));for(const auto& splash:flow->splashes())environment.splash(splash);}
                    if(environment.active(simulation)){
                        auto effectScene=simulation;effectScene.Characters.insert(effectScene.Characters.end(),collisionObjects.begin(),collisionObjects.end());environment.step(effectScene,dt,gameplayTime.value_or(seconds),flow.get(),playerBody);
                        rebuild=true;
                    }
                    auto liquidState=flow?std::make_shared<const LiquidSnapshot>(flow->snapshot()):nullptr;
                    {std::lock_guard lock(mutex_);liquidSnapshot_=std::move(liquidState);status_.environment_epoch=environment.epoch();status_.environment_lightning=environment.lightning_serial();status_.environment_sounds=environment.lightning_sounds();status_.effect_sparks=environment.spark_count();status_.fire_particles=environment.flame_count();status_.liquid_splashes=environment.splash_count();status_.liquid_volume=flow?flow->volume():0;status_.liquid_wave_energy=flow?flow->wave_energy():0;}
                }
            }
            if (viewport && (w != requestedW || h != requestedH)) {
                if (!sl_resize(viewport.get(), requestedW, requestedH))
                    throw std::runtime_error(sl_error());
                w=requestedW; h=requestedH;
            }
            if (!viewport) {
                w = requestedW;
                h = requestedH;
                viewport.reset(sl_create(window_, w, h));
                if (!viewport)
                    throw std::runtime_error(sl_error());
                rebuild = true;
            }
            if(fogTerrainDirty||(flow&&play)){std::array<float,4096> heights{};for(int z=0;z<64;++z)for(int x=0;x<64;++x)heights[z*64+x]=float(atmosphere_surface(simulation.Terrain?&*simulation.Terrain:nullptr,flow.get(),-16+(x+.5)*.5,-16+(z+.5)*.5));if(!sl_fog_terrain(viewport.get(),heights.data()))throw std::runtime_error(sl_error());fogTerrainDirty=false;}
            size_t movingFogBodies=0;std::vector<float> fogBodies;std::unordered_map<std::string,Vec3> nextFogCenters;
            if(playerBody){auto center=(playerBody->low+playerBody->high)*.5;Vec3 velocity{};if(play&&dt>0)if(auto old=fogCenters.find(lightning_player_id);old!=fogCenters.end()){velocity=(center-old->second)/dt;double speed=velocity.length();if(speed>30)velocity=velocity*(30/speed);}nextFogCenters[lightning_player_id]=center;for(double value:{playerBody->low.x,playerBody->low.y,playerBody->low.z,playerBody->high.x,playerBody->high.y,playerBody->high.z,velocity.x,velocity.y,velocity.z})fogBodies.push_back(float(value));if(velocity.length()>.05)++movingFogBodies;}
            auto fogObjects=simulation.Characters;fogObjects.insert(fogObjects.end(),collisionObjects.begin(),collisionObjects.end());
            for(auto c:fogObjects)if(lightning_solid(c)){
                if(play){c=route_pose(c,gameplayTime.value_or(seconds));}auto box=object_bounds(c);auto center=(box.low+box.high)*.5;Vec3 velocity{};
                if(play&&dt>0)if(auto old=fogCenters.find(c.Id);old!=fogCenters.end()){velocity=(center-old->second)/dt;double speed=velocity.length();if(speed>30)velocity=velocity*(30/speed);}
                nextFogCenters[c.Id]=center;
                if(box.high.x< -16||box.low.x>16||box.high.z< -16||box.low.z>16||box.high.y< -2||box.low.y>14)continue;
                if(fogBodies.size()<128*9&&velocity.length()>.05)++movingFogBodies;
                if(fogBodies.size()<128*9)for(double value:{box.low.x,box.low.y,box.low.z,box.high.x,box.high.y,box.high.z,velocity.x,velocity.y,velocity.z})fogBodies.push_back(float(value));
            }
            fogCenters=std::move(nextFogCenters);
            {std::lock_guard lock(mutex_);status_.fog_bodies=fogBodies.size()/9;status_.fog_moving_bodies=movingFogBodies;}
            if(!sl_fog_obstacles(viewport.get(),fogBodies.data(),int(fogBodies.size()/9)))throw std::runtime_error(sl_error());
            sl_multicore_begin(viewport.get(),measurePacing?1:0);
            if (rebuild || packet.empty()) {
                auto uploadStart=std::chrono::steady_clock::now();
                auto overlays = editor_overlays(simulation, selected, tool, play);
                overlays.insert(overlays.end(),gameplayOverlays.begin(),gameplayOverlays.end());
                if(play){auto effects=environment.geometry(simulation);overlays.insert(overlays.end(),std::make_move_iterator(effects.begin()),std::make_move_iterator(effects.end()));}
                auto joints=joint_geometry(simulation,authored);overlays.insert(overlays.end(),std::make_move_iterator(joints.begin()),std::make_move_iterator(joints.end()));
                packet = render_scene(simulation, flow.get(), overlays, &sceneCache,play);
                if (packet.size() > INT_MAX || !sl_scene(viewport.get(), packet.data(), int(packet.size())))
                    throw std::runtime_error(sl_error());
                sceneCostMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count();
                {std::lock_guard lock(mutex_);status_.scene_cpu_total_ms+=sceneCostMs;++status_.scene_updates;}
            }
            std::vector<float> telegraphData;std::vector<uint64_t> telegraphIds;telegraphData.reserve(gpuTelegraphs.size()*11);
            for(const auto& effect:gpuTelegraphs){const auto* data=reinterpret_cast<const float*>(&effect);telegraphData.insert(telegraphData.end(),data,data+11);telegraphIds.push_back(effect.identity);}
            if(!sl_telegraphs(viewport.get(),telegraphData.data(),int(gpuTelegraphs.size()))||!sl_telegraph_ids(viewport.get(),telegraphIds.data(),int(telegraphIds.size())))throw std::runtime_error(sl_error());
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
            sl_camera(viewport.get(), float(camera.eye.x), float(camera.eye.y), float(camera.eye.z),
                      float(camera.target.x), float(camera.target.y), float(camera.target.z));
            sl_camera_up(viewport.get(),float(camera.up.x),float(camera.up.y),float(camera.up.z));
            if (shot)
                sl_shot(viewport.get(), shot->c_str());
            if(!sl_effect_clock(viewport.get(),routeTime))throw std::runtime_error("Invalid environment simulation clock");
            auto environmentLights=environment_lights(simulation,routeTime,flow.get());if(!sl_environment_lights(viewport.get(),environmentLights.empty()?nullptr:environmentLights[0].data(),int(environmentLights.size())))throw std::runtime_error(sl_error());
            auto smokeSources=smoke_sources(simulation,routeTime,flow.get());if(!sl_fog_sources(viewport.get(),smokeSources.empty()?nullptr:smokeSources[0].data(),int(smokeSources.size())))throw std::runtime_error(sl_error());
            auto frameStart=std::chrono::steady_clock::now();
            if (!sl_frame(viewport.get(), routeTime, float(camera.distance)))
                throw std::runtime_error(sl_error());
            const double frameCallMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart).count();
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
                    if(kind=="grass" || kind=="chaos-lightning" || kind=="lava" || kind=="fire" || kind=="shadowenemy-smoke")
                        pacing.effect(c.Id,{sl_effect_time(viewport.get())},"rendered-effect-time",now);
                }
                if(now-lastPacingPublish>=.1){auto snapshot=pacing.snapshot(now);snapshot.gpuEffectCount=gpuCount;snapshot.gpuEffectBytes=gpuBytes;snapshot.multicore=work;snapshot.battleWorkers=timing.battleWorkers;snapshot.logFailed=pacingLog->failed();snapshot.droppedLogRecords=pacingLog->dropped();pacingLog->push(snapshot,std::move(pacingFrames),now-logStart);pacingFrames.clear();std::lock_guard lock(mutex_);pacingSnapshot_=std::move(snapshot);lastPacingPublish=now;}
            }
            wasPacing=measurePacing;
            {
                std::lock_guard lock(mutex_);
                status_.frames++;
                status_.frame_cpu_total_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart).count();
                if (shot) ++status_.captures;
                if (next) ++status_.scene_revision;
                sl_scene_resources(viewport.get(), &status_.scene_parts, &status_.scene_blas, &status_.pending_uploads);
                sl_upload_pool_stats(viewport.get(), &status_.upload_pool_count, &status_.upload_pool_bytes, &status_.upload_created, &status_.upload_reused);
                status_.seconds = seconds;
                status_.playing = play;
                status_.gpu_ms = sl_gpu_ms(viewport.get());
                status_.ray_ms = sl_ray_ms(viewport.get());
                status_.denoise_ms = sl_denoise_ms(viewport.get());
                status_.tier = sl_rt_active(viewport.get());
                status_.rr = sl_rr_active(viewport.get()) != 0;
                status_.width = sl_render_width(viewport.get());
                // Coalesce to one UI update. Simulation remains on its owning
                // thread, but is no longer held behind a 16 ms window timer.
                if(updateWindow_ && !updatePosted_)updatePosted_=PostMessageW(updateWindow_,frameUpdateMessage,0,0)!=0;
            }
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
