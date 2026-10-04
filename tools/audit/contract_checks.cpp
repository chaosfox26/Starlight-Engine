#include "render_contract.hpp"
#include "visible_resolution.hpp"
#include "denoise_data.hpp"
#include "display_test_sizes.hpp"
#include "../../deep/src/render_limits.hpp"
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
    check(starlight::valid_render_size(15360,8640));
    check(starlight::valid_render_size(8640,15360));
    check(starlight::valid_render_size(16384,8100));
    check(!starlight::valid_render_size(16384,8101));
    check(!starlight::valid_render_size(15360,8641));
    check(!starlight::valid_render_size(16385,720));
    check(!starlight::valid_render_size(-1,720));
    check(!starlight::valid_render_size(0,720));
    // Sweep irregular dimensions as well as named display presets. New or
    // custom aspect ratios must not require adding an entry to the catalogue.
    for(int width=720;width<=8192;width+=113)for(int height=720;height<=4320;height+=97){
      const auto v=starlight::audit::visible_resolution(width,height);
      check(v.width>0 && v.height>0 && v.width<=width && v.height<=height);
      check(int64_t(v.width)*v.height<=3840*2160);
      check(std::abs(double(v.width)/v.height-double(width)/height)<.03);
      if(int64_t(width)*height>=1280*720 && int64_t(width)*height<=3840*2160)
        check(v.supported && v.fullscreen && v.width==width && v.height==height);
    }
    for(const auto size:starlight::audit::display_test_sizes){
      const auto v=starlight::audit::visible_resolution(size.width,size.height);
      check(v.supported && int64_t(v.width)*v.height<=3840*2160);
      check(std::abs(double(v.width)/v.height-double(size.width)/size.height)<.005);
      if(int64_t(size.width)*size.height<=3840*2160)check(v.fullscreen && v.width==size.width && v.height==size.height);
    }
    {
      std::string old(216,'\0');
      auto put=[&](size_t offset,auto value){std::memcpy(old.data()+offset,&value,sizeof(value));};
      put(0,uint32_t(0x37564c53));put(4,uint32_t(1));
      put(156,uint32_t(1));put(196,float(10));put(200,uint32_t(3));
      const auto current=starlight::audit::identified_diagnostic_packet(old);
      check(current.size()==old.size()+12);
      check(starlight::audit::data_value<uint64_t>(current,48)==1);
      check(starlight::audit::data_value<uint32_t>(current,56)==0);
      check(current.substr(60)==old.substr(48));
      check(starlight::audit::prop_offset(old).first==72);
      check(starlight::audit::prop_offset(current).first==84);
      bool rejected=false;try{starlight::audit::identified_diagnostic_packet(old.substr(0,210));}catch(const std::exception&){rejected=true;}
      check(rejected);
    }
    for (const auto size : {std::pair{1280,720},std::pair{1920,1080},std::pair{2560,1440},std::pair{3840,2160},std::pair{2560,1080},std::pair{3440,1440},std::pair{3840,1600},std::pair{5120,1440},std::pair{1280,800},std::pair{1920,1200},std::pair{2560,1600},std::pair{3840,1080}}) {
      const auto v=starlight::audit::visible_resolution(size.first,size.second);
      check(v.supported && v.fullscreen && v.width==size.first && v.height==size.second);
    }
    { const auto v=starlight::audit::visible_resolution(7680,4320);
      check(v.supported && !v.fullscreen && v.width==3840 && v.height==2160); }
    { const auto v=starlight::audit::visible_resolution(7680,2160);
      check(v.supported && !v.fullscreen && int64_t(v.width)*v.height<=3840*2160);
      check(std::abs(double(v.width)/v.height-7680.0/2160)<.005); }
    check(!starlight::audit::visible_resolution(0,0).supported);
    check(!starlight::audit::visible_resolution(1024,768).supported);
    check(!starlight::audit::visible_resolution(1280,600).supported);
    if(argc==2) {
      std::ifstream input(argv[1]); json saved; input>>saved;
      check(starlight::audit::validate_visible_editor(saved));
      auto missing=saved;
      for(auto it=missing["checks"].begin();it!=missing["checks"].end();++it)
        if(it->at("name")=="Fullscreen live telemetry"){missing["checks"].erase(it);break;}
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
    { auto unavailable=r; unavailable["debugLayerRequested"]=true;
      auto v=starlight::audit::validate_render(unavailable,1280,720,1,"focused-present",true);
      check(v["accepted"] == false && v["performanceEligible"] == false);
      check(v["errors"].size()==1 && v["errors"][0].get<std::string>().find("debug layer is inactive")!=std::string::npos); }
    std::cout << count << " audit evidence checks passed\n";
    return 0;
  } catch (const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
