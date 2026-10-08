// Asset, file-writing and platform robustness checks from the 2026-10-08 audit (06 assets/platform, 10 security).
#include "../../deep/engine/event_log.h"
#include "../../deep/engine/model/load_error.h"
static void asset_platform_checks(const auto& require,const fs::path& folder){
    const fs::path dir=folder/"asset-platform-checks";std::error_code cleared;fs::remove_all(dir,cleared);fs::create_directories(dir);
    {
        // A-06-3: every save with backup used to leave one more "<file>.<id>.bak" forever; now only the newest few remain and only that exact name is ever removed.
        const auto target=dir/"scene.starscene";
        const std::vector<std::string> bystanders{"scene.starscene.bak","scene.starscene.20260101.bak","other.starscene."+std::string(32,'a')+".bak","scene.starscene."+std::string(32,'A')+".bak","scene.starscene."+std::string(32,'a')+".bak.keep"};
        for(const auto& name:bystanders)atomic_write(dir/name,"not ours");
        atomic_write(target,"version 0");
        for(int i=1;i<=2;++i){Sleep(20);atomic_write(target,"early "+std::to_string(i),true);}
        const auto countBackups=[&]{size_t n=0;for(const auto& entry:fs::directory_iterator(dir)){const auto name=entry.path().filename().string();if(name.size()==std::string("scene.starscene.").size()+32+4&&name.starts_with("scene.starscene.")&&name.ends_with(".bak")&&name.find("aaaa")==std::string::npos)++n;}return n;};
        const auto beforePrune=countBackups();
        require(beforePrune>=2,"A-06-3: Backups are kept by default, nothing is deleted unless the owner asks");
        SetEnvironmentVariableA("STARLIGHT_BACKUPS_KEPT","5");
        for(int i=1;i<=8;++i){Sleep(20);atomic_write(target,"version "+std::to_string(i),true);}
        SetEnvironmentVariableA("STARLIGHT_BACKUPS_KEPT",nullptr);
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
        fs::create_directories(dir/"bundle-like"/"assets");atomic_write(dir/"bundle-like"/"assets"/"x.slasset","x");atomic_write(dir/"bundle-like"/"scene.starscene","{}");
        why.clear();try{verify_exported_content(dir/"bundle-like");}catch(const std::exception& e){why=e.what();}
        require(why.find("manifest.json is missing")!=std::string::npos,"A-06-5: A scene folder holding engine assets but no manifest is refused");
        fs::create_directories(dir/"plain-assets"/"assets");atomic_write(dir/"plain-assets"/"scene.starscene","{}");
        bool plain=true;try{verify_exported_content(dir/"plain-assets");}catch(const std::exception&){plain=false;}
        require(plain,"A-06-5: A scene folder with an unrelated assets folder still opens");
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
    {
        // A-10-4 / A-10-5: a too-long log variable must not be used half-read, and the event log must write out what was logged when it is closed.
        _putenv_s("STARLIGHT_EVENT_LOG",std::string(600,'p').c_str());
        bool ignored=false;{starlight_diag::EventLog tooLong;ignored=!tooLong.enabled&&tooLong.prefix[0]==0;}
        require(ignored,"A-10-4: An event-log path longer than the buffer leaves the log off");
        const auto prefix=dir/"events";_putenv_s("STARLIGHT_EVENT_LOG",prefix.string().c_str());
        {
            starlight_diag::EventLog log;
            require(log.enabled,"A-10-5: The event log switches on for a valid prefix");
            log.add("first",1,2,3);log.add("second",4,5,6);
        }
        _putenv_s("STARLIGHT_EVENT_LOG","");
        size_t lines=0;for(const auto& entry:fs::directory_iterator(dir))if(entry.path().filename().string().starts_with("events.")){std::ifstream in(entry.path());std::string line;while(std::getline(in,line))++lines;}
        require(lines==2,"A-10-5: Both events are written out when the log is closed");
    }
    {
        // A-10-1: one DLL load kept the unrestricted search order after the vendor-DLL hardening; no load in the engine sources may use it.
        const fs::path engine="deep/engine";std::string offender;
        require(fs::is_directory(engine),"A-10-1: The check runs from the repository root so it can read the engine sources");
        if(fs::is_directory(engine))
            for(const auto& entry:fs::recursive_directory_iterator(engine)){
                const auto extension=entry.path().extension().string();
                if(!entry.is_regular_file()||(extension!=".cpp"&&extension!=".h"))continue;
                if(read_file(entry.path()).find("LOAD_WITH_ALTERED_SEARCH_PATH")!=std::string::npos)offender=entry.path().string();
            }
        require(offender.empty(),"A-10-1: No engine DLL load uses the unrestricted LOAD_WITH_ALTERED_SEARCH_PATH search order");
    }
    {
        // CORE-B01 / A-06-9: files and collections are bounded before memory is allocated for them, and storing an asset no longer joins the kind and payload into a second copy.
        const auto budgets=dir/"budgets";fs::create_directories(budgets);
        atomic_write(budgets/"ten.bin","0123456789");
        std::string why;
        require(read_file(budgets/"ten.bin",10)=="0123456789"&&read_file(budgets/"ten.bin")=="0123456789","CORE-B01: A file exactly at the byte limit is read");
        try{(void)read_file(budgets/"ten.bin",9);}catch(const std::exception& e){why=e.what();}
        require(why.find("is larger than")!=std::string::npos,"CORE-B01: A file over the byte limit is refused before it is read");
        model::LoadError modelRead;std::vector<uint8_t> modelBytes;
        modelRead=model::readWholeFile((budgets/"ten.bin").string(),modelBytes,9);
        require(modelRead.code==model::LoadCode::TooLarge&&modelBytes.empty(),"CORE-B01: The model loader refuses a file over its byte limit before allocating for it");
        modelRead=model::readWholeFile((budgets/"ten.bin").string(),modelBytes,10);
        require(modelRead.ok()&&modelBytes.size()==10,"CORE-B01: The model loader still reads a file at its byte limit");
        require(hash_bytes("abc")=="BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD","A-06-9: SHA-256 of a known input is unchanged");
        require(hash_bytes("abc","defgh")==hash_bytes("abcdefgh")&&hash_bytes({},"abcdefgh")==hash_bytes("abcdefgh")&&hash_bytes("abcdefgh","")==hash_bytes("abcdefgh"),"A-06-9: Hashing a head and a body in two steps gives the hash of the joined bytes");
        EngineAssets::ScopedRoot library(budgets/"library");
        std::string payload(5000,'q');for(size_t i=0;i<payload.size();++i)payload[i]=char('a'+i%23);
        const uint32_t kindValue=uint32_t(AssetKind::Blob);
        std::string joined(reinterpret_cast<const char*>(&kindValue),4);joined+=payload;
        auto expected=hash_bytes(joined);std::transform(expected.begin(),expected.end(),expected.begin(),[](unsigned char c){return char(std::tolower(c));});
        const auto id=EngineAssets::put(AssetKind::Blob,payload);
        require(id=="blob-"+expected,"A-06-9: An asset id is still the SHA-256 of the kind and payload joined");
        require(*EngineAssets::get(AssetKind::Blob,id)==payload,"A-06-9: A stored asset reads back unchanged from memory");
        EngineAssets::drop_memory_cache();
        require(*EngineAssets::get(AssetKind::Blob,id)==payload,"A-06-9: A stored asset reads back unchanged from the library file");
        EngineAssets::drop_memory_cache();
        require(EngineAssets::put(AssetKind::Blob,payload)==id&&*EngineAssets::get(AssetKind::Blob,id)==payload,"A-06-9: Storing an asset that is already in the library returns the same id and payload");
        const auto verifiedFile=EngineAssets::verified_file_bytes(id);
        require(verifiedFile.size()==payload.size()+20&&verifiedFile.substr(20)==payload,"A-06-9: The whole-file read of an asset still returns header and payload");
        // A file over the limit of its kind is refused as a damaged asset before it is read; a payload over the limit is refused before anything is written.
        const std::string terrainId="terrain-"+std::string(64,'0');
        {std::ofstream oversize(EngineAssets::file_for(terrainId),std::ios::binary);const std::string chunk(1<<20,'\0');for(int i=0;i<65;++i)oversize.write(chunk.data(),std::streamsize(chunk.size()));}
        why.clear();try{(void)EngineAssets::get(AssetKind::Terrain,terrainId);}catch(const AssetMissing& e){why=e.what();}
        require(why.find("larger than the 64 MiB limit")!=std::string::npos,"CORE-B01: A terrain asset file over 64 MiB is refused before it is read");
        const std::string bigPayload((size_t(64)<<20)-19,'z');   // header (20 bytes) + payload is one byte over 64 MiB
        why.clear();try{(void)EngineAssets::put(AssetKind::Terrain,std::string_view(bigPayload));}catch(const std::exception& e){why=e.what();}
        require(why.find("exceeds the 64 MiB limit")!=std::string::npos,"CORE-B01: A terrain payload over 64 MiB is refused before it is written");
    }
    // Later groups scan the scratch folder for scenes and leftover backups, so these files must not stay behind.
    fs::remove_all(dir,cleared);
}
