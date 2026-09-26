#pragma once
#include "scene.hpp"
#include "mesh.hpp"
#include <unordered_set>
namespace starlight::editor {
// No recursive event emission: one event evaluates at most 64 rules, each with
// at most 16 conditions and 16 actions. All state belongs to one Play session.
class GameplayRulesRuntime {
    std::vector<GameplayRule> rules_;
    std::vector<bool> fired_, inside_;
    bool run(size_t index, SceneData& scene) {
        const auto& rule=rules_[index];
        if(!rule.Enabled || (rule.Once && fired_[index]))return false;
        if(!quest_rule_available(rule.Id,scene))return false;
        for(const auto& c:rule.Conditions)
            if(!(c.Type=="has-key" ? items.contains(c.Value) : objectives.contains(c.Value)))return false;
        fired_[index]=true;
        for(const auto& action:rule.Actions)apply(action,scene);
        return true;
    }
public:
    std::unordered_set<std::string> items,objectives,removed;
    bool finished=false;
    uint64_t revision=0;
    std::string message;
    bool rule_completed(const std::string& id) const {
        for(size_t i=0;i<rules_.size();++i)if(rules_[i].Id==id)return fired_[i];
        return false;
    }
    bool quest_rule_available(const std::string& id,const SceneData& scene)const {
        for(const auto& quest:scene.Quests){
            if(quest.StepSizes.empty())continue;
            auto found=std::find(quest.RuleIds.begin(),quest.RuleIds.end(),id);
            if(found==quest.RuleIds.end())continue;
            size_t index=size_t(found-quest.RuleIds.begin()),start=0;
            for(int count:quest.StepSizes){
                if(index<start+count)break;
                for(size_t i=start;i<start+count;++i)if(!rule_completed(quest.RuleIds[i]))return false;
                start+=count;
            }
        }
        return true;
    }
    void reset(const std::vector<GameplayRule>& rules) {
        if(rules.size()>64)throw std::runtime_error("Too many gameplay rules");
        for(const auto& rule:rules)rule.validate();
        rules_=rules;fired_.assign(rules.size(),false);inside_.assign(rules.size(),false);
        items.clear();objectives.clear();removed.clear();finished=false;revision=0;message.clear();
    }
    bool remove_object(SceneData& scene,const std::string& id,bool doorOnly=false) {
        auto object=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==id && (!doorOnly || c.GameplayRole=="door");});
        if(object==scene.Characters.end())return false;
        removed.insert(id);scene.Characters.erase(object);++revision;return true;
    }
    void apply(const GameplayAction& action,SceneData& scene) {
        if(action.Type=="open-door") {
            if(remove_object(scene,action.Target,true))message="Door opened.";
        } else if(action.Type=="complete-objective") {
            if(objectives.insert(action.Target).second)message="Objective complete: "+action.Target;
        } else if(action.Type=="finish-game") {finished=true;message="Adventure complete!";}
    }
    bool emit(const std::string& event,const std::string& subject,SceneData& scene) {
        message.clear();bool activated=false;
        for(size_t i=0;i<rules_.size();++i)
            if(rules_[i].Event==event && (rules_[i].Subject.empty() || rules_[i].Subject==subject))activated=run(i,scene)||activated;
        return activated;
    }
    void collect(const std::string& item,SceneData& scene) {
        items.insert(item);emit("item-collected",item,scene);
    }
    void areas(Vec3 position,SceneData& scene) {
        message.clear();
        for(size_t i=0;i<rules_.size();++i) {
            const auto& r=rules_[i];if(r.Event!="enter-area")continue;
            const bool inside=(position-Vec3{r.X,r.Y,r.Z}).length()<=r.Radius;
            if(inside && !inside_[i])run(i,scene);
            inside_[i]=inside;
        }
    }
};
}
