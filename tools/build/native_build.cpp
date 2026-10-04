#include "platform.hpp"
#include "../audit/native_process.hpp"
#include <iostream>
#include <algorithm>
#include <cwctype>
using namespace starlight::editor;

// Explicit tool locations keep a packaged build independent of machine PATH.
int wmain(int argc, wchar_t** argv) try {
    fs::path root, build, toolchain;
    std::wstring config=L"Release";
    bool test=false, compact=false;
    for(int i=1;i<argc;++i) {
        std::wstring option=argv[i];
        if(option==L"--test") { test=true;continue; }
        if(option==L"--compact") { compact=true;continue; }
        if(i+1==argc)throw std::runtime_error("Missing build option value");
        if(option==L"--root")root=fs::absolute(argv[++i]);
        else if(option==L"--build-dir")build=fs::absolute(argv[++i]);
        else if(option==L"--toolchain")toolchain=fs::absolute(argv[++i]);
        else if(option==L"--config")config=argv[++i];
        else throw std::runtime_error("Unknown build option");
    }
    if(root.empty()||build.empty()||toolchain.empty()||(config!=L"Release"&&config!=L"Debug"))
        throw std::runtime_error("Specify --root, --build-dir, --toolchain and optional --config Debug|Release, --test, --compact");
    if(!fs::is_regular_file(root/L"CMakeLists.txt"))throw std::runtime_error("Source root has no CMake project");
    auto settings=json::parse(read_file(toolchain));
    if(settings.at("format")!="starlight-build-toolchain-v1")throw std::runtime_error("Unsupported toolchain manifest");
    auto locate=[&](const char* key) {
        auto path=fs::u8path(settings.at(key).get<std::string>());
        if(path.is_relative())path=toolchain.parent_path()/path;
        path=fs::weakly_canonical(path);
        if(!fs::is_regular_file(path))throw std::runtime_error(std::string("Missing toolchain file: ")+key);
        return path;
    };
    auto cmake=locate("cmake"),ctest=locate("ctest"),ninja=locate("ninja"),compiler=locate("compiler"),dxc=locate("dxc");
    fs::create_directories(build);
    auto logs=build/(L"build-run-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if(!fs::create_directory(logs))throw std::runtime_error("Cannot create fresh build logs");
    json stages=json::array();
    auto execute=[&](const char* name,const fs::path& exe,const std::vector<std::wstring>& args) {
        auto result=starlight::audit::run(exe,args,root,logs/(std::string(name)+".log"),3600000);
        stages.push_back({{"name",name},{"exitCode",result.exitCode},{"timedOut",result.timedOut}});
        atomic_write(logs/L"results.json",stages.dump(2));
        if(result.timedOut||result.exitCode)throw std::runtime_error(std::string(name)+" failed; see build logs");
    };
    std::vector<std::wstring> configure={L"-S",root.wstring(),L"-B",build.wstring(),L"-G",L"Ninja Multi-Config",
        L"-DCMAKE_MAKE_PROGRAM="+ninja.wstring(),L"-DCMAKE_CXX_COMPILER="+compiler.wstring(),L"-DSTARLIGHT_DXC="+dxc.wstring()};
    if(settings.contains("cmakeArguments"))for(const auto& value:settings.at("cmakeArguments"))configure.push_back(wide(value.get<std::string>()));
    if(settings.contains("cmakePaths")) {
        const auto& paths=settings.at("cmakePaths");
        if(!paths.is_object())throw std::runtime_error("Toolchain cmakePaths must be an object");
        for(auto entry=paths.begin();entry!=paths.end();++entry) {
            const auto key=entry.key();
            if(key.empty()||key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos)
                throw std::runtime_error("Invalid CMake path variable");
            auto path=fs::u8path(entry.value().get<std::string>());
            if(path.is_relative())path=toolchain.parent_path()/path;
            path=fs::weakly_canonical(path);
            if(!fs::exists(path))throw std::runtime_error("Missing CMake toolchain path: "+key);
            configure.push_back(L"-D"+wide(key)+L"="+path.generic_wstring());
        }
    }
    execute("configure",cmake,configure);
    MEMORYSTATUSEX memory{};
    memory.dwLength=sizeof(memory);
    const bool memoryKnown=GlobalMemoryStatusEx(&memory)!=FALSE;
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    constexpr ULONGLONG gib=1024ull*1024*1024;
    unsigned jobs=1;
    if(memoryKnown&&memory.ullTotalPhys>4*gib&&memory.ullAvailPhys>gib) {
        const auto budget=(memory.ullAvailPhys-gib)/(2*gib);
        jobs=static_cast<unsigned>(std::max<ULONGLONG>(1,std::min<ULONGLONG>(
            std::min<DWORD>(4,system.dwNumberOfProcessors),budget)));
    }
    atomic_write(logs/L"resources.json",json{{"memoryQuerySucceeded",memoryKnown},
        {"totalPhysicalBytes",memory.ullTotalPhys},{"availablePhysicalBytes",memory.ullAvailPhys},
        {"logicalProcessors",system.dwNumberOfProcessors},{"parallelJobs",jobs},
        {"reservedAvailableBytes",gib},{"budgetPerJobBytes",2*gib},
        {"scope","Conservative initial concurrency limit; not a peak-memory guarantee"}}.dump(2));
    execute("build",cmake,{L"--build",build.wstring(),L"--config",config,L"--parallel",std::to_wstring(jobs)});
    if(fs::is_regular_file(root/L"starlight-package.json")) {
        auto package=json::parse(read_file(root/L"starlight-package.json"));
        if(!package.contains("sourceBuildRuntimeFiles"))throw std::runtime_error("Package lacks the source-build runtime dependency list");
        json copied=json::array();
        const auto destinationRoot=fs::weakly_canonical(build/config);
        for(const auto& value:package.at("sourceBuildRuntimeFiles")) {
            const auto name=value.get<std::string>();
            if(name.rfind("runtime/Release/",0)!=0)throw std::runtime_error("Invalid runtime dependency location");
            auto relative=fs::u8path(name.substr(16));
            if(relative.empty()||relative.is_absolute())throw std::runtime_error("Invalid runtime dependency path");
            for(const auto& part:relative)if(part==L"..")throw std::runtime_error("Runtime dependency escapes output");
            auto filename=relative.filename().wstring();
            std::transform(filename.begin(),filename.end(),filename.begin(),[](wchar_t c){return std::towlower(c);});
            const auto first=relative.begin()->wstring();
            const bool vendor=relative.parent_path().empty()&&relative.extension()==L".dll"&&filename.rfind(L"starlight.",0)!=0;
            const bool notice=first==L"licenses"&&filename!=L"starlight-license.txt"&&filename!=L"third-party-notices.md";
            if(!vendor&&!notice&&first!=L"D3D12")throw std::runtime_error("Runtime dependency could replace a built program");
            const auto source=fs::weakly_canonical(root/fs::u8path(name));
            const auto sourceRelative=source.lexically_relative(fs::weakly_canonical(root/L"runtime/Release"));
            if(sourceRelative.empty()||sourceRelative.is_absolute())throw std::runtime_error("Invalid runtime source");
            for(const auto& part:sourceRelative)if(part==L"..")throw std::runtime_error("Runtime dependency source is redirected");
            std::string expected;
            for(const auto& entry:package.at("files"))if(entry.at("path")==name)expected=entry.at("sha256").get<std::string>();
            if(expected.empty()||_stricmp(hash_file(source).c_str(),expected.c_str())!=0)throw std::runtime_error("Runtime dependency hash mismatch: "+name);
            const auto destination=fs::weakly_canonical(destinationRoot/relative);
            auto targetRelative=destination.lexically_relative(destinationRoot);
            if(targetRelative.empty()||targetRelative.is_absolute())throw std::runtime_error("Invalid runtime destination");
            for(const auto& part:targetRelative)if(part==L"..")throw std::runtime_error("Runtime dependency destination is redirected");
            fs::create_directories(destination.parent_path());
            fs::copy_file(source,destination,fs::copy_options::overwrite_existing);
            if(_stricmp(hash_file(destination).c_str(),expected.c_str())!=0)throw std::runtime_error("Copied runtime dependency hash mismatch");
            copied.push_back({{"path",name},{"sha256",expected}});
        }
        atomic_write(logs/L"runtime-dependencies.json",copied.dump(2));
    }
    if(test)execute("test",ctest,{L"--test-dir",build.wstring(),L"-C",config,L"--output-on-failure"});
    // Never compact a failed configure, build, or requested test run.
    if(compact)execute("compact",build/config/L"Starlight.NativeCleanup.exe",{L"build",root.wstring(),build.wstring(),L"--apply"});
    std::cout<<"Native build completed. "<<utf8(logs.wstring())<<'\n';
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"Native build failed: "<<error.what()<<'\n';return 1;
}
