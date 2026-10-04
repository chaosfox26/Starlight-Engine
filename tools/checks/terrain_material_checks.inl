template<class Require> void terrain_material_checks(Require require,const fs::path& folder){
    SceneData scene;scene.Name="Terrain paint identity";scene.Terrain.emplace();
    for(int z=0;z<TerrainData::N;++z)for(int x=0;x<TerrainData::N;++x)
        scene.Terrain->Paint[z*TerrainData::N+x]=(x+z)%5;
    Character grass;grass.Id=new_id();grass.Shape="grass";grass.Name="Blade identity";scene.Characters.push_back(grass);
    auto terrain=terrain_geometry(*scene.Terrain);auto packet=render_scene(scene);
    size_t offset=0;
    auto take=[&]<class T>(){if(offset+sizeof(T)>packet.size())throw std::runtime_error("Truncated terrain material fixture");T value;std::memcpy(&value,packet.data()+offset,sizeof(T));offset+=sizeof(T);return value;};
    auto skip=[&](size_t count){if(offset+count>packet.size())throw std::runtime_error("Truncated terrain material fixture");offset+=count;};
    require(take.template operator()<uint32_t>()==0x42564c53,"Terrain identity fixture uses current native packet");
    auto draws=take.template operator()<uint32_t>();auto lights=take.template operator()<uint32_t>();skip(lights*12*sizeof(float));
    skip(4*sizeof(float));auto fog=take.template operator()<uint32_t>();skip(fog*5*sizeof(float));skip(4*sizeof(int32_t));
    bool safe=true,decoded=true,leaf=false;size_t vertices=0;
    for(uint32_t draw=0;draw<draws;++draw){
        skip(sizeof(uint64_t)+sizeof(uint32_t));skip(10*sizeof(float));auto routes=take.template operator()<uint32_t>();skip(routes*2*sizeof(float)+16*sizeof(float));
        auto count=take.template operator()<uint32_t>();
        for(uint32_t v=0;v<count;++v){skip(9*sizeof(float));float kind=take.template operator()<float>();skip(sizeof(uint64_t));
            if(draw<terrain.size()){
                safe&=kind>=-32&&kind<-25;
                decoded&=std::abs((kind+32)-terrain[draw].mesh.vertices[v].kind)<.00001;
                ++vertices;
            }else leaf|=kind>1.3f&&kind<1.4f;
        }
        auto indices=take.template operator()<uint32_t>();skip(indices*sizeof(uint32_t));
    }
    require(offset==packet.size()&&vertices>0&&safe,"Continuous terrain paint cannot interpolate through grass blade or wind tags");
    require(decoded&&leaf,"Tagged terrain retains paint values while actual grass retains leaf shading identity");
    RenderSceneCache cache;require(render_scene(scene,nullptr,{},&cache)==packet,"Tagged terrain remains identical through static scene caching");
    auto encoded=scene.encode();require(render_scene(SceneData::decode(encoded))==packet,"Saved terrain paint regenerates its GPU identity without changing scene data");
    atomic_write(folder/"terrain-material-identity.bin",std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));
}
