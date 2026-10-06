#include "rejection_note.hpp"
template<class Require> void rain_checks(Require require,const fs::path& folder){
    SceneData legacy=SceneData::decode("{\"SchemaVersion\":16}");
    require(legacy.BloomQuality==2&&legacy.BloomStrength==.12,"Legacy scenes receive the shared moderate glow default without a migration");
    auto glow=legacy;glow.BloomQuality=3;glow.BloomStrength=.23;auto savedGlow=SceneData::decode(glow.encode());
    require(savedGlow.BloomQuality==3&&savedGlow.BloomStrength==.23,"Authored glow quality and intensity persist through scene save and reload");
    bool rejectedQuality=false,rejectedStrength=false;
    try{auto invalid=glow;invalid.BloomQuality=4;invalid.validate();}catch(...){rejectedQuality=true;note_rejection(__FILE__,__LINE__);}
    try{auto invalid=glow;invalid.BloomStrength=std::numeric_limits<double>::quiet_NaN();invalid.validate();}catch(...){rejectedStrength=true;note_rejection(__FILE__,__LINE__);}
    require(rejectedQuality&&rejectedStrength,"Glow settings reject unsupported presets and nonfinite intensities before rendering");
    require(legacy.RainIntensity==0&&legacy.RainWindX==0&&legacy.RainWindZ==0,"Older scenes keep weather disabled without new required fields");
    SceneData scene;scene.RainIntensity=.65;scene.RainWindX=2;scene.RainWindZ=-1;scene.FogDensity=.01;scene.Terrain.emplace();
    auto saved=SceneData::decode(scene.encode());
    require(saved.RainIntensity==.65&&saved.RainWindX==2&&saved.RainWindZ==-1,"Rain intensity and wind survive scene save and reload");
    for(auto value:{-1.,1.1,std::numeric_limits<double>::infinity()}){auto invalid=scene;invalid.RainIntensity=value;bool rejected=false;try{invalid.validate();}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}require(rejected,"Invalid rain intensity is rejected at the authored scene boundary");}
    for(auto value:{-21.,21.,std::numeric_limits<double>::quiet_NaN()}){auto invalid=scene;invalid.RainWindX=value;bool rejected=false;try{invalid.validate();}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}require(rejected,"Invalid rain wind is rejected at the authored scene boundary");}
    RainWeather weather;weather.step(legacy,.05,{},nullptr);require(weather.particles().empty(),"Default scenes do not spawn rain or change existing visuals");
    WeatherSurface moisture,fineMoisture;for(int i=0;i<200;++i)moisture.step(1,.05);for(int i=0;i<400;++i)fineMoisture.step(1,.025);
    require(moisture.moisture()>.9&&std::abs(moisture.moisture()-fineMoisture.moisture())<1e-12,"Wet surfaces accumulate consistently across simulation frame subdivisions");
    auto pausedMoisture=moisture.moisture();moisture.step(1,0);require(moisture.moisture()==pausedMoisture,"Paused rain freezes surface moisture");
    for(int i=0;i<400;++i)moisture.step(0,.05);require(moisture.moisture()>0&&moisture.moisture()<pausedMoisture,"Surfaces dry gradually after rain stops");
    moisture.reset();require(moisture.moisture()==0,"Weather reset restores dry materials");
    scene.RainIntensity=1;weather.step(scene,0,{},nullptr);require(weather.particles().empty(),"Paused weather does not emit or advance rain");
    weather.step(scene,.025,{},nullptr);auto original=weather.particles();auto first=weather.geometry();weather.step(scene,.025,{},nullptr);auto moved=weather.geometry();
    require(original.size()==30&&weather.particles().size()==60,"Rain emission uses elapsed time and bounded fractional accumulation");
    bool history=!first.empty()&&!moved.empty(),motion=true,normals=true;std::set<uint64_t> ids;
    for(size_t i=0;i<first[0].mesh.vertices.size();++i){history&=first[0].mesh.vertices[i].motionIdentity==moved[0].mesh.vertices[i].motionIdentity;normals&=std::abs(first[0].mesh.vertices[i].normal.length()-1)<1e-9;}
    for(size_t i=0;i<original.size();++i){motion&=weather.particles()[i].position.y<original[i].position.y&&weather.particles()[i].velocity.x==2&&weather.particles()[i].velocity.z==-1;ids.insert(original[i].motionId);}
    require(history&&motion&&ids.size()==original.size()&&normals,"Falling rain retains stable motion identities, wind direction and smooth 3D normals");
    bool finite=true;size_t maximum=0;uint64_t hits=0;double impactHeight=0;
    for(int i=0;i<160;++i){weather.step(scene,.025,{},nullptr);maximum=std::max(maximum,weather.particles().size());for(const auto& p:weather.particles())finite&=std::isfinite(p.position.x+p.position.y+p.position.z)&&p.position.y>=p.radius-1e-5;for(const auto& impact:weather.impacts()){++hits;impactHeight=std::max(impactHeight,impact.position.y);}}
    require(finite&&hits>500&&maximum<=RainWeather::Capacity,"Rain hits terrain without tunneling and remains within its particle budget");
    RainWeather roof;Bounds shelter{{-30,2,-30},{30,3,30}};std::array<Bounds,1> obstacles{shelter};bool roofOnly=true;uint64_t roofHits=0;
    for(int i=0;i<180;++i){roof.step(scene,.025,obstacles,nullptr);for(const auto& impact:roof.impacts()){++roofHits;roofOnly&=impact.position.y>=3&&impact.position.y<=3.03;}}
    require(roofHits>500&&roofOnly,"Shelter roofs intercept falling rain before the ground underneath");
    auto mound=scene;mound.RainWindX=0;mound.RainWindZ=0;mound.Terrain->Volumes.push_back({0,0,0,6,false});RainWeather solid;uint64_t solidHits=0;bool onSurface=true;
    for(int i=0;i<180;++i){solid.step(mound,.025,{},nullptr);for(const auto& impact:solid.impacts()){if(impact.position.y>.1){++solidHits;onSurface&=std::abs(mound.Terrain->density_at(impact.position.x,impact.position.y,impact.position.z))<.02;}}}
    require(solidHits>100&&onSurface,"Rain contact follows added solid terrain rather than the obsolete flat heightfield");
    auto flowOwner=std::make_unique<TerrainFlow>(*scene.Terrain,std::vector<LiquidSource>{});auto& flow=*flowOwner;flow.water.fill(.25);RainWeather lake;uint64_t waterHits=0;
    for(int i=0;i<160;++i){lake.step(scene,.025,{},&flow);for(const auto& impact:lake.impacts())waterHits+=impact.water;}
    require(waterHits>500&&flow.wave_energy()>0,"Rain water contact drives ripples in the existing liquid simulation");
    auto dry=scene;dry.RainIntensity=0;for(int i=0;i<500;++i)weather.step(dry,.05,{},nullptr);
    require(weather.particles().empty(),"Switching rain off lets existing drops finish without unbounded respawning");
    weather.reset();require(weather.particles().empty()&&weather.impacts().empty()&&weather.impact_count()==0,"Weather reset removes runtime rain and impact state");
    EnvironmentEffects effects;for(int i=0;i<100;++i)effects.step(scene,.025,i*.025);require(effects.active(scene)&&!effects.rain().particles().empty()&&effects.splash_count()>0,"Shared editor and player effects submit rain with impact splash droplets");
    auto save=[&](const char* name,const SceneData& fixture,const std::vector<MeshPart>& overlay){auto packet=render_scene(fixture,nullptr,overlay,nullptr,true);atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));};
    Character stone;stone.Shape="boulder";stone.Id="rain-stone";stone.Y=0;stone.Size=2;stone.Color="#879090";scene.Characters.push_back(stone);scene.Name="Rain weather - working graphics prototype";
    save("rain-weather",scene,effects.geometry(scene));save("rain-weather-empty",scene,{});
    effects.step(scene,.025,2.525);save("rain-weather-moved",scene,effects.geometry(scene));
    auto blocked=scene;Character wall;wall.Shape="cube";wall.Id="rain-occlusion-wall";wall.Y=-10;wall.Z=14;wall.Width=60;wall.Height=60;wall.Depth=.3;wall.Color="#445E68";blocked.Characters.push_back(wall);
    save("rain-weather-blocked",blocked,effects.geometry(scene));save("rain-weather-blocked-empty",blocked,{});
    atomic_write(folder/"rain-weather.starscene",scene.encode());
    auto geometry=effects.geometry(scene);size_t vertices=0,triangles=0,bytes=0;for(const auto& part:geometry){vertices+=part.mesh.vertices.size();triangles+=part.mesh.indices.size()/3;bytes+=part.mesh.vertices.capacity()*sizeof(Vertex)+part.mesh.indices.capacity()*sizeof(int);}
    atomic_write(folder/"rain-weather-diagnostics.json",json{{"rainCount",effects.rain().particles().size()},{"impactCount",effects.rain().impact_count()},{"splashCount",effects.splash_count()},{"maximumRain",maximum},{"groundHits",hits},{"roofHits",roofHits},{"solidHits",solidHits},{"waterHits",waterHits},{"waveEnergy",flow.wave_energy()},{"vertices",vertices},{"triangles",triangles},{"cpuMeshBytes",bytes}}.dump(2));
    effects.reset();require(!effects.active(legacy)&&effects.rain().particles().empty(),"Reset restores disabled legacy scene state without retained rain geometry");
    auto currentOwner=std::make_unique<SceneData>();auto& current=*currentOwner;current.Name="Current graphics - textured terrain, curved grass and geological rocks";current.Terrain.emplace();current.FogDensity=.007;current.RayReconstructionEnabled=false;
    for(int z=0;z<TerrainData::N;++z)for(int x=0;x<TerrainData::N;++x){double px=x*TerrainData::Step-TerrainData::Half,pz=z*TerrainData::Step-TerrainData::Half;current.Terrain->Heights[z*TerrainData::N+x]=.5*std::exp(-((px-4)*(px-4)+(pz+4)*(pz+4))*.06);}
    for(int z=0;z<6;++z)for(int x=0;x<8;++x){Character blade;blade.Shape="grass";blade.Id="current-grass-"+std::to_string(z*8+x);blade.X=-6+x*1.5;blade.Z=-6+z*1.5;blade.Y=current.Terrain->height_at(blade.X,blade.Z);current.Characters.push_back(blade);}
    for(int i=0;i<4;++i){Character rock;rock.Shape="boulder";rock.Id="current-rock-"+std::to_string(i);rock.X=-4+i*2.5;rock.Z=-3;rock.Y=current.Terrain->height_at(rock.X,rock.Z);rock.Color="#A4A7A5";current.Characters.push_back(rock);}
    Character currentWall;currentWall.Shape="cube";currentWall.Id="current-textured-wall";currentWall.Surface=4;currentWall.X=-5;currentWall.Y=0;currentWall.Z=-7;currentWall.Width=3;currentWall.Height=3;currentWall.Depth=.35;currentWall.Color="#A8A0A0";current.Characters.push_back(currentWall);
    auto currentPacket=render_scene(current);atomic_write(folder/"current-graphics.bin",std::string_view(reinterpret_cast<const char*>(currentPacket.data()),currentPacket.size()));atomic_write(folder/"current-graphics.starscene",current.encode());
    require(current.Characters.back().Surface==4&&current.Characters.size()==53,"Current graphics scene contains new grass meshes, textured walls and rebuilt rocks");
    SceneData grassOnly;Character grass;grass.Shape="grass";grassOnly.Characters={grass};EnvironmentEffects gpuEffects;
    gpuEffects.step(grassOnly,.025,.025,nullptr,{},true);
    require(!gpuEffects.active(grassOnly,true)&&gpuEffects.geometry(grassOnly).empty(),"GPU grass does not allocate or rebuild CPU vegetation overlays");
    RenderSceneCache grassCache;auto resident=render_scene(grassOnly,nullptr,{},&grassCache,true,true);uint32_t residentCount=0;std::memcpy(&residentCount,resident.data()+4,4);
    require(residentCount==2,"GPU grass retains the complete authored blade mesh with the ground in the resident packet");
    auto cpuPacket=render_scene(grassOnly,nullptr,{},&grassCache,true,false);uint32_t cpuCount=0;std::memcpy(&cpuCount,cpuPacket.data()+4,4);
    require(cpuCount==1&&resident==render_scene(grassOnly,nullptr,{},&grassCache,true,true),"Switching CPU and GPU grass invalidates cached vegetation without duplication or stale meshes");
}
