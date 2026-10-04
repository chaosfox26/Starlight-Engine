#pragma once
#include "native_audit_records.hpp"
#include "native_process.hpp"
namespace starlight::audit {
inline fs::path runtime_child(const fs::path& root,const std::string& relative){
 auto name=fs::u8path(relative);if(name.is_absolute()||name.empty())throw std::runtime_error("Runtime entry must be a relative path");
 for(const auto& part:name)if(part==L"..")throw std::runtime_error("Runtime entry escapes its root");
 auto path=root/name;for(auto p=path;p!=root;p=p.parent_path()){auto attrs=GetFileAttributesW(long_path(p).c_str());if(attrs!=INVALID_FILE_ATTRIBUTES&&(attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Runtime entry is redirected");}return path.lexically_normal();
}
inline std::string runtime_substitute(std::string value,const std::map<std::string,fs::path>& variables){for(const auto& [token,path]:variables){auto text=utf8(path.wstring());size_t pos=0;while((pos=value.find(token,pos))!=std::string::npos){value.replace(pos,token.size(),text);pos+=text.size();}}return value;}
inline void runtime_saved_evidence(const fs::path& folder,json& evidence){
 std::vector<fs::path> paths;for(auto it=fs::recursive_directory_iterator(long_path(folder));it!=fs::recursive_directory_iterator();++it){auto attrs=GetFileAttributesW(long_path(it->path()).c_str());if(attrs==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Cannot inspect runtime result");if(attrs&FILE_ATTRIBUTE_REPARSE_POINT){if(it->is_directory())it.disable_recursion_pending();continue;}if(it->is_regular_file()&&it->path().extension()==L".json")paths.push_back(audit_display_path(it->path()));}
 std::sort(paths.begin(),paths.end());for(const auto& path:paths){json item={{"path",utf8(path.wstring())}};try{if(fs::file_size(long_path(path))>2*1024*1024)throw std::runtime_error("Too large to interpret");item["sha256"]=digest(path).first;item["data"]=json::parse(read_file(path));}catch(const std::exception& e){item["error"]=e.what();}evidence.push_back(item);}
}
inline bool run_runtime_plan(const fs::path& root,const fs::path& runtime,const fs::path& out,const json& plan,json& records,json& evidence){
 if(plan.at("format")!="starlight-runtime-checks-v1"||plan.at("profile")!="prebuilt-runtime"||!plan.at("stages").is_array()||plan.at("stages").empty())throw std::runtime_error("Invalid prebuilt runtime plan");
 std::set<std::string> names;for(const auto& stage:plan.at("stages")){auto name=stage.at("name").get<std::string>();if(name.empty()||name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos||!names.insert(name).second)throw std::runtime_error("Invalid or duplicate stage name");auto timeout=stage.at("timeoutSeconds").get<unsigned>();if(!timeout||timeout>3600)throw std::runtime_error("Invalid runtime deadline");runtime_child(runtime,stage.at("executable").get<std::string>());}
 auto sources=audit_source_paths(root);bool executed=false;size_t number=0;
 for(const auto& stage:plan.at("stages")){
  auto name=stage.at("name").get<std::string>();auto folder=out/fs::u8path(name);if(fs::exists(folder))throw std::runtime_error("Runtime output already exists");if(!stage.value("createsOutput",false)&&!fs::create_directory(folder))throw std::runtime_error("Cannot create runtime output");
  if(stage.value("delegated",false)){records.push_back({{"name",name},{"outcome","delegated"},{"reason","Executed by bounded render/lifecycle stage"}});continue;}
  json result,before;fs::path packet;std::string packetHash;
  auto exe=runtime_child(runtime,stage.at("executable").get<std::string>()),identity=stage.contains("identity")?runtime_child(runtime,stage.at("identity").get<std::string>()):exe;
  try{
   if(stage.contains("packet")){packet=runtime_child(root,stage.at("packet").get<std::string>());if(!fs::is_regular_file(packet))result={{"outcome","not-run"},{"reason","Required diagnostic scene packet is missing"},{"packet",utf8(packet.wstring())}};else packetHash=digest(packet).first;}
   if(result.is_null()&&!fs::is_regular_file(exe))result={{"outcome","unavailable"},{"error","Required packaged check executable is missing"},{"executable",utf8(exe.wstring())}};
   if(result.is_null()){
    before=capture_feature_record(identity,root,sources,stage.at("features"),"unknown",name);std::vector<std::wstring> args;json command=json::array({utf8(exe.wstring())});
    for(const auto& arg:stage.at("arguments")){auto value=runtime_substitute(arg.get<std::string>(),{{"{root}",root},{"{runtime}",runtime},{"{build}",runtime.parent_path()},{"{output}",folder}});args.push_back(wide(value));command.push_back(value);}
    auto timeout=stage.at("timeoutSeconds").get<unsigned>();int percent=5+int(55*number/plan.at("stages").size());executed=true;
    auto child=run(exe,args,root,out/fs::u8path(name+".log"),timeout*1000,[&](double elapsed){atomic_write(out/"progress.txt",std::to_string(percent)+"\n"+name+" — "+std::to_string(int(elapsed))+"s; keep visible tests in front.");});
    result={{"outcome",child.timedOut?"timeout":child.exitCode?"fail":"pass"},{"command",command},{"seconds",child.elapsedSeconds}};
    if(child.timedOut)result["timeoutSeconds"]=timeout;else result["exitCode"]=child.exitCode;
    auto after=capture_feature_record(identity,root,sources,stage.at("features"),"unknown",name);
    if(before.at("sourceHashes")!=after.at("sourceHashes")||before.at("build")!=after.at("build")||(!packet.empty()&&digest(packet).first!=packetHash))result["outcome"]="invalid: inputs changed";
    before["outcome"]=result.at("outcome");before["sourceBuildRelationship"]="Prebuilt runtime check; source compilation and reproducible provenance not established";
    if(!packet.empty()){result["packet"]=utf8(packet.wstring());result["packetSha256"]=packetHash;before["workload"]={{"name",name},{"packet",utf8(packet.wstring())},{"sha256",packetHash}};}
   }
  }catch(const std::exception& e){result={{"outcome","fail"},{"error",e.what()}};if(!before.is_null())before["outcome"]="invalid: evidence capture or execution failed";}
  if(!before.is_null()){auto path=out/fs::u8path(name+".feature-evidence.json");atomic_write(path,before.dump(2));evidence.push_back({{"path",utf8(path.wstring())},{"data",before}});}
  try{if(fs::is_directory(folder))runtime_saved_evidence(folder,evidence);}catch(const std::exception& e){result["testOutcome"]=result.value("outcome",json("unknown"));result["outcome"]="fail";result["error"]="Evidence collection failed: "+std::string(e.what());}
  result["name"]=name;records.push_back(result);++number;
  if(name=="native-render-lifecycle-visible"){
   std::string outcome="not-run";try{auto verdict=json::parse(read_file(folder/"audit-verdict.json"));for(const auto& check:verdict.value("checks",json::array()))if(check.value("name",std::string())=="lifecycle-traced")outcome=check.value("outcome",std::string())=="bounded-check-passed"?"pass":"fail";}catch(...){}
   for(auto& row:records)if(row.at("outcome")=="delegated"){row["outcome"]=outcome;row["evidence"]=utf8((folder/"lifecycle-traced").wstring());}
  }
  atomic_write(out/"stages.partial.json",records.dump(2));
 }
 for(auto& row:records)if(row.at("outcome")=="delegated"){row["outcome"]="not-run";row["reason"]="Delegated lifecycle check did not execute";}
 atomic_write(out/"stages.partial.json",records.dump(2));return executed;
}
}
