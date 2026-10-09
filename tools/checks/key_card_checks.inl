#include <fstream>
#include "../../deep/engine/keycard_view.h"
template<class Require> void key_card_checks(Require require,const fs::path& folder){
    const auto work=folder/"key-card-checks";fs::remove_all(work);fs::create_directories(work);
    Character cube;cube.Shape="cube";
    Character tree;tree.Id="kc-tree";tree.Name="Authored sculpt";tree.Sculpt=sculpt_from(object_mesh(cube));tree.Shape.reset();tree.validate();
    Character twin=tree;twin.Id="kc-twin";twin.X=4;
    SceneData scene;scene.Characters={tree,twin};scene.Terrain.emplace();scene.Terrain->brush(0,0,2,1,"raise",0,0,.05);
    const auto text=scene.encode();
    const auto document=json::parse(text);
    require(document.at("SchemaVersion")==16,"Key cards are written at schema 16");
    require(!document.at("Characters")[0].at("Sculpt").contains("Positions")&&document.at("Characters")[0].at("Sculpt").contains("Asset"),"A key card holds a sculpt reference and no vertices");
    require(!document.at("Terrain").contains("Heights")&&!document.at("Terrain").contains("Paint")&&document.at("Terrain").contains("Asset"),"A key card holds a terrain reference and no grids");
    require(text.size()<8192,"A scene with a sculpt and a terrain stays a small key card");
    require(document.at("Characters")[0].at("Sculpt")==document.at("Characters")[1].at("Sculpt"),"Identical sculpt geometry is one shared asset");
    const auto ids=EngineAssets::referenced_ids(document);
    require(ids.size()==2,"A scene names exactly its sculpt asset and its terrain asset");
    auto first=SceneData::decode(text),second=SceneData::decode(text);
    require(first.Characters[0].Sculpt->Revision==second.Characters[0].Sculpt->Revision&&first.Characters[0].Sculpt->Revision==first.Characters[1].Sculpt->Revision,"Loading the same asset again keeps the same geometry identity");
    require(first.Characters[0].Sculpt->content_revision()==first.Characters[1].Sculpt->content_revision(),"Both instances of one asset share one geometry key");
    auto edited=first;edited.Characters[0].Sculpt->Positions[0]+=.01;edited.Characters[0].Sculpt->touch();
    require(edited.Characters[0].Sculpt->content_revision()!=first.Characters[0].Sculpt->content_revision(),"An edit gives the edited geometry a new identity");
    const auto originalAsset=document.at("Characters")[0].at("Sculpt").at("Asset").get<std::string>();
    const auto editedAsset=store_sculpt(*edited.Characters[0].Sculpt);
    require(EngineAssets::valid_id(AssetKind::Sculpt,editedAsset)&&editedAsset!=originalAsset,"Storing an edited copy gives it a new asset id");
    EngineAssets::verify_on_disk(originalAsset);
    const auto again=SceneData::decode(text);
    require(again.Characters[0].Sculpt->Positions==tree.Sculpt->Positions&&again.Characters[0].Sculpt->Revision==first.Characters[0].Sculpt->Revision,"Editing one copy does not change the stored original asset or its identity");

    const auto bundle=work/"bundle";
    export_portable_scene(scene,bundle);
    verify_portable_scene(bundle);
    require(fs::is_directory(bundle/"assets"),"Portable export carries an assets folder");
    size_t slassets=0;for(const auto& e:fs::directory_iterator(bundle/"assets"))if(e.path().extension()==".slasset")++slassets;
    require(slassets==2,"Portable export bundles exactly the referenced engine assets");
    auto scenePath=bundle/"scene.starscene";
    auto reopened=load_scene(scenePath);
    require(reopened.Characters[0].Sculpt&&reopened.Characters[0].Sculpt->Positions==scene.Characters[0].Sculpt->Positions,"A bundle reopens with its sculpt geometry intact");
    {
        auto broken=work/"bundle-missing";fs::copy(bundle,broken,fs::copy_options::recursive);
        for(const auto& e:fs::directory_iterator(broken/"assets"))if(e.path().filename().wstring().starts_with(L"sculpt-")){fs::remove(e.path());break;}
        bool failed=false;try{verify_portable_scene(broken);}catch(const std::exception&){failed=true;}
        require(failed,"A bundle missing an engine asset fails verification even while the asset is cached in memory");
    }
    {
        auto broken=work/"bundle-damaged";fs::copy(bundle,broken,fs::copy_options::recursive);
        for(const auto& e:fs::directory_iterator(broken/"assets"))if(e.path().filename().wstring().starts_with(L"terrain-")){
            auto bytes=read_file(e.path());bytes[bytes.size()/2]^=0x5A;atomic_write(e.path(),bytes);break;}
        bool failed=false;try{verify_portable_scene(broken);}catch(const std::exception&){failed=true;}
        require(failed,"A damaged engine asset fails verification");
    }
    Character reusable=tree;reusable.Id="kc-reusable";
    const auto templates=work/"templates";fs::create_directories(templates);
    save_object_template(reusable,templates/"tree.starobject");
    require(fs::is_directory(templates/"assets"),"A reusable object is saved with its engine assets beside it");
    {
        EngineAssets::ScopedRoot other(work/"other-machine-library");
        auto loaded=load_object_template(templates/"tree.starobject");
        require(loaded.Sculpt&&loaded.Sculpt->Positions==tree.Sculpt->Positions,"A reusable object reopens from a library that never held its geometry, using the assets beside it");
    }
    save_object_group({reusable,twin},templates/"pair.stargroup");
    {
        EngineAssets::ScopedRoot other(work/"other-machine-library-2");
        require(load_object_group(templates/"pair.stargroup").size()==2,"A reusable group reopens from its bundled assets");
    }
    {
        auto old=json::parse(read_file(templates/"tree.starobject"));old["Version"]=1;
        bool conversion=false;try{decode_object_template(old.dump());}catch(const ConversionRequired&){conversion=true;}
        require(conversion,"A version 1 reusable object is refused with a conversion-required error");
        auto oldGroup=json::parse(read_file(templates/"pair.stargroup"));oldGroup["Version"]=1;
        conversion=false;try{decode_object_group(oldGroup.dump());}catch(const ConversionRequired&){conversion=true;}
        require(conversion,"A version 1 reusable group is refused with a conversion-required error");
    }
    {
        EngineAssets::ScopedRoot library(work/"retry-library");
        const auto id=store_sculpt(*tree.Sculpt);
        auto file=EngineAssets::file_for(id);
        auto bytes=read_file(file);const auto intact=bytes;bytes[bytes.size()-9]^=0x33;atomic_write(file,bytes);
        bool damaged=false;try{EngineAssets::verify_on_disk(id);}catch(const AssetMissing&){damaged=true;}
        require(damaged,"A damaged library file is detected on disk");
        EngineAssets::ScopedRoot fresh(work/"retry-library");
        store_sculpt(*tree.Sculpt);
        EngineAssets::verify_on_disk(id);
        require(read_file(file)==intact,"Storing an asset over a damaged file repairs it");
    }
    // ASSET-2: a damaged copy in a higher-priority root must not hide an intact copy in a later root.
    {
        std::string id;
        {
            EngineAssets::ScopedRoot source(work/"intact-source");
            id=store_sculpt(*tree.Sculpt);
        }
        EngineAssets::ScopedRoot home(work/"damaged-home");EngineAssets::add_search_root(work/"intact-source");
        atomic_write(EngineAssets::file_for(id),std::string{});
        EngineAssets::drop_memory_cache();
        require(fs::is_regular_file(EngineAssets::file_for(id))&&fs::file_size(EngineAssets::file_for(id))==0,"The primary library holds an empty copy of the asset");
        const auto loaded=load_sculpt(id);
        require(loaded.Positions==tree.Sculpt->Positions,"An intact copy in a later root is used when the primary copy is damaged");
        EngineAssets::drop_memory_cache();
        const auto bytes=EngineAssets::verified_file_bytes(id);
        require(bytes==read_file(work/"intact-source"/starlight::editor::wide(id+".slasset")),"The verified file bytes come from the intact copy");
        std::string missing="sculpt-"+std::string(64,'e');
        bool reported=false;try{(void)EngineAssets::get(AssetKind::Sculpt,missing);}catch(const AssetMissing&){reported=true;}
        require(reported,"An asset that no root holds is still reported missing");
        atomic_write(work/"damaged-home"/starlight::editor::wide(id+".slasset"),std::string{});
        fs::remove(work/"intact-source"/starlight::editor::wide(id+".slasset"));
        EngineAssets::drop_memory_cache();
        bool damaged=false;try{(void)EngineAssets::get(AssetKind::Sculpt,id);}catch(const AssetMissing& e){damaged=std::string(e.what()).find("not a Starlight asset")!=std::string::npos||std::string(e.what()).find("damaged")!=std::string::npos||std::string(e.what()).find("truncated")!=std::string::npos;}
        require(damaged,"When every copy is damaged the damage is reported, not a plain missing asset");
    }
    // KEYCARD-1: the key-card host API carries paths as UTF-8, so a folder outside the ANSI code page is found.
    {
        const auto odd=work/L"Zo\u00eb-\u043a\u043b\u044e\u0447";fs::create_directories(odd);
        atomic_write(odd/"card.starscene","{}");
        auto utf8=[](const fs::path& path){const auto text=path.u8string();return std::string(reinterpret_cast<const char*>(text.data()),text.size());};
        std::string error;
        const auto named=keycard::resolveCardPath(utf8(odd/"card.starscene"),std::string(),"card.starscene",error);
        require(error.empty()&&named==utf8(odd/"card.starscene"),"A --card path with characters outside the ANSI code page is found");
        error.clear();
        const auto beside=keycard::resolveCardPath(std::string(),utf8(odd),"card.starscene",error);
        require(error.empty()&&beside==utf8(odd/"card.starscene"),"A card beside the host module is found when the module folder has such characters, and is returned as UTF-8");
        error.clear();
        require(keycard::resolveCardPath(utf8(odd/"missing.starscene"),std::string(),"card.starscene",error).empty()&&error.find("does not exist")!=std::string::npos,"A missing --card file is still reported");
    }
}

