// Starlight DX12 client — DLSS backend (NVIDIA Streamline 2.12: DLSS super resolution +
// DLSS frame generation + Reflex/PCL markers, all through sl.interposer.dll). Compiled only when
// build/flags.json enables backends.dlss (buildctl adds UP_HAVE_DLSS + the SDK include dir and copies
// the Streamline DLLs next to the exe). Nothing is linked statically: sl.interposer.dll is
// LoadLibrary'd and every entry point is fetched by name, so a machine without the DLLs (or without
// an RTX card) just reports "unavailable" and the renderer falls through to Off.
// Integration style = Streamline "manual hooking": the factory and device the renderer creates are
// upgraded to SL proxies (slUpgradeInterface), so the swapchain + command queue the renderer makes
// are the ones DLSS-G interposes. The DLSS / DLSS-G versions that ACTUALLY loaded are read back
// from slGetFeatureVersion and surfaced in versionString() — never assumed.
#ifdef UP_HAVE_DLSS
#include "upscale.h"
#include "upscale_paths.h"
#include <atomic>
#include <vector>
#include <cmath>
#include <sl.h>
#include <sl_consts.h>
#include <sl_helpers.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_reflex.h>
#include <sl_pcl.h>

using Microsoft::WRL::ComPtr;

namespace up {
namespace {

struct DlssUpscaler;
static DlssUpscaler* g_dlssForLog = nullptr;   // the SL log callback has no user pointer

struct DlssUpscaler : Upscaler {
    HMODULE mod = nullptr;
    SL_FUN_DECL(slInit); SL_FUN_DECL(slShutdown); SL_FUN_DECL(slIsFeatureSupported); SL_FUN_DECL(slIsFeatureLoaded);
    SL_FUN_DECL(slSetTagForFrame); SL_FUN_DECL(slSetConstants); SL_FUN_DECL(slGetFeatureVersion);
    SL_FUN_DECL(slGetFeatureRequirements); SL_FUN_DECL(slFreeResources); SL_FUN_DECL(slEvaluateFeature);
    SL_FUN_DECL(slUpgradeInterface); SL_FUN_DECL(slGetNativeInterface); SL_FUN_DECL(slGetFeatureFunction);
    SL_FUN_DECL(slGetNewFrameToken); SL_FUN_DECL(slSetD3DDevice);
    SL_FUN_DECL(slDLSSGetOptimalSettings); SL_FUN_DECL(slDLSSSetOptions);
    SL_FUN_DECL(slDLSSGGetState); SL_FUN_DECL(slDLSSGSetOptions);
    SL_FUN_DECL(slReflexSetOptions); SL_FUN_DECL(slReflexSleep); SL_FUN_DECL(slReflexGetState); SL_FUN_DECL(slPCLSetMarker);

    ComPtr<ID3D12Device> nativeDev;
    LogFn   log = nullptr;
    UINT    outW = 0, outH = 0;
    int     curMode = 1;
    bool    slUp = false, dlssOk = false, fgAvail = false, reflexOk = false, pclOk = false;
    bool    fgOn = false, reflexOn = false, reflexLowLatencyAvail = false;
    uint32_t fgFrames = 1, fgFramesMax = 1;
    std::string fgWhy;
    std::string dlssVer = "dlss (version unknown)", fgVer;
    sl::FrameToken* tok = nullptr;
    sl::ViewportHandle vp{ 0u };
    uint32_t frameCounter = 0;
    std::atomic<uint32_t> presented{ 0 };
    uint32_t lastGState = 0;

    ~DlssUpscaler() override { shutdown(); }
    Path path() const override { return Path::DLSS; }

    void say(const std::string& s) { if (log) log("dlss: " + s); }
    static void logCb(sl::LogType t, const char* m) {
        if (!g_dlssForLog || !m) return;
        if (t == sl::LogType::eInfo) return;     // SL info is chatty; errors/warnings go to the client log
        std::string s(m); while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        g_dlssForLog->say(std::string(t == sl::LogType::eError ? "SDK error: " : "SDK warning: ") + s);
    }
    static const char* rs(sl::Result r) { return sl::getResultAsStr(r); }

