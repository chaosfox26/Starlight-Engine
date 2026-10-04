#pragma once
#include "native_knowledge.hpp"
#include <chrono>
namespace starlight::audit {
inline std::string utc_now(){SYSTEMTIME now{};GetSystemTime(&now);char text[40];sprintf_s(text,"%04u-%02u-%02uT%02u:%02u:%02u.%03u000+00:00",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds);return text;}
inline std::chrono::sys_days iso_day(const std::string& text){int y,m,d;char tail;int count=sscanf_s(text.c_str(),"%d-%d-%d%c",&y,&m,&d,&tail,1u);if(count!=3)throw std::runtime_error("Invalid comparison date");auto day=std::chrono::year(y)/unsigned(m)/unsigned(d);if(!day.ok())throw std::runtime_error("Invalid comparison date");return std::chrono::sys_days(day);}
inline std::string python_json(const json& value){auto compact=value.dump(-1,' ',true);std::string text;bool quoted=false,escaped=false;for(char c:compact){text+=c;if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;}else if(c=='"')quoted=true;else if(c==',' || c==':')text+=' ';}return text;}
inline json comparison_model_native(const json& model,const json& graph,const json& rules,const std::string& today,const std::string& generated){
    if(rules.at("format")!="starlight-comparison-rules-v1")throw std::runtime_error("Unknown comparison rules");auto reviewed=rules.at("reviewed").get<std::string>();auto age=(iso_day(today)-iso_day(reviewed)).count();std::string freshness=age<0 || age>90?"review required":"within 90-day review window; not rechecked online this run";
    std::map<std::string,json> byName,linked;for(const auto& feature:model.value("features",json::array()))byName[feature.at("name").get<std::string>()]=feature;for(const auto& feature:graph.value("features",json::array()))linked[feature.at("name").get<std::string>()]=feature;
    std::set<std::string> mapped;json rows=json::array();
    for(const auto& row:rules.at("rows")){json starlight=json::array(),competitors=json::array();for(const auto& item:row[1]){auto name=item.get<std::string>();mapped.insert(name);auto feature=byName.contains(name)?byName.at(name):json::object();auto link=linked.contains(name)?linked.at(name):json::object();
            starlight.push_back({{"feature",name},{"implementation",feature.empty()?json("unknown: missing source assessment"):feature.value("implementation_status",json("unknown"))},{"sourceSupported",!feature.empty() && truth(feature.value("source_supported",json()))},{"workflow",feature.value("workflow",json(""))},{"limits",feature.value("limits",json(""))},{"sourceEvidence",feature.value("evidence",json::array())},{"verification",link.value("verification",json("No linked feature verification supplied"))},{"runtimeEvidence",link.value("evidence",json::array())},{"issues",link.value("issues",json::array())}});
        }
        for(size_t i=0;i<std::min(rules.at("engines").size(),row[2].size());++i)competitors.push_back({{"engine",rules["engines"][i]},{"claim",row[2][i][0]},{"url",row[2][i][1]},{"reviewed",reviewed},{"freshness",freshness},{"evidenceType","official documentation; not locally tested"}});
        rows.push_back({{"area",row[0]},{"starlight",starlight},{"competitors",competitors},{"nextValidation",row[3]}});
    }
    json unmapped=json::array();for(const auto& [name,feature]:byName)if(!mapped.contains(name))unmapped.push_back(name);auto hash=hash_bytes(python_json(model));std::transform(hash.begin(),hash.end(),hash.begin(),[](unsigned char c){return char(std::tolower(c));});
    return {{"schema","starlight.engine-comparison.v1"},{"generatedUtc",generated},{"starlightProvenance",model.value("provenance",json::object())},{"inputModelSha256",hash},{"starlightValidation",model.value("validation",json::object())},{"catalogueReviewed",reviewed},{"competitorFreshness",freshness},{"referenceScope","UE5 rolling documentation, Unity 6000.0/Hub, Godot stable 4.x; exact installed builds not tested"},{"rows",rows},{"unmappedStarlightFeatures",unmapped},
        {"performance",{{"status","not compared"},{"reason","No matched cross-engine benchmark dataset supplied. Starlight-only FPS and passing tests establish no ranking."},{"required",json::array({"exact engine builds","same hardware and drivers","same scene and workload","matched native resolution and visual quality","same presentation and frame-cap policy","warmup and measurement duration","CPU/GPU frame times, percentiles and raw evidence"})}}},
        {"limits","Selected capability areas, not exhaustive engine parity. Unknown is not absent. No percentages, winner or production-readiness score is inferred."}};
}
inline std::string comparison_markdown_native(const json& report){
    std::vector<std::string> lines={"# Starlight compared with Unreal Engine 5, Unity and Godot","",report.at("limits"),"","**Competitor references reviewed:** "+report.at("catalogueReviewed").get<std::string>()+". "+report.at("competitorFreshness").get<std::string>()+".","",report.at("referenceScope"),"","Starlight descriptions come from the supplied audit snapshot. Competitor descriptions come from official documentation, not tests on this computer. Planned Starlight modes are not counted as delivered features.",""};
    for(const auto& row:report.at("rows")){lines.push_back("## "+row["area"].get<std::string>());lines.push_back("");for(const auto& feature:row["starlight"]){
            for(auto line:std::vector<std::string>{"**Starlight — "+feature["feature"].get<std::string>()+":** "+feature["implementation"].get<std::string>(),feature["workflow"],"**Limits:** "+feature["limits"].get<std::string>(),"**Verification:** "+feature["verification"].get<std::string>(),""})lines.push_back(line);
            for(const auto& e:feature["runtimeEvidence"])lines.push_back("- Runtime evidence: "+python_text(e.value("path",json("unknown")))+"; "+python_text(e.value("outcome",json("unknown")))+"; "+python_text(e.value("freshness",json("unknown"))));
            for(const auto& issue:feature["issues"])lines.push_back("- Known issue: "+python_text(issue.value("status",json("unknown")))+" — "+python_text(issue.value("summary",json("See linked evidence"))));
        }
        for(const auto& peer:row["competitors"])lines.push_back("- **"+peer["engine"].get<std::string>()+":** "+peer["claim"].get<std::string>()+" [Official source]("+peer["url"].get<std::string>()+").");lines.push_back("");lines.push_back("**Next validation:** "+row["nextValidation"].get<std::string>());lines.push_back("");
    }
    std::string required;for(const auto& item:report["performance"]["required"]){if(!required.empty())required+=", ";required+=item.get<std::string>();}
    for(auto line:std::vector<std::string>{"## Performance comparison","","**Not compared.** "+report["performance"]["reason"].get<std::string>(),"","Required for a defensible comparison: "+required+".","","## Coverage still to map","","The full Starlight feature reference remains authoritative for these unmapped areas. This comparison does not assume competitors lack them.",""})lines.push_back(line);
    for(const auto& name:report["unmappedStarlightFeatures"])lines.push_back("- "+name.get<std::string>());std::string text;for(const auto& line:lines)text+=line+"\n";return text;
}
}
