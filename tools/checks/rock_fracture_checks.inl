template<class Require> void rock_fracture_checks(Require require,const fs::path& folder){
    auto volume=[](const Mesh& m){double v=0;for(size_t i=0;i<m.indices.size();i+=3){auto a=m.vertices[m.indices[i]].position,b=m.vertices[m.indices[i+1]].position,c=m.vertices[m.indices[i+2]].position;v+=a.dot(b.cross(c))/6.;}return std::abs(v);};
    json diagnostics=json::array();
    for(const char* shape:{"rock","boulder","pebbles","chaos-rock"}){
        Character stone;stone.Shape=shape;stone.Color="#555D67";auto source=shape_geometry(stone).front();
        std::string failure;auto start=std::chrono::steady_clock::now();auto pieces=fracture_rock(source.mesh,source.color,false,8241,&failure);double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        atomic_write(folder/"rock-fracture-progress.json",json{{"shape",shape},{"pieces",pieces.size()},{"failure",failure},{"fractureMs",ms}}.dump(2));
        require(pieces.size()>=(stone.Shape=="pebbles"?72:32)&&pieces.size()<=(stone.Shape=="pebbles"?144:64),"Geological destruction creates closed source-shaped chunks, including separate pebbles");
        auto original=source.mesh;require(orient_fracture_mesh(original),"Geological source is consistently closed before fracture");
        double sum=0,minVolume=INFINITY,maxVolume=0;size_t interiors=0,exteriors=0;bool finite=true,closed=true,capNormals=true;std::vector<MeshPart> exploded,joined;
        for(const auto& piece:pieces){double size=volume(piece.part->mesh);sum+=size;minVolume=std::min(minVolume,size);maxVolume=std::max(maxVolume,size);auto test=piece.part->mesh;closed&=orient_fracture_mesh(test);auto part=*piece.part,whole=*piece.part;
            for(size_t i=0;i<part.mesh.indices.size();i+=3){auto a=part.mesh.vertices[part.mesh.indices[i]],b=part.mesh.vertices[part.mesh.indices[i+1]],c=part.mesh.vertices[part.mesh.indices[i+2]];if((a.kind>6.35&&a.kind<6.55||a.kind>10.15&&a.kind<10.35)&&a.kind==b.kind&&a.kind==c.kind)capNormals&=(b.position-a.position).cross(c.position-a.position).unit().dot(a.normal)>.99;}
            for(auto& v:part.mesh.vertices){bool cap=v.kind>6.35&&v.kind<6.55||v.kind>10.15&&v.kind<10.35;interiors+=cap;exteriors+=!cap;finite&=std::isfinite(v.position.x+v.position.y+v.position.z)&&std::abs(v.normal.length()-1)<1e-8;v.position=v.position+piece.center+(piece.center-Vec3{0,.4,0})*.8+Vec3{0,.55,0};}
            for(auto& v:whole.mesh.vertices)v.position=v.position+piece.center;
            exploded.push_back(std::move(part));joined.push_back(std::move(whole));
        }
        atomic_write(folder/"rock-fracture-validation.json",json{{"shape",shape},{"closed",closed},{"finiteUnitNormals",finite},{"interiorVertices",interiors},{"exteriorVertices",exteriors},{"pieces",pieces.size()}}.dump(2));
        require(closed,"All returned rock chunks remain closed in their actual local rigid-body coordinates");
        require(finite,"Rock chunks retain finite unit exterior and interior normals");
        require(interiors>0&&exteriors>0,"Rock chunks retain distinct exposed interiors and their original exterior");
        require(capNormals,"New mineral interior caps have correctly oriented hard face normals");
        require(std::abs(sum-volume(original))<1e-6,"Source-shaped geological chunks preserve the original solid volume");
        require(minVolume<maxVolume*.15,"Shattering includes substantially smaller chips alongside the larger chunks");
        diagnostics.push_back({{"shape",shape},{"pieces",pieces.size()},{"sourceVolume",volume(original)},{"fragmentVolume",sum},{"smallestVolume",minVolume},{"largestVolume",maxVolume},{"interiorVertices",interiors},{"exteriorVertices",exteriors},{"fractureMs",ms}});
        if(stone.Shape=="boulder"||stone.Shape=="chaos-rock"){
            SceneData scene;scene.Name="Source-shaped rock fracture";scene.Hour=14;scene.FogDensity=0;scene.GroundColor="#777168";
            auto save=[&](const char* state,const std::vector<MeshPart>& overlay){auto packet=render_scene(scene,nullptr,overlay);atomic_write(folder/(std::string(shape)+"-fracture-"+state+".bin"),std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));};
            save("source",{source});save("joined",joined);save("exploded",exploded);
            auto plain=exploded;for(auto& part:plain)for(auto& v:part.mesh.vertices)if(v.kind>6.35&&v.kind<6.55||v.kind>10.15&&v.kind<10.35)v.kind=stone.Shape=="chaos-rock"?10.f:6.f;save("plain-interior",plain);
        }
    }
    {
        Character block;block.Shape="cube";auto original=shape_geometry(block).front().mesh;require(orient_fracture_mesh(original),"Cleavage probe begins with a closed original solid");
        auto a=fracture_concave_half(original,0,0,true,6.45f,true),b=fracture_concave_half(original,0,0,false,6.45f,true);require(a&&b,"Cleavage probe retains both neighboring solids");
        roughen_rock_cut(original,*a,*b,0,6.45f,411);
        double low=INFINITY,high=-INFINITY;for(const auto& vertex:a->vertices)if(vertex.kind==6.45f){low=std::min(low,vertex.position.x);high=std::max(high,vertex.position.x);}
        require(high-low>.005&&orient_fracture_mesh(*a)&&orient_fracture_mesh(*b),"One cleavage interface has measurable jagged depth with two closed matching neighbors");
        require(std::abs(volume(*a)+volume(*b)-volume(original))<1e-8,"Displaced neighboring cut faces conserve the source volume without invented extra rubble");
    }
    {
        Character rock;rock.Shape="boulder";auto original=shape_geometry(rock).front();auto first=fracture_rock(original.mesh,original.color,false,117),repeated=fracture_rock(original.mesh,original.color,false,117),different=fracture_rock(original.mesh,original.color,false,914);
        require(first.size()>=32&&first.size()<=64&&repeated.size()==first.size()&&different.size()>=32&&different.size()<=64,"Different reproducible fracture seeds retain complete physical breakup");
        bool reproducible=true,changed=false,rough=false;
        for(size_t i=0;i<std::min(first.size(),different.size());++i){reproducible&=(first[i].center-repeated[i].center).length()<1e-12&&first[i].part->mesh.indices==repeated[i].part->mesh.indices;changed|=(first[i].center-different[i].center).length()>.01;
            const auto& mesh=first[i].part->mesh;for(size_t t=0;t+8<mesh.indices.size();t+=3){auto a=mesh.vertices[mesh.indices[t]],b=mesh.vertices[mesh.indices[t+3]],c=mesh.vertices[mesh.indices[t+6]];if(a.kind==6.45f&&b.kind==6.45f&&c.kind==6.45f)rough|=a.normal.dot(b.normal)<.98||a.normal.dot(c.normal)<.98;}}
        require(reproducible&&changed,"New fracture seeds change chunk placement while fixed seeds reproduce the cut for debugging");
        require(rough,"Geological cut faces contain actual irregular geometric facets rather than one smooth plane");
    }
    auto scene=std::make_unique<SceneData>();scene->Game.emplace();scene->Game->SpawnZ=0;Character stone;stone.Shape="boulder";stone.Id="breakable-boulder";stone.GameplayRole="breakable";stone.Size=2;stone.Z=-3;stone.Color="#555D67";scene->Characters={stone};auto authored=scene->encode();
    auto player=std::make_unique<AdventurePlayer>();player->start(*scene);player->fire_button(true);player->fire_button(false);for(int i=0;i<330;++i)player->move(0,0,1./120.);
    auto p=player->presentation();require(player->scene().Characters.empty()&&p.fragments.size()>=32&&p.fragments.size()<=64&&scene->encode()==authored,"Actual boulder gameplay destruction uses a mixture of closed rigid chunks and small chips without changing the authored scene");
    auto initial=p.fragments;for(int i=0;i<240;++i)player->move(0,0,1./120.);p=player->presentation();bool moved=false,rotated=false;for(size_t i=0;i<p.fragments.size();++i){moved|=(p.fragments[i].position-initial[i].position).length()>.01;rotated|=std::abs(p.fragments[i].rotation[0])+std::abs(p.fragments[i].rotation[1])+std::abs(p.fragments[i].rotation[2])>.01;}
    require(p.fragments.size()>=32&&p.fragments.size()<=64&&moved&&rotated,"Source-shaped rock chunks move and tumble in the physical debris world");
    auto save=[&](const char* name){auto presentation=player_presentation(player->presentation());auto packet=render_scene(player->scene(),nullptr,presentation);atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));};save("rock-debris-settling");
    for(int i=0;i<1200;++i)player->move(0,0,1./120.);require(player->presentation().fragments.empty(),"Geological rigid bodies still expire under the bounded debris lifetime");player->start(*scene);require(player->scene().Characters.size()==1&&player->presentation().fragments.empty(),"Restart restores the unbroken authored rock and clears geological bodies");
    player->fire_button(true);player->fire_button(false);for(int i=0;i<330;++i)player->move(0,0,1./120.);auto another=player->presentation();bool newBreak=false;for(size_t i=0;i<std::min(initial.size(),another.fragments.size());++i)newBreak|=initial[i].part->mesh.vertices.size()!=another.fragments[i].part->mesh.vertices.size();
    require(another.fragments.size()>=32&&another.fragments.size()<=64&&newBreak&&scene->encode()==authored,"Repeating a real gameplay break after reset uses a new fracture pattern without editing the source");
    atomic_write(folder/"rock-fracture.starscene",scene->encode());atomic_write(folder/"rock-fracture-diagnostics.json",diagnostics.dump(2));
}
