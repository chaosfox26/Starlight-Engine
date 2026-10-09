#pragma once
#include "native_knowledge.hpp"
namespace starlight::audit {
inline fs::path audit_display_path(const fs::path& path){auto text=path.wstring();if(text.starts_with(L"\\\\?\\UNC\\"))return fs::path(L"\\\\"+text.substr(8));if(text.starts_with(L"\\\\?\\"))return fs::path(text.substr(4));return path;}

inline std::vector<std::string> audit_source_paths(const fs::path& root){
 std::set<std::string> paths;const std::set<std::string> extensions={".cpp",".hpp",".h",".inl",".hlsl",".py",".c",".inc",".cmake",".rc",".manifest",".star"};
 for(auto folder:{"editor/cpp","editor/native","deep/engine","deep/Physics","deep/src","deep/projects","deep/third_party/JoltPhysics/Jolt","deep/third_party/JoltPhysics/Build","cmake","tools"}){auto base=root/folder;if(!fs::is_directory(base))continue;for(auto it=fs::recursive_directory_iterator(long_path(base));it!=fs::recursive_directory_iterator();++it){auto attrs=GetFileAttributesW(long_path(it->path()).c_str());if(attrs==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Cannot inspect source path");if(attrs&FILE_ATTRIBUTE_REPARSE_POINT){if(it->is_directory())it.disable_recursion_pending();continue;}if(it->is_regular_file()&&(extensions.contains(it->path().extension().string())||it->path().filename()==L"CMakeLists.txt"))paths.insert(utf8(it->path().lexically_relative(long_path(root)).generic_wstring()));}}
 for(auto file:{"CMakeLists.txt","CMakePresets.json","Build-Native.py","tools/build/renderer-transforms.json","tools/toolchain/toolchain.json"})if(fs::exists(root/file))paths.insert(file);return {paths.begin(),paths.end()};
}
inline json audit_source_hashes(const fs::path& root,const std::vector<std::string>& sources){json hashes=json::object();for(const auto& name:sources){auto relative=fs::u8path(name);if(relative.is_absolute())throw std::runtime_error("Source evidence must use relative paths");for(const auto& part:relative)if(part==L"..")throw std::runtime_error("Source evidence cannot escape its root");hashes[name]=digest(root/relative).first;}return hashes;}
inline json capture_feature_record(const fs::path& executable,const fs::path& root,const std::vector<std::string>& sources,const json& features,const std::string& outcome,const json& workload,const json& issues=json::array()){
 return {{"schema","starlight.feature-evidence.v1"},{"features",features},{"outcome",outcome},{"workload",workload},{"build",{{"path",utf8(executable.wstring())},{"sha256",digest(executable).first}}},{"sourceHashes",audit_source_hashes(root,sources)},{"issues",issues}};
}
inline json collect_audit_findings(const fs::path& directory,bool includeRetained=false){
 json findings=json::array();std::vector<fs::path> paths;const std::set<std::wstring> names={L"result.json",L"session-results.json",L"audit-verdict.json",L"render-matrix.json",L"native-pe.json"};
 if(!fs::is_directory(directory))return findings;
 for(auto it=fs::recursive_directory_iterator(long_path(directory));it!=fs::recursive_directory_iterator();++it){auto attrs=GetFileAttributesW(long_path(it->path()).c_str());if(attrs==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Cannot inspect audit evidence path");if(attrs&FILE_ATTRIBUTE_REPARSE_POINT){if(it->is_directory())it.disable_recursion_pending();continue;}if(it->is_regular_file()&&names.contains(it->path().filename().wstring()))paths.push_back(audit_display_path(it->path()));}
 std::sort(paths.begin(),paths.end());for(const auto& path:paths){auto add=[&](const std::string& detail){findings.push_back({{"file",utf8(path.wstring())},{"detail",detail}});};try{if(fs::file_size(long_path(path))>2*1024*1024){add("Too large to read (over 2 MiB); its result was not checked");continue;}auto data=json::parse(read_file(path));auto rows=data.is_array()?data:json::array({data});for(const auto& row:rows){if(!row.is_object())continue;if(truth(row.value("error",json()))){auto detail=python_text(row.at("error"));auto state=row.value("denoiserState",json::object());if(state.is_object() && state.contains("status") && state.at("status").is_string())detail+=" — "+state.at("status").get<std::string>();add(detail);}auto checks=row.value("checks",json::array());if(checks.is_array())for(const auto& check:checks)if(check.is_object()&&(check.value("pass",json())==false||check.value("outcome",json())=="failed"))add(python_text(check.value("name",json("Unnamed check"))));auto evidenceChecks=row.value("evidenceChecks",json::array());
if(evidenceChecks.is_array())for(const auto& evidence:evidenceChecks){
 if(!evidence.is_object() || evidence.value("accepted",json())!=false)continue;
 const auto errors=evidence.value("errors",json::array());
 if(errors.is_array())for(const auto& error:errors)if(error.is_string())
  add(python_text(evidence.value("name",json("Graphics check")))+": "+error.get<std::string>());
}
auto violations=row.value("violations",json::array());if(violations.is_array())for(const auto& violation:violations)add(python_text(violation));if(row.value("passed",json())==false)add(python_text(row.value("case",row.value("path",json("Failed result")))));}}
 catch(const std::exception& e){add("Unreadable result: "+std::string(e.what()));}}
 if(includeRetained&&fs::is_regular_file(directory/"audit-findings.json")){
  auto saved=json::parse(read_file(directory/"audit-findings.json"));if(!saved.is_array())throw std::runtime_error("Retained findings must be an array");
  for(const auto& row:saved){if(!row.is_object()||!row.contains("file")||!row.at("file").is_string()||!row.contains("detail")||!row.at("detail").is_string())throw std::runtime_error("Retained finding is malformed");if(std::find(findings.begin(),findings.end(),row)==findings.end())findings.push_back(row);}
 }
 return findings;
}
}
