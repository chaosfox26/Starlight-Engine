#include "native_comparison.hpp"
#include <iostream>
using namespace starlight::audit;
int wmain(int argc,wchar_t** argv){try{
    if(argc!=5 && argc!=6)throw std::runtime_error("Usage: NativeComparison model.json graph.json rules.json new-output-directory [date]");
    auto output=fs::absolute(argv[4]);if(fs::exists(output))throw std::runtime_error("Comparison output already exists");auto now=utc_now();auto report=comparison_model_native(json::parse(read_file(argv[1])),json::parse(read_file(argv[2])),json::parse(read_file(argv[3])),argc==6?utf8(argv[5]):now.substr(0,10),now);
    if(!fs::create_directory(output))throw std::runtime_error("Cannot create comparison output");atomic_write(output/"engine-comparison.json",report.dump(2));atomic_write(output/"ENGINE-COMPARISON.md",comparison_markdown_native(report));return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
