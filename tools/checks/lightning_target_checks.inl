// Removing an object that a lightning emitter targets must clear that target (audit ED-13), as it already does for a joint target,
// so the emitter falls back to its authored fixed target instead of going dark with a dangling id in the saved scene.
static void lightning_target_checks(const auto& require){
    const auto work=fs::temp_directory_path()/"starlight-lightning-target-check";
    fs::remove_all(work);fs::create_directories(work);
    const auto file=work/"scene.starscene";
    Character emitter;emitter.Shape="chaos-lightning";emitter.Name="Emitter";
    Character target;target.Shape="cube";target.Name="Target";
    Character other;other.Shape="cube";other.Name="Other";
    emitter.LightningTargetId=target.Id;
    {SceneData scene;scene.Characters={emitter,target,other};save_scene(scene,file);}
    json request={{"schemaVersion",1},{"scenePath",utf8(fs::absolute(file).wstring())},{"expectedSha256",hash_file(file)},
        {"operations",json::array({json{{"op","remove"},{"id",target.Id}}})}};
    atomic_write(work/"request.json",request.dump());
    apply_commands(work/"request.json");
    const auto after=SceneData::decode(read_file(file));
    const Character* saved=nullptr;
    for(const auto& c:after.Characters)if(c.Id==emitter.Id)saved=&c;
    require(after.Characters.size()==2&&saved,"ED-13: Removing the lightning target leaves the emitter and the other object in the scene");
    require(saved&&saved->LightningTargetId.empty(),"ED-13: Removing the lightning target clears the emitter's target id so its fixed target applies");
    fs::remove_all(work);
}
