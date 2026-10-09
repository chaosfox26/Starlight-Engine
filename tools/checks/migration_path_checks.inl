// Migration inventory output paths stay beneath the stage and archive roots (audit DEP-08).
#include "confined_relative.hpp"
static void migration_path_checks(const auto& require,const fs::path& folder){
    auto refused=[](const std::string& text){
        try{confined_relative(text);}catch(const std::runtime_error&){return true;}
        return false;
    };
    require(!refused("scenes/a.scene.json"),"DEP-08: a plain relative path is accepted");
    require(confined_relative("a.json")=="a.json","DEP-08: an accepted path is returned unchanged");
    require(refused(""),"DEP-08: an empty path is refused");
    require(refused("..\\x.json")&&refused("a/../../x.json")&&refused("a/.."),"DEP-08: a path that climbs out with .. is refused");
    require(refused("C:\\x.json")&&refused("C:x.json")&&refused("\\\\server\\share\\x.json"),"DEP-08: a drive or network path is refused");
    require(refused("/x.json")&&refused("\\x.json"),"DEP-08: a rooted path is refused");
    require(refused("a/b:stream")&&refused("a:b"),"DEP-08: a path with a colon (drive or alternate stream) is refused");
    const fs::path root=folder/L"migration-path-links";
    fs::create_directories(root/L"stage"/L"real");
    fs::create_directories(root/L"outside");
    const std::string link=utf8((root/L"stage"/L"link").wstring()), outside=utf8((root/L"outside").wstring());
    const bool made=std::system(("cmd /c mklink /J \""+link+"\" \""+outside+"\" >nul 2>&1").c_str())==0;
    require(made,"DEP-08: the junction used by the link check can be created");
    auto refusedLink=[&](const fs::path& output){
        try{refuse_links_under(root/L"stage",output);}catch(const std::runtime_error&){return true;}
        return false;
    };
    require(refusedLink(root/L"stage"/L"link"/L"scene.starscene"),"DEP-08: an output that passes through a junction under the stage is refused");
    require(refusedLink(root/L"stage"/L"link"),"DEP-08: a junction that is itself the output folder is refused");
    require(!refusedLink(root/L"stage"/L"real"/L"scene.starscene"),"DEP-08: an output in a plain folder under the stage is accepted");
    require(!refusedLink(root/L"stage"/L"missing"/L"scene.starscene"),"DEP-08: an output in a folder that does not exist yet is accepted");
    std::system(("cmd /c rmdir \""+link+"\" >nul 2>&1").c_str());
}
