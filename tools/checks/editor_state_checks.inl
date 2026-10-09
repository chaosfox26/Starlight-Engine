// Editor state edges found by the 2026-10-08 editor audit: a scene load reports the hash of the bytes it decoded (finding 7) and deleting an object names the rules left pointing at it (finding 11).
#include "../../editor/cpp/object_template.hpp"
#include "source_text.hpp"
#include <atomic>
#include <chrono>
#include <thread>
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
        // The file is replaced over and over while it is opened: whichever bytes load_scene decoded, the hash it reports must be the hash of those same bytes.
        SceneData a;a.Name="race-a";SceneData b;b.Name="race-b";
        const std::string bytesA=a.encode(),bytesB=b.encode();
        const auto hashA=hash_bytes(bytesA),hashB=hash_bytes(bytesB);
        const auto file=work/"race.starscene";
        atomic_write(file,bytesA);
        std::atomic<bool> stop{false};
        // The writer swaps the file in whenever no reader has it open, so a reader that opens the file twice sees the swap between its two reads.
        std::thread writer([&]{
            for(unsigned n=0;!stop;++n){
                const auto staged=work/("race-"+std::to_string(n&1)+".staged");
                {std::ofstream out(staged,std::ios::binary);out<<((n&1)?bytesA:bytesB);}
                while(!stop&&!MoveFileExW(staged.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING)){}
            }
        });
        size_t loads=0,mismatches=0;bool sawA=false,sawB=false;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(std::chrono::steady_clock::now()<deadline&&mismatches==0&&(loads<3000||!sawA||!sawB)){
            try{
                std::string hash;const auto loaded=load_scene(file,&hash);++loads;
                const bool isA=loaded.Name=="race-a";sawA=sawA||isA;sawB=sawB||!isA;
                if(hash!=(isA?hashA:hashB))++mismatches;
            }catch(const std::exception&){}
        }
        stop=true;writer.join();        require(sawA&&sawB,"Editor audit 7: the replaced scene file was opened in both of its versions");
        require(mismatches==0,"Editor audit 7: a scene replaced while it is opened never gets the hash of the other version");
    }
    // Open and --scene run inside the editor window, which this program cannot drive: they must keep the hash load_scene returned instead of reading the file a second time.
    require(source_occurrences("editor/cpp/editor_main.cpp","diskHash = loadedHash")==2&&source_occurrences("editor/cpp/editor_main.cpp","diskHash = hash_file(")==0&&source_occurrences("editor/cpp/editor_main.cpp","load_scene(path, hash)")==1,"Editor audit 7: Open and --scene keep the hash of the bytes that were decoded");
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
    {
        // The --commands remove operation reports the rules it leaves pointing at nothing, once each, and says nothing when there are none.
        Character lever;lever.Shape="cube";lever.Name="Lever";
        Character other;other.Shape="cube";other.Name="Other";
        SceneData scene;scene.Characters={lever,other};
        GameplayRule rule;rule.Name="Press the lever";rule.Subject=lever.Id;rule.Actions={{"complete-objective","lever-pressed"}};
        scene.Rules={rule};
        const auto file=work/"rules.starscene";save_scene(scene,file);
        auto removeOne=[&](const std::string& id){
            json request={{"schemaVersion",1},{"scenePath",utf8(fs::absolute(file).wstring())},{"expectedSha256",hash_file(file)},
                {"operations",json::array({json{{"op","remove"},{"id",id}}})}};
            atomic_write(work/"request.json",request.dump());
            return apply_commands(work/"request.json");
        };
        const auto unrelated=removeOne(other.Id);
        require(unrelated.value("success",false)&&!unrelated.contains("rulesNamingRemovedObjects"),"Editor audit 11: removing an object no rule names adds no rule warning to the --commands result");
        const auto named=removeOne(lever.Id);
        require(named.contains("rulesNamingRemovedObjects")&&named["rulesNamingRemovedObjects"]==json::array({"Press the lever"}),"Editor audit 11: removing an object a rule names lists that rule in the --commands result");
    }
    // The editor's delete handler shows the same list in its status line; this program cannot press Delete in the window.
    require(source_occurrences("editor/cpp/editor_main.cpp","rules_naming_objects(scene, ids)")==1&&source_occurrences("editor/cpp/editor_main.cpp","These gameplay rules still point at a deleted object")==1,"Editor audit 11: the editor's delete handler reports the rules left pointing at the deleted objects");
}