template<class Require> void model_asset_checks(Require require,const fs::path& folder){
    const auto work=folder/"model-asset-checks";fs::remove_all(work);fs::create_directories(work);
    EngineAssets::ScopedRoot library(work/"library");
    auto modelFiles=[&]{size_t count=0;for(const auto& entry:fs::directory_iterator(EngineAssets::root()))if(entry.path().filename().wstring().starts_with(L"model-"))++count;return count;};
    const auto fileA=solid_glb_fixture(work);
    const auto copyA=work/"copy-of-solid.glb";fs::copy_file(fileA,copyA);
    const auto idA=import_model(fileA),idCopy=import_model(copyA);
    require(EngineAssets::valid_id(AssetKind::Model,idA)&&idA==idCopy,"Importing the same GLB twice, even from another file, gives one model id");
    require(modelFiles()==1,"The library stores a model imported twice exactly once");
    const auto idB=import_model(solid_glb_variant(work,2.f));
    require(idB!=idA&&modelFiles()==2,"A model with different bytes gets its own id");
    require(*EngineAssets::get(AssetKind::Model,idA)==read_file(fileA),"The stored model payload is the GLB exactly as authored");
    require(!EngineAssets::exists(AssetKind::Texture,idA),"A model id is never accepted as a texture");

    const auto original=read_file(fileA);uint32_t jsonSize;std::memcpy(&jsonSize,original.data()+12,4);
    const auto description=json::parse(original.substr(20,jsonSize));const auto suffix=original.substr(20+jsonSize);
    auto rebuilt=[&](json document){auto text=document.dump();while(text.size()%4)text+=' ';auto result=original.substr(0,20);uint32_t size=uint32_t(text.size()),total=uint32_t(20+size+suffix.size());std::memcpy(result.data()+8,&total,4);std::memcpy(result.data()+12,&size,4);result+=text+suffix;return result;};
    auto refused=[&](const std::string& bytes){const auto path=work/"candidate.glb";atomic_write(path,bytes);try{(void)import_model(path);}catch(const std::exception&){return true;}return false;};
    auto external=description;external["buffers"][0]["uri"]="outside.bin";
    auto embedded=description;embedded["images"]=json::array({json{{"uri","data:image/png;base64,AAAA"}}});
    auto cyclic=description;cyclic["nodes"][0]["children"]=json::array({0});
    const auto before=modelFiles();
    require(refused("not a glb")&&refused(original.substr(0,original.size()-8))&&refused(rebuilt(external))&&refused(rebuilt(cyclic)),"Text, a truncated GLB, a model with an external resource link and a cyclic model are refused at import");
    require(modelFiles()==before,"A refused import stores nothing");
    {
        bool imported=true;try{import_model_bytes(rebuilt(embedded));}catch(const std::exception&){imported=false;}
        require(imported,"A model whose resources are embedded as data links imports");
    }
    bool missingReported=false;try{(void)load_model("model-"+std::string(64,'d'));}catch(const AssetMissing&){missingReported=true;}
    bool pathRefused=false;try{(void)load_model("C:/models/old.glb");}catch(const AssetMissing&){pathRefused=true;}
    require(missingReported&&pathRefused,"Loading a model the library does not hold, or a file path, reports a missing asset");

    {
        Character legacy;legacy.Asset="C:/models/old.glb";bool invalid=false;try{legacy.validate();}catch(const std::exception&){invalid=true;}
        require(invalid,"A character whose model is a file path does not validate");
        SceneData scene;Character model;model.Asset=idA;scene.Characters={model};
        auto document=json::parse(scene.encode());
        require(document.at("Characters")[0].at("Asset")==idA&&EngineAssets::referenced_ids(document).contains(idA)&&scene_asset_ids(scene).contains(idA),"A key card holds the model id and names the model as a dependency");
        document["Characters"][0]["Asset"]="C:/models/old.glb";bool conversion=false;
        try{SceneData::decode(document.dump());}catch(const ConversionRequired&){conversion=true;}
        require(conversion,"A schema 16 scene that names a model by file path is refused with a conversion-required error");
    }
    {
        model_service_trim();
        Character a,b;a.Id="share-a";b.Id="share-b";a.Asset=b.Asset=idA;b.X=5;b.RotationY=40;
        const auto start=model_service_stats();
        auto first=shared_parts(a),second=shared_parts(b);auto direct=load_model(idA);
        const auto end=model_service_stats();
        require(first.get()==second.get()&&first.get()==direct.get(),"Two objects using the same model share one parsed geometry");
        require(end.misses-start.misses==1&&end.hits-start.hits==2,"A shared model is parsed once and every later user is a cache hit");
        require(geometry_key(a)==geometry_key(b)&&render_geometry_key(a)==render_geometry_key(b),"Objects using one model share one geometry identity wherever they stand");
        model_service_trim();
        require(load_model(idA).get()==first.get(),"A parsed model that is still in use is never evicted");
        first.reset();second.reset();direct.reset();
        const auto beforeTrim=model_service_stats();model_service_trim();
        require(model_service_stats().evicted>beforeTrim.evicted,"A parsed model nobody uses any more can be evicted");
        const auto beforeReload=model_service_stats();(void)load_model(idA);
        require(model_service_stats().misses==beforeReload.misses+1,"An evicted model is parsed again from the library on its next use");
    }
    {
        SceneData scene;Character model;model.Id="bundle-model";model.Asset=idA;scene.Characters={model};
        const auto bundle=work/"bundle";
        export_portable_scene(scene,bundle);verify_portable_scene(bundle);
        require(fs::is_regular_file(bundle/"assets"/starlight::editor::wide(idA+".slasset")),"Portable export bundles a model as a library asset named by its id");
        size_t loose=0;for(const auto& entry:fs::directory_iterator(bundle/"assets"))if(entry.path().extension()==".glb")++loose;
        const auto manifest=json::parse(read_file(bundle/"manifest.json"));
        bool listed=false;for(const auto& asset:manifest.at("assets"))listed|=asset.at("path")=="assets/"+idA+".slasset";
        require(loose==0&&listed,"The bundle holds no loose GLB and its manifest lists the model asset");
        auto missing=work/"bundle-model-missing";fs::copy(bundle,missing,fs::copy_options::recursive);fs::remove(missing/"assets"/starlight::editor::wide(idA+".slasset"));
        bool failed=false;try{verify_portable_scene(missing);}catch(const std::exception&){failed=true;}
        require(failed,"A bundle missing its model fails verification even while the model is cached in memory");
        auto damaged=work/"bundle-model-damaged";fs::copy(bundle,damaged,fs::copy_options::recursive);
        {const auto path=damaged/"assets"/starlight::editor::wide(idA+".slasset");auto bytes=read_file(path);bytes[bytes.size()/2]^=0x5A;atomic_write(path,bytes);}
        failed=false;try{verify_portable_scene(damaged);}catch(const std::exception&){failed=true;}
        require(failed,"A bundle whose model was damaged fails verification even while the model is cached in memory");
        auto unlisted=work/"bundle-model-unlisted";fs::copy(bundle,unlisted,fs::copy_options::recursive);
        {auto edited=manifest;edited["assets"]=json::array();atomic_write(unlisted/"manifest.json",edited.dump());}
        failed=false;try{verify_portable_scene(unlisted);}catch(const std::exception&){failed=true;}
        require(failed,"A scene whose model is missing from the bundle manifest fails verification");
    }
    {
        Character prop;prop.Id="reusable-model";prop.Name="Imported prop";prop.Asset=idA;prop.Width=2;
        const auto templates=work/"templates";fs::create_directories(templates);
        save_object_template(prop,templates/"prop.starobject");
        require(fs::is_regular_file(templates/"assets"/starlight::editor::wide(idA+".slasset")),"A reusable object with an imported model is saved with the model beside it");
        {
            EngineAssets::ScopedRoot other(work/"other-library");
            auto loaded=load_object_template(templates/"prop.starobject");
            const auto located=EngineAssets::locate(idA);
            require(loaded.Asset==idA&&loaded.Width==2&&loaded.Id!=prop.Id&&!located.empty()&&fs::equivalent(located.parent_path(),templates/"assets"),"A reusable object with a model reopens in a library that never held the model, using the model saved beside it");
        }
        Character second=prop;second.Id="reusable-model-2";second.X=3;
        save_object_group({prop,second},templates/"pair.stargroup");
        {
            EngineAssets::ScopedRoot other(work/"other-library-2");
            auto group=load_object_group(templates/"pair.stargroup");
            const auto located=EngineAssets::locate(idA);
            require(group.size()==2&&group[0].Asset==idA&&group[1].Asset==idA&&!located.empty()&&fs::equivalent(located.parent_path(),templates/"assets"),"A reusable group of imported models reopens from the models saved beside it");
        }
        Character ghost=prop;ghost.Asset="model-"+std::string(64,'f');bool unsaved=false;
        try{save_object_template(ghost,templates/"ghost.starobject");}catch(const AssetMissing&){unsaved=true;}
        require(unsaved&&!fs::exists(templates/"ghost.starobject"),"A reusable object whose model the library does not hold is not saved");
        bool unopened=false;try{decode_object_template(json{{"Format","starlight-object"},{"Version",2},{"Object",ghost}}.dump());}catch(const AssetMissing&){unopened=true;}
        require(unopened,"A reusable object whose model is nowhere to be found cannot be reopened");
    }
    {
        EngineAssets::ScopedRoot source(work/"keep-source");
        const auto id=import_model(fileA);
        EngineAssets::ScopedRoot home(work/"keep-home");EngineAssets::add_search_root(work/"keep-source");
        require(!fs::exists(EngineAssets::file_for(id))&&EngineAssets::keep(id)&&fs::is_regular_file(EngineAssets::file_for(id)),"The library copies a model it only finds in a search root into its own folder on request");
        require(!EngineAssets::keep("model-"+std::string(64,'9')),"Keeping a model that exists nowhere reports false");
    }

    const unsigned char pngBytes[]={0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x72,0xb6,0x0d,0x24,0x00,0x00,0x00,0x14,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xcf,0xf0,0xff,0x3f,0x83,0xc3,0xff,0xff,0x0c,0x30,0x06,0x00,0x5c,0x79,0x0a,0x77,0x46,0x16,0xfa,0x08,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
    const std::string png(reinterpret_cast<const char*>(pngBytes),sizeof(pngBytes));
    const unsigned char jpegBytes[]={0xff,0xd8,0xff,0xe0,0x00,0x10,0x4a,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0xff,0xc0,0x00,0x11,0x08,0x00,0x02,0x00,0x03,0x03,0x01,0x22,0x00,0x02,0x11,0x01,0x03,0x11,0x01,0xff,0xda,0x00,0x0c,0x03,0x01,0x00,0x02,0x11,0x03,0x11,0x00,0x3f,0x00,0x00,0x00,0xff,0xd9};
    const std::string jpeg(reinterpret_cast<const char*>(jpegBytes),sizeof(jpegBytes));
    {
        const auto idPng=import_texture_bytes(png);
        atomic_write(work/"texture.png",png);
        require(EngineAssets::valid_id(AssetKind::Texture,idPng)&&import_texture(work/"texture.png")==idPng,"A texture imported from bytes and from a file with the same contents has one id");
        const auto info=describe_texture(png),jpegInfo=describe_texture(jpeg);
        require(info.png&&info.width==2&&info.height==2&&!jpegInfo.png&&jpegInfo.width==3&&jpegInfo.height==2,"PNG and JPEG headers are read for their format and size");
        const auto idJpeg=import_texture_bytes(jpeg);
        require(idJpeg!=idPng&&*load_texture(idPng)==png&&*load_texture(idJpeg)==jpeg,"Textures come back byte for byte as authored");
        auto bad=png;bad[16]=0;bad[17]=0;bad[18]=0;bad[19]=0;
        auto rejected=[&](const std::string& bytes){try{(void)import_texture_bytes(bytes);}catch(const std::exception&){return true;}return false;};
        require(rejected("plain text")&&rejected(png.substr(0,png.size()-4))&&rejected(bad)&&rejected(original)&&rejected(jpeg.substr(0,jpeg.size()-2)),"Text, a truncated PNG or JPEG, a zero-width PNG and a model are refused as textures");
        std::set<std::string> ids{idPng,idJpeg};
        const auto textureBundle=work/"texture-bundle";EngineAssets::bundle_into(ids,textureBundle);
        EngineAssets::verify_on_disk(idPng);
        require(fs::is_regular_file(textureBundle/starlight::editor::wide(idPng+".slasset"))&&fs::is_regular_file(textureBundle/starlight::editor::wide(idJpeg+".slasset")),"Textures bundle into a folder like every other asset");
        const auto file=EngineAssets::file_for(idPng);auto bytes=read_file(file);bytes[bytes.size()-9]^=0x33;atomic_write(file,bytes);
        bool damaged=false;try{EngineAssets::verify_on_disk(idPng);}catch(const AssetMissing&){damaged=true;}
        EngineAssets::ScopedRoot again(work/"library");
        (void)import_texture_bytes(png);
        EngineAssets::verify_on_disk(idPng);
        require(damaged,"A damaged stored texture is detected on disk and importing it again repairs it");
    }
}