    template <class F> bool fetch(F*& fn, const char* name) {
        fn = reinterpret_cast<F*>(GetProcAddress(mod, name));
        if (!fn) say(std::string("sl.interposer.dll lacks ") + name);
        return fn != nullptr;
    }
    template <class F> bool feature(sl::Feature f, F*& fn, const char* name) {
        void* p = nullptr;
        sl::Result r = slGetFeatureFunction(f, name, p);
        if (r != sl::Result::eOk || !p) { say(std::string(name) + " unavailable: " + rs(r)); fn = nullptr; return false; }
        fn = reinterpret_cast<F*>(p); return true;
    }

    bool preDevice(LogFn l) override {
        log = l; g_dlssForLog = this;
        std::wstring dir = vendorDir(L"sl.interposer.dll");
        mod = loadVendorModule(L"sl.interposer.dll");
        if (!mod) { say("sl.interposer.dll not found next to the host module or the exe"); return false; }
        bool ok = fetch(slInit, "slInit") && fetch(slShutdown, "slShutdown") && fetch(slIsFeatureSupported, "slIsFeatureSupported")
            && fetch(slIsFeatureLoaded, "slIsFeatureLoaded") && fetch(slSetTagForFrame, "slSetTagForFrame") && fetch(slSetConstants, "slSetConstants")
            && fetch(slGetFeatureVersion, "slGetFeatureVersion") && fetch(slGetFeatureRequirements, "slGetFeatureRequirements")
            && fetch(slFreeResources, "slFreeResources") && fetch(slEvaluateFeature, "slEvaluateFeature") && fetch(slUpgradeInterface, "slUpgradeInterface")
            && fetch(slGetNativeInterface, "slGetNativeInterface") && fetch(slGetFeatureFunction, "slGetFeatureFunction")
            && fetch(slGetNewFrameToken, "slGetNewFrameToken") && fetch(slSetD3DDevice, "slSetD3DDevice");
        if (!ok) return false;

        static const sl::Feature feats[] = { sl::kFeatureDLSS, sl::kFeatureDLSS_G, sl::kFeatureReflex, sl::kFeaturePCL };
        static std::wstring pluginDir; pluginDir = dir;
        const wchar_t* paths[] = { pluginDir.c_str() };
        sl::Preferences pref{};
        pref.showConsole = false; pref.logLevel = GetEnvironmentVariableW(L"STARLIGHT_SL_VERBOSE", nullptr, 0) ? sl::LogLevel::eVerbose : sl::LogLevel::eDefault;
        pref.pathsToPlugins = paths; pref.numPathsToPlugins = 1; pref.pathToLogsAndData = pluginDir.c_str();
        pref.logMessageCallback = &logCb;
        pref.flags = sl::PreferenceFlags::eDisableCLStateTracking | sl::PreferenceFlags::eUseManualHooking | sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        pref.featuresToLoad = feats; pref.numFeaturesToLoad = 4;
        pref.applicationId = 231313132;  // Streamline sample-app id (no NVIDIA-issued id for this client yet)
        pref.engine = sl::EngineType::eCustom; pref.engineVersion = "0.5.0";
        pref.projectId = "a0e2f6c1-5d3b-4b0e-9c7a-1f2e3d4c5b6a";   // NGX validates this as a GUID (hex + dashes only)
        pref.renderAPI = sl::RenderAPI::eD3D12;
        sl::Result r = slInit(pref, sl::kSDKVersion);
        if (r != sl::Result::eOk) { say(std::string("slInit failed: ") + rs(r)); return false; }
        slUp = true;
        say("Streamline " + std::to_string(SL_VERSION_MAJOR) + "." + std::to_string(SL_VERSION_MINOR) + "." + std::to_string(SL_VERSION_PATCH) + " initialised (manual hooking)");
        return true;
    }

