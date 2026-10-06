#include "rejection_note.hpp"
{
    SceneData level;level.Game.emplace();
    GameplayRule collect;collect.Name="Collect power cells";collect.Description="Find the three cells near the outpost entrance.";collect.Conditions={{"has-key","red"},{"has-key","blue"},{"has-key","green"}};collect.Actions={{"complete-objective","cells"}};
    GameplayRule enter;enter.Name="Enter the control room";enter.Event="enter-area";enter.X=8;enter.Y=1.7;enter.Radius=1;enter.Conditions={{"objective-complete","cells"}};enter.Actions={{"complete-objective","room"}};
    GameplayRule talk;talk.Name="Return to the engineer";talk.Event="interact";talk.Subject="engineer";talk.Conditions={{"objective-complete","room"}};talk.Actions={{"complete-objective","return"}};
    level.Rules={collect,enter,talk};QuestDefinition q;q.Title="Restore the outpost";q.Description="Recover three power cells, restore the control room, then report to the engineer.";q.RuleIds={collect.Id,enter.Id,talk.Id};level.Quests={q};
    auto saved=level.encode();auto loaded=SceneData::decode(saved);require(loaded.SchemaVersion==16 && loaded.Quests[0].Id==q.Id && loaded.Quests[0].Description==q.Description && loaded.Rules[0].Description==collect.Description,"Quest text and stable objective links survive scene save/reload");
    GameplayRulesRuntime runtime;runtime.reset(loaded.Rules);auto views=quest_views(loaded,runtime);
    require(views.size()==1 && views[0].objectives[0].current==0 && views[0].objectives[0].required==3 && views[0].objectives[1].locked,"Quest tracker starts at0/3 and locks dependent objectives");
    runtime.collect("red",loaded);runtime.collect("red",loaded);views=quest_views(loaded,runtime);
    require(views[0].objectives[0].current==1 && !views[0].complete,"Duplicate item events do not inflate quest progress");
    runtime.collect("blue",loaded);runtime.collect("green",loaded);views=quest_views(loaded,runtime);
    require(views[0].objectives[0].complete && views[0].objectives[0].current==3 && !views[0].objectives[1].locked && !views[0].complete,"Real collection unlocks next step without completing entire quest");
    runtime.emit("interact","engineer",loaded);require(!quest_views(loaded,runtime)[0].complete,"Return stage cannot complete before prerequisite");
    runtime.areas({8,1.7,0},loaded);runtime.emit("interact","engineer",loaded);require(quest_views(loaded,runtime)[0].complete,"Area then authored return stage completes quest");
    runtime.reset(loaded.Rules);require(!quest_views(loaded,runtime)[0].complete && level.encode()==saved,"Play reset clears quest progress without changing authored definition");
    auto broken=level;broken.Quests[0].RuleIds={"missing"};bool rejected=false;try{broken.validate();}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}require(rejected,"Missing quest objective links rejected");
    broken=level;broken.Quests.push_back(q);rejected=false;try{broken.validate();}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}require(rejected,"Duplicate quest identity rejected");
    loaded.Quests.clear();require(quest_views(loaded,runtime).size()==3,"Legacy rules expose objectives without requiring scene conversion");
}

