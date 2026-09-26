#pragma once
#include "../../deep/src/json.hpp"
#include <cmath>
namespace starlight::audit {
inline bool validate_visible_editor(const nlohmann::json &r) {
  try {
    const char* required[]={"File menu","Builder menu","Environment menu","Characters panel",
      "More catalogue","Place shape","Advanced build and body controls","Terrain settings",
      "Shapes and snapping","Nature tools","Sculpt tools","Camera controls","Music panel",
      "Project selection","Fullscreen live telemetry","Windowed live telemetry restored"};
    if(r.at("pass")!=true || !r.at("checks").is_array()) return false;
    for(const auto& c:r.at("checks"))
      if(c.at("pass")!=true || c.at("foreground")!=true || c.at("captureSucceeded")!=true) return false;
    for(const auto* name:required) {
      int matches=0;
      for(const auto& c:r.at("checks")) if(c.at("name")==name) ++matches;
      if(matches!=1) return false;
    }
    return true;
  } catch(...) { return false; }
}
inline nlohmann::json validate_render(const nlohmann::json &r, int width, int height,
                                     int tier, const std::string &presentation, bool debug) {
  using nlohmann::json;
  json errors = json::array();
  auto check = [&](bool valid, const char *reason) { if (!valid) errors.push_back(reason); };
  try {
    check(r.at("success") == true && r.at("exitCode") == 0, "Renderer did not pass");
    check(r.at("width") == width && r.at("height") == height &&
          r.at("renderWidth") == width && r.at("renderHeight") == height, "Native dimensions mismatch");
    check(r.at("nativeContractVerified") == true && r.at("upscaling") == false &&
          r.at("frameGeneration") == false, "Native rendering contract not verified");
    check(r.at("requestedTier") == tier && r.at("activeTier") == tier, "Requested renderer tier not active");
    check(r.at("denoiserRequested") == 0 && r.at("rrActive") == false, "Native wavelet contract mismatch");
    check(r.at("presentationMode") == presentation, "Presentation contract mismatch");
    check(r.at("debugLayerRequested") == debug && r.at("debugLayerActive") == debug, "Debug instrumentation mismatch");
    const auto frames = r.at("frames").get<long long>();
    const auto duration = r.at("durationSeconds").get<double>();
    const auto gpu = r.at("gpuMs").get<double>();
    check(frames > 0 && std::isfinite(duration) && duration >= 5 && std::isfinite(gpu) && gpu > 0,
          "Insufficient measured GPU work");
    if (presentation == "focused-present")
      check(r.at("headless") == false && r.at("foreground") == true &&
            r.at("foregroundFrames") == frames && r.at("backgroundThrottleBypassed") == false,
            "Visible test did not remain foreground");
    else if (presentation == "offscreen-no-present")
      check(r.at("benchmarkContract").get<int>() >= 2 && r.at("backgroundThrottleBypassed") == true,
            "Unsafe or unknown offscreen contract");
    else check(false, "Unknown presentation mode");
  } catch (const std::exception &) { errors.push_back("Missing or malformed evidence"); }
  return {{"accepted",errors.empty()}, {"errors",errors},
          {"performanceEligible",errors.empty() && !debug}, {"presentationMode",presentation}};
}
}
