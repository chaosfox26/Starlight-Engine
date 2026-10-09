#pragma once
#include <atomic>
#include <chrono>
#include "environment_events.hpp"
#include "platform.hpp"
#include "interactions.hpp"
#include "render_scene.hpp"
#include "frame_pacing.hpp"
#include "runtime_timing.hpp"
#include <condition_variable>
#include <deque>
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
    uint64_t gpu_fence_done=0; // highest request_gpu_fence() number whose GPU work has completed
    bool preload_pending=false; // a bulk preload of prepared pieces is still reaching the graphics card
    uint64_t preload_generation_requested=0,preload_generation_submitted=0;
    uint64_t preload_pinned_parts=0,preload_pin_holds=0;
    uint64_t gpu_fence_preload_generation=0,gpu_fence_scene_generation=0;
    uint64_t scene_generation_installed=0;
    // Where a scene update goes (milliseconds are cumulative render-thread totals).
    double stage_sim_ms=0,stage_prep_ms=0,stage_overlay_ms=0,stage_packet_ms=0,stage_install_ms=0,stage_frame_ms=0;
    double packet_flow_geometry_ms=0,packet_parts_ms=0,packet_serialize_ms=0,packet_base_ms=0;
    double install_parse_ms=0,install_hash_ms=0,install_bounds_ms=0,install_upload_ms=0,install_tail_ms=0,install_total_ms=0;
    uint64_t install_calls=0,install_delta_calls=0,install_bytes=0,install_vertices=0,packet_bytes=0;
    uint64_t scene_delta_updates=0,scene_base_installs=0,scene_full_updates=0;uint64_t effect_delta_frames=0,effect_delta_actors=0,effect_whole_republish=0,light_channel_frames=0,light_channel_changes=0,emitter_channel_changes=0;uint64_t body_cache_hits=0,body_cache_misses=0,body_cache_evicted=0,body_cache_pinned_skips=0,body_cache_entries=0,body_cache_bytes=0,parts_cache_hits=0,parts_cache_misses=0,parts_cache_evicted=0,parts_cache_entries=0,parts_cache_bytes=0;
    double flow_setup_ms=0,flow_grid_ms=0,flow_refine_ms=0,flow_clip_ms=0,flow_vertex_ms=0,flow_obstacle_ms=0,flow_normals_ms=0;
    uint64_t flow_geometry_calls=0,flow_vertices=0,flow_triangles=0;
    double flow_clip_topology_ms=0,flow_clip_crossings_ms=0,flow_clip_clearance_ms=0;
    // Resident rubble publication on the render thread (cumulative ms and the worst single publication).
    double rubble_serialize_ms=0,rubble_upload_ms=0,rubble_instances_ms=0,rubble_worst_publish_ms=0;uint64_t rubble_uploaded_meshes=0;
    // Camera delivery: rendered frames that reused the previous frame's exact camera, and the longest run of such frames.
    uint64_t camera_repeat_frames=0,camera_longest_repeat=0,camera_changes=0;double camera_change_gap_max_ms=0,camera_change_gap_sum_ms=0;
    uint64_t grass_instances=0,grass_contacts=0,grass_dispatches=0,grass_bytes=0;
    uint64_t lifetime_rt_pending=0,lifetime_rt_drained=0,lifetime_rt_max_pending=0,lifetime_rt_forced_stalls=0,lifetime_grass_pending=0,lifetime_grass_drained=0,lifetime_grass_max_pending=0,lifetime_grass_forced_stalls=0,lifetime_resident_pending=0,lifetime_resident_drained=0,lifetime_resident_max_pending=0,lifetime_resident_forced_stalls=0,lifetime_rt_pending_bytes=0,lifetime_grass_pending_bytes=0,lifetime_forced_stall_us=0,lifetime_side_staging_pending=0;
    uint64_t hdr_post_bytes=0,bloom_levels=0;
    uint64_t sun_shadow_resolution=0,local_shadow_resolution=0,local_shadow_capacity=0,shadow_bytes=0;
    double gpu_ms = 0, ray_ms = 0, denoise_ms = 0, seconds = 0;
    size_t effect_sparks=0,fire_particles=0,liquid_splashes=0,fog_bodies=0,fog_moving_bodies=0;
    size_t rain_particles=0;
    uint64_t rain_impacts=0;
    double liquid_volume=0,liquid_wave_energy=0;
    uint64_t environment_epoch=0,environment_lightning=0;
    std::vector<LightningSoundEvent> environment_sounds;
    uint64_t motion_probes=0,fog_probes=0;
    std::array<double,7> fog_sample{};
    std::array<double,3> motion_sample{};
    int width = 0, tier = 0;
    bool rr = false, playing = false, suspended = false;
    bool physics_pending = false;  // a loading barrier is preparing the rigid-body session and it has not finished cooking yet
    bool physics_prepared = false; // the rigid-body session was created and finished preparing behind the loading barrier
    std::string error;
};
// A rigid mesh drawn at a pose. The mesh is uploaded to the GPU once per distinct mesh (identified by contentKey when set, otherwise by address;
// the part must be immutable while shared) and only the pose travels each update.
struct RubbleInstance {std::shared_ptr<const MeshPart> part;Vec3 position{};std::array<double,4> rotation{0,0,0,1};uint64_t serial=0,contentKey=0;};
class NativeSession {
    HWND window_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::jthread thread_;
    std::optional<SceneData> pending_;
    bool pendingRuntime_=false,motionProbe_=false,fogProbe_=false;
    std::optional<fs::path> capture_;
    std::deque<fs::path> captureQueue_; // one queued capture is taken per rendered frame (consecutive-frame captures)
    CameraState camera_;
    std::optional<Bounds> playerBody_;
    std::vector<Character> collisionObjects_;
    std::optional<double> gameplayTime_;
    RenderStatus status_;
    int width_ = 1, height_ = 1;
    bool playing_ = false, reset_ = false, repeat_ = false, suspended_ = false;
    std::string follow_;
    std::string selected_, tool_;
    bool tools_dirty_ = false;
    // Immutable once published: the render thread shares it instead of deep-copying every frame.
    std::shared_ptr<const std::vector<MeshPart>> gameplayOverlays_=std::make_shared<const std::vector<MeshPart>>();
    bool gameplayOverlaysDirty_=false;
    bool pacingEnabled_=false;
    std::vector<double> playerPose_;
    Vec3 playerPosition_{};
    std::vector<std::pair<std::string,std::array<double,7>>> debrisPoses_;
    PacingSnapshot pacingSnapshot_;
    std::shared_ptr<const LiquidSnapshot> liquidSnapshot_;
    HWND updateWindow_=nullptr;
    bool updatePosted_=false;
    RuntimeTiming runtimeTiming_;
    uint64_t runtimeSequence_=0,inputSequence_=0;
    double inputEventTime_=0;
    int attackCount_=-1;
    std::vector<GpuTelegraph> gpuTelegraphs_;
    std::vector<Character> runtimeObjects_;
    std::shared_ptr<const std::vector<RubbleInstance>> rubble_=std::make_shared<const std::vector<RubbleInstance>>();
    bool rubbleDirty_=false;
    std::shared_ptr<const std::vector<std::shared_ptr<const MeshPart>>> rubblePreload_=std::make_shared<const std::vector<std::shared_ptr<const MeshPart>>>();
    bool rubblePreloadDirty_=false,rubblePreloadBulk_=false;uint64_t fenceRequested_=0,preloadGeneration_=0,sceneGeneration_=0;
    bool preloadPinned_=false,prewarmPlay_=false;
    std::vector<std::shared_ptr<const MeshPart>> preloadPinnedList_;
    bool runtimeObjectsDirty_=false;
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
    uint64_t scene(SceneData,bool runtimeUpdate=false);
    void size(int, int);
    void camera(CameraState);
    void player_body(std::optional<Bounds>);
    void collision_objects(std::vector<Character>);
    void gameplay_overlays(std::vector<MeshPart>,bool probeMotion=false);
    void rubble_instances(std::vector<RubbleInstance>);
    // Meshes expected to become instances soon (e.g. a rock that was already fractured in the background). They are
    // uploaded a few per frame so the first use costs nothing.
    // `bulk` uploads every listed mesh at once (used while a scene is loading, behind the loading screen); otherwise a few per frame.
    uint64_t rubble_preload(std::vector<std::shared_ptr<const MeshPart>>,bool bulk=false);
    void pin_preload(bool);
    // Asks the render thread to wait until the graphics card has finished everything submitted so far; returns the request number to look for in status().gpu_fence_done.
    uint64_t request_gpu_fence();
    void runtime_objects(std::vector<Character>);
    void pacing_enabled(bool);
    void pacing_player(Vec3,std::vector<double>);
    void pacing_debris(std::vector<std::pair<std::string,std::array<double,7>>>);
    PacingSnapshot pacing_snapshot();
    std::shared_ptr<const LiquidSnapshot> liquid_snapshot();
    void playback(bool, bool reset = false);
    // While true and playback is stopped, the resident static scene is built and installed exactly as play will use it (runtime effects,
    // GPU grass) with the clock still stopped, so a loading barrier prepares the scene that is revealed and starting play installs no second base.
    // playback(true) ends it; callers also clear it when a load fails.
    void prewarm_play(bool);
    void gameplay_time(std::optional<double>);
    void playback_options(bool repeat, std::string follow);
    void tools(std::string selected, std::string mode);
    void capture(fs::path);
    void probe_motion();
    void probe_fog();
    // Park between frames while keeping device/resources alive. Changes coalesce
    // in pending_; shutdown always wakes the parked worker before joining it.
    void suspend_rendering(bool);
    RenderStatus status();
};
} // namespace starlight::editor
