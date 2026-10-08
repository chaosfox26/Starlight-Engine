#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "../../deep/src/json.hpp"
namespace fs=std::filesystem;
using json=nlohmann::json;
// Filesystem paths are UTF-16 on Windows; JSON and policy paths are UTF-8.
static std::string path_utf8(const fs::path& path) {
    auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}
static std::string read(const fs::path& path) {
    std::ifstream f(path,std::ios::binary);
    if(!f)throw std::runtime_error("Cannot read file");
    return {std::istreambuf_iterator<char>(f),{}};
}
static void write(const fs::path& path,const std::string& text) {
    fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f<<text;
    if(!f)throw std::runtime_error("Cannot write result");
}
static std::string sha256(const fs::path& path) {
    auto bytes=read(path);if(bytes.size()>ULONG_MAX)throw std::runtime_error("Hash input too large");
    unsigned char digest[32];
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,reinterpret_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest,32)<0)throw std::runtime_error("Hash failed");
    std::ostringstream s;for(auto b:digest)s<<std::hex<<std::setw(2)<<std::setfill('0')<<int(b);return s.str();
}
static std::string lower(std::string s) {std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return char(std::tolower(c));});return s;}
static uint32_t integer(const std::string& b,size_t offset,size_t width) {
    if(offset>b.size() || width>b.size()-offset)throw std::runtime_error("Truncated PE header");
    uint32_t n=0;for(size_t i=0;i<width;++i)n|=uint32_t(static_cast<unsigned char>(b[offset+i]))<<(i*8);return n;
}
static json inspect(const fs::path& requestedRoot,const json& manifest) {
    auto absolute=fs::absolute(requestedRoot).wstring();
    if(absolute.rfind(L"\\\\?\\",0)!=0)absolute=absolute.rfind(L"\\\\",0)==0?L"\\\\?\\UNC\\"+absolute.substr(2):L"\\\\?\\"+absolute;
    fs::path root(absolute);
    json violations=json::array(),historical=json::array();size_t images=0;
    for(fs::recursive_directory_iterator it(root),end;it!=end;++it) {
        const auto p=it->path();auto full=fs::absolute(p).wstring();
        if(full.rfind(L"\\\\?\\",0)!=0)full=full.rfind(L"\\\\",0)==0?L"\\\\?\\UNC\\"+full.substr(2):L"\\\\?\\"+full;
        auto flags=GetFileAttributesW(full.c_str());
        if(flags==INVALID_FILE_ATTRIBUTES)throw std::runtime_error("Cannot inspect file attributes");
        if(flags&FILE_ATTRIBUTE_REPARSE_POINT){it.disable_recursion_pending();violations.push_back({{"path",path_utf8(p.lexically_relative(root))},{"reason","Linked path requires separate verification"}});continue;}
        if(!it->is_regular_file())continue;
        auto ext=lower(path_utf8(p.extension())),name=lower(path_utf8(p.filename()));std::string reason;
        if(ext==".cs" || ext==".csproj" || ext==".xaml" || name.ends_with(".deps.json") || name.ends_with(".runtimeconfig.json"))reason="Managed source/project/runtime manifest";
        if(ext==".props" || ext==".targets") {auto text=read(p);if(text.find("Microsoft.NET.Sdk")!=std::string::npos || text.find("NuGet")!=std::string::npos)reason="Managed build import";}
        if(ext==".exe" || ext==".dll") {
            std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read executable");
            std::string b(4096,'\0');f.read(b.data(),b.size());b.resize(size_t(f.gcount()));
            if(b.starts_with("MZ"))try {
                auto pe=integer(b,0x3c,4);if(pe>b.size() || b.substr(pe,4)!=std::string("PE\0\0",4))throw std::runtime_error("PE header outside the inspected window or with a wrong signature");
                auto opt=size_t(pe)+24;auto magic=integer(b,opt,2);
                if(magic!=0x20b && magic!=0x10b)throw std::runtime_error("Unsupported PE header");
                auto dirs=opt+(magic==0x20b?112:96);
                if(integer(b,dirs+14*8,4))reason="CLR assembly";
                ++images;
            }catch(...){reason="Unable to verify PE header";}
        }
        if(reason.empty())continue;
        auto relative=path_utf8(p.lexically_relative(root));
        json finding={{"path",relative},{"reason",reason}};bool archived=false;
        for(const auto& ref:manifest.at("references"))if(ref.at("path").get<std::string>()==relative && reason=="CLR assembly" && sha256(p)==ref.at("sha256").get<std::string>()) {
            finding["sha256"]=ref.at("sha256");finding["purpose"]=ref.at("purpose");historical.push_back(finding);archived=true;break;
        }
        if(!archived)violations.push_back(finding);
    }
    return {{"success",violations.empty()},{"scope",path_utf8(requestedRoot)},{"peImagesChecked",images},{"violations",violations},{"historicalReferences",historical},
            {"scopePolicy","All repository files scanned. Only exact hash-pinned historical audit reference binaries are classified separately; active sources and all build outputs remain checked. Linked paths are reported for separate verification."}};
}
static void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void selfTest() {
    auto root=fs::temp_directory_path()/(L"starlight-native-guard-\u03a9-"+std::to_wstring(GetCurrentProcessId()));
    require(!fs::exists(root),"Fixture already exists");fs::create_directory(root);
    std::string b(512,'\0');b.replace(0,2,"MZ");b[0x3c]=char(0x80);b.replace(0x80,4,std::string("PE\0\0",4));b[0x98]=0x0b;b[0x99]=0x02;b[0x98+112+14*8+1]=0x10;
    auto archive=root/L"archive/old.dll",active=root/L"out/active.dll",source=root/L"archive/unexpected.cs";
    write(archive,b);json manifest={{"references",json::array({{{"path","archive/old.dll"},{"sha256",sha256(archive)},{"purpose","Synthetic test fixture"}}})}};
    auto result=inspect(root,manifest);require(result.at("success")==true && result.at("historicalReferences").size()==1,"Pinned reference failed");
    write(active,b);require(inspect(root,manifest).at("violations").size()==1,"Active copy incorrectly exempted");fs::remove(active);
    write(archive,b+"changed");require(inspect(root,manifest).at("violations").size()==1,"Changed reference incorrectly exempted");write(archive,b);
    write(source,"class Unexpected {}");require(inspect(root,manifest).at("violations").size()==1,"Archived source incorrectly exempted");fs::remove(source);
    write(active,"MZbroken");require(inspect(root,manifest).at("violations").size()==1,"Broken executable silently accepted");fs::remove(active);
    {std::string late(9000,'\0');late.replace(0,2,"MZ");late[0x3c]=0x00;late[0x3d]=0x20;write(active,late);require(inspect(root,manifest).at("violations").size()==1,"PE header beyond the read window silently accepted");fs::remove(active);}
    {auto wrong=b;wrong[0x80]='X';write(active,wrong);require(inspect(root,manifest).at("violations").size()==1,"PE image with a wrong signature silently accepted");fs::remove(active);}
    auto unicode=root/L"\u2605.txt",unicodeSource=root/L"\u2605.cs";
    write(unicode,"Plain Unicode-named file");
    auto unicodePlain=inspect(root,manifest);
    require(unicodePlain.at("success")==true && unicodePlain.at("scope")==path_utf8(root),"Unicode file or root could not be inspected");
    write(unicodeSource,"class UnicodeSource {}");auto unicodeResult=inspect(root,manifest);
    require(unicodeResult.at("violations").size()==1 && unicodeResult.at("violations")[0].at("path")==path_utf8(unicodeSource.lexically_relative(root)),"Unicode managed source was skipped or misreported");
    fs::remove(unicodeSource);fs::remove(unicode);
    fs::remove(archive);fs::remove(root/L"archive");fs::remove(root/L"out");fs::remove(root);
    std::cout<<"PASS: pinned reference, active copy, changed reference, unexpected source, malformed PE, Unicode paths and findings\n";
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc==2 && std::wstring(argv[1])==L"--self-test"){selfTest();return 0;}
        fs::path root,manifest,output;
        for(int i=1;i<argc;i+=2){require(i+1<argc,"Missing option value");std::wstring flag=argv[i];if(flag==L"--root")root=fs::absolute(argv[i+1]);else if(flag==L"--manifest")manifest=argv[i+1];else if(flag==L"--output")output=fs::absolute(argv[i+1]);else throw std::runtime_error("Unknown option");}
        require(!root.empty() && !manifest.empty() && !output.empty(),"Root, manifest and output required");
        auto report=inspect(root,json::parse(read(manifest)));write(output,report.dump(2));std::cout<<report.dump(2)<<'\n';return report.at("success").get<bool>()?0:1;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
