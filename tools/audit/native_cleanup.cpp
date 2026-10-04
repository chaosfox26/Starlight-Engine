#include "native_cleanup.hpp"
#include "native_result_summary.hpp"
#include <iostream>
using namespace starlight::audit;
int wmain(int argc,wchar_t** argv){try{
    if(argc==4 && std::wstring(argv[1])==L"status"){
        std::cout<<json{{"text",final_status(json::parse(read_file(argv[2])),json::parse(read_file(argv[3])))}}.dump()<<'\n';return 0;
    }
    if(argc<4 || argc>7)throw std::runtime_error("Usage: NativeCleanup build|report|captures root selected [--apply] [--keep=N] [preexisting.json]; or status summary.json findings.json");
    auto mode=utf8(argv[1]);auto root=fs::canonical(argv[2]);auto selected=fs::absolute(argv[3]).lexically_normal();bool apply=false;std::set<std::string> original;size_t keep=3;
    for(int i=4;i<argc;++i){if(std::wstring(argv[i])==L"--apply"){if(apply)throw std::runtime_error("Duplicate apply option");apply=true;}else{
        auto option=utf8(argv[i]);if(option.starts_with("--keep=")){auto number=option.substr(7);if(number.empty() || number.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("Invalid retention count");keep=std::stoull(number);if(keep<1)throw std::runtime_error("At least one run must be retained");continue;}
        auto list=json::parse(read_file(argv[i]));if(!list.is_array())throw std::runtime_error("Preexisting list must be an array");
        for(const auto& item:list){auto path=fs::u8path(item.get<std::string>());original.insert(utf8(path.generic_wstring()));}
    }}
    auto plan=mode=="captures"?capture_retention_plan(root,selected,keep):cleanup_plan(root,selected,mode,original);json result={{"mode",mode},{"applied",apply}};
    if(apply){
        auto folder=selected/"cleanup-records";if(!fs::exists(folder))fs::create_directory(folder);plain_chain(root,folder);
        auto log=folder/(L"native-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L".json");
        plan=apply_cleanup(root,mode=="captures"?root/"analysis":selected,plan,log);result["journal"]=utf8(log.generic_wstring());
        if(mode=="report")remove_empty_report_directories(root,selected);
    }
    result["files"]=plan;bool retained=false;for(const auto& item:plan)if(item.at("status")=="retained")retained=true;
    std::cout<<result.dump(2)<<'\n';return retained?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
