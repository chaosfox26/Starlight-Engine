// Starlight DX12 client — milestone 5 XeSS backend (Intel XeSS SDK: XeSS-SR super resolution, XeSS-FG
// frame generation through its proxy swapchain, XeLL latency reduction which XeSS-FG requires).
// Compiled only when build/flags.json enables backends.xess (buildctl adds UP_HAVE_XESS + the SDK
// include dir and copies libxess.dll / libxess_fg.dll / libxell.dll next to the exe). Nothing is
// linked statically: each DLL is LoadLibrary'd and every entry point fetched by name, so a machine
// without them reports "unavailable" and the renderer falls through to Off. The versions surfaced in
// versionString() are read back from the libraries (and the XeFX kernel actually activated), never
// assumed. Conventions (all three SDKs agree, see upscale.h): jitter shear proj[2][0] += 2jx/W,
// proj[2][1] -= 2jy/H; motion vectors in render-res pixels, current -> previous, y-down; depth not
// inverted; colour is 8-bit display-referred (LDR flag, exposure 1).
#ifdef UP_HAVE_XESS
#include "upscale.h"
#include <atomic>
#include <cstring>
#include <xess/xess_d3d12.h>
#include <xess_fg/xefg_swapchain_d3d12.h>
#include <xell/xell_d3d12.h>

using Microsoft::WRL::ComPtr;

namespace up {
namespace {

struct XessUpscaler;
static XessUpscaler* g_xessForLog = nullptr;   // the SR / XeLL log callbacks carry no user pointer

#define XS_FUN(name) decltype(&::name) name = nullptr

struct XessUpscaler : Upscaler {
    HMODULE modSR = nullptr, modFG = nullptr, modLL = nullptr;
    // XeSS-SR
    XS_FUN(xessGetVersion); XS_FUN(xessGetIntelXeFXVersion); XS_FUN(xessD3D12CreateContext);
    XS_FUN(xessD3D12BuildPipelines); XS_FUN(xessD3D12Init); XS_FUN(xessD3D12Execute);
    XS_FUN(xessGetOptimalInputResolution); XS_FUN(xessDestroyContext); XS_FUN(xessSetLoggingCallback);
    XS_FUN(xessIsOptimalDriver); XS_FUN(xessSetVelocityScale);
    // XeSS-FG
    XS_FUN(xefgSwapChainGetVersion); XS_FUN(xefgSwapChainD3D12CreateContext); XS_FUN(xefgSwapChainD3D12InitFromSwapChainDesc);
    XS_FUN(xefgSwapChainD3D12GetSwapChainPtr); XS_FUN(xefgSwapChainD3D12TagFrameResource); XS_FUN(xefgSwapChainTagFrameConstants);
    XS_FUN(xefgSwapChainSetEnabled); XS_FUN(xefgSwapChainSetPresentId); XS_FUN(xefgSwapChainGetLastPresentStatus);
    XS_FUN(xefgSwapChainSetLoggingCallback); XS_FUN(xefgSwapChainDestroy); XS_FUN(xefgSwapChainSetLatencyReduction);
    XS_FUN(xefgSwapChainGetProperties); XS_FUN(xefgSwapChainD3D12GetInitializationParameters); XS_FUN(xefgSwapChainSetNumInterpolatedFrames);
    // XeLL
    XS_FUN(xellGetVersion); XS_FUN(xellD3D12CreateContext); XS_FUN(xellDestroyContext); XS_FUN(xellSetSleepMode);
    XS_FUN(xellSleep); XS_FUN(xellAddMarkerData); XS_FUN(xellSetLoggingCallback);

    xess_context_handle_t   sr = nullptr;
    xefg_swapchain_handle_t fg = nullptr;
    xell_context_handle_t   ll = nullptr;
    ComPtr<ID3D12Device> dev;
    ID3D12CommandQueue*  queue = nullptr;
    LogFn   log = nullptr;
    UINT    outW = 0, outH = 0;
    int     curMode = 3;
    bool    fgOn = false, fgAvail = false, llOn = false, llWant = true, isIntel = false;
    uint32_t fgMax = 1, fgFrames = 1;
    uint32_t frameId = 0;
    std::string srVer = "xess (version unknown)", fxVer, fgVer, llVer, fgWhy;
    std::atomic<uint32_t> presented{ 0 };

