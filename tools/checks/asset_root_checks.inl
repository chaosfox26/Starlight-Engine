#include <limits>
#include <sstream>
#include <wincodec.h>
#include <wrl/client.h>
inline std::string asset_checks_uv_glb(bool plainPrimitive){
 json description=json::parse(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1},"indices":2}]}],"buffers":[{"byteLength":104}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":32},{"buffer":0,"byteOffset":80,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},{"bufferView":1,"componentType":5126,"count":4,"type":"VEC2"},{"bufferView":2,"componentType":5123,"count":12,"type":"SCALAR"}]})");
 if(plainPrimitive)description["meshes"][0]["primitives"].push_back(json::parse(R"({"attributes":{"POSITION":0},"indices":2})"));
 const std::array<float,12> points{0,0,0,1,0,0,0,1,0,0,0,1};const std::array<float,8> coordinates{.1f,.1f,.9f,.1f,.1f,.9f,.5f,.5f};const std::array<uint16_t,12> indices{0,2,1,0,1,3,0,3,2,1,2,3};
 auto text=description.dump();while(text.size()%4)text+=' ';
 std::string bin;bin.append(reinterpret_cast<const char*>(points.data()),48);bin.append(reinterpret_cast<const char*>(coordinates.data()),32);bin.append(reinterpret_cast<const char*>(indices.data()),24);
 std::string bytes;auto word=[&](uint32_t v){bytes.append(reinterpret_cast<const char*>(&v),4);};
 word(0x46546c67);word(2);word(uint32_t(12+8+text.size()+8+bin.size()));word(uint32_t(text.size()));word(0x4e4f534a);bytes+=text;word(uint32_t(bin.size()));word(0x004e4942);bytes+=bin;
 return bytes;
}
inline std::string asset_checks_atlas(size_t pairs){
 std::string bytes;for(size_t i=0;i<pairs;++i){const float u=float(i%7+1)/8.f,v=float(i%5+1)/6.f;bytes.append(reinterpret_cast<const char*>(&u),4);bytes.append(reinterpret_cast<const char*>(&v),4);}return bytes;
}
inline std::string asset_checks_png(uint32_t width,uint32_t height,const std::function<void(std::string&)>& tamper={},bool breakAdler=false){
 auto be=[](std::string& out,uint32_t v){for(int s=24;s>=0;s-=8)out.push_back(char((v>>s)&255));};
 auto chunk=[&](std::string& out,const char* type,const std::string& body){be(out,uint32_t(body.size()));std::string typed=std::string(type)+body;out+=typed;be(out,crc32_bytes(typed));};
 std::string raw;for(uint32_t y=0;y<height;++y){raw.push_back(char(y%5));for(uint32_t x=0;x<width;++x){raw.push_back(char(x*7+y));raw.push_back(char(y*3));raw.push_back(char(x+y));raw.push_back(char(255));}}
 if(tamper)tamper(raw);
 uint32_t a=1,b=0;for(const unsigned char c:raw){a=(a+c)%65521;b=(b+a)%65521;}
 if(breakAdler)a^=1;
 std::string zlib("\x78\x01",2);size_t at=0;
 do{const size_t n=std::min<size_t>(65535,raw.size()-at);zlib.push_back(at+n==raw.size()?1:0);zlib.push_back(char(n&255));zlib.push_back(char(n>>8));zlib.push_back(char(~n&255));zlib.push_back(char((~n>>8)&255));zlib.append(raw,at,n);at+=n;}while(at<raw.size());
 be(zlib,(b<<16)|a);
 std::string header;be(header,width);be(header,height);header+=std::string("\x08\x06\x00\x00\x00",5);
 std::string png("\x89PNG\r\n\x1a\n",8);chunk(png,"IHDR",header);chunk(png,"IDAT",zlib);chunk(png,"IEND","");return png;
}
inline std::string asset_checks_jpeg(){
 using Microsoft::WRL::ComPtr;
 (void)CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 ComPtr<IWICImagingFactory> factory;if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))))return {};
 ComPtr<IStream> stream;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)))return {};
 ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> options;
 if(FAILED(factory->CreateEncoder(GUID_ContainerFormatJpeg,nullptr,&encoder))||FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(&frame,&options))||FAILED(frame->Initialize(options.Get())))return {};
 const UINT size=16;WICPixelFormatGUID format=GUID_WICPixelFormat24bppBGR;std::vector<BYTE> pixels(size*size*3);
 for(UINT y=0;y<size;++y)for(UINT x=0;x<size;++x){pixels[(y*size+x)*3]=BYTE(x*15);pixels[(y*size+x)*3+1]=BYTE(y*15);pixels[(y*size+x)*3+2]=128;}
 if(FAILED(frame->SetSize(size,size))||FAILED(frame->SetPixelFormat(&format))||FAILED(frame->WritePixels(size,size*3,UINT(pixels.size()),pixels.data()))||FAILED(frame->Commit())||FAILED(encoder->Commit()))return {};
 STATSTG statistics{};stream->Stat(&statistics,STATFLAG_NONAME);LARGE_INTEGER start{};stream->Seek(start,STREAM_SEEK_SET,nullptr);
 std::string bytes(size_t(statistics.cbSize.QuadPart),'\0');ULONG read=0;stream->Read(bytes.data(),ULONG(bytes.size()),&read);bytes.resize(read);return bytes;
}
struct AssetChecksEnvironment{
 std::wstring name,previous;bool had=false;
 AssetChecksEnvironment(const wchar_t* variable,const std::wstring& value):name(variable){wchar_t buffer[32768];auto n=GetEnvironmentVariableW(variable,buffer,32768);if(n&&n<32768){had=true;previous.assign(buffer,n);}SetEnvironmentVariableW(variable,value.c_str());}
 ~AssetChecksEnvironment(){SetEnvironmentVariableW(name.c_str(),had?previous.c_str():nullptr);}
};
template<class Require> void asset_root_checks(Require require,const fs::path& folder){
 const auto work=fs::absolute(folder/"asset-root-checks").lexically_normal();fs::remove_all(work);fs::create_directories(work);
 const auto dataFolder=work/"data";const auto registryPath=dataFolder/"asset-roots.json";
 AssetChecksEnvironment dataRoot(L"STARLIGHT_DATA_ROOT",dataFolder.wstring());
 AssetChecksEnvironment noOverride(L"STARLIGHT_ASSET_LIBRARY",L"");
 auto refused=[&](auto&& action){try{action();}catch(const std::exception&){return true;}return false;};
 auto text_of=[&](auto&& action){try{action();}catch(const std::exception& e){return std::string(e.what());}return std::string();};
 auto has=[&](const std::string& text,const std::string& part){return text.find(part)!=std::string::npos;};
 auto files_in=[&](const fs::path& root,const std::string& extension){size_t n=0;std::error_code e;if(fs::is_directory(root,e))for(const auto& entry:fs::directory_iterator(root))if(entry.path().extension()==extension)++n;return n;};
 auto origins=[&]{std::vector<std::string> list;for(const auto& s:EngineAssets::root_status())if(s.Origin!="note")list.push_back(s.Origin);return list;};
 auto marker=[&](const fs::path& root,const std::string& format,int version){atomic_write(root/"library.json",json{{"format",format},{"version",version}}.dump());};
 const auto durable=work/"stores"/"durable";const auto starter=work/"repo"/"library";const auto scene=work/"repo"/"scene";
 require(EngineAssets::registry_file()==registryPath,"The root registry lives in the Starlight data folder and honors STARLIGHT_DATA_ROOT");
 require(EngineAssets::cache_folder()==dataFolder/"AssetCache","Derived cache data has its own folder in the data folder, never inside a library root");
 EngineAssets::ScopedRegistry registry(registryPath);
 require(EngineAssets::registered_roots().empty()&&EngineAssets::root()==dataFolder/"AssetLibrary"&&origins()==std::vector<std::string>{"default local library"},"With nothing registered the primary root is the default local library and it is reported as such");
 require(has(EngineAssets::describe_roots(),"default local library")&&EngineAssets::root_problems().empty(),"The root report names the default local library and has no problems");

 EngineAssets::initialize_root(durable,false,"durable store");
 require(EngineAssets::marker_problem(durable).empty()&&json::parse(read_file(durable/"library.json"))==json({{"format","starlight-asset-library"},{"version",1}}),"Initializing a new root writes the marker with the supported format and version");
 const auto markerBytes=read_file(durable/"library.json");
 EngineAssets::initialize_root(durable,false,"durable store");
 require(read_file(durable/"library.json")==markerBytes&&EngineAssets::registered_roots().size()==1,"Initializing an existing valid root changes nothing and does not register it twice");
 require(fs::is_regular_file(registryPath)&&json::parse(read_file(registryPath)).at("format")=="starlight-asset-roots"&&json::parse(read_file(registryPath)).at("version")==1,"The registry is a file with an explicit format and version");
 {
  EngineAssets::ScopedRegistry again(registryPath);
  const auto roots=EngineAssets::registered_roots();
  require(roots.size()==1&&roots[0].Path==durable&&!roots[0].Writable&&roots[0].Label=="durable store","A registration persists and reads back with its path, role and label");
 }
 require(files_in(dataFolder,".tmp")==0&&files_in(dataFolder,".lock")==0,"Writing the registry leaves no temporary files behind");

 std::string atlasId,modelId;
 {
  EngineAssets::ScopedRoot writer(durable);
  atlasId=import_blob_bytes(UvAtlasType,asset_checks_atlas(4));
  modelId=import_model_bytes(asset_checks_uv_glb(false));
  require(fs::is_regular_file(EngineAssets::file_for(atlasId))&&files_in(durable,".slasset")==2,"A tool with an explicit root writes the authoritative assets into that root only");
  require(files_in(durable,".tmp")==0&&files_in(durable,".lock")==0&&files_in(durable,".json")==1,"A library root holds assets and its marker only, with no leftover or cache files");
 }
 EngineAssets::drop_memory_cache();
 require(EngineAssets::exists(AssetKind::Blob,atlasId)&&EngineAssets::locate(atlasId)==durable/starlight::editor::wide(atlasId+".slasset"),"A registered root is searched without any environment variable or explicit call");
 require(EngineAssets::root()==dataFolder/"AssetLibrary"&&origins()==std::vector<std::string>{"default local library","registry"},"A read-only registered root is searched but is never the write target");
 require(*load_blob(atlasId).envelope==*EngineAssets::get(AssetKind::Blob,atlasId)&&!fs::exists(EngineAssets::file_for(atlasId)),"Reading from the registered root does not copy the asset into the primary root");
 require(EngineAssets::put(AssetKind::Blob,*EngineAssets::get(AssetKind::Blob,atlasId))==atlasId&&!fs::exists(EngineAssets::file_for(atlasId)),"Storing content a registered root already holds does not duplicate it");
 require(EngineAssets::keep(atlasId)&&!fs::exists(EngineAssets::file_for(atlasId)),"Keeping an asset a registered root holds needs no copy");
 EngineAssets::drop_memory_cache();
 {
  EngineAssets::ScopedRoot explicitRoot(work/"explicit");
  require(EngineAssets::root()==work/"explicit"&&!EngineAssets::exists(AssetKind::Blob,atlasId),"An explicit root stands alone: the registry is not consulted");
 }
 require(EngineAssets::exists(AssetKind::Blob,atlasId),"Leaving the explicit scope restores registry resolution");
 {
  AssetChecksEnvironment overrideRoot(L"STARLIGHT_ASSET_LIBRARY",(work/"env-library").wstring());
  EngineAssets::drop_memory_cache();
  require(EngineAssets::root()==work/"env-library"&&!EngineAssets::exists(AssetKind::Blob,atlasId)&&has(EngineAssets::describe_roots(),"STARLIGHT_ASSET_LIBRARY"),"The environment override stands alone and the report says so");
  AssetChecksEnvironment relative(L"STARLIGHT_ASSET_LIBRARY",L"relative-folder");
  require(refused([&]{(void)EngineAssets::root();}),"A relative environment override is refused");
 }
 require(EngineAssets::root()==dataFolder/"AssetLibrary","Removing the environment override restores registry resolution");

 const auto bad=work/"stores"/"bad";fs::create_directories(bad);
 require(has(EngineAssets::marker_problem(bad),"no library.json")&&has(EngineAssets::marker_problem(work/"stores"/"absent"),"does not exist"),"A folder without a marker, or an absent one, is not a library and says why");
 atomic_write(bad/"library.json","{not json");
 require(has(EngineAssets::marker_problem(bad),"JSON"),"A marker that is not JSON is rejected");
 marker(bad,"some-other-format",1);
 require(has(EngineAssets::marker_problem(bad),"format"),"A marker with the wrong format is rejected");
 marker(bad,"starlight-asset-library",2);
 require(has(EngineAssets::marker_problem(bad),"version 2"),"A marker with an unsupported version is rejected, not guessed at");
 atomic_write(bad/"library.json",R"({"format":"starlight-asset-library"})");
 require(has(EngineAssets::marker_problem(bad),"version"),"A marker without a version is rejected");
 require(refused([&]{EngineAssets::register_root(bad,false);})&&refused([&]{EngineAssets::initialize_root(bad,false);})&&EngineAssets::registered_roots().size()==1,"A folder with an invalid marker can be neither registered nor initialized, and the registry is unchanged");
 const auto occupied=work/"stores"/"occupied";fs::create_directories(occupied);atomic_write(occupied/"notes.txt","not a library");
 require(refused([&]{EngineAssets::initialize_root(occupied,false);})&&!fs::exists(occupied/"library.json"),"A non-empty folder without a marker is not initialized as a new library");
 require(refused([&]{EngineAssets::register_root(occupied,false);}),"A folder without a marker cannot be registered");
 marker(bad,"starlight-asset-library",1);
 EngineAssets::register_root(bad,false,"second");
 require(EngineAssets::registered_roots().size()==2&&origins().size()==3,"A valid existing library is registered without being changed");
 require(refused([&]{(void)EngineAssets::get(AssetKind::Blob,"blob-"+std::string(64,'a'));})&&EngineAssets::marker_problem(bad).empty(),"A valid marker does not prove the assets exist: a missing asset is still reported missing");
 require(EngineAssets::unregister_root(bad)&&!EngineAssets::unregister_root(bad)&&fs::exists(bad/"library.json")&&EngineAssets::registered_roots().size()==1,"Unregistering removes only the registration and touches no file");

 const auto local=work/"stores"/"local";
 EngineAssets::initialize_root(local,true,"local writable");
 require(EngineAssets::root()==local&&origins()==std::vector<std::string>{"registry","registry"},"The writable registry root becomes the primary root");
 require(refused([&]{EngineAssets::register_root(durable,true);}),"Only one writable root can be registered");
 {
  const auto id=import_blob_bytes(UvAtlasType,asset_checks_atlas(3));
  require(fs::is_regular_file(local/starlight::editor::wide(id+".slasset"))&&!fs::exists(durable/starlight::editor::wide(id+".slasset")),"New assets are written to the writable root and never to a read-only one");
 }
 {
  const auto away=work/"stores"/"local-away";
  fs::rename(local,away);
  require(refused([&]{(void)EngineAssets::root();})&&refused([&]{(void)import_blob_bytes(UvAtlasType,asset_checks_atlas(2));})&&!fs::exists(local),"A missing writable root fails visibly and no folder is silently created in its place");
  fs::rename(away,local);
 }
 require(EngineAssets::unregister_root(local)&&EngineAssets::root()==dataFolder/"AssetLibrary","Unregistering the writable root restores the default local library as primary");

 {
  EngineAssets::ScopedRegistry fresh(registryPath);
  fs::create_directories(starter);marker(starter,"starlight-asset-library",1);fs::create_directories(scene/"assets");
  EngineAssets::discover_near(scene);
  require(origins()==std::vector<std::string>{"default local library","registry","shipped library","bundle assets"},"Resolution order is explicit/primary, then registry, then the shipped library, then the bundle assets folder");
  const auto name=starlight::editor::wide(atlasId+".slasset");
  fs::copy_file(durable/name,starter/name);fs::copy_file(durable/name,scene/"assets"/name);
  require(EngineAssets::locate(atlasId)==durable/name,"The registry is searched before the shipped library and the bundle");
  fs::rename(durable/name,work/"held.slasset");
  require(EngineAssets::locate(atlasId)==starter/name,"The shipped library is searched before the bundle");
  fs::remove(starter/name);
  require(EngineAssets::locate(atlasId)==scene/"assets"/name,"The bundle assets are the last place searched");
  fs::remove(scene/"assets"/name);
  require(EngineAssets::locate(atlasId).empty(),"An asset held by no root is not found");
  fs::rename(work/"held.slasset",durable/name);
 }
 {
  EngineAssets::ScopedRegistry fresh(registryPath);
  const auto crooked=work/"repo2"/"library";fs::create_directories(crooked);marker(crooked,"starlight-asset-library",2);
  EngineAssets::discover_near(work/"repo2"/"scene2");
  bool noted=false;for(const auto& s:EngineAssets::root_status())if(s.Origin=="note"&&has(s.Problem,"ignored")&&has(s.Problem,"version 2"))noted=true;
  require(noted&&has(EngineAssets::describe_roots(),"ignored")&&!has(EngineAssets::describe_roots(),"shipped library"),"A shipped library with an unsupported marker is ignored and the report says why");
 }
 {
  EngineAssets::ScopedRegistry fresh(registryPath);
  const auto away=work/"stores"/"durable-away";
  EngineAssets::drop_memory_cache();
  fs::rename(durable,away);
  AssetRootStatus found;for(const auto& s:EngineAssets::root_status())if(s.Path==durable)found=s;
  require(!found.Usable&&!found.Present&&has(found.Problem,"does not exist"),"A registered root that is missing is reported unusable with the reason");
  const auto description=EngineAssets::describe_roots();
  require(has(description,"UNUSABLE")&&has(description,utf8(durable.wstring())),"The human-readable root report names the missing root");
  const auto message=text_of([&]{(void)EngineAssets::get(AssetKind::Blob,atlasId);});
  require(has(message,"is missing from the asset library")&&has(message,utf8(durable.wstring()))&&has(message,"does not exist"),"Loading an asset that lived in a missing root fails with the missing root named");
  require(EngineAssets::root()==dataFolder/"AssetLibrary","A missing read-only root does not move the primary root");
  fs::rename(away,durable);
  require(EngineAssets::exists(AssetKind::Blob,atlasId)&&EngineAssets::root_problems().empty(),"Reconnecting the root makes its assets available again without a restart");
 }
 {
  EngineAssets::ScopedRegistry fresh(registryPath);
  const auto good=read_file(registryPath);
  auto broken=[&](const std::string& content,const std::string& part,const char* name){
   atomic_write(registryPath,content);
   const bool listed=has(text_of([&]{(void)EngineAssets::registered_roots();}),part);
   const bool resolving=refused([&]{(void)EngineAssets::root();});
   const bool reported=has(EngineAssets::describe_roots(),part);
   const bool loading=refused([&]{(void)EngineAssets::get(AssetKind::Blob,atlasId);});
   require(listed&&resolving&&reported&&loading,name);
  };
  broken("{ broken","cannot be read","A registry that is not JSON is reported and stops resolution instead of being ignored");
  broken(R"({"format":"other","version":1,"roots":[]})","format","A registry with the wrong format is refused");
  broken(R"({"format":"starlight-asset-roots","version":2,"roots":[]})","version 2","A registry with an unsupported version is refused");
  broken(R"({"format":"starlight-asset-roots","version":1,"roots":[{"path":"relative","role":"read-only"}]})","not absolute","A registry entry with a relative path is refused");
  broken(R"({"format":"starlight-asset-roots","version":1,"roots":[{"path":"C:\\x","role":"maybe"}]})","role","A registry entry with an unknown role is refused");
  broken(R"({"format":"starlight-asset-roots","version":1,"roots":[{"path":"C:\\x","role":"writable"},{"path":"C:\\y","role":"writable"}]})","more than one writable","A registry with two writable roots is refused");
  broken(R"({"format":"starlight-asset-roots","version":1,"roots":[{"path":"C:\\x","role":"read-only"},{"path":"c:\\X\\","role":"read-only"}]})","twice","A registry that lists a folder twice is refused");
  atomic_write(registryPath,good);
  require(EngineAssets::registered_roots().size()==1&&EngineAssets::exists(AssetKind::Blob,atlasId),"Restoring the registry restores resolution");
 }

 {
  const auto blobs=work/"blob-library";
  EngineAssets::ScopedRoot library(blobs);
  EngineAssets::drop_memory_cache();
  const auto atlas=asset_checks_atlas(4);
  const auto id=import_blob_bytes(UvAtlasType,atlas);
  const auto blob=load_blob(id);
  require(EngineAssets::valid_id(AssetKind::Blob,id)&&id.starts_with("blob-")&&blob.type==UvAtlasType&&blob.version==1&&blob.data==atlas,"A blob carries its declared type and version and returns exactly the authored bytes");
  require(reinterpret_cast<uintptr_t>(blob.data.data())%8==0,"Blob data is 8-byte aligned for direct float access");
  require(import_blob_bytes(UvAtlasType,atlas)==id&&files_in(blobs,".slasset")==1,"The same blob imported twice is one asset");
  atomic_write(work/"atlas.bin",atlas);
  require(import_blob(work/"atlas.bin",UvAtlasType)==id,"A blob imported from a file and from bytes has one id");
  require(!EngineAssets::exists(AssetKind::Model,id)&&!EngineAssets::exists(AssetKind::Texture,id)&&!EngineAssets::exists(AssetKind::Blob,"model-"+id.substr(5)),"A blob id is never accepted as another kind of asset");
  register_blob_type("test-data/raw/v1",[](std::string_view){});
  register_blob_type("test-data/raw/v2",[](std::string_view){});
  register_blob_type("test-data/big/v7",[](std::string_view){});
  const auto rawOne=import_blob_bytes("test-data/raw/v1",atlas),rawTwo=import_blob_bytes("test-data/raw/v2",atlas);
  require(rawOne!=id&&rawOne!=rawTwo&&import_blob_bytes("test-data/raw/v1",atlas)==rawOne,"The declared type is part of the identity: identical bytes under another type are another asset");
  require(load_blob(rawTwo).version==2&&blob_type_known(UvAtlasType)&&!blob_type_known("mystery/raw/v1"),"The version comes from the explicit type name and validators are registered per type");
  const size_t before=files_in(blobs,".slasset");
  for(const auto* name:{"","uv-atlas","uv-atlas/float32x2","uv-atlas/float32x2/v0","UV/float32x2/v1","uv-atlas//v1","uv-atlas/float32x2/v1 ","uv atlas/x/v1","mystery/raw/v1"})
   require(refused([&]{(void)import_blob_bytes(name,atlas);}),"A blob type must be well formed, versioned and registered");
  require(refused([&]{(void)import_blob_bytes(UvAtlasType,std::string());})&&refused([&]{(void)import_blob_bytes(UvAtlasType,std::string(7,'\0'));})&&refused([&]{(void)import_blob_bytes(UvAtlasType,std::string(12,'\0'));}),"A UV atlas whose size is not a whole number of float pairs is refused");
  auto with=[&](float u,float v){std::string bytes=asset_checks_atlas(2);std::memcpy(bytes.data()+8,&u,4);std::memcpy(bytes.data()+12,&v,4);return bytes;};
  const float nan=std::numeric_limits<float>::quiet_NaN(),infinity=std::numeric_limits<float>::infinity();
  require(refused([&]{(void)import_blob_bytes(UvAtlasType,with(nan,.5f));})&&refused([&]{(void)import_blob_bytes(UvAtlasType,with(.5f,infinity));}),"A UV atlas holding NaN or infinity is refused");
  require(refused([&]{(void)import_blob_bytes(UvAtlasType,with(1.5f,.5f));})&&refused([&]{(void)import_blob_bytes(UvAtlasType,with(.5f,-.25f));})&&!refused([&]{(void)import_blob_bytes(UvAtlasType,with(0.f,1.f));}),"UV coordinates outside 0..1 are refused and the boundary values are accepted");
  require(refused([&]{(void)import_blob_bytes("test-data/big/v7",std::string(MaxBlobBytes+1,'x'));})&&files_in(blobs,".slasset")==before+1,"A blob over the size limit is refused and the refusals stored nothing");
  require(files_in(blobs,".tmp")==0&&files_in(blobs,".lock")==0,"Writing blobs leaves no temporary files");
  const auto junk=EngineAssets::put(AssetKind::Blob,"not an envelope at all but long enough to parse");
  require(refused([&]{(void)load_blob(junk);})&&!check_asset_ready(junk).ready,"A stored blob with a damaged envelope is refused when loaded and is not ready");
  require(check_asset_ready(id).ready&&has(check_asset_ready(id).detail,UvAtlasType),"A valid blob is ready and readiness names its type");

  auto bytes=read_file(EngineAssets::file_for(id));bytes[bytes.size()-3]^=0x55;atomic_write(EngineAssets::file_for(id),bytes);
  EngineAssets::drop_memory_cache();
  require(refused([&]{(void)load_blob(id);})&&refused([&]{EngineAssets::verify_on_disk(id);})&&!check_asset_ready(id).ready,"A damaged blob file is detected when it is read back and is not ready");
  EngineAssets::drop_memory_cache();
  {
   EngineAssets::ScopedRoot reopened(blobs);
   require(import_blob_bytes(UvAtlasType,atlas)==id&&check_asset_ready(id).ready,"Importing the blob again repairs the damaged file");
  }

  const auto glb=asset_checks_uv_glb(false),mixed=asset_checks_uv_glb(true);
  const auto uvModel=import_model_bytes(glb),mixedModel=import_model_bytes(mixed),plainModel=import_model(solid_glb_fixture(work));
  require(*load_model_bytes(uvModel)==glb&&model_uv_vertex_count(glb)==4&&model_uv_vertex_count(mixed)==4,"The model bytes are available in memory exactly as authored and the UV-bearing vertices are counted");
  size_t parsedMixed=0;for(const auto& part:*load_model(mixedModel))parsedMixed+=part.mesh.vertices.size();
  require(parsedMixed==8,"The mixed model parses to more vertices than carry texture coordinates");
  const auto atlas3=import_blob_bytes(UvAtlasType,asset_checks_atlas(3)),atlas5=import_blob_bytes(UvAtlasType,asset_checks_atlas(5)),atlas8=import_blob_bytes(UvAtlasType,asset_checks_atlas(8));
  require(validate_uv_atlas_for_model(id,uvModel)==4,"An atlas with exactly the model's vertex count is accepted for that model");
  const auto fewer=text_of([&]{(void)validate_uv_atlas_for_model(atlas3,uvModel);}),more=text_of([&]{(void)validate_uv_atlas_for_model(atlas5,uvModel);});
  require(has(fewer,"3")&&has(fewer,"4")&&has(more,"5")&&has(more,"4"),"An atlas with fewer or more vertices than the model is rejected with both counts");
  require(validate_uv_atlas_for_model(id,mixedModel)==4&&refused([&]{(void)validate_uv_atlas_for_model(atlas8,mixedModel);}),"The atlas must match the vertices that carry texture coordinates, not the primitives without them");
  require(has(text_of([&]{(void)validate_uv_atlas_for_model(id,plainModel);}),"texture coordinates"),"A model without texture coordinates cannot have a UV atlas");
  require(has(text_of([&]{(void)validate_uv_atlas_for_model(rawOne,uvModel);}),"not a UV atlas"),"A blob of another type is not accepted as a UV atlas");
  bool missingModel=false;try{(void)validate_uv_atlas_for_model(id,"model-"+std::string(64,'0'));}catch(const AssetMissing&){missingModel=true;}
  require(missingModel&&refused([&]{(void)validate_uv_atlas_for_model(uvModel,uvModel);}),"A missing model or an id that is not a blob is rejected");
  require(check_asset_ready(uvModel).ready&&has(check_asset_ready(uvModel).detail,"parsed")&&!check_asset_ready("model-"+std::string(64,'0')).ready,"Model readiness parses the model, and an absent model is not ready");
  auto damagedModel=read_file(EngineAssets::file_for(uvModel));damagedModel[damagedModel.size()-5]^=0x21;atomic_write(EngineAssets::file_for(uvModel),damagedModel);
  EngineAssets::drop_memory_cache();model_service_trim();
  require(refused([&]{(void)load_model_bytes(uvModel);})&&refused([&]{(void)load_model(uvModel);})&&!check_asset_ready(uvModel).ready,"A damaged model file is detected by the in-memory accessors and readiness");
  EngineAssets::drop_memory_cache();
 }

 {
  EngineAssets::ScopedRoot library(work/"decode-library");
  require(texture_decoder_available(),"The Windows imaging component can decode images in this headless process");
  const unsigned char pngBytes[]={0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x02,0x00,0x00,0x00,0x02,0x08,0x06,0x00,0x00,0x00,0x72,0xb6,0x0d,0x24,0x00,0x00,0x00,0x14,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xcf,0xf0,0xff,0x3f,0x83,0xc3,0xff,0xff,0x0c,0x30,0x06,0x00,0x5c,0x79,0x0a,0x77,0x46,0x16,0xfa,0x08,0x00,0x00,0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
  const std::string png(reinterpret_cast<const char*>(pngBytes),sizeof(pngBytes));
  const auto decoded=decode_texture_bytes(png);
  require(decoded.width==2&&decoded.height==2&&decoded.container=="png"&&decoded.frames==1&&decoded.decodedBytes==16,"A PNG is really decoded: size, format and pixel bytes come from the decoder");
  const auto jpeg=asset_checks_jpeg();
  require(!jpeg.empty(),"A test JPEG can be produced");
  const auto jpegDecoded=decode_texture_bytes(jpeg);
  require(jpegDecoded.width==16&&jpegDecoded.height==16&&jpegDecoded.container=="jpeg"&&jpegDecoded.decodedBytes==16*16*4,"A JPEG is really decoded as well");
  const unsigned char stubBytes[]={0xff,0xd8,0xff,0xe0,0x00,0x10,0x4a,0x46,0x49,0x46,0x00,0x01,0x01,0x00,0x00,0x01,0x00,0x01,0x00,0x00,0xff,0xc0,0x00,0x11,0x08,0x00,0x02,0x00,0x03,0x03,0x01,0x22,0x00,0x02,0x11,0x01,0x03,0x11,0x01,0xff,0xda,0x00,0x0c,0x03,0x01,0x00,0x02,0x11,0x03,0x11,0x00,0x3f,0x00,0x00,0x00,0xff,0xd9};
  const std::string stub(reinterpret_cast<const char*>(stubBytes),sizeof(stubBytes));
  require(describe_texture(stub).width==3&&refused([&]{(void)decode_texture_bytes(stub);}),"A JPEG whose header is valid but whose pixel data is missing passes the header check and fails decoding");
  auto damagedPng=png;for(size_t i=41;i<53;++i)damagedPng[i]^=char(0xa5);
  require(describe_texture(damagedPng).width==2&&refused([&]{(void)decode_texture_bytes(damagedPng);}),"A PNG with a damaged pixel stream passes the header check and fails decoding");
  const auto plain=asset_checks_png(8,6);
  const auto plainDecoded=decode_texture_bytes(plain);
  require(plainDecoded.width==8&&plainDecoded.height==6&&plainDecoded.pixelStreamBytes==6*(1+8*4)&&plainDecoded.decodedBytes==8*6*4,"A well formed PNG passes the strict pixel stream check and the decoder");
  const auto shortRows=asset_checks_png(8,6,[](std::string& raw){raw.resize(raw.size()/2);});
  const auto longRows=asset_checks_png(8,6,[](std::string& raw){raw.append(40,'\0');});
  const auto badFilter=asset_checks_png(8,6,[](std::string& raw){raw[0]=9;});
  auto badChunk=plain;badChunk[plain.size()-20]^=0x01;
  const auto badAdler=asset_checks_png(8,6,{},true);
  require(refused([&]{(void)validate_png_stream(shortRows);})&&refused([&]{(void)validate_png_stream(longRows);})&&refused([&]{(void)validate_png_stream(badFilter);}),"A PNG with too little or too much pixel data, or an invalid row filter, is rejected");
  require(refused([&]{(void)validate_png_stream(badChunk);})&&refused([&]{(void)validate_png_stream(badAdler);}),"A PNG with a bad chunk checksum or a bad pixel stream checksum is rejected");
  require(describe_texture(shortRows).width==8&&describe_texture(badFilter).width==8&&!check_asset_ready(EngineAssets::put(AssetKind::Texture,shortRows)).ready&&!check_asset_ready(EngineAssets::put(AssetKind::Texture,badFilter)).ready,"Damage the header check cannot see makes an already stored texture not ready");
  require(refused([&]{(void)import_texture_bytes(shortRows);})&&refused([&]{(void)import_texture_bytes(badFilter);})&&refused([&]{(void)import_texture_bytes(badAdler);})&&refused([&]{(void)import_texture_bytes(badChunk);}),"Import runs the strict integrity check, so damaged PNGs are refused before they are stored");
  require(has(text_of([&]{(void)import_texture_bytes(badChunk);}),"integrity check")&&has(text_of([&]{(void)import_texture_bytes(badChunk);}),"at byte"),"The import refusal says it is the integrity check and names the chunk position");
  auto mismatched=png;mismatched[19]=3;
  require(refused([&]{(void)decode_texture_bytes(mismatched);}),"A PNG whose header size disagrees with the pixel data is not ready");
  const auto textureId=import_texture_bytes(png),jpegId=import_texture_bytes(jpeg),damagedId=EngineAssets::put(AssetKind::Texture,damagedPng);
  require(refused([&]{(void)import_texture_bytes(damagedPng);}),"A PNG with a damaged pixel stream cannot be imported");
  require(check_asset_ready(textureId).ready&&check_asset_ready(jpegId).ready&&has(check_asset_ready(textureId).detail,"2x2"),"Texture readiness decodes the stored texture");
  require(!check_asset_ready(damagedId).ready&&decode_texture(textureId).width==2,"Readiness fails for a texture the header check accepted but the decoder cannot read");
  auto stored=read_file(EngineAssets::file_for(textureId));stored[stored.size()-9]^=0x33;atomic_write(EngineAssets::file_for(textureId),stored);
  EngineAssets::drop_memory_cache();
  require(refused([&]{(void)load_texture(textureId);})&&refused([&]{(void)decode_texture(textureId);})&&!check_asset_ready(textureId).ready,"A damaged texture file is detected by the in-memory accessor, the decoder and readiness");
  {
   auto be=[](std::string& out,uint32_t v){for(int s=24;s>=0;s-=8)out.push_back(char((v>>s)&255));};
   auto chunk=[&](std::string& out,const std::string& type,const std::string& body,bool flip=false){be(out,uint32_t(body.size()));const std::string typed=type+body;out+=typed;auto crc=crc32_bytes(typed);if(flip)crc^=1;be(out,crc);};
   auto zeros=[&](uint64_t total){
    std::string z("\x78\x01",2),out;uint32_t acc=0;int used=0;
    auto bit=[&](uint32_t v){acc|=(v&1u)<<used;if(++used==8){out.push_back(char(acc));acc=0;used=0;}};
    auto code=[&](uint32_t v,int len){for(int i=len-1;i>=0;--i)bit(v>>i);};
    bit(1);bit(1);bit(0);
    uint64_t left=total;
    while(left>=259){code(0x30,8);code(0xC5,8);code(0,5);left-=259;}
    while(left){code(0x30,8);--left;}
    code(0,7);if(used)out.push_back(char(acc));
    z+=out;be(z,uint32_t((total%65521u)<<16)|1u);return z;
   };
   auto made=[&](uint32_t width,uint32_t height,int depth,int colour,const std::string& idat,const std::vector<std::pair<std::string,std::string>>& before={},const std::string& tail={}){
    std::string header;be(header,width);be(header,height);header.push_back(char(depth));header.push_back(char(colour));header+=std::string("\x00\x00\x00",3);
    std::string out("\x89PNG\r\n\x1a\n",8);chunk(out,"IHDR",header);
    for(const auto& [type,body]:before)chunk(out,type,body,type=="BADC");
    chunk(out,"IDAT",idat);chunk(out,"IEND","");return out+tail;
   };
   auto rejected=[&](const std::string& bytes){try{(void)validate_png_stream(bytes);}catch(const std::exception&){return true;}return false;};
   auto reason=[&](const std::string& bytes){try{(void)validate_png_stream(bytes);}catch(const std::exception& e){return std::string(e.what());}return std::string();};
   const auto bigZero=made(2048,2048,8,0,zeros(2048ull*2049ull));
   const auto bigDecoded=decode_texture_bytes(bigZero);
   require(validate_png_stream(bigZero)==2048ull*2049ull&&bigDecoded.width==2048&&bigDecoded.pixelStreamBytes==2048ull*2049ull&&bigDecoded.decodedBytes==2048ull*2048ull*4,"A highly compressed valid large PNG is fully inflated and checksummed, then decoded to the last row");
   require(rejected(made(2,2,8,6,zeros(1+258ull*4000ull))),"A tiny image whose zlib stream inflates to a megabyte is rejected at the size the header declares");
   const auto hugeDeclared=made(16000,16000,8,6,zeros(1+258ull*10ull));
   std::cout<<"  16000x16000 RGBA header over a tiny stream: "<<reason(hugeDeclared)<<'\n';
   require(reason(hugeDeclared).find("can hold")!=std::string::npos&&!check_asset_ready(EngineAssets::put(AssetKind::Texture,hugeDeclared)).ready&&refused([&]{(void)import_texture_bytes(hugeDeclared);}),"A header declaring about 1 GB of pixels over a tiny stream is rejected by the size check before any inflation or allocation, at import and at readiness");
   require(reason(made(16384,16384,8,6,zeros(1000))).find("1 GiB")!=std::string::npos,"A header whose pixel stream exceeds 1 GiB is rejected");
   const auto wide=made(20000,1,8,0,zeros(20001));
   require(refused([&]{(void)describe_texture(wide);})&&refused([&]{(void)import_texture_bytes(wide);}),"A 20000 pixel wide PNG is refused at import: import and runtime share one limit");
   const auto wideText=text_of([&]{(void)import_texture_bytes(wide);});
   std::cout<<"  oversize import refusal: "<<wideText<<'\n';
   require(has(wideText,"20000x1")&&has(wideText,"16384")&&has(wideText,"never downsamples")&&has(wideText,"graphics device"),"The oversize refusal names the size, the limit, the no-silent-downsample rule and that device limits are checked later");
   const auto wideId=EngineAssets::put(AssetKind::Texture,wide);
   require(refused([&]{(void)decode_texture_bytes(wide);})&&!check_asset_ready(wideId).ready&&has(check_asset_ready(wideId).detail,"16384"),"A PNG wider than the limit that is already stored is not ready, and the reason names the limit");
   const auto edge=made(16384,1,8,0,zeros(16385));
   require(describe_texture(edge).width==16384&&!refused([&]{(void)import_texture_bytes(edge);})&&check_asset_ready(import_texture_bytes(edge)).ready,"A 16384 pixel wide PNG is accepted at import and is ready: the two limits are the same number");
   require(refused([&]{(void)import_texture_bytes(made(16385,1,8,0,zeros(16386)));})&&refused([&]{(void)describe_texture(made(1,16385,8,0,zeros(16386)));}),"One pixel above the limit is refused on either side");
   require(MaxRuntimeTextureSide==starlight::texture_limits::MaxSide&&MaxPngPixelStreamBytes==starlight::texture_limits::MaxPngPixelStreamBytes,"The editor constants are the central limits");
   starlight::texture_limits::setDecodedBudget(100);
   const auto budgetText=text_of([&]{(void)import_texture_bytes(plain);});
   const bool budgetDecode=refused([&]{(void)decode_texture_bytes(plain);});
   const bool budgetReady=!check_asset_ready(EngineAssets::put(AssetKind::Texture,plain)).ready;
   starlight::texture_limits::setDecodedBudget(0);
   require(has(budgetText,"budget")&&has(budgetText,"never downsamples")&&budgetDecode&&budgetReady,"A configurable decoded-memory budget is enforced at import, in the decoder and in readiness, with an explanation");
   require(check_asset_ready(import_texture_bytes(plain)).ready&&EngineAssets::cache_folder().filename()==starlight::texture_limits::CacheRootFolder,"With the default budget restored the same PNG imports and is ready; derived data keeps the one cache folder name");
   require(has(text_of([&]{(void)validate_png_stream(plain+"junk");}),"after its end marker, starting at byte")&&has(text_of([&]{(void)import_texture_bytes(plain+"junk");}),"bytes after the end marker"),"Trailing bytes are refused with their position and the reason, at import and in the stream check");
   require(has(text_of([&]{(void)validate_png_stream(made(4,4,8,0,std::string(),{{"ABCD","x"}}));}),"ABCD"),"An unknown critical chunk is named in the refusal");
   {
    (void)CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    struct Variant{const char* name;GUID format;UINT bytes;bool interlace;};
    const std::vector<Variant> variants={{"8-bit gray",GUID_WICPixelFormat8bppGray,1,false},{"24-bit BGR",GUID_WICPixelFormat24bppBGR,3,false},{"32-bit BGRA",GUID_WICPixelFormat32bppBGRA,4,false},{"16-bit gray",GUID_WICPixelFormat16bppGray,2,false},{"64-bit RGBA",GUID_WICPixelFormat64bppRGBA,8,false},{"32-bit BGRA interlaced",GUID_WICPixelFormat32bppBGRA,4,true},{"24-bit BGR interlaced",GUID_WICPixelFormat24bppBGR,3,true}};
    size_t produced=0;
    for(const auto& v:variants){
     using Microsoft::WRL::ComPtr;
     const UINT W=37,H=21;
     ComPtr<IWICImagingFactory> factory;ComPtr<IStream> stream;ComPtr<IWICBitmapEncoder> encoder;ComPtr<IWICBitmapFrameEncode> frame;ComPtr<IPropertyBag2> bag;
     if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)))||FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))||FAILED(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder))||FAILED(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache))||FAILED(encoder->CreateNewFrame(&frame,&bag)))continue;
     if(v.interlace){PROPBAG2 option{};option.pstrName=const_cast<LPOLESTR>(L"InterlaceOption");VARIANT value{};value.vt=VT_BOOL;value.boolVal=VARIANT_TRUE;if(FAILED(bag->Write(1,&option,&value)))continue;}
     WICPixelFormatGUID format=v.format;std::vector<BYTE> pixels(size_t(W)*H*v.bytes);
     for(size_t i=0;i<pixels.size();++i)pixels[i]=BYTE(i*7+i/W);
     if(FAILED(frame->Initialize(bag.Get()))||FAILED(frame->SetSize(W,H))||FAILED(frame->SetPixelFormat(&format))||format!=v.format||FAILED(frame->WritePixels(H,W*v.bytes,UINT(pixels.size()),pixels.data()))||FAILED(frame->Commit())||FAILED(encoder->Commit()))continue;
     STATSTG statistics{};stream->Stat(&statistics,STATFLAG_NONAME);LARGE_INTEGER start{};stream->Seek(start,STREAM_SEEK_SET,nullptr);
     std::string encoded(size_t(statistics.cbSize.QuadPart),'\0');ULONG read=0;stream->Read(encoded.data(),ULONG(encoded.size()),&read);encoded.resize(read);
     ++produced;
     std::string id;bool imported=true;try{id=import_texture_bytes(encoded);}catch(const std::exception& e){imported=false;std::cout<<"  re-encode variant "<<v.name<<" refused: "<<e.what()<<'\n';}
     require(imported&&check_asset_ready(id).ready&&*load_texture(id)==encoded&&decode_texture_bytes(encoded).width==W,(std::string("Re-encode check: a PNG written by the Windows encoder (")+v.name+") imports, reads back byte for byte and is ready").c_str());
    }
    require(produced>=5,"Re-encode check: at least five encoder variants were produced");
   }
   require(rejected(plain+"junk")&&rejected(plain+std::string(12,'\0')),"Bytes after the end marker are rejected");
   std::string gray;for(int y=0;y<4;++y){gray.push_back(0);gray.append(4,char(y*17));}
   const auto grayZlib=[&]{uint32_t a=1,b=0;for(const unsigned char c:gray){a=(a+c)%65521;b=(b+a)%65521;}std::string z("\x78\x01\x01\x14\x00\xeb\xff",7);z+=gray;be(z,(b<<16)|a);return z;}();
   require(!rejected(made(4,4,8,0,grayZlib))&&!rejected(made(4,4,8,0,grayZlib,{{"tRNS",std::string("\x00\x09",2)},{"tEXt",std::string("a\0b",3)},{"prVt","xyz"}})),"A gray PNG with a valid transparency chunk and harmless ancillary chunks passes");
   require(rejected(made(4,4,8,0,grayZlib,{{"BADC","xyz"}})),"A wrong CRC in an ancillary chunk is rejected");
   require(rejected(made(4,4,8,0,grayZlib,{{"tRNS","abcd"}}))&&rejected(made(4,4,8,0,grayZlib,{{"tRNS",std::string("\x00\x09",2)},{"tRNS",std::string("\x00\x09",2)}})),"A transparency chunk of the wrong size or repeated is rejected");
   require(rejected(made(4,4,8,0,grayZlib,{{"PLTE",std::string("\0\0\0",3)}}))&&rejected(made(4,4,8,3,grayZlib,{{"PLTE",std::string("\0\0\0",3)},{"PLTE",std::string("\0\0\0",3)}}))&&rejected(made(4,4,8,3,grayZlib)),"A palette in a gray image, a second palette, or a missing palette is rejected");
   require(rejected(made(4,4,4,2,grayZlib))&&rejected(made(4,4,16,3,grayZlib,{{"PLTE",std::string("\0\0\0",3)}})),"A bit depth that is invalid for the colour type is rejected");
   require(rejected(made(4,4,8,0,grayZlib,{{"ABCD","x"}})),"An unknown critical chunk is rejected");
   require(rejected(made(4,4,8,0,grayZlib+std::string(3,'\0'))),"Bytes after the end of the zlib stream are rejected");
   size_t accepted=0;std::string survivors;
   for(size_t i=0;i<plain.size();++i){auto flipped=plain;flipped[i]^=1;if(!rejected(flipped)){++accepted;survivors+=" "+std::to_string(i);}}
   std::cout<<"  single-bit sweep over "<<plain.size()<<" PNG bytes in the strict validator: "<<accepted<<" accepted"<<survivors<<"\n";
   require(accepted==0,"Every single-bit flip anywhere in a PNG is detected by the strict validator");
   size_t cutAccepted=0;for(size_t cut=0;cut<plain.size();++cut)if(!rejected(plain.substr(0,cut)))++cutAccepted;
   require(cutAccepted==0,"Every truncation of a PNG is rejected by the strict validator");
  }
  {
   const auto jpeg=asset_checks_jpeg();
   require(!jpeg.empty()&&decode_texture_bytes(jpeg).container=="jpeg","A test JPEG decodes");
   require(!refused([&]{validate_jpeg_stream(jpeg);}),"The JPEG structure check accepts a JPEG written by the Windows encoder");
   size_t cutAccepted=0;
   for(size_t cut=0;cut<jpeg.size();++cut){const std::string part=jpeg.substr(0,cut);bool ok=true;try{(void)decode_texture_bytes(part);}catch(const std::exception&){ok=false;}if(ok)++cutAccepted;}
   require(cutAccepted==0,"Every truncation of a JPEG is rejected");
   const size_t sos=jpeg.find("\xff\xda",2);
   require(sos!=std::string::npos,"The test JPEG has a scan");
   const size_t entropy=sos+2+((size_t(uint8_t(jpeg[sos+2]))<<8)|uint8_t(jpeg[sos+3]));
   auto stray=jpeg;stray[entropy+1]=char(0xff);stray[entropy+2]=char(0xc0);
   auto restarts=jpeg;restarts.insert(entropy+1,std::string("\xff\xd3\xff\xd5",4));
   auto trailing=jpeg;trailing+="junk";
   require(refused([&]{validate_jpeg_stream(stray);})&&refused([&]{validate_jpeg_stream(restarts);})&&refused([&]{validate_jpeg_stream(trailing);}),"A stray marker inside the image data, restart markers out of sequence, and bytes after the end marker are rejected");
   size_t flips=0,flipAccepted=0;
   for(size_t i=entropy+1;i+2<jpeg.size();++i){auto flipped=jpeg;flipped[i]^=0x10;++flips;try{(void)decode_texture_bytes(flipped);++flipAccepted;}catch(const std::exception&){}}
   std::cout<<"  JPEG image-data damage: "<<flipAccepted<<" of "<<flips<<" single-byte changes inside the scan were still accepted (the decoder cannot tell a changed coefficient from a real one)\n";
  }
 }

 {
  const auto cli=work/"cli";fs::create_directories(cli);
  EngineAssets::ScopedRegistry fresh(cli/"asset-roots.json");
  auto run=[&](std::vector<std::wstring> args){
   const auto result=cli/"result.json";fs::remove(result);
   args.insert(args.begin(),L"Starlight.NativeCommands");args.push_back(L"--result");args.push_back(result.wstring());
   std::vector<wchar_t*> argv;for(auto& a:args)argv.push_back(a.data());
   std::ostringstream captured;auto* old=std::cout.rdbuf(captured.rdbuf());
   const int code=run_commands(int(argv.size()),argv.data());
   std::cout.rdbuf(old);
   auto report=json::parse(read_file(result));report["exit"]=code;return report;
  };
  const auto store=cli/"store";
  auto added=run({L"--asset-root-init",store.wstring(),L"--label",L"cli store"});
  require(added.at("success")==true&&fs::is_regular_file(store/"library.json")&&EngineAssets::registered_roots().size()==1,"The command line creates and registers a new asset root");
  auto listed=run({L"--asset-root-list"});
  require(listed.at("success")==true&&listed.at("healthy")==true&&listed.at("roots").size()==2&&has(listed.at("text").get<std::string>(),"cli store"),"The command line lists the roots in resolution order");
  auto refusedAdd=run({L"--asset-root-add",(cli/"no-marker").wstring()});
  require(refusedAdd.at("success")==false&&refusedAdd.at("exit")==1&&has(refusedAdd.at("error").get<std::string>(),"marker"),"The command line refuses to register a folder without a marker");
  const auto atlasFile=cli/"atlas.bin";atomic_write(atlasFile,asset_checks_atlas(4));
  auto imported=run({L"--import-blob",L"uv-atlas/float32x2/v1",atlasFile.wstring()});
  const auto blobId=imported.at("asset").get<std::string>();
  auto ready=run({L"--asset-ready",wide(blobId)});
  require(imported.at("success")==true&&ready.at("success")==true&&run({L"--asset-ready",L"blob-"+std::wstring(64,L'0')}).at("exit")==1,"The command line imports a blob and reports readiness, failing for an absent asset");
  auto removed=run({L"--asset-root-remove",store.wstring()});
  require(removed.at("success")==true&&EngineAssets::registered_roots().empty()&&fs::is_regular_file(store/"library.json")&&run({L"--asset-root-remove",store.wstring()}).at("success")==false,"The command line unregisters a root without touching its files and reports an unknown root");
 }
 {
  // A model file may nest JSON arbitrarily deep; the URI check must not recurse, or a small crafted file overflows the stack.
  std::ifstream fixture(fs::absolute("migration/fixtures/triangle.glb"),std::ios::binary);
  const std::string glb((std::istreambuf_iterator<char>(fixture)),std::istreambuf_iterator<char>());
  auto u32=[&](const std::string& text,size_t at){uint32_t value;std::memcpy(&value,text.data()+at,4);return value;};
  auto put32=[&](std::string& text,size_t at,uint32_t value){std::memcpy(&text[at],&value,4);};
  auto with_extras=[&](const std::string& extras){
   const uint32_t jsonLength=u32(glb,12);std::string json=glb.substr(20,jsonLength);json.insert(json.rfind('}'),",\"extras\":"+extras);
   while(json.size()%4)json.push_back(' ');
   std::string out=glb.substr(0,12),header(8,'\0');put32(header,0,uint32_t(json.size()));std::memcpy(&header[4],glb.data()+16,4);
   out+=header+json+glb.substr(20+jsonLength);put32(out,8,uint32_t(out.size()));return out;
  };
  require(glb.size()>40,"The triangle fixture is readable");
  bool plain=true;try{validate_model_bytes(glb);}catch(const std::exception&){plain=false;}
  require(plain,"CC-01: An ordinary embedded model validates");
  int deepOutcome=0;
  try{validate_model_bytes(with_extras(std::string(300000,'[')+std::string(300000,']')));deepOutcome=1;}
  catch(const std::runtime_error&){deepOutcome=2;}
  catch(...){deepOutcome=3;}
  require(deepOutcome==1||deepOutcome==2,"CC-01: A model with 300000 levels of nested JSON is handled without exhausting the stack");
  bool refused=false;try{validate_model_bytes(with_extras("{\"a\":[{\"uri\":\"http://example.com/x.bin\"}]}"));}catch(const std::runtime_error&){refused=true;}
  require(refused,"CC-01: A nested external URI is still refused");
  bool refusedAfterDepth=false;
  try{validate_model_bytes(with_extras(std::string(300000,'[')+"{\"uri\":\"file.bin\"}"+std::string(300000,']')));}catch(const std::runtime_error&){refusedAfterDepth=true;}
  require(refusedAfterDepth,"CC-01: An external URI buried 300000 levels deep is still refused");
 }
 require(files_in(dataFolder,".tmp")==0,"No temporary files are left in the data folder");
}
