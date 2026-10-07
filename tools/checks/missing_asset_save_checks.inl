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
    fs::remove_all(work);
}
