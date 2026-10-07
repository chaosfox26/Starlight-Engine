// A project's module must be a plain file name inside the project folder (audit H1): a .star file from elsewhere must not be able to name any DLL on the disk.
#include "../../deep/src/project_module.hpp"
static void project_module_checks(const auto& require){
    const std::filesystem::path folder=L"C:\\Projects\\Demo";
    auto refused=[&](const wchar_t* name){try{(void)project_module_path(folder,name);return false;}catch(const std::runtime_error&){return true;}};
    require(project_module_path(folder,L"Empty.dll")==std::filesystem::path(L"C:\\Projects\\Demo\\Empty.dll"),"H1: A plain module file name resolves inside the project folder");
    require(refused(L"..\\x.dll")&&refused(L"../x.dll")&&refused(L"..\\..\\Windows\\x.dll"),"H1: A module path that climbs out of the project folder is refused");
    require(refused(L"C:\\x.dll")&&refused(L"C:/x.dll")&&refused(L"\\\\server\\share\\x.dll")&&refused(L"\\x.dll"),"H1: An absolute or network module path is refused");
    require(refused(L"sub\\x.dll")&&refused(L"sub/x.dll"),"H1: A module path with a folder part is refused");
    require(refused(L"")&&refused(L".")&&refused(L".."),"H1: An empty or dot module name is refused");
    require(refused(L"Empty.dll:stream"),"H1: A module name with an alternate stream suffix is refused");
}