    // every preset the SR API enumerates, best -> fastest (index == CLI --quality name)
    struct XMode { const char* name; xess_quality_settings_t q; };
    static const XMode* modes() {
        static const XMode m[] = {
            { "native",           XESS_QUALITY_SETTING_AA },
            { "ultraqualityplus", XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS },
            { "ultraquality",     XESS_QUALITY_SETTING_ULTRA_QUALITY },
            { "quality",          XESS_QUALITY_SETTING_QUALITY },
            { "balanced",         XESS_QUALITY_SETTING_BALANCED },
            { "performance",      XESS_QUALITY_SETTING_PERFORMANCE },
            { "ultraperf",        XESS_QUALITY_SETTING_ULTRA_PERFORMANCE },
        };
        return m;
    }
    static const int kModeCount = 7;

    ~XessUpscaler() override { shutdown(); }
    Path path() const override { return Path::XeSS; }

    void say(const std::string& s) { if (log) log("xess: " + s); }
    static void srLog(const char* m, xess_logging_level_t lvl) {
        if (g_xessForLog && m && lvl >= XESS_LOGGING_LEVEL_WARNING) g_xessForLog->say(std::string(lvl == XESS_LOGGING_LEVEL_ERROR ? "SR error: " : "SR warning: ") + m);
    }
    static void fgLog(const char* m, xefg_swapchain_logging_level_t lvl, void* user) {
        if (user && m && lvl >= XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING) ((XessUpscaler*)user)->say(std::string(lvl == XEFG_SWAPCHAIN_LOGGING_LEVEL_ERROR ? "FG error: " : "FG warning: ") + m);
    }
    static void llLog(const char* m, xell_logging_level_t lvl) {
        if (g_xessForLog && m && lvl >= XELL_LOGGING_LEVEL_WARNING) g_xessForLog->say(std::string(lvl == XELL_LOGGING_LEVEL_ERROR ? "XeLL error: " : "XeLL warning: ") + m);
    }
    static std::string v3(uint16_t a, uint16_t b, uint16_t c) { return std::to_string(a) + "." + std::to_string(b) + "." + std::to_string(c); }
    static std::string rs(xess_result_t r) { return "rc=" + std::to_string((int)r); }
    static std::string rf(xefg_swapchain_result_t r) {
        switch (r) {
            case XEFG_SWAPCHAIN_RESULT_SUCCESS: return "ok";
            case XEFG_SWAPCHAIN_RESULT_WARNING_OLD_DRIVER: return "warning: old driver";
            case XEFG_SWAPCHAIN_RESULT_WARNING_TOO_FEW_FRAMES: return "warning: too few frames yet (history warming up)";
            case XEFG_SWAPCHAIN_RESULT_WARNING_FRAMES_ID_MISMATCH: return "warning: frame id mismatch";
            case XEFG_SWAPCHAIN_RESULT_WARNING_MISSING_PRESENT_STATUS: return "warning: missing present status";
            case XEFG_SWAPCHAIN_RESULT_WARNING_RESOURCE_SIZES_MISMATCH: return "warning: resource sizes mismatch";
            default: return "rc=" + std::to_string((int)r);
        }
    }

    template <class F> bool fetch(HMODULE m, F& fn, const char* name, const char* dll) {
        fn = reinterpret_cast<F>(GetProcAddress(m, name));
        if (!fn) say(std::string(dll) + " lacks " + name);
        return fn != nullptr;
    }
    static HMODULE loadNextToExe(const wchar_t* dll) {
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir(exe); size_t k = dir.find_last_of(L"\\/"); dir = (k == std::wstring::npos) ? L"." : dir.substr(0, k);
        HMODULE m = LoadLibraryW((dir + L"\\" + dll).c_str());
        return m ? m : LoadLibraryW(dll);
    }

