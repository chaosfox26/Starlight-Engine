#define wWinMain StarlightFolderLauncherMain
#include "launcher.cpp"
#undef wWinMain
#include <compressapi.h>
#include <cstring>
#include <vector>

struct SetupCancelled : std::runtime_error { SetupCancelled():std::runtime_error("Setup cancelled"){} };
class SetupProgress {
    std::unique_ptr<PackageWindow> view;
    ULONGLONG lastUpdate=0;
    std::wstring lastPhase;
public:
    explicit SetupProgress(bool visible) {
        if(!visible)return;
        view=std::make_unique<PackageWindow>(L"Setting up Starlight",L"Preparing engine files...",true);
        view->addButton(IDCANCEL,L"Cancel",280,352,200,42);view->show();
        packageProgress=[this](const wchar_t* phase,size_t done,size_t total){update(phase,done,total);};
    }
    void update(const wchar_t* phase,size_t done,size_t total) {
        view->pump();
        if(view->choice==IDCANCEL){view->update(L"Cancelling and removing files from this attempt...",0);throw SetupCancelled();}
        auto now=GetTickCount64();if(now-lastUpdate<70 && lastPhase==phase && done!=total)return;
        lastUpdate=now;lastPhase=phase;
        view->update(std::wstring(phase)+L"\n"+std::to_wstring(done)+L" / "+std::to_wstring(total)+L" files",total?int(done*1000/total):0);
    }
    void finish(){packageProgress={};view.reset();}
    ~SetupProgress(){finish();}
};

