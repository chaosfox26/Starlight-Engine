#pragma once
#include "gameplay_rules.hpp"
namespace starlight::editor {
struct QuestObjectiveView {std::string text,description;int current=0,required=1;bool complete=false,locked=false;int step=0;};
struct QuestView {std::string id,title,description;bool tracked=true,complete=false;int currentStep=0,stepCount=0;std::vector<QuestObjectiveView> objectives;};
inline std::vector<QuestView> quest_views(const SceneData& scene,const GameplayRulesRuntime& runtime){
    auto definitions=scene.Quests;
    if(definitions.empty())for(const auto& rule:scene.Rules)if(rule.Enabled){
        QuestDefinition q;q.Id=rule.Id;q.Title=rule.Name;q.RuleIds={rule.Id};definitions.push_back(q);
    }
    std::vector<QuestView> result;
    for(const auto& q:definitions){
        QuestView view;view.id=q.Id;view.title=q.Title;view.description=q.Description;view.tracked=q.Tracked;view.complete=true;
        view.stepCount=int(q.StepSizes.size());
        size_t objectiveIndex=0,stepEnd=q.StepSizes.empty()?q.RuleIds.size():size_t(q.StepSizes[0]);int step=0;
        for(const auto& id:q.RuleIds){
            if(!q.StepSizes.empty() && objectiveIndex==stepEnd){++step;stepEnd+=q.StepSizes[step];}++objectiveIndex;
            auto it=std::find_if(scene.Rules.begin(),scene.Rules.end(),[&](const auto& r){return r.Id==id;});
            if(it==scene.Rules.end())continue;
            QuestObjectiveView item;item.text=it->Name;item.description=it->Description;item.complete=runtime.rule_completed(id);item.locked=!it->Enabled || !runtime.quest_rule_available(id,scene);item.step=step;
            std::unordered_set<std::string> required;
            for(const auto& c:it->Conditions){
                if(c.Type=="has-key")required.insert(c.Value);
                if(c.Type=="objective-complete" && !runtime.objectives.contains(c.Value))item.locked=true;
            }
            if(!required.empty()){
                item.required=int(required.size());
                for(const auto& key:required)if(runtime.items.contains(key))++item.current;
            } else {item.current=item.complete?1:0;}
            view.complete=view.complete&&item.complete;view.objectives.push_back(item);
        }
        if(view.stepCount){
            view.currentStep=view.stepCount-1;
            for(const auto& item:view.objectives)if(!item.complete){view.currentStep=item.step;break;}
        }
        if(!view.objectives.empty())result.push_back(view);
    }
    return result;
}
}
