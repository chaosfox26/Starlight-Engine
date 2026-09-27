#include "commands.hpp"
#include "geometry.hpp"
#include "mesh.hpp"
#include "platform.hpp"
#include "render_scene.hpp"
#include "assets.hpp"
#include "interactions.hpp"
#include "object_template.hpp"
#include "asset_library.hpp"
#include "adventure.hpp"
#include "grapple_motion.hpp"
#include "player_presentation.hpp"
#include "gameplay_audio.hpp"
#include "combat_control.hpp"
#include "quest_model.hpp"
#include "object_behaviors.hpp"
#include "adventure_template.hpp"
#include "tutorial_progress.hpp"
#include <cstring>
#include <iostream>
using namespace starlight::editor;
int main(int argc, char **argv) {
    std::vector<std::string> checks;
    auto require = [&](bool ok, const char *name) {
        if (!ok)
            throw std::runtime_error(name);
        checks.emplace_back(name);
    };
    fs::path folder = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "Starlight-native-checks";
    fs::create_directories(folder);
    try {
        {
            MeshPart a;a.color={.2,.4,.6};a.mesh.vertices={{{0,0,0}},{{1,0,0}},{{0,1,0}}};a.mesh.indices={0,1,2};
            auto b=a;b.mesh.vertices[0].position.z=1;auto glow=a;glow.emissive=true;
            auto merged=batch_presentation({a,b,glow});
            require(merged.size()==2 && merged[0].mesh.vertices.size()==6 && merged[0].mesh.indices==std::vector<int>({0,1,2,3,4,5}) && merged[0].mesh.vertices[3].position.z==1 && merged[1].emissive,"Presentation batching preserves triangle offsets, world positions and emissive separation");
            AdventurePlayer::Presentation p;p.showBody=false;p.showTelegraph=true;p.attackAim={0,0,-1};p.channelProgress=.5;
            auto cone=player_presentation(p);for(auto& part:cone)part.mesh.validate();
            require(cone.size()==2,"Moving cone uses two material batches instead of one allocation per edge");
        }
        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            AdventurePlayer player;player.start(level);
            player.cameraDistance=0;
            auto initial=player.presentation();
            require(!initial.showBody && initial.showWeapons && !player_presentation(initial).empty(),"First person shows dual pistols without a body");
            player.fire_button(true);player.fire_button(false);
            for(int i=0;i<49;++i)player.move(0,0,.05);
            require(player.presentation().shots==0 && player.presentation().shotFlash==0,"Weapon effects wait for the committed attack to resolve");
            player.move(0,0,.05);
            require(player.presentation().shots==1 && player.presentation().shotFlash>0,"Resolved shot emits one event and a muzzle flash even on a miss");
            for(int i=0;i<5;++i)player.move(0,0,.05);
            require(player.presentation().shots==1 && player.presentation().shotFlash==0,"Released attack does not repeat and muzzle flash expires");
            player.fire_button(true);player.move(0,0,.05);player.cancel_mobility_input();
            for(int i=0;i<60;++i)player.move(0,0,.05);
            require(player.presentation().shots==1,"Cancelled windup produces no phantom sound or muzzle flash");
            player.start(level);
            require(player.presentation().epoch!=initial.epoch && player.presentation().shots==0 && player.presentation().shotFlash==0,"Restart clears presentation events and changes the audio epoch");
            player.start(level,false);player.cameraDistance=0;
            require(!player.presentation().showWeapons && player_presentation(player.presentation()).empty(),"Authoring exploration does not introduce combat weapons");
            for(auto cue:{GameplayCue::Shot,GameplayCue::Shatter,GameplayCue::Impact,GameplayCue::Chirp}){
                auto pcm=gameplay_sound(cue);long energy=0;int peak=0;
                for(auto sample:pcm){peak=std::max(peak,std::abs(int(sample)));energy+=std::abs(int(sample));}
                require(!pcm.empty() && pcm.front()==0 && std::abs(int(pcm.back()))<2 && peak<7000 && energy>10000,"Synthesized placeholder cue is audible data with bounded gain and click-free endpoints");
            }
        }
        {
            GrappleMotion g;MovementResources resources;g.press(resources,true,{0,0,0},{0,0,-20},{0,0,1},false);
            for(int i=0;i<15;i++)g.step({0,0,0},{},1./60.);
            require(g.flying() && std::abs(g.hook_position().z+10)<.001,"Hook travels forty metres per second before attachment");
            g.release();require(g.state==GrappleMotion::State::Free && !g.flying(),"Releasing during hook flight cancels attachment");
            SceneData s;Character c;c.GrappleMode=2;s.Characters={c};require(SceneData::decode(s.encode()).Characters[0].GrappleMode==2,"Creator grapple mode survives scene round trip");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;level.Terrain.emplace();
            for(int z=0;z<TerrainData::N;z++)for(int x=0;x<TerrainData::N;x++)level.Terrain->Heights[z*TerrainData::N+x]=2+(x*TerrainData::Step-TerrainData::Half)*.1;
            Character enemy;enemy.Shape="cube";enemy.Id="slope-npc";enemy.X=-5;enemy.Y=1.5;enemy.CombatHealth=100;enemy.CombatPursuit=true;level.Characters={enemy};
            AdventurePlayer p;p.start(level);for(int i=0;i<300;i++)p.move(0,0,1./120.);
            auto c=p.scene().Characters[0];require(c.X>-3 && std::abs(c.Y-level.Terrain->height_at(c.X,c.Z))<.03,"Pursuing NPC follows walkable terrain elevation");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=10;
            Character a;a.Shape="cube";a.Id="patrolling-npc";a.CombatHealth=100;a.Speed=2;a.Route={{8,0}};level.Characters={a};auto saved=level.encode();
            AdventurePlayer p;p.start(level);for(int i=0;i<120;i++)p.move(0,0,1./120.);double before=p.scene().Characters[0].X;
            require(before>1 && before<2.1,"Combat NPC follows authored route through runtime steering");
            p.control_actor(a.Id,ControlEffect::Stun,{1,true});for(int i=0;i<120;i++)p.move(0,0,1./120.);
            require(std::abs(p.scene().Characters[0].X-before)<.001,"Stun pauses authored NPC route");
            for(int i=0;i<150;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].X>before && p.scene().Characters[0].X<before+1,"NPC resumes route without catch-up teleport");
            require(level.encode()==saved,"Runtime route control preserves authored route");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.Id="launched-npc";enemy.Z=-3;enemy.CombatHealth=100;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.control_actor(enemy.Id,ControlEffect::Launch,{1,true});for(int i=0;i<60;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Y>1 && p.scene().Characters[0].Z<-5,"NPC launch physically lifts and throws actor backward");
            for(int i=0;i<100;i++)p.move(0,0,1./120.);
            require(std::abs(p.scene().Characters[0].Y)<.01 && p.combat_actors().at(enemy.Id).control.active==ControlEffect::Launch,"NPC launch lands before its recovery timer ends");
            for(int i=0;i<150;i++)p.move(0,0,1./120.);
            require(!p.combat_actors().at(enemy.Id).control.active,"NPC launch releases control after landing recovery");
            enemy.Z=-8;level.Characters={enemy};p.start(level);p.control_actor(enemy.Id,ControlEffect::Tether,{1,true});for(int i=0;i<70;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z>=-5.6,"NPC tether pulls actor back inside attachment boundary");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character a;a.Shape="cube";a.Id="taunter";a.Z=-4;a.CombatHealth=100;Character b=a;b.Id="hidden-target";b.X=1;level.Characters={a,b};
            AdventurePlayer p;p.start(level);p.select_target();if(p.selected_target()!=b.Id)p.select_target();
            p.apply_control(ControlEffect::Taunt,{combat_identity(a.Id),false},true);p.move(0,0,.01);
            require(p.selected_target().empty() && !p.combat_display().hasTarget,"Taunt clears hidden target marker and health display");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;Character enemy;enemy.Shape="cube";enemy.CombatHealth=100;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.control_actor(enemy.Id,ControlEffect::Taunt,{999999,false});p.move(0,0,.01);
            require(!p.combat_actors().at(enemy.Id).control.active,"NPC taunt clears when its source no longer exists");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=20;
            Character a;a.Shape="cube";a.Id="npc-finisher";a.CombatHealth=100;a.CombatTeam=1;a.CombatAutoAttack=true;
            Character b=a;b.Id="npc-victim";b.CombatHealth=10;b.CombatTeam=2;b.CombatAutoAttack=false;b.Z=-2;level.Characters={a,b};auto authored=level.encode();
            AdventurePlayer p;p.start(level);for(int i=0;i<190;i++)p.move(0,0,1./120.);
            require(p.rules().removed.contains(b.Id) && p.scene().Characters.size()==1,"NPC kill removes defeated runtime actor and collision target");
            require(level.encode()==authored,"NPC death leaves authored scene untouched");
            p.start(level);require(p.scene().Characters.size()==2 && p.combat_actors().at(b.Id).health==10,"Restart restores NPC defeated by another NPC");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.Id="displaced-npc";enemy.Z=-5;enemy.CombatHealth=100;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.control_actor(enemy.Id,ControlEffect::Knockback,{1,true});for(int i=0;i<60;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z<-7.8,"NPC knockback physically displaces away from source");
            p.start(level);p.control_actor(enemy.Id,ControlEffect::Pull,{1,true});for(int i=0;i<60;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z>=-1.1 && p.scene().Characters[0].Z<=-1,"NPC pull stops near source without snapping onto it");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.Id="feared-npc";enemy.Z=-3;enemy.CombatHealth=100;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.control_actor(enemy.Id,ControlEffect::Fear,{1,true});for(int i=0;i<120;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z<-4,"Fear drives NPC away from source without requiring pursuit setting");
            Character wall;wall.Shape="cube";wall.Z=-5;wall.Width=8;wall.Height=4;level.Characters.push_back(wall);p.start(level);p.control_actor(enemy.Id,ControlEffect::Fear,{1,true});
            for(int i=0;i<210;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z>-4.1,"NPC fear respects obstructing wall");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=20;
            Character friendly;friendly.Shape="cube";friendly.Id="friendly";friendly.CombatTeam=1;friendly.CombatHealth=100;friendly.CombatAutoAttack=true;
            Character enemy=friendly;enemy.Id="hostile";enemy.CombatTeam=2;enemy.CombatSightRange=20;enemy.Z=-2;level.Characters={friendly,enemy};
            AdventurePlayer p;p.start(level);for(int i=0;i<190;i++)p.move(0,0,1./120.);
            require(p.combat_actors().at(friendly.Id).health==90 && p.combat_actors().at(enemy.Id).health==90,"Friendly and enemy NPCs exchange Swipe damage");
            require(p.health()==100,"NPC exchange does not damage distant player");
            p.start(level);p.control_actor(enemy.Id,ControlEffect::Taunt,{1,true});
            auto target=p.actor_target(enemy,p.combat_actors().at(enemy.Id));
            require(!target,"Taunted NPC does not substitute a nearby friendly for out-of-range taunter");
            p.position.z=4;target=p.actor_target(enemy,p.combat_actors().at(enemy.Id));
            require(target && target->id.empty(),"Taunted NPC selects player source instead of nearer friendly");
        }

        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            level.Terrain.emplace();for(auto& h:level.Terrain->Heights)h=3;
            Character enemy;enemy.Shape="cube";enemy.Z=-2;enemy.CombatHealth=100;enemy.CombatAutoAttack=true;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.fire_button(true);p.fire_button(false);
            for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.health()==100 && p.combat_actors().at(enemy.Id).health==100,"Terrain blocks both player attack and enemy Swipe");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;AdventurePlayer p;p.start(level);
            for(unsigned key: {'2','3','4','5','R'}){p.ability_button(key,true);p.move(0,0,.01);require(!p.basic_attack().winding,"Unassigned ability slot remains disabled");}
            p.ability_button('1',true);p.ability_button('1',false);p.move(0,0,.01);
            require(p.basic_attack().winding,"Basic attack runs through shared slot dispatch");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.Z=-2;enemy.CombatHealth=100;enemy.CombatAutoAttack=true;level.Characters={enemy};
            for(int effect=0;effect<14;effect++){
                level.Characters[0].CombatControlEffect=effect;AdventurePlayer p;p.start(level);
                for(int i=0;i<180;i++)p.move(0,0,1./120.);
                require(p.crowd_control().active==static_cast<ControlEffect>(effect),"Authored enemy swipe applies selected crowd control after landed channel");
                require(SceneData::decode(level.encode()).Characters[0].CombatControlEffect==effect,"Authored control effect survives scene round trip");
            }
            level.Characters[0].CombatTeam=1;AdventurePlayer p;p.start(level);for(int i=0;i<180;i++)p.move(0,0,1./120.);
            require(!p.crowd_control().active && p.health()==100,"Friendly swipe never damages or controls player");
        }
        {
            HitChance first,second;int hits=0;bool repeatable=true;
            for(int i=0;i<10000;i++){bool a=first.roll(.5);hits+=a;repeatable&=a==second.roll(.5);}
            require(repeatable && hits>4800 && hits<5200,"Blind seeded hit rolls halve hit probability repeatably");
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character a;a.Shape="cube";a.Z=-3;a.CombatHealth=100;Character b=a;b.Id="other-enemy";b.X=1;level.Characters={a,b};
            AdventurePlayer p;p.start(level);p.apply_control(ControlEffect::Taunt,{combat_identity(a.Id),false},true);p.move(0,0,.01);
            require(p.visible_scene().Characters.size()==1 && p.scene().Characters.size()==2,"Taunt hides other enemies without removing collision scene");
            p.select_target();require(p.selected_target()==a.Id,"Taunt selects only its source");
            p.healing_cast(b.Id,true);p.move(0,0,.01);
            require(p.presentation().actorTelegraphs.empty(),"Taunt hides other enemy healing cones");
            require(!p.unobstructed({1,1,0},{1,1,-6},"",a.Id),"Hidden enemy remains a physical healing obstruction");
            p.cleanse_control();require(p.visible_scene().Characters.size()==2,"Removing taunt restores enemy awareness");
            require(p.presentation().actorTelegraphs.size()==1,"Removing taunt restores active healing cone");

            p.start(level);p.apply_control(ControlEffect::Blind,{9,true},true);Character distantActor=a;distantActor.Z=-40;
            require(p.sight_range()==30 && !p.perceived(distantActor) && p.perceived(a),"Blind halves sight distance while preserving near targets");
            p.cleanse_control();require(p.sight_range()==60 && p.perceived(distantActor),"Cleanse restores sight range");
            level.Game->SightRange=20;a.CombatSightRange=18;a.CombatPursuit=true;a.Z=-15;level.Characters={a};
            auto restored=SceneData::decode(level.encode());require(restored.Game->SightRange==20 && restored.Characters[0].CombatSightRange==18,"Player and NPC sight ranges survive scene save and load");
            p.start(restored);p.control_actor(a.Id,ControlEffect::Blind,{1,true});for(int i=0;i<120;i++)p.move(0,0,1./120.);
            require(std::abs(p.scene().Characters[0].Z+15)<.001,"Blinded NPC cannot pursue beyond half its configured sight range");
            for(int i=0;i<360;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z>-14,"NPC resumes pursuit within normal sight range after Blind ends");
            p.apply_control(ControlEffect::Blind,{9,true},true);require(p.sight_range()==10,"Player Blind halves the configured baseline instead of a fixed constant");
        }
        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;AdventurePlayer p;p.start(level);p.take_damage(40);
            p.healing_cast("",true);p.move(0,0,.05);p.cancel_mobility_input();for(int i=0;i<240;i++)p.move(0,0,1./120.);
            require(p.health()==60 && p.presentation().actorTelegraphs.empty(),"Focus cancellation clears player healing without delayed recovery");
            p.healing_cast("",true);p.apply_control(ControlEffect::Silence,{7,true},true);for(int i=0;i<180;i++)p.move(0,0,1./120.);
            require(p.health()==60,"Silence blocks non-basic healing ability");
            Character wall;wall.Shape="cube";wall.Z=-1;wall.Width=5;wall.Height=4;level.Characters={wall};p.start(level);
            require(!p.unobstructed({0,1,0},{0,1,-2},"",""),"Healing line of sight respects intervening wall");
        }
        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;Character enemy;enemy.Shape="cube";enemy.CombatHealth=100;enemy.Z=-2;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.take_damage(40);p.healing_cast("",true);p.move(0,0,.05);
            require(p.presentation().actorTelegraphs.back().kind==TelegraphKind::Healing,"Player healing publishes green cone");
            for(int i=0;i<180;i++)p.move(0,0,1./120.);
            require(p.health()==80 && p.combat_actors().at(enemy.Id).health==100,"Player healing restores health without healing hostile actor");
            p.healing_cast("",false);p.healing_cast(enemy.Id,true);p.move(0,0,.05);
            bool orange=false;for(auto t:p.presentation().actorTelegraphs)orange|=t.kind==TelegraphKind::EnemyHealing;
            require(orange,"Enemy healing publishes orange cone");
            p.start(level);require(p.presentation().actorTelegraphs.empty(),"Reset clears healing channels");
        }
        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;AdventurePlayer p;p.start(level);
            p.fire_button(true);p.fire_button(false);for(int i=0;i<60;i++)p.move(0,0,1./120.);
            auto before=p.presentation();p.position.x+=2;p.yaw=.7;p.pitch=.1;p.cameraYaw=-1;
            auto after=p.presentation();
            require((after.attackOrigin-before.attackOrigin-Vec3{2,0,0}).length()<1e-8 && after.channelProgress==before.channelProgress,"Player cone follows caster translation without resetting channel");
            require((after.attackAim-p.attack_direction()).length()<1e-8 && after.attackAim.x>0,"Player cone follows character facing independently of camera orbit");
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            Character enemy;enemy.Shape="cube";enemy.CombatHealth=100;enemy.CombatAutoAttack=true;level.Characters={enemy};
            AdventurePlayer p;
            for(double degrees:{12.,15.,18.,90.,145.,230.,315.}){
                auto angle=degrees*3.141592653589793/180;p.start(level);p.position={2*std::sin(angle),1.7,2*std::cos(angle)};
                for(int i=0;i<60;i++)p.move(0,0,1./120.);auto a=p.presentation().actorTelegraphs;
                require(a.size()==1 && (a[0].aim-Vec3{std::sin(angle),0,std::cos(angle)}).length()<1e-8,"Enemy telegraph aims at target at arbitrary full-circle heading");
            }
            p.start(level);p.position={0,1.7,2};for(int i=0;i<90;i++)p.move(0,0,1./120.);
            p.position={2,1.7,0};p.move(0,0,1./120.);auto a=p.presentation().actorTelegraphs;
            require(a.size()==1 && a[0].aim.x>0 && a[0].aim.x<.06 && a[0].progress>.5,"Active enemy cone turns gradually without restarting fill");
            for(int i=0;i<29;i++)p.move(0,0,1./120.);
            require((p.presentation().actorTelegraphs[0].aim-Vec3{1,0,0}).length()<1e-8,"Enemy cone reaches new aim through bounded turning");
            for(int i=0;i<60;i++)p.move(0,0,1./120.);

            require(p.health()==90,"Enemy hit resolves using updated advertised aim");
        }
        {
            GroundSteering steering;Vec3 at{},target{7,0,-11};double maximum=0;
            for(int i=0;i<600;i++){auto delta=steering.step(at,target,1./120.,2.5,2);maximum=std::max(maximum,delta.length());at=at+delta;}
            require(maximum<=2.5/120.+1e-9 && (at.unit()-target.unit()).length()<1e-8,"Pursuit keeps continuous heading with speed-bounded displacement");
            for(int i=0;i<1200;i++)at=at+steering.step(at,target,1./120.,2.5,2);
            require(std::abs((target-at).length()-2)<.01,"Pursuit brakes at stable attack spacing");
            auto previous=at;at=at+steering.step(at,{-100,0,100},10,2.5,2);
            require((at-previous).length()<=.125+1e-9,"Target reversal and frame stall cannot teleport pursuer");
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            Character enemy;enemy.Shape="cube";enemy.X=3;enemy.Z=-8;enemy.CombatHealth=100;enemy.CombatPursuit=true;level.Characters={enemy};auto saved=level.encode();
            AdventurePlayer p;p.start(level);Vec3 last{enemy.X,enemy.Y,enemy.Z};bool bounded=true;
            for(int i=0;i<600;i++){p.move(0,0,1./120.);auto c=p.scene().Characters[0];Vec3 next{c.X,c.Y,c.Z};bounded&=(next-last).length()<=2.5/120.+1e-8;last=next;}
            require(bounded && std::hypot(last.x,last.z)<2.1,"Runtime enemy follows smoothly and stops near player");
            require(level.encode()==saved,"Pursuit never changes authored enemy positions");
            p.start(level);require(p.scene().Characters[0].X==3 && p.scene().Characters[0].Z==-8,"Reset restores original enemy position");
            Character wall;wall.Shape="cube";wall.Width=20;wall.Height=5;wall.Depth=.5;wall.Z=-4;level.Characters.push_back(wall);p.start(level);
            bool crossedWall=false;last={enemy.X,enemy.Y,enemy.Z};bounded=true;
            for(int i=0;i<1200;i++){p.move(0,0,1./120.);const auto& moved=p.scene().Characters[0];auto b=object_bounds(moved),w=object_bounds(wall);crossedWall|=b.low.x<w.high.x && b.high.x>w.low.x && b.low.z<w.high.z && b.high.z>w.low.z;Vec3 next{moved.X,moved.Y,moved.Z};bounded&=(next-last).length()<=2.5/120.+1e-8;last=next;}
            require(!crossedWall && bounded,"Pursuit routes around wall without intersecting it or teleporting");
            wall.Width=4;level.Characters[1]=wall;level.Characters[0].X=0;p.start(level);
            for(int i=0;i<1200;i++)p.move(0,0,1./120.);
            require(std::hypot(p.scene().Characters[0].X,p.scene().Characters[0].Z)<2.2,"Pursuit reaches attack distance around intervening obstacle");

        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            for(double degrees:{12.,15.,18.,47.,123.,271.}){
                double angle=degrees*3.141592653589793/180;
                AdventurePlayer p;p.start(level);p.yaw=angle;auto start=p.position;
                for(int i=0;i<120;i++)p.move(0,1,1./120.);
                auto delta=p.position-start;auto expected=Vec3{std::sin(angle),0,-std::cos(angle)};
                require((delta.unit()-expected).length()<1e-8,"Forward movement preserves arbitrary world heading without compass snapping");
                p.start(level);start=p.position;for(int i=0;i<120;i++)p.move(std::sin(angle),std::cos(angle),1./120.);
                delta=p.position-start;
                require((delta.unit()-expected).length()<1e-8 && std::abs(delta.length()-level.Game->MoveSpeed)<1e-8,"Continuous directional input preserves arbitrary angle and movement speed");
            }
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            Character enemy;enemy.Shape="cube";enemy.Z=-2;enemy.CombatHealth=100;enemy.CombatAutoAttack=true;level.Characters={enemy};
            AdventurePlayer p;p.start(level);
            for(int i=0;i<90;i++)p.move(0,0,1./120.);
            auto visual=p.presentation();
            require(p.health()==100 && visual.actorTelegraphs.size()==1 && visual.actorTelegraphs[0].kind==TelegraphKind::Enemy,"Enemy swipe advertises red channel before damage");
            require(std::abs(visual.actorTelegraphs[0].progress-.5)<1e-8,"Swipe fills halfway after three quarters of a second");
            for(int i=0;i<90;i++)p.move(0,0,1./120.);
            require(p.health()==90,"Enemy swipe hits after one and a half seconds");
            for(int i=0;i<180;i++)p.move(0,0,1./120.);
            require(p.health()==80,"Enemy swipe repeats complete cycles");
            p.start(level);for(int i=0;i<90;i++)p.move(0,0,1./120.);p.position.x=4;
            for(int i=0;i<100;i++)p.move(0,0,1./120.);
            require(p.health()==100,"Moving outside advertised swipe avoids damage");
            p.start(level);p.control_actor(enemy.Id,ControlEffect::Stun,{1,true});for(int i=0;i<180;i++)p.move(0,0,1./120.);
            require(p.health()==100 && p.presentation().actorTelegraphs.empty(),"Stun blocks enemy swipe and telegraph");
            level.Characters[0].CombatTeam=1;p.start(level);for(int i=0;i<360;i++)p.move(0,0,1./120.);
            require(p.health()==100,"Friendly actor never swipes player");
            require(SceneData::decode(level.encode()).Characters[0].CombatAutoAttack,"Enemy attack setting persists");
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            Character enemy;enemy.Shape="cube";enemy.Z=-4;enemy.Y=1;enemy.CombatHealth=100;enemy.CombatTeam=2;enemy.CombatBoss=true;level.Characters={enemy};
            auto saved=level.encode();auto restored=SceneData::decode(saved);
            require(restored.SchemaVersion==12 && restored.Characters[0].CombatHealth==100 && restored.Characters[0].CombatBoss,"Combat actor and boss settings round-trip through scene schema");
            AdventurePlayer p;p.start(restored);p.select_target();p.fire_button(true);p.fire_button(false);for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.combat_actors().at(enemy.Id).health==90 && p.scene().Characters.size()==1,"Basic attack damages combat actor without prematurely removing it");
            for(int i=0;i<4;i++)p.control_actor(enemy.Id,ControlEffect::Stun,{1,true});
            require(p.combat_actors().at(enemy.Id).bar.phase==BossBreakBar::Phase::Staggered,"Repeated player CC breaks authored boss bar");
            p.fire_button(true);p.fire_button(false);for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.combat_actors().at(enemy.Id).health==77.5,"Boss stagger increases real attack damage by twenty-five percent");
            require(p.heal_actor(enemy.Id,20,1)==0 && p.heal_actor(enemy.Id,20,2)==20,"Healing respects actor affiliation");
            require(restored.encode()==saved,"Runtime actor health and break bar do not change authored scene");
            p.start(restored);require(p.combat_actors().at(enemy.Id).health==100 && p.combat_actors().at(enemy.Id).bar.strength==1000,"Reset restores boss health and break strength");
            restored.Characters[0].CombatTeam=1;p.start(restored);p.select_target();
            require(p.selected_target().empty() && !p.control_actor(enemy.Id,ControlEffect::Stun,{1,true}),"Friendly combat actors reject hostile selection and CC");
        }
        {
            for(auto kind:{TelegraphKind::Friendly,TelegraphKind::Enemy,TelegraphKind::Healing,TelegraphKind::EnemyHealing}){
                AdventurePlayer::Presentation p;p.showBody=false;p.showTelegraph=true;p.attackAim={0,0,-1};p.channelProgress=.5;p.telegraphKind=kind;
                auto parts=player_presentation(p);auto expected=telegraph_color(kind);
                require(!parts.empty() && (parts.front().color-expected).length()<1e-9 && (parts.back().color-expected*.4).length()<1e-9,"Telegraph category colors both cone outline and channel fill");
                for(const auto& part:parts)part.mesh.validate();
                double radius=0;for(const auto& v:parts.back().mesh.vertices)radius=std::max(radius,std::hypot(v.position.x,v.position.z));
                require(std::abs(radius-10)<1e-8,"Half-channel fill reaches half cone range");
            }
        }
        {
            TerrainData slope;
            for(int z=0;z<TerrainData::N;z++)for(int x=0;x<TerrainData::N;x++)slope.Heights[z*TerrainData::N+x]=-(z*TerrainData::Step-TerrainData::Half)*.2;
            AdventurePlayer::Presentation p;p.showBody=false;p.showTelegraph=true;p.attackAim={0,0,-1};p.channelProgress=.5;p.telegraphTerrain=&slope;
            auto parts=player_presentation(p);bool follows=true;double reach=0;
            for(const auto& v:parts.back().mesh.vertices){follows=follows && std::abs(v.position.y-slope.height_at(v.position.x,v.position.z)-telegraph_elevation(p.telegraphKind))<1e-8;reach=std::max(reach,std::hypot(v.position.x,v.position.z));}
            require(follows && std::abs(reach-10)<1e-8,"Channel fill conforms to sloped terrain while preserving horizontal progress");
            Vec3 contact{};
            require(surface_telegraph_contact(&slope,{0,0,0},{0,0,-1},{-.2,3,-15.2},{.2,4.8,-14.8},contact) && contact.y>=3,"Ground cone hits an uphill target above the caster plane");
            require(!surface_telegraph_contact(&slope,{0,0,0},{0,0,-1},{-.2,0,-15.2},{.2,1,-14.8},contact),"Ground cone rejects a target below the sloping surface");
            auto airborne=telegraph_surface(&slope,{0,5,0},{0,0,-1},{0,5,-10});
            require(airborne.y==5,"Airborne cone retains its aimed plane instead of snapping to terrain");
        }
        {
            BasicAttack a;a.press(true);int shots=0;
            for(int i=0;i<600;i++)shots+=a.advance(1./120.,false);
            require(shots==2,"Held basic attack repeats complete two-and-a-half-second cycles");
            a={};a.press(true);a.advance(.05,false);a.press(false);
            require(!a.advance(.05,false) && a.winding,"Releasing attack preserves the active firing cycle");
            shots=0;for(int i=0;i<600;i++)shots+=a.advance(1./120.,false);
            require(shots==1 && !a.winding,"Tap completes exactly one firing cycle without repeating");
            a.press(true);a.advance(.05,false);a.advance(.05,true);
            require(!a.winding,"Action block cancels attack windup");
            require(inside_basic_telegraph({0,2,0},{0,0,-1},{.9,2,-19}) && !inside_basic_telegraph({0,2,0},{0,0,-1},{2,2,-5}),"Cone telegraph widens from its origin and respects range");
            Vec3 contact{};
            require(telegraph_contact({0,0,0},{0,0,-1},{1.1,.2,-5.2},{2,1.8,-4.8},contact) && inside_basic_telegraph({0,0,0},{0,0,-1},contact),"Cone catches partial target side overlap and returns a valid contact");
            require(!telegraph_contact({0,0,0},{0,0,-1},{2,.2,-5.2},{3,1.8,-4.8},contact),"Cone rejects a wholly outside target");
            require(telegraph_contact({0,0,0},{0,0,-1},{-.5,.2,-21},{.5,1.8,-19.5},contact) && !telegraph_contact({0,0,0},{0,0,-1},{-.5,.2,-22},{.5,1.8,-20.1},contact),"Cone range accepts partial overlap but rejects targets beyond its edge");
            require(!telegraph_contact({0,0,0},{0,0,-1},{-.5,2.1,-5},{.5,3,-4},contact) && telegraph_contact({0,0,0},{0,0,-1},{-30,-1,-30},{30,4,30},contact),"Cone respects height and detects an enclosing target");
            auto level=SceneData{};level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            Character left;left.Shape="cube";left.GameplayRole="breakable";left.X=-.6;left.Y=1;left.Z=-4;left.Width=.4;
            Character right=left;right.Id="second-attack-target";right.X=.6;level.Characters={left,right};auto original=level.encode();
            AdventurePlayer p;p.start(level);p.select_target();for(int i=0;i<60;i++)p.move(0,0,1./120.);
            require(!p.selected_target().empty() && p.scene().Characters.size()==2,"Tab selects without automatically firing");
            p.fire_button(true);for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.scene().Characters.empty() && level.encode()==original,"Manual area attack breaks both eligible targets and preserves authoring");
            require(p.presentation().fragments.size()==32,"Two broken targets emit sixteen cosmetic fragments each");
            auto partialLevel=level;partialLevel.Characters={left};partialLevel.Characters[0].X=1.45;partialLevel.Characters[0].Z=-5;partialLevel.Characters[0].Width=.8;
            AdventurePlayer partialPlayer;partialPlayer.start(partialLevel);partialPlayer.fire_button(true);partialPlayer.fire_button(false);
            for(int i=0;i<310;i++)partialPlayer.move(0,0,1./120.);
            require(partialPlayer.scene().Characters.empty(),"Actual attack breaks a target whose edge enters the cone while its centre remains outside");
            p.fire_button(false);for(int i=0;i<300;i++)p.move(0,0,1./120.);
            require(p.presentation().fragments.empty(),"Cosmetic shatter fragments expire after two seconds");
            Character wall;wall.Shape="cube";wall.Width=5;wall.Height=4;wall.Depth=.2;wall.Z=-2;level.Characters.push_back(wall);
            p.start(level);p.fire_button(true);for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.scene().Characters.size()==3,"Solid wall blocks aimed attacks to targets behind it");
            level.Characters.pop_back();p.start(level);p.select_target();p.yaw=p.cameraYaw=1.5;p.fire_button(true);
            for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.scene().Characters.size()==2,"Selecting a target never redirects an off-target attack");
            p.start(level);p.fire_button(true);p.move(0,0,.05);p.dodge(1,0);p.move(0,0,.05);
            require(!p.basic_attack().winding && p.scene().Characters.size()==2,"Dodge cancels an unfinished shot before targets are hit");
        }
        {
            CombatControl c;c.player=true;ControlSource first{1,true},other{2,true};
            require(c.apply(ControlEffect::Stun,first,false)==CombatControl::Result::Rejected && !c.active,"Friendly CC is rejected without immunity");
            require(c.apply(ControlEffect::Stun,first,true,true)==CombatControl::Result::Rejected && !c.active,"Self CC is rejected without immunity");
            c.apply(ControlEffect::Stun,first,true);
            require(c.blocks_basic() && c.blocks_mobility(),"Stun locks attacks and voluntary mobility");
            require(c.apply(ControlEffect::Fear,other,true)==CombatControl::Result::Rejected && c.remaining==2,"Other attacker cannot replace or extend active CC");
            require(c.apply(ControlEffect::Slow,first,true)==CombatControl::Result::CancelledBySource && !c.active && c.immunity==5,"Same attacker early CC cancels control and starts immunity");
            c.advance(2);c.apply(ControlEffect::Blind,other,true);
            require(c.immunity==3 && !c.active,"Blocked CC does not extend immunity");
            c.advance(3);c.apply(ControlEffect::Sleep,first,true);c.hit(1);
            require(c.waking && c.remaining==1.5 && c.immunity==0,"Damage starts sleep waking before immunity");
            c.advance(.5);c.hit(1);require(c.remaining==1,"Repeated damage does not restart waking");
            c.advance(1);require(!c.active && c.immunity==5,"Sleep immunity begins after waking finishes");
            c.advance(5);c.apply(ControlEffect::Fear,first,true);c.hit(1);
            require(!c.active && c.immunity==5,"Damaging hit breaks fear and grants player immunity");
            c.advance(5);c.apply(ControlEffect::Root,first,true);
            require(c.blocks_mobility() && !c.blocks_basic(),"Root permits basic fire while blocking mobility");
            c.cleanse();require(c.immunity==5 && !c.active,"Early cleanse immediately grants immunity");
            CombatControl npc;npc.apply(ControlEffect::Silence,first,true);
            require(npc.blocks_skills() && !npc.blocks_basic() && !npc.blocks_mobility(),"Silence preserves basic fire and movement");
            npc.advance(3);require(npc.immunity==0 && !npc.active,"Ordinary NPC receives no post-control immunity");
            npc.apply(ControlEffect::Disorient,first,true);
            require(npc.blocks_basic() && !npc.blocks_mobility() && npc.movement_factor()==.75,"Disorient permits slower movement while blocking weapons");
            npc.cleanse();npc.apply(ControlEffect::Slow,first,true);
            require(npc.movement_factor()==.67 && npc.firing_factor()==.67,"Slow reduces both movement and firing rate by thirty-three percent");
            BossBreakBar boss;for(int i=0;i<4;i++)boss.apply(ControlEffect::Stun);
            require(boss.phase==BossBreakBar::Phase::Staggered && boss.damage_factor()==1.25 && boss.remaining==8,"Four stuns break boss bar for eight-second damage window");
            require(!boss.apply(ControlEffect::Stun) && boss.remaining==8,"Further CC cannot extend boss stagger");
            boss.advance(8);boss.advance(45);
            require(boss.phase==BossBreakBar::Phase::Recovering && boss.strength==500 && !boss.apply(ControlEffect::Stun),"Boss recovery is protected and halfway full after forty-five seconds");
            boss.advance(45);require(boss.phase==BossBreakBar::Phase::Ready && boss.strength==1000 && boss.apply(ControlEffect::Blind),"Boss accepts CC again only after ninety-second refill");
            const double contributions[]={250,150,150,250,200,150,75,100,200,100,200,75,150,100};
            bool allContributions=true;for(int i=0;i<14;i++){BossBreakBar bar;bar.apply(static_cast<ControlEffect>(i));allContributions=allContributions && bar.strength==1000-contributions[i];}
            require(allContributions,"All fourteen boss CC contributions match the authored baseline");
            BossBreakBar composite;
            require(composite.apply_composite({ControlEffect::Slow,ControlEffect::Stun},17) && composite.strength==750 && !composite.apply(ControlEffect::Stun,17) && composite.strength==750,"Composite boss event uses strongest CC once and rejects duplicate delivery");
            for(double frame:{1./30,1./60,1./144}){
                BossBreakBar bar;for(int i=0;i<4;i++)bar.apply(ControlEffect::Stun);
                auto elapsed=[&](double duration){while(duration>1e-10){double dt=std::min(frame,duration);bar.advance(dt);duration-=dt;}};
                elapsed(7.9);bool valid=bar.phase==BossBreakBar::Phase::Staggered && !bar.apply(ControlEffect::Launch);
                elapsed(.2);valid=valid && bar.phase==BossBreakBar::Phase::Recovering && bar.damage_factor()==1 && !bar.apply(ControlEffect::Stun);
                elapsed(89.8);valid=valid && bar.phase==BossBreakBar::Phase::Recovering && bar.strength<1000;
                elapsed(.2);valid=valid && bar.phase==BossBreakBar::Phase::Ready && bar.strength==1000 && bar.apply(ControlEffect::Stun);
                require(valid,"Boss stagger and protected refill preserve phase boundaries at varied frame rates");
            }
        }
        {
            AdventurePlayer::Presentation p;p.showBody=false;
            require(player_presentation(p).empty(),"First-person placeholder produces no body geometry");
            p.showTether=true;p.tetherAnchor={0,5,-4};
            auto tether=player_presentation(p);for(const auto& part:tether)part.mesh.validate();
            require(tether.size()==2,"First-person grapple retains a visible tether and hook");
            p.showBody=true;
            auto body=player_presentation(p);for(const auto& part:body)part.mesh.validate();
            require(body.size()>tether.size(),"Third-person placeholder adds body geometry without gameplay objects");
        }
        {
            MovementResources resource;
            GrappleMotion grapple;
            require(!grapple.press(resource,false,{0,2,0},{0,2,-5},{0,0,1},false) && resource.grappleCharges==1,"Missed grapple spends one charge");
            grapple.press(resource,false,{0,2,0},{0,2,-5},{0,0,1},false);
            require(resource.grappleCharges==1,"Held grapple cannot repeatedly spend charges");
            grapple.release();
            require(grapple.press(resource,true,{0,2,0},{0,2,-5},{0,0,1},false),"Fresh grapple starts wall pull");
            grapple.release();require(grapple.state==GrappleMotion::State::Free,"Releasing before wall arrival detaches");
            resource={};grapple.reset();
            grapple.press(resource,true,{0,2,-4.5},{0,2,-5},{0,0,1},false);
            for(int i=0;i<4;i++)grapple.step({0,2,-4.5},{},.01);grapple.release();
            require(grapple.state==GrappleMotion::State::Wall,"Wall attachment survives G release");
            auto jump=grapple.wall_jump();
            require(grapple.state==GrappleMotion::State::Free && jump.y==5 && jump.z==4,"Wall jump detaches upward and outward");
            grapple.reset();resource={};
            grapple.press(resource,true,{0,0,0},{0,5,0},{0,-1,0},true);
            for(int i=0;i<7;i++)grapple.step({0,0,0},{},.02);auto v=grapple.step({0,0,0},{1,0,0},.02);grapple.release();
            require(grapple.state==GrappleMotion::State::Free && (grapple.velocity-v).length()<1e-9 && v.x>0,"Swing release preserves steering momentum");
        }
        {
            MovementResources m;
            m.advance(10,true,true);
            require(std::abs(m.stamina-350)<1e-8,"Sprint drains exactly fifteen stamina per second");
            m.advance(2,false,true);
            require(std::abs(m.stamina-400)<1e-8,"Released Shift restores twenty-five stamina per second");
            m.advance(10,true,false);
            require(m.stamina==400,"Stationary held Shift does not consume sprint stamina");
            m.advance(400./15,true,true);
            require(m.stamina==0 && m.exhausted,"Empty stamina starts exhausted recovery");
            require(m.advance(19,true,true)==0 && m.stamina==475,"Held Shift recovers stamina without early sprint restart");
            m.advance(1,true,true);
            require(m.stamina==500 && !m.exhausted,"Held Shift recovers to full in twenty seconds");
            require(m.advance(1,true,true)==1 && m.stamina==485,"Held Shift automatically resumes sprint after full recovery");
            m.advance(.4,false,false);
            require(m.stamina==495,"Releasing Shift recovers from a partial stamina balance");
            m.advance(1,false,false);
            require(m.stamina==500,"Partial recovery caps at five hundred");
            require(m.spend_dodge() && m.spend_dodge() && !m.spend_dodge(),"Two separate dodge charges prevent a third dodge");
            m.advance(4.9,false,false);
            require(m.dodgeCharges==0,"Dodge charge does not return early");
            m.advance(.11,false,false);
            require(m.dodgeCharges==1,"One dodge charge returns after five seconds");
            m.advance(5,false,false);
            require(m.dodgeCharges==2 && m.dodgeRecharge==0,"Second dodge returns sequentially and recharge stops at full");
            require(m.spend_grapple() && m.spend_grapple() && !m.spend_grapple(),"Grapple has two independent charges");
            m.advance(9.9,false,false);require(m.grappleCharges==0,"Grapple does not recharge before ten seconds");
            m.advance(.2,false,false);require(m.grappleCharges==1,"One grapple charge returns every ten seconds");
            m.advance(10,false,false);require(m.grappleCharges==2 && m.dodgeCharges==2,"Grapple recharge is independent of dodge charges");
        }
        {
            std::vector<TutorialSection> catalog{{"start",3},{"save",2},{"build",4}};
            TutorialProgress progress(3);progress.selected=2;progress.positions={1,1,3};
            auto encoded=encode_tutorial_progress(catalog,progress);
            auto file=folder/"tutorial-progress.json";atomic_write(file,encoded);
            auto restored=decode_tutorial_progress(catalog,read_file(file));
            require(restored.selected==2 && restored.positions==progress.positions,"Tutorial reading positions survive saved-file round trip");
            auto reordered=decode_tutorial_progress({{"build",4},{"new",2},{"save",2}},encoded);
            require(reordered.selected==0 && reordered.positions==std::vector<size_t>({3,0,1}),"Tutorial IDs preserve progress after reordering and adding lessons");
            auto shortened=decode_tutorial_progress({{"start",1},{"save",2}},encoded);
            require(shortened.selected==0 && shortened.positions==std::vector<size_t>({0,1}),"Removed lesson and shortened steps recover safely");
            for(auto bad:{"broken","{}","1 2 1","1 99 0 0 0","1 0 0 0 0 extra"}){
                auto clean=decode_tutorial_progress(catalog,bad);
                require(clean.selected==0 && clean.positions==std::vector<size_t>(3,0),"Invalid tutorial preferences do not prevent help opening");
            }
            auto old=decode_tutorial_progress(catalog,"1 2 1 1 3");
            require(old.selected==2 && old.positions==progress.positions,"First tutorial progress format remains readable");
            auto invalid=nlohmann::json::parse(encoded);invalid["positions"]["build"]=-1;
            auto bounded=decode_tutorial_progress(catalog,invalid.dump());
            require(bounded.positions[2]==0 && bounded.positions[1]==1,"Invalid individual step cannot escape lesson bounds");
        }
        SceneData s;
        {
            auto nested=folder;
            while(nested.wstring().size()<290)nested/=L"long-path-fixture";
            auto file=nested/L"saved.txt";
            atomic_write(file,"first");
            atomic_write(file,"second",true,hash_file(file));
            require(read_file(file)=="second","Atomic save and backup work beyond MAX_PATH");
        }
        {
            auto library = folder / ("library-" + new_id());
            atomic_write(library / "models" / "Oak.GLB", "index only");
            atomic_write(library / "props.starobject", "index only");
            atomic_write(library / "arrangement.stargroup", "index only");
            atomic_write(library / "world.starscene", "index only");
            atomic_write(library / "ignore.txt", "not an asset");
            auto index = scan_asset_library(library);
            require(index.entries.size() == 4 && !index.partial && index.errors == 0,
                "Asset library indexes supported file types including uppercase extensions");
            require(filter_asset_library(index, L"OAK").size() == 1 && filter_asset_library(index, L"models/").size() == 1 &&
                filter_asset_library(index, L"missing").empty(), "Asset library search filters names and relative paths case insensitively");
            require(filter_asset_library(index, L"", L".GLB").size() == 1 &&
                filter_asset_library(index, L"oak", L".starobject").empty() &&
                filter_asset_library(index, L"props", L".starobject").size() == 1,
                "Asset type filter combines with search and accepts uppercase model extensions");
        }
        s.Terrain.emplace();
        Character c;
        c.Route = {{3, 0}, {3, 4}};
        c.Speed = 2;
        s.Characters.push_back(c);
        require(c.at(1).X == 2 && c.at(2).Z == 1 && c.at(20).Z == 4,
                "Route interpolation and completion match managed behavior");
        s.Terrain->brush(0, 0, 2, 1, "raise", 0, 0, .05);
        require(s.Terrain->height_at(0, 0) > .09, "Terrain raise");
        auto copy = SceneData::decode(s.encode());
        require(copy.Characters[0].Id == c.Id && copy.Terrain->Heights == s.Terrain->Heights,
                "Scene schema7 roundtrip");
        require(SceneData::decode("{\"SchemaVersion\":6}").SchemaVersion == 7, "Older scene migration");
        for (const auto *text : {"null", "{\"Characters\":null}", "{\"SchemaVersion\":99}",
                                 "{\"PathBounces\":99}", "{\"Terrain\":{\"Heights\":[]}}",
                                 "{\"SchemaVersion\":7.5}", "{\"PathBounces\":4294967297}",
                                 "{\"Characters\":[{\"Sculpt\":{\"Positions\":[0,0,0],\"Indices\":[0.1,0,0]}}]}"}) {
            bool rejected = false;
            try {
                SceneData::decode(text);
            } catch (...) {
                rejected = true;
            }
            require(rejected, "Malformed scene rejected");
        }
        auto file = folder / L"roundtrip.starscene";
        save_scene(s, file);
        auto old = hash_file(file);
        s.Name = "Updated";
        save_scene(s, file, old);
        require(SceneData::decode(read_file(file)).Name == "Updated", "Atomic scene save");
        bool stale = false;
        try {
            save_scene(s, file, old);
        } catch (...) {
            stale = true;
        }
        require(stale, "Stale edit rejected");
        configure(c, 2, -1, 2.25, .75);
        require(c.Route[0].X == 5 && c.Route[0].Z == -1, "Character configure moves route");
        SculptMesh{}.validate();
        SculptMesh wide{{9999, 0, 0}, {}};
        wide.validate();
        require(true, "Legacy sculpt limits preserved");
        Character transform;
        transform.Width = 2;
        transform.Height = .5;
        transform.Depth = 3;
        transform.Size = 1.8;
        transform.X = 4;
        transform.Y = -2;
        transform.Z = 1;
        transform.RotationX = 23;
        transform.RotationY = 81;
        transform.RotationZ = -47;
        Vec3 p{.2, 1, -.7};
        require((local_point(transform, world_point(transform, p)) - p).length() < 1e-12,
                "Sculpt inverse transform with rotation and nonuniform scale");
        Mesh tri;
        tri.vertices = {{{0, 0, 0}}, {{0, 0, 1}}, {{1, 0, 0}}};
        tri.indices = {0, 1, 2};
        Character sculpt;
        auto hit = mesh_hit(tri, sculpt, {.2, 2, .2}, {0, -1, 0});
        require(hit && std::abs(hit->distance - 2) < 1e-12 && hit->normal.y > 0,
                "Nearest triangle picking and facing normal");
        require(!mesh_hit(tri, sculpt, {2, 2, 2}, {0, -1, 0}), "Ray outside triangle rejected");
        sculpt.Sculpt = prepare_sculpt(tri);
        require(sculpt.Sculpt->Indices.size() == 12288, "Sculpt subdivision preserves six-pass limit");
        auto before = *sculpt.Sculpt;
        sculpt_brush(sculpt, {0, 0, 0}, {0, 1, 0}, .5, 1, "pull", false, .05);
        require(sculpt.Sculpt->Positions[1] > .039, "Pull brush modifies local mesh");
        sculpt.Sculpt = before;
        sculpt_brush(sculpt, {0, 0, 0}, {0, 1, 0}, .5, 1, "pull", true, .05);
        require(sculpt.Sculpt->Positions[1] < -.039, "Reverse brush changes direction");
        SceneHistory history;
        auto original = s.Name;
        history.begin(s);
        s.Name = "Stroke";
        history.commit();
        require(history.undo(s) && s.Name == original, "Undo transaction");
        require(history.redo(s) && s.Name == "Stroke", "Redo transaction");
        history.begin(s);
        s.Name = "Cancelled";
        history.cancel(s);
        require(s.Name == "Stroke", "Cancel restores active transaction");
        {
            Character object;
            object.Shape = "rock"; object.Name = "Reusable stone"; object.Color = "#81716A";
            object.Dynamic = true; object.Density = 2800; object.Width = 1.7;
            object.X = 2; object.Z = -3; object.Route = {{3,-2},{4,-1}};
            const auto saved = folder / "stone.starobject";
            save_object_template(object, saved);
            const auto savedHash = hash_file(saved);
            auto first = load_object_template(saved), second = load_object_template(saved);
            require(first.Id != second.Id && first.Id != object.Id && first.Color == object.Color &&
                first.Dynamic && first.Density == 2800 && first.Width == 1.7,
                "Reusable objects preserve settings with independent identities");
            place_object_template(first, 5, 1);
            require(first.X == 5 && first.Z == 1 && first.Route[0].X == 6 && first.Route[0].Z == 2 &&
                hash_file(saved) == savedHash, "Reusable object placement translates route without altering saved file");
            auto sculptCopy = decode_object_template(encode_object_template(sculpt));
            require(sculptCopy.Sculpt && sculptCopy.Sculpt->Positions == sculpt.Sculpt->Positions &&
                sculptCopy.Sculpt->Indices == sculpt.Sculpt->Indices, "Reusable object preserves sculpt geometry");
            SceneData target; SceneHistory edits;
            edits.begin(target); target.Characters.push_back(first); edits.commit();
            require(edits.undo(target) && target.Characters.empty() && edits.redo(target) &&
                target.Characters.front().Id == first.Id, "Reusable object insertion supports Undo and Redo");
            auto invalid = json::parse(read_file(saved)); invalid["Version"] = 2;
            bool refused = false;
            try { decode_object_template(invalid.dump()); } catch (...) { refused = true; }
            require(refused, "Unknown reusable object version is rejected");
            invalid["Version"] = 1; invalid["Object"]["Asset"] = "outside.glb";
            refused = false;
            try { decode_object_template(invalid.dump()); } catch (...) { refused = true; }
            require(refused, "Reusable objects reject hidden external asset dependencies");
            Character imported; imported.Asset = "missing.glb";
            refused = false;
            try { encode_object_template(imported); } catch (...) { refused = true; }
            require(refused, "Unsupported imported models are not advertised as portable objects");
            auto secondObject = object; secondObject.Id = new_id(); secondObject.X += 3;
            secondObject.Y = 2; secondObject.Route = {{7,4}};
            auto groupFile = folder / "arrangement.stargroup";
            save_object_group({object, secondObject}, groupFile);
            auto group = load_object_group(groupFile);
            require(group.size() == 2 && group[0].Id != object.Id && group[1].Id != secondObject.Id && group[0].Id != group[1].Id,
                "Reusable groups assign independent identities to every member");
            SceneData placement;
            place_object_group(group, placement, 1.24, 2.24, true, false, false);
            require(group[0].X == 1 && group[0].Z == 2 && group[1].X == 4 &&
                group[1].Y - group[0].Y == 2 && group[1].Route[0].X == 6 && group[1].Route[0].Z == 9,
                "Group placement preserves spacing and routes while snapping its anchor");
            SceneHistory groupHistory; groupHistory.begin(placement); placement.Characters = group; groupHistory.commit();
            require(groupHistory.undo(placement) && placement.Characters.empty() && groupHistory.redo(placement) && placement.Characters.size() == 2,
                "Whole group insertion is a single Undo and Redo transaction");
            auto badGroup = json::parse(read_file(groupFile)); badGroup["Objects"] = json::array(); refused = false;
            try { decode_object_group(badGroup.dump()); } catch (...) { refused = true; }
            require(refused, "Empty reusable group is rejected");
        }
        for (const auto &kind : shapes) {
            Character shape;
            shape.Shape = kind;
            auto parts = shape_geometry(shape);
            require(!parts.empty(), "Shape has native geometry");
            for (const auto &part : parts) {
                part.mesh.validate();
                require(!part.mesh.indices.empty(), "Shape triangles valid");
            }
        }
        Character cube;
        cube.Shape = "cube";
        auto cubeMesh = shape_geometry(cube)[0].mesh;
        require(cubeMesh.vertices.size() == 24 && cubeMesh.indices.size() == 36, "Cube face topology");
        Character grass;
        grass.Shape = "grass";
        auto grassMesh = shape_geometry(grass)[0].mesh;
        require(grassMesh.vertices.size() == 1000 && grassMesh.indices.size() == 2400,
                "Grass blade topology");
        TerrainData flat;
        auto terrain = terrain_geometry(flat);
        require(terrain.size() == 1 && terrain[0].mesh.vertices.size() == 193 * 193 &&
                    terrain[0].mesh.indices.size() == 192 * 192 * 6,
                "Terrain render sampling");
        flat.WaterEnabled = true;
        flat.WaterLevel = .5;
        auto wet = terrain_geometry(flat);
        require(wet.size() == 2 && wet[1].mesh.indices.size() == 192 * 192 * 6,
                "Water clipping on submerged plane");
        stamp_shape(flat, cube);
        require(flat.height_at(0, 0) == 1 && flat.height_at(2, 2) == 0,
                "Terrain stamp raises footprint only");
        if (argc > 2) {
            auto reference = json::parse(read_file(argv[2]));
            for (auto it = reference.begin(); it != reference.end(); ++it) {
                Character shape;
                shape.Shape = it.key();
                auto actual = shape_geometry(shape);
                require(actual.size() == it.value().size(), "Managed/native geometry part count");
                for (size_t p = 0; p < actual.size(); p++) {
                    const auto &expected = it.value()[p];
                    const auto &mesh = actual[p].mesh;
                    require(mesh.vertices.size() == expected.at("vertices").get<size_t>() &&
                                mesh.indices.size() == expected.at("indices").get<size_t>(),
                            "Managed/native geometry topology");
                    Vec3 sum;
                    for (const auto &v : mesh.vertices)
                        sum = sum + v.position;
                    auto e = expected.at("sum").get<std::vector<double>>();
                    require((sum - Vec3{e[0], e[1], e[2]}).length() < 1e-8,
                            "Managed/native geometry position sums");
                    for (size_t i = 0; i < expected.at("positions").size(); i++) {
                        auto q = expected.at("positions")[i].get<std::vector<double>>();
                        require((mesh.vertices[i].position - Vec3{q[0], q[1], q[2]}).length() < 1e-12,
                                "Managed/native seeded vertex coordinates");
                    }
                }
            }
        }
        require(rgb("DarkSlateBlue").z == 139. / 255 && rgb("#abc").x == 170. / 255,
                "Legacy named and short hex colors");
        auto request = folder / "request.json";
        json ops = {{"schemaVersion", 1},
                    {"scenePath", utf8(fs::absolute(file).wstring())},
                    {"expectedSha256", hash_file(file)},
                    {"operations",
                     json::array({json{{"op", "shape"}, {"shape", "cube"}, {"name", "Native command"}}})}};
        atomic_write(request, ops.dump());
        auto response = apply_commands(request);
        require(response.at("success").get<bool>() &&
                    SceneData::decode(read_file(file)).Characters.size() == 2,
                "Native command transaction saves scene");
        auto hash = hash_file(file);
        ops["expectedSha256"] = hash;
        ops["operations"].push_back(json{{"op", "invalid"}, {"id", c.Id}});
        atomic_write(request, ops.dump());
        bool invalid = false;
        try {
            apply_commands(request);
        } catch (...) {
            invalid = true;
        }
        require(invalid && hash_file(file) == hash, "Failed command batch preserves original bytes");
        {
            auto source = folder / "stone.starobject";
            ops["expectedSha256"] = hash_file(file);
            ops["operations"] = json::array({json{{"op","insert-object"},{"file","stone.starobject"},
                {"expectedSha256",hash_file(source)},{"name","Inserted stone"},{"x",5},{"z",1}}});
            atomic_write(request, ops.dump()); apply_commands(request);
            auto inserted = SceneData::decode(read_file(file)).Characters.back();
            require(inserted.Name == "Inserted stone" && inserted.Route[0].X == 6 && inserted.Route[0].Z == 2,
                "Command inserts saved object with relative path and translated route");
            ops["expectedSha256"] = hash_file(file);
            ops["operations"] = json::array({json{{"op","duplicate"},{"id",inserted.Id},{"x",7},{"z",2}}});
            atomic_write(request, ops.dump()); apply_commands(request);
            auto duplicated = SceneData::decode(read_file(file)).Characters.back();
            require(duplicated.Id != inserted.Id && duplicated.Name == "Inserted stone copy" &&
                duplicated.Route[0].X == 8 && duplicated.Route[0].Z == 3 && duplicated.Density == inserted.Density,
                "Command duplication preserves settings and moves route independently");
            auto unchanged = hash_file(file);
            ops["expectedSha256"] = unchanged;
            ops["operations"].push_back(json{{"op","insert-object"},{"file","stone.starobject"},{"expectedSha256","stale"}});
            atomic_write(request, ops.dump()); bool rejected = false;
            try { apply_commands(request); } catch (...) { rejected = true; }
            require(rejected && hash_file(file) == unchanged,
                "Stale object file rolls back entire command batch including earlier duplication");
            SceneData from; from.Hour = 1;
            Character item; item.Shape = "cube"; item.X = 2; item.Route = {{3,1}};
            from.Characters.push_back(item);
            auto sourceScene = folder / "merge-source.starscene"; save_scene(from, sourceScene);
            auto originalWorld = load_scene(file).Hour;
            auto originalCount = load_scene(file).Characters.size();
            ops["expectedSha256"] = hash_file(file);
            ops["operations"] = json::array({json{{"op","merge-scene"},{"file","merge-source.starscene"},
                {"expectedSha256",hash_file(sourceScene)},{"x",4},{"z",2}}});
            atomic_write(request, ops.dump()); apply_commands(request);
            auto merged = load_scene(file);
            require(merged.Characters.size() == originalCount + 1 && merged.Hour == originalWorld &&
                merged.Characters.back().Id != item.Id && merged.Characters.back().Route[0].X == 5,
                "Scene composition preserves destination world and gives imported objects fresh identities");
            auto priorGroupCount = merged.Characters.size();
            ops["expectedSha256"] = hash_file(file);
            ops["operations"] = json::array({json{{"op","insert-group"},{"file","arrangement.stargroup"},
                {"expectedSha256",hash_file(folder / "arrangement.stargroup")},{"x",1},{"z",2}}});
            atomic_write(request, ops.dump()); apply_commands(request);
            auto insertedGroup = load_scene(file);
            require(insertedGroup.Characters.size() == priorGroupCount + 2 &&
                insertedGroup.Characters.back().X - insertedGroup.Characters[priorGroupCount].X == 3,
                "Guarded group command inserts the complete arrangement with spacing intact");
        }
        TerrainFlow flow(TerrainData{}, {{0, 0, false, 2}, {2, 0, true, 1}});
        for (int i = 0; i < 200; i++)
            flow.step(.025);
        require(std::abs(flow.volume() - 3) < 1e-10 && std::abs(flow.emitted - 3) < 1e-10,
                "Closed fluid volume conserved after emission");
        FlowObstacleMask mask;
        mask.add(cubeMesh, cube);
        mask.finish();
        require(mask.coverage[TerrainFlow::index(.1, .1)] == 1 && mask.top[TerrainFlow::index(.1, .1)] == 1,
                "Obstacle voxel footprint samples solid cube");
        flow.set_obstacles(std::move(mask));
        for (int i = 0; i < 40; i++)
            flow.step(.025);
        require(std::abs(flow.volume() - 3) < 1e-10, "Obstacle transport conserves liquid");
        for (const auto &part : flow.geometry())
            part.mesh.validate();
        require(!flow.geometry().empty(), "Fluid native surface mesh");
        SceneData grouped;
        Character first, second;
        first.Shape = second.Shape = "cube";
        first.KeepAboveFloor = second.KeepAboveFloor = false;
        second.X = 2;
        second.Route = {{3, 1}};
        grouped.Characters = {first, second};
        std::unordered_map<std::string, Character> originals{{first.Id, first}, {second.Id, second}};
        grouped.Characters[0].X = 1;
        move_group(grouped, grouped.Characters[0], originals);
        require(grouped.Characters[1].X == 3 && grouped.Characters[1].Route[0].X == 4,
                "Group translation preserves relative route offsets");
        grouped.Characters[0].X = .74;
        snap_object(grouped, grouped.Characters[0], true, false, false);
        require(grouped.Characters[0].X == .5, "Native grid snapping");
        require(terrain_hit(nullptr, {0, 2, 0}, {0, -1, 0}).has_value() &&
                    !terrain_hit(nullptr, {0, 2, 0}, {0, 1, 0}).has_value(),
                "Ground picking rejects rays pointed away from surface");
        if (argc > 2) {
            auto fixture = fs::path(argv[2]).parent_path() / "fixtures/triangle.glb";
            auto parts = load_glb(fixture);
            require(!parts.empty() && parts[0].mesh.vertices.size() == 3, "Native GLB triangle import");
            {
                SceneData broken; Character imported;
                imported.Asset = "missing-old-model.glb"; imported.Name = "Route keeper";
                imported.Width = 2; imported.X = 3; imported.Route = {{4,5}};
                broken.Characters.push_back(imported);
                auto brokenFile = folder / "repair.starscene"; save_scene(broken, brokenFile);
                auto brokenInspection = inspect_scene(brokenFile);
                require(brokenInspection.at("missingAssetCount") == 1 && brokenInspection.at("assets").size() == 1,
                    "Scene inspection identifies missing imported models without opening renderer");
                auto repairDraft = broken; repairDraft.Characters.push_back(imported); repairDraft.Characters.back().Id = new_id();
                size_t prompts = 0;
                auto repairedCount = repair_scene_models(repairDraft, [&](const fs::path&) -> std::optional<fs::path> { ++prompts; return fixture; });
                require(repairedCount && *repairedCount == 1 && prompts == 1 &&
                    repairDraft.Characters[0].Asset == repairDraft.Characters[1].Asset &&
                    inspect_scene(brokenFile).at("missingAssetCount") == 1,
                    "Guided model repair resolves shared references once without writing the scene");
                auto cancelDraft = broken;
                auto extraMissing = imported; extraMissing.Id = new_id(); extraMissing.Asset = "another-missing.glb";
                cancelDraft.Characters.push_back(extraMissing);
                const auto cancelledBefore = cancelDraft.encode(); prompts = 0;
                auto cancelled = repair_scene_models(cancelDraft, [&](const fs::path&) -> std::optional<fs::path> {
                    if (++prompts == 1) return fixture;
                    return std::nullopt;
                });
                require(!cancelled && cancelDraft.encode() == cancelledBefore,
                    "Cancelling later model repair preserves the complete original scene draft");
                auto replacement = folder / "replacement.glb"; atomic_write(replacement, read_file(fixture));
                json repair{{"schemaVersion",1},{"scenePath",utf8(fs::absolute(brokenFile).wstring())},
                    {"expectedSha256",hash_file(brokenFile)},
                    {"operations",json::array({json{{"op","replace-model"},{"id",imported.Id},{"asset","replacement.glb"}}})}};
                atomic_write(request, repair.dump()); apply_commands(request);
                auto repaired = load_scene(brokenFile);
                require(inspect_scene(brokenFile).at("missingAssetCount") == 0, "Scene inspection clears missing model finding after relink");
                require(repaired.Characters[0].Id == imported.Id && repaired.Characters[0].Name == imported.Name &&
                    repaired.Characters[0].Width == 2 && repaired.Characters[0].X == 3 &&
                    repaired.Characters[0].Route[0].Z == 5 && render_scene(repaired).size() > 0,
                    "Missing model can be relinked without losing object identity or route");
                auto intact = hash_file(brokenFile);
                atomic_write(replacement, "invalid GLB"); repair["expectedSha256"] = intact;
                atomic_write(request, repair.dump()); bool rejected = false;
                try { apply_commands(request); } catch (...) { rejected = true; }
                require(rejected && hash_file(brokenFile) == intact, "Invalid replacement model leaves scene bytes unchanged");
            }
            {
                SceneData portable;
                Character model; model.Asset = utf8(fs::absolute(fixture).wstring());
                portable.Characters = {model, model}; portable.Characters.back().Id = new_id();
                auto destination = folder / ("portable-" + new_id());
                auto report = export_portable_scene(portable, destination);
                require(report.at("assetCount") == 1, "Portable export deduplicates shared model bytes");
                auto selectionFolder = folder / ("selection-" + new_id());
                portable.Terrain.emplace(); portable.Hour = 2;
                export_scene_selection(portable, {portable.Characters.back().Id}, selectionFolder);
                auto subset = load_scene(selectionFolder / "scene.starscene");
                require(subset.Characters.size() == 1 && subset.Characters.front().Id == portable.Characters.back().Id &&
                    !subset.Terrain && verify_portable_scene(selectionFolder).at("success").get<bool>(),
                    "Portable selection includes only requested objects and required assets, without terrain");
                auto mergeTarget = folder / "portable-merge-target.starscene";
                SceneData emptyTarget; emptyTarget.Hour = 22; save_scene(emptyTarget, mergeTarget);
                json mergeRequest{{"schemaVersion",1},{"scenePath",utf8(fs::absolute(mergeTarget).wstring())},
                    {"expectedSha256",hash_file(mergeTarget)},{"operations",json::array({json{{"op","merge-scene"},
                    {"file",utf8(fs::absolute(selectionFolder / "scene.starscene").wstring())},
                    {"expectedSha256",hash_file(selectionFolder / "scene.starscene")},{"x",2},{"z",3}}})}};
                atomic_write(request, mergeRequest.dump()); apply_commands(request);
                auto composed = load_scene(mergeTarget);
                require(composed.Hour == 22 && composed.Characters.size() == 1 &&
                    composed.Characters.front().Id != subset.Characters.front().Id && render_scene(composed).size() > 0,
                    "Portable imported-model composition resolves source-relative model files");
                bool missingSelection = false;
                auto absentSelection = folder / ("missing-selection-" + new_id());
                try { export_scene_selection(portable, {"missing"}, absentSelection); } catch (...) { missingSelection = true; }
                require(missingSelection && !fs::exists(absentSelection), "Unknown selected object cannot produce an incomplete export");
                auto relocated = folder / ("relocated-" + new_id());
                fs::rename(destination, relocated);
                require(verify_portable_scene(relocated).at("success").get<bool>(), "Relocated export passes integrity verification");
                auto sceneFile = relocated / "scene.starscene";
                auto reopened = load_scene(sceneFile);
                require(fs::exists(starlight::editor::wide(reopened.Characters[0].Asset)) &&
                    render_scene(reopened).size() > 0, "Relocated portable scene resolves and renders bundled models");
                save_scene(reopened, sceneFile, hash_file(sceneFile));
                auto onDisk = SceneData::decode(read_file(sceneFile));
                require(fs::path(starlight::editor::wide(onDisk.Characters[0].Asset)).is_relative(),
                    "Saving reopened portable scene retains relative model paths");
                bool refused = false;
                auto before = hash_file(sceneFile);
                try { export_portable_scene(portable, relocated); } catch (...) { refused = true; }
                require(refused && hash_file(sceneFile) == before, "Portable export refuses to overwrite existing folders");
                portable.Characters[0].Asset = utf8((folder / "missing-model.glb").wstring());
                auto failed = folder / ("failed-export-" + new_id()); refused = false;
                try { export_portable_scene(portable, failed); } catch (...) { refused = true; }
                require(refused && !fs::exists(failed), "Missing model does not publish a partial portable scene");
                auto manifestFile = relocated / "manifest.json";
                auto manifest = json::parse(read_file(manifestFile));
                manifest["sceneSha256"] = hash_file(sceneFile);
                atomic_write(manifestFile, manifest.dump());
                require(verify_portable_scene(relocated).at("success").get<bool>(), "Intact exported assets pass before corruption test");
                auto escaped = manifest; escaped["assets"][0]["path"] = utf8(fs::absolute(fixture).wstring());
                atomic_write(manifestFile, escaped.dump()); refused = false;
                try { verify_portable_scene(relocated); } catch (...) { refused = true; }
                require(refused, "Portable verification rejects absolute asset paths");
                atomic_write(manifestFile, manifest.dump());
                auto assetPath = fs::path(starlight::editor::wide(reopened.Characters[0].Asset));
                atomic_write(assetPath, "damaged"); refused = false;
                try { verify_portable_scene(relocated); } catch (...) { refused = true; }
                require(refused, "Portable verification rejects damaged asset bytes");
            }
            auto bytes = read_file(fixture);
            uint32_t jsonSize;
            std::memcpy(&jsonSize, bytes.data() + 12, 4);
            auto description = json::parse(bytes.substr(20, jsonSize));
            auto suffix = bytes.substr(20 + jsonSize);
            auto malformed = [&](json document) {
                auto text = document.dump();
                while (text.size() % 4) text += ' ';
                auto result = bytes.substr(0, 20);
                uint32_t size = uint32_t(text.size()), total = uint32_t(20 + size + suffix.size());
                std::memcpy(result.data() + 8, &total, 4);
                std::memcpy(result.data() + 12, &size, 4);
                result += text + suffix;
                auto path = folder / "malformed.glb";
                atomic_write(path, result);
                try { load_glb(path); return false; } catch (const std::exception &) { return true; }
            };
            auto bad = description;
            bad["accessors"][0]["count"] = 3.5;
            require(malformed(bad), "Fractional GLB counts rejected");
            bad = description; bad["accessors"][0]["byteOffset"] = 2147483648ull;
            require(malformed(bad), "Overflow GLB offsets rejected");
            bad = description; bad["accessors"][0]["componentType"] = 5126.5;
            require(malformed(bad), "Fractional GLB component types rejected");
            bad = description; bad["bufferViews"][0]["byteStride"] = 4;
            require(malformed(bad), "Invalid GLB vertex stride rejected");
            bad = description; bad["nodes"][0]["children"] = json::array({0});
            require(malformed(bad), "Cyclic GLB node graph rejected");
        }
        #include "gameplay_rules_checks.inl"
        #include "quest_checks.inl"
        {
            SceneData adventure;adventure.Game.emplace();adventure.Game->MoveSpeed=6.25;
            adventure.Game->LookSensitivity=.37;adventure.Game->Collision=false;
            auto restored=SceneData::decode(adventure.encode());
            require(restored.SchemaVersion==8 && restored.Game && restored.Game->MoveSpeed==6.25 &&
                    restored.Game->LookSensitivity==.37 && !restored.Game->Collision,
                    "Adventure player settings survive scene save and load");
            require(!SceneData::decode("{}").Game,"Legacy scenes do not acquire gameplay implicitly");
            for(auto field:{"MoveSpeed","LookSensitivity","PlayerRadius"}){
                auto invalid=json(adventure);invalid["Game"][field]=-1;
                bool rejected=false;try{SceneData::decode(invalid.dump());}catch(...){rejected=true;}
                require(rejected,"Invalid gameplay tuning rejected");
            }
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=2;
            Character wall;wall.Shape="cube";wall.Width=4;wall.Height=3;wall.Depth=.1;level.Characters.push_back(wall);
            auto original=level.encode();AdventurePlayer player;player.start(level);
            for(int i=0;i<60;i++)player.move(0,1,.05);
            require(player.position.z>=.3,"Adventure movement cannot cross a thin wall");
            require(level.encode()==original,"Adventure play does not mutate authored scene");
            level.Game->Collision=false;player.start(level);for(int i=0;i<60;i++)player.move(0,1,.05);
            require(player.position.z<0,"Adventure collision setting is honored");
            level.Characters.clear();player.start(level);player.move(1,1,.05);
            require(std::abs(std::hypot(player.position.x,player.position.z-2)-.2)<1e-8,"Diagonal movement is normalized");
            player.look(0,100000);require(std::abs(player.pitch)<=1.45,"Adventure look pitch is bounded");
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=2;level.Game->SpawnY=.5;
            Character door;door.Shape="cube";door.Height=2;door.GameplayRole="door";door.GameplayKey="blue";
            Character key;key.Shape="cube";key.X=3;key.GameplayRole="key";key.GameplayKey="blue";
            Character exit;exit.Shape="cube";exit.X=6;exit.GameplayRole="exit";exit.GameplayKey="blue";
            level.Characters={door,key,exit};AdventurePlayer player;player.start(level);
            require(player.interact()=="Requires key: blue","Locked door rejects missing key");
            player.position.x=3;require(player.interact()=="Collected key: blue","Matching key collected");
            player.position.x=0;require(player.interact()=="Door opened.","Matching key opens door");
            player.position.x=6;require(player.interact()=="Adventure complete!" && player.finished,"Exit completes adventure");
            require(level.Characters.size()==3 && player.scene().Characters.size()==1,"Gameplay mutations remain in play copy");
            auto saved=SceneData::decode(level.encode());require(saved.Characters[0].GameplayKey=="blue","Gameplay roles and key links persist");
            auto reusable=decode_object_template(encode_object_template(key));require(reusable.GameplayRole=="key" && reusable.GameplayKey=="blue","Reusable objects preserve gameplay interaction");
        }
        {
            auto level=adventure_template();auto original=level.encode();
            AdventurePlayer player;player.start(SceneData::decode(original));
            for(int i=0;i<5;i++)player.move(0,1,.05);
            require(player.interact()=="Collected key: gold","Starter key reachable by walking from spawn");
            for(int i=0;i<40;i++)player.move(0,1,.05);
            require(player.position.z>0,"Starter locked door blocks passage");
            require(player.interact()=="Door opened.","Starter collected key opens actual doorway");
            for(int i=0;i<13;i++)player.move(0,1,.05);
            require(player.interact()=="Adventure complete!","Starter adventure completes through walking and interactions");
            require(level.encode()==original,"Starter play preserves authoring data");
            player.start(level);
            require(!player.finished && player.scene().Characters.size()==level.Characters.size(),"Starter restart restores all objects and progress");
            save_scene(level,folder/"starter-adventure.starscene");
        }
        {
            SceneData level;Character object;object.Shape="cube";object.X=.37;object.Z=-.37;
            object.Route={{.37,-.37}};
            snap_object(level,object,true,false,false,.25);
            require(object.X==.25 && object.Z==-.25 && object.Route[0].X==.25 && object.Route[0].Z==-.25,
                "Custom grid snaps negative coordinates and preserves attached route offsets");
            std::vector<Character> group={object,object};group[1].Id=new_id();group[1].X+=.17;
            place_object_group(group,level,1.13,0,true,false,false,.25);
            require(group[0].X==1.25 && std::abs(group[1].X-group[0].X-.17)<1e-9,
                "Custom grid preserves grouped object spacing");
            require(snap_increment(23,15)==30 && snap_increment(-23,15)==-30,"Rotation snap supports both directions");
            bool rejected=false;try{snap_increment(1,0);}catch(...){rejected=true;}
            require(rejected,"Invalid snap spacing rejected");
        }
        SceneData render;
        {
            auto starter=adventure_template();
            require(adventure_readiness(starter).empty(),"Starter adventure has no basic setup findings");
            starter.Characters.erase(std::remove_if(starter.Characters.begin(),starter.Characters.end(),[](const auto& c){return c.GameplayRole=="key";}),starter.Characters.end());
            auto findings=adventure_readiness(starter);
            require(std::count_if(findings.begin(),findings.end(),[](const auto& f){return f.find("no collectible key")!=std::string::npos;})==2,
                    "Gameplay setup identifies missing door and exit keys");
            starter.Game->SpawnZ=0;
            findings=adventure_readiness(starter);
            require(std::any_of(findings.begin(),findings.end(),[](const auto& f){return f.find("inside a solid")!=std::string::npos;}),
                    "Gameplay setup identifies blocked player start");
        }
        {
            auto level=adventure_template();AdventurePlayer builder;builder.start(level);
            builder.move(1,0,.05);builder.look(80,-12);
            const auto position=builder.position;const auto yaw=builder.yaw,pitch=builder.pitch;
            Character object;object.Shape="cube";object.X=2;level.Characters.push_back(object);
            builder.rebuild_for_authoring(level);
            require(builder.position.x==position.x && builder.position.y==position.y && builder.position.z==position.z &&
                    builder.yaw==yaw && builder.pitch==pitch,"Building refresh preserves explorer viewpoint");
            require(builder.scene().Characters.size()==level.Characters.size(),"Building refresh uses the same authored objects");
            auto original=level.encode();AdventurePlayer test;test.start(level);test.position={0,1.7,5};test.interact();
            builder.rebuild_for_authoring(level);
            require(level.encode()==original && builder.scene().Characters.size()==level.Characters.size(),
                    "Returning from gameplay retains authored key rather than collected play state");
            SceneData empty;empty.Game.emplace();builder.start(empty);
            Character wall;wall.Shape="cube";wall.Width=4;wall.Height=3;wall.Depth=.1;wall.Z=5;
            empty.Characters.push_back(wall);builder.rebuild_for_authoring(empty);
            for(int i=0;i<20;i++)builder.move(0,1,.05);
            require(builder.position.z>5,"New building edits update explorer collision");
            empty.Characters.clear();builder.rebuild_for_authoring(empty);
            for(int i=0;i<20;i++)builder.move(0,1,.05);
            require(builder.position.z<5,"Removed building objects no longer block exploration");
        }
        #include "character_controller_checks.inl"
        render.Terrain.emplace();
        Character rock;
        rock.Shape = "chaos-rock";
        render.Characters.push_back(rock);
        auto packet = render_scene(render);
        atomic_write(folder / "native-scene.bin",
                     std::string_view(reinterpret_cast<const char *>(packet.data()), packet.size()));
        require(packet.size() > 1000, "Native renderer packet emitted");
        atomic_write(folder / "result.json", json{{"success", true}, {"checks", checks}}.dump(2));
        std::cout << "PASS " << checks.size() << " native checks\n";
        return 0;
    } catch (const std::exception &e) {
        atomic_write(folder / "result.json",
                     json{{"success", false}, {"checks", checks}, {"error", e.what()}}.dump(2));
        std::cerr << e.what() << '\n';
        return 1;
    }
}
