// Deliberately neutral surfaces isolate lighting from texture appearance.
// Render checks compare these at the same camera and accumulation frame.
inline void lighting_fixtures(const fs::path& folder){
 auto owner=std::make_unique<SceneData>();auto& scene=*owner;
 scene.Name="Shared surface lighting comparison";scene.Hour=12;scene.FogDensity=0;scene.GroundColor="#888888";
 Character wall;wall.Id="neutral-wall";wall.Shape="cube";wall.Color="#888888";wall.Width=4;wall.Height=2.5;wall.Depth=.25;wall.Z=-1;wall.Surface=0;
 Character rock;rock.Id="lighting-rock";rock.Shape="boulder";rock.Color="#9C948D";rock.X=-3;rock.Size=1.6;rock.Surface=4;
 scene.Characters={wall,rock};
 auto save=[&](const char* name){auto packet=render_scene(scene);atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));atomic_write(folder/(std::string(name)+".starscene"),scene.encode());};
 save("lighting-day");scene.Hour=24;save("lighting-night");
 Character light;light.Id="lighting-key";light.Shape="light";light.X=0;light.Y=.7;light.Z=1;light.Size=.04;light.Color="#FFFFFF";light.LightRange=12;light.LightIntensity=24;light.LightCone=180;
 scene.Characters.push_back(light);save("lighting-near");scene.Characters.back().Z=3;save("lighting-far");
 scene.Characters.back().Color="#9B4DFF";save("lighting-violet");
 scene.Characters={wall};scene.Hour=12;scene.Terrain.emplace();std::fill(scene.Terrain->Heights.begin(),scene.Terrain->Heights.end(),-1.5);scene.Terrain->WaterEnabled=true;scene.Terrain->WaterLevel=0;
 scene.Characters[0].Height=3;scene.Characters[0].Width=4;scene.Characters[0].Y=.1;scene.Characters[0].Z=-3;scene.Characters[0].Color="#DE3C24";save("water-reflection-red");
 scene.Characters[0].Color="#245BDE";save("water-reflection-blue");

 // Real fluid mesh and a submerged receiver isolate transmitted lighting.
 // Preserve the older reflection/shadow fixtures unchanged.
 auto waterOwner=std::make_unique<SceneData>();auto& waterScene=*waterOwner;
 waterScene.Name="Light through water";waterScene.Hour=24;waterScene.FogDensity=0;waterScene.GroundColor="#888888";
 waterScene.Terrain.emplace();auto& bed=*waterScene.Terrain;
 std::fill(bed.Heights.begin(),bed.Heights.end(),-1.5);std::fill(bed.Paint.begin(),bed.Paint.end(),3);
 bed.WaterEnabled=true;bed.WaterLevel=0;waterScene.WaterWindX=waterScene.WaterWindZ=waterScene.WaterWaveStrength=0;
 auto waterFlow=std::make_unique<TerrainFlow>(bed,std::vector<LiquidSource>{});
 auto saveWater=[&](const char* name){auto packet=render_scene(waterScene,waterFlow.get());atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));atomic_write(folder/(std::string(name)+".starscene"),waterScene.encode());};
 saveWater("water-light-off");
 auto waterLight=light;waterLight.Id="above-water-light";waterLight.X=-3;waterLight.Y=3;waterLight.Z=0;waterLight.LightRange=15;waterLight.LightIntensity=40;waterLight.Color="#FFFFFF";
 waterScene.Characters={waterLight};saveWater("water-light-on");
 Character receiver;receiver.Id="submerged-color-receiver";receiver.Shape="cube";receiver.Width=receiver.Depth=12;receiver.Height=.1;receiver.Y=-1.4;receiver.Surface=0;
 receiver.Color="#BF5A38";waterScene.Characters.push_back(receiver);saveWater("water-bed-red");waterScene.Characters.back().Color="#386CBF";saveWater("water-bed-blue");waterScene.Characters.pop_back();
 Character waterBlocker;waterBlocker.Id="opaque-over-water";waterBlocker.Shape="cube";waterBlocker.Color="#888888";waterBlocker.X=-1.5;waterBlocker.Y=.7;waterBlocker.Width=2;waterBlocker.Height=.2;waterBlocker.Depth=4;
 waterScene.Characters.push_back(waterBlocker);saveWater("water-light-blocked");

 scene.Terrain.reset();scene.Hour=15;scene.GroundColor="#A0A0A0";scene.Characters.clear();scene.Name="Triangle-cast shadow coverage";
 Character blocker;blocker.Id="shadow-blocker";blocker.Shape="cube";blocker.Color="#929292";blocker.Width=2;blocker.Height=3;blocker.Depth=.8;blocker.Surface=0;
 scene.Characters={blocker};save("sun-shadow-cube");
 scene.Characters[0].X=2;save("sun-shadow-moved");
 scene.Characters[0]=blocker;scene.Characters[0].Shape="boulder";scene.Characters[0].Size=2;scene.Characters[0].Surface=4;save("sun-shadow-rock");
 scene.Characters.clear();save("sun-shadow-empty");
 scene.Characters={blocker};scene.Hour=24;save("sun-shadow-night");

 blocker.Width=1.2;blocker.Height=1.5;blocker.Depth=1.2;scene.Characters={blocker};scene.Name="Local-light triangle shadows";
 light.Id="local-shadow-key";light.X=-2;light.Y=3;light.Z=1.5;light.LightRange=15;light.LightIntensity=35;light.Color="#FFFFFF";light.LightCone=180;
 scene.Characters.push_back(light);save("local-shadow-white");
 scene.Characters.back().X=2;save("local-shadow-moved");scene.Characters.back()=light;scene.Characters.back().Color="#9B4DFF";save("local-shadow-purple");
 scene.Characters.back()=light;scene.Characters.back().LightCone=60;scene.Characters.back().RotationX=30;scene.Characters.back().RotationZ=40;save("local-shadow-cone");
 scene.Characters={light};save("local-shadow-empty");
 scene.Characters={blocker};for(int i=0;i<32;++i){auto lamp=light;lamp.Id="full-shadow-light-"+std::to_string(i);lamp.X=(i%8-3.5)*2;lamp.Z=(i/8-1.5)*2;lamp.LightIntensity=1;lamp.LightRange=6;scene.Characters.push_back(lamp);}save("local-shadow-all32");

}
