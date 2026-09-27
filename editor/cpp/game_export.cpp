#include "platform.hpp"
#include "adventure.hpp"
namespace starlight::editor {
json export_playable_game(const SceneData& source,const fs::path& destination,const fs::path& runtime){
    source.validate();if(!source.Game)throw std::runtime_error("Enable adventure controls before exporting a game");
    auto findings=adventure_readiness(source);
    const std::vector<std::wstring> required={L"Starlight.NativePlayer.exe",L"Starlight.Viewport.dll",L"Starlight.Physics.dll",L"msvcp140.dll",L"vcruntime140.dll",L"vcruntime140_1.dll"};
    for(const auto& name:required)if(!fs::is_regular_file(runtime/name))throw std::runtime_error("Build the standalone player and runtime files before exporting: "+utf8(name));
    auto target=fs::absolute(destination).lexically_normal();if(fs::exists(target))throw std::runtime_error("Choose a new game folder; existing exports are never overwritten");
    fs::create_directories(target.parent_path());auto stage=target.parent_path()/(L".starlight-game-"+wide(new_id()));
    if(!fs::create_directory(stage))throw std::runtime_error("Cannot create game staging folder");
    try{
        export_portable_scene(source,stage/L"content");verify_portable_scene(stage/L"content");
        json files=json::array();
        auto copy=[&](const fs::path& name){fs::copy_file(runtime/name,stage/name);files.push_back({{"path",utf8(name.wstring())},{"sha256",hash_file(stage/name)}});};
        for(const auto& name:required)copy(name);
        for(const auto& name:{L"msvcp140_1.dll",L"msvcp140_2.dll",L"msvcp140_atomic_wait.dll",L"msvcp140_codecvt_ids.dll",L"concrt140.dll"})if(fs::is_regular_file(runtime/name))copy(name);
        atomic_write(stage/L"README.txt","Launch Starlight.NativePlayer.exe. W A S D: move. Space: jump/double jump. Shift: sprint. V: dodge (hold to lunge). Control: crouch/prone/slide. G: grapple. Tab: target. 1: attack. Right-drag: turn. Left-drag: orbit. Wheel: zoom. E/F: interact. Escape: pause. F5: restart. F8: toggle frame and motion monitor. Slots2-5/R are reserved and disabled.\nWindows x64 with Direct3D 12 graphics is required. Keep this entire folder together.\n");
        atomic_write(stage/L"game-manifest.json",json{{"format","starlight-playable-game"},{"version",1},{"platform","windows-x64"},{"files",files},{"scope","Shared adventure movement and combat runtime; native resolution"}}.dump(2));
        atomic_write(stage/L"gameplay-check.json",json{{"findings",findings},{"scope","Missing key links, finish presence, moving interactions and spawn overlap; not a solvability proof"}}.dump(2));
        fs::rename(stage,target);return {{"success",true},{"folder",utf8(target.wstring())},{"runtimeFiles",files.size()},{"gameplayFindings",findings}};
    }catch(...){std::error_code ignored;fs::remove_all(stage,ignored);throw;}
}
}
