#pragma once
#include "quest_model.hpp"
#include <unordered_map>
namespace starlight::editor {
inline void assign_object_behavior(SceneData& scene,const std::string& id,const std::string& role){
    if(role!="none" && role!="key" && role!="door" && role!="breakable")throw std::runtime_error("Unsupported object behavior");
    auto it=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==id;});
    if(it==scene.Characters.end())throw std::runtime_error("The selected object no longer exists");
    const bool wasKey=it->GameplayRole=="key";
    const std::string key=role=="key"?(wasKey && !it->GameplayKey.empty()?it->GameplayKey:it->Id):wasKey?std::string{}:it->GameplayKey;
    const std::string ownRule="object-rule-"+id+"-";
    for(auto& rule:scene.Rules)if(rule.Subject==id || rule.Id.starts_with(ownRule)){
        if(rule.Event=="stand-on-object" && role=="key")throw std::runtime_error("Remove the stand-on objective before making this object collectible");
        if(rule.Id.starts_with("object-rule-")){
            if(role=="none")throw std::runtime_error("Remove this object's quest objective before clearing its behavior");
            if(rule.Event!="stand-on-object"){
                rule.Event=role=="door"?"object-opened":role=="breakable"?"object-broken":"item-collected";
                rule.Subject=role=="key"?key:id;
                rule.Name=(role=="door"?"Open ":role=="breakable"?"Break ":"Collect ")+it->Name;
            }
        }
    }
    it->GameplayRole=role;it->GameplayKey=key;
    if(role=="breakable")it->Breakable=true;
    if(!scene.Game)scene.Game.emplace();
    scene.validate();
}
inline void add_object_quest(SceneData& scene,const std::string& id,bool stand=false){
    auto object=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==id;});
    if(object==scene.Characters.end())throw std::runtime_error("Object no longer exists");
    if(stand && (object->GameplayRole=="key" || object->GameplayRole=="exit" || object->Dynamic))throw std::runtime_error("Choose a solid static block to stand on");
    if(!stand && object->GameplayRole!="door" && object->GameplayRole!="key" && object->GameplayRole!="breakable")throw std::runtime_error("Choose Open, Breakable or Collect first");
    const std::string event=stand?"stand-on-object":object->GameplayRole=="door"?"object-opened":object->GameplayRole=="breakable"?"object-broken":"item-collected";
    if(event=="item-collected" && object->GameplayKey.empty())throw std::runtime_error("Give a collectible key a key name");
    const std::string subject=event=="item-collected"?object->GameplayKey:id;
    const std::string ruleId="object-rule-"+id+"-"+event;
    for(const auto& rule:scene.Rules)if(rule.Id==ruleId || (rule.Event==event && rule.Subject==subject && event!="item-collected"))throw std::runtime_error("This object already has that objective");
    GameplayRule rule;rule.Id=ruleId;rule.Name=(stand?"Stand on ":object->GameplayRole=="door"?"Open ":object->GameplayRole=="breakable"?"Break ":"Collect ")+object->Name;
    rule.Description=stand?"Reach the top of this object and stand on it.":"In Play, look at this object within reach and press F (E also works).";
    rule.Event=event;rule.Subject=subject;rule.Actions={{"complete-objective",rule.Id}};
    auto quest=std::find_if(scene.Quests.begin(),scene.Quests.end(),[](const auto& q){return q.Id=="object-interactions";});
    if(quest==scene.Quests.end()){QuestDefinition q;q.Id="object-interactions";q.Title="Object interactions";q.Description="Try the behaviors assigned to your objects.";scene.Quests.push_back(q);quest=std::prev(scene.Quests.end());}
    if(quest->RuleIds.empty() || (!quest->StepSizes.empty() && quest->StepSizes.back()==5))quest->StepSizes.push_back(1);
    else if(!quest->StepSizes.empty())++quest->StepSizes.back();
    quest->RuleIds.push_back(rule.Id);scene.Rules.push_back(rule);if(!scene.Game)scene.Game.emplace();scene.validate();
}
class CreationGuide {
    struct ObjectiveParameters {std::string role,shape="cube",target;};
    std::vector<ObjectiveParameters> workshop_;std::unordered_set<std::string> openedObjects_,movedObjects_;std::unordered_map<std::string,Vec3> origins_;
    std::unordered_set<std::string> original_;std::string target_,role_;bool opened_=false;
public:
    bool active=false;SceneData definition;GameplayRulesRuntime runtime;
    void start(const SceneData& scene,const std::string& role,const std::vector<std::string>& requirements={"door","breakable","move"}){
        if(requirements.empty() || requirements.size()>5)throw std::runtime_error("Creation step needs one to five objectives");
        workshop_.clear();for(const auto& requirement:requirements){if(requirement!="door" && requirement!="breakable" && requirement!="move")throw std::runtime_error("Unsupported creation requirement");workshop_.push_back({requirement});}
        openedObjects_.clear();movedObjects_.clear();origins_.clear();
        active=true;role_=role;target_.clear();opened_=false;original_.clear();for(const auto& c:scene.Characters)original_.insert(c.Id);
        if(role=="workshop"){
            definition=SceneData{};QuestDefinition q;q.Title="Three blocks, three possibilities";q.Description="Create three separate cubes from Build > Shapes. Press F on each. Make one Open, another Breakable, and Carry the third to a different spot. Complete these in any order; one cube cannot count for multiple objectives.";q.StepSizes={int(workshop_.size())};
            for(const auto& name:requirements){GameplayRule rule;rule.Name=name;rule.Description=q.Description;rule.Event="interact";rule.Subject=rule.Id;rule.Actions={{"complete-objective",rule.Id}};q.RuleIds.push_back(rule.Id);definition.Rules.push_back(rule);}definition.Quests={q};runtime.reset(definition.Rules);return;
        }
        definition=SceneData{};QuestDefinition q;q.Title=role=="door"?"Create an opening block":"Create a breakable block";q.Description="Open Build > Shapes, choose a cube and place it. Point at that cube and press F. Choose the requested behavior. Test it in Play when ready.";q.StepSizes={3};
        for(auto name:{std::string("Place a cube"),std::string("Press F on your cube"),role=="door"?std::string("Set your cube to Open"):std::string("Set your cube to Breakable")}){GameplayRule rule;rule.Name=name;rule.Event="interact";rule.Subject=rule.Id;rule.Actions={{"complete-objective",rule.Id}};q.RuleIds.push_back(rule.Id);definition.Rules.push_back(rule);}definition.Quests={q};runtime.reset(definition.Rules);
    }
    void opened(const std::string& id){if(id==target_)opened_=true;if(!original_.contains(id))openedObjects_.insert(id);}
    void carried(const std::string& id){if(!original_.contains(id))movedObjects_.insert(id);}
    const std::string& target()const{return target_;}
    void update(const SceneData& scene){
        if(!active)return;
        if(role_=="workshop"){
            for(const auto& c:scene.Characters)if(c.Shape && *c.Shape=="cube" && !original_.contains(c.Id))origins_.try_emplace(c.Id,Vec3{c.X,c.Y,c.Z});
            runtime.reset(definition.Rules);
            for(size_t slot=0;slot<workshop_.size();++slot){
                auto& params=workshop_[slot];auto& target=params.target;
                if(!target.empty() && std::none_of(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==target;}))target.clear();
                if(target.empty())for(const auto& c:scene.Characters)if(origins_.contains(c.Id) && c.Shape && *c.Shape==params.shape && std::none_of(workshop_.begin(),workshop_.end(),[&](const auto& o){return o.target==c.Id;})){target=c.Id;break;}
                auto object=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==target;});
                auto& rule=definition.Rules[slot];const auto label=params.role=="door"?"Open":params.role=="breakable"?"Breakable":"Carry";
                bool complete=false;
                if(object==scene.Characters.end())rule.Name="Place cube "+std::to_string(slot+1)+" for "+label;
                else if(!openedObjects_.contains(target))rule.Name="Press F on "+object->Name+" ("+label+")";
                else {
                    complete=params.role=="move"?movedObjects_.contains(target) && (Vec3{object->X,object->Y,object->Z}-origins_.at(target)).length()>.01:object->GameplayRole==params.role;
                    rule.Name=complete?object->Name+" — "+label+" ready":params.role=="move"?"Carry "+object->Name+" and place it elsewhere":"Set "+object->Name+" to "+label;
                }
                if(complete)runtime.emit("interact",rule.Id,definition);
            }
            return;
        }
        if(target_.empty())for(const auto& c:scene.Characters)if(c.Shape && *c.Shape=="cube" && !original_.contains(c.Id)){target_=c.Id;break;}
        runtime.reset(definition.Rules);
        auto object=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==target_;});
        if(object==scene.Characters.end())return;
        auto complete=[&](int i){runtime.emit("interact",definition.Rules[i].Id,definition);};
        complete(0);if(opened_)complete(1);if(object->GameplayRole==role_)complete(2);
    }
};
}
