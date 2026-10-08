// Asset, file-writing and platform robustness checks from the 2026-10-08 audit (06 assets/platform, 10 security).
#include "../../deep/engine/event_log.h"
static void asset_platform_checks(const auto& require,const fs::path& folder){
    const fs::path dir=folder/"asset-platform-checks";std::error_code cleared;fs::remove_all(dir,cleared);fs::create_directories(dir);
    {
        // A-06-3: every save with backup used to leave one more "<file>.<id>.bak" forever; now only the newest few remain and only that exact name is ever removed.
        const auto target=dir/"scene.starscene";
        const std::vector<std::string> bystanders{"scene.starscene.bak","scene.starscene.20260101.bak","other.starscene."+std::string(32,'a')+".bak","scene.starscene."+std::string(32,'A')+".bak","scene.starscene."+std::string(32,'a')+".bak.keep"};
        for(const auto& name:bystanders)atomic_write(dir/name,"not ours");
        atomic_write(target,"version 0");
        for(int i=1;i<=8;++i){Sleep(20);atomic_write(target,"version "+std::to_string(i),true);}
        std::set<std::string> kept;size_t ours=0;const std::string head="scene.starscene.";
        for(const auto& entry:fs::directory_iterator(dir)){
            const auto name=entry.path().filename().string();
            if(name.size()==head.size()+32+4&&name.starts_with(head)&&name.ends_with(".bak")&&std::all_of(name.begin()+head.size(),name.begin()+head.size()+32,[](unsigned char c){return std::isdigit(c)||(c>='a'&&c<='f');})){++ours;kept.insert(read_file(entry.path()));}
        }
        require(ours==5&&kept==std::set<std::string>{"version 3","version 4","version 5","version 6","version 7"},"A-06-3: Repeated saves keep only the five newest backups of the file");
        bool bystandersKept=true;for(const auto& name:bystanders)bystandersKept=bystandersKept&&fs::exists(dir/name)&&read_file(dir/name)=="not ours";
        require(bystandersKept&&read_file(target)=="version 8","A-06-3: Pruning never touches a file that is not this program's own backup name, and the saved file is intact");
    }
    {
        // A-06-10: a failure to take the write lock used to be reported as "File is busy" whatever the cause.
        const auto contested=dir/"contested.txt";
        HANDLE holder=CreateFileW((contested.wstring()+L".lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        bool busy=false;try{atomic_write(contested,"x");}catch(const std::exception& e){busy=std::string(e.what())=="File is busy";}
        require(holder!=INVALID_HANDLE_VALUE&&busy,"A-06-10: A file held by another writer is still reported as busy");
        std::thread release([holder]{Sleep(150);CloseHandle(holder);});
        bool waited=true;try{atomic_write(contested,"second writer");}catch(const std::exception&){waited=false;}
        release.join();
        require(waited&&read_file(contested)=="second writer","A-06-10: A writer that finds the lock held for a moment waits and then saves instead of failing");
        fs::create_directories(dir/"blocked.txt.lock");
        std::string why;try{atomic_write(dir/"blocked.txt","x");}catch(const std::exception& e){why=e.what();}
        require(why.find("Windows error")!=std::string::npos&&why.find("busy")==std::string::npos,"A-06-10: A lock that cannot be created for another reason reports the Windows error instead of 'busy'");
    }
    {
        // A-06-1: put() used to count a copy found only in a bundle or search folder as stored, so a saved scene could depend on a folder that later disappears.
        const std::string payload="asset platform check: terrain payload that lives in a search folder";std::string id;
        {EngineAssets::ScopedRoot source(dir/"put-source");id=EngineAssets::put(AssetKind::Terrain,payload);}
        EngineAssets::ScopedRoot home(dir/"put-home");EngineAssets::add_search_root(dir/"put-source");
        require(!fs::exists(EngineAssets::file_for(id)),"A-06-1: The check starts with the asset only in the search folder");
        require(EngineAssets::put(AssetKind::Terrain,payload)==id&&fs::is_regular_file(EngineAssets::file_for(id)),"A-06-1: Storing an asset that exists only in a search folder copies it into the library");
        fs::remove_all(dir/"put-source");EngineAssets::drop_memory_cache();
        bool survives=true;try{EngineAssets::verify_on_disk(id);}catch(const std::exception&){survives=false;}
        require(survives,"A-06-1: The stored asset is still readable after the search folder is gone");
    }
    {
        // A-06-4: a file that is a symlink (or any file with a reparse attribute) was dropped from the asset browser without a trace.
        const auto scan=dir/"scan";fs::create_directories(scan/"real");
        atomic_write(scan/"real"/"model.glb","x");
        std::error_code linked;fs::create_symlink(scan/"real"/"model.glb",scan/"linked.glb",linked);
        if(!linked){
            const auto found=scan_asset_library(scan);bool seen=false;
            for(const auto& entry:found.entries)seen=seen||entry.path.filename()==L"linked.glb";
            require(seen&&found.entries.size()==2,"A-06-4: A model that is a symlink appears in the asset browser");
        } else require(true,"A-06-4: Symlinks cannot be created in this session, so the symlink listing was not exercised");
    }
    {
        // A-06-5: an exported folder that lost its manifest used to run with no check at all.
        SceneData scene;Character cube;cube.Id="manifest-check";cube.Name="manifest-check";cube.Shape="cube";scene.Characters={cube};
        const auto game=dir/"game";fs::create_directories(game);
        export_portable_scene(scene,game/"content");atomic_write(game/"game-manifest.json","{}");
        bool intact=true;try{verify_exported_content(game/"content");}catch(const std::exception&){intact=false;}
        require(intact,"A-06-5: An intact export passes the player's content check");
        fs::remove(game/"content"/"manifest.json");
        std::string why;try{verify_exported_content(game/"content");}catch(const std::exception& e){why=e.what();}
        require(why.find("manifest.json is missing")!=std::string::npos,"A-06-5: An exported game whose manifest was deleted is refused");
        fs::create_directories(dir/"bundle-like"/"assets");atomic_write(dir/"bundle-like"/"scene.starscene","{}");
        why.clear();try{verify_exported_content(dir/"bundle-like");}catch(const std::exception& e){why=e.what();}
        require(why.find("manifest.json is missing")!=std::string::npos,"A-06-5: A scene folder with an assets folder but no manifest is refused");
        fs::create_directories(dir/"loose");atomic_write(dir/"loose"/"scene.starscene","{}");
        bool loose=true;try{verify_exported_content(dir/"loose");}catch(const std::exception&){loose=false;}
        require(loose,"A-06-5: A scene that is not part of an export still opens without a manifest");
    }
    {
        // A-06-12: a JPEG ending in a run of 0xFF bytes read one byte past its end.
        const std::string ends{"\xff\xd8\xff\xe0\x00\x02\xff\xff\xff\xff",10};
        std::string why;try{(void)describe_texture(ends);}catch(const std::exception& e){why=e.what();}
        require(why.find("truncated")!=std::string::npos,"A-06-12: A JPEG that ends in 0xFF fill bytes is reported as truncated");
    }
}