    HRESULT createFactory(UINT flags, IDXGIFactory6** out) override {
        HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(out));
        if (FAILED(hr)) return hr;
        sl::Result r = slUpgradeInterface((void**)out);
        if (r != sl::Result::eOk) say(std::string("factory proxy failed: ") + rs(r) + " (DLSS-G will be n/a)");
        return hr;
    }
    HRESULT createDevice(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL fl, ID3D12Device** out) override {
        HRESULT hr = D3D12CreateDevice(adapter, fl, IID_PPV_ARGS(out));
        if (FAILED(hr)) return hr;
        nativeDev = *out;
        sl::Result r = slSetD3DDevice(*out);
        if (r != sl::Result::eOk) say(std::string("slSetD3DDevice failed: ") + rs(r));
        r = slUpgradeInterface((void**)out);
        if (r != sl::Result::eOk) say(std::string("device proxy failed: ") + rs(r) + " (DLSS-G will be n/a)");
        return hr;
    }

    static std::string ver(const sl::Version& v) { return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.build); }

    bool init(const DeviceCtx& ctx, ComPtr<IDXGISwapChain4>& swap) override {
        (void)swap;   // the renderer's swapchain came from the SL proxy factory, so it is already the DLSS-G one
        outW = ctx.outW; outH = ctx.outH; log = ctx.log;
        if (!slUp) return false;
        DXGI_ADAPTER_DESC1 ad{}; ctx.adapter->GetDesc1(&ad);
        sl::AdapterInfo ai{}; ai.deviceLUID = (uint8_t*)&ad.AdapterLuid; ai.deviceLUIDSizeInBytes = sizeof(LUID);

        sl::Result r = slIsFeatureSupported(sl::kFeatureDLSS, ai);
        if (r != sl::Result::eOk) { say(std::string("DLSS not supported on this adapter: ") + rs(r)); return false; }
        sl::FeatureVersion fv{};
        if (slGetFeatureVersion(sl::kFeatureDLSS, fv) == sl::Result::eOk) dlssVer = "DLSS " + ver(fv.versionNGX) + " (sl " + ver(fv.versionSL) + ")";
        dlssOk = feature(sl::kFeatureDLSS, slDLSSGetOptimalSettings, "slDLSSGetOptimalSettings") && feature(sl::kFeatureDLSS, slDLSSSetOptions, "slDLSSSetOptions");
        if (!dlssOk) return false;
        say("upscaler ACTIVE: " + dlssVer);

        reflexOk = slIsFeatureSupported(sl::kFeatureReflex, ai) == sl::Result::eOk
            && feature(sl::kFeatureReflex, slReflexSetOptions, "slReflexSetOptions") && feature(sl::kFeatureReflex, slReflexSleep, "slReflexSleep");
        if (reflexOk) {
            feature(sl::kFeatureReflex, slReflexGetState, "slReflexGetState");
            sl::ReflexState st{};
            if (slReflexGetState && slReflexGetState(st) == sl::Result::eOk) reflexLowLatencyAvail = st.lowLatencyAvailable;
            if (!applyReflex(true)) reflexOk = false;
        }
        pclOk = slIsFeatureSupported(sl::kFeaturePCL, ai) == sl::Result::eOk && feature(sl::kFeaturePCL, slPCLSetMarker, "slPCLSetMarker");
        say(std::string("Reflex ") + (reflexOk ? "on" : "n/a") + ", PCL markers " + (pclOk ? "on" : "n/a"));

        r = slIsFeatureSupported(sl::kFeatureDLSS_G, ai);
        if (r != sl::Result::eOk) { fgWhy = rs(r); say("DLSS-G not supported: " + fgWhy); }
        else if (!reflexOk || !pclOk) { fgWhy = "needs Reflex+PCL"; say("DLSS-G n/a: " + fgWhy); }
        else if (!feature(sl::kFeatureDLSS_G, slDLSSGGetState, "slDLSSGGetState") || !feature(sl::kFeatureDLSS_G, slDLSSGSetOptions, "slDLSSGSetOptions")) fgWhy = "entry points missing";
        else {
            sl::FeatureVersion gv{};
            if (slGetFeatureVersion(sl::kFeatureDLSS_G, gv) == sl::Result::eOk) fgVer = ver(gv.versionNGX) + " (sl " + ver(gv.versionSL) + ")";
            sl::DLSSGState st{};
            sl::DLSSGOptions probe = fgOptions(false);
            r = slDLSSGGetState(vp, st, &probe);
            if (r != sl::Result::eOk) { fgWhy = rs(r); say("DLSS-G state query failed: " + fgWhy); }
            else if (st.status != sl::DLSSGStatus::eOk) { fgWhy = gStatus(st.status); say("DLSS-G reports " + fgWhy); }
            else {
                fgAvail = true; fgFramesMax = st.numFramesToGenerateMax ? st.numFramesToGenerateMax : 1;
                say("frame-gen ACTIVE: DLSS-G " + fgVer + " (up to " + std::to_string(fgFramesMax + 1) + "x, min side " + std::to_string(st.minWidthOrHeight) + ")");
            }
        }
        return true;
    }

