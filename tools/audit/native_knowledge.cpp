#include "native_knowledge.hpp"
#include "native_feature_evidence.hpp"
#include <iostream>
using namespace starlight::audit;
int wmain(int argc,wchar_t** argv){try{
    if(argc!=4)throw std::runtime_error("Usage: NativeKnowledge input.json knowledge-rules.json new-output.json");
    auto output=fs::absolute(argv[3]);auto graphFile=output.parent_path()/"feature-status.json",briefingFile=output.parent_path()/"GENERATED-FEATURE-BRIEFING.md";
    if(fs::exists(output) || fs::exists(graphFile) || fs::exists(briefingFile))throw std::runtime_error("Knowledge output already exists");auto input=json::parse(read_file(argv[1]));auto model=knowledge_model_native(input,json::parse(read_file(argv[2])));
    auto graph=connect_features_native(model,input.at("index"),input.at("evidence"),input.value("inventory",json::array()));atomic_write(graphFile,graph.dump(2));atomic_write(briefingFile,feature_briefing_native(graph));
    atomic_write(output,model.dump(2));std::cout<<model["features"].size()<<" feature interpretations\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
