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
    // CORE-B01: a scene file names its objects in one array; a count over the limit is refused before any object is converted.
    auto bigJson=nlohmann::json{{"SchemaVersion",16},{"Characters",nlohmann::json::array()}};
    for(size_t i=0;i<SceneData::MaxCharacters+1;++i)bigJson["Characters"].push_back(nullptr);
    std::string countWhy;try{(void)SceneData::decode(bigJson.dump());}catch(const std::exception& e){countWhy=e.what();}
    require(countWhy=="A scene supports at most 20000 objects","CORE-B01: A scene file with more than 20000 objects is refused by count, before any object is converted");
    SceneData full;full.Characters.reserve(SceneData::MaxCharacters);
    for(size_t i=0;i<SceneData::MaxCharacters;++i){Character c;c.Id="o"+std::to_string(i);c.Name=c.Id;c.Shape="cube";full.Characters.push_back(c);}
    require(SceneData::decode(full.encode()).Characters.size()==SceneData::MaxCharacters,"CORE-B01: A scene with exactly 20000 objects still encodes and loads");
}
