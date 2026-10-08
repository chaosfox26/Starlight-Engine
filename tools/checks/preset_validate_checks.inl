// Test preset values are range-checked before they are narrowed, and a preset file is written through a checked stream (audit CORE-03, CORE-04).
#include "../../deep/src/preset_validate.hpp"
static void preset_validate_checks(const auto& require,const fs::path& folder){
    auto preset=[](const nlohmann::json& frameLimit,const nlohmann::json& durationMs){
        return nlohmann::json{{"schemaVersion",2},{"name","check"},{"settings",{{"resolution",{1280,720}},{"displayMode","windowed"},{"frameLimit",frameLimit},{"durationMs",durationMs},{"upscaler","off"},{"quality","native"},{"frameGeneration",false},{"vsync",false},{"culling",true},{"foreground",false}}}};
    };
    auto refused=[&](const nlohmann::json& frameLimit,const nlohmann::json& durationMs){
        try{starlight::preset::validate(preset(frameLimit,durationMs));}catch(const std::runtime_error&){return true;}
        return false;
    };
    require(!refused(600,60000),"CORE-04: an ordinary preset validates");
    require(!refused(10000,3600000),"CORE-04: the frame limit 10000 and duration 3600000 are accepted");
    require(refused(10001,60000)&&refused(600,3600001),"CORE-04: one past either limit is refused");
    require(refused(4294967297LL,60000),"CORE-04: a frame limit that would wrap to 1 when narrowed to int is refused");
    require(refused(600,4294967297LL),"CORE-04: a duration that would wrap to 1 when narrowed to int is refused");
    require(refused(2147483648LL,60000)&&refused(600,2147483648LL),"CORE-04: INT_MAX + 1 is refused instead of wrapping negative");
    require(refused(0,60000)&&refused(600,-1),"CORE-04: zero and negative values are still refused");
    const fs::path file=folder/L"preset-write.json";
    starlight::preset::write(file,preset(600,60000));
    std::ifstream in(file);
    require(nlohmann::json::parse(in)==preset(600,60000),"CORE-03: a written preset reads back unchanged");
    bool failed=false;
    try{starlight::preset::write(folder/L"no-such-folder"/L"preset.json",preset(600,60000));}catch(const std::runtime_error&){failed=true;}
    require(failed,"CORE-03: a preset that cannot be written is reported, not ignored");
}
