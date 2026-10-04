#include "../audit/native_cleanup.hpp"
#include "../audit/native_result_summary.hpp"
#include <fstream>
#include <iostream>
using namespace starlight::audit;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void put(const fs::path& p,std::string_view value){fs::create_directories(p.parent_path());std::ofstream stream(p,std::ios::binary);stream<<value;stream.close();require(bool(stream),"Fixture write failed");}
int main(){try{
    auto root=fs::temp_directory_path()/(L"starlight-cleanup-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    require(fs::create_directory(root),"Fixture already exists");auto build=root/"out"/"fixture";
    put(build/"CMakeCache.txt","CMAKE_HOME_DIRECTORY:INTERNAL="+utf8(root.generic_wstring())+"\n");
    put(build/"a.obj","generated");put(build/"b.pch","generated");put(build/"engine.exe","keep");put(build/"symbols.pdb","keep");
    auto plan=cleanup_plan(root,build,"build");require(plan.size()==2,"Build plan scope incorrect");
    put(build/"b.pch","changed by another process");
    auto result=apply_cleanup(root,build,plan,build/"cleanup.json");
    require(!fs::exists(build/"a.obj") && fs::exists(build/"b.pch") && fs::exists(build/"engine.exe") && fs::exists(build/"symbols.pdb"),"Build cleanup lost protected file");
    require(result[0]["status"]=="removed" && result[1]["status"]=="retained","Changed file not reported as retained");
    put(build/"CMakeCache.txt","CMAKE_HOME_DIRECTORY:INTERNAL=C:/wrong-root\n");bool rejected=false;try{cleanup_plan(root,build,"build");}catch(...){rejected=true;}require(rejected,"Wrong source tree accepted");
    auto report=root/"analysis"/"run";put(report/"audit-summary.json","{}");put(report/"readme.md","keep");put(report/"user.bin","keep");put(report/"test"/"capture.bmp","discard");put(report/"details.json","discard");
    plan=cleanup_plan(root,report,"report",{"user.bin"});require(plan.size()==2,"Report plan scope incorrect");
    result=apply_cleanup(root,report,plan,report/"summary-retention.json");
    require(fs::exists(report/"user.bin") && fs::exists(report/"audit-summary.json") && fs::exists(report/"readme.md") && !fs::exists(report/"details.json") && !fs::exists(report/"test"/"capture.bmp"),"Report cleanup scope incorrect");
    rejected=false;try{apply_cleanup(root,report,json::array(),report/"summary-retention.json");}catch(...){rejected=true;}require(rejected,"Existing cleanup journal overwritten");
    rejected=false;try{cleanup_plan(root,root/"analysis","report");}catch(...){rejected=true;}require(rejected,"Analysis root accepted");
    auto escaped=json::array({{{"path","../../outside.txt"},{"bytes",4},{"sha256","irrelevant"}}});put(root/"outside.txt","keep");
    result=apply_cleanup(root,report,escaped,report/"escaped-retention.json");require(result[0]["status"]=="retained" && fs::exists(root/"outside.txt"),"Escaped path removed");
    put(report/"locked.bmp","busy");plan=cleanup_plan(root,report,"report",{"user.bin"});
    {starlight::editor::Handle lock(CreateFileW((report/"locked.bmp").c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr));require(bool(lock),"Cannot create lock fixture");
    result=apply_cleanup(root,report,plan,report/"locked-retention.json");require(fs::exists(report/"locked.bmp"),"Locked file removed");}
    auto now=fs::file_time_type::clock::now();
    for(int i=0;i<7;++i){auto run=root/"analysis"/("retention-"+std::to_string(i));put(run/"audit-summary.json",json{{"root",utf8(root.generic_wstring())},{"allRequestedStagesPassed",i!=5}}.dump());put(run/"progress.txt",i==6?"80\nRunning":"100\nDone");put(run/"capture.bmp","image");put(run/"summary.md","keep");fs::last_write_time(run/"audit-summary.json",now-std::chrono::hours(i));}
    plan=capture_retention_plan(root,root/"analysis/retention-4");require(plan.size()==1 && plan[0]["path"]=="retention-3/capture.bmp","Capture retention lost current/recent/failed/incomplete protection");
    result=apply_cleanup(root,root/"analysis",plan,report/"captures.json");require(!fs::exists(root/"analysis/retention-3/capture.bmp") && fs::exists(root/"analysis/retention-5/capture.bmp"),"Capture retention execution incorrect");
    put(report/"empty/deep/file.bmp","x");fs::remove(report/"empty/deep/file.bmp");remove_empty_report_directories(root,report);require(!fs::exists(report/"empty"),"Empty report directories remain");
    require(final_status(json{{"mode","features"}},json::array()).find("No new runtime tests")!=std::string::npos,"Historical report reported fresh tests");
    require(final_status(json{{"runtimeExecuted",true},{"stages",json::array({{{"outcome","pass"}}})}},json::array())=="All 1 test stages successful.","Success summary incorrect");
    fs::remove_all(root);std::cout<<"PASS: scoped cleanup, changed/locked/escaped files, journal protection, capture retention, empty directories, final summaries\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"; fixtures retained\n";return 1;}}
