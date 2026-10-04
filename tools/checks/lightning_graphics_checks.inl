template<class Require> void lightning_graphics_checks(Require require,const fs::path& folder){
    auto mesh=lightning_channel({{0,0,0},{0,1,0},{.1,2,0}},.035,false,891);
    mesh.validate();std::map<std::pair<int,int>,int> edges;bool valid=true,outward=true;
    for(size_t i=0;i<mesh.indices.size();i+=3){
        auto a=mesh.vertices[mesh.indices[i]],b=mesh.vertices[mesh.indices[i+1]],c=mesh.vertices[mesh.indices[i+2]];
        auto face=(b.position-a.position).cross(c.position-a.position);
        valid&=face.length()>1e-12;outward&=face.dot(a.normal+b.normal+c.normal)>0;
        for(int e=0;e<3;++e){int a=mesh.indices[i+e],b=mesh.indices[i+(e+1)%3];edges[{std::min(a,b),std::max(a,b)}]++;}
    }
    for(const auto& v:mesh.vertices)valid&=std::abs(v.normal.length()-1)<1e-10&&v.motionIdentity!=0;
    require(valid&&outward&&std::all_of(edges.begin(),edges.end(),[](const auto& e){return e.second==2;}),"Lightning channels have closed outward triangles, smooth unit normals and motion identities");
    require(lightning_channel({{1,2,3},{1,2,3}},.035,false).vertices.empty(),"Zero-length lightning contacts do not generate invalid channel geometry");
    Character bolt;bolt.Id="lightning-graphics";bolt.Shape="chaos-lightning";bolt.BranchSpread=1.4;bolt.Y=1;bolt.TargetX=1;bolt.TargetY=-1;bolt.Color="#C42DFF";
    auto authored=shape_geometry(bolt);require(authored.size()==4&&std::all_of(authored.begin(),authored.end(),[](const auto& p){return p.emissive&&p.mesh.vertices.front().kind>9.3f;}),"Authored lightning uses the same closed core and three tapered branch channels as runtime");
    auto owner=std::make_unique<SceneData>();auto& scene=*owner;scene.Name="Branching purple lightning - graphics prototype";scene.Hour=18;scene.FogDensity=.01;scene.Terrain.emplace();std::fill(scene.Terrain->Paint.begin(),scene.Terrain->Paint.end(),4);scene.Characters={bolt};
    Character stone;stone.Id="lightning-ground-rock";stone.Shape="boulder";stone.X=-1.7;stone.Size=1.5;stone.Color="#87929B";scene.Characters.push_back(stone);
    atomic_write(folder/"lightning-graphics.starscene",scene.encode());
    auto save=[&](const char* name,const std::vector<MeshPart>& overlay,bool runtime){auto packet=render_scene(scene,nullptr,overlay,nullptr,runtime);atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));};
    save("lightning-authored",{},false);EnvironmentEffects effects;effects.step(scene,.03,.06);save("lightning-live",effects.geometry(scene),true);effects.step(scene,.03,.28);save("lightning-next",effects.geometry(scene),true);
    auto arcs=trace_lightning(bolt,scene,.06);require(arcs.front().contact&&arcs.front().path.size()>4,"Rebuilt branching channels retain physical ground contact");
    auto glow=environment_lights(scene,.06,nullptr);Vec3 total{};for(const auto& light:glow)total=total+Vec3{light[8],light[9],light[10]};auto expected=rgb("#C42DFF")*bolt.LightIntensity;
    require(glow.size()==3&&(total-expected).length()<1e-5&&glow[0][1]>glow[2][1]&&glow[1][1]>glow[0][1],"Lightning illuminates its traced channel at several heights while conserving authored light intensity");
    auto crowdedOwner=std::make_unique<SceneData>();auto& crowded=*crowdedOwner;crowded.Characters={bolt};for(int i=0;i<31;++i){Character light;light.Id="reserved-light-"+std::to_string(i);light.Shape="light";light.X=20+i;crowded.Characters.push_back(light);}
    auto crowdedLights=environment_lights(crowded,.06,nullptr);require(crowdedLights.size()==32&&crowdedLights.back()[0]>49,"Channel light samples never evict authored sources or exceed the native light budget");
    auto caveOwner=std::make_unique<SceneData>();auto& cave=*caveOwner;cave.Terrain.emplace();cave.Terrain->Volumes.push_back({0,-5,0,4,true});bolt.BranchSpread=0;bolt.Y=-8;bolt.TargetX=8;bolt.TargetY=3;bolt.TargetZ=0;cave.Characters={bolt};
    auto wall=trace_lightning(bolt,cave,0).front();require(wall.contact&&wall.target.x>3.99&&wall.target.x<4.01&&wall.normal.x<-.99,"Lightning inside a cave reaches its carved wall instead of colliding with the old flat ground");
    bolt.TargetX=0;bolt.TargetY=9;cave.Characters={bolt};auto ceiling=trace_lightning(bolt,cave,0).front();require(ceiling.contact&&ceiling.target.y>-1.01&&ceiling.target.y<-.99&&ceiling.normal.y<-.99,"Lightning inside a cave contacts the carved ceiling with its physical surface normal");
}
