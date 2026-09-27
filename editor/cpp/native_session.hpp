#pragma once
#include "platform.hpp"
#include "render_scene.hpp"
#include "frame_pacing.hpp"
#include "runtime_timing.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>
namespace starlight::editor {
struct CameraState {
    Vec3 eye{0, 9.36, 13}, target{};
    double distance = 13;
    Vec3 up{0,1,0};
};
struct RenderStatus {
    unsigned long long upload_pool_count = 0, upload_pool_bytes = 0, upload_created = 0, upload_reused = 0;
    uint64_t frames = 0;
    unsigned long long scene_parts = 0, scene_blas = 0, pending_uploads = 0, captures = 0, scene_revision = 0;
    double scene_cpu_total_ms=0,frame_cpu_total_ms=0;
    uint64_t scene_updates=0;
    double gpu_ms = 0, ray_ms = 0, denoise_ms = 0, seconds = 0;
    int width = 0, tier = 0;
    bool rr = false, playing = false, suspended = false;
    std::string error;
};
class NativeSession {
    HWND window_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::jthread thread_;
    std::optional<SceneData> pending_;
    std::optional<fs::path> capture_;
    CameraState camera_;
    std::optional<double> gameplayTime_;
    RenderStatus status_;
    int width_ = 1, height_ = 1;
    bool playing_ = false, reset_ = false, repeat_ = false, suspended_ = false;
    std::string follow_;
    std::string selected_, tool_;
    bool tools_dirty_ = false;
    std::vector<MeshPart> gameplayOverlays_;
    bool gameplayOverlaysDirty_=false;
    bool pacingEnabled_=false;
    std::vector<double> playerPose_;
    Vec3 playerPosition_{};
    PacingSnapshot pacingSnapshot_;
    HWND updateWindow_=nullptr;
    bool updatePosted_=false;
    RuntimeTiming runtimeTiming_;
    uint64_t runtimeSequence_=0,inputSequence_=0;
    double inputEventTime_=0;
    int attackCount_=-1;
    std::vector<std::string> timingEvents_;
    void run(std::stop_token);

  public:
    static constexpr UINT frameUpdateMessage=WM_APP+142;
    void frame_updates(HWND);
    void frame_update_handled();
    void runtime_timing(RuntimeTiming,int attackCount);
    void input_event();
    NativeSession(HWND, int, int);
    ~NativeSession();
    NativeSession(const NativeSession &) = delete;
    NativeSession &operator=(const NativeSession &) = delete;
    void scene(SceneData);
    void size(int, int);
    void camera(CameraState);
    void gameplay_overlays(std::vector<MeshPart>);
    void pacing_enabled(bool);
    void pacing_player(Vec3,std::vector<double>);
    PacingSnapshot pacing_snapshot();
    void playback(bool, bool reset = false);
    void gameplay_time(std::optional<double>);
    void playback_options(bool repeat, std::string follow);
    void tools(std::string selected, std::string mode);
    void capture(fs::path);
    // Park between frames while keeping device/resources alive. Changes coalesce
    // in pending_; shutdown always wakes the parked worker before joining it.
    void suspend_rendering(bool);
    RenderStatus status();
};
} // namespace starlight::editor