    bool preDevice(LogFn l) override {
        log = l;
        modSR = loadNextToExe(L"libxess.dll");
        if (!modSR) { say("libxess.dll not found next to the exe"); return false; }
#define SR(n) fetch(modSR, n, #n, "libxess.dll")
        bool ok = SR(xessGetVersion) && SR(xessGetIntelXeFXVersion) && SR(xessD3D12CreateContext) && SR(xessD3D12BuildPipelines)
               && SR(xessD3D12Init) && SR(xessD3D12Execute) && SR(xessGetOptimalInputResolution) && SR(xessDestroyContext)
               && SR(xessSetLoggingCallback) && SR(xessIsOptimalDriver) && SR(xessSetVelocityScale);
#undef SR
        if (!ok) return false;
        xess_version_t v{}; if (xessGetVersion(&v) == XESS_RESULT_SUCCESS) srVer = "XeSS-SR " + v3(v.major, v.minor, v.patch);
        // frame generation + latency are optional: missing DLL = FG n/a, SR still runs
        modFG = loadNextToExe(L"libxess_fg.dll"); modLL = loadNextToExe(L"libxell.dll");
        if (modFG && modLL) {
#define FG(n) fetch(modFG, n, #n, "libxess_fg.dll")
#define LL(n) fetch(modLL, n, #n, "libxell.dll")
            ok = FG(xefgSwapChainGetVersion) && FG(xefgSwapChainD3D12CreateContext) && FG(xefgSwapChainD3D12InitFromSwapChainDesc)
              && FG(xefgSwapChainD3D12GetSwapChainPtr) && FG(xefgSwapChainD3D12TagFrameResource) && FG(xefgSwapChainTagFrameConstants)
              && FG(xefgSwapChainSetEnabled) && FG(xefgSwapChainSetPresentId) && FG(xefgSwapChainGetLastPresentStatus)
              && FG(xefgSwapChainSetLoggingCallback) && FG(xefgSwapChainDestroy) && FG(xefgSwapChainSetLatencyReduction)
              && FG(xefgSwapChainGetProperties) && FG(xefgSwapChainD3D12GetInitializationParameters) && FG(xefgSwapChainSetNumInterpolatedFrames)
              && LL(xellGetVersion) && LL(xellD3D12CreateContext) && LL(xellDestroyContext) && LL(xellSetSleepMode)
              && LL(xellSleep) && LL(xellAddMarkerData) && LL(xellSetLoggingCallback);
#undef FG
#undef LL
            if (!ok) { fgWhy = "entry points missing"; modFG = modLL = nullptr; }
            else {
                xefg_swapchain_version_t gv{}; if (xefgSwapChainGetVersion(&gv) == XEFG_SWAPCHAIN_RESULT_SUCCESS) fgVer = "XeSS-FG " + v3(gv.major, gv.minor, gv.patch);
                xell_version_t lv{}; if (xellGetVersion(&lv) == XELL_RESULT_SUCCESS) llVer = "XeLL " + v3(lv.major, lv.minor, lv.patch);
            }
        } else { fgWhy = !modFG ? "libxess_fg.dll missing" : "libxell.dll missing"; }
        return true;
    }

