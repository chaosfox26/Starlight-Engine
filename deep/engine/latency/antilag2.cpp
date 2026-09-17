// Starlight DX12 client — AMD Anti-Lag 2 wrapper (see antilag2.h).
#include "antilag2.h"
#include <cstdio>
#ifdef UP_HAVE_ANTILAG2
#include <ffx_antilag2_dx12.h>
#endif

namespace lat {

struct AntiLag2::Impl {
#ifdef UP_HAVE_ANTILAG2
    AMD::AntiLag2DX12::Context ctx{};
#else
    int unused = 0;
#endif
};

AntiLag2::AntiLag2() : impl(new Impl()) {}
AntiLag2::~AntiLag2() { shutdown(); delete impl; }

bool AntiLag2::compiledIn() const {
#ifdef UP_HAVE_ANTILAG2
    return true;
#else
    return false;
#endif
}

bool AntiLag2::init(ID3D12Device* device, up::LogFn logFn) {
    log = logFn;
#ifdef UP_HAVE_ANTILAG2
    HRESULT hr = AMD::AntiLag2DX12::Initialize(&impl->ctx, device);
    if (hr == S_OK) { avail = true; why.clear(); }
    else if (hr == E_HANDLE) why = "hardware-gated (AMD driver amdxc64.dll not loaded: not an AMD GPU/driver)";
    else { char b[64]; snprintf(b, sizeof(b), "failed (Initialize hr=0x%08lx)", (unsigned long)hr); why = b; }
#else
    (void)device;
    why = "n/a (not compiled in: backends.antilag2.enabled=false)";
#endif
    if (log) log("antilag2: " + (avail ? std::string("available (") + version() + ")" : why));
    return avail;
}

void AntiLag2::shutdown() {
#ifdef UP_HAVE_ANTILAG2
    if (avail) { AMD::AntiLag2DX12::Update(&impl->ctx, false, 0); AMD::AntiLag2DX12::DeInitialize(&impl->ctx); }
#endif
    avail = false; on = false;
}

void AntiLag2::setEnabled(bool want) { on = want; }

void AntiLag2::update() {
#ifdef UP_HAVE_ANTILAG2
    if (avail) AMD::AntiLag2DX12::Update(&impl->ctx, on, 0);
#endif
}

void AntiLag2::markEndOfFrameRendering() {
#ifdef UP_HAVE_ANTILAG2
    if (avail && on) AMD::AntiLag2DX12::MarkEndOfFrameRendering(&impl->ctx);
#endif
}

void AntiLag2::setFrameGenFrameType(bool interpolated) {
#ifdef UP_HAVE_ANTILAG2
    if (avail && on) AMD::AntiLag2DX12::SetFrameGenFrameType(&impl->ctx, interpolated);
#else
    (void)interpolated;
#endif
}

std::string AntiLag2::status() const {
    if (!avail) return why;
    return on ? "live" : "off";
}

} // namespace lat
