// A project's module must be a plain file name inside the project folder (audit H1): a .star file from elsewhere must not be able to name any DLL on the disk.
#include "../../deep/src/project_module.hpp"
#include "../../deep/engine/keycard_view.h"
static void project_module_checks(const auto& require){
    const std::filesystem::path folder=L"C:\\Projects\\Demo";
    auto refused=[&](const wchar_t* name){try{(void)project_module_path(folder,name);return false;}catch(const std::runtime_error&){return true;}};
    require(project_module_path(folder,L"Empty.dll")==std::filesystem::path(L"C:\\Projects\\Demo\\Empty.dll"),"H1: A plain module file name resolves inside the project folder");
    require(refused(L"..\\x.dll")&&refused(L"../x.dll")&&refused(L"..\\..\\Windows\\x.dll"),"H1: A module path that climbs out of the project folder is refused");
    require(refused(L"C:\\x.dll")&&refused(L"C:/x.dll")&&refused(L"\\\\server\\share\\x.dll")&&refused(L"\\x.dll"),"H1: An absolute or network module path is refused");
    require(refused(L"sub\\x.dll")&&refused(L"sub/x.dll"),"H1: A module path with a folder part is refused");
    require(refused(L"")&&refused(L".")&&refused(L".."),"H1: An empty or dot module name is refused");
    require(refused(L"Empty.dll:stream"),"H1: A module name with an alternate stream suffix is refused");
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
}
