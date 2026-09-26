// SampleGame DX12 client — milestone 5 FSR backend (AMD FidelityFX SDK 2.3.0: upscaler 4.1.1 / 3.1.x
// fallback, frame generation 4.0.1 / 3.1.x fallback, FG swapchain 3.1.7). Compiled only when
// build/flags.json enables backends.fsr (buildctl adds UP_HAVE_FSR + the SDK include dirs and copies
// the signed DLLs next to the exe). Nothing here is linked statically: the loader DLL is LoadLibrary'd
// so a machine without the DLLs just reports "unavailable" and the renderer falls through to Off.
// The provider that ACTUALLY activated (ML upscaler vs the 3.1 fallback on non-AMD hardware) is read
// back from the API and surfaced in versionString() — never assumed.
#ifdef UP_HAVE_FSR
#define _WINDOWS 1
#include "upscale.h"
#include <atomic>
#include <vector>
#include <ffx_api.h>
#include <ffx_api_loader.h>
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_framegeneration_dx12.h>

using Microsoft::WRL::ComPtr;

namespace up {
namespace {

struct FsrUpscaler;
static FsrUpscaler* g_fsrForMessages = nullptr;   // the SDK message callback has no user pointer

struct FsrUpscaler : Upscaler {
    HMODULE      loader = nullptr;
    ffxFunctions fx{};
    ffxContext   upCtx = nullptr;
    ffxContext   fgCtx = nullptr;
    ffxContext   scCtx = nullptr;       // frame-generation swapchain (wraps the renderer's)
    ComPtr<ID3D12Device> dev;
    ID3D12CommandQueue*  queue = nullptr;
    IDXGISwapChain4*     fgSwap = nullptr;
    LogFn   log = nullptr;
    UINT    outW = 0, outH = 0;
    int     curMode = 1;
    bool    fgOn = false, fgAvail = false;
    uint64_t frameId = 0;
    std::string upVer = "fsr (provider unknown)", fgVer;
    std::atomic<uint32_t> presented{ 0 };

    ~FsrUpscaler() override { shutdown(); }
    Path path() const override { return Path::FSR; }

    void say(const std::string& s) { if (log) log("fsr: " + s); }
    static void msgCb(uint32_t type, const wchar_t* m) {
        if (!g_fsrForMessages || !m) return;
        std::string s; for (const wchar_t* p = m; *p; ++p) s.push_back(*p < 128 ? (char)*p : '?');
        g_fsrForMessages->say(std::string(type == FFX_API_MESSAGE_TYPE_ERROR ? "SDK error: " : "SDK warning: ") + s);
    }

    bool preDevice(LogFn l) override {
        log = l;
        wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir(exe); size_t k = dir.find_last_of(L"\\/"); dir = (k == std::wstring::npos) ? L"." : dir.substr(0, k);
        loader = LoadLibraryW((dir + L"\\amd_fidelityfx_loader_dx12.dll").c_str());
        if (!loader) loader = LoadLibraryW(L"amd_fidelityfx_loader_dx12.dll");
        if (!loader) { say("amd_fidelityfx_loader_dx12.dll not found next to the exe"); return false; }
        ffxLoadFunctions(&fx, loader);
        if (!fx.CreateContext || !fx.DestroyContext || !fx.Configure || !fx.Query || !fx.Dispatch) { say("loader DLL is missing entry points"); return false; }
        return true;
    }

    std::string providerName(ffxContext ctx) {
        ffxQueryGetProviderVersion q{}; q.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if (fx.Query(&ctx, &q.header) == FFX_API_RETURN_OK && q.versionName) return q.versionName;
        return "?";
    }
    std::string listVersions(uint64_t createType) {
        ffxQueryDescGetVersions q{}; q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        q.createDescType = createType; q.device = dev.Get(); uint64_t n = 0; q.outputCount = &n;
        if (fx.Query(nullptr, &q.header) != FFX_API_RETURN_OK || n == 0) return "(none)";
        std::vector<uint64_t> ids(n); std::vector<const char*> names(n);
        q.versionIds = ids.data(); q.versionNames = names.data();
        if (fx.Query(nullptr, &q.header) != FFX_API_RETURN_OK) return "(query failed)";
        std::string s; for (uint64_t i = 0; i < n; ++i) { if (i) s += ", "; s += names[i] ? names[i] : "?"; }
        return s;
    }

