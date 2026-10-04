#include "platform.hpp"
#include "../audit/native_process.hpp"
#include "../audit/native_pe.hpp"
#include <iostream>
using namespace starlight::editor;
int wmain(int argc,wchar_t** argv){
    fs::path build,packet,out;std::wstring config=L"Release";bool peOnly=false;
    try{
        for(int i=1;i<argc;++i){std::wstring key=argv[i];if(key==L"--pe-only"){peOnly=true;continue;}if(++i>=argc)throw std::runtime_error("Missing argument value");if(key==L"--build-dir")build=fs::absolute(argv[i]);else if(key==L"--packet")packet=fs::absolute(argv[i]);else if(key==L"--output")out=fs::absolute(argv[i]);else if(key==L"--config")config=argv[i];else throw std::runtime_error("Unknown option");}
        if(build.empty() || out.empty() || (!peOnly && !fs::is_regular_file(packet)))throw std::runtime_error("Build directory, output and diagnostic packet required");
        if(!fs::create_directories(out))throw std::runtime_error("Output must be new");
        json results=json::array(),pe=json::array();bool passed=true;
        struct Case{std::wstring name;int tier,denoiser;std::map<std::wstring,std::wstring> environment;};
        std::vector<Case> cases;for(int i=0;i<5;++i)cases.push_back({L"tier-"+std::to_wstring(i),i,0,{}});
        cases.push_back({L"rr-auto",3,1,{}});cases.push_back({L"rr-disabled",3,1,{{L"STARLIGHT_DISABLE_RR",L"1"}}});cases.push_back({L"dxr-disabled",4,1,{{L"STARLIGHT_DISABLE_DXR",L"1"}}});cases.push_back({L"rr-missing",3,1,{{L"STARLIGHT_RR_DIRECTORY",(out/L"missing-sdk").wstring()}}});
        if(peOnly)cases.clear();
        for(const auto& item:cases){auto report=out/(item.name+L".json"),shot=out/(item.name+L".bmp");json data;
            try{
                auto run=starlight::audit::run(build/config/L"Starlight.NativeRenderChecks.exe",{packet.wstring(),report.wstring(),shot.wstring(),L"1280",L"720",std::to_wstring(item.tier),L"headless",L"4",std::to_wstring(item.denoiser)},out,out/(item.name+L".log"),75000,{},item.environment);
                data=fs::is_regular_file(report)?json::parse(read_file(report)):json::object();
                bool ok=!run.timedOut && run.exitCode==0 && data.value("success",false) && data.value("renderWidth",0)==1280 && data.value("renderHeight",0)==720;
                if(item.name==L"dxr-disabled")ok=ok && data.value("activeTier",-1)==0;
                if(item.name==L"rr-disabled" || item.name==L"rr-missing")ok=ok && !data.value("rrActive",false);
                data["exitCode"]=run.exitCode;data["timedOut"]=run.timedOut;data["passed"]=ok;
            }catch(const std::exception& e){data={{"passed",false},{"error",e.what()}};}
            data["case"]=utf8(item.name);passed=passed && data.at("passed").get<bool>();results.push_back(data);atomic_write(out/L"render-matrix.json",results.dump(2));std::cout<<utf8(item.name)<<' '<<(data.at("passed").get<bool>()?"PASS":"FAIL")<<std::endl;
        }
        for(const auto& item:fs::recursive_directory_iterator(build/config)){auto ext=item.path().extension().wstring();std::transform(ext.begin(),ext.end(),ext.begin(),::towlower);if(ext!=L".exe" && ext!=L".dll")continue;json info;try{info=starlight::audit::inspect_pe(read_file(item.path()));}catch(const std::exception& e){info={{"passed",false},{"error",e.what()}};}info["path"]=item.path().lexically_relative(build).generic_string();passed=passed && info.at("passed").get<bool>();pe.push_back(info);}
        atomic_write(out/L"native-pe.json",pe.dump(2));return passed && !pe.empty()?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
