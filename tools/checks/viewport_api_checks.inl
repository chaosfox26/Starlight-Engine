// Regression checks for the native viewport API (audits VWP-1 to VWP-8). The module is loaded at run time like in device_fault_checks.inl.
// Every group below uses a real GPU device unless it says otherwise; STARLIGHT_SKIP_GPU_CHECKS reports them as NOT RUN, never as a pass.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <vector>

struct VwpApi {
    HMODULE module = nullptr;
    void *(*create)(HWND, int, int) = nullptr;
    void (*destroy)(void *) = nullptr;
    int (*frame)(void *, double, float) = nullptr;
    const char *(*error)() = nullptr;
    template <class F> F get(const char *name) const { return reinterpret_cast<F>(reinterpret_cast<void *>(GetProcAddress(module, name))); }
    bool open(const wchar_t *path = L"Starlight.Viewport.dll") {
        module = LoadLibraryW(path);
        if (!module) return false;
        create = get<decltype(create)>("sl_create");
        destroy = get<decltype(destroy)>("sl_destroy");
        frame = get<decltype(frame)>("sl_frame");
        error = get<decltype(error)>("sl_error");
        return create && destroy && frame && error;
    }
};
struct VwpSession {
    VwpApi api;
    HWND window = nullptr;
    ~VwpSession() { if (window) DestroyWindow(window); }
};
// Loads the module without a device; the calls that need none can run while the GPU is reserved.
template <class Require> static void vwp_load(const Require &require, VwpSession &session) {
    require(session.api.open(), "VWP: The viewport module loads and exports the lifecycle calls the API checks use");
}
static HWND vwp_window() { return CreateWindowExW(0, L"STATIC", L"Starlight viewport api checks", 0, 0, 0, 640, 360, nullptr, nullptr, nullptr, nullptr); }
// False when the checks must not run (the GPU is reserved); otherwise the module is loaded and a window exists for the viewport.
template <class Require> static bool vwp_open(const Require &require, VwpSession &session, const char *name) {
    if (std::getenv("STARLIGHT_SKIP_GPU_CHECKS")) {
        std::cout << name << ": NOT RUN (STARLIGHT_SKIP_GPU_CHECKS is set)" << std::endl;
        return false;
    }
    vwp_load(require, session);
    session.window = vwp_window();
    require(session.window != nullptr, "VWP: A window exists for the viewport api checks");
    return true;
}

