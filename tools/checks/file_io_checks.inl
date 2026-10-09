// read_file must return exactly the file's bytes or fail (audit ED-09). A byte range locked by another handle makes the read fail part-way; the old
// iterator-based read ended quietly there and returned the truncated prefix as if it were the whole file.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../../deep/engine/utf8_file.h"
static void file_io_checks(const auto& require,const fs::path& folder){
    const fs::path dir=folder/"file-io-checks";fs::create_directories(dir);
    auto expected=[&](size_t size){std::string bytes(size,'\0');uint32_t state=0x9e3779b9u+uint32_t(size);for(auto& c:bytes){state=state*1664525u+1013904223u;c=char(state>>24);}return bytes;};
    bool exact=true;
    for(size_t size:{size_t(0),size_t(1),size_t(4095),size_t(4096),size_t(4097),size_t(1u<<20)+3}){
        const auto path=dir/("size-"+std::to_string(size)+".bin");const auto bytes=expected(size);
        {std::ofstream out(path,std::ios::binary);out.write(bytes.data(),std::streamsize(bytes.size()));}
        exact=exact&&read_file(path)==bytes&&hash_file(path)==hash_bytes(bytes);
    }
    require(exact,"ED-09: Files of every tested size, including empty and block-boundary sizes, read back byte for byte with a matching hash");
    bool missing=false;try{(void)read_file(dir/"does-not-exist.bin");}catch(const std::exception&){missing=true;}
    require(missing,"ED-09: A file that does not exist is an error, not an empty string");
    const auto locked=dir/"locked.bin";const auto bytes=expected(300000);
    {std::ofstream out(locked,std::ios::binary);out.write(bytes.data(),std::streamsize(bytes.size()));}
    HANDLE holder=CreateFileW(locked.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    require(holder!=INVALID_HANDLE_VALUE&&LockFile(holder,150000,0,1000,0)!=0,"ED-09: A byte range in the middle of a file can be locked for the failure check");
    bool refused=false;std::string why;
    try{(void)read_file(locked);}catch(const std::exception& e){refused=true;why=e.what();}
    require(refused&&why.find("Read failed")!=std::string::npos,"ED-09: A read that fails part-way is reported instead of returning the truncated prefix as the whole file");
    if(holder!=INVALID_HANDLE_VALUE){UnlockFile(holder,150000,0,1000,0);CloseHandle(holder);}
    require(read_file(locked)==bytes,"ED-09: The same file reads completely once the range is unlocked");
    // ED-10: the editor used to re-read the file after saving to learn its hash, which fails while another process holds a lock on it.
    SceneData scene;Character cube;cube.Id="hash-check";cube.Name="hash-check";cube.Shape="cube";scene.Characters={cube};
    const auto savedPath=dir/"saved.starscene";
    const auto savedHash=save_scene(scene,savedPath);
    require(savedHash.size()==64&&savedHash==hash_bytes(scene.encode())&&savedHash==hash_file(savedPath),"ED-10: save_scene returns the SHA-256 of exactly the bytes it wrote");
    HANDLE saver=CreateFileW(savedPath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    const bool lockedWhole=saver!=INVALID_HANDLE_VALUE&&LockFile(saver,0,0,0xFFFFFFFFu,0)!=0;
    bool rereadFails=false;try{(void)hash_file(savedPath);}catch(const std::exception&){rereadFails=true;}
    if(saver!=INVALID_HANDLE_VALUE){UnlockFile(saver,0,0,0xFFFFFFFFu,0);CloseHandle(saver);}
    require(lockedWhole&&rereadFails,"ED-10: While another handle locks the saved file, re-reading it to hash it fails, which is why the hash is now returned by the save itself");
    // DEEP-1: a report path outside the ANSI code page (Cyrillic and CJK folder names) must be written where it was asked for.
    const std::wstring wideName=L"report-ж中.json";
    const auto reportFolder=dir/L"ж中-folder";fs::create_directories(reportFolder);
    const std::string reportPath=utf8FromWide((reportFolder/wideName).wstring());
    FILE* report=nullptr;
    const bool opened=fopenUtf8(&report,reportPath,L"w")==0&&report;
    if(opened){fputs("{}",report);fclose(report);}
    require(utf8FromWide(wideName)=="report-\xD0\xB6\xE4\xB8\xAD.json"&&opened&&fs::exists(reportFolder/wideName)&&fs::file_size(reportFolder/wideName)==2,"DEEP-1: A --report path with characters outside the ANSI code page is converted to UTF-8 and opened at exactly that path");
}