{
    SceneData level;level.Game.emplace();QuestDefinition q;q.Title="Five objectives, any order";q.StepSizes={5,1};
    for(int i=0;i<6;++i){GameplayRule rule;rule.Name="Objective "+std::to_string(i+1);rule.Event="interact";rule.Subject="block-"+std::to_string(i);rule.Actions={{"complete-objective",rule.Subject}};q.RuleIds.push_back(rule.Id);level.Rules.push_back(rule);}
    level.Quests={q};auto saved=level.encode();auto loaded=SceneData::decode(saved);
    require(loaded.SchemaVersion==16 && loaded.Quests[0].StepSizes==q.StepSizes,"Grouped quest steps round-trip through schema11");
    std::vector<int> order={0,1,2,3,4};bool allOrders=true;int permutations=0;
    do{
        GameplayRulesRuntime runtime;runtime.reset(level.Rules);auto play=level;
        runtime.emit("interact","block-5",play);allOrders=allOrders&&!runtime.rule_completed(q.RuleIds[5]);
        for(int i:order)runtime.emit("interact","block-"+std::to_string(i),play);
        auto view=quest_views(play,runtime)[0];allOrders=allOrders&&!view.complete&&view.currentStep==1&&!view.objectives[5].locked;
        runtime.emit("interact","block-5",play);allOrders=allOrders&&quest_views(play,runtime)[0].complete;
        ++permutations;
    }while(std::next_permutation(order.begin(),order.end()));
    require(allOrders && permutations==120,"All120 objective orders complete first step and unlock second only after all five");
    GameplayRulesRuntime runtime;runtime.reset(level.Rules);auto play=level;
    for(int i:{4,1,3,0})runtime.emit("interact","block-"+std::to_string(i),play);
    auto view=quest_views(play,runtime)[0];require(view.currentStep==0 && view.objectives[4].complete && !view.objectives[2].complete && view.objectives[5].locked,"Order5,2,4,1 retains completed marks and waits for objective3");
    runtime.emit("interact","block-2",play);require(quest_views(play,runtime)[0].currentStep==1,"Last remaining objective advances the step");
    runtime.reset(level.Rules);require(quest_views(play,runtime)[0].currentStep==0 && !quest_views(play,runtime)[0].objectives[4].complete && level.encode()==saved,"Restart resets grouped progress without modifying authoring");
    auto rejected=[&](SceneData bad){try{bad.validate();return false;}catch(...){return true;}};
    auto bad=level;bad.Quests[0].StepSizes={6};require(rejected(bad),"Six objectives in one step are rejected");
    bad=level;bad.Quests[0].StepSizes={0,5,1};require(rejected(bad),"Empty step rejected");
    bad=level;bad.Quests[0].StepSizes={3,2};require(rejected(bad),"Step sizes must include every objective");
    bad=level;bad.Rules[0].Conditions={{"objective-complete","block-1"}};require(rejected(bad),"Same-step prerequisite rejected to preserve any-order completion");
    bad=level;bad.Rules[0].Conditions={{"objective-complete","block-5"}};require(rejected(bad),"Future-step prerequisite rejected to prevent deadlock");
    bad=level;bad.Rules[5].Conditions={{"objective-complete","block-0"}};bad.validate();require(true,"Previous-step prerequisite remains valid");
}

{
    SceneData scene;scene.Game.emplace();scene.Game->SpawnZ=2;
    Character block;block.Shape="cube";block.Width=block.Depth=1;block.Height=2;block.Name="Test block";scene.Characters={block};
    for(const auto& role:{"door","breakable","key"}){
        auto authored=scene;assign_object_behavior(authored,block.Id,role);add_object_quest(authored,block.Id);
        auto saved=authored.encode();auto loaded=SceneData::decode(saved);AdventurePlayer player;player.start(loaded);player.interact();
        require(player.scene().Characters.empty() && quest_views(loaded,player.rules())[0].complete,"Object preset executes real removal and completes its linked quest");
        require(authored.encode()==saved,"Object interaction does not mutate authored scene");
        player.start(loaded);require(player.scene().Characters.size()==1 && !quest_views(loaded,player.rules())[0].complete,"Object preset restarts with its object restored");
    }
    auto authored=scene;assign_object_behavior(authored,block.Id,"breakable");add_object_quest(authored,block.Id);
    auto another=block;another.Id=new_id();another.X=4;authored.Characters.push_back(another);assign_object_behavior(authored,another.Id,"door");add_object_quest(authored,another.Id);
    authored.Quests[0].StepSizes={1,1};AdventurePlayer player;player.start(authored);player.position.x=4;player.interact();require(player.scene().Characters.size()==2,"Future-step object cannot be consumed before its objective is active");
    player.position.x=0;player.interact();player.position.x=4;player.interact();require(quest_views(authored,player.rules())[0].complete,"Object sequence completes after prerequisite step");
    CreationGuide guide;guide.start(scene,"door");guide.update(scene);require(guide.target().empty(),"Creation lesson ignores cubes that existed before lesson start");
    auto created=block;created.Id=new_id();auto build=scene;build.Characters.push_back(created);guide.update(build);guide.opened(block.Id);guide.update(build);
    require(guide.target()==created.Id && !quest_views(guide.definition,guide.runtime)[0].objectives[1].complete,"Creation lesson binds new cube and ignores F on another cube");
    assign_object_behavior(build,block.Id,"door");guide.update(build);require(!quest_views(guide.definition,guide.runtime)[0].complete,"Changing another cube cannot satisfy the lesson");
    guide.opened(created.Id);assign_object_behavior(build,created.Id,"door");guide.update(build);require(quest_views(guide.definition,guide.runtime)[0].complete,"Correct cube placement F and property satisfy creation objectives");
    assign_object_behavior(build,created.Id,"none");guide.update(build);require(!quest_views(guide.definition,guide.runtime)[0].complete,"Undoing the requested property revokes lesson completion");
    build.Characters.pop_back();guide.update(build);require(!quest_views(guide.definition,guide.runtime)[0].objectives[0].complete,"Deleting lesson cube clears its placement completion");
}

