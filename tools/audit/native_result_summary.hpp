#pragma once
#include "../../editor/cpp/scene.hpp"
#include <set>
namespace starlight::audit {
inline std::string result_text(const starlight::editor::json& value){return value.is_string()?value.get<std::string>():value.dump();}
inline std::string final_status(const starlight::editor::json& summary,const starlight::editor::json& findings){
    using starlight::editor::json;
    if(!summary.value("runtimeExecuted",false) && summary.value("mode",std::string("features"))=="features")return "Report complete. No new runtime tests were run; any listed results are historical.";
    auto stages=summary.value("stages",json::array());size_t passed=0,failed=0;
    for(const auto& stage:stages){auto outcome=stage.value("outcome",std::string());if(outcome=="pass")++passed;else if(outcome=="fail" || outcome=="timeout" || outcome.starts_with("invalid"))++failed;}
    auto text=(!stages.empty() && passed==stages.size())?"All "+std::to_string(stages.size())+" test stages successful.":"Tests complete: "+std::to_string(passed)+" passed, "+std::to_string(failed)+" failed, "+std::to_string(stages.size()-passed-failed)+" unavailable or not run.";
    std::string details;for(const auto& finding:findings)details+=result_text(finding.value("detail",json("")))+" ";
    std::set<std::string> seen;
    for(const auto& stage:stages){auto outcome=stage.value("outcome",std::string("unknown result"));if(outcome=="pass")continue;auto name=stage.value("name",std::string("Unnamed stage"));std::string reason;
        auto exitCode=stage.value("exitCode",uint64_t(0));
        if(exitCode==0xc0000135ULL)reason=name+": Windows could not find a required runtime DLL in the package.";
        else if(exitCode==0xc0000139ULL)reason=name+": A packaged DLL has the wrong version or is missing a required function.";
        else if(exitCode==0xc000007bULL)reason=name+": A packaged program or DLL has an incompatible format.";
        else if(name=="native-classic-ui" && details.find("deadline")!=std::string::npos)reason="Editor controls did not finish within the allowed time.";
        else if(name=="native-render-lifecycle-visible" && details.find("Fullscreen live telemetry")!=std::string::npos)reason="The fullscreen timing display did not pass its visibility check.";
        else if(name=="denoiser-quality" && details.find("dynamic transform produces object motion vectors")!=std::string::npos)reason="Moving objects did not produce the expected motion signal for the denoiser.";
        else if(outcome=="timeout")reason=name+" exceeded its time limit.";
        else {auto detail=stage.value("error",json());if(detail.is_null() || detail=="")detail=stage.value("reason",json());if(detail.is_null() || detail=="")for(const auto& finding:findings){auto file=finding.value("file",std::string());if(file.find("/"+name+"/")!=std::string::npos || file.find("\\"+name+"\\")!=std::string::npos){detail=finding.value("detail",json());break;}}if(detail.is_null() || detail=="")detail=outcome;reason=name+": "+result_text(detail);}
        if(seen.insert(reason).second)text+='\n'+reason;
        if(name=="native-render-lifecycle-visible" && details.find("debug layer is inactive")!=std::string::npos)
            text+="\nGraphics debug validation could not run because the Direct3D debug layer is inactive. This check remains unverified.";
    }return text;
}
}
