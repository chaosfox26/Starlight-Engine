// Focused limit checks kept in their own frame to avoid growing main's Windows stack.
static void physics_boundary_checks(const auto& require){
    TerrainData terrain;terrain.WaterEnabled=true;terrain.WaterLevel=1;
    auto flow=std::make_unique<TerrainFlow>(terrain,std::vector<LiquidSource>{});
    require(flow->liquid_at(-12,-12).second==1000&&flow->liquid_at(11.999,11.999).second==1000,"Liquid domain includes its minimum edge and last interior sample");
    require(flow->liquid_at(12,0).second==0&&flow->liquid_at(0,12).second==0&&flow->liquid_at(-12.001,0).second==0&&flow->liquid_at(0,-12.001).second==0,"Liquid domain rejects all four exterior boundaries without wrapping");
    require(flow->liquid_at(NAN,0).second==0&&flow->liquid_at(INFINITY,0).second==0&&flow->velocity_at(NAN,0).length()==0,"Non-finite liquid queries return dry stationary samples");
    auto volume=flow->volume();flow->impulse(12,0,1);flow->impulse(NAN,0,1);require(flow->wave_energy()==0&&flow->volume()==volume,"Out-of-domain liquid impulses do not disturb edge cells");
    Character cube;cube.Shape="cube";auto nonmanifold=shape_geometry(cube).front().mesh;auto extra=std::vector<uint32_t>(nonmanifold.indices.begin(),nonmanifold.indices.begin()+3);nonmanifold.indices.insert(nonmanifold.indices.end(),extra.begin(),extra.end());
    require(fracture_mesh(nonmanifold,{1,1,1}).empty(),"Nonmanifold fracture input is rejected instead of generating invalid physical pieces");
    SceneData unsupported;unsupported.Game.emplace();unsupported.Game->SpawnZ=0;Character bad;bad.Sculpt=prepare_sculpt(nonmanifold);bad.GameplayRole="breakable";bad.Z=-4;bad.Y=.5;unsupported.Characters={bad};AdventurePlayer broken;broken.start(unsupported);broken.fire_button(true);broken.fire_button(false);for(int i=0;i<310;++i)broken.move(0,0,1./120.);auto debris=broken.presentation().fragments;
    require(broken.scene().Characters.empty()&&debris.size()==16&&std::all_of(debris.begin(),debris.end(),[](const auto& f){return !f.part;}),"Unsupported gameplay fracture removes source and emits sixteen generic non-rigid fragments");
    Mesh panels;for(int i=0;i<8001;++i){double x=(i%100)*.03,z=(i/100)*.03;int base=int(panels.vertices.size());panels.vertices.push_back({{x,0,z}});panels.vertices.push_back({{x+.01,0,z}});panels.vertices.push_back({{x,0,z+.01}});panels.indices.insert(panels.indices.end(),{base,base+1,base+2});}
    require(fracture_surface(panels,{1,1,1},false).empty(),"Shell fracture refuses input above its 8000-triangle limit");
    panels.indices.resize(24000);auto pieces=fracture_surface(panels,{1,1,1},false);size_t faces=0;for(const auto& piece:pieces)faces+=piece.part->mesh.indices.size()/3;
    require(!pieces.empty()&&pieces.size()<=64&&faces==8000*8,"Shell fracture accepts the exact triangle limit and preserves every source face within 64 groups");
    auto malformed=panels;malformed.indices[0]=uint32_t(malformed.vertices.size());bool rejected=false;try{fracture_surface(malformed,{1,1,1},false);}catch(const std::exception&){rejected=true;}require(rejected,"Malformed fracture indices fail validation before geometry access");
    ProjectilePhysics shots;for(int i=0;i<130;++i)shots.fire({0,1,0},{1,0,0},1,0);
    require(shots.shots().size()==128&&shots.shots().front().serial==3&&shots.shots().back().serial==130,"Projectile capacity keeps the newest 128 shots and retires the oldest first");
    shots.clear();require(shots.shots().empty(),"Projectile capacity state clears on reset");
    SceneData smoke;for(int i=0;i<17;++i){Character c;c.Shape="shadowenemy-smoke";c.X=i;smoke.Characters.push_back(c);}auto sources=smoke_sources(smoke,0,nullptr);
    require(sources.size()==16&&sources.back()[0]==15,"Fog source capacity admits the first 16 eligible emitters in scene order");
    smoke.Characters.erase(smoke.Characters.begin());sources=smoke_sources(smoke,0,nullptr);require(sources.size()==16&&sources.back()[0]==16,"Removing a fog emitter admits the next source without stale slots");
    EnvironmentEffects effects;for(int i=0;i<50;++i)effects.splash({{0,3,0},{},2,false});require(effects.splash_count()==1024,"Splash emission is bounded at 1024 independent droplets");effects.reset();require(effects.splash_count()==0,"Reset releases a fully populated droplet pool");
    Character rope;rope.Shape="cube";rope.Dynamic=true;rope.JointKind=3;bool valid=true;for(int n:{0,2,64}){rope.JointSegments=n;try{rope.validate();}catch(...){valid=false;}}require(valid,"Rope authoring accepts endpoints and the unsegmented mode");
    bool invalid=true;for(int n:{-1,1,65}){rope.JointSegments=n;bool threw=false;try{rope.validate();}catch(...){threw=true;}invalid&=threw;}require(invalid,"Rope authoring rejects negative, one-link and over-limit counts");
}