{
    SceneData scene;scene.Game.emplace();Character block;block.Shape="cube";block.Height=2;scene.Characters={block};
    assign_object_behavior(scene,block.Id,"door");add_object_quest(scene,block.Id);auto ruleId=scene.Rules[0].Id;
    assign_object_behavior(scene,block.Id,"breakable");require(scene.Rules[0].Id==ruleId && scene.Rules[0].Event=="object-broken" && scene.Quests[0].RuleIds[0]==ruleId,"Changing preset updates its objective without losing stable links");
    bool rejected=false;try{assign_object_behavior(scene,block.Id,"none");}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}require(rejected,"Clearing behavior cannot silently strand its objective");
#ifdef STARLIGHT_CHARACTER_CONTROLLER
    SceneData landing;landing.Game.emplace();landing.Terrain.emplace();block.Height=.4;landing.Characters={block};add_object_quest(landing,block.Id,true);
    AdventurePlayer player;player.start(landing);player.position={0,2.5,0};for(int i=0;i<100;++i)player.move(0,0,1./120);
    require(quest_views(landing,player.rules())[0].complete,"Landing on the actual block completes its stand-on objective");
#endif
}

{
    SceneData scene;CreationGuide guide;guide.start(scene,"workshop");Character cube;cube.Shape="cube";scene.Characters={cube};guide.update(scene);guide.opened(cube.Id);
    require(quest_views(guide.definition,guide.runtime)[0].objectives[0].text.find("Press F")!=std::string::npos,"Placed cube changes only its own objective instruction");
    assign_object_behavior(scene,cube.Id,"door");guide.update(scene);require(quest_views(guide.definition,guide.runtime)[0].objectives[0].complete,"Workshop credits first distinct Open cube");
    assign_object_behavior(scene,cube.Id,"breakable");guide.update(scene);auto view=quest_views(guide.definition,guide.runtime)[0];require(!view.objectives[0].complete && !view.objectives[1].complete,"Changing first cube to Breakable cannot satisfy a second block objective");
    scene.Characters[0].X=3;guide.carried(cube.Id);guide.update(scene);require(!quest_views(guide.definition,guide.runtime)[0].objectives[2].complete,"Carrying first cube cannot satisfy the third-block objective");
    assign_object_behavior(scene,cube.Id,"door");auto second=cube;second.Id=new_id();auto third=cube;third.Id=new_id();scene.Characters.push_back(second);scene.Characters.push_back(third);guide.update(scene);
    guide.opened(third.Id);scene.Characters[2].X=5;guide.carried(third.Id);guide.update(scene);
    guide.opened(second.Id);assign_object_behavior(scene,second.Id,"breakable");guide.update(scene);require(quest_views(guide.definition,guide.runtime)[0].complete,"Three distinct blocks complete workshop in order Open Carry Breakable");
    scene.Characters[2].X=0;guide.update(scene);require(!quest_views(guide.definition,guide.runtime)[0].objectives[2].complete,"Undoing third-block movement revokes its completion");
}

{
    SceneData scene;CreationGuide guide;guide.start(scene,"workshop",{"door","breakable","move","door","breakable"});
    Character first;first.Shape="cube";scene.Characters={first};guide.update(scene);guide.opened(first.Id);
    for(const auto& role:{"door","breakable","door"}){assign_object_behavior(scene,first.Id,role);guide.update(scene);}
    scene.Characters[0].X=4;guide.carried(first.Id);guide.update(scene);
    auto view=quest_views(guide.definition,guide.runtime)[0];int done=0;for(const auto& o:view.objectives)done+=o.complete;
    require(view.objectives.size()==5 && done==1,"One cube repeatedly changed and carried cannot cheese five distinct objectives");
    for(int i=1;i<5;++i){auto block=first;block.Id=new_id();scene.Characters.push_back(block);}guide.update(scene);
    for(int i:{4,2,1,3}){auto id=scene.Characters[i].Id;guide.opened(id);if(i==2){scene.Characters[i].X=5;guide.carried(id);}else assign_object_behavior(scene,id,i==3?"door":"breakable");guide.update(scene);}
    require(quest_views(guide.definition,guide.runtime)[0].complete,"Five separate cubes satisfy their individual requirements in arbitrary order");
}
