// Editor state edges found by the 2026-10-08 editor audit: a scene load reports the hash of the bytes it decoded (finding 7) and deleting an object names the rules left pointing at it (finding 11).
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
    {
        SceneData scene;
        GameplayRule bySubject;bySubject.Name="Press the lever";bySubject.Subject="lever";
        GameplayRule byDoor;byDoor.Name="Open the vault";byDoor.Actions={{"open-door","vault"}};
        GameplayRule byObjective;byObjective.Name="Collect cells";byObjective.Actions={{"complete-objective","vault"}};
        GameplayRule anywhere;anywhere.Name="Whole level";
        scene.Rules={bySubject,byDoor,byObjective,anywhere};
        const auto none=rules_naming_objects(scene,{"unrelated"});
        require(none.empty(),"Editor audit 11: rules that name no deleted object are not reported");
        const auto both=rules_naming_objects(scene,{"lever","vault"});
        require(both.size()==2&&both[0]=="Press the lever"&&both[1]=="Open the vault","Editor audit 11: a rule is reported when its subject or its open-door target is deleted");
        require(rules_naming_objects(scene,{"vault"}).size()==1,"Editor audit 11: an objective named like an object is not an object link");
        require(rules_naming_objects(scene,{""}).empty(),"Editor audit 11: a rule with no subject is not matched by an empty id");
    }
}
