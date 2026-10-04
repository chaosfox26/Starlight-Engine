#include "native_inventory.hpp"
#include <iostream>
using namespace starlight::audit;
int wmain(int argc,wchar_t** argv){try{
    if(argc!=3)throw std::runtime_error("Usage: NativeInventory <scan root> <new report file>");
    auto root=fs::canonical(argv[1]);auto output=fs::absolute(argv[2]).lexically_normal();
    if(!fs::is_directory(root))throw std::runtime_error("Scan root must be a directory");
    auto parent=fs::canonical(output.parent_path());output=parent/output.filename();
    auto relative=output.lexically_relative(root);
    if(!relative.empty() && *relative.begin()!=L"..")throw std::runtime_error("Write this inventory report outside the scanned directory");
    if(fs::exists(output))throw std::runtime_error("Report already exists");
    json issues=json::array(),entries=json::array();auto before=enumerate(root,issues);size_t count=0;uint64_t bytes=0;
    for(const auto& [path,stamp]:before){auto item=hash_entry(root,path,stamp);
        if(item.contains("error") || !item.value("stable",false) || !item.value("streams_enumerated",false) || !item.value("streams_stable",false))
            issues.push_back({{"phase","hashing"},{"path",path},{"error","File or stream could not be verified as stable"}});
        for(const auto& stream:item.value("streams",json::array()))if(!stream.value("size_matches",false))issues.push_back({{"phase","stream"},{"path",path},{"error","Named stream size changed"}});
        bytes+=item.value("bytes_read",uint64_t(0));entries.push_back(std::move(item));
        if(++count%100==0 || count==before.size())std::cout<<count<<" / "<<before.size()<<" files"<<std::endl;
    }
    auto after=enumerate(root,issues);
    if(before!=after)issues.push_back({{"phase","snapshot"},{"error","File list or attributes changed during the scan"}});
    json report={{"format","starlight-native-inventory-v1"},{"files",entries},{"issues",issues},{"file_count",entries.size()},{"bytes_read",bytes},{"passed",issues.empty()}};
    auto text=report.dump(2);starlight::editor::Handle file(CreateFileW(long_path(output).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    if(!file)throw std::runtime_error("Cannot create new inventory report");
    size_t offset=0;while(offset<text.size()){DWORD written=0;DWORD length=DWORD(std::min<size_t>(text.size()-offset,4*1024*1024));if(!WriteFile(file.value,text.data()+offset,length,&written,nullptr)||!written)throw std::runtime_error("Cannot finish report");offset+=written;}
    if(!FlushFileBuffers(file.value))throw std::runtime_error("Cannot flush report");
    std::cout<<(issues.empty()?"PASS":"FAIL")<<": inventory complete\n";return issues.empty()?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}}