    bool init(const DeviceCtx& ctx, ComPtr<IDXGISwapChain4>& swap) override {
        dev = ctx.device; queue = ctx.queue; outW = ctx.outW; outH = ctx.outH; log = ctx.log;
        g_xessForLog = this;
        { DXGI_ADAPTER_DESC1 ad{}; if (ctx.adapter && SUCCEEDED(ctx.adapter->GetDesc1(&ad))) isIntel = (ad.VendorId == 0x8086); }

        // ---- super resolution context (pipelines built now, blocking, so first frame is not a stall) ----
        xess_result_t r = xessD3D12CreateContext(dev.Get(), &sr);
        if (r != XESS_RESULT_SUCCESS) { say("SR context failed " + rs(r)); sr = nullptr; return false; }
        xessSetLoggingCallback(sr, XESS_LOGGING_LEVEL_WARNING, &srLog);
        r = xessIsOptimalDriver(sr);
        if (r == XESS_RESULT_WARNING_OLD_DRIVER) say("driver older than the SDK recommends (still running)");
        const uint32_t srFlags = XESS_INIT_FLAG_LDR_INPUT_COLOR;
        r = xessD3D12BuildPipelines(sr, nullptr, true, srFlags);
        if (r != XESS_RESULT_SUCCESS && r != XESS_RESULT_WARNING_OLD_DRIVER) { say("SR pipeline build failed " + rs(r)); return false; }
        if (!initSR(curMode)) return false;
        xess_version_t fx{};
        if (xessGetIntelXeFXVersion(sr, &fx) == XESS_RESULT_SUCCESS) fxVer = (fx.major | fx.minor | fx.patch) ? "XeFX kernel " + v3(fx.major, fx.minor, fx.patch) : "generic DP4a kernel (no XeFX on this GPU)";
        say("SR ACTIVE: " + srVer + (fxVer.empty() ? "" : " (" + fxVer + ")"));

        // ---- latency + frame generation: XeLL context first (FG needs it), then the FG proxy swapchain
        // created from the renderer's swapchain DESC (its own swapchain is released first; DXGI allows one
        // per hwnd). Any failure = FG n/a and the plain swapchain is recreated. ----
        if (!modFG || !modLL) { say("frame-gen n/a: " + fgWhy); return true; }
        xell_result_t lr = xellD3D12CreateContext(dev.Get(), &ll);
        if (lr != XELL_RESULT_SUCCESS) { fgWhy = "XeLL context rc=" + std::to_string((int)lr); ll = nullptr; say("frame-gen n/a: " + fgWhy); return true; }
        xellSetLoggingCallback(ll, XELL_LOGGING_LEVEL_WARNING, &llLog);
        xefg_swapchain_result_t gr = xefgSwapChainD3D12CreateContext(dev.Get(), &fg);
        if (gr != XEFG_SWAPCHAIN_RESULT_SUCCESS) { fgWhy = "FG context " + rf(gr); fg = nullptr; say("frame-gen n/a: " + fgWhy); return true; }
        xefgSwapChainSetLoggingCallback(fg, XEFG_SWAPCHAIN_LOGGING_LEVEL_WARNING, &fgLog, this);
        gr = xefgSwapChainSetLatencyReduction(fg, ll);
        if (gr != XEFG_SWAPCHAIN_RESULT_SUCCESS) say("FG latency link " + rf(gr));
        {
            DXGI_SWAP_CHAIN_DESC1 sd{}; swap->GetDesc1(&sd);
            ComPtr<IDXGIFactory2> factory; swap->GetParent(IID_PPV_ARGS(&factory));
            swap.Reset();
            DXGI_SWAP_CHAIN_FULLSCREEN_DESC fs{}; fs.Windowed = TRUE;
            xefg_swapchain_d3d12_init_params_t ip{};
            ip.pApplicationSwapChain = nullptr; ip.initFlags = XEFG_SWAPCHAIN_INIT_FLAG_NONE;
            ip.maxInterpolatedFrames = XEFG_SWAPCHAIN_USE_MAX_SUPPORTED_INTERPOLATED_FRAMES;
            ip.uiMode = XEFG_SWAPCHAIN_UI_MODE_NONE;   // no UI layer yet: HUD-less == backbuffer
            gr = xefgSwapChainD3D12InitFromSwapChainDesc(fg, ctx.hwnd, &sd, &fs, queue, factory.Get(), &ip);
            ComPtr<IDXGISwapChain4> proxy;
            if (gr == XEFG_SWAPCHAIN_RESULT_SUCCESS || gr == XEFG_SWAPCHAIN_RESULT_WARNING_OLD_DRIVER)
                gr = xefgSwapChainD3D12GetSwapChainPtr(fg, IID_PPV_ARGS(&proxy));
            if ((gr != XEFG_SWAPCHAIN_RESULT_SUCCESS && gr != XEFG_SWAPCHAIN_RESULT_WARNING_OLD_DRIVER) || !proxy) {
                fgWhy = "FG swapchain " + rf(gr); say("frame-gen n/a: " + fgWhy + " - recreating plain swapchain");
                xefgSwapChainDestroy(fg); fg = nullptr; xellDestroyContext(ll); ll = nullptr;
                ComPtr<IDXGISwapChain1> s1;
                if (FAILED(factory->CreateSwapChainForHwnd(queue, ctx.hwnd, &sd, nullptr, nullptr, &s1)) || FAILED(s1.As(&swap))) { say("plain swapchain recreate FAILED"); return false; }
                return true;
            }
            swap = proxy;
            xefg_swapchain_d3d12_init_params_t got{};
            if (xefgSwapChainD3D12GetInitializationParameters(fg, &got) == XEFG_SWAPCHAIN_RESULT_SUCCESS && got.maxInterpolatedFrames) fgMax = got.maxInterpolatedFrames;
            fgFrames = 1;
            fgAvail = true;
            applyLL();
            say("frame-gen ACTIVE: " + fgVer + " + " + llVer + " (up to " + std::to_string(fgMax + 1) + "x, proxy swapchain)");
        }
        return true;
    }

