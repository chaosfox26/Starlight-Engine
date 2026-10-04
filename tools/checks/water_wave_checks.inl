template<class Require> void water_wave_checks(Require require,const fs::path& folder){
    auto owner=std::make_unique<SceneData>();auto& scene=*owner;
    require(scene.WaterWindX==1.5&&scene.WaterWindZ==.5&&scene.WaterWaveStrength==.35,"Legacy water receives an authorable moderate breeze and wave strength");
    scene.WaterWindX=-3;scene.WaterWindZ=2;scene.WaterWaveStrength=.65;
    {auto saved=std::make_unique<SceneData>(SceneData::decode(scene.encode()));require(saved->WaterWindX==-3&&saved->WaterWindZ==2&&saved->WaterWaveStrength==.65,"Water wind and wave strength round trip through saved scenes");}
    auto source=json::parse(scene.encode());for(auto [key,value]:std::vector<std::pair<std::string,double>>{{"WaterWindX",21},{"WaterWindZ",-21},{"WaterWaveStrength",1.1},{"WaterWaveStrength",-.1}}){auto invalid=source;invalid[key]=value;bool rejected=false;try{auto saved=std::make_unique<SceneData>(SceneData::decode(invalid.dump()));}catch(...){rejected=true;}require(rejected,"Out-of-range water controls are rejected before simulation");}
    scene.Terrain.emplace();scene.Terrain->WaterEnabled=true;scene.Terrain->WaterLevel=1;
    auto flow=std::make_unique<TerrainFlow>(*scene.Terrain,std::vector<LiquidSource>{});flow->wind(2,.7,.6);double volume=flow->volume();
    for(int i=0;i<480;++i)flow->step(1./120.);
    double low=10,high=-10;auto snapshot=std::make_unique<LiquidSnapshot>(flow->snapshot());
    bool snapshotAgree=true;for(int z=1;z<47;++z)for(int x=1;x<47;++x){double px=-12+(x+.5)*.5,pz=-12+(z+.5)*.5;auto actual=flow->liquid_at(px,pz);low=std::min(low,actual.first);high=std::max(high,actual.first);snapshotAgree&=std::abs(actual.first-snapshot->at(px,pz).first)<1e-12;}
    require(snapshotAgree,"Physics snapshot retains the visible wave height");
    require(high-low>.015&&high-low<=.600001&&std::abs(flow->volume()-volume)<1e-8,"Wind produces bounded metre-scale geometry waves without changing fluid volume");
    auto geometry=flow->geometry();bool consistent=true,continuousDepth=true,finite=true;
    for(const auto& part:geometry)for(const auto& vertex:part.mesh.vertices){auto sample=flow->liquid_at(vertex.position.x,vertex.position.z);if(sample.second>0)consistent&=std::abs(vertex.position.y-sample.first-.009)<1e-8;continuousDepth&=std::abs(vertex.color.x*2-(vertex.position.y-scene.Terrain->height_at(vertex.position.x,vertex.position.z)))<1e-8;finite&=std::isfinite(vertex.normal.x+vertex.normal.y+vertex.normal.z)&&std::abs(vertex.normal.length()-1)<1e-8;}
    require(consistent&&continuousDepth&&finite,"Water triangles follow the shared liquid surface with smooth normals and continuous shoreline depth");
    {auto shallow=std::make_unique<TerrainFlow>(*scene.Terrain,std::vector<LiquidSource>{});shallow->water.fill(.05);shallow->wind(4,1,1);for(int i=0;i<480;++i)shallow->step(1./120.);bool bounded=true;for(int z=0;z<48;++z)for(int x=0;x<48;++x){double height=shallow->liquid_at(-12+(x+.5)*.5,-12+(z+.5)*.5).first;bounded&=height>=.025-1e-9&&height<=.075+1e-9;}require(bounded,"Shallow water limits both wave troughs and crests to available depth");}
    auto calm=std::make_unique<TerrainFlow>(*scene.Terrain,std::vector<LiquidSource>{});calm->wind(2,.7,0);for(int i=0;i<240;++i)calm->step(1./120.);require(calm->wave_energy()==0,"Zero authored wind-wave strength retains a calm surface without disabling impact ripples");calm->impulse(0,0,1);calm->step(1./120.);require(calm->wave_energy()>0,"Calm wind still permits physically triggered surface ripples");
    auto lava=std::make_unique<TerrainFlow>(*scene.Terrain,std::vector<LiquidSource>{});lava->water.fill(0);lava->lava.fill(2);lava->wind(2,.7,1);for(int i=0;i<240;++i)lava->step(1./120.);require(lava->wave_energy()==0,"Water wind does not inject water waves into molten lava");
    scene.Name="Moving shoreline and water geometry";scene.Hour=15;scene.FogDensity=.004;scene.WaterWindX=2;scene.WaterWindZ=.7;scene.WaterWaveStrength=.6;scene.Terrain->WaterLevel=0;
    for(int z=0;z<97;++z)for(int x=0;x<97;++x){double px=x*.25-12,pz=z*.25-12;scene.Terrain->Heights[z*97+x]=-.8+1.4*std::exp(-((px-3)*(px-3)+pz*pz)/12);scene.Terrain->Paint[z*97+x]=2;}
    Character rock;rock.Id="shore-rock";rock.Shape="boulder";rock.X=2.8;rock.Z=-2;rock.Size=1.1;rock.Surface=4;rock.Color="#89827A";scene.Characters={rock};
    auto shore=std::make_unique<TerrainFlow>(*scene.Terrain,std::vector<LiquidSource>{});shore->wind(scene.WaterWindX,scene.WaterWindZ,scene.WaterWaveStrength);
    bool flatShore=true;for(const auto& part:shore->geometry())for(const auto& vertex:part.mesh.vertices)flatShore&=std::abs(vertex.position.y-.009)<1e-8;
    require(flatShore,"Calm shore water extrapolates liquid height instead of climbing the dry bank");
    auto save=[&](const char* name,double clock){auto packet=render_scene(scene,shore.get());atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));atomic_write(folder/(std::string(name)+".json"),json{{"simulationTime",clock},{"volume",shore->volume()},{"waveEnergy",shore->wave_energy()}}.dump(2));};
    for(int i=0;i<120;++i)shore->step(1./120.);save("water-shoreline-start",1);
    for(int i=0;i<240;++i)shore->step(1./120.);save("water-shoreline-motion",3);
    bool clearBank=true;for(const auto& part:shore->geometry())for(const auto& vertex:part.mesh.vertices)clearBank&=vertex.position.y-scene.Terrain->height_at(vertex.position.x,vertex.position.z)>=-.025;
    require(clearBank,"Moving water shoreline does not protrude below the sampled sloping bank");
    atomic_write(folder/"water-shoreline.starscene",scene.encode());
    atomic_write(folder/"water-wave-diagnostics.json",json{{"minimumSurface",low},{"maximumSurface",high},{"volume",volume},{"waveEnergy",flow->wave_energy()},{"visibleAndPhysicsAgree",consistent},{"continuousDepth",continuousDepth}}.dump(2));
}
