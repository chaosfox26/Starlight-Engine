// Migration inventory output paths stay beneath the stage and archive roots (audit DEP-08).
#include "confined_relative.hpp"
static void migration_path_checks(const auto& require){
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
}
