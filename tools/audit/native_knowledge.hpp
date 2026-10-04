#pragma once
#include "native_inventory.hpp"
#include <regex>
namespace starlight::audit {
inline bool truth(const json& value){if(value.is_null())return false;if(value.is_boolean())return value.get<bool>();if(value.is_number())return value.get<double>()!=0;return !value.empty();}
inline std::string python_repr(const json& value){
    if(value.is_null())return "None";if(value.is_boolean())return value.get<bool>()?"True":"False";
    if(value.is_string()){auto text=value.get<std::string>();char quote=text.find('\'')!=std::string::npos && text.find('"')==std::string::npos?'"':'\'';std::string out(1,quote);for(char c:text){if(c==quote || c=='\\')out+='\\';if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else out+=c;}return out+quote;}
    if(value.is_array() || value.is_object()){std::string out=value.is_array()?"[":"{";bool first=true;for(auto it=value.begin();it!=value.end();++it){if(!first)out+=", ";first=false;if(value.is_object())out+=python_repr(json(it.key()))+": ";out+=python_repr(it.value());}return out+(value.is_array()?"]":"}");}return value.dump();
}
inline std::string python_text(const json& value){return value.is_string()?value.get<std::string>():python_repr(value);}
inline json objects_only(const json& value){json rows=json::array();if(value.is_array())for(const auto& row:value)if(row.is_object())rows.push_back(row);return rows;}
inline size_t passed_checks(const json& rows){size_t count=0;for(const auto& row:rows)if(row.value("pass",json())==true)++count;return count;}
inline json latest_validation_native(const json& input){
    json evidence=json::array();std::map<std::string,json> runs;std::regex cohort(R"(audit-runs/(run-[^/]+)/)");
    for(auto record:input){auto path=record.at("path").get<std::string>();if(path.ends_with(".progress.json") || path.ends_with(".status.json"))continue;record["originalPath"]=path;std::replace(path.begin(),path.end(),'\\','/');record["path"]=path;evidence.push_back(record);std::smatch match;if(std::regex_search(path,match,cohort)){auto name=match[1].str();if(!runs.contains(name))runs[name]=json::array();runs[name].push_back(record);}}
    json name=nullptr,entries=json::array(),summary=json::array(),ui=json::array();if(!runs.empty()){name=runs.rbegin()->first;entries=runs.rbegin()->second;}
    std::map<std::pair<std::string,std::string>,size_t> renders;std::set<std::string> used;
    for(const auto& record:entries){auto data=record.value("data",json::object());auto path=record.at("path").get<std::string>();used.insert(path);if(!data.is_object())continue;
        if(truth(record.value("error",json()))){summary.push_back("Unreadable evidence: "+path+".");continue;}
        if(path.find("/tier-")!=std::string::npos){auto arm=path.find("-new/")!=std::string::npos?"new":path.find("-old/")!=std::string::npos?"legacy":"unspecified";auto outcome=data.value("success",json())==true?"recorded success":python_text(data.value("error",json("unknown result")));++renders[{arm,outcome}];}
        else if(path.ends_with("native-session/session-results.json"))summary.push_back("Session checks: "+std::string(data.value("success",json())==true?"passed":"failed/unknown")+"; "+std::to_string(data.value("checks",json::array()).size())+" checks reached. "+python_text(data.value("error",json(""))));
        else if(path.ends_with("native-session/lifecycle-memory.json")){auto before=data.value("baseline",json::object()),after=data.value("final",json::object());summary.push_back("Scene lifecycle: "+python_text(data.value("cycles",json("unknown")))+" cycles; resource retirement="+python_text(data.value("resourcesRetired",json("not recorded")))+"; handles "+python_text(before.value("handles",data.value("initialHandles",json("?"))))+" → "+python_text(after.value("handles",data.value("finalHandles",json("?"))))+".");}
        else if(path.ends_with("native-classic-ui/result.json")){ui=objects_only(data.value("checks",json::array()));summary.push_back("Classic UI: "+std::to_string(passed_checks(ui))+"/"+std::to_string(ui.size())+" named checks passed in this saved run.");auto trace=data.value("window_creation_trace",json());if(!trace.is_null())summary.push_back("Helper-window trace: "+std::to_string(trace.value("events",json::array()).size())+" creation events; "+python_text(trace.value("hookedThreads",json("unknown")))+" hooked threads. No events does not establish ownership.");}
    }
    for(const auto& [key,count]:renders)summary.push_back("Renderer "+key.first+": "+std::to_string(count)+" result(s) — "+key.second+". Success alone does not prove comparable, unthrottled throughput.");
    summary.insert(summary.begin(),"Historical lifecycle/renderer cohort: "+python_text(name)+". Later UI and visible-render records below are separate workloads, not replacements for lifecycle evidence.");
    for(const auto& record:evidence){auto path=record.at("path").get<std::string>();auto data=record.value("data",json::object());if(!data.is_object())continue;
        if(path.find("editor-menu-comparison-")!=std::string::npos || path.find("/visible-editor/result.json")!=std::string::npos){auto checks=objects_only(data.value("checks",json::array()));if(!checks.empty() || data.value("pass",json())==false || truth(data.value("error",json()))){summary.push_back("Separate saved UI evidence "+path+": "+std::to_string(passed_checks(checks))+"/"+std::to_string(checks.size())+" checks passed; overall pass="+python_text(data.value("pass",json("not recorded")))+". Not proof of current-source equivalence or visual completeness.");if(truth(data.value("error",json())))summary.push_back("UI failure: "+python_text(data["error"]));used.insert(path);}}
        else if(path.find("/visible-presentation/render.json")!=std::string::npos){summary.push_back("Separate saved visible render evidence "+path+": success="+python_text(data.value("success",json("not recorded")))+". Inspect dimensions, foreground validity and workload before comparing performance.");used.insert(path);}
        else if(path.find("gate0-")!=std::string::npos && path.ends_with("/audit-verdict.json")){summary.push_back("Gate 0 verdict preserved at "+path+"; bounded stage passes do not close unresolved lifecycle/long-soak gaps.");used.insert(path);}
    }
    json declared=json::array();for(const auto& record:evidence){if(record.value("reportEvidenceRole",std::string())!="declared runtime input")continue;auto data=record.value("data",json::object());if(!data.is_object())continue;auto path=record.at("path").get<std::string>();if(data.contains("outcome") || data.contains("pass") || data.contains("success")){auto outcome=data.value("outcome",data.value("pass",data.value("success",json("not recorded"))));declared.push_back("Declared runtime evidence "+path+": outcome="+python_text(outcome)+". "+python_text(data.value("error",json(""))));used.insert(path);}}
    if(!declared.empty()){declared.insert(declared.begin(),"Runtime evidence explicitly supplied for this report (may be reused; source/build freshness is assessed separately):");for(const auto& row:summary)declared.push_back(row);summary=std::move(declared);}
    json paths=json::array();for(const auto& record:evidence)if(used.contains(record["path"].get<std::string>()))paths.push_back(record["originalPath"]);
    return {{"run",name},{"summary",summary},{"ui_checks",ui},{"paths",paths}};
}
inline json knowledge_model_native(const json& input,const json& rules){
    if(rules.at("format")!="starlight-knowledge-rules-v1")throw std::runtime_error("Unknown knowledge rules");
    const auto& capabilities=input.at("capabilities");const auto& definitions=rules.at("features");const auto& evidence=input.at("evidence");json features=json::array(),unmapped=json::array();std::set<std::string> covered;
    for(const auto& capability:capabilities){auto title=capability.at("title").get<std::string>();bool mapped=definitions.contains(title);auto details=mapped?definitions.at(title):json::array({capability.at("interpretation"),"Not yet described in the curated knowledge rules.","Needs knowledge review.","Unmapped feature"});bool supported=!truth(capability.value("missing",json())) && truth(capability.value("evidence",json()));
        features.push_back({{"name",title},{"purpose",details[0]},{"workflow",details[1]},{"connections",details[2]},{"implementation_status",supported?details[3]:json("Unknown — required source support missing")},{"source_supported",supported},{"limits",capability.at("limits")},{"evidence",capability.value("evidence",json::array())},{"missing",capability.value("missing",json::array())},{"interpretation_status","Curated description corroborated by lexical source evidence; not automatic semantic proof"}});
        for(const auto& record:capability.value("evidence",json::array()))covered.insert(record.at("file").get<std::string>());if(!mapped)unmapped.push_back(title);
    }
    auto validation=latest_validation_native(evidence);auto interpreted=validation["paths"].get<std::set<std::string>>();json gaps=json::array(),uncited=json::array();
    for(const auto& record:evidence)if(truth(record.value("error",json())) || !interpreted.contains(record.at("path").get<std::string>()))gaps.push_back({{"path",record["path"]},{"reason",record.value("error",json("No summary rule; manual interpretation required"))}});
    for(const auto& record:input.at("index"))if(!covered.contains(record.at("path").get<std::string>()))uncited.push_back(record["path"]);
    auto catalogue=input.at("contract").value("feature_catalog",json());if(!truth(catalogue))catalogue=json::array();
    return {{"evidence_review_required",gaps},{"version",rules.at("version")},{"provenance",input.at("provenance")},{"features",features},{"validation",validation},{"catalogue",catalogue},{"source_files_without_direct_feature_citation",uncited},{"unmapped_capabilities",unmapped}};
}
}
