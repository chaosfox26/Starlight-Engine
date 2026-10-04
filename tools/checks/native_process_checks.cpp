#define NOMINMAX
#include "../audit/native_process.hpp"
#include <fstream>
#include <iostream>
namespace fs=std::filesystem;
using namespace starlight::audit;
int wmain(int argc,wchar_t** argv){
    if(argc>1 && std::wstring(argv[1])==L"--child"){
        if(argc==3 && std::wstring(argv[2])==L"wait"){Sleep(30000);return 0;}
        if(argc==3 && std::wstring(argv[2])==L"fail")return 7;
        if(argc==3 && std::wstring(argv[2])==L"environment"){wchar_t value[128]{};GetEnvironmentVariableW(L"STARLIGHT_PROCESS_TEST",value,128);return std::wstring(value)==L"child-only value"?0:10;}
        if(argc!=5 || std::wstring(argv[2])!=L"space and \\\"quote" || std::wstring(argv[3])!=L"trailing\\" || std::wstring(argv[4])!=L"")return 9;
        std::cout<<"captured child output\n";return 0;
    }
    try{
        wchar_t path[32768];auto n=GetModuleFileNameW(nullptr,path,32768);require(n>0 && n<32768,"Executable location failed");
        auto root=fs::temp_directory_path()/(L"Starlight-process-check-"+std::to_wstring(GetCurrentProcessId()));require(!fs::exists(root),"Fixture exists");fs::create_directory(root);
        auto ok=run(path,{L"--child",L"space and \\\"quote",L"trailing\\",L""},root,root/L"success.log",5000);
        require(ok.exitCode==0 && !ok.timedOut,"Argument round trip failed");
        std::ifstream log(root/L"success.log");std::string line;std::getline(log,line);require(line=="captured child output","Output capture failed");log.close();
        auto failed=run(path,{L"--child",L"fail"},root,root/L"failure.log",5000);require(failed.exitCode==7 && !failed.timedOut,"Exit status lost");
        wchar_t prior[32768]{};auto priorLength=GetEnvironmentVariableW(L"STARLIGHT_PROCESS_TEST",prior,32768);
        auto isolated=run(path,{L"--child",L"environment"},root,root/L"environment.log",5000,{},{{L"STARLIGHT_PROCESS_TEST",L"child-only value"}});require(isolated.exitCode==0,"Child environment override failed");
        wchar_t after[32768]{};require(GetEnvironmentVariableW(L"STARLIGHT_PROCESS_TEST",after,32768)==priorLength && std::wstring(prior)==after,"Parent environment changed");
        auto timed=run(path,{L"--child",L"wait"},root,root/L"timeout.log",250);require(timed.timedOut && timed.exitCode!=STILL_ACTIVE,"Timeout containment failed");
        bool refused=false;try{run(path,{L"--child",L"fail"},root,root/L"success.log",5000);}catch(...){refused=true;}require(refused,"Existing log overwritten");
        for(auto name:{L"success.log",L"failure.log",L"timeout.log",L"environment.log"})fs::remove(root/name);fs::remove(root);
        std::cout<<"PASS: argument quoting, output capture, failure status, timeout, existing log protection\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