    static std::string gStatus(sl::DLSSGStatus s) {
        if (s == sl::DLSSGStatus::eOk) return "ok";
        std::string o;
        auto has = [&](sl::DLSSGStatus b, const char* n) { if ((uint32_t)s & (uint32_t)b) { if (!o.empty()) o += "+"; o += n; } };
        has(sl::DLSSGStatus::eFailResolutionTooLow, "resolution too low");
        has(sl::DLSSGStatus::eFailReflexNotDetectedAtRuntime, "reflex not detected");
        has(sl::DLSSGStatus::eFailHDRFormatNotSupported, "format unsupported");
        has(sl::DLSSGStatus::eFailCommonConstantsInvalid, "constants invalid");
        has(sl::DLSSGStatus::eFailGetCurrentBackBufferIndexNotCalled, "backbuffer index not queried");
        return o.empty() ? "status 0x" + std::to_string((uint32_t)s) : o;
    }

    sl::DLSSGOptions fgOptions(bool on) const {
        sl::DLSSGOptions o{};
        o.mode = on ? sl::DLSSGMode::eOn : sl::DLSSGMode::eOff;
        o.numFramesToGenerate = fgFrames;
        o.flags = sl::DLSSGFlags::eRetainResourcesWhenOff;
        o.colorWidth = outW; o.colorHeight = outH; o.mvecDepthWidth = outW; o.mvecDepthHeight = outH;
        o.numBackBuffers = 2;
        o.colorBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; o.mvecBufferFormat = DXGI_FORMAT_R16G16_FLOAT; o.depthBufferFormat = DXGI_FORMAT_R32_TYPELESS;
        o.hudLessBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        o.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
        return o;
    }
    void applyFG() {
        if (!fgAvail) return;
        sl::DLSSGOptions o = fgOptions(fgOn);
        sl::Result r = slDLSSGSetOptions(vp, o);
        if (r != sl::Result::eOk) say(std::string("DLSS-G options failed: ") + rs(r));
    }

    void shutdown() override {
        if (slUp) {
            if (fgAvail) { fgOn = false; applyFG(); }
            if (dlssOk) slFreeResources(sl::kFeatureDLSS, vp);
            if (fgAvail) slFreeResources(sl::kFeatureDLSS_G, vp);
            slShutdown(); slUp = false;
        }
        nativeDev.Reset(); dlssOk = false; fgAvail = false;
        if (g_dlssForLog == this) g_dlssForLog = nullptr;
        mod = nullptr;   // sl.interposer.dll stays resident: it owns the proxy objects the renderer is still releasing
    }

