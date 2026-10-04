#include "native_audit_records.hpp"
#include <iostream>
using namespace starlight::audit;
int wmain(int argc,wchar_t** argv){try{
 if(argc!=4)throw std::runtime_error("Usage: NativeAuditRecords capture|findings input.json new-output.json");auto output=fs::absolute(argv[3]);if(fs::exists(output))throw std::runtime_error("Evidence output already exists");auto input=json::parse(read_file(argv[2]));auto root=fs::canonical(fs::u8path(input.at("root").get<std::string>()));json result;
 auto mode=utf8(argv[1]);if(mode=="capture"){auto paths=input.contains("sources")?input.at("sources").get<std::vector<std::string>>():audit_source_paths(root);result=capture_feature_record(fs::absolute(fs::u8path(input.at("executable").get<std::string>())),root,paths,input.at("features"),input.value("outcome",std::string("unknown")),input.at("workload"),input.value("issues",json::array()));}else if(mode=="findings")result=collect_audit_findings(root,input.value("includeRetained",false));else throw std::runtime_error("Unknown record operation");atomic_write(output,result.dump(2));return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