    bool init(const DeviceCtx& ctx, ComPtr<IDXGISwapChain4>& swap) override {
        dev = ctx.device; queue = ctx.queue; outW = ctx.outW; outH = ctx.outH; log = ctx.log;
        g_fsrForMessages = this;
        say("available upscaler providers: " + listVersions(FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE));
        say("available frame-gen providers: " + listVersions(FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION));

        // ---- upscaler context: colour is 8-bit display-referred (no tonemapper), depth NOT inverted ----
        ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = dev.Get();
        ffxCreateContextDescUpscaleVersion uv{}; uv.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION; uv.version = FFX_UPSCALER_VERSION;
        ffxCreateContextDescUpscale ud{}; ud.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        ud.flags = FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        ud.maxRenderSize = { outW, outH }; ud.maxUpscaleSize = { outW, outH }; ud.fpMessage = &msgCb;
        ud.header.pNext = &be.header; be.header.pNext = &uv.header;
        ffxReturnCode_t rc = fx.CreateContext(&upCtx, &ud.header, nullptr);
        if (rc != FFX_API_RETURN_OK) { say("upscale context failed rc=" + std::to_string(rc)); upCtx = nullptr; return false; }
        upVer = providerName(upCtx);
        say("upscaler provider ACTIVE: " + upVer);

        // ---- frame generation: REPLACE the renderer's swapchain with the SDK's frame-interpolation one
        // (same desc, same hwnd; DXGI allows one swapchain per hwnd so the old one is released first),
        // then the FG context. Optional: any failure = FG n/a, the plain swapchain is recreated. ----
        {
            DXGI_SWAP_CHAIN_DESC1 sd{}; swap->GetDesc1(&sd);
            ComPtr<IDXGIFactory> factory; swap->GetParent(IID_PPV_ARGS(&factory));
            swap.Reset();
            DXGI_SWAP_CHAIN_FULLSCREEN_DESC fs{}; fs.Windowed = TRUE;
            IDXGISwapChain4* created = nullptr;
            ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 cd{};
            cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
            cd.swapchain = &created; cd.hwnd = ctx.hwnd; cd.desc = &sd; cd.fullscreenDesc = &fs;
            cd.dxgiFactory = factory.Get(); cd.gameQueue = queue;
            ffxCreateContextDescFrameGenerationSwapChainVersionDX12 sv{}; sv.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12; sv.version = FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;
            cd.header.pNext = &sv.header;
            rc = fx.CreateContext(&scCtx, &cd.header, nullptr);
            if (rc != FFX_API_RETURN_OK || !created) {
                say("FG swapchain create failed rc=" + std::to_string(rc) + " (FG n/a) - recreating plain swapchain"); scCtx = nullptr;
                ComPtr<IDXGIFactory2> f2; factory.As(&f2);
                ComPtr<IDXGISwapChain1> s1;
                if (!f2 || FAILED(f2->CreateSwapChainForHwnd(queue, ctx.hwnd, &sd, nullptr, nullptr, &s1)) || FAILED(s1.As(&swap))) { say("plain swapchain recreate FAILED"); return false; }
            } else {
                swap.Attach(created);
                fgSwap = created;
                ffxCreateBackendDX12Desc be2{}; be2.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be2.device = dev.Get();
                ffxCreateContextDescFrameGenerationVersion fv{}; fv.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION; fv.version = FFX_FRAMEGENERATION_VERSION;
                ffxCreateContextDescFrameGeneration fd{}; fd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
                fd.flags = 0; fd.displaySize = { outW, outH }; fd.maxRenderSize = { outW, outH };
                fd.backBufferFormat = ffxApiGetSurfaceFormatDX12(ctx.backbufferFormat);
                fd.header.pNext = &be2.header; be2.header.pNext = &fv.header;
                rc = fx.CreateContext(&fgCtx, &fd.header, nullptr);
                if (rc != FFX_API_RETURN_OK) { say("FG context failed rc=" + std::to_string(rc) + " (FG n/a)"); fgCtx = nullptr; }
                else { fgAvail = true; fgVer = providerName(fgCtx); say("frame-gen provider ACTIVE: " + fgVer + " (swapchain wrapped)"); }
            }
        }
        return true;
    }

    void shutdown() override {
        if (fgCtx) { configureFG(false); fx.DestroyContext(&fgCtx, nullptr); fgCtx = nullptr; }
        if (upCtx) { fx.DestroyContext(&upCtx, nullptr); upCtx = nullptr; }
        if (scCtx) { fx.DestroyContext(&scCtx, nullptr); scCtx = nullptr; }
        dev.Reset(); if (g_fsrForMessages == this) g_fsrForMessages = nullptr;
        loader = nullptr;   // deliberately NOT FreeLibrary'd: the loader keeps the effect DLLs resident and
                            // unloading it under them crashed at process exit; the OS unloads all three cleanly
    }

    int modeCount() const override { return kStdModeCount; }
    const char* modeName(int i) const override { return (i >= 0 && i < kStdModeCount) ? kStdModes[i].name : "?"; }
    int mode() const override { return curMode; }
    bool setMode(int i, UINT& rw, UINT& rh) override {
        if (i < 0 || i >= kStdModeCount || !upCtx) return false;
        ffxQueryDescUpscaleGetRenderResolutionFromQualityMode q{};
        q.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE;
        q.displayWidth = outW; q.displayHeight = outH; q.qualityMode = (uint32_t)i;   // kStdModes order == FfxApiUpscaleQualityMode
        uint32_t w = 0, h = 0; q.pOutRenderWidth = &w; q.pOutRenderHeight = &h;
        if (fx.Query(&upCtx, &q.header) != FFX_API_RETURN_OK || !w || !h) { say("render-size query failed for mode " + std::to_string(i)); return false; }
        curMode = i; rw = w; rh = h; return true;
    }

