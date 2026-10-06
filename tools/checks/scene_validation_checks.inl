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
}