    static sl::DLSSMode slMode(int i) {
        switch (i) { case 0: return sl::DLSSMode::eDLAA; case 1: return sl::DLSSMode::eMaxQuality; case 2: return sl::DLSSMode::eBalanced;
                     case 3: return sl::DLSSMode::eMaxPerformance; default: return sl::DLSSMode::eUltraPerformance; }
    }
    int modeCount() const override { return kStdModeCount; }
    const char* modeName(int i) const override { return (i >= 0 && i < kStdModeCount) ? kStdModes[i].name : "?"; }
    int mode() const override { return curMode; }
    bool setMode(int i, UINT& rw, UINT& rh) override {
        if (i < 0 || i >= kStdModeCount || !dlssOk) return false;
        sl::DLSSOptions o{}; o.mode = slMode(i); o.outputWidth = outW; o.outputHeight = outH;
        o.colorBuffersHDR = sl::Boolean::eFalse; o.preExposure = 1.0f; o.exposureScale = 1.0f; o.useAutoExposure = sl::Boolean::eFalse;
        sl::DLSSOptimalSettings s{};
        sl::Result r = slDLSSGetOptimalSettings(o, s);
        if (r != sl::Result::eOk || !s.optimalRenderWidth || !s.optimalRenderHeight) { say(std::string("optimal settings failed for mode ") + std::to_string(i) + ": " + rs(r)); return false; }
        r = slDLSSSetOptions(vp, o);
        if (r != sl::Result::eOk) { say(std::string("DLSS options failed: ") + rs(r)); return false; }
        curMode = i; rw = s.optimalRenderWidth; rh = s.optimalRenderHeight; return true;
    }

    bool fgSupported() const override { return fgAvail; }
    bool fgEnabled() const override { return fgOn; }
    void setFG(bool on) override { if (!fgAvail) { fgOn = false; return; } fgOn = on; applyFG(); }
    void cycleMultiplier() override {
        if (!fgAvail) return;
        fgFrames = (fgFrames % fgFramesMax) + 1;
        if (fgOn) applyFG();
    }
    std::string fgStatus() const override {
        if (!fgAvail) return "FG n/a" + (fgWhy.empty() ? std::string() : " (" + fgWhy + ")");
        return fgOn ? "FG " + std::to_string(fgFrames + 1) + "x" : "FG off";
    }

    void marker(sl::PCLMarker m) { if (pclOk && tok) slPCLSetMarker(m, *tok); }
    bool applyReflex(bool on) {
        sl::ReflexOptions ro{}; ro.mode = on ? sl::ReflexMode::eLowLatency : sl::ReflexMode::eOff; ro.frameLimitUs = 0; ro.useMarkersToOptimize = false;
        sl::Result r = slReflexSetOptions(ro);
        if (r != sl::Result::eOk) { say(std::string("Reflex options failed: ") + rs(r)); return false; }
        reflexOn = on; return true;
    }
    const char* latencyName() const override { return "Reflex"; }
    bool latencySupported() const override { return reflexOk; }
    bool latencyEnabled() const override { return reflexOk && reflexOn; }
    void setLatency(bool on) override { if (reflexOk) applyReflex(on); }
    std::string latencyStatus() const override {
        if (!reflexOk) return "n/a (Reflex feature not supported by this adapter/driver)";
        if (!reflexLowLatencyAvail) return "hardware-gated (driver reports low-latency mode unavailable)";
        return reflexOn ? "live" : "off";
    }

    // SL wants the full description alongside the native pointer (it does not GetDesc() on our behalf).
    static sl::Resource res(ID3D12Resource* r, D3D12_RESOURCE_STATES state) {
        sl::Resource o(sl::ResourceType::eTex2d, r, (uint32_t)state);
        D3D12_RESOURCE_DESC d = r->GetDesc();
        o.width = (uint32_t)d.Width; o.height = d.Height; o.nativeFormat = (uint32_t)d.Format;
        o.mipLevels = d.MipLevels; o.arrayLayers = d.DepthOrArraySize; o.flags = 0; o.usage = 0;
        return o;
    }