    bool initSR(int m) {
        xess_d3d12_init_params_t ip{};
        ip.outputResolution = { outW, outH };
        ip.qualitySetting = modes()[m].q;
        ip.initFlags = XESS_INIT_FLAG_LDR_INPUT_COLOR;
        xess_result_t r = xessD3D12Init(sr, &ip);
        if (r != XESS_RESULT_SUCCESS && r != XESS_RESULT_WARNING_OLD_DRIVER) { say(std::string("SR init failed for ") + modes()[m].name + " " + rs(r)); return false; }
        xessSetVelocityScale(sr, 1.0f, 1.0f);   // mvec already in render-res pixels, current -> previous
        return true;
    }

    void shutdown() override {
        if (fg) { xefgSwapChainSetEnabled(fg, 0); }
        if (ll && llOn) { xell_sleep_params_t sp{}; xellSetSleepMode(ll, &sp); llOn = false; }
        if (fg) { xefgSwapChainDestroy(fg); fg = nullptr; }   // the renderer released the proxy swapchain before teardown
        if (ll) { xellDestroyContext(ll); ll = nullptr; }
        if (sr) { xessDestroyContext(sr); sr = nullptr; }
        dev.Reset(); if (g_xessForLog == this) g_xessForLog = nullptr;
        modSR = modFG = modLL = nullptr;   // kept resident (see FSR note): the OS unloads them at exit
    }

    int modeCount() const override { return kModeCount; }
    const char* modeName(int i) const override { return (i >= 0 && i < kModeCount) ? modes()[i].name : "?"; }
    int mode() const override { return curMode; }
    bool setMode(int i, UINT& rw, UINT& rh) override {
        if (i < 0 || i >= kModeCount || !sr) return false;
        xess_2d_t out{ outW, outH }, opt{}, mn{}, mx{};
        xess_result_t r = xessGetOptimalInputResolution(sr, &out, modes()[i].q, &opt, &mn, &mx);
        if (r != XESS_RESULT_SUCCESS || !opt.x || !opt.y) { say(std::string("render-size query failed for ") + modes()[i].name + " " + rs(r)); return false; }
        if (i != curMode) { curMode = i; if (!initSR(i)) return false; }   // preset change = cheap re-init (caller flushed the GPU)
        rw = opt.x; rh = opt.y; return true;
    }

