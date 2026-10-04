#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <set>
#include <algorithm>
#include <exception>
#include <functional>
#include "../../deep/src/json.hpp"
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
namespace fs = std::filesystem;
using json = nlohmann::json;
#include "packaging_ui.hpp"
std::function<void(const wchar_t*,size_t,size_t)> packageProgress;
void reportPackageProgress(const wchar_t* phase,size_t done,size_t total) { if(packageProgress)packageProgress(phase,done,total); }
const wchar_t* regKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\StarlightPreview";
void require(bool good, const char* message) { if (!good) throw std::runtime_error(message); }
fs::path self() { wchar_t b[32768]; auto n=GetModuleFileNameW(nullptr,b,32768); require(n && n<32768,"Cannot locate launcher"); return b; }
fs::path known(REFKNOWNFOLDERID id) { PWSTR p=nullptr; require(SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&p)),"Cannot locate user folder"); fs::path out(p); CoTaskMemFree(p); return out; }
std::wstring quote(const fs::path& p) { return L"\""+p.wstring()+L"\""; }
std::string read(const fs::path& p) { std::ifstream in(p,std::ios::binary); require(bool(in),"Cannot read package file"); return {std::istreambuf_iterator<char>(in),{}}; }
void write(const fs::path& p,const std::string& s) { std::ofstream f(p,std::ios::binary); f<<s; require(bool(f),"Cannot write file. Choose a writable location."); }
std::string hash(const fs::path& p) { auto s=read(p); require(s.size()<ULONG_MAX,"Package file too large"); unsigned char b[32]; require(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,(PUCHAR)s.data(),ULONG(s.size()),b,32)>=0,"Cannot verify package"); std::ostringstream o; for(auto v:b)o<<std::hex<<std::setw(2)<<std::setfill('0')<<int(v); return o.str(); }
fs::path safe(const fs::path& root,const std::string& relative) {
    require(root.is_absolute(),"Package root must be absolute");
    for(auto parent=root;!parent.empty();){auto flags=GetFileAttributesW(parent.c_str());require(flags==INVALID_FILE_ATTRIBUTES || !(flags&FILE_ATTRIBUTE_REPARSE_POINT),"Package root links are not allowed");auto next=parent.parent_path();if(next==parent)break;parent=next;}
    auto p=fs::u8path(relative); require(!p.empty() && !p.is_absolute() && !p.has_root_name(),"Unsafe package path");
    require(relative.find(':')==std::string::npos,"Unsafe package stream");
    fs::path out=root;
    for(const auto& part:p) { require(part!=L".." && part!=L".","Unsafe package traversal");auto name=part.wstring();require(!name.empty() && name.back()!=L'.' && name.back()!=L' ',"Ambiguous package path");for(auto& c:name)c=towupper(c);auto stem=name.substr(0,name.find(L'.'));require(stem!=L"CON" && stem!=L"PRN" && stem!=L"AUX" && stem!=L"NUL" && !(stem.size()==4 && (stem.substr(0,3)==L"COM" || stem.substr(0,3)==L"LPT") && stem[3]>=L'0' && stem[3]<=L'9'),"Reserved package filename");out/=part; auto a=GetFileAttributesW(out.c_str()); require(a==INVALID_FILE_ATTRIBUTES || !(a&FILE_ATTRIBUTE_REPARSE_POINT),"Package links are not allowed"); }
    return out;
}
json verify(const fs::path& root,bool contents=true) {
    auto m=json::parse(read(root/L"starlight-package.json"));
    require(m.at("format")=="starlight-portable-v1" && m.at("publisher")=="ChaosFox","Wrong package manifest");
    require(m.at("files").is_array() && !m.at("files").empty(),"Empty package");
    std::set<std::string> paths;
    size_t verifiedFiles=0;
    for(const auto& f:m.at("files")) { if(contents)reportPackageProgress(L"Verifying engine files",verifiedFiles++,m.at("files").size()); auto rel=f.at("path").get<std::string>();auto key=rel;std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return c=='\\'?'/':char(tolower(c));});require(key!="installed.mode" && key!="portable.mode" && key!="starlight-package.json" && key.rfind("data/",0)!=0,"Package contains mutable user state");require(paths.insert(key).second,"Duplicate package entry"); auto p=safe(root,rel); if(contents)require(fs::is_regular_file(p) && hash(p)==f.at("sha256").get<std::string>(),"Package file missing or damaged"); }
    auto editor=m.at("editor").get<std::string>();std::transform(editor.begin(),editor.end(),editor.begin(),[](unsigned char c){return c=='\\'?'/':char(tolower(c));});
    require(paths.count(editor) && paths.count("starlight.exe"),"Package entry points missing");
    if(m.contains("analysis")) {
        auto analysis=m.at("analysis").get<std::string>();
        std::transform(analysis.begin(),analysis.end(),analysis.begin(),[](unsigned char c){return c=='\\'?'/':char(tolower(c));});
        require(paths.count(analysis),"Analysis entry point missing");safe(root,m.at("analysis").get<std::string>());
    }
    return m;
}
void spawn(const fs::path& exe,std::wstring args,const fs::path& cwd) {
    auto command=quote(exe)+L" "+args; STARTUPINFOW si{sizeof(si)}; PROCESS_INFORMATION pi{};
    require(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,cwd.c_str(),&si,&pi),"Could not start Starlight"); CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
}
fs::path installRoot() { return known(FOLDERID_LocalAppData)/L"Programs/Starlight Preview"; }
void shortcut(const fs::path& target,bool analysis=false,bool desktop=false) {
    IShellLinkW* link=nullptr; require(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))),"Cannot create shortcut");
    link->SetPath((target/L"Starlight.exe").c_str()); link->SetWorkingDirectory(target.c_str());
    if(analysis)link->SetArguments(L"--analysis");
    IPersistFile* file=nullptr; auto hr=link->QueryInterface(IID_PPV_ARGS(&file));
    if(SUCCEEDED(hr)){hr=file->Save((known(desktop?FOLDERID_Desktop:FOLDERID_Programs)/(analysis?L"Starlight Analysis.lnk":desktop?L"Starlight Engine.lnk":L"Starlight Preview.lnk")).c_str(),TRUE);file->Release();} link->Release(); require(SUCCEEDED(hr),"Cannot save shortcut");
}
void registerInstall(const fs::path& root) {
    HKEY key=nullptr; require(RegCreateKeyExW(HKEY_CURRENT_USER,regKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)==ERROR_SUCCESS,"Cannot register install");
    auto set=[&](const wchar_t* name,std::wstring value){auto rc=RegSetValueExW(key,name,0,REG_SZ,(const BYTE*)value.c_str(),DWORD((value.size()+1)*sizeof(wchar_t))); if(rc!=ERROR_SUCCESS){RegCloseKey(key);throw std::runtime_error("Cannot register install value");}};
    set(L"DisplayName",L"Starlight Engine Preview");set(L"Publisher",L"ChaosFox");set(L"InstallLocation",root.wstring());set(L"DisplayIcon",(root/L"Starlight.exe").wstring());set(L"UninstallString",quote(root/L"Starlight.exe")+L" --uninstall");RegCloseKey(key);
}
void copyPackage(const fs::path& source,const fs::path& target,const json& m) {
    require(!fs::exists(target),"An installation already exists. Uninstall it before replacing it.");
    auto stage=target.parent_path()/(L".Starlight-stage-"+std::to_wstring(GetCurrentProcessId()));
    require(!fs::exists(stage),"Installation staging folder already exists");safe(stage,"Starlight.exe");fs::create_directories(stage);
    try{
        size_t copiedFiles=0;
        for(const auto& f:m.at("files")){reportPackageProgress(L"Installing engine files",copiedFiles++,m.at("files").size());auto rel=f.at("path").get<std::string>();auto dest=safe(stage,rel);fs::create_directories(dest.parent_path());fs::copy_file(safe(source,rel),dest);}
        fs::copy_file(source/L"starlight-package.json",stage/L"starlight-package.json");
        verify(stage);write(stage/L"installed.mode","Starlight Preview installation\n");fs::rename(stage,target);
    }catch(...){
        std::error_code ec;std::set<fs::path> dirs;
        for(const auto& f:m.at("files")){auto p=safe(stage,f.at("path").get<std::string>());fs::remove(p,ec);for(auto d=p.parent_path();d!=stage && d!=d.parent_path();d=d.parent_path())dirs.insert(d);}
        fs::remove(stage/L"starlight-package.json",ec);fs::remove(stage/L"installed.mode",ec);
        for(auto it=dirs.rbegin();it!=dirs.rend();++it)fs::remove(*it,ec);fs::remove(stage,ec);throw;
    }
}
void launchEditor(const fs::path& root,const json& m,bool installed) {
    auto data=installed?known(FOLDERID_LocalAppData)/L"Starlight":root/L"data";
    fs::create_directories(data);write(data/L".write-test","");fs::remove(data/L".write-test");
    SetEnvironmentVariableW(L"STARLIGHT_DATA_ROOT",data.c_str());SetEnvironmentVariableW(L"STARLIGHT_RESOURCE_ROOT",root.c_str());
    spawn(safe(root,m.at("editor").get<std::string>()),L"--show-terrain",root);
}
void removeInstalledFiles(const fs::path& root,const json& m) {
    safe(root,"Starlight.exe");
    std::set<fs::path> dirs;
    for(const auto& f:m.at("files")) {
        auto p=safe(root,f.at("path").get<std::string>());
        // A changed file may be user work. Keep it rather than remove it.
        if(fs::is_regular_file(p) && hash(p)==f.at("sha256").get<std::string>()) fs::remove(p);
        for(auto d=p.parent_path();d!=root && d!=d.parent_path();d=d.parent_path())dirs.insert(d);
    }
    fs::remove(safe(root,"starlight-package.json"));fs::remove(safe(root,"installed.mode"));
    std::error_code ec;
    for(auto it=dirs.rbegin();it!=dirs.rend();++it)fs::remove(*it,ec);
    fs::remove(root,ec);
}
void installTransaction(const fs::path& source,const fs::path& target,const json& m,
                        const std::function<void()>& registerProgram,
                        const std::function<void()>& undoRegistration) {
    copyPackage(source,target,m);
    try { registerProgram(); }
    catch(...) {
        auto failure=std::current_exception();
        undoRegistration();
        removeInstalledFiles(target,m);
        std::rethrow_exception(failure);
    }
}
void installPackage(const fs::path& source,const fs::path& target,const json& m) {
    auto link=known(FOLDERID_Programs)/L"Starlight Preview.lnk";
    auto analysisLink=known(FOLDERID_Programs)/L"Starlight Analysis.lnk";
    auto desktopLink=known(FOLDERID_Desktop)/L"Starlight Engine.lnk";
    auto desktopAnalysis=known(FOLDERID_Desktop)/L"Starlight Analysis.lnk";
    require(!fs::exists(desktopLink) && (!m.contains("analysis")||!fs::exists(desktopAnalysis)),"A Starlight desktop shortcut already exists. Resolve it before installing.");
    require(!fs::exists(link) && (!m.contains("analysis")||!fs::exists(analysisLink)),"A Starlight shortcut already exists. Resolve the existing installation first.");
    HKEY prior=nullptr;auto found=RegOpenKeyExW(HKEY_CURRENT_USER,regKey,0,KEY_READ,&prior);
    if(prior)RegCloseKey(prior);
    require(found==ERROR_FILE_NOT_FOUND,"Starlight registration already exists or cannot be checked.");
    installTransaction(source,target,m,[&]{shortcut(target);shortcut(target,false,true);if(m.contains("analysis")){shortcut(target,true);shortcut(target,true,true);}registerInstall(target);},[&]{
        std::error_code ec;fs::remove(link,ec);fs::remove(desktopLink,ec);if(m.contains("analysis")){fs::remove(analysisLink,ec);fs::remove(desktopAnalysis,ec);}
        RegDeleteTreeW(HKEY_CURRENT_USER,regKey);
    });
}
int choose() { return packageChoice(); }
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    bool unattended=false;
    try {
        int count=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&count);std::wstring mode=count>1?argv[1]:L"",argument=count>2?argv[2]:L"";LocalFree(argv);
        unattended=mode==L"--self-test" || mode==L"--verify-package" || mode==L"--install-only" || mode==L"--uninstall-quiet" || mode==L"--finish-uninstall-quiet";
        auto root=self().parent_path();
        if(mode==L"--finish-uninstall" || mode==L"--finish-uninstall-quiet"){
            auto pid=std::stoul(argument);HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,DWORD(pid));if(parent){auto waited=WaitForSingleObject(parent,30000);CloseHandle(parent);require(waited==WAIT_OBJECT_0,"Uninstaller is still running");}
            root=installRoot();require(fs::is_regular_file(root/L"installed.mode"),"Installation marker missing");auto m=verify(root,false);
            removeInstalledFiles(root,m);
            fs::remove(known(FOLDERID_Programs)/L"Starlight Preview.lnk");
            fs::remove(known(FOLDERID_Desktop)/L"Starlight Engine.lnk");
            if(m.contains("analysis"))fs::remove(known(FOLDERID_Desktop)/L"Starlight Analysis.lnk");
            if(m.contains("analysis"))fs::remove(known(FOLDERID_Programs)/L"Starlight Analysis.lnk");
            RegDeleteTreeW(HKEY_CURRENT_USER,regKey);
            if(!unattended)packageMessage(L"Starlight removed",L"Starlight program files were removed. Your projects and settings were kept.");return 0;
        }
        if(mode==L"--self-test") {
            bool blocked=false;try{safe(root,"../escape");}catch(...){blocked=true;}require(blocked,"Traversal check failed");
            blocked=false;try{safe(root,"C:/escape");}catch(...){blocked=true;}require(blocked,"Root check failed");
            require(safe(root,"bin/editor.exe")==root/L"bin/editor.exe","Relative path check failed");
            for(auto name:{"file:stream","CON.txt","name. ","data/../escape"}){blocked=false;try{safe(root,name);}catch(...){blocked=true;}require(blocked,"Unsafe filename accepted");}
            auto test=fs::temp_directory_path()/(L"Starlight-launcher-check-"+std::to_wstring(GetCurrentProcessId()));require(!fs::exists(test),"Test directory collision");fs::create_directories(test/L"source");
            auto source=test/L"source",target=test/L"installed";fs::copy_file(self(),source/L"Starlight.exe");
            json m={{"format","starlight-portable-v1"},{"publisher","ChaosFox"},{"editor","Starlight.exe"},{"files",json::array({{{"path","Starlight.exe"},{"sha256",hash(source/L"Starlight.exe")}}})}};
            write(source/L"starlight-package.json",m.dump());verify(source);copyPackage(source,target,m);verify(target);require(fs::exists(source/L"Starlight.exe"),"Portable source changed");
            write(target/L"user-project.txt","keep user work");
            removeInstalledFiles(target,m);
            require(fs::is_regular_file(target/L"user-project.txt") && !fs::exists(target/L"Starlight.exe"),"Rollback did not preserve user files");
            fs::remove(target/L"user-project.txt");fs::remove(target);
            copyPackage(source,target,m);write(target/L"Starlight.exe","changed after installation");
            removeInstalledFiles(target,m);
            require(read(target/L"Starlight.exe")=="changed after installation","Rollback removed changed files");
            fs::remove(target/L"Starlight.exe");fs::remove(target);
            bool undoCalled=false;blocked=false;
            try {installTransaction(source,target,m,[]{throw std::runtime_error("Injected registration failure");},[&]{undoCalled=true;});}catch(...){blocked=true;}
            require(blocked && undoCalled && !fs::exists(target) && fs::exists(source/L"Starlight.exe"),"Registration failure rollback failed");
            copyPackage(source,target,m);
            auto invalid=m;invalid["files"].push_back({{"path","missing.dll"},{"sha256","none"}});blocked=false;try{copyPackage(source,test/L"broken",invalid);}catch(...){blocked=true;}require(blocked && !fs::exists(test/L"broken") && !fs::exists(test/(L".Starlight-stage-"+std::to_wstring(GetCurrentProcessId()))),"Partial install rollback failed");
            auto duplicate=m;duplicate["files"].push_back({{"path","STARLIGHT.EXE"},{"sha256",hash(source/L"Starlight.exe")}});write(source/L"starlight-package.json",duplicate.dump());blocked=false;try{verify(source);}catch(...){blocked=true;}require(blocked,"Case collision accepted");
            for(auto folder:{source,target}){fs::remove(folder/L"Starlight.exe");fs::remove(folder/L"starlight-package.json");fs::remove(folder/L"installed.mode");fs::remove(folder);}fs::remove(test);return 0;
        }
        if(mode==L"--uninstall" || mode==L"--uninstall-quiet") {
            require(fs::equivalent(root,installRoot()) && fs::is_regular_file(root/L"installed.mode"),"Not a registered installation");
            if(!unattended && packageMessage(L"Uninstall Starlight",L"Remove Starlight program files? Your projects and settings will be kept.",true)!=IDOK)return 0;
            verify(root,false);wchar_t temp[32768];require(GetTempPathW(32768,temp)>0,"Cannot locate temporary folder");auto helper=fs::path(temp)/(L"Starlight-uninstall-"+std::to_wstring(GetCurrentProcessId())+L".exe");fs::copy_file(self(),helper);
            spawn(helper,std::wstring(unattended?L"--finish-uninstall-quiet ":L"--finish-uninstall ")+std::to_wstring(GetCurrentProcessId()),helper.parent_path());return 0;
        }
        auto m=verify(root);bool installed=fs::is_regular_file(root/L"installed.mode");
        if(mode==L"--verify-package")return 0;
        if(mode==L"--install-only"){installPackage(root,installRoot(),m);return 0;}
        if(m.value("testFixture",false)){MessageBoxW(nullptr,L"This is an automated launcher test fixture. It contains no editor and cannot run Starlight. Use the complete engine preview package when available.",L"Starlight — Test fixture only",MB_OK|MB_ICONINFORMATION);return 0;}
        if(mode==L"--analysis") {
            require(m.contains("analysis"),"This package does not include the Analysis Tool");
            spawn(safe(root,m.at("analysis").get<std::string>()),L"",root);return 0;
        }
        require(mode.empty() || mode==L"--choose","Unknown launcher option");
        int choice=installed?101:((mode!=L"--choose" && fs::exists(root/L"portable.mode"))?101:choose());
        if(choice==102){auto target=installRoot();installPackage(root,target,m);launchEditor(target,m,true);}
        else if(choice==101){if(!installed)write(root/L"portable.mode","portable\n");launchEditor(root,m,installed);}
        return 0;
    } catch(const std::exception& e) {if(unattended)OutputDebugStringA(e.what());else packageMessage(L"Starlight could not complete this action",std::wstring(e.what(),e.what()+strlen(e.what())));return 1;}
}
