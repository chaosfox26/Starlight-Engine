#include <array>
#include <map>
#include <numbers>
template<class Require> void club_schema_checks(Require require,const fs::path& folder){
    const auto work=folder/"club-schema-checks";fs::remove_all(work);fs::create_directories(work);
    const std::string emptyGolden="{\n"
        "  \"BloomQuality\": 2,\n"
        "  \"BloomStrength\": 0.12,\n"
        "  \"Characters\": [],\n"
        "  \"DayCycle\": false,\n"
        "  \"DayLength\": 180.0,\n"
        "  \"FogDensity\": 0.018,\n"
        "  \"FreezeRaytracingAnimation\": false,\n"
        "  \"GroundColor\": \"#182033\",\n"
        "  \"Hour\": 15.0,\n"
        "  \"LavaCoolingRate\": 0.012,\n"
        "  \"LavaCrustDepth\": 0.08,\n"
        "  \"LavaInitialHeat\": 1.0,\n"
        "  \"LavaLightStrength\": 1.0,\n"
        "  \"LiquidSources\": [],\n"
        "  \"Name\": \"My first scene\",\n"
        "  \"PathBounces\": 3,\n"
        "  \"RainIntensity\": 0.0,\n"
        "  \"RainWindX\": 0.0,\n"
        "  \"RainWindZ\": 0.0,\n"
        "  \"RayReconstructionEnabled\": true,\n"
        "  \"RaytracingEnabled\": false,\n"
        "  \"RaytracingQuality\": 1,\n"
        "  \"SchemaVersion\": 16,\n"
        "  \"ShadowQuality\": 2,\n"
        "  \"Terrain\": null,\n"
        "  \"WaterWaveStrength\": 0.35,\n"
        "  \"WaterWindX\": 1.5,\n"
        "  \"WaterWindZ\": 0.5\n"
        "}";
    const std::string modelGolden="{\n"
        "  \"BloomQuality\": 2,\n"
        "  \"BloomStrength\": 0.12,\n"
        "  \"Characters\": [\n"
        "    {\n"
        "      \"AngularVelocityX\": 0.0,\n"
        "      \"AngularVelocityY\": 0.0,\n"
        "      \"AngularVelocityZ\": 0.0,\n"
        "      \"Asset\": \"model-6892a6a3d79a778262ef3b6dceb6fc887e277e19aa45fda1478b64f3b7693478\",\n"
        "      \"BranchSpread\": 1.0,\n"
        "      \"Breakable\": true,\n"
        "      \"Color\": \"#B7C6E0\",\n"
        "      \"CombatProjectileGravity\": 0.0,\n"
        "      \"CombatProjectileMass\": 0.02,\n"
        "      \"CombatProjectileSpeed\": 0.0,\n"
        "      \"Density\": 2700.0,\n"
        "      \"Depth\": 1.0,\n"
        "      \"DestructionHealth\": 10.0,\n"
        "      \"Dynamic\": false,\n"
        "      \"Friction\": 0.72,\n"
        "      \"Height\": 1.0,\n"
        "      \"Id\": \"3c96cfb907504c55a5e1f13d75871b58\",\n"
        "      \"JointAngleMax\": 180.0,\n"
        "      \"JointAngleMin\": -180.0,\n"
        "      \"JointAxisX\": 0.0,\n"
        "      \"JointAxisY\": 1.0,\n"
        "      \"JointAxisZ\": 0.0,\n"
        "      \"JointDamping\": 0.5,\n"
        "      \"JointFrequency\": 3.0,\n"
        "      \"JointKind\": 0,\n"
        "      \"JointLength\": 1.0,\n"
        "      \"JointRadius\": 0.025,\n"
        "      \"JointSegments\": 0,\n"
        "      \"JointTargetId\": \"\",\n"
        "      \"JointTargetX\": 0.0,\n"
        "      \"JointTargetY\": 0.0,\n"
        "      \"JointTargetZ\": 0.0,\n"
        "      \"JointX\": 0.0,\n"
        "      \"JointY\": 0.0,\n"
        "      \"JointZ\": 0.0,\n"
        "      \"KeepAboveFloor\": true,\n"
        "      \"LightCone\": 180.0,\n"
        "      \"LightIntensity\": 12.0,\n"
        "      \"LightRange\": 6.0,\n"
        "      \"LightningChainRadius\": 3.0,\n"
        "      \"LightningChains\": 0,\n"
        "      \"LightningDamage\": 0.0,\n"
        "      \"Name\": \"fox-lead\",\n"
        "      \"ParticleDrag\": 0.6,\n"
        "      \"ParticleLifetime\": 1.0,\n"
        "      \"ParticleLift\": 14.0,\n"
        "      \"ParticleRate\": 40.0,\n"
        "      \"ParticleWindX\": 0.3,\n"
        "      \"ParticleWindY\": 0.0,\n"
        "      \"ParticleWindZ\": 0.15,\n"
        "      \"Restitution\": 0.03,\n"
        "      \"RotationX\": 0.0,\n"
        "      \"RotationY\": 0.0,\n"
        "      \"RotationZ\": 0.0,\n"
        "      \"Route\": [],\n"
        "      \"Sculpt\": null,\n"
        "      \"Shape\": null,\n"
        "      \"Size\": 1.0,\n"
        "      \"Speed\": 1.5,\n"
        "      \"Surface\": 0,\n"
        "      \"TargetX\": 1.0,\n"
        "      \"TargetY\": 0.05,\n"
        "      \"TargetZ\": 0.0,\n"
        "      \"VelocityX\": 0.0,\n"
        "      \"VelocityY\": 0.0,\n"
        "      \"VelocityZ\": 0.0,\n"
        "      \"Width\": 1.0,\n"
        "      \"X\": 0.0,\n"
        "      \"Y\": 0.0,\n"
        "      \"Z\": 0.0\n"
        "    }\n"
        "  ],\n"
        "  \"DayCycle\": false,\n"
        "  \"DayLength\": 180.0,\n"
        "  \"FogDensity\": 0.018,\n"
        "  \"FreezeRaytracingAnimation\": false,\n"
        "  \"GroundColor\": \"#182033\",\n"
        "  \"Hour\": 15.0,\n"
        "  \"LavaCoolingRate\": 0.012,\n"
        "  \"LavaCrustDepth\": 0.08,\n"
        "  \"LavaInitialHeat\": 1.0,\n"
        "  \"LavaLightStrength\": 1.0,\n"
        "  \"LiquidSources\": [],\n"
        "  \"Name\": \"My first scene\",\n"
        "  \"PathBounces\": 3,\n"
        "  \"RainIntensity\": 0.0,\n"
        "  \"RainWindX\": 0.0,\n"
        "  \"RainWindZ\": 0.0,\n"
        "  \"RayReconstructionEnabled\": true,\n"
        "  \"RaytracingEnabled\": false,\n"
        "  \"RaytracingQuality\": 1,\n"
        "  \"SchemaVersion\": 16,\n"
        "  \"ShadowQuality\": 2,\n"
        "  \"Terrain\": null,\n"
        "  \"WaterWaveStrength\": 0.35,\n"
        "  \"WaterWindX\": 1.5,\n"
        "  \"WaterWindZ\": 0.5\n"
        "}";
    auto failure=[](auto&& action)->std::string{try{action();}catch(const std::exception& problem){return problem.what();}return {};};
    auto mentions=[](const std::string& message,const char* text){return !message.empty()&&message.find(text)!=std::string::npos;};
    auto solid=[](const char* id,const char* shape){Character c;c.Id=id;c.Name=id;c.Shape=shape;c.KeepAboveFloor=false;c.Breakable=false;return c;};

    {
        require(SceneData::decode(emptyGolden).encode()==emptyGolden,"A scene written before the club-room fields decodes and encodes to the identical bytes");
        require(SceneData::decode(modelGolden).encode()==modelGolden,"A scene holding a model object written before the club-room fields decodes and encodes to the identical bytes");
        bool leaked=false;
        SceneData plain;plain.Characters={solid("plain-cube","cube"),solid("plain-light","light")};
        const auto document=json::parse(plain.encode());
        for(const char* key:{"Lighting","Moods","ActiveMood","View","Zones","Crowd","Cast"})leaked|=document.contains(key);
        for(const auto& object:document.at("Characters"))for(const char* key:{"Glow","Group","LightInner","RingInner","Follow"})leaked|=object.contains(key);
        require(!leaked,"A scene without the new blocks and fields writes none of their keys");
        require(!plain.Lighting&&plain.Moods.empty()&&plain.ActiveMood.empty()&&!plain.View&&plain.Zones.empty()&&!plain.Crowd&&plain.Cast.Models.empty(),"The new scene blocks default to absent");
        const Character defaults;
        require(defaults.Glow==0&&defaults.Group.empty()&&defaults.LightInner==.8&&defaults.RingInner==.82&&!defaults.Follow,"The new object fields default to the values existing scenes already behave as");
    }

    auto volume=[](const Mesh& mesh){double result=0;for(size_t i=0;i<mesh.indices.size();i+=3){auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;result+=a.dot(b.cross(c))/6.;}return result;};
    auto closed=[](const Mesh& mesh){
        std::map<std::array<long long,6>,int> edges;
        auto key=[](const Vec3& p){return std::array<long long,3>{std::llround(p.x*1e7),std::llround(p.y*1e7),std::llround(p.z*1e7)};};
        for(size_t i=0;i<mesh.indices.size();i+=3)for(int e=0;e<3;++e){
            auto a=key(mesh.vertices[mesh.indices[i+e]].position),b=key(mesh.vertices[mesh.indices[i+(e+1)%3]].position);
            if(b<a)std::swap(a,b);
            edges[{a[0],a[1],a[2],b[0],b[1],b[2]}]++;
        }
        return std::all_of(edges.begin(),edges.end(),[](const auto& edge){return edge.second==2;});
    };
    {
        const double sector=24*std::sin(2*std::numbers::pi/48);
        auto octagon=shape_geometry(solid("o","octagon")).front().mesh;
        auto cylinderParts=shape_geometry(solid("c","cylinder"));
        const auto& cylinder=cylinderParts.front().mesh;
        require(cylinderParts.size()==1&&!cylinder.vertices.empty()&&cylinder.indices.size()%3==0,"A cylinder is one mesh of whole triangles");
        double radius=0,low=1e9,high=-1e9;for(const auto& v:cylinder.vertices){radius=std::max(radius,std::hypot(v.position.x,v.position.z));low=std::min(low,v.position.y);high=std::max(high,v.position.y);}
        require(std::abs(radius-1)<1e-12&&low==0&&high==1,"A cylinder has unit radius and unit height standing on its base like every other solid shape");
        require(closed(cylinder),"A cylinder is a closed surface");
        require(std::abs(std::abs(volume(cylinder))-sector)<1e-9&&(volume(cylinder)>0)==(volume(octagon)>0),"A cylinder encloses the 48-sided prism volume with the same face orientation as the octagon");
        auto ringCharacter=solid("r","ring");ringCharacter.RingInner=.96;
        auto ring=shape_geometry(ringCharacter).front().mesh;
        double inner=1e9;for(const auto& v:ring.vertices)inner=std::min(inner,std::hypot(v.position.x,v.position.z));
        require(std::abs(inner-.96)<1e-12&&closed(ring),"A ring is a closed surface whose inner radius is the requested fraction of the outer radius");
        require(std::abs(std::abs(volume(ring))-sector*(1-.96*.96))<1e-9&&(volume(ring)>0)==(volume(octagon)>0),"A ring encloses the annulus volume with outward faces");
        auto solidRing=solid("rz","ring");solidRing.RingInner=0;
        require(std::abs(std::abs(volume(shape_geometry(solidRing).front().mesh))-sector)<1e-9,"A ring with inner ratio zero is a full disc");
        auto glowing=solid("g","cube");glowing.Glow=.5;
        require(shape_geometry(glowing).front().emissive&&!shape_geometry(solid("n","cube")).front().emissive,"Glow draws a cube emissive and no glow leaves it unchanged");
        auto glowingSlab=solid("gs","cylinder");glowingSlab.Glow=1;
        require(shape_geometry(glowingSlab).front().emissive&&shape_geometry(ringCharacter).front().emissive==false,"Glow applies to a cylinder and not to an unlit ring");
        auto plainCube=solid("k","cube"),glowCube=plainCube;glowCube.Glow=1;
        auto thin=ringCharacter,thick=ringCharacter;thick.RingInner=.5;
        require(geometry_key(plainCube)!=geometry_key(glowCube)&&render_geometry_key(plainCube)!=render_geometry_key(glowCube)&&geometry_key(thin)!=geometry_key(thick)&&render_geometry_key(thin)!=render_geometry_key(thick),"Glow and the ring inner ratio change the geometry identity so cached shapes are rebuilt");
        require(geometry_key(plainCube)==geometry_key(solid("k","cube")),"A cube without glow keeps the geometry identity it had before the field existed");
        require(std::find(shapes.begin(),shapes.end(),"cylinder")!=shapes.end()&&std::find(shapes.begin(),shapes.end(),"ring")!=shapes.end(),"Cylinder and ring are listed shapes");
    }

    {
        auto sum=[&](const Character& c){return failure([&]{c.validate();});};
        auto glowOnLight=solid("a","light");glowOnLight.Glow=1;
        require(mentions(sum(glowOnLight),"Glow applies only"),"Glow on a light is rejected");
        auto glowHigh=solid("b","cube");glowHigh.Glow=1.5;
        require(mentions(sum(glowHigh),"glow"),"A glow above one is rejected");
        auto glowNegative=solid("b2","cube");glowNegative.Glow=-.1;
        require(!sum(glowNegative).empty(),"A negative glow is rejected");
        auto innerOnCube=solid("c","cube");innerOnCube.RingInner=.5;
        require(mentions(sum(innerOnCube),"ring inner ratio applies only to a ring"),"A ring inner ratio on a cube is rejected");
        auto ringFull=solid("d","ring");ringFull.RingInner=1;
        require(!sum(ringFull).empty(),"A ring inner ratio of one is rejected");
        auto coneOnCube=solid("e","cube");coneOnCube.LightInner=.5;
        require(mentions(sum(coneOnCube),"inner cone ratio applies only to a light"),"An inner cone on a cube is rejected");
        auto coneTiny=solid("f","light");coneTiny.LightInner=.05;
        require(!sum(coneTiny).empty(),"An inner cone ratio below 0.1 is rejected");
        auto followOnCube=solid("g2","cube");followOnCube.Follow=LightFollow{};
        require(mentions(sum(followOnCube),"applies only to a light"),"Follow-spot behaviour on a cube is rejected");
        auto badGroup=solid("h","cube");badGroup.Group="has space";
        require(mentions(sum(badGroup),"group name"),"A group name with a space is rejected");
        auto longGroup=solid("h2","cube");longGroup.Group=std::string(65,'g');
        require(!sum(longGroup).empty(),"A group name over 64 characters is rejected");
        auto dynamicRing=solid("i","ring");dynamicRing.Dynamic=true;
        require(mentions(sum(dynamicRing),"Dynamic body needs a solid shape"),"A dynamic ring is rejected");
        auto dynamicCylinder=solid("j","cylinder");dynamicCylinder.Dynamic=true;
        require(sum(dynamicCylinder).empty(),"A dynamic cylinder is a valid solid body");
        auto spot=solid("k","light");spot.Follow=LightFollow{};
        auto expectFollow=[&](auto change,const char* text){auto c=spot;change(*c.Follow);return mentions(sum(c),text);};
        require(expectFollow([](LightFollow& f){f.Lag=-1;},"lag")&&expectFollow([](LightFollow& f){f.Lag=61;},"lag"),"Follow-spot lag outside 0-60 is rejected");
        require(expectFollow([](LightFollow& f){f.Wobble=21;},"wobble")&&expectFollow([](LightFollow& f){f.PoolRadius=0;},"pool radius"),"Follow-spot wobble and pool radius ranges are enforced");
        require(expectFollow([](LightFollow& f){f.Target="two words";},"target")&&expectFollow([](LightFollow& f){f.OffsetX=2000;},"offset"),"Follow-spot target name and offset are validated");
        require(expectFollow([](LightFollow& f){f.Housing=LightFollowHousing{};f.Housing->Color="not-a-color";},"")&&expectFollow([](LightFollow& f){f.Housing=LightFollowHousing{};f.Housing->Width=0;},"housing"),"A follow-spot housing needs a colour and a positive size");
        require(expectFollow([](LightFollow& f){f.Beam=LightFollowBeam{};f.Beam->Pool=3;},"beam"),"Follow-spot beam strengths above 2 are rejected");
        require(expectFollow([](LightFollow& f){f.Lag=std::nan("");},"lag"),"A non-finite follow-spot value is rejected");
    }

    auto lighting=[]{SceneLighting l;l.Mode="authored";l.KeyDirection={-.4,-.85,.35};l.KeyIntensity=3;l.KeyIntensityFlat=3.5;l.Ambient={.46,.42,.62};l.AmbientGround=.45;l.Clear={.043,.031,.09};l.FogMix=.35;l.Modulators["Darkness"]={{"Clear",-.65},{"Key",-.65},{"Ambient",-.65},{"FogMix",.5},{"group:neon",-.7},{"Spots",-.45}};return l;};
    auto tex=[](char c){return "texture-"+std::string(64,c);};
    auto castModel=[&](const char* id,int variants,bool atlas,bool fur){
        CastModel m;m.Id=id;m.Model="model-"+std::string(64,'a');m.Role="crowd";m.Variants=variants;
        CastRig rig;rig.Clips["idle"]="idle";rig.Clips["dodge"]="dodge_roll_f";rig.Joints["head"]="head";rig.Joints["handL"]="hand_l";rig.HeightRule=CastHeightRule{};m.Rig=rig;
        m.Uv.Layout=atlas?"atlas":"glb";if(atlas)m.Uv.Asset="blob-"+std::string(64,'c');
        for(int v=0;v<variants;++v){
            auto& slots=m.Maps["v"+std::to_string(v)];
            slots["basecolor"]=tex(char('0'+v));slots["normal"]=tex('a');slots["roughness"]=tex(char('4'+v));slots["ao"]=tex('b');
            if(fur){slots["furflow"]=tex(char('6'+v));slots["sparkle"]=tex('c');}
        }
        m.Requires={"model","skeleton","map:basecolor","map:normal","map:roughness","map:ao"};
        if(atlas)m.Requires.push_back("uv:atlas");
        if(fur){CastEffect effect;effect.Maps={"furflow","sparkle"};effect.Profiles["f"]=FurProfile{.012,.85,.25,.8,.5,1.};effect.Profiles["m"]=FurProfile{.016,.45,.8,.45,.3,.9};m.Effects["fur"]=effect;m.Wants={"effect:fur"};}
        return m;
    };
    auto full=[&]{
        SceneData s;s.Name="Club schema";
        auto neon=solid("neon-strip","cube");neon.Glow=1;neon.Group="neon";neon.Width=60;neon.Height=.12;neon.Depth=.12;neon.Color="#FF1ACC";
        auto slab=solid("slab","cylinder");slab.Width=9.3;slab.Depth=9.3;slab.Height=.1;slab.Group="stage";
        auto ring=solid("ring","ring");ring.Width=9.35;ring.Depth=9.35;ring.Height=.02;ring.RingInner=.96;ring.Glow=1;ring.Group="neon";ring.Y=.11;
        auto spot=solid("spot","light");spot.Y=6.8;spot.X=-4.5;spot.Z=2.5;spot.LightCone=20.17;spot.LightInner=.604;spot.LightIntensity=22;spot.LightRange=14.4;
        LightFollow follow;follow.OffsetX=-4.5;follow.OffsetZ=2.5;follow.Lag=3.2;follow.Wobble=.9;follow.PoolRadius=1.6;follow.Housing=LightFollowHousing{};follow.Beam=LightFollowBeam{};spot.Follow=follow;
        s.Characters={neon,slab,ring,spot};
        s.Lighting=lighting();
        SceneMood calm;calm.Id="default";calm.Clear={.043,.031,.09};
        SceneMood boss;boss.Id="boss";boss.Clear={.06,.02,.06};boss.Ambient=std::array<double,3>{.3,.2,.3};boss.KeyIntensity=2;boss.FogMix=.6;boss.AmbientGround=.3;
        s.Moods={calm,boss};s.ActiveMood="default";
        SceneView view;view.FarWhenDistanceAbove=ViewFarRule{};view.Follow=ViewFollow{};view.IdleOrbit=ViewIdleOrbit{};s.View=view;
        SceneZone floor;floor.Id="dancefloor";floor.Kind="raised-floor";floor.Shape="circle";floor.Radius=9.3;floor.Height=.1;
        SceneZone bar;bar.Id="bar";bar.Kind="anchor";bar.Shape="circle";bar.Z=22;bar.Radius=3.6;bar.Note="informational";
        SceneZone room;room.Id="room";room.Kind="bounds";room.Shape="box";room.Width=60;room.Depth=60;
        s.Zones={floor,bar,room};
        s.Crowd=SceneCrowd{};
        s.Cast.Models={castModel("lead",1,false,false),castModel("crowd",2,true,true)};
        return s;
    };
    const SceneData reference=full();
    const auto text=reference.encode();
    {
        const auto decoded=SceneData::decode(text);
        require(decoded.encode()==text,"A scene with every club-room block encodes to identical bytes after a decode");
        require(decoded.Lighting==reference.Lighting&&decoded.Moods==reference.Moods&&decoded.ActiveMood==reference.ActiveMood&&decoded.View==reference.View&&decoded.Zones==reference.Zones&&decoded.Crowd==reference.Crowd&&decoded.Cast==reference.Cast,"Lighting, moods, view, zones, crowd and cast survive a round trip unchanged");
        require(decoded.Characters==reference.Characters,"Glow, group, inner cone, ring ratio and follow-spot behaviour survive a round trip unchanged");
        const auto document=json::parse(text);
        require(document.at("SchemaVersion")==16,"The club-room fields live in schema 16");
        require(document.at("Characters")[3].contains("Follow")&&document.at("Characters")[3].at("Follow").at("Housing").at("Color")=="#120F1A"&&document.at("Characters")[3].at("LightInner")==.604,"The follow-spot block is written under the light object");
        require(!document.at("Characters")[0].contains("RingInner")&&document.at("Characters")[2].at("RingInner")==.96,"The ring ratio is written only for rings");
        require(document.at("Zones")[1].contains("Radius")&&!document.at("Zones")[1].contains("Width")&&document.at("Zones")[2].contains("Width")&&!document.at("Zones")[2].contains("Radius"),"A zone writes the size fields of its own shape");
        require(document.at("Cast").at("Models")[0].at("Uv")==json{{"Layout","glb"}}&&!document.at("Cast").at("Models")[0].contains("Effects")&&!document.at("Cast").at("Models")[0].contains("Wants"),"A model with no atlas, fur or sparkle writes none of them");
        auto emptied=reference;emptied.Cast.Models.clear();emptied.Moods.clear();emptied.ActiveMood.clear();emptied.Zones.clear();emptied.Lighting.reset();emptied.View.reset();emptied.Crowd.reset();
        for(auto& c:emptied.Characters){c.Glow=0;c.Group.clear();c.LightInner=.8;c.RingInner=.82;c.Follow.reset();}
        emptied.Characters.erase(std::remove_if(emptied.Characters.begin(),emptied.Characters.end(),[](const Character& c){return c.Shape=="ring"||c.Shape=="cylinder";}),emptied.Characters.end());
        const auto bare=json::parse(emptied.encode());
        bool clean=true;for(const char* key:{"Lighting","Moods","ActiveMood","View","Zones","Crowd","Cast"})clean&=!bare.contains(key);
        for(const auto& object:bare.at("Characters"))for(const char* key:{"Glow","Group","LightInner","RingInner","Follow"})clean&=!object.contains(key);
        require(clean,"Clearing every club-room block and field returns the encoding to one without any of their keys");
    }

    {
        const auto base=json::parse(text);
        auto rejected=[&](auto change,const char* fragment){json d=base;change(d);return mentions(failure([&]{SceneData::decode(d.dump());}),fragment);};
        require(rejected([](json& d){d["Lighting"]["Mode"]="night";},"Lighting mode"),"An unknown lighting mode is rejected");
        require(rejected([](json& d){d["Lighting"]["KeyDirection"]={0,0,0};},"key direction"),"A zero key direction is rejected");
        require(rejected([](json& d){d["Lighting"]["KeyDirection"]={1,2};},"three numbers"),"A key direction with two numbers is rejected");
        require(rejected([](json& d){d["Lighting"]["Clear"]={1.5,0,0};},"clear colour"),"A clear colour component above one is rejected");
        require(rejected([](json& d){d["Lighting"]["FogMix"]=-.1;},"fog mix"),"A negative fog mix is rejected");
        require(rejected([](json& d){d["Lighting"]["KeyIntensity"]="bright";},"must be a number"),"Text where a number belongs is rejected with the field named");
        require(rejected([](json& d){d["Lighting"]["Bogus"]=1;},"unknown field 'Bogus'"),"An unknown lighting field is rejected");
        require(rejected([](json& d){d["Lighting"]["Modulators"]["Darkness"]["group:missing"]=-.5;},"no object belongs to"),"A modulator that targets a group no object has is rejected");
        require(rejected([](json& d){d["Lighting"]["Modulators"]["Darkness"]["Sky"]=-.5;},"unknown target"),"A modulator with an unknown target is rejected");
        require(rejected([](json& d){d["Lighting"]["Modulators"]["Darkness"]["Spots"]=9;},"coefficients"),"A modulator coefficient out of range is rejected");
        require(rejected([](json& d){d["Moods"][1]["Id"]="default";},"Duplicate mood"),"A duplicate mood name is rejected");
        require(rejected([](json& d){d["ActiveMood"]="missing";},"active mood"),"An active mood that does not exist is rejected");
        require(rejected([](json& d){d["Moods"][0]["Clear"]={.1,.1};},"three numbers"),"A mood colour with two numbers is rejected");
        require(rejected([](json& d){d["Moods"][0]["Extra"]=1;},"unknown field"),"An unknown mood field is rejected");
        require(rejected([](json& d){d["View"]["Near"]=500;},"near and far"),"A near plane beyond the far plane is rejected");
        require(rejected([](json& d){d["View"]["FovDegrees"]=5;},"field of view"),"A field of view of five degrees is rejected");
        require(rejected([](json& d){d["View"]["IdleOrbit"]["PeriodSeconds"]=0;},"idle orbit"),"A zero idle orbit period is rejected");
        require(rejected([](json& d){d["View"]["Follow"]["Zoom"]=1;},"unknown field 'Zoom'"),"An unknown view follow field is rejected");
        require(rejected([](json& d){d["Zones"][1]["Id"]="dancefloor";},"Duplicate zone"),"A duplicate zone name is rejected");
        require(rejected([](json& d){d["Zones"][0]["Kind"]="vip";},"unknown kind"),"An unknown zone kind is rejected");
        require(rejected([](json& d){d["Zones"][0].erase("Radius");},"give a radius"),"A circular zone without a radius is rejected");
        require(rejected([](json& d){d["Zones"][2]["Radius"]=5;},"no radius"),"A box zone with a radius is rejected");
        require(rejected([](json& d){d["Zones"][1]["Height"]=1;},"only a raised floor"),"A height on a zone that is not a raised floor is rejected");
        require(rejected([](json& d){d["Zones"][0]["Shape"]="triangle";},"circle or box"),"An unknown zone shape is rejected");
        require(rejected([](json& d){d["Crowd"]["Lod"]["ExitPx"]={78};},"matching"),"Crowd LOD lists of different lengths are rejected");
        require(rejected([](json& d){d["Crowd"]["Lod"]["ExitPx"]={10,19};},"exit not below enter"),"A crowd LOD exit threshold below its enter threshold is rejected");
        require(rejected([](json& d){d["Crowd"]["Variants"]=9;},"variants"),"More than eight crowd variants are rejected");
        require(rejected([](json& d){d["Crowd"]["Variants"]=2.5;},"whole number"),"A fractional crowd variant count is rejected");
        require(rejected([](json& d){d["Crowd"]["CullSphere"]["Radius"]=0;},"cull sphere"),"A zero crowd cull radius is rejected");
        require(rejected([](json& d){d["Characters"][3]["Follow"]["Bogus"]=1;},"unknown field 'Bogus'"),"An unknown follow-spot field is rejected");
        require(rejected([](json& d){d["Characters"][3]["Follow"]["Lag"]="slow";},"must be a number"),"Text where a follow-spot number belongs is rejected");
        require(rejected([](json& d){d["Characters"][3]["Follow"]["Housing"]["Color"]="blue-ish";},""),"A follow-spot housing colour that is not a colour is rejected");
        require(rejected([](json& d){d["Characters"][0]["Glow"]="on";},"must be a number"),"Text where glow belongs is rejected");
        require(rejected([](json& d){d["Characters"][2]["RingInner"]=1;},"ring inner"),"A ring ratio of one is rejected at decode");
        require(rejected([](json& d){d["Cast"]["Extra"]=1;},"unknown field 'Extra'"),"An unknown cast field is rejected");
    }

    {
        auto cast=[&](auto change){auto s=reference;change(s.Cast.Models[1]);return failure([&]{s.validate();});};
        require(failure([&]{reference.validate();}).empty(),"The reference cast validates: a model with no atlas, fur or sparkle beside one with all three");
        require(cast([](CastModel&){}).empty(),"A model that declares fur as optional validates");
        require(mentions(cast([](CastModel& m){m.Effects.clear();}),"effect:fur"),"effect:fur named in Wants without the effect declared is a card error");
        require(mentions(cast([](CastModel& m){m.Effects.clear();m.Wants.clear();m.Requires.push_back("effect:fur");}),"Requires item 'effect:fur'"),"effect:fur named in Requires without the effect declared is a card error");
        require(mentions(cast([](CastModel& m){m.Maps["v1"].erase("sparkle");}),"sparkle map declared for every variant"),"effect:fur is a card error when a variant lacks one of its maps");
        require(cast([](CastModel& m){m.Wants.clear();m.Maps["v1"].erase("sparkle");}).empty(),"A declared fur effect that no list names creates no item, so a missing map is not an error");
        require(cast([](CastModel& m){m.Wants.clear();m.Effects.clear();m.Maps["v0"].erase("furflow");m.Maps["v1"].erase("furflow");m.Maps["v0"].erase("sparkle");m.Maps["v1"].erase("sparkle");}).empty(),"A model that declares no fur and no sparkle passes");
        require(mentions(cast([](CastModel& m){m.Uv.Layout="glb";m.Uv.Asset.clear();}),"uv:atlas"),"uv:atlas named for a glb layout is a card error");
        require(mentions(cast([](CastModel& m){m.Requires.push_back("map:furflow");m.Wants.clear();m.Effects.clear();m.Maps["v1"].erase("furflow");}),"needs that map declared for every variant"),"A required map missing from one variant is a card error");
        require(mentions(cast([](CastModel& m){m.Requires.push_back("effect:fur");}),"more than once"),"An item named in both Requires and Wants is a card error");
        require(mentions(cast([](CastModel& m){m.Requires.push_back("model");}),"more than once"),"An item named twice in Requires is a card error");
        require(mentions(cast([](CastModel& m){m.Requires.push_back("shiny");}),"not a known item"),"An unknown Requires item is a card error");
        require(mentions(cast([](CastModel& m){m.Wants.push_back("map:depth");}),"unknown map slot"),"An unknown map slot in Wants is a card error");
        require(mentions(cast([](CastModel& m){m.Rig.reset();}),"rig with joints"),"skeleton named without a rig is a card error");
        require(cast([](CastModel& m){m.Requires.erase(std::find(m.Requires.begin(),m.Requires.end(),"skeleton"));m.Rig.reset();}).empty(),"A model with no skeleton declared and none required passes");
        require(mentions(cast([](CastModel& m){m.Uv.Asset="blob-xyz";}),"UV blob id"),"An atlas layout needs a well-formed UV blob id");
        require(mentions(cast([](CastModel& m){m.Uv.Asset="texture-"+std::string(64,'c');}),"UV blob id"),"A texture id is not accepted as a UV blob");
        require(mentions(cast([](CastModel& m){m.Uv.Asset="blob-"+std::string(64,'C');}),"UV blob id"),"A UV blob id must use lower-case hex");
        require(mentions(cast([](CastModel& m){m.Model="model-abc";}),"model asset id"),"A malformed model id is rejected");
        require(mentions(cast([](CastModel& m){m.Model="texture-"+std::string(64,'a');}),"model asset id"),"A texture id is not accepted as a model");
        require(mentions(cast([](CastModel& m){m.Maps["v0"]["basecolor"]="model-"+std::string(64,'a');}),"texture asset id"),"A model id is not accepted as a texture");
        require(mentions(cast([&](CastModel& m){m.Maps["v0"].erase("sparkle");m.Maps["v0"]["glow"]=tex('a');}),"known slot"),"A map slot outside the vocabulary is rejected");
        require(mentions(cast([](CastModel& m){m.Maps["v2"]=m.Maps["v0"];}),"v0 to v1"),"A map variant beyond the variant count is rejected");
        require(mentions(cast([](CastModel& m){auto copy=m.Maps["v0"];m.Maps.erase("v0");m.Maps["v00"]=copy;}),"map variant"),"A map variant key with a leading zero is rejected");
        require(mentions(cast([](CastModel& m){m.Role="boss";}),"Role"),"An unknown role is rejected");
        require(mentions(cast([](CastModel& m){m.Variants=0;}),"Variants"),"Zero variants is rejected");
        require(mentions(cast([](CastModel& m){m.Variants=9;}),"Variants"),"Nine variants is rejected");
        require(mentions(cast([](CastModel& m){m.Id="lead";}),"Duplicate cast model"),"A duplicate cast model name is rejected");
        require(mentions(cast([](CastModel& m){m.Id="two words";}),"cast model name"),"A cast model name with a space is rejected");
        require(mentions(cast([](CastModel& m){m.Effects["glitter"]=CastEffect{};}),"unknown effect"),"An unknown effect is rejected");
        require(mentions(cast([](CastModel& m){m.Effects["fur"].Maps={"furflow","furflow"};}),"repeated"),"A repeated effect map is rejected");
        require(mentions(cast([](CastModel& m){m.Effects["fur"].Profiles["f"].softness=2;}),"out of range"),"A fur profile value out of range is rejected");
        require(mentions(cast([](CastModel& m){m.Rig->Clips["bad name"]="x";}),"rig names"),"A rig name with a space is rejected");
        require(mentions(cast([](CastModel& m){m.Rig->HeightRule->AcceptRange={2.4,1.4};}),"height rule"),"A reversed rig height range is rejected");
        {
            auto crowded=reference;crowded.Cast.Models.clear();
            for(int i=0;i<33;++i){auto m=castModel("m",1,false,false);m.Id="m"+std::to_string(i);crowded.Cast.Models.push_back(m);}
            require(mentions(failure([&]{crowded.validate();}),"at most 32"),"More than 32 cast models are rejected");
        }
        const auto expected=[&]{std::set<std::string> ids{"model-"+std::string(64,'a'),"blob-"+std::string(64,'c')};for(char c:{'0','1','4','5','6','7','a','b','c'})ids.insert(tex(c));for(int i=0;i<2;++i)ids.insert(tex(char('0'+i)));ids.insert(tex('a'));return ids;}();
        require(cast_asset_ids(reference)==expected&&cast_asset_ids(json::parse(text))==expected&&expected.size()==11,"A card names every cast model, UV blob and map texture as a dependency, from the scene and from its JSON");
        require(cast_missing_assets(reference).size()==11,"The cast assets are reported missing from a library that does not hold them");
        require(cast_asset_ids(SceneData{}).empty()&&cast_asset_ids(json::parse(SceneData{}.encode())).empty(),"A scene with no cast names no cast assets");
    }

    {
        EngineAssets::ScopedRoot library(work/"library");
        const auto modelId=import_model(solid_glb_fixture(work));
        const unsigned char pngBytes[]={0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x72,0xb6,0x0d,0x24,0x00,0x00,0x00,0x14,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xcf,0xf0,0xff,0x3f,0x83,0xc3,0xff,0xff,0x0c,0x30,0x06,0x00,0x5c,0x79,0x0a,0x77,0x46,0x16,0xfa,0x08,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        const auto textureId=import_texture_bytes(std::string(reinterpret_cast<const char*>(pngBytes),sizeof(pngBytes)));
        const auto file=work/"commands.starscene";
        {SceneData s;auto c=solid("cmd-cube","cube");s.Characters={c};save_scene(s,file);}
        auto run=[&](json operations){
            json request={{"schemaVersion",1},{"scenePath",utf8(fs::absolute(file).wstring())},{"expectedSha256",hash_file(file)},{"operations",std::move(operations)}};
            const auto path=work/"request.json";atomic_write(path,request.dump());return apply_commands(path);
        };
        auto refused=[&](json operations){const auto before=hash_file(file);bool failed=false;std::string message;try{run(std::move(operations));}catch(const std::exception& problem){failed=true;message=problem.what();}return std::make_pair(failed&&hash_file(file)==before,message);};
        auto idOf=[&](const json& response,const char* name){for(const auto& c:response.at("characters"))if(c.at("Name")==name)return c.at("Id").get<std::string>();return std::string{};};
        auto made=run(json::array({
            json{{"op","shape"},{"shape","ring"},{"name","Neon ring"},{"x",0},{"y",.11},{"z",0},{"width",4.5},{"height",.02},{"depth",4.5},{"color","#FF1ACC"},{"glow",1},{"group","neon"},{"ringInner",.96}},
            json{{"op","shape"},{"shape","cylinder"},{"name","Slab"},{"width",9.3},{"height",.1},{"depth",9.3},{"group","stage"}},
            json{{"op","shape"},{"shape","light"},{"name","Spot"},{"y",6.8},{"lightInner",.6}}}));
        auto scene=load_scene(file);
        const auto ringId=idOf(made,"Neon ring"),spotId=idOf(made,"Spot");
        auto find=[&](const std::string& id)->Character&{return *std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const Character& c){return c.Id==id;});};
        require(scene.Characters.size()==4&&find(ringId).Shape=="ring"&&find(ringId).Glow==1&&find(ringId).Group=="neon"&&find(ringId).RingInner==.96&&find(ringId).Width==4.5&&find(ringId).Color=="#FF1ACC","The shape command creates a glowing ring with its size, colour, group and ring ratio");
        require(find(spotId).LightInner==.6&&!find(spotId).Follow,"The shape command sets the inner cone of a new light");
        auto followed=run(json::array({json{{"op","follow"},{"id",spotId},{"target","focus"},{"offsetX",-4.5},{"offsetZ",2.5},{"lag",3.2},{"wobble",.9},{"poolRadius",1.6},{"housing",json{{"Width",.6},{"Height",.4},{"Depth",.6},{"Color","#120F1A"}}},{"beam",json::object()}}}));
        scene=load_scene(file);
        require(followed.at("success").get<bool>()&&find(spotId).Follow&&find(spotId).Follow->Lag==3.2&&find(spotId).Follow->Housing&&find(spotId).Follow->Housing->Color=="#120F1A"&&find(spotId).Follow->Beam&&find(spotId).Follow->Beam->Pool==.3,"The follow command sets follow-spot behaviour with housing and default beam");
        run(json::array({json{{"op","follow"},{"id",spotId},{"lag",5},{"beam",nullptr}}}));
        scene=load_scene(file);
        require(find(spotId).Follow->Lag==5&&find(spotId).Follow->Wobble==.9&&!find(spotId).Follow->Beam&&find(spotId).Follow->Housing,"A follow command changes only the settings it names and null removes a beam");
        run(json::array({json{{"op","follow"},{"id",spotId},{"clear",true}}}));
        scene=load_scene(file);
        require(!find(spotId).Follow,"The follow command can remove follow-spot behaviour");
        run(json::array({json{{"op","transform"},{"id","cmd-cube"},{"glow",.5},{"group","neon"}}}));
        scene=load_scene(file);
        require(find("cmd-cube").Glow==.5&&find("cmd-cube").Group=="neon","The transform command sets glow and group");
        require(refused(json::array({json{{"op","transform"},{"id","cmd-cube"},{"glow",3}}})).first,"A transform with an invalid glow changes nothing");
        require(refused(json::array({json{{"op","follow"},{"id","cmd-cube"},{"lag",1}}})).first,"A follow command on a cube changes nothing");
        require(refused(json::array({json{{"op","follow"},{"id",spotId},{"speed",1}}})).first,"A follow command with an unknown setting changes nothing");

        auto blocks=full();
        auto setBlock=[&](const char* block,json value){return json{{"op","set-block"},{"block",block},{"value",std::move(value)}};};
        run(json::array({setBlock("Lighting",json(*blocks.Lighting)),setBlock("Moods",json(blocks.Moods)),setBlock("ActiveMood","boss"),setBlock("View",json(*blocks.View)),setBlock("Zones",json(blocks.Zones)),setBlock("Crowd",json(*blocks.Crowd))}));
        scene=load_scene(file);
        require(scene.Lighting==blocks.Lighting&&scene.Moods==blocks.Moods&&scene.ActiveMood=="boss"&&scene.View==blocks.View&&scene.Zones==blocks.Zones&&scene.Crowd==blocks.Crowd,"The set-block command sets lighting, moods, the active mood, view, zones and crowd");
        require(refused(json::array({setBlock("Lighting",json{{"Mode","night"}})})).first,"A set-block with an invalid lighting block changes nothing");
        require(refused(json::array({setBlock("Lighting",json{{"Mood","authored"}})})).first,"A set-block with an unknown field changes nothing");
        require(refused(json::array({setBlock("Weather",json::object())})).first,"A set-block naming an unknown block changes nothing");
        require(refused(json::array({setBlock("ActiveMood","missing")})).first,"A set-block naming an active mood that does not exist changes nothing");
        require(refused(json::array({json{{"op","set-block"},{"block","View"}}})).first,"A set-block without a value changes nothing");
        require(refused(json::array({json{{"op","set-block"},{"block","View"},{"value",nullptr},{"extra",1}}})).first,"A set-block with an unknown command setting changes nothing");

        CastModel real=castModel("real",1,false,false);real.Model=modelId;real.Maps.clear();real.Maps["v0"]["basecolor"]=textureId;real.Maps["v0"]["normal"]=textureId;real.Maps["v0"]["roughness"]=textureId;real.Maps["v0"]["ao"]=textureId;
        SceneCast realCast;realCast.Models={real};
        run(json::array({setBlock("Cast",json(realCast))}));
        scene=load_scene(file);
        require(scene.Cast==realCast&&cast_missing_assets(scene).empty(),"The set-block command sets a cast whose assets the library holds");
        auto fake=realCast;fake.Models[0].Maps["v0"]["basecolor"]=tex('f');
        const auto outcome=refused(json::array({setBlock("Cast",json(fake))}));
        require(outcome.first&&mentions(outcome.second,"does not hold"),"A cast naming an asset the library does not hold is refused, naming the asset");
        run(json::array({json{{"op","set-block"},{"block","Cast"},{"value",json(fake)},{"verifyAssets",false}}}));
        scene=load_scene(file);
        require(scene.Cast==fake,"A cast can be set without the library check when the check is turned off");
        const auto inspected=inspect_scene(file);
        require(inspected.at("scene").contains("Cast")&&inspected.at("scene").contains("Lighting")&&inspected.at("castAssets").size()==3&&inspected.at("missingCastAssetCount")==1,"Inspecting a scene reports every block and the cast assets it can and cannot find");
        run(json::array({setBlock("Cast",nullptr),setBlock("Zones",nullptr),setBlock("Moods",nullptr),setBlock("Lighting",nullptr),setBlock("View",nullptr),setBlock("Crowd",nullptr),setBlock("ActiveMood",nullptr)}));
        scene=load_scene(file);
        require(!scene.Lighting&&scene.Moods.empty()&&scene.ActiveMood.empty()&&!scene.View&&scene.Zones.empty()&&!scene.Crowd&&scene.Cast.Models.empty(),"A null value clears a block through the set-block command");
        const auto described=describe_commands();
        bool listed=false,follows=false;for(const auto& name:described.at("operations")){listed|=name=="set-block";follows|=name=="follow";}
        require(listed&&follows&&described.at("sceneBlocks").size()==7,"The command description lists the new operations and scene blocks");
    }

    {
        fs::path starter;
        for(auto base=fs::current_path();!base.empty();base=base.parent_path()){
            auto candidate=base/"starters"/"club-room"/"club-room.starscene";
            if(fs::exists(candidate)){starter=candidate;break;}
            if(base==base.parent_path())break;
        }
        if(starter.empty())std::cout<<"NOT RUN: the club starter card is not in this checkout, so its canonical form, 68 cast assets and portable export were not checked"<<std::endl;
        else{
        const auto bytes=read_file(starter);
        const auto card=SceneData::decode(bytes);
        require(card.encode()==bytes,"The club starter card is stored in the engine's own canonical form");
        size_t lights=0,followed=0,rings=0,cylinders=0,glowing=0;
        for(const auto& c:card.Characters){if(c.Shape=="light")++lights;if(c.Follow)++followed;if(c.Shape=="ring")++rings;if(c.Shape=="cylinder")++cylinders;if(c.Glow>0)++glowing;}
        require(card.Characters.size()==33&&lights==3&&followed==3&&rings==2&&cylinders==1&&glowing==17,"The starter card holds the room surfaces, three follow-spot lights, the dance floor slab and its two rings");
        require(card.Lighting&&card.Lighting->Mode=="authored"&&card.Lighting->Modulators.at("Darkness").size()==6&&card.Moods.size()==6&&card.ActiveMood=="default"&&card.View&&card.Zones.size()==3&&card.Crowd&&card.Cast.Models.size()==4,"The starter card carries lighting, six moods, the view, three zones, the crowd and four cast models");
        require(card.Cast.Models[0].Role=="lead"&&card.Cast.Models[0].Variants==1&&card.Cast.Models[1].Variants==4&&card.Cast.Models[1].Wants==std::vector<std::string>{"effect:fur"}&&card.Cast.Models[1].Requires.size()==7,"The starter cast declares one lead and three crowd models with fur requested and atlas UVs required");
        require(failure([&]{card.validate();}).empty()&&cast_asset_ids(card).size()==68,"The starter card validates with asset checks deferred and names 68 assets");
        require(card.Cast.Models[0].Rig&&card.Cast.Models[0].Rig->Clips.size()==8&&card.Cast.Models[1].Rig&&card.Cast.Models[1].Rig->Clips.size()==5&&card.Cast.Models[2].Rig->Clips.size()==5&&card.Cast.Models[3].Rig->Clips.size()==5&&!card.Cast.Models[1].Rig->Clips.contains("sway")&&!card.Cast.Models[1].Rig->Clips.contains("dodge")&&!card.Cast.Models[1].Rig->Clips.contains("fire"),"Each crowd rig declares only the clips its model carries, the lead declares all eight");
        std::vector<fs::path> libraryRoots;std::string rootError;
        try{libraryRoots=EngineAssets::default_roots();}catch(const std::exception& e){rootError=e.what();}
        std::string rootList;for(const auto& r:libraryRoots)rootList+=(rootList.empty()?"":"; ")+utf8(r.wstring());
        const std::string rootMessage="The starter card needs its 68 cast assets in a registered asset library; this machine's registered roots: "+(rootList.empty()?std::string("none"):rootList)+(rootError.empty()?std::string():" (registry problem: "+rootError+")")+". Restore or copy the durable asset store and register it with: Starlight.NativeCommands --asset-root-add <folder>";
        require(!libraryRoots.empty(),rootMessage.c_str());
        {
            EngineAssets::ScopedRoot library(libraryRoots.front());
            for(size_t i=1;i<libraryRoots.size();++i)EngineAssets::add_search_root(libraryRoots[i]);
            {
                size_t missingCount=0;std::string missingList;
                for(const auto& id:cast_asset_ids(card)){
                    bool held=false;try{EngineAssets::verify_on_disk(id);held=true;}catch(const std::exception&){}
                    if(!held){++missingCount;if(missingCount<=5)missingList+=" "+id;}
                }
                const std::string missingMessage="The starter card's cast assets are missing from the registered asset libraries ("+std::to_string(missingCount)+" of 68, first:"+missingList+"). "+rootMessage;
                require(missingCount==0,missingMessage.c_str());
            }
            size_t models=0,textures=0,blobs=0,absent=0;
            for(const auto& id:cast_asset_ids(card)){
                if(valid_blob_id(id)){if(EngineAssets::exists(AssetKind::Blob,id)){EngineAssets::verify_on_disk(id);++blobs;}else++absent;}
                else if(EngineAssets::exists(AssetKind::Model,id)){EngineAssets::verify_on_disk(id);++models;}
                else if(EngineAssets::exists(AssetKind::Texture,id))++textures;
                else ++absent;
            }
            require(models==4&&textures==60&&blobs==4&&absent==0,"Every model, texture and UV blob the starter card names is in the durable asset store");
            require(cast_missing_assets(card).empty(),"The starter card has no missing cast asset in the durable store");
            bool realBlobs=true,atlasesMatch=true;std::string atlasNote;
            for(const auto& model:card.Cast.Models){
                realBlobs&=valid_blob_id(model.Uv.Asset)&&model.Uv.Asset.find("0000000000000000")==std::string::npos;
                size_t vertices=0;
                try{vertices=validate_uv_atlas_for_model(model.Uv.Asset,model.Model);}catch(const std::exception& error){atlasNote=model.Id+": "+error.what();}
                atlasesMatch&=vertices>0;
            }
            if(!atlasNote.empty())throw std::runtime_error("A cast UV atlas does not match its model: "+atlasNote);
            require(realBlobs,"Every cast model of the starter card names a real UV atlas blob, not a placeholder");
            require(atlasesMatch,"The UV atlas of every cast model matches the vertex count of its model");
            const auto bundle=work/"club-bundle";
            const auto exported=export_portable_scene(card,bundle);
            require(exported.at("assetCount")==68&&scene_asset_ids(card)==cast_asset_ids(card),"Portable export of the starter card lists exactly its 68 cast assets (4 models, 60 textures, 4 UV blobs)");
            size_t slassets=0;for(const auto& entry:fs::directory_iterator(bundle/"assets"))if(entry.path().extension()==".slasset")++slassets;
            require(slassets==68,"The exported starter bundle carries all 68 cast asset files");
            bool allBundled=true;for(const auto& id:cast_asset_ids(card))allBundled&=fs::is_regular_file(bundle/"assets"/(id+".slasset"));
            require(allBundled,"The exported bundle holds a file for every cast asset id");
            verify_portable_scene(bundle);
            require(EngineAssets::referenced_ids(json::parse(read_file(bundle/"scene.starscene")))==cast_asset_ids(card),"The exported card names the same 68 assets as the original");
            const auto victim=*cast_asset_ids(card).begin();
            fs::remove(bundle/"assets"/(victim+".slasset"));
            auto manifest=json::parse(read_file(bundle/"manifest.json"));
            json kept=json::array();for(const auto& a:manifest.at("assets"))if(a.at("path").get<std::string>().find(victim)==std::string::npos)kept.push_back(a);
            manifest["assets"]=kept;atomic_write(bundle/"manifest.json",manifest.dump(2));
            bool refused=false;try{verify_portable_scene(bundle);}catch(const std::exception&){refused=true;}
            require(refused,"A bundle missing one cast asset fails verification");
        }
        }
    }
    {
        fs::path demo;
        for(auto base=fs::current_path();!base.empty();base=base.parent_path()){
            auto candidate=base/"starters"/"empty-demo"/"empty-demo.starscene";
            if(fs::exists(candidate)){demo=candidate;break;}
            if(base==base.parent_path())break;
        }
        require(!demo.empty(),"The Empty host demonstration card is found in the engine checkout");
        const auto bytes=read_file(demo);
        const auto card=SceneData::decode(bytes);
        require(card.encode()==bytes,"The Empty demonstration card is stored in the engine's own canonical form");
        require(failure([&]{card.validate();}).empty()&&card.Characters.size()==1&&card.Characters[0].Shape=="cube"&&card.Cast.Models.empty()&&card.Lighting&&card.Lighting->Mode=="authored"&&card.View&&card.View->Follow&&card.View->Follow->DefaultDistance==8&&cast_asset_ids(card).empty(),"The Empty demonstration card holds one box, an authored light, a camera and no cast or library asset");
    }

    {
        const double pi=3.141592653589793;
        bool identical=true,widened=true,narrowed=true,flat=true;
        for(const double cone:{5.,17.1,20.17,23.9,45.,90.,120.,179.}){
            SceneData s;s.Name="Inner cone";Character light;light.Id="spot";light.Shape="light";light.LightCone=cone;light.LightRange=10;light.LightIntensity=5;light.Y=3;s.Characters={light};
            const auto stored=SceneData::decode(s.encode());
            identical&=environment_lights(stored,0,nullptr).at(0)[11]==float(std::cos(cone*pi/450))&&environment_lights(stored,0,nullptr).at(0)[7]==float(std::cos(cone*pi/360));
            s.Characters[0].LightInner=1;
            widened&=environment_lights(s,0,nullptr).at(0)[11]==float(std::cos(cone*pi/360));
            s.Characters[0].LightInner=.6;
            const auto soft=environment_lights(s,0,nullptr).at(0);
            narrowed&=soft[11]>float(std::cos(cone*pi/450))&&std::abs(soft[11]-float(std::cos(cone*pi*.6/360)))<1e-6f;
        }
        require(identical,"A light that stores no inner cone ratio keeps exactly the inner cone it had before the ratio existed");
        require(widened,"An inner cone ratio of one makes the inner cone equal the outer cone");
        require(narrowed,"A smaller stored inner cone ratio gives a narrower inner cone");
        SceneData wide;Character light;light.Id="wide";light.Shape="light";light.LightCone=180;light.LightRange=10;light.LightInner=.5;wide.Characters={light};
        flat&=environment_lights(wide,0,nullptr).at(0)[11]==-1.f&&environment_lights(wide,0,nullptr).at(0)[7]==-2.f;
        require(flat,"An all-round light ignores the inner cone ratio");
    }
}
