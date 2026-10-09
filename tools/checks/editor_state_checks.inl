// Editor state edges found by the 2026-10-08 editor audit: a scene load reports the hash of the bytes it decoded (finding 7) and deleting an object names the rules left pointing at it (finding 11).
#include "../../editor/cpp/object_template.hpp"
#include "source_text.hpp"
#include <atomic>
#include <chrono>
#include <thread>
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
        // Editor audit 4: the recovery copy sits beside the scene under a fixed name, is the only file the autosave touches, and never replaces the scene.
        SceneData scene;scene.Name="saved";
        const auto file=work/"recover.starscene";
        save_scene(scene,file);
        const auto savedHash=hash_file(file);
        require(autosave_path(file)==fs::path(file.wstring()+L".autosave")&&autosave_path(file)!=file&&autosave_path(file).parent_path()==file.parent_path(),"Editor audit 4: the autosave is named <scene>.autosave in the scene's folder");
        bool refused=false;try{(void)autosave_path(fs::path());}catch(const std::exception&){refused=true;}
        require(refused,"Editor audit 4: a scene with no file name gets no autosave path");
        scene.Name="edited, not saved";
        write_autosave(scene,file);
        require(hash_file(file)==savedHash,"Editor audit 4: writing an autosave leaves the scene file byte-identical");
        require(fs::exists(autosave_path(file))&&SceneData::decode(read_file(autosave_path(file))).Name=="edited, not saved","Editor audit 4: the autosave holds the unsaved edits");
        scene.Name="edited again";write_autosave(scene,file);
        require(SceneData::decode(read_file(autosave_path(file))).Name=="edited again"&&hash_file(file)==savedHash,"Editor audit 4: a later autosave replaces only the earlier autosave");
        std::vector<std::string> names;for(const auto& entry:fs::directory_iterator(work))names.push_back(entry.path().filename().string());
        require(std::count(names.begin(),names.end(),std::string("recover.starscene.autosave"))==1&&std::none_of(names.begin(),names.end(),[](const std::string& name){return name.ends_with(".lock")||name.ends_with(".tmp");}),"Editor audit 4: an autosave leaves no lock or temporary file behind");
        remove_autosave(file);
        require(!fs::exists(autosave_path(file))&&hash_file(file)==savedHash,"Editor audit 4: removing the autosave leaves the scene");
        remove_autosave(work/"never-existed.starscene");
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
    {
        // Editor audit 8 follow-up: a failed Save while the window closes or Windows ends the session must keep the window open. An exception that leaves the window procedure lets DefWindowProc destroy the window, and the edits go with it.
        const auto closing=source_span("editor/cpp/editor_main.cpp","case WM_CLOSE:","KillTimer(window, 1);");
        const auto ending=source_span("editor/cpp/editor_main.cpp","case WM_QUERYENDSESSION:","case WM_CLOSE:");
        require(closing.find("catch (const std::exception")!=std::string::npos,"Editor audit 8: a failed save while the window closes keeps the window and its edits");
        require(ending.find("catch (const std::exception")!=std::string::npos,"Editor audit 8: a failed save while Windows ends the session keeps the session open");
    }
    {
        // Editor audit 6 and 8 follow-up: the warnings about a missing or externally changed scene file must stay on the status line. The once-a-second watcher must not write them there, because the frame-rate line overwrites that text within half a second.
        const auto watcher=source_span("editor/cpp/editor_main.cpp","watchTick = now;","diagnostic(\"scene watcher\"");
        const auto line=source_span("editor/cpp/editor_main.cpp","if (telemetryTick && now - telemetryTick < 500) return;","SetWindowTextW(status, wide(out.str())");
        require(!watcher.empty()&&watcher.find("SetWindowTextW(status")==std::string::npos,"Editor audit 8: the scene watcher leaves the status line to the one writer that keeps the warning visible");
        require(line.find("watchMissing")!=std::string::npos&&line.find("watchChanged")!=std::string::npos,"Editor audit 8: the status line shows the missing-file and changed-file warnings while they apply");
    }
    // The editor's delete handler shows the same list in its status line; this program cannot press Delete in the window.
    require(source_occurrences("editor/cpp/editor_main.cpp","rules_naming_objects(scene, removed)")==1&&source_occurrences("editor/cpp/editor_main.cpp","These gameplay rules still point at a deleted object")==1,"Editor audit 11: the editor's delete handler reports the rules left pointing at the deleted objects");
}
