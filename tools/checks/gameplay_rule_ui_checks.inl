{
    scene=SceneData{};scene.Game.emplace();history.clear();selected=-1;selectionIds.clear();
    Character door;door.Shape="cube";door.Name="Linked test door";door.GameplayRole="door";scene.Characters.push_back(door);refresh();show_inspector(6);
    SetWindowTextW(inputs.at("ruleName"),L"Collect two items");SetWindowTextW(inputs.at("ruleKeys"),L"red, blue");
    SetWindowTextW(inputs.at("ruleObjective"),L"Find both items");command(AddGameplayRule);
    check("authoring adds a bounded collection rule with both item conditions",scene.Rules.size()==1 && scene.Rules[0].Conditions.size()==2 && scene.Rules[0].Actions[0].Target=="Find both items");
    const auto ruleId=scene.Rules.at(0).Id;
    command(Undo);check("one Undo removes the authored rule",scene.Rules.empty());
    command(Redo);check("Redo restores the same rule identity",scene.Rules.size()==1 && scene.Rules[0].Id==ruleId);
    auto file=output.parent_path()/"rules-authored.starscene";save_scene(scene,file);
    auto restored=SceneData::decode(read_file(file));check("authored rule fields survive actual scene saving",json(restored.Rules)==json(scene.Rules));
    SendMessageW(inputs.at("savedRule"),CB_SETCURSEL,1,0);command(RemoveGameplayRule);
    check("selected saved rule can be removed",scene.Rules.empty());command(Undo);
    check("Undo restores a removed rule and its conditions",scene.Rules.size()==1 && scene.Rules[0].Conditions.size()==2);
    show_inspector(6);SendMessageW(inputs.at("ruleEvent"),CB_SETCURSEL,1,0);SendMessageW(inputs.at("ruleAction"),CB_SETCURSEL,1,0);
    SendMessageW(inputs.at("ruleDoor"),CB_SETCURSEL,1,0);command(AddGameplayRule);
    check("authoring links an area action to a stable door identity",scene.Rules.size()==2 && scene.Rules[1].Actions[0].Target==door.Id);
    auto authored=scene.encode();playback_command(Play);
    check("editor Play executes the saved area rule in an independent copy",adventure && adventure->scene().Characters.empty() && scene.encode()==authored);
    playback_command(Stop);check("Stop restores authoring without copying Play changes",!adventure && scene.encode()==authored);
    command(Undo);check("Play does not consume or replace rule-authoring Undo",scene.Rules.size()==1 && scene.Rules[0].Id==ruleId);
    show_inspector(6);scroll_inspector(INT_MAX);RECT last{},viewport{};GetWindowRect(fields.back(),&last);MapWindowPoints(nullptr,inspector,reinterpret_cast<POINT*>(&last),2);GetClientRect(inspector,&viewport);
    check("the final rule action control is fully reachable",last.top>=0 && last.bottom<=viewport.bottom);
}

{
    scene=quest_template();history.clear();refresh();show_inspector(6);
    auto prior=scene.encode();SendMessage(inputs.at("savedQuest"),CB_SETCURSEL,1,0);command(LoadQuest);
    check("quest authoring loads description and stable objective links",SendMessage(inputs.at("questRules"),LB_GETSELCOUNT,0,0)==3 && text(inputs.at("questTitle"))==scene.Quests[0].Title);
    SetWindowTextW(inputs.at("questTitle"),L"Power restoration");command(SaveQuest);
    check("quest authoring updates selected quest",scene.Quests.size()==1 && scene.Quests[0].Title=="Power restoration");
    command(Undo);check("quest text edit supports Undo",scene.encode()==prior);command(Redo);
    auto path=output.parent_path()/"quest-authored.starscene";save_scene(scene,path);auto loaded=load_scene(path);
    check("quest descriptions and links survive actual file save",json(loaded.Quests)==json(scene.Quests));
    auto authored=scene.encode();playback_command(Play);check("quest tracker visible in editor Play",IsWindowVisible(questTracker.handle()) && questTracker.views().size()==1);
    playback_command(Stop);check("Stop hides tracker and preserves authored quests",!IsWindowVisible(questTracker.handle()) && scene.encode()==authored);
    show_inspector(6);SendMessage(inputs.at("savedQuest"),CB_SETCURSEL,1,0);command(RemoveQuest);check("quest removal works",scene.Quests.empty());command(Undo);check("Undo restores quest definition",scene.Quests.size()==1);
}

{
    scene=quest_template();history.clear();refresh();show_inspector(6);
    SendMessage(inputs.at("savedQuest"),CB_SETCURSEL,1,0);command(LoadQuest);auto original=scene.encode();
    SetWindowTextW(inputs.at("questStepSizes"),L"1,1,1");command(SaveQuest);
    check("Grouped steps save through authoring",scene.Quests[0].StepSizes==std::vector<int>({1,1,1}));
    auto grouped=scene.encode();command(Undo);check("Undo restores previous quest grouping",scene.encode()==original);
    command(Redo);check("Redo restores saved steps",scene.encode()==grouped);
    show_inspector(6);SendMessage(inputs.at("savedQuest"),CB_SETCURSEL,1,0);command(LoadQuest);
    check("Load exposes saved step sizes",text(inputs.at("questStepSizes"))=="1,1,1");
    auto file=output.parent_path()/"grouped-quest.starscene";save_scene(scene,file);check("Grouped quest file reload preserves steps",load_scene(file).Quests[0].StepSizes==scene.Quests[0].StepSizes);
}

{
    scene=SceneData{};scene.Game.emplace();scene.Game->SpawnZ=2;Character block;block.Shape="cube";block.Height=2;block.Name="Carry block";scene.Characters={block};history.clear();selected=0;refresh();
    command(InteractiveBuild);explorer->position={0,1.7,2};explorer->yaw=explorer->pitch=0;
    SendMessage(view,WM_KEYDOWN,'F',0);check("F opens behavior menu on aimed nearby block",openMenu==ObjectBehaviorMenu && behaviorTarget==block.Id);
    command(BehaviorBreak);check("F menu applies breakable behavior",scene.Characters[0].GameplayRole=="breakable");
    command(BehaviorQuest);check("Object menu creates saved quest linked to selected block",scene.Rules.back().Subject==block.Id && scene.Quests[0].RuleIds[0]==scene.Rules.back().Id);
    auto prior=scene.encode();command(BehaviorCarry);carryPoint={3,0,3};
    check("Carrying leaves authored scene unchanged until placement",!carriedId.empty() && scene.encode()==prior);
    SendMessage(view,WM_KEYDOWN,'F',0);check("F places carried block without changing its identity",carriedId.empty() && scene.Characters[0].X==3 && scene.Characters[0].Id==block.Id && scene.Rules.back().Subject==block.Id);
    command(Undo);check("Undo restores carried block position and quest links",scene.encode()==prior);
    behaviorTarget=block.Id;command(BehaviorCarry);carryPoint={5,0,5};escape();check("Escape cancels carry without authoring changes",carriedId.empty() && scene.encode()==prior);
    if(explorer)command(InteractiveBuild);
}
