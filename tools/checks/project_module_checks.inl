// A project's module must be a plain file name inside the project folder (audit H1): a .star file from elsewhere must not be able to name any DLL on the disk.
#include "../../deep/src/project_module.hpp"
#include "../../deep/engine/keycard_view.h"
#include "../../deep/engine/smooth_track.h"
static void project_module_checks(const auto& require,const std::filesystem::path& scratch){
    const std::filesystem::path folder=L"C:\\Projects\\Demo";
    auto refused=[&](const wchar_t* name){try{(void)project_module_path(folder,name);return false;}catch(const std::runtime_error&){return true;}};
    require(project_module_path(folder,L"Empty.dll")==std::filesystem::path(L"C:\\Projects\\Demo\\Empty.dll"),"H1: A plain module file name resolves inside the project folder");
    require(refused(L"..\\x.dll")&&refused(L"../x.dll")&&refused(L"..\\..\\Windows\\x.dll"),"H1: A module path that climbs out of the project folder is refused");
    require(refused(L"C:\\x.dll")&&refused(L"C:/x.dll")&&refused(L"\\\\server\\share\\x.dll")&&refused(L"\\x.dll"),"H1: An absolute or network module path is refused");
    require(refused(L"sub\\x.dll")&&refused(L"sub/x.dll"),"H1: A module path with a folder part is refused");
    require(refused(L"")&&refused(L".")&&refused(L".."),"H1: An empty or dot module name is refused");
    require(refused(L"Empty.dll:stream"),"H1: A module name with an alternate stream suffix is refused");
    {
        // 10-security 6: the module must exist as a plain file in the project folder, and a project file's own arguments follow the command-line allow-list.
        const auto project=scratch/"project-module";std::filesystem::create_directories(project/"folder.dll");
        { std::ofstream(project/"Real.dll") << "x"; }
        auto fileRefused=[&](const wchar_t* name){try{(void)project_module_file(project,name);return false;}catch(const std::runtime_error&){return true;}};
        require(project_module_file(project,L"Real.dll")==(project/"Real.dll").lexically_normal(),"10-security 6: An existing plain module file is accepted");
        require(fileRefused(L"Missing.dll"),"10-security 6: A module file that does not exist is refused");
        require(fileRefused(L"folder.dll"),"10-security 6: A module that is a folder is refused");
        require(fileRefused(L"..\\Real.dll"),"10-security 6: A module outside the project folder is still refused");
        std::error_code linked;std::filesystem::create_symlink(project/"Real.dll",project/"Link.dll",linked);
        if(!linked)require(fileRefused(L"Link.dll"),"10-security 6: A module that is a symbolic link is refused");
        else require(true,"10-security 6: Symbolic links cannot be created in this session, so the link case was not exercised");
        std::vector<std::wstring> out;
        append_output_arguments({L"--report",L"r.json",L"--shot",L"s.png"},out);
        require(out==std::vector<std::wstring>({L"--report",L"r.json",L"--shot",L"s.png"}),"10-security 6: Output-destination arguments pass the filter in order");
        auto argumentsRefused=[&](std::vector<std::wstring> in){std::vector<std::wstring> sink;try{append_output_arguments(in,sink);return false;}catch(const std::runtime_error&){return true;}};
        require(argumentsRefused({L"--res",L"640x480"})&&argumentsRefused({L"--bench",L"1"})&&argumentsRefused({L"--report",L"r.json",L"--fg"}),"10-security 6: Any option outside the allow-list is refused");
        require(argumentsRefused({L"--log"}),"10-security 6: An output option with no value is refused");
        std::vector<std::wstring> none;append_output_arguments({},none);
        require(none.empty(),"10-security 6: A project with no arguments adds none");
    }
    {   // PROJ-4: the saved default test settings are found from the build tree, the staged package and the old deep/bin layout
        namespace fs=std::filesystem;
        const fs::path scratch=fs::temp_directory_path()/(L"starlight-default-preset-"+std::to_wstring(GetCurrentProcessId()));
        std::error_code ignored;fs::remove_all(scratch,ignored);
        auto put=[&](const fs::path& file){fs::create_directories(file.parent_path());std::ofstream(file)<<"{}";};
        put(scratch/L"source/deep/settings/default.startest");
        put(scratch/L"package/deep/settings/default.startest");
        put(scratch/L"old/deep/settings/default.startest");fs::create_directories(scratch/L"old/deep/bin");
        fs::create_directories(scratch/L"source/out/classic/Release");fs::create_directories(scratch/L"package/runtime/Release");
        require(find_default_preset(scratch/L"source/out/classic/Release")==scratch/L"source/deep/settings/default.startest","PROJ-4: A host in the build tree finds deep/settings/default.startest of the source tree");
        require(find_default_preset(scratch/L"package/runtime/Release")==scratch/L"package/deep/settings/default.startest","PROJ-4: A host in the staged package finds deep/settings/default.startest of the package");
        require(find_default_preset(scratch/L"old/deep/bin")==scratch/L"old/deep/settings/default.startest","PROJ-4: A host in the old deep/bin layout still finds its settings");
        fs::create_directories(scratch/L"none/a");
        require(find_default_preset(scratch/L"none/a").empty(),"PROJ-4: A host with no saved default settings gets an empty path, not a made-up one");
        require(default_preset_candidates(scratch/L"none/a").front()==scratch/L"none/a/settings/default.startest","PROJ-4: The nearest folder is searched first");
        fs::remove_all(scratch,ignored);
    }
    {   // PROJ-3: the key card lookup holds paths as UTF-8, so a folder named outside the ANSI code page is found
        namespace fs=std::filesystem;
        const fs::path folder=fs::temp_directory_path()/(L"Starlight Moved Ω "+std::to_wstring(GetCurrentProcessId()));
        std::error_code ignored;fs::remove_all(folder,ignored);fs::create_directories(folder);
        std::ofstream(folder/L"Empty.starscene")<<"x";
        const std::string utf8=keycard::pathToUtf8(folder);
        require(utf8.find("\xCE\xA9")!=std::string::npos,"PROJ-3: A folder path converts to UTF-8");
        require(keycard::pathFromUtf8(utf8)==folder,"PROJ-3: A UTF-8 folder path converts back to the same path");
        std::string error;
        const std::string beside=keycard::resolveCardPath("",utf8,"Empty.starscene",error);
        require(error.empty()&&!beside.empty()&&fs::is_regular_file(keycard::pathFromUtf8(beside)),"PROJ-3: The card beside a module in a non-ANSI folder is found");
        const std::string named=keycard::resolveCardPath(beside,"","Empty.starscene",error);
        require(error.empty()&&named==beside,"PROJ-3: A --card path in a non-ANSI folder is found");
        fs::remove_all(folder,ignored);
    }
    {   // PROJ-9: ids that stop being tracked (spent projectiles, departed players) leave the smoothing map, tracked ones stay
        std::unordered_map<uint64_t,SmoothXf> map;
        for(uint64_t id=0;id<1000;++id){SmoothXf s;s.init=true;s.seen=id<10?5000:100;map[id]=s;}
        pruneSmooth(map,5000);
        require(map.size()==10,"PROJ-9: Entries untouched for longer than the maximum age are removed and the recently tracked ones are kept");
        pruneSmooth(map,5000+kSmoothMaxAgeFrames);
        require(map.size()==10,"PROJ-9: An entry exactly at the maximum age is kept");
        pruneSmooth(map,5000+kSmoothMaxAgeFrames+1);
        require(map.empty(),"PROJ-9: An entry past the maximum age is removed");
        SmoothXf late;late.seen=UINT32_MAX-5;map[1]=late;
        pruneSmooth(map,10);
        require(map.size()==1,"PROJ-9: The age is measured across the frame counter wrapping around");
    }
    {   // RND-1: a snapshot far enough from the tracked state to overflow float snaps to it, and the entity never turns NaN
        SmoothXf s;
        s.follow(-3e38f,-3e38f,0,-3e38f,1.f);s.follow(3e38f,3e38f,0,3e38f,1.f);
        require(std::isfinite(s.x)&&std::isfinite(s.z)&&std::isfinite(s.yaw)&&s.x==3e38f&&s.z==3e38f,"RND-1: A snapshot far from the tracked state snaps to it instead of overflowing");
        s.follow(3e38f,3e38f,0,3e38f,1.f);
        require(std::isfinite(s.x)&&std::isfinite(s.z)&&std::isfinite(s.yaw)&&s.x==3e38f,"RND-1: The tracked state stays finite after a far snapshot");
    }
}
