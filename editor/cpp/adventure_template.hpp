#pragma once
#include "scene.hpp"
namespace starlight::editor {
// A new project, never a destructive conversion of an existing scene.
inline SceneData adventure_template() {
    SceneData scene;
    scene.Name="My first adventure";
    scene.Game.emplace();
    scene.Game->SpawnZ=6;
    scene.Terrain.emplace();
    auto box=[&](const char* name,double x,double z,double width,double height,double depth,const char* color) -> Character& {
        Character object;
        object.Name=name;object.Shape="cube";object.X=x;object.Z=z;
        object.Width=width;object.Height=height;object.Depth=depth;object.Color=color;
        scene.Characters.push_back(object);return scene.Characters.back();
    };
    box("Left boundary",-4,1, .3,3,14,"#394665");
    box("Right boundary",4,1, .3,3,14,"#394665");
    box("Back boundary",0,8,8,3,.3,"#394665");
    box("Far boundary",0,-6,8,3,.3,"#394665");
    box("Left doorway wall",-2.5,0,3,3,.3,"#536485");
    box("Right doorway wall",2.5,0,3,3,.3,"#536485");
    auto& key=box("Gold key token - press E",0,3,.5,.5,.5,"#FFD15C");
    key.Y=1.45;key.GameplayRole="key";key.GameplayKey="gold";
    auto& door=box("Door - requires gold key",0,0,2,3,.3,"#8A5C38");
    door.GameplayRole="door";door.GameplayKey="gold";
    auto& exit=box("Exit - press E to finish",0,-4,2,2.5,.3,"#50DCB3");
    exit.GameplayRole="exit";exit.GameplayKey="gold";
    scene.validate();return scene;
}
inline SceneData quest_template(){
    auto scene=adventure_template();scene.Name="Restore the outpost";
    auto key=std::find_if(scene.Characters.begin(),scene.Characters.end(),[](const auto& c){return c.GameplayRole=="key";});
    key->Name="Power cell 1";key->GameplayKey="cell-1";key->X=-1.5;
    auto second=*key;second.Id=new_id();second.X=0;second.GameplayKey="cell-2";second.Name="Power cell 2";
    auto third=second;third.Id=new_id();third.X=1.5;third.GameplayKey="cell-3";third.Name="Power cell 3";
    scene.Characters.push_back(second);scene.Characters.push_back(third);
    auto door=std::find_if(scene.Characters.begin(),scene.Characters.end(),[](const auto& c){return c.GameplayRole=="door";});door->GameplayKey="cell-1";
    auto exit=std::find_if(scene.Characters.begin(),scene.Characters.end(),[](const auto& c){return c.GameplayRole=="exit";});exit->GameplayRole="none";exit->Name="Engineer terminal — press E";
    GameplayRule collect;collect.Name="Collect three power cells";collect.Conditions={{"has-key","cell-1"},{"has-key","cell-2"},{"has-key","cell-3"}};
    collect.Actions={{"complete-objective","cells"},{"open-door",door->Id}};
    GameplayRule area;area.Name="Enter the control room";area.Event="enter-area";area.X=0;area.Y=1.7;area.Z=-2;area.Radius=1.5;
    area.Conditions={{"objective-complete","cells"}};area.Actions={{"complete-objective","room"}};
    GameplayRule finish;finish.Name="Report to the engineer terminal";finish.Event="interact";finish.Subject=exit->Id;
    finish.Conditions={{"objective-complete","room"}};finish.Actions={{"complete-objective","report"},{"finish-game",""}};
    collect.Description="Find the three gold cells ahead. Approach each cell, look at it and press E. Each different cell counts once.";
    area.Description="The complete set opens the doorway. Walk through it into the control room.";
    finish.Description="Approach the green terminal at the far end of the room, look at it and press E to report back.";
    scene.Rules={collect,area,finish};QuestDefinition q;q.Title="Restore the outpost";
    q.Description="The outpost has lost power. Recover the three gold power cells ahead. The full set opens the control-room door. Enter the room, then approach the green engineer terminal and press E to report your success.";
    q.RuleIds={collect.Id,area.Id,finish.Id};scene.Quests={q};scene.validate();return scene;
}

}
