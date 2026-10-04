// Starlight DX12 client — upscaler / frame-generation abstraction.
// One vendor path is chosen at STARTUP (--upscaler dlss|fsr|xess|off, default = adapter vendor):
// the three vendor swapchain interposers (DLSS-G proxy, FSR-FG wrapper, XeFG wrapper) cannot
// coexist, so exactly one backend owns device/swapchain creation hooks and the per-frame hooks.
// The renderer feeds every backend the same groundwork (see main.cpp):
//   COLOR  R8G8B8A8_UNORM at render res           (NON_PIXEL_SHADER_RESOURCE at evaluate time)
//   DEPTH  D32_FLOAT, NOT inverted, near 0.1 far 400 (NON_PIXEL_SHADER_RESOURCE)
//   MVEC   R16G16_FLOAT, PIXEL units at render res, y-down, value = previousPixel - currentPixel,
//          computed from UNJITTERED current + previous MVP per instance (camera motion included)
//   EXPO   1x1 R32_FLOAT constant exposure (NON_PIXEL_SHADER_RESOURCE)
//   OUTPUT R8G8B8A8_UNORM at output res           (UNORDERED_ACCESS at evaluate time; same format
//          as the swapchain so the renderer can CopyResource it into the backbuffer)
// A backend must leave every resource in the state it received it.
// Jitter: Halton(2,3) sub-pixel offset (jx, jy) in pixels (each in [-0.5, 0.5]); the renderer adds
// (+2*jx/renderW, -2*jy/renderH) to clip.xy (i.e. proj[2][0] += 2jx/W, proj[2][1] -= 2jy/H), which
// is the convention all three SDKs document for their `jitterOffset` inputs.
#pragma once
#define NOMINMAX
#include <windows.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <cstdint>
#include <string>

#ifndef UP_DEFAULT_REQUEST
#define UP_DEFAULT_REQUEST "off"
#endif

namespace up {

enum class Path { Off = 0, DLSS, FSR, XeSS };
inline const char* pathName(Path p) {
    switch (p) { case Path::DLSS: return "dlss"; case Path::FSR: return "fsr"; case Path::XeSS: return "xess"; default: return "off"; }
}

typedef void (*LogFn)(const std::string&);

// Everything a backend needs after the device / queue / swapchain exist.
struct DeviceCtx {
    ID3D12Device*       device = nullptr;
    ID3D12CommandQueue* queue = nullptr;
    IDXGIAdapter1*      adapter = nullptr;
    HWND                hwnd = nullptr;
    UINT                outW = 0, outH = 0;
    DXGI_FORMAT         backbufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT                backbufferCount = 3;
    LogFn               log = nullptr;
};

// Per-frame inputs (all matrices row-major DirectXMath, UNJITTERED).
struct FrameIn {
    ID3D12GraphicsCommandList* cmd = nullptr;
    ID3D12Resource* color = nullptr;
    ID3D12Resource* depth = nullptr;
    ID3D12Resource* mvec = nullptr;
    ID3D12Resource* exposure = nullptr;
    ID3D12Resource* output = nullptr;      // upscaled scene (also the HUD-less color for FG)
    ID3D12Resource* backbuffer = nullptr;  // current swapchain buffer (after the blit)
    D3D12_RESOURCE_STATES backbufferState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    UINT renderW = 0, renderH = 0, outW = 0, outH = 0;
    float jitterX = 0, jitterY = 0;        // pixels, see header comment
    float dtMs = 16.0f;
    bool  reset = false;
    uint32_t frameIndex = 0;
    UINT  backbufferIndex = 0;
    DirectX::XMFLOAT4X4 view, proj, viewProj, prevViewProj;
    DirectX::XMFLOAT3 camPos, camUp, camRight, camFwd;
    float nearZ = 0.1f, farZ = 400.0f, fovY = 0.0f, aspect = 1.0f;
};

struct Upscaler {
    virtual ~Upscaler() {}
    virtual Path path() const = 0;

