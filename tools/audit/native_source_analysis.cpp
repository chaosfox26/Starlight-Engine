#include "native_source_analysis.hpp"
#include <iostream>
using namespace starlight::audit;
int wmain(int argc,wchar_t** argv){try{
    if(argc!=5)throw std::runtime_error("Usage: NativeSourceAnalysis root inventory.json source-rules.json new-output-directory");
    auto root=fs::canonical(argv[1]);auto output=fs::absolute(argv[4]);if(fs::exists(output))throw std::runtime_error("Source report directory already exists");
    auto manifest=json::parse(read_file(argv[2]));auto rules=json::parse(read_file(argv[3]));auto result=analyze_sources(root,manifest.at("files"),rules);
    if(!fs::create_directory(output))throw std::runtime_error("Cannot create source report directory");
    atomic_write(output/"source-index.json",result.index.dump(2));atomic_write(output/"capabilities.json",result.capabilities.dump(2));atomic_write(output/"contracts-index.json",result.contracts.dump(2));
    atomic_write(output/"build-graph.json",result.build.dump(2));
    json sources=json::array();for(const auto& [path,text]:result.sources)sources.push_back({{"path",path},{"text",text}});atomic_write(output/"source-text.json",sources.dump());
    std::string evidence;for(const auto& entry:result.evidence)evidence+=entry.dump()+"\n";atomic_write(output/"source-evidence.jsonl",evidence);
    bool errors=false;for(const auto& entry:result.index)if(entry.contains("error") || (entry.contains("matches_inventory") && !entry["matches_inventory"].get<bool>()))errors=true;
    std::cout<<result.index.size()<<" source records; "<<result.evidence.size()<<" evidence records\n";return errors?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