    bool fgSupported() const override { return fgAvail; }
    bool fgEnabled() const override { return fgOn; }
    void setFG(bool on) override { if (!fgAvail) { fgOn = false; return; } fgOn = on; }
    std::string fgStatus() const override { return !fgAvail ? "FG n/a" : fgOn ? "FG 2x" : "FG off"; }

    static ffxReturnCode_t fgDispatchCb(ffxDispatchDescFrameGeneration* p, void* user) {
        FsrUpscaler* self = (FsrUpscaler*)user;
        self->presented.fetch_add(1 + p->numGeneratedFrames);
        return self->fx.Dispatch(&self->fgCtx, &p->header);
    }
    void configureFG(bool on) {
        if (!fgCtx || !fgSwap) return;
        ffxConfigureDescFrameGeneration c{}; c.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        c.swapChain = fgSwap; c.frameGenerationEnabled = on; c.allowAsyncWorkloads = false;
        c.frameGenerationCallback = &fgDispatchCb; c.frameGenerationCallbackUserContext = this;
        c.presentCallback = nullptr; c.HUDLessColor = FfxApiResource{}; c.flags = 0;
        c.onlyPresentGenerated = false; c.generationRect = { 0, 0, (int32_t)outW, (int32_t)outH }; c.frameID = frameId;
        ffxReturnCode_t rc = fx.Configure(&fgCtx, &c.header);
        if (rc != FFX_API_RETURN_OK) say("FG configure rc=" + std::to_string(rc));
    }

    void evaluate(const FrameIn& f) override {
        ++frameId;
        ffxDispatchDescUpscale d{}; d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        d.commandList = f.cmd;
        d.color = ffxApiGetResourceDX12(f.color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.depth = ffxApiGetResourceDX12(f.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.motionVectors = ffxApiGetResourceDX12(f.mvec, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.exposure = ffxApiGetResourceDX12(f.exposure, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        d.output = ffxApiGetResourceDX12(f.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, FFX_API_RESOURCE_USAGE_UAV);
        d.jitterOffset = { f.jitterX, f.jitterY };
        d.motionVectorScale = { 1.0f, 1.0f };            // mvec already in pixels, previous - current
        d.renderSize = { f.renderW, f.renderH }; d.upscaleSize = { f.outW, f.outH };
        d.enableSharpening = false; d.sharpness = 0.0f;
        d.frameTimeDelta = f.dtMs; d.preExposure = 1.0f; d.reset = f.reset;
        d.cameraNear = f.nearZ; d.cameraFar = f.farZ; d.cameraFovAngleVertical = f.fovY; d.viewSpaceToMetersFactor = 1.0f;
        d.flags = FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
        ffxReturnCode_t rc = fx.Dispatch(&upCtx, &d.header);
        if (rc != FFX_API_RETURN_OK && (frameId % 300) == 1) say("upscale dispatch rc=" + std::to_string(rc));

        if (fgCtx && fgOn) {
            ffxDispatchDescFrameGenerationPrepareV2 p{}; p.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
            p.frameID = frameId; p.flags = 0; p.commandList = f.cmd;
            p.renderSize = { f.renderW, f.renderH }; p.jitterOffset = { f.jitterX, f.jitterY }; p.motionVectorScale = { 1.0f, 1.0f };
            p.frameTimeDelta = f.dtMs; p.reset = f.reset;
            p.cameraNear = f.nearZ; p.cameraFar = f.farZ; p.cameraFovAngleVertical = f.fovY; p.viewSpaceToMetersFactor = 1.0f;
            p.depth = ffxApiGetResourceDX12(f.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            p.motionVectors = ffxApiGetResourceDX12(f.mvec, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            p.cameraPosition[0] = f.camPos.x; p.cameraPosition[1] = f.camPos.y; p.cameraPosition[2] = f.camPos.z;
            p.cameraUp[0] = f.camUp.x; p.cameraUp[1] = f.camUp.y; p.cameraUp[2] = f.camUp.z;
            p.cameraRight[0] = f.camRight.x; p.cameraRight[1] = f.camRight.y; p.cameraRight[2] = f.camRight.z;
            p.cameraForward[0] = f.camFwd.x; p.cameraForward[1] = f.camFwd.y; p.cameraForward[2] = f.camFwd.z;
            rc = fx.Dispatch(&fgCtx, &p.header);
            if (rc != FFX_API_RETURN_OK && (frameId % 300) == 1) say("FG prepare rc=" + std::to_string(rc));
        }
    }

    void beforePresent(const FrameIn&) override { if (fgCtx) configureFG(fgOn); }
    void afterPresent() override { if (fgAvail && !fgOn) presented.fetch_add(1); }   // the FG dispatch callback only fires while FG is on

    std::string versionString() const override { return upVer + (fgAvail ? " | FG " + fgVer : ""); }
    uint32_t takePresentedCount() override { return presented.exchange(0); }
    bool hasPresentedCounter() const override { return fgAvail; }
};

} // namespace

Upscaler* createFSR() { return new FsrUpscaler(); }

} // namespace up
#endif // UP_HAVE_FSR
