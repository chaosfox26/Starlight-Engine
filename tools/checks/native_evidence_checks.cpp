#include "../audit/native_evidence.hpp"
#include <fstream>
#include <iostream>
using namespace starlight::audit;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void put(const fs::path& path,const std::string& bytes){fs::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);stream.write(bytes.data(),bytes.size());stream.close();require(bool(stream),"Fixture write failed");}
int main(){try{
    auto root=fs::temp_directory_path()/(L"starlight-evidence-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));require(fs::create_directory(root),"Fixture already exists");
    put(root/"not-pe.exe","ordinary data");require(pe_summary_native(root/"not-pe.exe").is_null(),"Non-PE classified as PE");
    put(root/"truncated.exe","MZ");require(pe_summary_native(root/"truncated.exe").contains("error"),"Truncated PE not reported");
    std::string bytes(512,'\0');bytes.replace(0,2,"MZ");bytes[0x3c]=char(0x80);bytes.replace(0x80,4,std::string("PE\0\0",4));bytes[0x84]=0x64;bytes[0x85]=char(0x86);bytes[0x94]=char(0xf0);bytes[0x98]=0x0b;bytes[0x99]=2;
    put(root/"native.exe",bytes);auto pe=pe_summary_native(root/"native.exe");require(!pe.contains("error") && pe["pe32plus"]==true && pe["machine"]=="0x8664","Minimal native PE rejected");
    bytes[0x98+112+8]=1;bytes[0x98+112+12]=20;put(root/"bad-rva.exe",bytes);require(pe_summary_native(root/"bad-rva.exe").contains("error"),"Unmapped import RVA accepted");
    put(root/"audit-runs/run-test/good.json","{\"success\":false}");put(root/"audit-runs/run-test/bad.json","not json");put(root/"audit-runs/run-test/large.json",std::string(2*1024*1024+1,'x'));put(root/"unrelated.json","{}");
    json issues=json::array(),files=json::array();for(const auto& [path,stamp]:enumerate(root,issues))files.push_back(hash_entry(root,path,stamp));
    auto result=analyze_evidence(root,files);require(result.saved.size()==3,"Saved evidence scope incorrect");size_t errors=0;for(const auto& record:result.saved)if(record.contains("error"))++errors;require(errors==2,"Malformed/oversized evidence not reported");
    fs::remove_all(root);std::cout<<"PASS: non-PE, truncated headers, native x64, invalid RVA, malformed/oversized evidence\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"; fixtures retained\n";return 1;}}
