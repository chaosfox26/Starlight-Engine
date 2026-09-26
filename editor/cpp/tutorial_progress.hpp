#pragma once
#include "scene.hpp"
#include <sstream>

namespace starlight::editor {
struct TutorialSection { std::string id; size_t steps; };
struct TutorialProgress {
    size_t selected=0;
    std::vector<size_t> positions;
    explicit TutorialProgress(size_t count):positions(count,0){}
};
// IDs keep progress attached to the right lesson when the catalog is reordered.
inline std::string encode_tutorial_progress(const std::vector<TutorialSection>& catalog,
                                           const TutorialProgress& progress) {
    if(progress.selected>=catalog.size() || progress.positions.size()!=catalog.size())
        throw std::runtime_error("Invalid tutorial progress");
    nlohmann::json positions=nlohmann::json::object();
    for(size_t i=0;i<catalog.size();++i) {
        if(progress.positions[i]>=catalog[i].steps)throw std::runtime_error("Invalid tutorial step");
        positions[catalog[i].id]=progress.positions[i];
    }
    return nlohmann::json{{"version",2},{"selected",catalog[progress.selected].id},{"positions",positions}}.dump();
}
inline TutorialProgress decode_tutorial_progress(const std::vector<TutorialSection>& catalog,
                                                 std::string_view source) {
    TutorialProgress fresh(catalog.size());
    if(source.size()>65536)return fresh;
    try {
        auto json=nlohmann::json::parse(source);
        if(!json.is_object() || json.at("version")!=2 || !json.at("selected").is_string() ||
           !json.at("positions").is_object())return fresh;
        auto result=fresh;
        for(size_t i=0;i<catalog.size();++i) {
            if(json["selected"]==catalog[i].id)result.selected=i;
            const auto& positions=json["positions"];
            if(!positions.contains(catalog[i].id))continue;
            const auto& value=positions[catalog[i].id];
            if(!value.is_number_unsigned())continue;
            auto step=value.get<uint64_t>();
            if(step<catalog[i].steps)result.positions[i]=size_t(step);
        }
        return result;
    } catch(...) {
        // Read the first shipped positional format without accepting partial files.
        std::istringstream input{std::string(source)};
        size_t version=0,selected=0;
        if(!(input>>version>>selected) || version!=1 || selected>=catalog.size())return fresh;
        auto result=fresh;result.selected=selected;
        for(size_t i=0;i<catalog.size();++i)
            if(!(input>>result.positions[i]) || result.positions[i]>=catalog[i].steps)return fresh;
        std::string extra;if(input>>extra)return fresh;
        return result;
    }
}
}