    bool fgSupported() const override { return fgAvail; }
    bool fgEnabled() const override { return fgOn; }
    void setFG(bool on) override {
        if (!fgAvail) { fgOn = false; return; }
        if (on) { fgOn = true; applyLL(); }   // XeLL first: XeSS-FG refuses to enable without latency reduction (caller flushed the GPU)
        xefg_swapchain_result_t r = xefgSwapChainSetEnabled(fg, on ? 1u : 0u);
        if (r != XEFG_SWAPCHAIN_RESULT_SUCCESS) {
            say(std::string("FG ") + (on ? "enable " : "disable ") + rf(r) + (on && !llOn ? " (XeLL is off: cross-vendor XeSS-FG needs --latency xell/auto)" : ""));
            fgOn = false; fgWhy = on && !llOn ? "needs XeLL on" : rf(r); applyLL(); return;
        }
        fgOn = on; applyLL();   // cross-vendor XeLL is only allowed while XeSS-FG is on
    }
    // XeLL low-latency mode: Intel GPUs always; any other vendor only in "cross-vendor mode" = XeSS-FG enabled.
    bool llAllowed() const { return ll && llWant && (isIntel || fgOn); }
    void applyLL() {
        if (!ll) return;
        bool want = llAllowed();
        if (want == llOn) return;
        xell_sleep_params_t sp{}; sp.bLowLatencyMode = want ? 1u : 0u; sp.minimumIntervalUs = 0;
        xell_result_t lr = xellSetSleepMode(ll, &sp);
        if (lr != XELL_RESULT_SUCCESS) { say("XeLL sleep mode rc=" + std::to_string((int)lr)); return; }
        llOn = want;
    }
    const char* latencyName() const override { return "XeLL"; }
    bool latencySupported() const override { return ll != nullptr; }
    bool latencyEnabled() const override { return llOn; }
    void setLatency(bool on) override {
        llWant = on;
        if (!on && fgOn && !isIntel) { setFG(false); fgWhy = "needs XeLL on"; return; }   // cross-vendor FG may not run without latency reduction
        applyLL();
    }
    std::string latencyStatus() const override {
        if (!ll) return "n/a (libxell.dll not loaded)";
        if (!llWant) return "off";
        if (llOn) return isIntel ? "live" : "live (cross-vendor mode: paced through XeSS-FG)";
        return isIntel ? "off (sleep mode rejected)" : "hardware-gated (non-Intel GPU: XeLL needs XeSS-FG on)";
    }
    void cycleMultiplier() override {
        if (!fgAvail || fgMax < 2) return;
        fgFrames = fgFrames % fgMax + 1;
        xefg_swapchain_result_t r = xefgSwapChainSetNumInterpolatedFrames(fg, fgFrames);
        if (r != XEFG_SWAPCHAIN_RESULT_SUCCESS) say("FG multiplier " + rf(r));
    }
    std::string fgStatus() const override {
        if (!fgAvail) return "FG n/a" + (fgWhy.empty() ? "" : " (" + fgWhy + ")");
        return fgOn ? "FG " + std::to_string(fgFrames + 1) + "x" : "FG off" + (fgWhy.empty() ? "" : " (" + fgWhy + ")");
    }

    void beginFrame(uint32_t f) override {
        frameId = f;
        if (!ll) return;
        xellSleep(ll, frameId);
        xellAddMarkerData(ll, frameId, XELL_SIMULATION_START);
    }
    void renderSubmitStart() override {
        if (!ll) return;
        xellAddMarkerData(ll, frameId, XELL_SIMULATION_END);
        xellAddMarkerData(ll, frameId, XELL_RENDERSUBMIT_START);
    }

    void evaluate(const FrameIn& f) override {
        xess_d3d12_execute_params_t e{};
        e.pColorTexture = f.color; e.pVelocityTexture = f.mvec; e.pDepthTexture = f.depth;
        e.pExposureScaleTexture = nullptr; e.pResponsivePixelMaskTexture = nullptr; e.pOutputTexture = f.output;
        e.jitterOffsetX = f.jitterX; e.jitterOffsetY = f.jitterY;
        e.exposureScale = 1.0f; e.resetHistory = f.reset ? 1u : 0u;
        e.inputWidth = f.renderW; e.inputHeight = f.renderH;
        xess_result_t r = xessD3D12Execute(sr, f.cmd, &e);
        if (r != XESS_RESULT_SUCCESS && (f.frameIndex % 300) == 1) say("SR execute " + rs(r));
    }

