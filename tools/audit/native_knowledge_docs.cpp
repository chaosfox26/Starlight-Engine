#include "native_knowledge_docs.hpp"
#include <iostream>
using namespace starlight::audit;
int wmain(int argc,wchar_t** argv){try{
    if(argc!=5 && argc!=6)throw std::runtime_error("Usage: NativeKnowledgeDocs root model.json templates.json new-output-directory [provenance.json]");
    auto root=fs::absolute(argv[1]),output=fs::absolute(argv[4]);if(fs::exists(output))throw std::runtime_error("Knowledge document output already exists");auto model=json::parse(read_file(argv[2]));std::string provenance;
    if(argc==6){auto ordered=nlohmann::ordered_json::parse(read_file(argv[5]));if(json(ordered)!=model.at("provenance"))throw std::runtime_error("Document provenance does not match model");provenance=ordered.dump(2);}
    auto documents=knowledge_documents_native(root,model,json::parse(read_file(argv[3])),provenance);
    if(!fs::create_directory(output))throw std::runtime_error("Cannot create knowledge output");for(const auto& [name,text]:documents)atomic_write(output/fs::u8path(name),text);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