    void beginFrame(uint32_t frameIndex) override {
        frameCounter = frameIndex;
        sl::Result r = slGetNewFrameToken(tok, &frameCounter);
        if (r != sl::Result::eOk) { tok = nullptr; return; }
        if (reflexOk) slReflexSleep(*tok);   // no-op when mode is eOff; DLSS-G still needs the call sequence
        marker(sl::PCLMarker::eSimulationStart);
    }

    static sl::float4x4 m4(const DirectX::XMFLOAT4X4& s) {
        sl::float4x4 m;
        for (uint32_t r = 0; r < 4; ++r) m[r] = sl::float4(s.m[r][0], s.m[r][1], s.m[r][2], s.m[r][3]);
        return m;
    }
    static sl::float4x4 m4(DirectX::CXMMATRIX x) { DirectX::XMFLOAT4X4 f; DirectX::XMStoreFloat4x4(&f, x); return m4(f); }

    void setConstants(const FrameIn& f) {
        using namespace DirectX;
        XMMATRIX proj = XMLoadFloat4x4(&f.proj), vpNow = XMLoadFloat4x4(&f.viewProj), vpPrev = XMLoadFloat4x4(&f.prevViewProj);
        XMMATRIX clipToPrev = XMMatrixMultiply(XMMatrixInverse(nullptr, vpNow), vpPrev);   // row vectors: clip -> world -> prev clip
        sl::Constants c{};
        c.cameraViewToClip = m4(proj);
        c.clipToCameraView = m4(XMMatrixInverse(nullptr, proj));
        c.clipToPrevClip = m4(clipToPrev);
        c.prevClipToClip = m4(XMMatrixInverse(nullptr, clipToPrev));
        c.jitterOffset = { f.jitterX, f.jitterY };
        c.mvecScale = { 1.0f / (float)f.renderW, 1.0f / (float)f.renderH };   // renderer writes pixel units, SL wants [-1,1]
        c.cameraPinholeOffset = { 0.0f, 0.0f };
        c.cameraPos = { f.camPos.x, f.camPos.y, f.camPos.z };
        c.cameraUp = { f.camUp.x, f.camUp.y, f.camUp.z };
        c.cameraRight = { f.camRight.x, f.camRight.y, f.camRight.z };
        c.cameraFwd = { f.camFwd.x, f.camFwd.y, f.camFwd.z };
        c.cameraNear = f.nearZ; c.cameraFar = f.farZ; c.cameraFOV = f.fovY; c.cameraAspectRatio = f.aspect;
        c.motionVectorsInvalidValue = sl::INVALID_FLOAT;
        c.depthInverted = sl::Boolean::eFalse; c.cameraMotionIncluded = sl::Boolean::eTrue; c.motionVectors3D = sl::Boolean::eFalse;
        c.reset = f.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
        c.orthographicProjection = sl::Boolean::eFalse; c.motionVectorsDilated = sl::Boolean::eFalse; c.motionVectorsJittered = sl::Boolean::eFalse;
        sl::Result r = slSetConstants(c, *tok, vp);
        if (r != sl::Result::eOk && (frameCounter % 300) == 1) say(std::string("constants rejected: ") + rs(r));
    }