    // XeSS-FG interpolates at Present on its own proxy; depth + motion are re-used by the renderer
    // next frame so they are tagged ONLY_NOW (the library records copies into f.cmd, still open).
    void beforePresent(const FrameIn& f) override {
        if (!fgAvail || !fgOn) return;
        xefg_swapchain_d3d12_resource_data_t rd{};
        rd.validity = XEFG_SWAPCHAIN_RV_ONLY_NOW;
        rd.resourceBase = { 0, 0 }; rd.resourceSize = { f.renderW, f.renderH };
        rd.type = XEFG_SWAPCHAIN_RES_DEPTH; rd.pResource = f.depth; rd.incomingState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        xefg_swapchain_result_t r1 = xefgSwapChainD3D12TagFrameResource(fg, f.cmd, frameId, &rd);
        rd.type = XEFG_SWAPCHAIN_RES_MOTION_VECTOR; rd.pResource = f.mvec;
        xefg_swapchain_result_t r2 = xefgSwapChainD3D12TagFrameResource(fg, f.cmd, frameId, &rd);
        rd.type = XEFG_SWAPCHAIN_RES_HUDLESS_COLOR; rd.pResource = f.output; rd.incomingState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        rd.resourceSize = { f.outW, f.outH };
        xefg_swapchain_result_t r3 = xefgSwapChainD3D12TagFrameResource(fg, f.cmd, frameId, &rd);
        xefg_swapchain_frame_constant_data_t c{};
        memcpy(c.viewMatrix, f.view.m, sizeof(float) * 16);
        memcpy(c.projectionMatrix, f.proj.m, sizeof(float) * 16);
        c.jitterOffsetX = f.jitterX; c.jitterOffsetY = f.jitterY;
        c.motionVectorScaleX = 1.0f; c.motionVectorScaleY = 1.0f;
        c.resetHistory = f.reset ? 1u : 0u; c.frameRenderTime = f.dtMs;
        xefg_swapchain_result_t r4 = xefgSwapChainTagFrameConstants(fg, frameId, &c);
        if ((r1 | r2 | r3 | r4) != 0 && (f.frameIndex % 300) == 1)
            say("FG tags " + rf(r1) + " " + rf(r2) + " " + rf(r3) + " const " + rf(r4));
    }

    void present(IDXGISwapChain4* swap, UINT sync, UINT flags) override {
        if (ll) xellAddMarkerData(ll, frameId, XELL_RENDERSUBMIT_END);
        if (fg) xefgSwapChainSetPresentId(fg, frameId);
        if (ll) xellAddMarkerData(ll, frameId, XELL_PRESENT_START);
        swap->Present(sync, flags);
        if (ll) xellAddMarkerData(ll, frameId, XELL_PRESENT_END);
    }
    void afterPresent() override {
        if (!fgAvail) return;
        xefg_swapchain_present_status_t st{};
        if (xefgSwapChainGetLastPresentStatus(fg, &st) == XEFG_SWAPCHAIN_RESULT_SUCCESS) {
            presented.fetch_add(st.framesPresented ? st.framesPresented : 1);
            if (st.frameGenResult != lastFgResult) { lastFgResult = st.frameGenResult; if (st.frameGenResult != XEFG_SWAPCHAIN_RESULT_SUCCESS) say("FG present status " + rf(st.frameGenResult)); }
        }
    }
    xefg_swapchain_result_t lastFgResult = XEFG_SWAPCHAIN_RESULT_SUCCESS;

    std::string versionString() const override { return srVer + (fgAvail ? " | " + fgVer + " | " + llVer : ""); }
    std::string extraStatus() const override { return fxVer; }
    uint32_t takePresentedCount() override { return presented.exchange(0); }
    bool hasPresentedCounter() const override { return fgAvail; }
};

} // namespace

Upscaler* createXeSS() { return new XessUpscaler(); }

} // namespace up
#endif // UP_HAVE_XESS