    // Stage 0 — BEFORE any D3D/DXGI object exists. DLSS loads sl.interposer.dll + slInit here and
    // then proxies factory/device creation; the others use the plain system entry points.
    // Returns false => the caller falls back to Path::Off.
    virtual bool preDevice(LogFn log) { (void)log; return true; }
    virtual HRESULT createFactory(UINT flags, IDXGIFactory6** out) { return CreateDXGIFactory2(flags, IID_PPV_ARGS(out)); }
    virtual HRESULT createDevice(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL fl, ID3D12Device** out) { return D3D12CreateDevice(adapter, fl, IID_PPV_ARGS(out)); }
    // Stage 1 — after device/queue/swapchain. A backend may REPLACE `swap` with its own
    // frame-generation swapchain (FSR-FG / XeFG); the renderer re-fetches backbuffers afterwards.
    virtual bool init(const DeviceCtx& ctx, Microsoft::WRL::ComPtr<IDXGISwapChain4>& swap) = 0;
    virtual void shutdown() = 0;

    // quality modes — EVERY mode the SDK enumerates; setMode() reports the SDK's optimal render size.
    virtual int  modeCount() const = 0;
    virtual const char* modeName(int i) const = 0;
    virtual int  mode() const = 0;
    virtual bool resizeOutput(UINT, UINT) { return false; }
    virtual bool setMode(int i, UINT& renderW, UINT& renderH) = 0;
    virtual bool wantsJitter() const { return true; }       // temporal paths need sub-pixel jitter; Off does not

    // frame generation
    virtual bool fgSupported() const = 0;
    virtual bool fgEnabled() const = 0;
    virtual void setFG(bool on) = 0;
    virtual void cycleMultiplier() {}                       // DLSS-G only (2x/3x/4x/dynamic)
    virtual std::string fgStatus() const = 0;               // e.g. "FG 3x", "FG off", "FG n/a (code)"

    // latency reduction owned by the vendor path: Reflex (via Streamline, DLSS path) and XeLL (XeSS path).
    // AMD Anti-Lag 2 is path-independent and lives in latency/antilag2.h instead.
    virtual const char* latencyName() const { return ""; }
    virtual bool latencySupported() const { return false; }
    virtual bool latencyEnabled() const { return false; }
    virtual void setLatency(bool on) { (void)on; }
    // "live" | "off" | "hardware-gated (why)" | "n/a (why)" — what the driver/SDK actually reports
    virtual std::string latencyStatus() const { return "n/a (no latency SDK on this path)"; }

    // per frame, in this order
    virtual void beginFrame(uint32_t frameIndex) { (void)frameIndex; }   // latency sleep + sim markers
    virtual void renderSubmitStart() {}
    virtual void evaluate(const FrameIn& f) = 0;             // records the upscale into f.cmd
    virtual void beforePresent(const FrameIn& f) { (void)f; } // FG tagging; f.cmd still open
    virtual void present(IDXGISwapChain4* swap, UINT sync, UINT flags) { swap->Present(sync, flags); }
    virtual void afterPresent() {}

    virtual std::string versionString() const = 0;          // exact SDK/feature version from the API
    virtual std::string extraStatus() const { return ""; }  // kernel path etc. for the title bar
    // frames the vendor FG reported presenting since the last call (0 if the backend has no counter)
    virtual uint32_t takePresentedCount() { return 0; }
    virtual bool hasPresentedCounter() const { return false; }
};

// Standard scale ratios shared by every backend's mode table (output / render, per axis).
// Native 1.0 | Quality 1.5 | Balanced 1.7 | Performance 2.0 | Ultra Performance 3.0.
struct ModeDef { const char* name; float ratio; };
static const ModeDef kStdModes[] = {
    { "native", 1.0f }, { "quality", 1.5f }, { "balanced", 1.7f }, { "performance", 2.0f }, { "ultraperf", 3.0f },
};
static const int kStdModeCount = (int)(sizeof(kStdModes) / sizeof(kStdModes[0]));
inline int modeIndexByName(const char* n) {
    for (int i = 0; i < kStdModeCount; ++i) if (std::string(kStdModes[i].name) == n) return i;
    return -1;
}

// Off = no vendor SDK: a plain bilinear compute upscale that runs on ANY D3D12 adapter, so every
// quality mode is still selectable (and measurable) on a machine with none of the three vendors.
Upscaler* createOff();
Upscaler* createDLSS();   // nullptr when not compiled in / SDK absent
Upscaler* createFSR();
Upscaler* createXeSS();

} // namespace up
