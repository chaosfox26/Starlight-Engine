// Adversarial checks for the engine asset store (.slasset files) and portable scene verification: damaged, truncated and forged inputs must be refused
// with a clean error (never a crash, an out-of-range read or a write outside the target folder). Fully deterministic; run alone with
// STARLIGHT_CHECK_GROUPS=asset_store_fuzz_checks.
#include <fstream>
template<class Require> void asset_store_fuzz_checks(Require require,const fs::path& folder){
    const auto work=fs::absolute(folder/"asset-store-fuzz").lexically_normal();std::error_code cleared;fs::remove_all(work,cleared);fs::create_directories(work);
    EngineAssets::ScopedRoot library(work/"library");
    enum class Result{Accepted,AssetMissingError,OtherError,Unknown};
    auto outcome=[](auto&& action){try{action();return Result::Accepted;}catch(const AssetMissing&){return Result::AssetMissingError;}catch(const std::exception&){return Result::OtherError;}catch(...){return Result::Unknown;}};
    auto refused=[&](auto&& action){const auto r=outcome(action);return r==Result::AssetMissingError||r==Result::OtherError;};
    auto put32=[](std::string& bytes,size_t at,uint32_t value){std::memcpy(bytes.data()+at,&value,4);};
    auto put64=[](std::string& bytes,size_t at,uint64_t value){std::memcpy(bytes.data()+at,&value,8);};
    uint64_t seed=0x9E3779B97F4A7C15ull;
    auto next=[&]{seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;return seed;};

    // The container: header = magic, version, kind, payload size.
    const std::string payload="payload bytes for the fuzz asset store";
    const auto id=EngineAssets::put(AssetKind::Blob,payload);
    const auto file=EngineAssets::file_for(id);
    const auto pristine=read_file(file);
    auto place=[&](const std::string& bytes){EngineAssets::drop_memory_cache();atomic_write(file,bytes);};
    auto readsBack=[&]{return outcome([&]{(void)EngineAssets::get(AssetKind::Blob,id);});};
    place(pristine);
    require(readsBack()==Result::Accepted,"Fuzz baseline: the untouched asset file loads");
    {
        size_t wrong=0;
        for(size_t keep=0;keep<pristine.size();++keep){place(pristine.substr(0,keep));if(readsBack()!=Result::AssetMissingError)++wrong;}
        require(wrong==0,"Fuzz: an asset file cut off at any length is reported as damaged, never loaded");
        wrong=0;
        for(size_t byte=0;byte<pristine.size();++byte)for(int bit=0;bit<8;++bit){auto damaged=pristine;damaged[byte]=char(damaged[byte]^(1<<bit));place(damaged);if(readsBack()!=Result::AssetMissingError)++wrong;}
        require(wrong==0,"Fuzz: flipping any single bit of an asset file (header or payload) is reported as damaged");
        wrong=0;
        for(const auto& tail:{std::string("x"),std::string(8,'\0'),pristine}){place(pristine+tail);if(readsBack()!=Result::AssetMissingError)++wrong;}
        require(wrong==0,"Fuzz: extra bytes after the declared payload are refused");
    }
    {
        size_t wrong=0;
        auto forged=[&](size_t at,bool wide,uint64_t value){auto bytes=pristine;if(wide)put64(bytes,at,value);else put32(bytes,at,uint32_t(value));place(bytes);if(readsBack()!=Result::AssetMissingError)++wrong;};
        const uint64_t real=payload.size();
        for(const uint64_t size:{uint64_t(0),real-1,real+1,real+8,uint64_t(1)<<32,uint64_t(1)<<63,~uint64_t(0),~uint64_t(0)-19,~uint64_t(0)-20})forged(12,true,size);
        for(const uint32_t version:{0u,2u,0x80000001u,~0u})forged(4,false,version);
        for(const uint32_t kind:{0u,uint32_t(AssetKind::Model),uint32_t(AssetKind::Texture),uint32_t(AssetKind::Sculpt),6u,99u,~0u})forged(8,false,kind);
        for(const uint32_t magic:{0u,0x53414C54u,~0u})forged(0,false,magic);
        require(wrong==0,"Fuzz: forged magic, version, kind and size fields (including sizes that overflow when added to the header) are refused");
        place(std::string());if(readsBack()!=Result::AssetMissingError)++wrong;
        place(pristine.substr(0,20));if(readsBack()!=Result::AssetMissingError)++wrong;
        require(wrong==0,"Fuzz: an empty file and a header with no payload are refused");
        place(pristine);
    }
    {
        // A file stored under another kind's name never loads as that kind, even with its header kind rewritten to match.
        const auto hex=id.substr(5);size_t wrong=0;
        for(const auto& [prefix,kind]:std::vector<std::pair<std::string,AssetKind>>{{"model-",AssetKind::Model},{"texture-",AssetKind::Texture},{"sculpt-",AssetKind::Sculpt},{"terrain-",AssetKind::Terrain}}){
            const auto other=prefix+hex;const auto path=file.parent_path()/starlight::editor::wide(other+".slasset");
            EngineAssets::drop_memory_cache();atomic_write(path,pristine);
            if(outcome([&]{(void)EngineAssets::get(kind,other);})!=Result::AssetMissingError)++wrong;
            auto rewritten=pristine;put32(rewritten,8,uint32_t(kind));atomic_write(path,rewritten);
            if(outcome([&]{(void)EngineAssets::get(kind,other);})!=Result::AssetMissingError||outcome([&]{EngineAssets::verify_on_disk(other);})!=Result::AssetMissingError)++wrong;
            if(EngineAssets::keep(other))++wrong;
        }
        require(wrong==0,"Fuzz: an asset renamed or re-labelled as another kind fails the identity check");
        place(pristine);
    }

    {
        // A file that cannot be read at all (held open by another program, or too large to load whole) is a damaged asset like any other, not a different kind of failure.
        place(pristine);
        EngineAssets::ScopedRoot sameFolder(work/"library");
        HANDLE holder=CreateFileW(file.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        const bool held=holder!=INVALID_HANDLE_VALUE;
        const auto locked=held?outcome([&]{(void)EngineAssets::get(AssetKind::Blob,id);}):Result::AssetMissingError;
        const auto lockedVerify=held?outcome([&]{EngineAssets::verify_on_disk(id);}):Result::AssetMissingError;
        const auto lockedKeep=held?outcome([&]{(void)EngineAssets::keep(id);}):Result::Accepted;
        if(held)CloseHandle(holder);
        require(held&&locked==Result::AssetMissingError&&lockedVerify==Result::AssetMissingError&&lockedKeep==Result::Accepted,"Fuzz: an asset file that cannot be opened is reported as an unusable asset (AssetMissing), and keep() answers false instead of throwing");
        const auto big=EngineAssets::file_for("blob-"+std::string(64,'b'));
        HANDLE sparse=CreateFileW(big.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        DWORD returned=0;LARGE_INTEGER length;length.QuadPart=(LONGLONG(1)<<30)+1;
        const bool made=sparse!=INVALID_HANDLE_VALUE&&DeviceIoControl(sparse,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&returned,nullptr)&&SetFilePointerEx(sparse,length,nullptr,FILE_BEGIN)&&SetEndOfFile(sparse);
        if(sparse!=INVALID_HANDLE_VALUE)CloseHandle(sparse);
        if(made)require(outcome([&]{(void)EngineAssets::get(AssetKind::Blob,"blob-"+std::string(64,'b'));})==Result::AssetMissingError,"Fuzz: an asset file larger than the 1 GiB load limit is reported as an unusable asset");
        else require(true,"Fuzz: a sparse 1 GiB file cannot be created here, so the oversize case was not exercised");
        std::error_code removed;fs::remove(big,removed);
        place(pristine);
    }

    // Crafted payloads whose identity is genuine (the id is the hash of the bytes), so only the payload parsers stand in the way.
    auto sculptBytes=[&](uint64_t positions,uint64_t indices,uint64_t coordinates,const std::vector<double>& p,const std::vector<int32_t>& i,const std::vector<double>& c){
        std::string out;out.append(reinterpret_cast<const char*>(&positions),8);out.append(reinterpret_cast<const char*>(&indices),8);out.append(reinterpret_cast<const char*>(&coordinates),8);
        out.append(reinterpret_cast<const char*>(p.data()),p.size()*8);out.append(reinterpret_cast<const char*>(i.data()),i.size()*4);out.append(reinterpret_cast<const char*>(c.data()),c.size()*8);return out;};
    auto loadsSculpt=[&](const std::string& bytes){return outcome([&]{(void)load_sculpt(EngineAssets::put(AssetKind::Sculpt,bytes));});};
    Character cube;cube.Shape="cube";
    Character tree;tree.Id="fz-tree";tree.Name="Fuzz sculpt";tree.Sculpt=sculpt_from(object_mesh(cube));tree.Shape.reset();tree.validate();
    const auto& mesh=*tree.Sculpt;
    const std::vector<int32_t> triangles(mesh.Indices.begin(),mesh.Indices.end());
    const double nan=std::nan("");
    require(loadsSculpt(sculptBytes(mesh.Positions.size(),triangles.size(),0,mesh.Positions,triangles,{}))==Result::Accepted,"Fuzz baseline: a hand-built sculpt payload loads");
    {
        size_t wrong=0;
        auto count=[&](const std::string& bytes){if(!refused([&]{(void)load_sculpt(EngineAssets::put(AssetKind::Sculpt,bytes));}))++wrong;};
        const uint64_t huge=uint64_t(1)<<62;
        count(sculptBytes(huge,0,0,{},{},{}));
        count(sculptBytes(0,huge,0,{},{},{}));
        count(sculptBytes(0,0,huge,{},{},{}));
        count(sculptBytes(~uint64_t(0),~uint64_t(0),~uint64_t(0),{},{},{}));
        count(sculptBytes(mesh.Positions.size()+1,triangles.size(),0,mesh.Positions,triangles,{}));
        count(sculptBytes(mesh.Positions.size(),triangles.size()+3,0,mesh.Positions,triangles,{}));
        count(sculptBytes(mesh.Positions.size(),triangles.size(),0,mesh.Positions,triangles,{})+"x");
        count(sculptBytes(mesh.Positions.size()-1,triangles.size(),0,{mesh.Positions.begin(),mesh.Positions.end()-1},triangles,{}));
        count(sculptBytes(mesh.Positions.size(),triangles.size()-1,0,mesh.Positions,{triangles.begin(),triangles.end()-1},{}));
        auto badIndex=triangles;badIndex[0]=int32_t(mesh.Positions.size()/3);count(sculptBytes(mesh.Positions.size(),badIndex.size(),0,mesh.Positions,badIndex,{}));
        badIndex[0]=-1;count(sculptBytes(mesh.Positions.size(),badIndex.size(),0,mesh.Positions,badIndex,{}));
        badIndex[0]=INT32_MIN;count(sculptBytes(mesh.Positions.size(),badIndex.size(),0,mesh.Positions,badIndex,{}));
        auto badPosition=mesh.Positions;badPosition[0]=nan;count(sculptBytes(badPosition.size(),triangles.size(),0,badPosition,triangles,{}));
        badPosition[0]=1e300;count(sculptBytes(badPosition.size(),triangles.size(),0,badPosition,triangles,{}));
        badPosition[0]=INFINITY;count(sculptBytes(badPosition.size(),triangles.size(),0,badPosition,triangles,{}));
        count(sculptBytes(mesh.Positions.size(),triangles.size(),mesh.Positions.size()-3,mesh.Positions,triangles,std::vector<double>(mesh.Positions.size()-3,.5)));
        count(sculptBytes(mesh.Positions.size(),triangles.size(),mesh.Positions.size(),mesh.Positions,triangles,std::vector<double>(mesh.Positions.size(),nan)));
        count(sculptBytes(mesh.Positions.size(),triangles.size(),mesh.Positions.size(),mesh.Positions,triangles,std::vector<double>(mesh.Positions.size(),2.)));
        auto threeShort=mesh.Positions;threeShort.pop_back();count(sculptBytes(threeShort.size(),triangles.size(),0,threeShort,triangles,{}));
        require(wrong==0,"Fuzz: sculpt payloads with absurd counts, short or long bodies, bad indices and non-finite values are refused");
    }
    {
        // Random edits of a genuine sculpt payload: every outcome is either a refusal or a mesh that passes its own validation.
        const auto genuine=*EngineAssets::get(AssetKind::Sculpt,store_sculpt(mesh));
        size_t wrong=0,accepted=0;
        for(int round=0;round<120;++round){
            auto bytes=genuine;
            switch(next()%4){
            case 0:bytes[next()%bytes.size()]=char(next());break;
            case 1:bytes.erase(next()%bytes.size(),1+next()%16);break;
            case 2:bytes.insert(next()%bytes.size(),std::string(1+next()%16,char(next())));break;
            default:bytes.resize(next()%bytes.size());break;
            }
            try{auto loaded=load_sculpt(EngineAssets::put(AssetKind::Sculpt,bytes));loaded.validate();++accepted;}catch(const std::exception&){}catch(...){++wrong;}
        }
        require(wrong==0&&accepted<120,"Fuzz: randomly edited sculpt payloads are refused or yield a valid mesh, never an unexpected failure");
    }
    auto terrainBytes=[&](uint32_t grid,double height,int32_t paint,uint32_t volumes,double radius,size_t writtenVolumes){
        std::string out;out.append(reinterpret_cast<const char*>(&grid),4);
        std::vector<double> heights(size_t(TerrainData::N)*TerrainData::N,0.);heights[7]=height;out.append(reinterpret_cast<const char*>(heights.data()),heights.size()*8);
        std::vector<int32_t> paints(heights.size(),0);paints[9]=paint;out.append(reinterpret_cast<const char*>(paints.data()),paints.size()*4);
        out.append(reinterpret_cast<const char*>(&volumes),4);
        for(size_t v=0;v<writtenVolumes;++v){const double fields[4]={0,0,0,radius};out.append(reinterpret_cast<const char*>(fields),32);const uint32_t subtract=1;out.append(reinterpret_cast<const char*>(&subtract),4);}
        return out;};
    auto loadsTerrain=[&](const std::string& bytes){return outcome([&]{TerrainData terrain;load_terrain_geometry(EngineAssets::put(AssetKind::Terrain,bytes),terrain);});};
    {
        const uint32_t n=uint32_t(TerrainData::N);
        require(loadsTerrain(terrainBytes(n,0,0,1,1,1))==Result::Accepted,"Fuzz baseline: a hand-built terrain payload loads");
        size_t wrong=0;
        auto count=[&](const std::string& bytes){if(!refused([&]{TerrainData terrain;load_terrain_geometry(EngineAssets::put(AssetKind::Terrain,bytes),terrain);}))++wrong;};
        count(terrainBytes(n+1,0,0,0,1,0));
        count(terrainBytes(0,0,0,0,1,0));
        count(terrainBytes(n,0,0,129,1,129));
        count(terrainBytes(n,0,0,~0u,1,0));
        count(terrainBytes(n,0,0,3,1,2));
        count(terrainBytes(n,0,0,1,1,2));
        count(terrainBytes(n,0,0,0,1,0)+"x");
        auto cut=terrainBytes(n,0,0,0,1,0);cut.resize(cut.size()-1);count(cut);
        cut.resize(100);count(cut);
        cut.resize(3);count(cut);
        require(wrong==0,"Fuzz: terrain payloads with a wrong grid size, absurd volume counts, short or long bodies are refused");
        // Loading the grids does not range-check them; opening a scene does (TerrainData::validate), so readiness must agree with what opening would do.
        const auto readyTerrain=[&](const std::string& bytes){return check_asset_ready(EngineAssets::put(AssetKind::Terrain,bytes)).ready;};
        require(readyTerrain(terrainBytes(n,0,0,1,1,1)),"Fuzz: a sound terrain asset is ready");
        require(!readyTerrain(terrainBytes(n,nan,0,0,1,0))&&!readyTerrain(terrainBytes(n,1e9,0,0,1,0))&&!readyTerrain(terrainBytes(n,0,9,0,1,0))&&!readyTerrain(terrainBytes(n,0,-1,0,1,0))&&
                !readyTerrain(terrainBytes(n,0,0,1,nan,1))&&!readyTerrain(terrainBytes(n,0,0,1,100,1)),"Fuzz: a terrain asset with a non-finite or out-of-range height, paint index or cut radius is not reported ready");
        auto scene=SceneData();scene.Terrain.emplace();auto card=json::parse(scene.encode());
        card["Terrain"]["Asset"]=EngineAssets::put(AssetKind::Terrain,terrainBytes(n,nan,0,0,1,0));
        require(refused([&]{(void)SceneData::decode(card.dump());}),"Fuzz: a scene whose terrain asset holds a non-finite height is refused when opened");
    }

    // Identifiers that are not exactly <kind>-<64 lowercase hex digits> must never be turned into a file path.
    {
        const auto decoy=work/"decoy.slasset";atomic_write(decoy,pristine);
        const std::vector<std::string> bad{"","blob-","blob-"+std::string(63,'a'),"blob-"+std::string(65,'a'),"blob-"+std::string(64,'A'),"Blob-"+std::string(64,'a'),
            "blob-\\..\\..\\decoy","blob-/../../decoy","model-\\..\\..\\decoy","blob-"+std::string(64,'a')+".slasset","blob-"+std::string(64,'a')+":stream","blob-"+std::string(64,'a')+"::$DATA",
            "blob-"+std::string(64,'a')+"\\x","blob-"+std::string(64,'a')+" ","blob-"+std::string(64,'a')+std::string(1,'\0'),"unknown-"+std::string(64,'a'),"C:\\Windows\\win.ini","\\\\server\\share\\x","texture-/x"};
        size_t wrong=0;
        for(const auto& name:bad){
            for(const auto kind:{AssetKind::Sculpt,AssetKind::Terrain,AssetKind::Model,AssetKind::Texture,AssetKind::Blob}){
                if(EngineAssets::valid_id(kind,name)||EngineAssets::exists(kind,name))++wrong;
                if(outcome([&]{(void)EngineAssets::get(kind,name);})!=Result::AssetMissingError)++wrong;
            }
            if(!EngineAssets::locate(name).empty())++wrong;
            if(EngineAssets::keep(name))++wrong;
            if(outcome([&]{(void)EngineAssets::verified_file_bytes(name);})!=Result::AssetMissingError)++wrong;
            if(outcome([&]{EngineAssets::verify_on_disk(name);})!=Result::AssetMissingError)++wrong;
            const auto target=work/"bundle-target";
            if(outcome([&]{EngineAssets::bundle_into({name},target);})!=Result::AssetMissingError)++wrong;
            if(fs::exists(target)&&!fs::is_empty(target))++wrong;
        }
        require(wrong==0,"Fuzz: malformed asset identifiers (traversal, stream names, wrong case or length) are never resolved to a file");
        require(read_file(decoy)==pristine,"Fuzz: a file outside the library is never read or changed through an asset identifier");
    }

    // Portable scenes: a manifest or bundle that was edited, forged or damaged must fail verification cleanly, and nothing may be read outside the bundle.
    {
        SceneData scene;scene.Characters={tree};scene.Terrain.emplace();scene.Terrain->brush(0,0,2,1,"raise",0,0,.05);
        const auto bundle=work/"bundle";export_portable_scene(scene,bundle);
        require(verify_portable_scene(bundle).at("success").get<bool>(),"Fuzz baseline: a fresh export verifies");
        std::string sculptFile;for(const auto& entry:fs::directory_iterator(bundle/"assets"))if(entry.path().filename().wstring().starts_with(L"sculpt-"))sculptFile=utf8(entry.path().filename().wstring());
        const auto manifest=json::parse(read_file(bundle/"manifest.json"));
        require(!sculptFile.empty()&&manifest.at("assets").size()==2,"Fuzz baseline: the export holds a sculpt asset and a terrain asset");
        int variant=0;
        auto variantOutcome=[&](auto&& edit,auto&& prepare){
            const auto copy=work/("variant-"+std::to_string(++variant));fs::copy(bundle,copy,fs::copy_options::recursive);
            auto edited=manifest;edit(edited);atomic_write(copy/"manifest.json",edited.dump());prepare(copy);
            return outcome([&]{(void)verify_portable_scene(copy);});};
        auto untouched=[](const fs::path&){};
        auto sameEdit=[](json&){};
        auto setPath=[&](const std::string& path){return [path](json& m){m["assets"][0]["path"]=path;};};
        size_t wrong=0;
        for(const std::string& path:std::vector<std::string>{"../manifest.json","..\\manifest.json","C:\\Windows\\win.ini","\\Windows\\win.ini","C:win.ini","assets\\..\\..\\x","assets/../../x","","."," ","assets","assets/","./assets/x","\\\\?\\C:\\Windows\\win.ini","\\\\server\\share\\x","assets/nul","NUL","assets/x.slasset.","assets/missing.slasset"})
            {const auto r=variantOutcome(setPath(path),untouched);if(r==Result::Accepted||r==Result::Unknown)++wrong;}
        require(wrong==0,"Fuzz: manifest entries that climb out of the bundle, are absolute, rooted, device names or missing are refused");
        wrong=0;
        auto expectRefused=[&](auto&& edit,auto&& prepare){const auto r=variantOutcome(edit,prepare);if(r!=Result::AssetMissingError&&r!=Result::OtherError)++wrong;};
        expectRefused([](json& m){m["assets"][0]["sha256"]=5;},untouched);
        expectRefused([](json& m){m["assets"][0]["sha256"]=std::string(64,'0');},untouched);
        expectRefused([](json& m){m["assets"][0].erase("sha256");},untouched);
        expectRefused([](json& m){m["assets"][0].erase("path");},untouched);
        expectRefused([](json& m){m["assets"][0].erase("bytes");},untouched);
        expectRefused([](json& m){m["assets"][0]["bytes"]=-1;},untouched);
        expectRefused([](json& m){m["assets"][0]["bytes"]=18446744073709551615ull;},untouched);
        expectRefused([](json& m){m["assets"][0]["bytes"]=1.5;},untouched);
        expectRefused([](json& m){m["assets"][0]["bytes"]="1";},untouched);
        expectRefused([](json& m){m["assets"][0]["path"]=5;},untouched);
        expectRefused([](json& m){m["assets"][0]="x";},untouched);
        expectRefused([](json& m){m["assets"][0]=nullptr;},untouched);
        expectRefused([](json& m){m["assets"]=nullptr;},untouched);
        expectRefused([](json& m){m["assets"]=json::object();},untouched);
        expectRefused([](json& m){m.erase("assets");},untouched);
        expectRefused([](json& m){m.erase("sceneSha256");},untouched);
        expectRefused([](json& m){m["sceneSha256"]=std::string(64,'0');},untouched);
        expectRefused([](json& m){m["version"]=2;},untouched);
        expectRefused([](json& m){m["version"]=1.5;},untouched);
        expectRefused([](json& m){m["version"]="1";},untouched);
        expectRefused([](json& m){m["format"]="starlight-playable-game";},untouched);
        expectRefused([](json& m){m["scene"]="../scene.starscene";},untouched);
        expectRefused([](json& m){m["scene"]="other.starscene";},untouched);
        expectRefused([](json& m){m=json::array();},untouched);
        expectRefused([](json& m){m=nullptr;},untouched);
        expectRefused(sameEdit,[](const fs::path& c){atomic_write(c/"manifest.json","{{{");});
        expectRefused(sameEdit,[](const fs::path& c){atomic_write(c/"manifest.json","");});
        expectRefused(sameEdit,[](const fs::path& c){fs::remove(c/"manifest.json");fs::create_directory(c/"manifest.json");});
        expectRefused(sameEdit,[](const fs::path& c){fs::remove(c/"scene.starscene");});
        expectRefused(sameEdit,[](const fs::path& c){fs::remove(c/"scene.starscene");fs::create_directory(c/"scene.starscene");});
        expectRefused(sameEdit,[](const fs::path& c){const auto b=read_file(c/"scene.starscene");atomic_write(c/"scene.starscene",b.substr(0,b.size()/2));});
        expectRefused(sameEdit,[](const fs::path& c){auto b=read_file(c/"scene.starscene");b[b.size()/2]^=1;atomic_write(c/"scene.starscene",b);});
        expectRefused(sameEdit,[&](const fs::path& c){fs::remove(c/"assets"/sculptFile);fs::create_directory(c/"assets"/sculptFile);});
        expectRefused(sameEdit,[&](const fs::path& c){fs::remove(c/"assets"/sculptFile);});
        expectRefused(sameEdit,[&](const fs::path& c){atomic_write(c/"assets"/sculptFile,"");});
        expectRefused(sameEdit,[&](const fs::path& c){auto b=read_file(c/"assets"/sculptFile);b.back()^=1;atomic_write(c/"assets"/sculptFile,b);});
        expectRefused(sameEdit,[](const fs::path& c){fs::remove_all(c/"assets");});
        expectRefused([](json& m){m["assets"]=json::array();},untouched);
        require(wrong==0,"Fuzz: manifests with wrong types, missing fields, wrong hashes or sizes, and bundles with missing or damaged files are refused");
        // Windows lets "file:name" address a hidden data stream of a file and "file::$DATA" address the file itself under another spelling; no export contains a colon.
        {
            const std::string streamData="not the asset";
            auto streamed=[&](const std::string& suffix,bool createStream){
                return variantOutcome([&](json& m){m["assets"][0]["path"]="assets/"+sculptFile+suffix;if(createStream){m["assets"][0]["sha256"]=hash_bytes(streamData);m["assets"][0]["bytes"]=streamData.size();}},
                    [&](const fs::path& c){if(createStream)std::ofstream(L"\\\\?\\"+fs::path(c/"assets"/sculptFile).make_preferred().wstring()+L":evil",std::ios::binary)<<streamData;});};
            require(streamed(":evil",true)!=Result::Accepted,"Fuzz: a manifest entry naming a hidden data stream with a matching hash does not verify");
            require(streamed("::$DATA",false)!=Result::Accepted,"Fuzz: a manifest entry spelling a file as file::$DATA is refused");
        }
        // A junction that points the assets folder somewhere else must not be followed out of the bundle.
        const auto outside=work/"outside-copy";fs::copy(bundle/"assets",outside,fs::copy_options::recursive);
        const auto linked=work/"bundle-linked";fs::copy(bundle,linked,fs::copy_options::recursive);fs::remove_all(linked/"assets");
        (void)std::system(("cmd /c mklink /J \""+utf8(fs::path(linked/"assets").make_preferred().wstring())+"\" \""+utf8(fs::path(outside).make_preferred().wstring())+"\" >nul 2>nul").c_str());
        std::error_code probe;
        if(fs::is_directory(linked/"assets",probe)){
            require(refused([&]{(void)verify_portable_scene(linked);}),"Fuzz: an assets folder that is a junction to somewhere else fails verification");
            fs::remove(linked/"assets",probe);
        } else require(true,"Fuzz: junctions cannot be created in this session, so the junction case was not exercised");
        require(variantOutcome([](json& m){m["assets"].push_back(m["assets"][0]);},untouched)==Result::Accepted,"Fuzz: a manifest listing an asset twice still verifies");
        require(variantOutcome([](json& m){auto twin=m["assets"][0];twin["sha256"]=std::string(64,'0');m["assets"].push_back(twin);},untouched)!=Result::Accepted,"Fuzz: a duplicate entry with a wrong hash fails verification");
        require(refused([&]{(void)verify_portable_scene(work/"no-such-bundle");}),"Fuzz: a folder that does not exist fails verification");
    }
}
