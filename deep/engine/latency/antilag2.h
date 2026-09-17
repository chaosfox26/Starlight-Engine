// Starlight DX12 client — AMD Anti-Lag 2 (header-only SDK, driver-backed: amdxc64.dll must be the
// loaded D3D12 UMD, i.e. an AMD GPU + AMD driver). Path-independent: works with any upscaler.
// Compiled against the SDK only when UP_HAVE_ANTILAG2 is defined (build/flags.json backends.antilag2).
#pragma once
#include <d3d12.h>
#include <string>
#include "../upscale/upscale.h"

namespace lat {

struct AntiLag2 {
    AntiLag2();
    ~AntiLag2();
    // after device creation; returns true when the driver exposes the Anti-Lag 2 interface
    bool init(ID3D12Device* device, up::LogFn log);
    void shutdown();
    void setEnabled(bool on);
    // per frame, in this order
    void update();                              // just before input is polled
    void markEndOfFrameRendering();             // right after the main ExecuteCommandLists
    void setFrameGenFrameType(bool interpolated); // just before Present
    bool compiledIn() const;
    bool supported() const { return avail; }
    bool enabled() const { return avail && on; }
    std::string status() const;                 // "live" | "off" | "hardware-gated (...)" | "n/a (...)"
    std::string version() const { return "Anti-Lag 2 SDK v2.0.0a"; }
private:
    struct Impl; Impl* impl = nullptr;
    up::LogFn log = nullptr;
    bool avail = false, on = false;
    std::string why;
};

} // namespace lat
