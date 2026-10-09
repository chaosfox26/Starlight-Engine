// Saving a scene that names assets missing from the library must say so before anything is written (audit CC-09); the editor can still choose to save anyway.
static void missing_asset_save_checks(const auto& require){
    const auto work=fs::temp_directory_path()/"starlight-missing-asset-save-check";
    fs::remove_all(work);fs::create_directories(work);
    const auto file=work/"scene.starscene";
    Character ghost;ghost.Name="Ghost";ghost.Asset="model-"+std::string(64,'f');
    SceneData scene;scene.Characters={ghost};
    bool refused=false;std::vector<std::string> ids;std::string message;
    try{save_scene(scene,file);}catch(const MissingAssetsError& problem){refused=true;ids=problem.ids;message=problem.what();}
    require(refused&&ids.size()==1&&ids[0]==ghost.Asset,"CC-09: Saving a scene that names an asset missing from the library is refused and names the asset");
    require(message.find("1 asset(s)")!=std::string::npos&&message.find(ghost.Asset)!=std::string::npos,"CC-09: The refusal message counts the missing assets and lists their ids");
    require(!fs::exists(file),"CC-09: A refused save writes nothing");
    const auto hash=save_scene(scene,file,{},true);
    require(fs::exists(file)&&hash==hash_file(file),"CC-09: Saving anyway writes the scene and returns the hash of the written bytes");
    Character plain;plain.Name="Plain";plain.Shape="cube";
    SceneData fine;fine.Characters={plain};
    const auto fineFile=work/"fine.starscene";
    save_scene(fine,fineFile);
    require(fs::exists(fineFile),"CC-09: A scene that names no missing asset still saves normally");
    {
        // Editor audit 10: a CLI batch on a scene that already names a missing model is refused unless the request opts in, as the editor's 'Save anyway' does.
        const auto batch=work/"batch.starscene";save_scene(scene,batch,{},true);
        auto run=[&](json extra){
            json request={{"schemaVersion",1},{"scenePath",utf8(fs::absolute(batch).wstring())},{"expectedSha256",hash_file(batch)},
                {"operations",json::array({json{{"op","rename"},{"id",ghost.Id},{"name","Renamed"}}})}};
            for(auto& [key,value]:extra.items())request[key]=value;
            atomic_write(work/"request.json",request.dump());
            std::string failure;try{apply_commands(work/"request.json");}catch(const std::exception& problem){failure=problem.what();}
            return failure;
        };
        const auto refusedBatch=run(json::object());
        require(refusedBatch.find("not in the asset library")!=std::string::npos&&SceneData::decode(read_file(batch)).Characters[0].Name=="Ghost","Editor audit 10: By default a batch on a scene naming a missing model is refused and writes nothing");
        require(run({{"allowMissingAssets",true}}).empty()&&SceneData::decode(read_file(batch)).Characters[0].Name=="Renamed","Editor audit 10: allowMissingAssets lets the batch save the scene anyway");
    }
    {
        // Editor audit 9: 'add' resolves a relative model path against the working directory unless the request opts into the request file's folder, like replace-model.
        const auto folder=work/"request-folder";fs::create_directories(folder);
        fs::copy_file(fs::absolute("migration/fixtures/triangle.glb"),folder/"local.glb",fs::copy_options::overwrite_existing);
        const auto target=folder/"scene.starscene";SceneData empty;save_scene(empty,target);
        auto run=[&](json extra){
            json request={{"schemaVersion",1},{"scenePath",utf8(fs::absolute(target).wstring())},{"expectedSha256",hash_file(target)},
                {"operations",json::array({json{{"op","add"},{"name","Local"},{"asset","local.glb"},{"x",1},{"z",2}}})}};
            for(auto& [key,value]:extra.items())request[key]=value;
            atomic_write(folder/"request.json",request.dump());
            bool failed=false;try{apply_commands(folder/"request.json");}catch(const std::exception&){failed=true;}
            return failed;
        };
        require(run(json::object())&&load_scene(target).Characters.empty(),"Editor audit 9: By default 'add' does not find a model that sits beside the request file");
        require(!run({{"correctedAdd",true}})&&load_scene(target).Characters.size()==1,"Editor audit 9: correctedAdd resolves the model against the request file's folder");
    }
    fs::remove_all(work);
}
