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
