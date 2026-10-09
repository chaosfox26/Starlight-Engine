inline bool guard_document_extension(const fs::path& file){
    const auto extension=file.extension().wstring();
    return extension==L".starscene"||extension==L".starobject"||extension==L".stargroup";
}
inline std::string guard_key_card_problem(const fs::path& file){
    const auto extension=file.extension().wstring();
    try{
        if(extension==L".starscene")load_scene(file);
        else if(extension==L".starobject")load_object_template(file);
        else load_object_group(file);
    }catch(const ConversionRequired& e){return std::string("conversion required: ")+e.what();
    }catch(const std::exception& e){return std::string("does not load: ")+e.what();}
    return {};
}
inline std::vector<fs::path> guard_documents(const fs::path& root,const std::set<std::wstring>& skippedFolders,const std::wstring& skippedPrefix){
    std::vector<fs::path> found;
    std::error_code error;
    for(fs::recursive_directory_iterator walk(root,fs::directory_options::skip_permission_denied,error),end;!error&&walk!=end;walk.increment(error)){
        const auto name=walk->path().filename().wstring();
        const auto type=walk->symlink_status(error).type();
        if(type==fs::file_type::symlink||type==fs::file_type::junction){walk.disable_recursion_pending();continue;}
        if(walk->is_directory(error)){
            if(skippedFolders.count(name)||(!skippedPrefix.empty()&&name.starts_with(skippedPrefix)))walk.disable_recursion_pending();
            continue;
        }
        if(walk->is_regular_file(error)&&guard_document_extension(walk->path()))found.push_back(walk->path());
    }
    std::sort(found.begin(),found.end());
    return found;
}
inline std::vector<std::string> guard_missing_ids(const json& document){
    std::vector<std::string> missing;
    for(const auto& id:EngineAssets::referenced_ids(document)){
        try{EngineAssets::verify_on_disk(id);}catch(const std::exception&){missing.push_back(id);}
    }
    return missing;
}
inline std::string guard_missing_message(const std::map<std::string,std::set<std::string>>& missing,const fs::path& root,const std::vector<AssetRootEntry>& registered){
    std::string text="Active scenes name "+std::to_string(missing.size())+" engine asset(s) that no library root on this machine holds or that fail verification. Registered roots: ";
    if(registered.empty())text+="none";
    for(size_t i=0;i<registered.size();++i)text+=(i?"; ":"")+utf8(registered[i].Path.wstring())+(fs::is_directory(registered[i].Path)?"":" (folder missing)");
    const auto durable=(root.parent_path()/"Starlight-Assets"/"library").lexically_normal();
    text+=". Expected durable store: "+utf8(durable.wstring())+(fs::is_directory(durable)?" (present; register it with: Starlight.NativeCommands --asset-root-add \""+utf8(durable.wstring())+"\")":" (not found; restore or copy the durable asset store there, then register it with --asset-root-add)")+". Missing:";
    for(const auto& [id,scenes]:missing){
        text+="\n  "+id+" used by";
        for(const auto& scene:scenes)text+=" "+scene;
    }
    return text;
}
inline bool guard_backup_payload_name(const std::wstring& name){
    std::wstring lower;
    for(const auto c:name)lower+=(c>=L'A'&&c<=L'Z')?static_cast<wchar_t>(c+32):c;
    const auto ends=[&](const std::wstring& tail){return lower.size()>=tail.size()&&lower.compare(lower.size()-tail.size(),tail.size(),tail)==0;};
    if(ends(L".bak")||ends(L".lock")||lower.find(L".before-")!=std::wstring::npos)return true;
    for(const auto* kind:{L".starscene.",L".starobject.",L".stargroup."})if(lower.find(kind)!=std::wstring::npos)return true;
    return false;
}
inline std::vector<std::string> guard_backup_payloads(const fs::path& root,const std::set<std::wstring>& skippedFolders,const std::set<std::string>& allowedFiles){
    std::vector<std::string> found;
    std::error_code error;
    for(fs::recursive_directory_iterator walk(root,fs::directory_options::skip_permission_denied,error),end;!error&&walk!=end;walk.increment(error)){
        const auto name=walk->path().filename().wstring();
        const auto type=walk->symlink_status(error).type();
        if(type==fs::file_type::symlink||type==fs::file_type::junction){walk.disable_recursion_pending();continue;}
        if(walk->is_directory(error)){
            if(skippedFolders.count(name))walk.disable_recursion_pending();
            continue;
        }
        if(!walk->is_regular_file(error)||!guard_backup_payload_name(name))continue;
        const auto relative=utf8(walk->path().lexically_relative(root).generic_wstring());
        if(!allowedFiles.count(relative))found.push_back(relative);
    }
    std::sort(found.begin(),found.end());
    return found;
}
inline std::string guard_backup_message(const std::vector<std::string>& found){
    std::string text="The active tree holds "+std::to_string(found.size())+" leftover backup or old-format file(s); archive them outside the tree with a hash manifest instead of leaving them beside live sources:";
    for(const auto& file:found)text+="\n  "+file;
    return text;
}
template<class Require> void active_scene_guard_checks(Require require,const fs::path& folder,const fs::path& root){
    const auto work=folder/"active-scene-guard";fs::remove_all(work);fs::create_directories(work);
    {
        EngineAssets::ScopedRoot control(work/"control-library");
        SceneData scene;Character cube;cube.Shape="cube";scene.Characters={cube};
        auto legacyScene=json::parse(scene.encode());legacyScene["SchemaVersion"]=7;
        atomic_write(work/"legacy.starscene",legacyScene.dump());
        atomic_write(work/"current.starscene",scene.encode());
        save_object_template(cube,work/"current.starobject");
        save_object_group({cube},work/"current.stargroup");
        auto legacyObject=json::parse(read_file(work/"current.starobject"));legacyObject["Version"]=1;
        atomic_write(work/"legacy.starobject",legacyObject.dump());
        auto legacyGroup=json::parse(read_file(work/"current.stargroup"));legacyGroup["Version"]=1;
        atomic_write(work/"legacy.stargroup",legacyGroup.dump());
        for(const auto* name:{"current.starscene","current.starobject","current.stargroup"})
            require(guard_key_card_problem(work/name).empty(),"The active scene guard accepts a key card of every document kind");
        for(const auto* name:{"legacy.starscene","legacy.starobject","legacy.stargroup"})
            require(guard_key_card_problem(work/name).starts_with("conversion required"),"The active scene guard names a pre-key-card document of every kind as needing conversion");
        require(guard_documents(work,{},L"").size()==6,"The active scene guard finds every document kind in a folder");
        const auto backups=work/"backup-control";
        fs::create_directories(backups/"nested");fs::create_directories(backups/"skipped");
        for(const auto* name:{"nested/a.starscene.20260101-000000.bak","nested/b.cpp.before-edit","nested/c.starscene.lock","nested/d.BAK","nested/e.starscene.old","skipped/f.bak","nested/ok.starscene","nested/ok.cpp","allowed.bak"})atomic_write(backups/name,"x");
        const auto leftovers=guard_backup_payloads(backups,{L"skipped"},{"allowed.bak"});
        const std::vector<std::string> expected{"nested/a.starscene.20260101-000000.bak","nested/b.cpp.before-edit","nested/c.starscene.lock","nested/d.BAK","nested/e.starscene.old"};
        require(leftovers==expected,"The backup guard names every leftover backup and old-format file and ignores skipped folders, allowed files and live documents");
        require(guard_backup_message(leftovers).contains("nested/b.cpp.before-edit")&&guard_backup_message({}).contains("0 leftover"),"The backup guard failure message names each leftover file");
        const auto present=EngineAssets::put(AssetKind::Terrain,"active scene guard control payload");
        const std::string absent="terrain-"+std::string(64,'e');
        require(guard_missing_ids(json{{"Terrain",{{"Asset",present}}}}).empty(),"The active scene guard accepts a scene whose assets the library holds");
        require(guard_missing_ids(json{{"Terrain",{{"Asset",absent}}}})==std::vector<std::string>{absent},"The active scene guard names an asset the library does not hold");
        const auto message=guard_missing_message({{absent,{"scene.starscene"}}},work/"engine",{AssetRootEntry{work/"nowhere",false,"x"}});
        require(message.contains(absent)&&message.contains("scene.starscene")&&message.contains("--asset-root-add")&&message.contains("nowhere"),"The active scene guard failure names the missing asset, the scene, the registered roots and how to register the durable store");
    }
    if(root.empty())return;
    require(EngineAssets::marker_problem(root/"library").empty(),"The in-repo starter library carries a valid marker");
    const auto leftovers=guard_backup_payloads(root,{L"out",L"publication",L".git",L"node_modules",L"audit-runs",L"analysis",L"releases",L"build",L"generated",L"third_party"},{"design/build_shareable_roadmap.pre-scaffolding.py.bak","design/EDITOR-REQUIREMENTS.md.20260916-184001.bak"});
    const std::string leftoverMessage=leftovers.empty()?std::string("The active tree holds no leftover backup or old-format file"):guard_backup_message(leftovers);
    require(leftovers.empty(),leftoverMessage.c_str());
    EngineAssets::ScopedRoot starter(root/"library");
    std::vector<AssetRootEntry> registered;
    try{registered=EngineAssets::registered_roots();}catch(const std::exception&){}
    for(const auto& entry:registered)if(fs::is_directory(entry.Path))EngineAssets::add_search_root(entry.Path);
    const auto documents=guard_documents(root,{L"out",L"publication",L".git",L"node_modules",L"library"},L"");
    require(!documents.empty(),"The active scene guard finds the engine's scene, object and group files");
    std::string problems;size_t failing=0;
    for(const auto& file:documents)
        if(auto problem=guard_key_card_problem(file);!problem.empty()){
            ++failing;
            problems+="\n"+utf8(file.lexically_relative(root).wstring())+": "+problem;
        }
    const std::string message="Every active scene, object and group file is a key card"+(failing?"; failing:"+problems:std::string());
    require(failing==0,message.c_str());
    std::map<std::string,std::set<std::string>> missing;
    for(const auto& file:documents){
        if(!guard_key_card_problem(file).empty())continue;
        for(const auto& id:guard_missing_ids(json::parse(read_file(file))))missing[id].insert(utf8(file.lexically_relative(root).wstring()));
    }
    const std::string assetMessage=missing.empty()?std::string("Every asset the active scenes name is in the in-repo library or a registered root"):guard_missing_message(missing,root,registered);
    require(missing.empty(),assetMessage.c_str());
    if(!missing.empty())return;
    size_t exported=0,withModels=0;std::string exportProblems;
    for(const auto& file:documents){
        if(file.extension()!=L".starscene")continue;
        const auto ids=EngineAssets::referenced_ids(json::parse(read_file(file)));
        std::set<std::string> models;for(const auto& id:ids)if(id.starts_with("model-"))models.insert(id);
        if(models.empty()||ids.size()>8)continue;
        ++withModels;
        const auto name=utf8(file.lexically_relative(root).wstring());
        try{
            const auto scene=load_scene(file);
            const auto bundle=folder/"active-scene-guard"/("portable-"+std::to_string(withModels));
            fs::remove_all(bundle);export_portable_scene(scene,bundle);verify_portable_scene(bundle);
            const auto moved=folder/"active-scene-guard"/("relocated-"+std::to_string(withModels))/"copy";
            fs::remove_all(moved.parent_path());fs::create_directories(moved.parent_path());fs::copy(bundle,moved,fs::copy_options::recursive);
            verify_portable_scene(moved);
            for(const auto& id:models)if(!fs::is_regular_file(moved/"assets"/(id+".slasset")))throw std::runtime_error("bundle lacks "+id);
            for(const auto* part:{"scene.starscene","manifest.json"}){
                const auto text=read_file(moved/part);
                for(size_t i=0;i+2<text.size();++i)
                    if(std::isalpha(static_cast<unsigned char>(text[i]))&&text[i+1]==':'&&(text[i+2]=='\\'||text[i+2]=='/'))throw std::runtime_error(std::string(part)+" holds a drive path near byte "+std::to_string(i));
            }
            ++exported;
        }catch(const std::exception& e){exportProblems+="\n"+name+": "+e.what();}
    }
    const std::string exportMessage="Every active scene that names models exports a portable bundle holding them, verifies from a relocated copy and holds no drive path"+(exportProblems.empty()?std::string():"; failing:"+exportProblems);
    require(exportProblems.empty()&&exported==withModels,exportMessage.c_str());
}
template<class Require> void generated_scene_readback_checks(Require require,const fs::path& folder,const fs::file_time_type& runStarted){
    auto documents=guard_documents(folder,{L"active-scene-guard",L"asset-store-fuzz"},L"library-");
    // Only files this run wrote: a leftover from an earlier build or a crashed run is not this run's output.
    std::erase_if(documents,[&](const fs::path& file){std::error_code timeError;const auto written=fs::last_write_time(file,timeError);return !timeError&&written<runStarted;});
    require(!documents.empty(),"Checks generate scene files for the read-back guard");
    std::string problems;size_t failing=0;
    for(const auto& file:documents)
        if(auto problem=guard_key_card_problem(file);!problem.empty()){
            ++failing;
            problems+="\n"+utf8(file.lexically_relative(folder).wstring())+": "+problem;
        }
    const std::string message="Every scene, object and group file the checks generate reads back as a key card"+(failing?"; failing:"+problems:std::string());
    require(failing==0,message.c_str());
}
