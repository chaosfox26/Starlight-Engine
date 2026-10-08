// Editor state edges found by the 2026-10-08 editor audit: a scene load reports the hash of the bytes it decoded, finding 7.
#include "../../editor/cpp/object_template.hpp"
static void editor_state_checks(const auto& require,const fs::path& folder){
    const auto work=folder/"editor-state-checks";fs::remove_all(work);fs::create_directories(work);
    {
        SceneData scene;scene.Name="first";
        const auto file=work/"scene.starscene";
        save_scene(scene,file);
        const auto firstHash=hash_file(file);
        std::string loadedHash;
        const auto loaded=load_scene(file,&loadedHash);
        require(loaded.Name=="first"&&loadedHash==firstHash,"Editor audit 7: load_scene reports the hash of the bytes it decoded");
        scene.Name="second";save_scene(scene,file);
        require(hash_file(file)!=loadedHash&&loadedHash==firstHash,"Editor audit 7: a later write does not change the hash reported for the scene that was loaded");
        require(load_scene(file).Name=="second","Editor audit 7: load_scene without a hash argument still loads");
    }
}
