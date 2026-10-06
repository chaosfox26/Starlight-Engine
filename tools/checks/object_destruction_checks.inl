#include "rejection_note.hpp"
// Keep large gameplay instances off the Windows stack.
__declspec(noinline) static void object_destruction_checks(const auto& require,const fs::path& folder){
    auto level=std::make_unique<SceneData>();level->Game.emplace();level->Game->SpawnZ=0;
    level->Hour=14;level->FogDensity=0;
    Character wall;wall.Shape="cube";wall.Id="ordinary-wall";wall.Name="Ordinary stone wall";
    wall.Z=-2;wall.Width=1.6;wall.Height=1.5;wall.Depth=.35;wall.Surface=4;wall.Color="#555D67";
    wall.DestructionHealth=25;level->Characters={wall};
    auto saved=level->encode();auto restored=SceneData::decode(saved);
    require(restored.SchemaVersion==16&&restored.Characters[0].Breakable&&restored.Characters[0].DestructionHealth==25&&restored.Characters[0].GameplayRole=="none","Independent scenery durability saves through schema13 without acquiring an interaction role");
    auto legacy=json(*level);legacy["Characters"][0].erase("Breakable");legacy["Characters"][0].erase("DestructionHealth");
    auto migrated=SceneData::decode(legacy.dump());require(migrated.Characters[0].Breakable&&migrated.Characters[0].DestructionHealth==10,"Legacy placed objects gain default breakability rather than being silently excluded");
    for(auto value:std::array<json,5>{json(0),json(-1),json(1e12),json("strong"),json(nullptr)}){
        auto data=json(*level);data["Characters"][0]["DestructionHealth"]=value;bool rejected=false;try{SceneData::decode(data.dump());}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}
        require(rejected,"Invalid scenery durability is rejected before gameplay");
    }
    for(auto value:std::array<json,3>{json(1),json("true"),json(nullptr)}){
        auto data=json(*level);data["Characters"][0]["Breakable"]=value;bool rejected=false;try{SceneData::decode(data.dump());}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}
        require(rejected,"Breakability requires an actual saved boolean");
    }
    auto templateObject=decode_object_template(encode_object_template(wall));auto group=decode_object_group(encode_object_group({wall}));
    require(templateObject.Breakable&&templateObject.DestructionHealth==25&&group[0].DestructionHealth==25,"Reusable objects and groups retain independent durability");
    auto player=std::make_unique<AdventurePlayer>();player->start(*level);
    player->apply_attack_hits({wall.Id,wall.Id},10);require(player->scene().Characters.size()==1&&player->destruction_health(wall.Id)==15,"One damage event accumulates scenery damage once even with duplicate target identities");
    auto revision=player->rules().revision;player->apply_attack_hits({wall.Id},0);player->apply_attack_hits({wall.Id},NAN);player->apply_attack_hits({"absent-object"},100);
    require(player->destruction_health(wall.Id)==15&&player->rules().revision==revision,"Zero, nonfinite and missing-target damage cannot destroy objects or fabricate events");
    player->apply_attack_hits({wall.Id},10);require(player->scene().Characters.size()==1&&player->destruction_health(wall.Id)==5,"Partially damaged scenery retains its original object until durability is exhausted");
    player->apply_attack_hits({wall.Id},10);auto debris=player->presentation();
    require(player->scene().Characters.empty()&&!debris.fragments.empty()&&std::all_of(debris.fragments.begin(),debris.fragments.end(),[](const auto& p){return bool(p.part);}),"An ordinary wall breaks into actual source-shaped parts without the legacy breakable role");
    require(!debris.dust.empty()&&!debris.grit.empty()&&level->encode()==saved,"Ordinary destruction retains mineral feedback and leaves authored scenery unchanged");
    player->start(*level);require(player->destruction_health(wall.Id)==25&&player->presentation().fragments.empty(),"Reset restores full scenery durability and clears transient fragments");
    auto protectedScene=*level;protectedScene.Characters[0].Breakable=false;player->start(protectedScene);player->apply_attack_hits({wall.Id},1e9);
    require(player->scene().Characters.size()==1&&player->presentation().fragments.empty()&&player->destruction_health(wall.Id)==25,"Explicit permanent scenery remains intact under damaging attacks");
    auto protectedSaved=SceneData::decode(protectedScene.encode());require(!protectedSaved.Characters[0].Breakable,"An explicit breakability opt-out survives scene saving");
    SceneHistory history;history.begin(protectedScene);protectedScene.Characters[0].Breakable=true;protectedScene.Characters[0].DestructionHealth=40;history.commit();
    require(history.undo(protectedScene)&&!protectedScene.Characters[0].Breakable&&protectedScene.Characters[0].DestructionHealth==25,"Undo restores independent breakability and durability in one authoring transaction");
    require(history.redo(protectedScene)&&protectedScene.Characters[0].Breakable&&protectedScene.Characters[0].DestructionHealth==40,"Redo restores both destruction fields without replacing the object");
    for(const auto& role:{"key","door","exit"}){
        auto authored=*level;authored.Characters[0].GameplayRole=role;authored.Characters[0].GameplayKey="test-key";
        GameplayRule rule;rule.Event="object-broken";rule.Subject=wall.Id;rule.Actions={{"complete-objective","scenery-broken"}};authored.Rules={rule};
        player->start(authored);player->apply_attack_hits({wall.Id},10);
        require(player->scene().Characters[0].GameplayRole==role&&player->rules().items.empty(),"Damage does not replace a key, door or exit interaction or grant collected keys");
        player->apply_attack_hits({wall.Id},20);require(player->scene().Characters.empty()&&player->rules().objectives.contains("scenery-broken")&&!player->finished&&player->rules().items.empty(),"Destroyed role objects emit actual break events without fabricating collection, opening or completion");
    }
    auto actors=*level;actors.Characters[0].CombatHealth=30;actors.Characters[0].CombatTeam=1;actors.Characters[0].Breakable=true;
    player->start(actors);player->apply_attack_hits({wall.Id},100,1);require(player->scene().Characters.size()==1&&player->destruction_health(wall.Id)==30,"Friendly actor protection takes precedence over independent breakability");
    actors.Characters[0].CombatTeam=2;actors.Characters[0].Breakable=false;player->start(actors);player->apply_attack_hits({wall.Id},30,1);
    require(player->scene().Characters.empty()&&player->presentation().fragments.empty(),"Combat health still defeats hostile actors when physical fragmentation is disabled");
    auto preview=*level;player->start(preview,false);player->apply_attack_hits({wall.Id},100);require(player->scene().Characters.size()==1,"Editor preview with gameplay disabled never applies damage");
    // NPC melee defeats must use the same fracture finish as projectiles and
    // player attacks, without mutating the scene during its attack iteration.
    auto duel=std::make_unique<SceneData>();duel->Game.emplace();duel->Game->Collision=false;duel->Game->SpawnZ=20;
    Character striker;striker.Shape="cube";striker.Id="destruction-striker";striker.CombatHealth=100;striker.CombatTeam=1;striker.CombatAutoAttack=true;
    Character victim=striker;victim.Id="destruction-victim";victim.Z=-2;victim.CombatHealth=10;victim.CombatTeam=2;victim.CombatAutoAttack=false;victim.DestructionHealth=1000;
    duel->Characters={striker,victim};auto duelSaved=duel->encode();
    GameplayRule death;death.Event="object-broken";death.Subject=victim.Id;death.Actions={{"complete-objective","duel-finished"}};duel->Rules={death};duelSaved=duel->encode();
    player->start(*duel);for(int i=0;i<190;++i)player->move(0,0,1./120.);
    auto defeat=player->presentation();
    require(player->scene().Characters.size()==1&&player->rules().removed.contains(victim.Id)&&player->destruction_health(victim.Id)==0,"NPC melee uses combat health and removes the defeated actor despite separate scenery durability");
    require(!defeat.fragments.empty()&&std::all_of(defeat.fragments.begin(),defeat.fragments.end(),[](const auto& f){return bool(f.part);})&&!defeat.dust.empty()&&!defeat.grit.empty(),"NPC melee defeat creates actual source fragments, mineral dust and grit through the shared finish");
    require(defeat.shatters==1&&player->rules().objectives.contains("duel-finished")&&duel->encode()==duelSaved,"NPC fracture signals one break and its authored rule while leaving the saved scene untouched");
    auto removedRevision=player->rules().revision;player->apply_attack_hits({victim.Id,victim.Id},100,1);
    require(player->presentation().shatters==1&&player->presentation().fragments.size()==defeat.fragments.size()&&player->rules().revision==removedRevision,"Repeated damage to an already defeated actor never duplicates fragments or removal events");
    player->start(*duel);require(player->presentation().shatters==0&&player->presentation().fragments.empty()&&player->presentation().dust.empty()&&player->combat_actors().at(victim.Id).health==10,"Reset restores NPC combat health and clears shared destruction feedback");
    duel->Characters[1].Breakable=false;player->start(*duel);for(int i=0;i<190;++i)player->move(0,0,1./120.);
    auto optedOut=player->presentation();
    require(player->scene().Characters.size()==1&&player->rules().removed.contains(victim.Id)&&optedOut.shatters==1&&optedOut.fragments.empty()&&optedOut.dust.empty()&&optedOut.grit.empty(),"NPC defeat retains removal and rule behavior when physical breakage is explicitly disabled");
    // Exercise all current placeable types through the same runtime route.
    json coverage=json::array();
    for(const auto& shape:shapes){
        auto scene=*level;scene.Characters[0].Shape=shape;scene.Characters[0].DestructionHealth=10;scene.Characters[0].Size=.65;
        player->start(scene);player->apply_attack_hits({wall.Id},10);auto view=player->presentation();bool rubble=destruction_has_rubble(scene.Characters[0]);
        require(player->scene().Characters.empty(),"Every current placed shape can be destroyed without changing its gameplay role");
        require(rubble?!view.fragments.empty()&&std::all_of(view.fragments.begin(),view.fragments.end(),[](const auto& f){return bool(f.part);}):view.fragments.empty()&&view.dust.empty(),"Solid object debris retains its source mesh and effect removal never creates proxy-box rubble");
        coverage.push_back({{"shape",shape},{"removed",true},{"fragments",view.fragments.size()},{"proxyOnlyRemoval",!rubble}});
    }
    // Sculpted and imported geometry use their real mesh, including open shells.
    auto sculpted=*level;sculpted.Characters[0].Shape.reset();sculpted.Characters[0].Sculpt=prepare_sculpt(shape_geometry(wall)[0].mesh);
    player->start(sculpted);player->apply_attack_hits({wall.Id},25);auto sculptPieces=player->presentation().fragments;require(player->scene().Characters.empty()&&!sculptPieces.empty()&&std::all_of(sculptPieces.begin(),sculptPieces.end(),[](const auto& f){return bool(f.part);}),"Independent sculpt destruction retains source geometry");
    auto imported=*level;imported.Characters[0].Shape.reset();imported.Characters[0].Asset=import_model(fs::absolute("migration/fixtures/triangle.glb"));
    player->start(imported);player->apply_attack_hits({wall.Id},25);auto importedPieces=player->presentation().fragments;
    require(player->scene().Characters.empty()&&!importedPieces.empty()&&std::all_of(importedPieces.begin(),importedPieces.end(),[](const auto& f){return f.part&&f.part->mesh.indices.size()>=24;}),"An imported open panel becomes source-shaped closed shell fragments instead of placeholder cubes");
    // Actual attack timing and projectile routing, not just direct damage calls.
    auto attack=*level;attack.Characters[0].DestructionHealth=10;player->start(attack);player->fire_button(true);player->fire_button(false);for(int i=0;i<330;++i)player->move(0,0,1./120.);
    require(player->scene().Characters.empty()&&!player->presentation().fragments.empty(),"Manual basic attack breaks ordinary scenery through the real hit path");
    auto packet=[&](const char* name){auto p=player->presentation();p.showBody=p.showWeapons=p.showTelegraph=false;auto bytes=render_scene(player->scene(),nullptr,player_presentation(p));atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));};
    packet("ordinary-wall-broken");for(int i=0;i<120;++i)player->move(0,0,1./120.);packet("ordinary-wall-moving");
    player->start(attack);packet("ordinary-wall-intact");atomic_write(folder/"ordinary-wall.starscene",attack.encode());
    attack.Game->ProjectileSpeed=20;attack.Characters[0].Height=2;player->start(attack);player->fire_button(true);player->fire_button(false);for(int i=0;i<360;++i)player->move(0,0,1./120.);
    require(player->scene().Characters.empty(),"Traveling player projectiles damage ordinary scenery with no special interaction role");
    auto cover=attack;Character behind=wall;behind.Id="behind-breakable-cover";behind.Z=-5;behind.Height=2;behind.DestructionHealth=25;cover.Characters.push_back(behind);
    player->start(cover);player->fire_button(true);player->fire_button(false);for(int i=0;i<360;++i)player->move(0,0,1./120.);
    require(player->scene().Characters.size()==1&&player->scene().Characters[0].Id==behind.Id&&player->destruction_health(behind.Id)==25,"A shot stops at breakable cover even when that impact destroys the cover");
    auto storm=*level;storm.Characters[0].DestructionHealth=10;Character bolt;bolt.Shape="chaos-lightning";bolt.Id="wall-lightning";bolt.Z=-2;bolt.Y=4;bolt.LightningTargetId=wall.Id;bolt.TargetX=bolt.TargetZ=0;bolt.TargetY=.7;bolt.BranchSpread=0;bolt.LightningDamage=10;storm.Characters.push_back(bolt);
    player->start(storm);for(int i=0;i<50;++i)player->move(0,0,1./120.);
    require(player->scene().Characters.size()==1&&player->scene().Characters[0].Id==bolt.Id,"Lightning damages ordinary scenery while retaining its emitter");
    storm.Characters[0].Shape="fire";storm.Characters[0].Size=.5;player->start(storm);for(int i=0;i<50;++i)player->move(0,0,1./120.);
    require(player->scene().Characters.size()==1&&player->scene().Characters[0].Id==bolt.Id&&player->presentation().dust.empty(),"Explicit lightning targets can destroy effect sources without inventing mineral rubble");
    atomic_write(folder/"object-destruction-coverage.json",json{{"shapes",coverage},{"terrainDestruction","unfinished"},{"vegetationFragments","unfinished"},{"unsupportedMeshFallback","legacy generic debris; refinement required"},{"scope","General saved object breakability and runtime attack/removal routing; not final visual acceptance"}}.dump(2));
}