std::string digestBytes(const std::string& bytes) {
    require(bytes.size()<ULONG_MAX,"Payload index too large");
    unsigned char value[32];
    require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,(PUCHAR)bytes.data(),ULONG(bytes.size()),value,32)>=0,"Cannot verify payload index");
    std::ostringstream result;
    for(auto c:value)result<<std::hex<<std::setw(2)<<std::setfill('0')<<int(c);
    return result.str();
}
json payloadIndex(std::ifstream& input) {
    input.seekg(0,std::ios::end);
    auto length=uint64_t(input.tellg());
    require(length>88,"Embedded engine payload is missing");
    input.seekg(length-88);
    char signature[16];uint64_t size=0;char expected[64];
    input.read(signature,16);input.read(reinterpret_cast<char*>(&size),8);input.read(expected,64);
    require(input.good() && std::memcmp(signature,"STARLIGHT-PACK01",16)==0,"Invalid engine payload footer");
    require(size>0 && size<32*1024*1024 && size<length-88,"Invalid engine payload index size");
    std::string index(size,'\0');input.seekg(length-88-size);input.read(index.data(),index.size());
    require(input.good() && digestBytes(index)==std::string(expected,64),"Engine payload index is damaged");
    auto result=json::parse(index);require(result.at("format")=="starlight-self-contained-v1","Unsupported engine payload");
    auto& files=result.at("files");require(files.is_array() && !files.empty() && files.size()<100000,"Invalid engine payload inventory");
    std::set<std::wstring> names;
    for(auto& item:files){
        auto path=fs::u8path(item.at("path").get<std::string>()).wstring();
        std::transform(path.begin(),path.end(),path.begin(),towupper);
        require(names.insert(path).second,"Duplicate engine payload path");
        auto offset=item.at("offset").get<uint64_t>(),stored=item.at("stored").get<uint64_t>(),plain=item.at("bytes").get<uint64_t>();
        require(offset<length-88-size && stored<=length-88-size-offset && plain<=512*1024*1024,"Invalid engine payload bounds");
    }
    return result;
}
void unpack(const fs::path& destination) {
    require(destination.is_absolute() && !fs::exists(destination),"Choose a new empty Starlight folder");
    std::ifstream input(self(),std::ios::binary);require(bool(input),"Cannot open embedded payload");
    auto index=payloadIndex(input);
    for(auto& item:index.at("files"))safe(destination,item.at("path").get<std::string>());
    DECOMPRESSOR_HANDLE decoder=nullptr;
    require(CreateDecompressor(COMPRESS_ALGORITHM_XPRESS_HUFF,nullptr,&decoder),"Cannot initialize engine extraction");
    std::vector<fs::path> created;
    try {
        fs::create_directories(destination);
        size_t extracted=0;
        for(auto& item:index.at("files")) {
            reportPackageProgress(L"Extracting engine files",extracted++,index.at("files").size());
            auto output=safe(destination,item.at("path").get<std::string>());
            std::string stored(item.at("stored").get<size_t>(),'\0'),plain(item.at("bytes").get<size_t>(),'\0');
            input.seekg(item.at("offset").get<uint64_t>());input.read(stored.data(),stored.size());require(input.good(),"Truncated engine payload");
            if(item.at("compressed").get<bool>()) {
                SIZE_T produced=0;
                require(Decompress(decoder,stored.data(),stored.size(),plain.data(),plain.size(),&produced) && produced==plain.size(),"Cannot decompress engine payload");
            } else {require(stored.size()==plain.size(),"Invalid stored engine file");plain=std::move(stored);}
            require(digestBytes(plain)==item.at("sha256").get<std::string>(),"Embedded engine file is damaged");
            fs::create_directories(output.parent_path());created.push_back(output);write(output,plain);
        }
        verify(destination);
        CloseDecompressor(decoder);
    }catch(...){
        CloseDecompressor(decoder);
        std::set<fs::path> directories;
        std::error_code error;
        for(auto& file:created){fs::remove(file,error);for(auto dir=file.parent_path();dir!=destination && dir!=dir.parent_path();dir=dir.parent_path())directories.insert(dir);}
        for(auto it=directories.rbegin();it!=directories.rend();++it)fs::remove(*it,error);
        fs::remove(destination,error);throw;
    }
}
fs::path portableDestination() {
    PackageWindow view(L"Your portable Starlight",L"Choose a new folder for the engine, settings and reports.");
    view.addEntry((known(FOLDERID_Desktop)/L"Starlight Alpha One").wstring());
    view.addButton(101,L"Create portable copy",48,394);view.addButton(IDCANCEL,L"Cancel",402,394);
    if(view.run()!=101)return {};
    auto result=fs::path(view.value());require(result.is_absolute(),"Choose an absolute destination path");return result;
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    bool quiet=false;
    try {
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        std::wstring mode=argc>1?argv[1]:L"";fs::path destination=argc>2?argv[2]:L"";LocalFree(argv);
        quiet=!mode.empty();
        require(mode.empty() || mode==L"--extract-only" || mode==L"--install-only","Unknown package option");
        int selection=mode==L"--extract-only"?101:mode==L"--install-only"?102:choose();
        if(selection!=101 && selection!=102){CoUninitialize();return 0;}
        if(selection==101){
            if(destination.empty())destination=portableDestination();if(destination.empty()){CoUninitialize();return 0;}
            SetupProgress progress(mode.empty());
            unpack(fs::absolute(destination));write(destination/L"portable.mode","portable\n");
            progress.finish();
            if(mode.empty())launchEditor(destination,json::parse(read(destination/L"starlight-package.json")),false);
        }else{
            require(!fs::exists(installRoot()),"Starlight is already installed, or its destination contains preserved files");
            GUID id{};require(SUCCEEDED(CoCreateGuid(&id)),"Cannot create extraction directory");wchar_t name[40];StringFromGUID2(id,name,40);
            auto temporary=fs::temp_directory_path()/(std::wstring(L"Starlight-Setup-")+name);
            SetupProgress progress(mode.empty());
            unpack(temporary);auto manifest=json::parse(read(temporary/L"starlight-package.json"));
            try {installPackage(temporary,installRoot(),manifest);}catch(...){removeInstalledFiles(temporary,manifest);throw;}
            packageProgress={};
            removeInstalledFiles(temporary,manifest);
            progress.finish();
            if(mode.empty())launchEditor(installRoot(),manifest,true);
        }
        CoUninitialize();return 0;
    }catch(const SetupCancelled&){CoUninitialize();return 2;}catch(const std::exception& error){if(!quiet)packageMessage(L"Starlight setup",std::wstring(error.what(),error.what()+strlen(error.what())));CoUninitialize();return 1;}
}
