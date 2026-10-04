#include "native_evidence.hpp"
#include <iostream>
using namespace starlight::audit;
int wmain(int argc,wchar_t** argv){try{
    if(argc!=4)throw std::runtime_error("Usage: NativeEvidence root inventory.json new-output-directory");
    auto root=fs::canonical(argv[1]),output=fs::absolute(argv[3]);if(fs::exists(output))throw std::runtime_error("Evidence output already exists");
    auto result=analyze_evidence(root,json::parse(read_file(argv[2])).at("files"));if(!fs::create_directory(output))throw std::runtime_error("Cannot create evidence output");
    atomic_write(output/"binary-index.json",result.binaries.dump(2));atomic_write(output/"saved-evidence.json",result.saved.dump(2));atomic_write(output/"duplicates.json",result.duplicates.dump(2));
    std::cout<<result.binaries.size()<<" binaries; "<<result.saved.size()<<" saved evidence records\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
