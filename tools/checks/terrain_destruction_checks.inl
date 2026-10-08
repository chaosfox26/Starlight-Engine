#include "rejection_note.hpp"
#include "physics.hpp"
// Runtime solid carving must preserve authoring, bodies, fluids and reset.
__declspec(noinline) static void terrain_destruction_checks(const auto& require,const fs::path& folder){
    auto level=std::make_unique<SceneData>();level->Game.emplace();level->Game->SpawnZ=0;level->Terrain.emplace();level->Hour=14;level->FogDensity=0;
    level->Terrain->DestructionHealth=20;level->Terrain->DestructionRadius=1.3;std::fill(level->Terrain->Paint.begin(),level->Terrain->Paint.end(),3);
    auto authored=level->encode();auto saved=SceneData::decode(authored);
    require(saved.SchemaVersion==16&&saved.Terrain->Breakable&&saved.Terrain->DestructionHealth==20&&saved.Terrain->DestructionRadius==1.3,"Terrain breakability, local durability and carve radius save through schema14");
    auto legacy=json(*level);for(const char* field:{"Breakable","DestructionHealth","DestructionRadius"})legacy["Terrain"].erase(field);
    auto old=SceneData::decode(legacy.dump());require(old.SchemaVersion==16&&old.Terrain->Breakable&&old.Terrain->DestructionHealth==10&&old.Terrain->DestructionRadius==.65,"Earlier terrain scenes retain their geometry and gain destruction defaults");
    for(const char* name:{"DestructionHealth","DestructionRadius"})for(json value:{json(0),json(-1),json(1e12),json("strong"),json(nullptr)}){
        auto bad=json(*level);bad["Terrain"][name]=value;bool rejected=false;try{SceneData::decode(bad.dump());}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}require(rejected,"Invalid terrain destruction controls are rejected before mutation");
    }
    auto terrain=*level->Terrain;TerrainDestruction damage;
    require(!damage.damage(terrain,{0,.04,0},{0,1,0},10)&&terrain.Volumes.empty(),"Terrain accumulates partial local damage without carving prematurely");
    require(damage.damage(terrain,{0,.04,0},{0,1,0},10)&&terrain.Volumes.size()==1&&terrain.density_at(0,-.3,0)>0,"Exhausted terrain durability carves actual solid density");
    require(terrain.Heights==level->Terrain->Heights&&terrain.Paint==level->Terrain->Paint&&terrain.surface_at(0,0)< -1&&terrain.surface_at(8,8)==0,"Carving preserves authored height and paint arrays while exposing a lowered crater floor");
    auto geometry=terrain_volume_geometry(terrain);require(!geometry.indices.empty()&&std::all_of(geometry.vertices.begin(),geometry.vertices.end(),[](const auto& v){return std::isfinite(v.position.x+v.position.y+v.position.z)&&std::abs(v.normal.length()-1)<1e-6;}),"Carved terrain supplies finite shared geometry and normalized contours");
    auto protectedTerrain=*level->Terrain;protectedTerrain.Breakable=false;auto protectedSaved=json(protectedTerrain).dump();
    require(!damage.damage(protectedTerrain,{0,.04,0},{0,1,0},1e9)&&json(protectedTerrain).dump()==protectedSaved,"Explicit permanent terrain rejects damage without altering its volume or paint");
    for(auto amount:std::array<double,4>{0.,-1.,NAN,INFINITY})require(!damage.damage(terrain,{0,.04,0},{0,1,0},amount),"Invalid terrain damage cannot create carving operations");
    require(!damage.damage(terrain,{NAN,0,0},{0,1,0},20)&&!damage.damage(terrain,{13,0,0},{0,1,0},20)&&!damage.damage(terrain,{0,0,0},{},20),"Invalid, out-of-domain and normal-less terrain contacts are ignored");
    {   // TERR-1: a hit just inside the +/-12 domain must not push its hole centre past the edge, where validate() would stop the game.
        auto edge=*level->Terrain;TerrainDestruction edgeDamage;bool carved=edgeDamage.damage(edge,{11.95,0,11.95},{-.5,.7,-.5},20);bool valid=true;try{edge.validate();terrain_volume_geometry(edge);}catch(...){valid=false;}
        require(carved&&edge.Volumes.size()==1&&valid&&std::abs(edge.Volumes[0].X)<=12&&std::abs(edge.Volumes[0].Z)<=12,"TERR-1: A hit next to the terrain edge still carves, with its hole centre kept inside the domain");
    }
    auto resetTerrain=*level->Terrain;damage.reset();require(!damage.damage(resetTerrain,{0,.04,0},{0,1,0},10),"Fresh terrain starts with undamaged local durability");damage.reset();require(!damage.damage(resetTerrain,{0,.04,0},{0,1,0},10),"Reset clears partial local terrain damage as well as visible carving");
    // Volume collision replaces the old phantom heightfield inside caves.
    auto cave=std::make_unique<TerrainData>();std::fill(cave->Heights.begin(),cave->Heights.end(),2.);cave->Volumes.push_back({0,-1,0,2,true});
    for(auto direction:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,-1,0}}){
        ProjectilePhysics shots;shots.fire({0,-1,0},direction,100,0);auto hit=shots.step(.05,cave.get(),{});
        require(hit.size()==1&&hit[0].target.empty()&&(hit[0].position-Vec3{0,-1,0}).length()>1.8&&hit[0].normal.dot(direction)< -.95,"Projectiles travel through carved air and contact actual cave walls, ceilings and floors");
    }
    ProjectilePhysics shots;Bounds box;box.low={.8,-1.2,-.2};box.high={1,-.8,.2};ProjectileTarget nearTarget{"cave-obstacle",box,box};shots.fire({0,-1,0},{1,0,0},100,0);auto nearby=shots.step(.05,cave.get(),std::span<const ProjectileTarget>{&nearTarget,1});
    require(nearby.size()==1&&nearby[0].target==nearTarget.id&&nearby[0].position.x<1,"Cave projectile sweeps retain nearest object contact before the terrain wall");
    auto player=std::make_unique<AdventurePlayer>();player->start(*level);for(int i=0;i<35;++i)player->move(0,0,1./120.);auto initialY=player->position.y;
    require(!player->damage_terrain({0,.04,0},{0,1,0},10)&&player->scene().Terrain->Volumes.empty(),"Gameplay terrain damage accumulates without changing the scene until durability is exhausted");
    require(player->damage_terrain({0,.04,0},{0,1,0},10)&&player->scene().Terrain->Volumes.size()==1&&!player->presentation().dust.empty()&&!player->presentation().grit.empty(),"Actual runtime carving updates scene geometry and mineral impact feedback");
    for(int i=0;i<260;++i)player->move(0,0,1./120.);
    require(player->position.y<initialY-.8&&player->grounded()&&level->encode()==authored,"A player standing above newly carved ground falls onto the real crater floor without changing authored terrain");
    auto packet=[&](const char* name){auto p=player->presentation();p.showBody=p.showWeapons=p.showTelegraph=false;auto bytes=render_scene(player->scene(),nullptr,player_presentation(p));atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()));};
    packet("terrain-carved");atomic_write(folder/"terrain-destruction.starscene",level->encode());
    player->start(*level);packet("terrain-intact");require(player->scene().Terrain->Volumes.empty()&&player->presentation().dust.empty(),"Restart restores authored terrain and clears carving feedback");
    auto preview=*level;player->start(preview,false);require(!player->damage_terrain({0,0,0},{0,1,0},100),"Paused authoring preview cannot apply terrain destruction");
    // Real projectile routing, not only the direct damage entry point.
    auto shooting=*level;shooting.Game->Collision=false;shooting.Game->ProjectileSpeed=20;shooting.Game->ProjectileGravity=9.81;shooting.Terrain->DestructionHealth=10;player->start(shooting);player->fire_button(true);player->fire_button(false);
    // Keep the normal 2.5-second attack windup, then allow the shot to travel
    // and fall onto terrain. The former 280 ticks ended before launch.
    for(int i=0;i<480;++i)player->move(0,0,1./120.);
    require(player->presentation().shots==1,"Terrain projectile fixture completes the normal attack windup and launches one shot");
    require(!player->scene().Terrain->Volumes.empty(),"Traveling gameplay projectiles carve actual terrain on ground contact");
    auto storm=*level;storm.Game->Collision=false;storm.Game->SpawnZ=8;storm.Terrain->DestructionHealth=10;Character emitter;emitter.Shape="chaos-lightning";emitter.Y=3;emitter.TargetX=emitter.TargetZ=0;emitter.TargetY=-4;emitter.BranchSpread=0;emitter.LightningDamage=10;storm.Characters={emitter};player->start(storm);for(int i=0;i<25;++i)player->move(0,0,1./120.);
    require(!player->scene().Terrain->Volumes.empty()&&player->scene().Characters.size()==1,"Lightning terrain contacts carve solid ground while retaining the magic source");
    // Fluid rebasing changes its bed, not its conserved amount or lava heat.
    auto pool=*level->Terrain;pool.WaterEnabled=true;pool.WaterLevel=1;auto flow=std::make_unique<TerrainFlow>(pool,std::vector<LiquidSource>{});double volume=flow->volume(),surface=flow->liquid_at(.25,.25).first;
    auto rebased=pool;rebased.Volumes=terrain.Volumes;auto water=flow->water;
    require(flow->terrain(rebased)&&flow->water==water&&flow->volume()==volume&&flow->liquid_at(.25,.25).first<surface-.5,"Water adopts the carved bed while retaining every cell quantity and total volume");
    for(int i=0;i<240;++i)flow->step(1./120.);require(std::abs(flow->volume()-volume)<1e-8&&flow->liquid_at(.25,.25).first>0,"Water flows into a new crater without disappearing or being refilled from authored water level");
    const auto settledWater=flow->water;const double settledVolume=flow->volume();
    require(!flow->terrain(rebased)&&flow->water==settledWater&&flow->volume()==settledVolume,"Unchanged terrain updates do not reinitialize fluid sources or amounts");
    pool.Lava=true;auto lava=std::make_unique<TerrainFlow>(pool,std::vector<LiquidSource>{});lava->lava_settings(.7,.01,.08,1,true);double lavaVolume=lava->volume(),heat=lava->lava_heat_at(.25,.25);rebased=pool;rebased.Volumes=terrain.Volumes;
    require(lava->terrain(rebased)&&lava->volume()==lavaVolume&&lava->lava_heat_at(.25,.25)==heat,"Lava retains its volume and transported heat across terrain damage");
    // Already sleeping props must wake when their support is removed; the
    // existing world and body identities remain in place through the change.
    auto falling=std::make_unique<SceneData>();falling->Terrain=*level->Terrain;Character body;body.Shape="cube";body.Id="terrain-supported-prop";body.Dynamic=true;body.Width=body.Depth=.4;body.Height=.5;body.Y=.03;falling->Characters={body};
    auto physics=std::make_unique<PhysicsSession>();physics->reset(*falling);for(int i=0;i<1000&&!physics->ready();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));require(physics->ready(),"Terrain replacement fixture reaches ready physics before its body settles");
    for(int i=0;i<600;++i)physics->step(*falling,1./120.,nullptr);double restingY=falling->Characters[0].Y;auto ticks=physics->ticks();physics->terrain(terrain);
    require(physics->ticks()==ticks&&falling->Characters[0].Id==body.Id,"Replacing terrain collision retains the physics world's tick history and prop identity");
    for(int i=0;i<360;++i)physics->step(*falling,1./120.,nullptr);require(falling->Characters[0].Y<restingY-.8&&falling->Characters[0].Id==body.Id,"Sleeping dynamic props wake and fall when carving removes their ground support");
    SceneHistory history;auto editable=*level;history.begin(editable);editable.Terrain->Breakable=false;editable.Terrain->DestructionRadius=2;history.commit();require(history.undo(editable)&&editable.Terrain->Breakable&&editable.Terrain->DestructionRadius==1.3,"Undo restores terrain destruction controls in the existing authoring transaction");require(history.redo(editable)&&!editable.Terrain->Breakable&&editable.Terrain->DestructionRadius==2,"Redo restores edited terrain destruction controls");
}
