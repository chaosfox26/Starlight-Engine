#include "native_session.hpp"
#include "interactions.hpp"
#include "physics.hpp"
#include "pacing_log.hpp"
#include <chrono>
namespace {
extern "C" {
__declspec(dllimport) void *sl_create(HWND, int, int);
__declspec(dllimport) int sl_resize(void*, int, int);
__declspec(dllimport) int sl_scene(void *, const unsigned char *, int);
__declspec(dllimport) int sl_frame(void *, double, float);
__declspec(dllimport) void sl_camera(void *, float, float, float, float, float, float);
__declspec(dllimport) void sl_camera_up(void *,float,float,float);
__declspec(dllimport) void sl_destroy(void *);
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
void NativeSession::scene(SceneData s) {
    s.validate();
    {
        std::lock_guard lock(mutex_);
        pending_ = std::move(s);
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
void NativeSession::gameplay_overlays(std::vector<MeshPart> parts){
    std::lock_guard lock(mutex_);
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
void NativeSession::pacing_player(Vec3 position,std::vector<double> pose){std::lock_guard lock(mutex_);playerPosition_=position;playerPose_=std::move(pose);}
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
        SceneData authored, simulation;
        RenderSceneCache sceneCache;
        int w = 0, h = 0;
        double seconds = 0;
        bool physicsNeeded = false;
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
            physicsNeeded = std::any_of(simulation.Characters.begin(), simulation.Characters.end(),
                                        [](const Character &c) { return c.Dynamic; });
            flow = simulation.LiquidSources.empty()
                       ? nullptr
                       : std::make_unique<TerrainFlow>(simulation.Terrain.value_or(TerrainData{}),
                                                       simulation.LiquidSources);
        };
        auto obstacles = [&] {
            if (!flow)
                return;
            FlowObstacleMask mask;
            for (const auto &c : simulation.Characters) {
                auto k = c.Shape.value_or("");
                if (k.empty() || k == "grass" || k == "fire" || k == "light" || k == "lava" ||
                    k == "chaos-lightning" || k == "light-shard" || k == "craxis-smoke")
                    continue;
                mask.add(object_mesh(c), c);
            }
            mask.finish();
            flow->set_obstacles(std::move(mask));
        };
        while (!stop.stop_requested()) {
            auto begin = std::chrono::steady_clock::now();
            double dt = std::clamp(std::chrono::duration<double>(begin - previous).count(), 0., .05);
            previous = begin;
            CameraState camera;
            std::vector<MeshPart> gameplayOverlays;
            bool play, doReset, repeat, toolsDirty;
            std::string follow, selected, tool;
            int requestedW, requestedH;
            std::optional<SceneData> next;
            std::optional<double> gameplayTime;
            std::optional<fs::path> shot;
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
                camera = camera_;
                measurePacing=pacingEnabled_;if(measurePacing){playerPosition=playerPosition_;playerPose=playerPose_;timing=runtimeTiming_;updateSequence=runtimeSequence_;inputSequence=inputSequence_;inputEvent=inputEventTime_;events.swap(timingEvents_);}
                gameplayTime=gameplayTime_;
                play = playing_;
                repeat = repeat_;
                follow = follow_;
                selected = selected_;
                tool = tool_;
                toolsDirty = tools_dirty_;
                toolsDirty |= gameplayOverlaysDirty_;gameplayOverlaysDirty_=false;
                gameplayOverlays=gameplayOverlays_;
                tools_dirty_ = false;
                doReset = reset_;
                reset_ = false;
                requestedW = width_;
                requestedH = height_;
                next = std::move(pending_);
                pending_.reset();
                shot = std::move(capture_);
                capture_.reset();
            }
            double sceneCostMs=0;
            bool rebuild = toolsDirty;
            if (next) {
                if(measurePacing){
                    for(const auto& c:next->Characters)if(std::none_of(authored.Characters.begin(),authored.Characters.end(),[&](const auto& old){return old.Id==c.Id;}))events.push_back("object-added:"+c.Id);
                    for(const auto& c:authored.Characters)if(std::none_of(next->Characters.begin(),next->Characters.end(),[&](const auto& fresh){return fresh.Id==c.Id;}))events.push_back("object-removed:"+c.Id);
                }
                authored = std::move(*next);
                reset();
                obstacles();
                rebuild = true;
            } else if (doReset) {
                reset();
                obstacles();
                rebuild = true;
            }
            if (play) {
                if (physicsNeeded && !physics) {
                    physics = std::make_unique<PhysicsSession>();
                    physics->reset(simulation);
                }
                if (!physics || physics->ready()) {
                    seconds += dt;
                    if (flow)
                        flow->step(std::max(1e-6, dt));
                    if (physics && physics->step(simulation, dt, flow.get()))
                        obstacles();
                    // Publish the current simulation on this rendered frame;
                    // do not hold moving objects behind a 20 Hz visual throttle.
                    if (flow || physics) {rebuild = true;sceneCache.clear();}
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
            sl_multicore_begin(viewport.get(),measurePacing?1:0);
            if (rebuild || packet.empty()) {
                auto uploadStart=std::chrono::steady_clock::now();
                auto overlays = editor_overlays(simulation, selected, tool, play);
                overlays.insert(overlays.end(),gameplayOverlays.begin(),gameplayOverlays.end());
                packet = render_scene(simulation, flow.get(), overlays, &sceneCache);
                if (packet.size() > INT_MAX || !sl_scene(viewport.get(), packet.data(), int(packet.size())))
                    throw std::runtime_error(sl_error());
                sceneCostMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-uploadStart).count();
                {std::lock_guard lock(mutex_);status_.scene_cpu_total_ms+=sceneCostMs;++status_.scene_updates;}
            }
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
            auto frameStart=std::chrono::steady_clock::now();
            if (!sl_frame(viewport.get(), routeTime, float(camera.distance)))
                throw std::runtime_error(sl_error());
            const double frameCallMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frameStart).count();
            if(measurePacing){
                if(!wasPacing){pacing={};lastPacingPublish=0;}
                double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
                if(!pacingLog){
                    wchar_t requested[32768]{};DWORD count=GetEnvironmentVariableW(L"STARLIGHT_PACING_LOG",requested,32768);
                    fs::path path=count && count<32768?fs::path(requested):application_data()/L"Diagnostics/FramePacing"/(L"frame-pacing-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(reinterpret_cast<uintptr_t>(window_))+L".jsonl");
                    pacingLog=std::make_unique<PacingLog>(path);logStart=now;
                }
                double fence=0,pacingWait=0,present=0;sl_wait_timings(viewport.get(),&fence,&pacingWait,&present);
                if(wasPacing && lastLoggedFrame>0){
                    PacingFrameRecord record{++logFrame,now-logStart,(now-lastLoggedFrame)*1000,sceneCostMs,frameCallMs,sl_gpu_ms(viewport.get())};
                    record.battleWorkers=timing.battleWorkers;record.simulationMs=timing.simulationMs;record.combatAiMs=timing.combatAiMs;record.physicsMs=timing.physicsMs;record.presentationMs=timing.presentationMs;
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
                if(!playerPose.empty())pacing.pose("player","Player",playerPosition,playerPose,now);
                for(const auto& c:simulation.Characters){auto at=c.at(routeTime);
                    std::vector<double> pose{at.X,c.Y,at.Z,c.RotationX,c.RotationY,c.RotationZ,c.Size,c.Width,c.Height,c.Depth};
                    if(c.PhysicsRotation)pose.insert(pose.end(),c.PhysicsRotation->begin(),c.PhysicsRotation->end());
                    pacing.pose(c.Id,c.Name,{at.X,c.Y+c.Size*c.Height,at.Z},std::move(pose),now);
                }
                if(now-lastPacingPublish>=.1){auto snapshot=pacing.snapshot(now);snapshot.multicore=work;snapshot.battleWorkers=timing.battleWorkers;snapshot.logFailed=pacingLog->failed();snapshot.droppedLogRecords=pacingLog->dropped();pacingLog->push(snapshot,std::move(pacingFrames),now-logStart);pacingFrames.clear();std::lock_guard lock(mutex_);pacingSnapshot_=std::move(snapshot);lastPacingPublish=now;}
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
