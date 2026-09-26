#include "render_contract.hpp"
#include <iostream>
#include <fstream>
int main(int argc, char** argv) {
  using nlohmann::json;
  json r{{"success",true},{"exitCode",0},{"width",1280},{"height",720},
    {"renderWidth",1280},{"renderHeight",720},{"nativeContractVerified",true},
    {"upscaling",false},{"frameGeneration",false},{"requestedTier",1},{"activeTier",1},
    {"denoiserRequested",0},{"rrActive",false},{"presentationMode","focused-present"},
    {"debugLayerRequested",false},{"debugLayerActive",false},{"frames",100},{"durationSeconds",5.1},
    {"gpuMs",2.0},{"headless",false},{"foreground",true},{"foregroundFrames",100},{"backgroundThrottleBypassed",false}};
  auto validate = [](const json &j) { return starlight::audit::validate_render(j,1280,720,1,"focused-present",false); };
  int count = 0;
  auto check = [&](bool value) { if (!value) throw std::runtime_error("Audit contract regression"); ++count; };
  try {
    if(argc==2) {
      std::ifstream input(argv[1]); json saved; input>>saved;
      check(starlight::audit::validate_visible_editor(saved));
      auto missing=saved; missing["checks"].erase(missing["checks"].end()-1);
      check(!starlight::audit::validate_visible_editor(missing));
      auto duplicate=saved; duplicate["checks"].push_back(duplicate["checks"][0]);
      check(!starlight::audit::validate_visible_editor(duplicate));
      auto failed=saved; failed["checks"][0]["pass"]=false;
      check(!starlight::audit::validate_visible_editor(failed));
      auto unfocused=saved; unfocused["checks"][0]["foreground"]=false;
      check(!starlight::audit::validate_visible_editor(unfocused));
    }
    check(validate(r)["performanceEligible"] == true);
    for (auto key : {"success","nativeContractVerified","foreground"}) { auto bad=r; bad[key]=false; check(validate(bad)["accepted"] == false); }
    for (auto key : {"renderHeight","frames","foregroundFrames","gpuMs"}) { auto bad=r; bad[key]=0; check(validate(bad)["accepted"] == false); }
    { auto bad=r; bad.erase("nativeContractVerified"); check(validate(bad)["accepted"] == false); }
    { auto bad=r; bad["frames"]="invalid"; check(validate(bad)["accepted"] == false); }
    { auto bad=r; bad["presentationMode"]="offscreen-no-present"; check(validate(bad)["accepted"] == false); }
    { auto bad=r; bad["debugLayerActive"]=true; check(validate(bad)["accepted"] == false); }
    { auto debug=r; debug["debugLayerActive"]=debug["debugLayerRequested"]=true;
      auto v=starlight::audit::validate_render(debug,1280,720,1,"focused-present",true);
      check(v["accepted"] == true && v["performanceEligible"] == false); }
    std::cout << count << " audit evidence checks passed\n";
    return 0;
  } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
