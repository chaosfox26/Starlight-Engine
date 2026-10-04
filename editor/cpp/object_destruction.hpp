#pragma once
#include "scene.hpp"
#include <unordered_map>
namespace starlight::editor {
// Runtime damage never changes authored durability or gameplay roles.
class ObjectDestruction {
    std::unordered_map<std::string,double> health_;
public:
    void reset(){health_.clear();}
    double remaining(const Character& object)const{
        auto found=health_.find(object.Id);return found==health_.end()?object.DestructionHealth:found->second;
    }
    bool damage(const Character& object,double amount){
        if(!object.Breakable||!std::isfinite(amount)||amount<=0)return false;
        auto [entry,fresh]=health_.try_emplace(object.Id,object.DestructionHealth);
        entry->second=std::max(0.,entry->second-amount);return entry->second<=0;
    }
};
// Emitters have visual proxy geometry, not solid objects to turn into rubble.
// Vegetation removal is supported here; torn leaves require the detail pass.
inline bool destruction_has_rubble(const Character& object){
    if(object.Sculpt||!object.Asset.empty())return true;
    const auto kind=object.Shape.value_or("");
    return kind!="grass"&&kind!="fire"&&kind!="light"&&kind!="lava"&&kind!="chaos-lightning"&&kind!="craxis-smoke";
}
}