struct VwpBytes {
    std::vector<uint8_t> data;
    template <class T> VwpBytes &put(const T &value) {
        const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
        data.insert(data.end(), bytes, bytes + sizeof(T));
        return *this;
    }
    VwpBytes &append(const std::vector<uint8_t> &other) {
        data.insert(data.end(), other.begin(), other.end());
        return *this;
    }
};
// Each part and resident mesh is one triangle; the vertex kind selects the special renderer paths (1.36 = GPU grass, 13.2 = dust volume).
struct VwpPart {
    uint64_t motion = 0;
    float kind = 0;
    float x = 0;
};
static void vwp_part(VwpBytes &bytes, const VwpPart &part) {
    bytes.put(part.motion).put(uint32_t(0)).put(0.f).put(int(0)).put(0.f);   // motion key, transient flag, emissive, effect, base height
    for (float value : {.5f, .5f, .5f, part.x, 0.f, 1.f, 0.f}) bytes.put(value);   // colour, x, z, size, speed
    bytes.put(uint32_t(0));   // route points
    for (float value : {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f}) bytes.put(value);
    bytes.put(uint32_t(3));   // vertex count
    const float corners[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    for (const auto &corner : corners) {
        for (float value : {corner[0], corner[1], corner[2], 0.f, 1.f, 0.f, .5f, .5f, .5f, part.kind}) bytes.put(value);
        bytes.put(uint64_t(0));
    }
    bytes.put(uint32_t(3)).put(uint32_t(0)).put(uint32_t(1)).put(uint32_t(2));
}
// A complete scene packet (version 0x42564c53) with no lights, smoke or ray tracing.
static std::vector<uint8_t> vwp_scene(const std::vector<VwpPart> &parts) {
    VwpBytes bytes;
    bytes.put(uint32_t(0x42564c53)).put(uint32_t(parts.size())).put(uint32_t(0));
    for (float value : {15.f, 0.f, 180.f, .018f}) bytes.put(value);
    bytes.put(uint32_t(0)).put(int(0)).put(int(3)).put(int(0)).put(int(1));
    for (const auto &part : parts) vwp_part(bytes, part);
    return bytes.data;
}
// A delta packet that replaces the parts after the resident base identified by the token.
static std::vector<uint8_t> vwp_delta(uint64_t token, const std::vector<VwpPart> &parts) {
    VwpBytes bytes;
    bytes.put(uint32_t(0x43564c53)).put(token).put(uint32_t(parts.size()));
    for (const auto &part : parts) vwp_part(bytes, part);
    return bytes.data;
}
// A resident mesh batch holding one triangle.
static std::vector<uint8_t> vwp_resident(uint64_t key, float kind) {
    VwpBytes blob;
    blob.put(uint32_t(0x44564c53)).put(key).put(uint32_t(3));
    const float corners[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    for (const auto &corner : corners) {
        for (float value : {corner[0], corner[1], corner[2], 0.f, 1.f, 0.f, .5f, .5f, .5f, kind}) blob.put(value);
        blob.put(uint64_t(0));
    }
    blob.put(uint32_t(3)).put(uint32_t(0)).put(uint32_t(1)).put(uint32_t(2));
    VwpBytes batch;
    batch.put(uint32_t(1)).put(uint32_t(blob.data.size())).append(blob.data);
    return batch.data;
}
struct VwpInstance {
    uint64_t mesh, motion;
    float px, py, pz, qx, qy, qz, qw, r, g, b, emissive, reserved;
};
static_assert(sizeof(VwpInstance) == 64);

// VWP-1: with ray tracing active, resident instances get their BLAS built instead of throwing from the post pass.
static void viewport_rt_instance_checks(const auto &require) {
    VwpSession session;
    if (!vwp_open(require, session, "viewport ray tracing instance checks")) return;
    const auto &api = session.api;
    const auto rt = api.get<void (*)(void *, int, int)>("sl_rt");
    const auto rtSupported = api.get<int (*)(void *)>("sl_rt_supported");
    const auto rtActive = api.get<int (*)(void *)>("sl_rt_active");
    const auto residentMesh = api.get<int (*)(void *, const uint8_t *, int)>("sl_resident_mesh");
    const auto instances = api.get<int (*)(void *, const uint8_t *, int)>("sl_instances");
    require(rt && rtSupported && rtActive && residentMesh && instances, "VWP-1: The viewport module exports the ray tracing and instance calls");
    void *viewport = api.create(session.window, 640, 360);
    require(viewport != nullptr, "VWP-1: A viewport is created");
    rt(viewport, 1, 2);
    const bool warmed = api.frame(viewport, 0, 13) == 1;
    if (!warmed || !rtSupported(viewport) || rtActive(viewport) <= 0) {
        api.destroy(viewport);
        std::cout << "viewport ray tracing instance checks: NOT RUN (ray tracing is not available on this device)" << std::endl;
        return;
    }
    const auto mesh = vwp_resident(1, 0);
    const bool uploaded = residentMesh(viewport, mesh.data(), int(mesh.size())) == 1;
    const VwpInstance instance{1, 5, 0, 0, 0, 0, 0, 0, 1, .5f, .5f, .5f, 0, 0};
    const bool listed = uploaded && instances(viewport, reinterpret_cast<const uint8_t *>(&instance), 1) == 1;
    const bool framed = listed && api.frame(viewport, 0, 13) == 1 && api.frame(viewport, 0, 13) == 1;
    const std::string why = framed ? "" : api.error();
    api.destroy(viewport);
    if (!why.empty()) std::cout << "viewport ray tracing instance checks: frame failed with: " << why << std::endl;
    require(uploaded && listed, "VWP-1: A resident mesh and an instance of it are accepted while ray tracing is requested");
    require(framed, "VWP-1: Frames render while a resident instance exists and ray tracing is active");
}

// VWP-2: a rejected fog obstacle list and a failed GPU wait report their own reason through sl_error.
static void viewport_error_text_checks(const auto &require) {
    VwpSession session;
    if (!vwp_open(require, session, "viewport error text checks")) return;
    const auto &api = session.api;
    const auto fogSources = api.get<int (*)(void *, const float *, int)>("sl_fog_sources");
    const auto fogObstacles = api.get<int (*)(void *, const float *, int)>("sl_fog_obstacles");
    const auto waitGpu = api.get<int (*)(void *)>("sl_wait_gpu");
    const auto fault = api.get<int (*)(int)>("sl_debug_fail_hresult");
    require(fogSources && fogObstacles && waitGpu && fault, "VWP-2: The viewport module exports the fog obstacle, wait and failure hook calls");
    void *viewport = api.create(session.window, 640, 360);
    require(viewport != nullptr, "VWP-2: A viewport is created");
    const float noRadius[4]{0, 0, 0, -1};
    const bool staleSet = fogSources(viewport, noRadius, 1) == 0;   // leaves an unrelated reason behind as the last error
    float obstacle[9]{};
    obstacle[0] = std::numeric_limits<float>::quiet_NaN();
    const bool obstacleRejected = fogObstacles(viewport, obstacle, 1) == 0;
    const std::string obstacleError = api.error();
    const bool staleAgain = fogSources(viewport, noRadius, 1) == 0;
    fault(1);
    const bool waitFailed = waitGpu(viewport) == 0;
    fault(0);
    const std::string waitError = api.error();
    api.destroy(viewport);
    require(staleSet && staleAgain && obstacleRejected, "VWP-2: A nonfinite fog obstacle list is rejected");
    require(obstacleError.find("fog obstacle") != std::string::npos, "VWP-2: The rejected fog obstacle list reports its own reason");
    require(waitFailed && waitError.find("GPU completion") != std::string::npos, "VWP-2: A failed GPU wait returns 0 and reports its own reason");
}

// VWP-3: a scene that fails validation leaves the resident base scene and its token as they were.
static void viewport_scene_failure_checks(const auto &require) {
    VwpSession session;
    if (!vwp_open(require, session, "viewport scene failure checks")) return;
    const auto &api = session.api;
    const auto sceneBase = api.get<int (*)(void *, const uint8_t *, int, uint64_t)>("sl_scene_base");
    const auto sceneDelta = api.get<int (*)(void *, const uint8_t *, int, uint64_t)>("sl_scene_delta");
    const auto partCount = api.get<int (*)(void *)>("sl_parts");
    require(sceneBase && sceneDelta && partCount, "VWP-3: The viewport module exports the resident scene calls");
    void *viewport = api.create(session.window, 640, 360);
    require(viewport != nullptr, "VWP-3: A viewport is created");
    const auto base = vwp_scene({{0, 0, 0}});
    const auto duplicate = vwp_scene({{5, 1.36f, 0}, {5, 1.36f, 2}});   // two grass parts with one motion key share an identity
    const auto tail = vwp_delta(7, {{0, 0, 3}});
    const bool baseOk = sceneBase(viewport, base.data(), int(base.size()), 7) == 1 && partCount(viewport) == 1;
    const bool duplicateRejected = sceneBase(viewport, duplicate.data(), int(duplicate.size()), 8) == 0;
    const std::string duplicateError = api.error();
    const int partsAfterFailure = partCount(viewport);
    const bool deltaOk = sceneDelta(viewport, tail.data(), int(tail.size()), 7) == 1;
    const int partsAfterDelta = partCount(viewport);
    const bool framed = api.frame(viewport, 0, 13) == 1;
    api.destroy(viewport);
    require(baseOk, "VWP-3: A resident base scene installs");
    require(duplicateRejected && duplicateError.find("Duplicate grass instance identity") != std::string::npos, "VWP-3: Two grass parts with one identity are rejected");
    require(partsAfterFailure == 1, "VWP-3: The rejected scene leaves the resident parts untouched");
    require(deltaOk && partsAfterDelta == 2, "VWP-3: The resident base token still accepts a delta after a rejected scene");
    require(framed, "VWP-3: The viewport still renders after a rejected scene");
}

// VWP-4: a resize that failed part way is redone by the identical retry instead of reporting success.
static void viewport_resize_failure_checks(const auto &require) {
    VwpSession session;
    if (!vwp_open(require, session, "viewport resize failure checks")) return;
    const auto &api = session.api;
    const auto resize = api.get<int (*)(void *, int, int)>("sl_resize");
    const auto waterBytes = api.get<uint64_t (*)(void *)>("sl_water_scene_bytes");
    const auto fault = api.get<int (*)(int)>("sl_debug_fail_hresult");
    require(resize && waterBytes && fault, "VWP-4: The viewport module exports the resize calls");
    void *reference = api.create(session.window, 640, 360);
    require(reference != nullptr, "VWP-4: A reference viewport is created");
    const bool referenceResized = resize(reference, 800, 450) == 1;
    const uint64_t expected = waterBytes(reference);
    api.destroy(reference);
    require(referenceResized && expected != 0, "VWP-4: A clean resize rebuilds the water scene textures");
    int failures = 0;
    for (int n = 1; n <= 6; ++n) {
        void *viewport = api.create(session.window, 640, 360);
        require(viewport != nullptr, "VWP-4: A viewport is created for the resize failure sweep");
        fault(n);
        const bool failed = resize(viewport, 800, 450) == 0;
        fault(0);
        const bool retried = resize(viewport, 800, 450) == 1;
        const uint64_t bytes = waterBytes(viewport);
        const bool framed = api.frame(viewport, 0, 13) == 1 && api.frame(viewport, 0, 13) == 1;
        api.destroy(viewport);
        if (failed) ++failures;
        if (failed) std::cout << "viewport resize failure checks: failure " << n << " retry=" << retried << " water scene bytes " << bytes << " expected " << expected << " framed=" << framed << std::endl;
        require(!failed || (retried && bytes == expected && framed), "VWP-4: Retrying a resize that failed part way rebuilds every size dependent resource");
    }
    require(failures >= 1, "VWP-4: The failure sweep failed at least one resize on purpose");
}

// VWP-5: a frame that throws after grass was queued leaves no stale grass entries behind.
static void viewport_grass_pending_checks(const auto &require) {
    VwpSession session;
    if (!vwp_open(require, session, "viewport grass pending checks")) return;
    const auto &api = session.api;
    const auto scene = api.get<int (*)(void *, const uint8_t *, int)>("sl_scene");
    require(scene != nullptr, "VWP-5: The viewport module exports the scene call");
    void *viewport = api.create(session.window, 640, 360);
    require(viewport != nullptr, "VWP-5: A viewport is created");
    std::vector<VwpPart> overfull{{11, 1.36f, 0}};   // one grass part is queued first, then 25 dust volumes exceed the 24 allowed
    for (int i = 0; i < 25; ++i) overfull.push_back({0, 13.2f, float(i)});
    const auto first = vwp_scene(overfull);
    const auto second = vwp_scene({{0, 0, 0}});
    const bool installed = scene(viewport, first.data(), int(first.size())) == 1;
    const bool failed = installed && api.frame(viewport, 0, 13) == 0;
    const std::string failure = api.error();
    const bool replaced = scene(viewport, second.data(), int(second.size())) == 1;   // the grass part goes away
    const bool framed = replaced && api.frame(viewport, 0, 13) == 1 && api.frame(viewport, 0, 13) == 1;
    api.destroy(viewport);
    require(installed && failed && failure.find("Dust volume capacity exceeded") != std::string::npos, "VWP-5: Too many dust volumes fail the frame");
    require(replaced && framed, "VWP-5: Frames after the failed frame and a scene without grass render without touching freed grass entries");
}

// VWP-6: a resident mesh that uses the GPU grass vertex kind is rejected up front.
static void viewport_resident_mesh_checks(const auto &require) {
    VwpSession session;
    if (!vwp_open(require, session, "viewport resident mesh checks")) return;
    const auto &api = session.api;
    const auto residentMesh = api.get<int (*)(void *, const uint8_t *, int)>("sl_resident_mesh");
    const auto instances = api.get<int (*)(void *, const uint8_t *, int)>("sl_instances");
    require(residentMesh && instances, "VWP-6: The viewport module exports the resident mesh calls");
    void *viewport = api.create(session.window, 640, 360);
    require(viewport != nullptr, "VWP-6: A viewport is created");
    const auto grassMesh = vwp_resident(1, 1.36f);
    const auto plainMesh = vwp_resident(2, 0.f);
    const bool grassRejected = residentMesh(viewport, grassMesh.data(), int(grassMesh.size())) == 0;
    const std::string grassError = api.error();
    const bool plainAccepted = residentMesh(viewport, plainMesh.data(), int(plainMesh.size())) == 1;
    const VwpInstance instance{2, 6, 0, 0, 0, 0, 0, 0, 1, .5f, .5f, .5f, 0, 0};
    const bool listed = plainAccepted && instances(viewport, reinterpret_cast<const uint8_t *>(&instance), 1) == 1;
    const bool framed = listed && api.frame(viewport, 0, 13) == 1;
    api.destroy(viewport);
    require(grassRejected && !grassError.empty(), "VWP-6: A resident mesh with the GPU grass vertex kind is rejected with a reason");
    require(plainAccepted && listed && framed, "VWP-6: An ordinary resident mesh and its instance still render");
}

// VWP-7: an overlong STARLIGHT_STALL_LOG or STARLIGHT_PASS_LOG value is ignored instead of aborting the process or opening a garbage path.
static void viewport_log_path_checks(const auto &require, const fs::path &folder) {
    VwpSession session;
    if (!vwp_open(require, session, "viewport log path checks")) return;
    // Both logs are opened once per module load, so the frames run in a private copy of the module with fresh statics.
    wchar_t original[MAX_PATH * 4]{};
    require(GetModuleFileNameW(session.api.module, original, DWORD(std::size(original))) > 0, "VWP-7: The viewport module path is known");
    const fs::path directory = folder / "viewport-log-path";
    fs::create_directories(directory);
    const fs::path copy = directory / "Starlight.Viewport.dll";
    std::error_code copied;
    fs::copy_file(original, copy, fs::copy_options::overwrite_existing, copied);
    require(!copied, "VWP-7: A private copy of the viewport module is made");
    VwpApi isolated;
    require(isolated.open(copy.c_str()), "VWP-7: The private copy of the viewport module loads");
    SetEnvironmentVariableA("STARLIGHT_STALL_LOG", std::string(510, 'a').c_str());
    SetEnvironmentVariableA("STARLIGHT_PASS_LOG", std::string(600, 'b').c_str());
    void *viewport = isolated.create(session.window, 640, 360);
    const bool framed = viewport && isolated.frame(viewport, 0, 13) == 1 && isolated.frame(viewport, 0, 13) == 1;
    const std::string why = framed || !viewport ? "" : isolated.error();
    if (viewport) isolated.destroy(viewport);
    SetEnvironmentVariableA("STARLIGHT_STALL_LOG", nullptr);
    SetEnvironmentVariableA("STARLIGHT_PASS_LOG", nullptr);
    FreeLibrary(isolated.module);
    if (!why.empty()) std::cout << "viewport log path checks: frame failed with: " << why << std::endl;
    require(viewport != nullptr && framed, "VWP-7: Frames render when the stall and pass log paths are too long to use");
}

// VWP-8: exported calls answer a null handle or a null output pointer with a default instead of an access violation.
static bool vwp_survives(void (*call)(void *), void *context) {
    __try {
        call(context);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
static void viewport_null_argument_checks(const auto &require) {
    VwpSession session;
    vwp_load(require, session);   // a null handle needs no device
    const auto &api = session.api;
    struct Call {
        const char *name;
        std::function<void()> run;
    };
    auto symbol = [&](const char *name) { return reinterpret_cast<void *>(GetProcAddress(api.module, name)); };
#define VWP_CALL(NAME, SIGNATURE, ...) Call{NAME, [&] { reinterpret_cast<SIGNATURE>(symbol(NAME))(__VA_ARGS__); }}
    uint64_t u64s[4]{};
    unsigned long long ulls[4]{};
    double doubles[16]{};
    const std::vector<Call> nullHandle = {
        VWP_CALL("sl_environment_reset", void (*)(void *), nullptr),
        VWP_CALL("sl_effect_time", float (*)(void *), nullptr),
        VWP_CALL("sl_surface_texture_stats", void (*)(void *, uint64_t *), nullptr, u64s),
        VWP_CALL("sl_water_scene_bytes", uint64_t (*)(void *), nullptr),
        VWP_CALL("sl_benchmark", void (*)(void *, int), nullptr, 1),
        VWP_CALL("sl_benchmark_flush", void (*)(void *), nullptr),
        VWP_CALL("sl_scene_resources", void (*)(void *, unsigned long long *, unsigned long long *, unsigned long long *), nullptr, ulls, ulls + 1, ulls + 2),
        VWP_CALL("sl_upload_pool_stats", void (*)(void *, unsigned long long *, unsigned long long *, unsigned long long *, unsigned long long *), nullptr, ulls, ulls + 1, ulls + 2, ulls + 3),
        VWP_CALL("sl_telegraph_stats", void (*)(void *, unsigned long long *, unsigned long long *), nullptr, ulls, ulls + 1),
        VWP_CALL("sl_multicore_begin", void (*)(void *, int), nullptr, 1),
        VWP_CALL("sl_multicore_read", int (*)(void *, double *, int), nullptr, doubles, 16),
        VWP_CALL("sl_wait_timings", void (*)(void *, double *, double *, double *), nullptr, doubles, doubles + 1, doubles + 2),
        VWP_CALL("sl_gpu_ms", double (*)(void *), nullptr),
        VWP_CALL("sl_render_width", int (*)(void *), nullptr),
        VWP_CALL("sl_render_height", int (*)(void *), nullptr),
        VWP_CALL("sl_debug_active", int (*)(void *), nullptr),
        VWP_CALL("sl_native_contract", int (*)(void *), nullptr),
        VWP_CALL("sl_output_width", int (*)(void *), nullptr),
        VWP_CALL("sl_camera", void (*)(void *, float, float, float, float, float, float), nullptr, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f),
        VWP_CALL("sl_camera_up", void (*)(void *, float, float, float), nullptr, 0.f, 1.f, 0.f),
        VWP_CALL("sl_uploads", uint64_t (*)(void *), nullptr),
        VWP_CALL("sl_parts", int (*)(void *), nullptr),
        VWP_CALL("sl_captured", int (*)(void *), nullptr),
        VWP_CALL("sl_shot", void (*)(void *, const wchar_t *), nullptr, L"shot.png"),
        VWP_CALL("sl_rt", void (*)(void *, int, int), nullptr, 1, 2),
        VWP_CALL("sl_freeze", void (*)(void *, int), nullptr, 1),
        VWP_CALL("sl_rt_supported", int (*)(void *), nullptr),
        VWP_CALL("sl_rt_active", int (*)(void *), nullptr),
        VWP_CALL("sl_ray_ms", double (*)(void *), nullptr),
        VWP_CALL("sl_as_ms", double (*)(void *), nullptr),
        VWP_CALL("sl_blas_builds", uint64_t (*)(void *), nullptr),
        VWP_CALL("sl_tlas_refits", uint64_t (*)(void *), nullptr),
        VWP_CALL("sl_rt_samples", int (*)(void *), nullptr),
        VWP_CALL("sl_rt_status", const char *(*)(void *), nullptr),
        VWP_CALL("sl_denoise_ms", double (*)(void *), nullptr),
        VWP_CALL("sl_history_resets", uint64_t (*)(void *), nullptr),
        VWP_CALL("sl_rr_active", int (*)(void *), nullptr),
        VWP_CALL("sl_rr_status", const char *(*)(void *), nullptr),
        VWP_CALL("sl_denoiser", void (*)(void *, int), nullptr, 1),
        VWP_CALL("sl_denoise_history_index", int (*)(void *), nullptr),
        VWP_CALL("sl_denoise_capture", int (*)(void *, int, const wchar_t *), nullptr, 0, L"capture.bin"),
    };
    std::string crashed;
    for (const auto &call : nullHandle) {
        require(symbol(call.name) != nullptr, "VWP-8: The viewport module exports every call the null argument checks use");
        std::function<void()> run = call.run;
        if (!vwp_survives([](void *context) { (*static_cast<std::function<void()> *>(context))(); }, &run)) crashed += std::string(" ") + call.name;
    }
    if (!crashed.empty()) std::cout << "viewport null argument checks: crashed with a null handle:" << crashed << std::endl;
    require(crashed.empty(), "VWP-8: Every exported call returns a default for a null handle");
    if (std::getenv("STARLIGHT_SKIP_GPU_CHECKS")) {
        std::cout << "viewport null argument checks (output pointers): NOT RUN (STARLIGHT_SKIP_GPU_CHECKS is set)" << std::endl;
        return;
    }
    session.window = vwp_window();
    require(session.window != nullptr, "VWP: A window exists for the viewport api checks");
    void *viewport = api.create(session.window, 640, 360);
    require(viewport != nullptr, "VWP-8: A viewport is created");
    const std::vector<Call> nullOutput = {
        VWP_CALL("sl_surface_texture_stats", void (*)(void *, uint64_t *), viewport, nullptr),
        VWP_CALL("sl_scene_resources", void (*)(void *, unsigned long long *, unsigned long long *, unsigned long long *), viewport, nullptr, nullptr, nullptr),
        VWP_CALL("sl_upload_pool_stats", void (*)(void *, unsigned long long *, unsigned long long *, unsigned long long *, unsigned long long *), viewport, nullptr, nullptr, nullptr, nullptr),
        VWP_CALL("sl_telegraph_stats", void (*)(void *, unsigned long long *, unsigned long long *), viewport, nullptr, nullptr),
        VWP_CALL("sl_multicore_read", int (*)(void *, double *, int), viewport, nullptr, 1000),
        VWP_CALL("sl_wait_timings", void (*)(void *, double *, double *, double *), viewport, nullptr, nullptr, nullptr),
        VWP_CALL("sl_shot", void (*)(void *, const wchar_t *), viewport, nullptr),
        VWP_CALL("sl_denoise_capture", int (*)(void *, int, const wchar_t *), viewport, 0, nullptr),
    };
    std::string crashedOutput;
    for (const auto &call : nullOutput) {
        std::function<void()> run = call.run;
        if (!vwp_survives([](void *context) { (*static_cast<std::function<void()> *>(context))(); }, &run)) crashedOutput += std::string(" ") + call.name;
    }
    unsigned long long parts = 7, blas = 7, uploads = 7;
    reinterpret_cast<void (*)(void *, unsigned long long *, unsigned long long *, unsigned long long *)>(symbol("sl_scene_resources"))(viewport, &parts, &blas, &uploads);
    api.destroy(viewport);
#undef VWP_CALL
    if (!crashedOutput.empty()) std::cout << "viewport null argument checks: crashed with a null output pointer:" << crashedOutput << std::endl;
    require(crashedOutput.empty(), "VWP-8: Exported calls with output pointers return for a null output pointer");
    require(parts == 0 && blas == 0, "VWP-8: A call with valid arguments still reports the viewport state");
}

// VWP-9: a telegraph list rejected for its parameters leaves the cached effect templates and their byte count as they were.
static void viewport_telegraph_order_checks(const auto &require) {
    VwpSession session;
    if (!vwp_open(require, session, "viewport telegraph order checks")) return;
    const auto &api = session.api;
    const auto telegraphs = api.get<int (*)(void *, const float *, int)>("sl_telegraphs");
    const auto telegraphStats = api.get<void (*)(void *, unsigned long long *, unsigned long long *)>("sl_telegraph_stats");
    require(telegraphs && telegraphStats, "VWP-9: The viewport module exports the telegraph calls");
    void *viewport = api.create(session.window, 640, 360);
    require(viewport != nullptr, "VWP-9: A viewport is created");
    // Creation makes two effect templates; 62 more distinct sweeps fill the 64 template cache.
    std::vector<float> filled;
    for (int i = 1; i <= 62; ++i)
        for (float value : {0.f, 0.f, 0.f, 0.f, 1.f, 1.f, .05f * float(i), 1.f, 1.f, 1.f, 1.f}) filled.push_back(value);
    const bool filledOk = telegraphs(viewport, filled.data(), 62) == 1;
    unsigned long long count = 0, bytes = 0;
    telegraphStats(viewport, &count, &bytes);
    const unsigned long long before = bytes;
    const float invalid[11]{0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 2.5f, 1.f, 1.f, 1.f, 1.f};   // zero range: rejected, but its template key is new
    const bool rejected = telegraphs(viewport, invalid, 1) == 0;
    const std::string why = api.error();
    unsigned long long afterCount = 0, afterBytes = 0;
    telegraphStats(viewport, &afterCount, &afterBytes);
    const bool framed = api.frame(viewport, 0, 13) == 1;
    api.destroy(viewport);
    require(filledOk && count == 62 && before > 0, "VWP-9: Sixty-two telegraphs with distinct sweeps are accepted");
    require(rejected && why.find("Invalid GPU telegraph parameters") != std::string::npos, "VWP-9: A telegraph with a zero range is rejected with its reason");
    require(afterCount == 62 && afterBytes == before, "VWP-9: A rejected telegraph list leaves the cached effect templates in place");
    require(framed, "VWP-9: Frames render after a rejected telegraph list");
}
