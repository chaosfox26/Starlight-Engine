// Scene validation checks (audit CC-04, duplicate object ids). Object ids key the caches, the runtime maps and every command, so a scene that names one id twice is ambiguous and must be refused.
static void scene_validation_checks(const auto& require){
    auto refused=[&](SceneData& scene,std::string& why){try{scene.validate();return false;}catch(const std::exception& e){why=e.what();return true;}};
    Character a;a.Id="dup-a";a.Name="first";a.Shape="cube";
    Character b=a;b.Name="second";
    Character c;c.Id="dup-c";c.Name="third";c.Shape="cube";
    SceneData scene;scene.Characters={a,c};
    std::string why;
    require(!refused(scene,why),"CC-04: A scene whose objects all have different ids still validates");
    scene.Characters={a,b,c};
    require(refused(scene,why)&&why.find("Duplicate object id: dup-a")!=std::string::npos,"CC-04: Two objects with the same id make the scene invalid, naming the id");
    bool decodeRefused=false;
    try{auto text=scene.encode();(void)text;}catch(const std::exception&){decodeRefused=true;}
    require(decodeRefused,"CC-04: A scene with a duplicated object id cannot be written out either");
    SceneData good;good.Characters={a,c};
    const auto text=good.encode();
    auto json=nlohmann::json::parse(text);
    json["Characters"].push_back(json["Characters"][0]);
    bool decoded=true;
    try{SceneData::decode(json.dump());}catch(const std::exception& e){decoded=false;why=e.what();}
    require(!decoded&&why.find("Duplicate object id")!=std::string::npos,"CC-04: Decoding a scene file that repeats an object id is refused with a clear error");
    require(SceneData::decode(text).Characters.size()==2,"CC-04: The same scene without the repeated id still decodes");
    // CC-23: a Characters entry that is not an object used to hit a JSON type error from the first j.value() call instead of the intended message.
    for(const char* bad:{"5","\"text\"","null","[1,2]"}){
        auto broken=nlohmann::json::parse(text);broken["Characters"]=nlohmann::json::array({nlohmann::json::parse(bad)});
        std::string reason;try{SceneData::decode(broken.dump());}catch(const std::exception& e){reason=e.what();}
        require(reason=="Expected Character object","CC-23: A Characters entry that is not an object is refused with the intended message");
    }
    // CC-04 limits: object count, route points and id length are bounded so a hostile or generated file cannot ask for unbounded work.
    {
        Character big;big.Name="limit";big.Shape="cube";
        SceneData limits;
        big.Id=std::string(128,'x');limits.Characters={big};
        require(!refused(limits,why),"CC-04: An object id of 128 characters is accepted");
        big.Id=std::string(129,'x');limits.Characters={big};
        require(refused(limits,why)&&why.find("id")!=std::string::npos,"CC-04: An object id longer than 128 characters is refused");
        big.Id="route-limit";big.Route.assign(4096,{0.,0.});limits.Characters={big};
        require(!refused(limits,why),"CC-04: A route of 4096 points is accepted");
        big.Route.assign(4097,{0.,0.});limits.Characters={big};
        require(refused(limits,why)&&why.find("route")!=std::string::npos,"CC-04: A route of more than 4096 points is refused");
        limits.Characters.clear();for(int i=0;i<20001;++i){Character item;item.Id="n"+std::to_string(i);item.Name="n";item.Shape="cube";limits.Characters.push_back(std::move(item));}
        require(refused(limits,why)&&why.find("objects")!=std::string::npos,"CC-04: A scene with more than 20000 objects is refused");
        limits.Characters.pop_back();
        require(!refused(limits,why),"CC-04: A scene with exactly 20000 objects is accepted");
    }
    auto decodeWhy=[&](const nlohmann::json& file){std::string reason;try{SceneData::decode(file.dump());}catch(const std::exception& e){reason=e.what();}return reason;};
    // SCENE-1: the pour and route-point limits apply while the file is decoded, before the oversized lists are turned into objects.
    {
        auto pours=nlohmann::json::parse(text);pours["LiquidSources"]=nlohmann::json::array();for(int i=0;i<33;++i)pours["LiquidSources"].push_back(nlohmann::json::object());
        require(decodeWhy(pours)=="Too many pours","SCENE-1: A file listing more than 32 pours is refused while decoding");
        auto route=nlohmann::json::parse(text);route["Characters"][0]["Route"]=nlohmann::json::array();for(int i=0;i<4097;++i)route["Characters"][0]["Route"].push_back({{"X",0.},{"Z",0.}});
        require(decodeWhy(route)=="A route supports at most 4096 points","SCENE-1: A file with more than 4096 route points on one object is refused while decoding");
        route["Characters"][0]["Route"].erase(route["Characters"][0]["Route"].begin());
        require(decodeWhy(route).empty(),"SCENE-1: A file with exactly 4096 route points still decodes");
    }
    // SCENE-3: whole-number settings reject values that would wrap or truncate.
    {
        for(const char* key:{"GrappleMode","CombatTeam","CombatControlEffect"}){
            for(const char* bad:{"4294967297","1.9","\"1\""}){
                auto file=nlohmann::json::parse(text);file["Characters"][0][key]=nlohmann::json::parse(bad);
                require(decodeWhy(file)=="Expected a 32-bit integer",(std::string("SCENE-3: ")+key+" = "+bad+" is refused instead of wrapping or truncating").c_str());
            }
        }
        auto file=nlohmann::json::parse(text);file["Characters"][0]["GrappleMode"]=2;file["Characters"][0]["CombatTeam"]=7;file["Characters"][0]["CombatControlEffect"]=3;
        const auto kept=SceneData::decode(file.dump()).Characters[0];
        require(kept.GrappleMode==2&&kept.CombatTeam==7&&kept.CombatControlEffect==3,"SCENE-3: Valid whole numbers still load");
    }
}
