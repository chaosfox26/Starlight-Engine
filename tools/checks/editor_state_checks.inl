// Editor state edges found by the 2026-10-08 editor audit: a scene load reports the hash of the bytes it decoded (finding 7) and deleting an object names the rules left pointing at it (finding 11).
#include "../../editor/cpp/object_template.hpp"
#include "../../editor/cpp/commands.hpp"
#include <regex>
#include <set>
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
        const auto object=[](const char* id,const char* role="none",const char* key=""){Character c;c.Id=id;c.GameplayRole=role;c.GameplayKey=key;return c;};
        const auto none=rules_naming_objects(scene,{object("unrelated")});
        require(none.empty(),"Editor audit 11: rules that name no deleted object are not reported");
        const auto both=rules_naming_objects(scene,{object("lever"),object("vault")});
        require(both.size()==2&&both[0]=="Press the lever"&&both[1]=="Open the vault","Editor audit 11: a rule is reported when its subject or its open-door target is deleted");
        require(rules_naming_objects(scene,{object("vault")}).size()==1,"Editor audit 11: an objective named like an object is not an object link");
        require(rules_naming_objects(scene,{object("")}).empty(),"Editor audit 11: a rule with no subject is not matched by an empty id");
        GameplayRule byKey;byKey.Name="Pick up the brass key";byKey.Event="item-collected";byKey.Subject="brass";
        GameplayRule byKeyOpened;byKeyOpened.Name="Door opened";byKeyOpened.Event="object-opened";byKeyOpened.Subject="brass";
        scene.Rules={byKey,byKeyOpened};
        const auto keyed=rules_naming_objects(scene,{object("gem1","key","brass")});
        require(keyed.size()==1&&keyed[0]=="Pick up the brass key","Editor audit 11: an item-collected rule is reported when the key object it waits for is deleted");
        require(rules_naming_objects(scene,{object("gem1","none","brass")}).empty(),"Editor audit 11: only key objects carry a collectable key");
        scene.Characters={object("gem2","key","brass")};
        require(rules_naming_objects(scene,{object("gem1","key","brass")}).empty(),"Editor audit 11: a rule is not reported while another key object still carries its key");
    }
    {
        // Editor audit 5: the windowed executable runs a command instead of opening a window for every command flag the interface documents.
        std::set<std::string> advertised;
        const std::regex flag("--[a-z][a-z-]*");
        const auto text=describe_commands().dump();
        for(std::sregex_iterator it(text.begin(),text.end(),flag),end;it!=end;++it)advertised.insert(it->str());
        for(const char* option:{"--result","--writable","--read-only","--label"})advertised.erase(option);
        bool every=advertised.size()>=10;std::string missed;
        for(const auto& name:advertised)if(!is_command_flag(wide(name))){every=false;missed+=name+" ";}
        if(!every)std::fprintf(stderr,"Editor audit 5: not routed: %s\n",missed.c_str());
        require(every,"Editor audit 5: every command flag in the interface description is routed by the windowed executable");
        require(!is_command_flag(L"--scene")&&!is_command_flag(L"--ui-check")&&!is_command_flag(L"--play"),"Editor audit 5: windowed-only flags still open the window");
    }
}
