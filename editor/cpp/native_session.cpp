#include "native_session.hpp"
#include "interactions.hpp"
#include "physics.hpp"
#include <chrono>
namespace {
extern "C" {
__declspec(dllimport) void *sl_create(HWND, int, int);
__declspec(dllimport) int sl_scene(void *, const unsigned char *, int);
__declspec(dllimport) int sl_frame(void *, double, float);
__declspec(dllimport) void sl_camera(void *, float, float, float, float, float, float);
__declspec(dllimport) void sl_destroy(void *);
__declspec(dllimport) void sl_shot(void *, const wchar_t *);
__declspec(dllimport) const char *sl_error();
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
        int w = 0, h = 0;
        double seconds = 0, simulationRender = 0;
        bool physicsNeeded = false;
        auto previous = std::chrono::steady_clock::now();
        std::vector<uint8_t> packet;
        auto reset = [&] {
            simulation = authored;
            seconds = simulationRender = 0;
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
            bool play, doReset, repeat, toolsDirty;
            std::string follow, selected, tool;
            int requestedW, requestedH;
            std::optional<SceneData> next;
            std::optional<fs::path> shot;
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
                play = playing_;
                repeat = repeat_;
                follow = follow_;
                selected = selected_;
                tool = tool_;
                toolsDirty = tools_dirty_;
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
            bool rebuild = toolsDirty;
            if (next) {
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
                    simulationRender += dt;
                    if ((flow || physics) && simulationRender >= .05) {
                        rebuild = true;
                        simulationRender = 0;
                    }
                }
            }
            if (!viewport || w != requestedW || h != requestedH) {
                viewport.reset();
                w = requestedW;
                h = requestedH;
                viewport.reset(sl_create(window_, w, h));
                if (!viewport)
                    throw std::runtime_error(sl_error());
                rebuild = true;
            }
            if (rebuild || packet.empty()) {
                auto overlays = editor_overlays(simulation, selected, tool, play);
                packet = render_scene(simulation, flow.get(), overlays);
                if (packet.size() > INT_MAX || !sl_scene(viewport.get(), packet.data(), int(packet.size())))
                    throw std::runtime_error(sl_error());
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
            if (play && duration > 0 && !physicsNeeded && !flow && !repeat && seconds >= duration) {
                seconds = duration;
                play = false;
                std::lock_guard lock(mutex_);
                playing_ = false;
            }
            double routeTime = repeat && duration > 0 ? std::fmod(seconds, duration) : seconds;
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
            if (shot)
                sl_shot(viewport.get(), shot->c_str());
            if (!sl_frame(viewport.get(), routeTime, float(camera.distance)))
                throw std::runtime_error(sl_error());
            {
                std::lock_guard lock(mutex_);
                status_.frames++;
                status_.seconds = seconds;
                status_.playing = play;
                status_.gpu_ms = sl_gpu_ms(viewport.get());
                status_.ray_ms = sl_ray_ms(viewport.get());
                status_.denoise_ms = sl_denoise_ms(viewport.get());
                status_.tier = sl_rt_active(viewport.get());
                status_.rr = sl_rr_active(viewport.get()) != 0;
                status_.width = sl_render_width(viewport.get());
            }
            std::unique_lock lock(mutex_);
            condition_.wait_until(lock, begin + std::chrono::milliseconds(8),
                                  [&] { return stop.stop_requested() || pending_.has_value(); });
        }
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
