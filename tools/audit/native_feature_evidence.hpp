#pragma once
#include "native_knowledge.hpp"
namespace starlight::audit {
inline std::string feature_id_native(std::string title){std::transform(title.begin(),title.end(),title.begin(),[](unsigned char c){return char(std::tolower(c));});title=std::regex_replace(title,std::regex("[^a-z0-9]+"),"-");auto start=title.find_first_not_of('-'),end=title.find_last_not_of('-');return start==std::string::npos?"":title.substr(start,end-start+1);}
inline json connect_features_native(const json& model,const json& index,const json& evidence,const json& inventory){
    std::map<std::string,json> hashes;for(const auto& source:inventory)if(source.value("stable",json())==true)hashes[source.at("path").get<std::string>()]=source.value("sha256",json());for(const auto& source:index)hashes[source.at("path").get<std::string>()]=source.value("sha256",json());
    json features=json::array(),warnings=json::array();std::map<std::string,size_t> positions;
    for(const auto& feature:model.at("features")){auto key=feature_id_native(feature.at("name").get<std::string>());std::set<std::string> sources;for(const auto& entry:feature.at("evidence"))sources.insert(entry.at("file").get<std::string>());
        json row={{"id",key},{"name",feature.at("name")},{"implementation",feature.at("implementation_status")},{"sources",sources},{"limits",feature.at("limits")},{"evidence",json::array()},{"issues",json::array()},{"verification","No explicitly linked runtime evidence"}};
        if(positions.contains(key))features[positions[key]]=row;else{positions[key]=features.size();features.push_back(row);}
    }
    std::regex hashPattern("[0-9a-fA-F]{64}");
    for(const auto& entry:evidence){auto data=entry.value("data",json());auto path=entry.at("path");if(!data.is_object() || data.value("schema",json())!="starlight.feature-evidence.v1"){warnings.push_back({{"path",path},{"reason",entry.value("error",json("Legacy/unrecognized schema; no inferred feature verdict"))}});continue;}
        auto refs=data.value("features",json());bool valid=refs.is_array() && !refs.empty();if(valid)for(const auto& ref:refs)if(!ref.is_string())valid=false;if(!valid){warnings.push_back({{"path",path},{"reason","Missing or invalid feature IDs"}});continue;}
        auto tested=data.value("sourceHashes",json::object()),build=data.value("build",json::object());bool validBuild=build.is_object() && build.contains("sha256") && build["sha256"].is_string() && std::regex_match(build["sha256"].get<std::string>(),hashPattern);
        for(const auto& ref:refs){auto key=ref.get<std::string>();if(!positions.contains(key)){warnings.push_back({{"path",path},{"reason","Unknown feature ID: "+key}});continue;}
            auto& feature=features[positions[key]];std::string freshness="indexed source matches; transitive build inputs not established";
            if(!tested.is_object() || tested.empty())freshness="unknown: no tested source hashes";
            else{bool stale=false,partial=false;for(auto it=tested.begin();it!=tested.end();++it)if(!truth(it.value()) || !hashes.contains(it.key()) || hashes.at(it.key())!=it.value())stale=true;
                for(const auto& source:feature["sources"])if(!tested.contains(source.get<std::string>()))partial=true;
                if(stale)freshness="stale or missing source";else if(partial)freshness="partial source coverage";
            }
            feature["evidence"].push_back({{"path",path},{"sha256",entry.value("sha256",json())},{"outcome",data.value("outcome",json("unknown"))},{"workload",data.value("workload",json("unspecified"))},{"build",build.is_object()?build:json::object()},{"buildIdentityRecorded",validBuild},{"freshness",freshness}});
            auto issues=data.value("issues",json::array());if(issues.is_array())for(const auto& issue:issues)if(issue.is_object())feature["issues"].push_back({{"id",issue.value("id",json())},{"status",issue.value("status",json("unknown"))},{"summary",issue.value("summary",json(""))},{"evidence",path}});
            feature["verification"]="Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues";
        }
    }
    return {{"schema","starlight.feature-status.v1"},{"features",features},{"reviewRequired",warnings},{"limits","Declared records are not trusted proof. No last-write-wins issue closure, whole-feature completion, or binary/source equivalence is inferred."}};
}
inline std::string feature_briefing_native(const json& graph){
    std::vector<std::string> lines={"# Generated feature and evidence briefing","",graph.at("limits").get<std::string>(),"","This report is generated from the current feature model and discovered evidence. Unlinked legacy tests remain review work, not silently converted passes.",""};
    for(const auto& f:graph.at("features")){std::string sources;for(const auto& source:f["sources"]){if(!sources.empty())sources+=", ";sources+=source.get<std::string>();}
        for(auto line:std::vector<std::string>{"## "+f["name"].get<std::string>(),"","**Feature ID:** `"+f["id"].get<std::string>()+"`","","**Implementation:** "+f["implementation"].get<std::string>(),"","**Verification:** "+f["verification"].get<std::string>(),"","**Limits:** "+f["limits"].get<std::string>(),"","**Source:** "+sources,""})lines.push_back(line);
        for(const auto& e:f["evidence"])lines.push_back("- "+python_text(e["path"])+": "+python_text(e["outcome"])+"; "+python_text(e["freshness"])+"; build identity recorded="+python_text(e["buildIdentityRecorded"])+"; workload="+python_text(e["workload"]));
        for(const auto& issue:f["issues"])lines.push_back("- Issue "+python_text(issue["id"])+": "+python_text(issue["status"])+" — "+python_text(issue["summary"])+" ("+python_text(issue["evidence"])+")");
    }
    lines.push_back("");lines.push_back("## Evidence requiring review");lines.push_back("");for(const auto& warning:graph.at("reviewRequired"))lines.push_back("- "+python_text(warning["path"])+": "+python_text(warning["reason"]));
    std::string text;for(const auto& line:lines)text+=line+"\n";return text;
}
}
