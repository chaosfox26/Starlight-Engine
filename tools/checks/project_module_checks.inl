// A project's module must be a plain file name inside the project folder (audit H1): a .star file from elsewhere must not be able to name any DLL on the disk.
#include "../../deep/src/project_module.hpp"
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
}