#include "render_limits.hpp"
#include "project_module.hpp"
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <memory>
#include "json.hpp"
#include "../engine/crash_diagnostics.h"
namespace fs=std::filesystem;
using json=nlohmann::json;
static std::wstring wide(const std::string&s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);if(!n&&!s.empty())throw std::runtime_error("Invalid UTF-8");std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),w.data(),n);return w;}
static std::wstring quote(const std::wstring&s){std::wstring r=L"\"";size_t n=0;for(auto c:s){if(c==L'\\'){++n;continue;}r.append(c==L'"'?n*2+1:n,L'\\');n=0;r+=c;}r.append(n*2,L'\\');return r+L"\"";}
static std::string u8text(const fs::path&p){auto s=p.u8string();return std::string(s.begin(),s.end());}
static json read(const fs::path&p){std::ifstream f(p);if(!f)throw std::runtime_error("Cannot open "+u8text(p));return json::parse(f);}
static void write(const fs::path&p,const json&j){std::ofstream f(p);if(!f || !(f<<j.dump(2)))throw std::runtime_error("Cannot write "+u8text(p));}
static void keys(const json&j,const std::set<std::string>&allowed){if(!j.is_object())throw std::runtime_error("Expected settings object");for(auto i=j.begin();i!=j.end();++i)if(!allowed.count(i.key()))throw std::runtime_error("Unknown setting: "+i.key()+". No safe migration is registered.");}
static void validate(const json&p){
    keys(p,{"schemaVersion","name","settings"});if(p.at("schemaVersion")!=2)throw std::runtime_error("Unsupported preset version");
    auto&s=p.at("settings");keys(s,{"resolution","displayMode","frameLimit","durationMs","upscaler","quality","frameGeneration","vsync","culling","foreground"});
    auto res=s.at("resolution");if(!res.is_array()||res.size()!=2)throw std::runtime_error("Resolution needs width and height");
    for(auto&v:res)if(!v.is_number_integer()||v.get<double>()<64||v.get<double>()>16384)throw std::runtime_error("Invalid resolution");
    if(!starlight::valid_render_size(res[0].get<int>(),res[1].get<int>()))throw std::runtime_error(starlight::renderSizeError);
    for(auto k:{"frameLimit","durationMs"})if(!s.at(k).is_number_integer()||s.at(k).get<double>()<=0)throw std::runtime_error("Positive numeric test setting required");
    if(s.at("frameLimit").get<int>()>10000||s.at("durationMs").get<int>()>3600000)throw std::runtime_error("Test setting outside supported range");
    for(auto k:{"frameGeneration","vsync","culling","foreground"})if(!s.at(k).is_boolean())throw std::runtime_error("Boolean test setting required");
    std::string mode=s.at("displayMode"),up=s.at("upscaler"),quality=s.at("quality");
    if(mode!="fullscreen"&&mode!="windowed"&&mode!="borderless")throw std::runtime_error("Unknown display mode");
    if(up!="off"&&up!="dlss"&&up!="fsr"&&up!="xess"&&up!="auto")throw std::runtime_error("Unknown upscaler");
    if(quality!="native"&&quality!="quality"&&quality!="balanced"&&quality!="performance"&&quality!="ultraperformance")throw std::runtime_error("Unknown quality");
}
#include "preset_reconciliation.inc"
static fs::path choose(const wchar_t*title,const wchar_t*filter){wchar_t file[32768]{};OPENFILENAMEW o{};o.lStructSize=sizeof(o);o.lpstrTitle=title;o.lpstrFile=file;o.nMaxFile=32768;o.lpstrFilter=filter;o.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;return GetOpenFileNameW(&o)?fs::path(file):fs::path();}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    fs::path errorFile=L"Starlight-launch-error.json";
    try{
        wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);fs::path root=fs::path(exe).parent_path().parent_path();errorFile=starlight_diagnostics::initialize();
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)throw std::runtime_error("Could not parse command line");std::unique_ptr<void,decltype(&LocalFree)> argvOwner(argv,LocalFree);
        fs::path projectPath=argc>1?fs::path(argv[1]):choose(L"Choose a project",L"Starlight projects\0*.star\0");
        if(projectPath.empty()){return 0;}
        fs::path presetPath=root/L"settings/default.startest",planPath;json approved=json::object();std::string scene;std::vector<std::wstring> extra;
        if(argc<2){auto choice=choose(L"Choose test settings (Cancel uses saved default)",L"Starlight test settings\0*.startest\0");if(!choice.empty())presetPath=choice;}
        for(int i=2;i<argc;++i){std::wstring a=argv[i];if(a==L"--accept-setting"){if(i+2>=argc)throw std::runtime_error("Confirmation needs old and supported setting names");std::string old=u8text(fs::path(argv[++i]));approved[old]=u8text(fs::path(argv[++i]));}else if(a==L"--preset"||a==L"--scene"||a==L"--plan"||a==L"--error"){if(++i>=argc)throw std::runtime_error("Missing launcher argument");if(a==L"--error")errorFile=argv[i];else if(a==L"--preset")presetPath=argv[i];else if(a==L"--plan")planPath=argv[i];else scene=u8text(fs::path(argv[i]));}else extra.push_back(a);}
        projectPath=fs::absolute(projectPath);auto j=read(projectPath);if(j.at("schemaVersion")!=3)throw std::runtime_error("Project needs schema version 3 migration");
        auto p=preset(presetPath,approved);auto&s=p.at("settings");
        if(scene.empty())scene=j.at("defaultScene").get<std::string>();
        if(!j.at("scenes").contains(scene))throw std::runtime_error("Selected project does not provide scene: "+scene);
        std::set<std::string> capabilities;for(auto&v:j.at("testCapabilities"))capabilities.insert(v.get<std::string>());
        for(auto i=s.begin();i!=s.end();++i)if(!capabilities.count(i.key()))throw std::runtime_error("Project does not support test setting: "+i.key());
        std::vector<std::wstring> args;for(auto&a:j.at("arguments"))args.push_back(wide(a.get<std::string>()));for(auto&a:j.at("scenes").at(scene).at("arguments"))args.push_back(wide(a.get<std::string>()));
        auto add=[&](const wchar_t*k,std::string v){args.push_back(k);args.push_back(wide(v));};
        add(L"--res",std::to_string(s["resolution"][0].get<int>())+"x"+std::to_string(s["resolution"][1].get<int>()));add(L"--display",s["displayMode"]);add(L"--upscaler",s["upscaler"]);add(L"--quality",s["quality"]);add(L"--bench",std::to_string(s["durationMs"].get<int>()));add(L"--nocull",s["culling"].get<bool>()?"0":"1");if(!s["vsync"].get<bool>())args.push_back(L"--novsync");if(s["frameGeneration"].get<bool>())args.push_back(L"--fg");
        // Raw options are limited to output destinations; test settings cannot silently override the preset.
        for(size_t i=0;i<extra.size();++i){if(extra[i]!=L"--report"&&extra[i]!=L"--log"&&extra[i]!=L"--shot"&&extra[i]!=L"--shotat")throw std::runtime_error("Put test options in the preset file");args.push_back(extra[i]);if(++i>=extra.size())throw std::runtime_error("Missing output argument");args.push_back(extra[i]);}
        fs::path module=project_module_path(projectPath.parent_path(),wide(j.at("module").get<std::string>()));
        if(!planPath.empty()){json a=json::array();for(auto&w:args)a.push_back(u8text(fs::path(w)));write(planPath,{{"project",u8text(projectPath)},{"scene",scene},{"module",u8text(module)},{"preset",u8text(fs::absolute(presetPath))},{"settings",s},{"arguments",a}});return 0;}
        SetEnvironmentVariableW(L"STARLIGHT_FRAME_LIMIT",std::to_wstring(s["frameLimit"].get<int>()).c_str());SetEnvironmentVariableW(L"STARLIGHT_FOREGROUND",s["foreground"].get<bool>()?L"1":L"0");
        std::wstring command;for(auto&a:args)command+=quote(a)+L" ";
        HMODULE library=LoadLibraryExW(module.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(!library)throw std::runtime_error("Project module could not load");
        using Run=int(WINAPI*)(HINSTANCE,LPWSTR,int);auto run=reinterpret_cast<Run>(GetProcAddress(library,"StarlightRunProject"));if(!run){FreeLibrary(library);throw std::runtime_error("Project entry point missing");}int result=run(instance,command.data(),show);FreeLibrary(library);return result;
    }catch(const std::exception&e){try{write(errorFile,{{"error",e.what()}});}catch(...){}return 3;}
}