    void evaluate(const FrameIn& f) override {
        if (!tok) return;
        marker(sl::PCLMarker::eSimulationEnd);
        marker(sl::PCLMarker::eRenderSubmitStart);
        setConstants(f);
        sl::Extent rExt{ 0, 0, f.renderW, f.renderH }, oExt{ 0, 0, f.outW, f.outH }, one{ 0, 0, 1, 1 };
        sl::Resource color = res(f.color, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        sl::Resource depth = res(f.depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        sl::Resource mvec = res(f.mvec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        sl::Resource expo = res(f.exposure, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        sl::Resource outp = res(f.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        sl::ResourceTag tags[] = {
            sl::ResourceTag(&color, sl::kBufferTypeScalingInputColor,  sl::ResourceLifecycle::eOnlyValidNow, &rExt),
            sl::ResourceTag(&outp,  sl::kBufferTypeScalingOutputColor, sl::ResourceLifecycle::eOnlyValidNow, &oExt),
            sl::ResourceTag(&depth, sl::kBufferTypeDepth,              sl::ResourceLifecycle::eOnlyValidNow, &rExt),
            sl::ResourceTag(&mvec,  sl::kBufferTypeMotionVectors,      sl::ResourceLifecycle::eOnlyValidNow, &rExt),
            sl::ResourceTag(&expo,  sl::kBufferTypeExposure,           sl::ResourceLifecycle::eOnlyValidNow, &one),
        };
        sl::Result r = slSetTagForFrame(*tok, vp, tags, (uint32_t)(sizeof(tags) / sizeof(tags[0])), f.cmd);
        if (r != sl::Result::eOk && (frameCounter % 300) == 1) say(std::string("tags rejected: ") + rs(r));
        const sl::BaseStructure* in[] = { &vp };
        r = slEvaluateFeature(sl::kFeatureDLSS, *tok, in, 1, f.cmd);
        if (r != sl::Result::eOk && (frameCounter % 300) == 1) say(std::string("DLSS evaluate failed: ") + rs(r));
    }

    // DLSS-G consumes depth / motion vectors / HUD-less colour at Present time on its own queue; the
    // renderer re-uses those textures next frame, so they are tagged volatile and SL copies them here.
    void beforePresent(const FrameIn& f) override {
        if (!tok || !fgAvail || !fgOn) return;
        sl::Extent rExt{ 0, 0, f.renderW, f.renderH }, oExt{ 0, 0, f.outW, f.outH };
        sl::Resource depth = res(f.depth, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        sl::Resource mvec = res(f.mvec, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        sl::Resource hud = res(f.output, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        sl::Resource back = res(f.backbuffer, f.backbufferState);
        sl::ResourceTag tags[] = {
            sl::ResourceTag(&depth, sl::kBufferTypeDepth,         sl::ResourceLifecycle::eOnlyValidNow, &rExt),
            sl::ResourceTag(&mvec,  sl::kBufferTypeMotionVectors, sl::ResourceLifecycle::eOnlyValidNow, &rExt),
            sl::ResourceTag(&hud,   sl::kBufferTypeHUDLessColor,  sl::ResourceLifecycle::eOnlyValidNow, &oExt),
            sl::ResourceTag(&back,  sl::kBufferTypeBackbuffer,    sl::ResourceLifecycle::eOnlyValidNow, &oExt),
        };
        sl::Result r = slSetTagForFrame(*tok, vp, tags, 4, f.cmd);
        if (r != sl::Result::eOk && (frameCounter % 300) == 1) say(std::string("FG tags rejected: ") + rs(r));
    }

    void present(IDXGISwapChain4* swap, UINT sync, UINT flags) override {
        marker(sl::PCLMarker::eRenderSubmitEnd);
        marker(sl::PCLMarker::ePresentStart);
        swap->Present(sync, flags);
        marker(sl::PCLMarker::ePresentEnd);
    }
    void afterPresent() override {
        if (!fgAvail) return;
        sl::DLSSGState st{};
        if (slDLSSGGetState(vp, st, nullptr) == sl::Result::eOk) {
            presented.fetch_add(st.numFramesActuallyPresented ? st.numFramesActuallyPresented : 1);
            if ((uint32_t)st.status != lastGState) { lastGState = (uint32_t)st.status; if (st.status != sl::DLSSGStatus::eOk) say("DLSS-G status: " + gStatus(st.status)); }
        }
    }

    std::string versionString() const override { return dlssVer + (fgAvail ? " | FG " + fgVer : ""); }
    uint32_t takePresentedCount() override { return presented.exchange(0); }
    bool hasPresentedCounter() const override { return fgAvail; }
};

} // namespace

Upscaler* createDLSS() { return new DlssUpscaler(); }

} // namespace up
#endif // UP_HAVE_DLSS
