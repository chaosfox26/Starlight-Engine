// Test preset values are range-checked before they are narrowed, and a preset file is written through a checked stream (audit CORE-03, CORE-04).
#include "../../deep/src/preset_validate.hpp"
namespace launcher_preset_checks {
namespace fs=std::filesystem;using json=nlohmann::json;
using starlight::preset::u8text;using starlight::preset::write;using starlight::preset::keys;using starlight::preset::validate;
#include "../../deep/src/preset_reconciliation.inc"
}
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
    auto refusedQuality=[&](const char* quality){
        auto p=preset(600,60000);p["settings"]["quality"]=quality;
        try{starlight::preset::validate(p);}catch(const std::runtime_error&){return true;}
        return false;
    };
    for(const char* quality:{"native","quality","balanced","performance","ultraperf"})require(!refusedQuality(quality),"QUALITY: every quality name the engine's mode table knows (kStdModes, including ultraperf) is accepted by a preset");
    const fs::path file=folder/L"preset-write.json";
    starlight::preset::write(file,preset(600,60000));
    std::ifstream in(file);
    require(nlohmann::json::parse(in)==preset(600,60000),"CORE-03: a written preset reads back unchanged");
    bool failed=false;
    try{starlight::preset::write(folder/L"no-such-folder"/L"preset.json",preset(600,60000));}catch(const std::runtime_error&){failed=true;}
    require(failed,"CORE-03: a preset that cannot be written is reported, not ignored");

    const fs::path dir=folder/L"launcher-preset-checks";
    fs::create_directories(dir);
    auto put=[](const fs::path& p,const std::string& text){std::ofstream f(p,std::ios::binary);f<<text;};
    auto refusedPreset=[&](const std::string& text){
        const fs::path file=dir/L"deep.startest";put(file,text);
        try{launcher_preset_checks::preset(file,nlohmann::json::object());}catch(const std::runtime_error&){return true;}
        return false;
    };
    const std::string nested(5000,'[');
    require(refusedPreset("{\"schemaVersion\":2,\"name\":"+nested+std::string(5000,']')+",\"settings\":{}}"),"LAUNCH-1: a preset nested thousands of levels deep is refused instead of overflowing the stack");
    require(refusedPreset("{\"schemaVersion\":2,\"name\":"+nested+",\"settings\":{}}"),"LAUNCH-1: an unterminated deeply nested preset is refused");
    require(refusedPreset("{\"schemaVersion\":2,\"name\":\""+std::string(1100000,'x')+"\",\"settings\":{}}"),"LAUNCH-1: a preset over one mebibyte is refused");
    require(refusedPreset("{\"schemaVersion\":2,\"name\":5,\"settings\":{\"resolution\":[1280,720],\"displayMode\":\"windowed\",\"frameLimit\":600,\"durationMs\":1000,\"upscaler\":\"off\",\"quality\":\"native\",\"frameGeneration\":false,\"vsync\":false,\"culling\":true,\"foreground\":false}}"),"LAUNCH-1: a preset whose name is not text is refused");
    {
        const fs::path file=dir/L"brackets-in-text.json";put(file,"{\"a\":\"[[[[[[[[[[[[[[[[[[[[ \\\" {{{{{{{{{{\"}");
        bool read=true;try{read=starlight::preset::read_limited(file).at("a").is_string();}catch(const std::exception&){read=false;}
        require(read,"LAUNCH-1: brackets inside text do not count as nesting");
    }

    const std::string v1="{\"schemaVersion\":1,\"name\":\"old\",\"settings\":{\"resolution\":[1280,720],\"displayMode\":\"windowed\",\"frameLimit\":600,\"durationMs\":1000,\"upscaler\":\"off\",\"quality\":\"native\",\"frameGeneration\":false,\"vsync\":false,\"culling\":true,\"foreground\":false}}";
    auto migrated=[&](const fs::path& file){
        try{return launcher_preset_checks::preset(file,nlohmann::json::object()).at("schemaVersion")==2;}catch(const std::exception&){return false;}
    };
    {
        const fs::path file=dir/L"torn-backup.startest";put(file,v1);
        fs::path backup=file;backup+=L".v1.backup";put(backup,"{\"schemaVersion\":1,\"na");
        const bool ok=migrated(file);
        int extra=0;for(auto& e:fs::directory_iterator(dir))if(e.path().filename().wstring().rfind(L"torn-backup.startest.v1.backup.",0)==0)++extra;
        require(ok&&extra==1,"LAUNCH-3: a torn backup from an earlier killed run does not block the launch; a new backup is written beside it");
    }
    {
        const fs::path file=dir/L"torn-report.startest";put(file,v1);
        require(migrated(file),"LAUNCH-3: setup migrates the preset");
        fs::path report=file;report+=L".reconciliation.json";put(report,"{torn");
        require(migrated(file),"LAUNCH-3: a torn reconciliation report does not block a clean preset");
        put(report,"[]");
        require(migrated(file),"LAUNCH-3: a reconciliation report that is not an object does not block a clean preset");
    }
    {
        const fs::path file=dir/L"unwritable-report.startest";put(file,v1);
        fs::path report=file;report+=L".reconciliation.json";fs::create_directories(report);
        require(migrated(file),"LAUNCH-3: a report that cannot be written does not fail a launch whose preset is already migrated");
        std::ifstream in(file);
        require(nlohmann::json::parse(in).at("schemaVersion")==2,"LAUNCH-3: the preset was migrated even though its report could not be written");
    }

    const fs::path ansi=dir/L"ansi-error.json";
    bool wrote=true;
    try{starlight::preset::write(ansi,nlohmann::json{{"error","Cannot copy caf\xE9 settings"}});}catch(const std::exception&){wrote=false;}
    require(wrote&&fs::exists(ansi),"LAUNCH-2: an error message holding non-UTF-8 text is still written as the launch error file");
}
