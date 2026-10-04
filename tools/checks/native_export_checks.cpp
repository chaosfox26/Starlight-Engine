#include "platform.hpp"
#include "../audit/native_process.hpp"
#include <iostream>
using namespace starlight::editor;
static json load(const fs::path& p){return json::parse(read_file(p));}
static json hashes(const fs::path& folder){json result=json::object();for(const auto& item:fs::recursive_directory_iterator(folder))if(item.is_regular_file())result[item.path().lexically_relative(folder).generic_string()]=hash_file(item.path());return result;}
int wmain(int argc,wchar_t** argv){
    fs::path runtime,out;json checks=json::array(),binaries=json::object(),report;
    try{
        fs::path base;
        for(int i=1;i<argc;i+=2){if(i+1>=argc)throw std::runtime_error("Missing option value");std::wstring key=argv[i];if(key==L"--runtime")runtime=fs::absolute(argv[i+1]);else if(key==L"--output")base=fs::absolute(argv[i+1]);else throw std::runtime_error("Unknown option");}
        if(runtime.empty() || base.empty())throw std::runtime_error("Runtime and output required");
        out=base/(L"run-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        if(!fs::create_directories(out))throw std::runtime_error("Fresh output required");
        auto check=[&](bool ok,const std::string& name){if(!ok)throw std::runtime_error(name);checks.push_back(name);};
        unsigned commandIndex=0;
        auto run=[&](const fs::path& program,const std::vector<std::wstring>& args,DWORD expected=0){
            auto result=starlight::audit::run(program,args,out,out/(L"command-"+std::to_wstring(commandIndex++)+L".log"),40000);
            if(result.timedOut || result.exitCode!=expected)throw std::runtime_error("Export check subprocess failed: "+utf8(program.filename().wstring()));
        };
        for(auto name:{L"Starlight.NativeCommands.exe",L"Starlight.NativePlayer.exe",L"Starlight.Viewport.dll",L"Starlight.Physics.dll"})binaries[utf8(name)]=hash_file(runtime/name);
        auto cli=runtime/L"Starlight.NativeCommands.exe",scene=out/L"starter.starscene",package=out/L"game";
        run(cli,{L"--new-adventure",scene.wstring()});auto original=hash_file(scene);
        run(cli,{L"--new-adventure",scene.wstring()},1);check(hash_file(scene)==original,"Existing starter scene preserved");
        auto fixture=load(scene);json door;
        for(const auto& c:fixture.at("Characters"))if(c.value("GameplayRole","")=="door"){door=c;break;}
        if(door.is_null())throw std::runtime_error("Starter door missing");
        auto game=fixture.at("Game");fixture["BloomQuality"]=3;fixture["BloomStrength"]=.23;
        fixture["Rules"]=json::array({{{"Id","export-rule-check"},{"Name","Exported rule"},{"Event","enter-area"},{"Subject",""},{"X",game.at("SpawnX")},{"Y",game.at("SpawnY")},{"Z",game.at("SpawnZ")},{"Radius",1},{"Once",true},{"Enabled",true},{"Conditions",json::array()},{"Actions",json::array({{{"Type","complete-objective"},{"Target","Export rule"}},{{"Type","open-door"},{"Target",door.at("Id")}}})}}});
        fixture["Quests"]=json::array({{{"Id","export-quest"},{"Title","Restore the outpost"},{"Description","Enter the marked area to open the door."},{"RuleIds",json::array({"export-rule-check"})},{"StepSizes",json::array({1})},{"Tracked",true}}});
        json links=json::array();int index=0;
        for(auto pair:{std::pair{"door","object-opened"},std::pair{"breakable","object-broken"},std::pair{"key","item-collected"}}){
            std::string role=pair.first,id="preset-object-"+role,ruleId="preset-rule-"+role;auto obj=door;
            obj["Id"]=id;obj["Name"]=role+" block";obj["X"]=8+index++;obj["GameplayRole"]=role;obj["GameplayKey"]=role=="key"?id:"";fixture["Characters"].push_back(obj);
            auto rule=fixture["Rules"][0];rule["Id"]=ruleId;rule["Name"]=role+" objective";rule["Event"]=pair.second;rule["Subject"]=id;rule["Actions"]=json::array({{{"Type","complete-objective"},{"Target",ruleId}}});fixture["Rules"].push_back(rule);links.push_back(ruleId);
        }
        fixture["Quests"].push_back({{"Id","preset-quest"},{"Title","Object interactions"},{"Description","Use your authored objects."},{"RuleIds",links},{"StepSizes",json::array({3})},{"Tracked",false}});
        atomic_write(scene,fixture.dump(2));run(cli,{L"--export-game",scene.wstring(),package.wstring(),runtime.wstring()});
        check(fs::is_regular_file(package/L"content/scene.starscene"),"Starter scene packaged");auto packaged=load(package/L"content/scene.starscene");
        check(packaged.at("Quests")==fixture.at("Quests"),"Export preserves quest title description and stable objective links");
        check(packaged.at("Rules")==fixture.at("Rules"),"Export preserves saved events conditions actions and target identities");
        check(load(package/L"gameplay-check.json").at("findings")==json::array(),"Starter export includes a clean basic gameplay setup report");
        auto manifest=load(package/L"game-manifest.json");
        for(auto name:{"Starlight-LICENSE.txt","THIRD-PARTY-NOTICES.md","Jolt-MIT.txt","cgltf-MIT.txt","jsmn-MIT.txt","nlohmann-json-MIT.txt","json-embedded-credits.txt"}){
            auto relative=std::string("licenses/")+name;auto path=package/fs::u8path(relative);
            check(fs::is_regular_file(path) && read_file(path)==read_file(runtime/fs::u8path(relative)),std::string("Export preserves ")+name);
            bool found=false;for(const auto& entry:manifest.at("files"))if(entry.at("path").get<std::string>()==relative)found=_stricmp(entry.at("sha256").get<std::string>().c_str(),hash_file(path).c_str())==0;
            check(found,std::string("Export manifest hashes ")+name);
        }
        auto player=package/L"Starlight.NativePlayer.exe";run(player,{L"--validate"});checks.push_back("Packaged content validates");
        if(fs::is_regular_file(runtime/L"nvngx_dlssd.dll")){
            check(hash_file(package/L"nvngx_dlssd.dll")==hash_file(runtime/L"nvngx_dlssd.dll"),"Export preserves ray reconstruction runtime");
            check(read_file(package/L"licenses/NVIDIA-RTX-SDK.txt")==read_file(runtime/L"licenses/NVIDIA-RTX-SDK.txt"),"Export preserves ray reconstruction license");
        }
        auto before=hashes(package);run(cli,{L"--export-game",scene.wstring(),package.wstring(),runtime.wstring()},1);check(before==hashes(package),"Existing package refused without modification");
        if(fs::is_regular_file(runtime/L"sl.interposer.dll"))for(auto name:{"sl.interposer.dll","sl.common.dll","sl.dlss_d.dll","NvLowLatencyVk.dll","licenses/NVIDIA-Streamline-MIT.txt","licenses/NVIDIA-Streamline-third-party.md","licenses/NVIDIA-Reflex.txt"}){
            auto file=fs::u8path(name);check(hash_file(package/file)==hash_file(runtime/file),std::string("Export preserves ")+name);
            bool found=false;for(const auto& entry:manifest.at("files"))if(fs::u8path(entry.at("path").get<std::string>())==file)found=_stricmp(entry.at("sha256").get<std::string>().c_str(),hash_file(package/file).c_str())==0;
            check(found,std::string("Export manifest hashes ")+name);
        }
        auto missing=out/L"missing-runtime";fs::create_directory(missing);run(cli,{L"--export-game",scene.wstring(),(out/L"must-not-exist").wstring(),missing.wstring()},1);check(!fs::exists(out/L"must-not-exist"),"Missing runtime rejected before creating output");
        auto smoke=out/L"render.json";run(player,{L"--smoke",smoke.wstring()});auto rendered=load(smoke);
        check(rendered.value("pass",false) && rendered.value("frames",0)>=60 && rendered.value("error",std::string()).empty(),"Packaged starter renders 60 frames without error");
        check(rendered.value("bloomLevels",0)==6&&rendered.value("hdrPostAllocationBytes",uint64_t(0))>0,"Exported saved glow quality reaches the actual shared renderer");
        check(rendered.value("questCount",0)==2 && rendered.value("firstQuestComplete",false),"Standalone quest tracker reads real runtime completion");
        check(rendered.at("completedObjectives")==json::array({"Export rule"}) && rendered.at("remainingCharacters").get<size_t>()==fixture.at("Characters").size()-1,"Packaged standalone executes saved area objective and linked-door actions");
        bool stable=true;for(auto it=binaries.begin();it!=binaries.end();++it){stable=stable && hash_file(runtime/fs::u8path(it.key()))==it.value().get<std::string>();if(it.key()!="Starlight.NativeCommands.exe")stable=stable && hash_file(package/fs::u8path(it.key()))==it.value().get<std::string>();}
        check(stable,"Tested binaries match the package and stayed unchanged during the check");report={{"success",true}};
    }catch(const std::exception& e){report={{"success",false},{"error",e.what()}};}
    report["checks"]=checks;report["binaries"]=binaries;report["scope"]="Fresh starter export, content validation, rejection safety and hidden rendering; not human input, visible polish or clean-machine certification";
    if(!out.empty())atomic_write(out/L"result.json",report.dump(2));std::cout<<report.dump(2)<<'\n';return report.at("success").get<bool>()?0:1;
}

