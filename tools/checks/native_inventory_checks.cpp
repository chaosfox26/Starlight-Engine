#include "../audit/native_inventory.hpp"
#include <fstream>
#include <iostream>
using namespace starlight::audit;
static void require(bool ok,const char* label){if(!ok)throw std::runtime_error(label);}
static void put(const fs::path& path,std::string_view bytes){
    fs::create_directories(long_path(path.parent_path()));
    std::ofstream out(long_path(path),std::ios::binary);out.write(bytes.data(),bytes.size());out.close();
    require(bool(out),"Fixture write failed");
}
int main(){fs::path root;try{
    root=fs::temp_directory_path()/(L"starlight-inventory-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    require(fs::create_directory(root),"Fixture directory already exists");
    put(root/"empty","");put(root/"abc","abc");
    require(digest(root/"empty").first=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","Empty hash wrong");
    require(digest(root/"abc").first=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","ABC hash wrong");
    require(digest(long_path(root/"abc")).first=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","Extended path hash wrong");
    put(root/"abc:note","secret");
    auto entry=hash_entry(root,"abc",file_stamp(root/"abc"));
    require(entry.at("stable")==true && entry.at("streams_stable")==true,"Stable file rejected");
    require(entry.at("streams").size()==1 && entry["streams"][0]["bytes_read"]==6,"Named stream omitted");
    auto stale=file_stamp(root/"abc");put(root/"abc","changed length");
    require(hash_entry(root,"abc",stale).at("stable")==false,"Changed file accepted");
    put(root/"analysis"/"excluded","x");put(root/"publication"/"excluded","x");put(root/"_quarantine"/"excluded","x");
    auto deep=root;for(int i=0;i<12;++i)deep/=L"long-path-fixture-segment";
    deep/=L"Unicode-\u2605.txt";put(deep,"long");
    json issues=json::array();auto files=enumerate(root,issues);
    require(issues.empty() && files.size()==3,"Inventory exclusions or long paths wrong");
    auto rel=utf8(deep.lexically_relative(root).generic_wstring());
    require(files.contains(rel) && hash_entry(root,rel,files.at(rel)).at("stable")==true,"Long Unicode file not hashed");
    require(category("tools/build/Build-Native.py")=="current source candidate","Build tooling misclassified");
    require(category("tools/toolchain/llvm/include/c++/v1/vector")=="third-party dependency","Bundled compiler headers misclassified as engine source");
    require(category("tools/toolchain/math/Inc/DirectXMath.h")=="third-party dependency","Bundled math headers misclassified as analysis tooling");
    require(category("out/Release/editor.exe")=="build/generated/binary artifacts","Build output misclassified");
    fs::remove_all(long_path(root));std::cout<<"PASS: known hashes, named streams, changed files, exclusions, long Unicode paths, categories\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"; fixture retained for investigation\n";return 1;}}
