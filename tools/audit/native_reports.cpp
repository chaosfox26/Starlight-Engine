#include "native_reports.hpp"
#include <iostream>
using namespace starlight::audit;
int wmain(int argc,wchar_t** argv){try{
 if(argc!=5)throw std::runtime_error("Usage: NativeReports root input.json templates.json new-output-directory");
 auto output=fs::absolute(argv[4]);if(fs::exists(output))throw std::runtime_error("Report output already exists");
 auto reports=reports_native(fs::absolute(argv[1]),Ordered::parse(read_file(argv[2])),json::parse(read_file(argv[3])));
 if(reports.empty())throw std::runtime_error("No reports requested");if(!fs::create_directory(output))throw std::runtime_error("Cannot create report output");for(const auto& [name,text]:reports)atomic_write(output/fs::u8path(name),text);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
