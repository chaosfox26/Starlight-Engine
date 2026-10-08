#include "adventure.hpp"
#include "scene_preparation.hpp"
#include "player_presentation.hpp"
#include "gameplay_audio.hpp"
#include "movement_hud.hpp"
#include "combat_hud.hpp"
#include "quest_tracker.hpp"
#include "startup_activation.hpp"
#include "native_session.hpp"
#include "pacing_hud.hpp"
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>
#pragma comment(lib,"dwmapi.lib")
using namespace starlight::editor;
namespace {
struct Player {
    HWND window{},view{},hud{};
    QuestTracker questTracker;
    MovementHud movementHud;
    GameplayAudio gameplayAudio;
    CombatHud combatHud;
    PacingHud pacingHud;bool pacingShown=true;
    HBRUSH background=CreateSolidBrush(RGB(20,25,42));
    ~Player(){if(background)DeleteObject(background);}
    SceneData authored;
    AdventurePlayer gameplay;
    std::unique_ptr<NativeSession> renderer;
    bool paused=false,looking=false;
    bool gameplayCheck=false;double checkRight=0,checkForward=0;
    std::function<void()> gameplayObserver;
    fs::path smokeOutput;ULONGLONG smokeDeadline=0;int smokeResult=1;
    fs::path combatCheckOutput;ULONGLONG combatCheckStart=0;bool combatCheckStarted=false,combatSawCone=false,combatSawDamage=false;
    POINT last{};ULONGLONG tick=0;
    std::chrono::steady_clock::time_point motionTick=std::chrono::steady_clock::now();
    ULONGLONG hudTick=0;
    PreparationReport lastPreparation;
    // Every start or restart goes through the same preparation as the editor: loading screen, shared cache warm-up, collision, break-ahead rock fracture, upload.
    fs::path scenePath;
    bool loadingActive=false;
    void fail_loading(LoadingScreen& loading,const PreparationReport& report){
        renderer->prewarm_play(false);
        loading.update(loading.fraction(),"Loading did not finish: "+report.reason);
        if(!report.cancelled)MessageBoxW(window,wide("The scene could not finish loading. "+report.reason).c_str(),L"Starlight",MB_OK|MB_ICONERROR);
        loading.close();loadingActive=false;
        if(!report.cancelled)PostMessageW(window,WM_CLOSE,0,0);
        release_deferred_close();
    }
    bool prepare_and_start(LoadingScreen& loading){
        const auto statusBefore=renderer->status();
        renderer->playback(false);renderer->prewarm_play(true); // the barrier installs the scene play will use; playback(true) below ends the prewarm
        questTracker.reset();gameplay.start(authored);
        lastPreparation=prepare_scene(gameplay,loading);
        if(lastPreparation.stopped()){fail_loading(loading,lastPreparation);return false;}
        gameplay.update_rules();
        renderer->gameplay_time(0);
        renderer->camera({gameplay.camera_eye(),gameplay.view_position()+gameplay.camera_direction(),1,gameplay.camera_up()});
        lastPreparation.sceneGeneration=renderer->scene(gameplay.visible_scene());
        renderer->playback(false,true);
        finish_scene_upload(*renderer,statusBefore,gameplay,loading,lastPreparation);
        if(!lastPreparation.ready()){fail_loading(loading,lastPreparation);return false;}
        renderer->playback(true);
        return true;
    }
    bool launch(bool first){
        struct Scope{Player& player;explicit Scope(Player& p):player(p){player.loadingActive=true;}~Scope(){player.loadingActive=false;}} scope(*this);
        if(first)message(WM_SIZE,0,0);
        LoadingScreen loading;loading.show(window,view);loading.update(0,"Starting your scene");
        paused=true;gameplay.cancel_mobility_input();
        if(first){
            renderer=std::make_unique<NativeSession>(view,1280,720);
            message(WM_SIZE,0,0);
            loading.update(0,"Reading the scene");pump_loading_messages();
            if(fs::exists(scenePath.parent_path()/L"manifest.json"))verify_portable_scene(scenePath.parent_path());
            authored=load_scene(scenePath);
            SetWindowTextW(window,wide(authored.Name).c_str());
            loading.update(0,"Starting your scene");
        }
        if(!prepare_and_start(loading))return false;
        loading.close();loadingActive=false;paused=false;
        SetWindowTextW(hud,L"WASD: walk | Space: jump | Right-drag: look | F / E: use | Escape: pause | F5: restart");
        release_deferred_close();
        return true;
    }
    void reset(){launch(false);}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto p=reinterpret_cast<Player*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<Player*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);p->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);}
        try {if(p)return p->message(m,w,l);}catch(const std::exception& e){diagnostic("standalone player",e.what());if(p->hud)SetWindowTextW(p->hud,wide(e.what()).c_str());p->paused=true;}
        return DefWindowProcW(h,m,w,l);
    }
    LRESULT message(UINT m,WPARAM w,LPARAM l){
        if(renderer && ((m>=WM_KEYFIRST && m<=WM_KEYLAST)||(m>=WM_MOUSEFIRST && m<=WM_MOUSELAST)))renderer->input_event();
        if(m==WM_KILLFOCUS || (m==WM_ACTIVATEAPP && !w))gameplay.cancel_mobility_input();
        if(m==WM_KEYUP && w=='G'){gameplay.grapple_button(false);return 0;}
        if(m==WM_KEYUP && AbilitySlots::recognizes(static_cast<unsigned>(w))){gameplay.ability_button(static_cast<unsigned>(w),false);return 0;}
        if(m==WM_KEYUP && w=='V'){if(!paused)gameplay.mobility_button(false,0,0);return 0;}
        if(m==WM_KEYUP && w==VK_CONTROL){if(!paused)gameplay.posture_button(false,0,0);return 0;}
        if(m==WM_SIZE){RECT r{};GetClientRect(window,&r);if(view){MoveWindow(view,0,0,r.right,std::max(1L,r.bottom-(gameplayCheck?0:36)),TRUE);MoveWindow(hud,12,std::max(0L,r.bottom-30),std::max(1L,r.right-24),24,TRUE);}questTracker.layout(r.right,r.bottom,16,MulDiv(100,GetDpiForWindow(window),96));if(renderer){renderer->size(std::max(1L,r.right),std::max(1L,r.bottom-(gameplayCheck?0:36)));renderer->suspend_rendering(w==SIZE_MINIMIZED);}return 0;}
        if(loadingActive && (m==WM_TIMER || m==NativeSession::frameUpdateMessage)){if(m==NativeSession::frameUpdateMessage && renderer)renderer->frame_update_handled();return 0;}
        if((m==WM_TIMER || m==NativeSession::frameUpdateMessage) && renderer){auto now=GetTickCount64();auto preciseNow=std::chrono::steady_clock::now();double dt=std::clamp(std::chrono::duration<double>(preciseNow-motionTick).count(),0.,.05);motionTick=preciseNow;tick=now;
            if(m==NativeSession::frameUpdateMessage)renderer->frame_update_handled();
            if(!paused && GetForegroundWindow()==window){double x=gameplayCheck?checkRight:((GetAsyncKeyState('D')&0x8000)?1.:0)-((GetAsyncKeyState('A')&0x8000)?1.:0);double z=gameplayCheck?checkForward:((GetAsyncKeyState('W')&0x8000)?1.:0)-((GetAsyncKeyState('S')&0x8000)?1.:0);auto revision=gameplay.rules().revision;if(!gameplayCheck){gameplay.posture_button((GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,x,z,dt);gameplay.mobility_button((GetAsyncKeyState('V')&0x8000)!=0,x,z,dt);gameplay.sprint((GetAsyncKeyState(VK_SHIFT)&0x8000)!=0);}gameplay.move(x,z,dt);if(gameplayObserver)gameplayObserver();renderer->gameplay_time(gameplay.seconds());if(gameplay.rules().revision!=revision)renderer->scene(gameplay.visible_scene(),true);if(!gameplay.rules().message.empty())SetWindowTextW(hud,wide(gameplay.rules().message).c_str());}
            if(now-hudTick>=33){hudTick=now;
            questTracker.update(authored,gameplay.rules(),true);
            movementHud.update(&gameplay.movement_resources(),view,MulDiv(100,GetDpiForWindow(window),96));
            combatHud.update(&gameplay,view,MulDiv(100,GetDpiForWindow(window),96));
            pacingHud.update(pacingShown,*renderer,view,{gameplay.camera_eye(),gameplay.view_position()+gameplay.camera_direction(),1,gameplay.camera_up()},MulDiv(100,GetDpiForWindow(window),96));
            }
            {renderer->player_body(gameplay.body_bounds());renderer->collision_objects(gameplay.environment_objects());gameplay.liquids(renderer->liquid_snapshot());renderer->runtime_objects(gameplay.dynamic_objects());renderer->pacing_debris(gameplay.debris_poses());double started=timing_seconds();auto p=gameplay.presentation();if(pacingShown)renderer->pacing_player(p.feet,{p.feet.x,p.feet.y,p.feet.z,p.facing,p.bodyHeight});auto timing=gameplay.lastTiming;{std::vector<std::shared_ptr<const MeshPart>> preloadParts;if(gameplay.take_prefractured_parts(preloadParts))renderer->rubble_preload(!authored.RaytracingEnabled?std::move(preloadParts):std::vector<std::shared_ptr<const MeshPart>>{});}renderer->rubble_instances(resident_rubble(p,!authored.RaytracingEnabled));renderer->gameplay_overlays(battle_presentation(p,timing.battleWorkers,!authored.RaytracingEnabled,!authored.RaytracingEnabled));timing.presentationMs=(timing_seconds()-started)*1000;renderer->runtime_timing(timing,int(p.actorTelegraphs.size())+int(p.showTelegraph));}
            {auto presentation=gameplay.presentation();gameplayAudio.update(&presentation,!paused && GetForegroundWindow()==window);}
            renderer->camera({gameplay.camera_eye(),gameplay.view_position()+gameplay.camera_direction(),1,gameplay.camera_up()});auto state=renderer->status();if(!state.error.empty()){SetWindowTextW(hud,wide(state.error).c_str());paused=true;}
            if(!combatCheckOutput.empty()){
                auto finish=[&](bool pass,const std::string& error){smokeResult=pass?0:1;atomic_write(combatCheckOutput,json{{"pass",pass},{"error",error},{"foreground",GetForegroundWindow()==window},{"frames",state.frames},{"cone",combatSawCone},{"damage",combatSawDamage},{"scope","Foreground packaged combat channel and enemy exchange; not clean-machine certification"}}.dump(2));SendMessageW(window,WM_CLOSE,0,0);};
                if(GetForegroundWindow()!=window){finish(false,"Foreground lost; stopped without retry");return 0;}
                if(!state.error.empty()){finish(false,state.error);return 0;}
                if(!combatCheckStarted){combatCheckStarted=true;combatCheckStart=now;gameplay.fire_button(true);gameplay.fire_button(false);gameplay.cameraPitch=-.5;}
                combatSawCone|=gameplay.presentation().showTelegraph;
                for(const auto& [id,a]:gameplay.combat_actors())combatSawDamage|=a.health<a.maximum;
                if(now-combatCheckStart>=7000){finish(combatSawCone && combatSawDamage && gameplay.health()<100 && state.frames>=60,"");return 0;}
            }
            if(!smokeOutput.empty() && (state.frames>=60 || !state.error.empty() || now>=smokeDeadline)){
                smokeResult=state.frames>=60 && state.error.empty() ? 0 : 1;
                atomic_write(smokeOutput,json{{"pass",smokeResult==0},{"frames",state.frames},{"error",state.error},{"bloomLevels",state.bloom_levels},{"hdrPostAllocationBytes",state.hdr_post_bytes},{"sunShadowResolution",state.sun_shadow_resolution},{"localShadowResolution",state.local_shadow_resolution},{"localShadowCapacity",state.local_shadow_capacity},{"shadowAllocationBytes",state.shadow_bytes},{"rainParticles",state.rain_particles},{"rainImpacts",state.rain_impacts},{"questCount",questTracker.views().size()},{"firstQuestComplete",!questTracker.views().empty() && questTracker.views()[0].complete},{"completedObjectives",gameplay.rules().objectives},{"remainingCharacters",gameplay.scene().Characters.size()},{"scope","Hidden standalone renderer startup; not visible gameplay or clean-machine certification"}}.dump(2));
                SendMessageW(window,WM_CLOSE,0,0);
            }
            return 0;}
        if(m==WM_KEYDOWN && !(l&(1LL<<30))){
            if(!paused){
                gameplay.movement_press(int(w),GetTickCount64()*.001);
                if(w==VK_CONTROL){gameplay.posture_button(true,((GetAsyncKeyState('D')&0x8000)?1.:0)-((GetAsyncKeyState('A')&0x8000)?1.:0),((GetAsyncKeyState('W')&0x8000)?1.:0)-((GetAsyncKeyState('S')&0x8000)?1.:0));return 0;}
                if(w=='G'){gameplay.grapple_button(true);return 0;}
                if(AbilitySlots::recognizes(static_cast<unsigned>(w))){gameplay.ability_button(static_cast<unsigned>(w),true);return 0;}
                if(w==VK_TAB){gameplay.select_target();return 0;}
                if(w=='V'){gameplay.mobility_button(true,((GetAsyncKeyState('D')&0x8000)?1.:0)-((GetAsyncKeyState('A')&0x8000)?1.:0),((GetAsyncKeyState('W')&0x8000)?1.:0)-((GetAsyncKeyState('S')&0x8000)?1.:0));return 0;}
            }
            if(w==VK_SPACE && !paused && !(l & (1LL<<30))){gameplay.jump();return 0;}
            if(w==VK_ESCAPE){gameplay.cancel_mobility_input();paused=!paused;renderer->playback(!paused);SetWindowTextW(hud,paused?L"Paused. Escape resumes; F5 restarts.":L"WASD: walk | Space: jump | Right-drag: look | F / E: use | F5: restart");}
            else if(w==VK_F8){pacingShown=!pacingShown;renderer->pacing_enabled(pacingShown);}
            else if(w==VK_F5)reset();
            else if((w=='E' || w=='F') && !paused){auto count=gameplay.scene().Characters.size();auto message=gameplay.interact();if(count!=gameplay.scene().Characters.size())renderer->scene(gameplay.visible_scene(),true);SetWindowTextW(hud,wide(message).c_str());}
            return 0;}
        if(m==WM_MOUSEWHEEL){if(!paused)gameplay.zoom(GET_WHEEL_DELTA_WPARAM(w)/120.);return 0;}
        if(m==WM_RBUTTONDOWN || m==WM_LBUTTONDOWN){looking=true;last={GET_X_LPARAM(l),GET_Y_LPARAM(l)};SetCapture(window);return 0;}
        if(m==WM_MOUSEMOVE && looking && !paused){POINT q{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(w&MK_RBUTTON)gameplay.look(q.x-last.x,q.y-last.y);else if(w&MK_LBUTTON)gameplay.orbit_camera(q.x-last.x,q.y-last.y);last=q;return 0;}
        if(m==WM_RBUTTONUP||m==WM_LBUTTONUP||m==WM_CAPTURECHANGED||m==WM_KILLFOCUS){looking=false;if(GetCapture()==window)ReleaseCapture();return 0;}
        if(m==WM_ERASEBKGND){RECT bounds{};GetClientRect(window,&bounds);FillRect((HDC)w,&bounds,background);return 1;}
        if(m==WM_CTLCOLORSTATIC){SetTextColor((HDC)w,RGB(225,232,247));SetBkColor((HDC)w,RGB(20,25,42));return (LRESULT)background;}
        if(m==WM_CLOSE && loadingActive){preparation_cancel()=true;auto& waiting=preparation_deferred_close();if(std::find(waiting.begin(),waiting.end(),window)==waiting.end())waiting.push_back(window);return 0;}
        if(m==WM_CLOSE){KillTimer(window,1);renderer.reset();DestroyWindow(window);return 0;}
        if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
        return DefWindowProcW(window,m,w,l);
    }
    #include "player_gameplay_checks.inl"
};
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=1;
    try{
        wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        fs::path scenePath=fs::path(executable).parent_path()/L"content"/L"scene.starscene";
        bool validate=false;fs::path smokeOutput,combatCheckOutput,gameplayCheckOutput;for(int i=1;i<argc;i++){
            if(std::wstring_view(argv[i])==L"--validate")validate=true;
            else if(std::wstring_view(argv[i])==L"--smoke"){if(++i>=argc)throw std::runtime_error("Smoke report path missing");smokeOutput=argv[i];}
            else if(std::wstring_view(argv[i])==L"--combat-check"){if(++i>=argc)throw std::runtime_error("Combat report path missing");combatCheckOutput=argv[i];}
            else if(std::wstring_view(argv[i])==L"--gameplay-check"){if(++i>=argc)throw std::runtime_error("Gameplay report path missing");gameplayCheckOutput=argv[i];}
            else scenePath=argv[i];
        }LocalFree(argv);
        Player player;player.combatCheckOutput=combatCheckOutput;player.smokeOutput=smokeOutput;player.smokeDeadline=GetTickCount64()+20000;player.scenePath=scenePath;
        if(validate){if(fs::exists(scenePath.parent_path()/L"manifest.json"))verify_portable_scene(scenePath.parent_path());player.authored=load_scene(scenePath);player.gameplay.start(player.authored);CoUninitialize();return 0;}
        WNDCLASSW cls{};cls.hInstance=instance;cls.lpszClassName=L"StarlightGamePlayer";cls.lpfnWndProc=Player::proc;cls.hCursor=LoadCursor(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);RegisterClassW(&cls);
        auto window=CreateWindowW(cls.lpszClassName,L"Starlight",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1280,800,nullptr,nullptr,instance,&player);
        if(!window)throw std::runtime_error("Cannot create game window");
        BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));
        player.view=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|WS_DISABLED,0,0,1280,720,window,nullptr,instance,nullptr);
        player.hud=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE,12,740,1240,24,window,nullptr,instance,nullptr);
        player.questTracker.create(window);
        player.movementHud.create(window);player.combatHud.create(window);
        const bool started=player.launch(true);player.renderer->pacing_enabled(player.pacingShown);player.renderer->frame_updates(window);player.tick=GetTickCount64();SetTimer(window,1,16,nullptr);
        SendMessageW(window,WM_SIZE,0,0);if(smokeOutput.empty() && started){activate_startup_window(window);}
        if(!gameplayCheckOutput.empty()){result=player.gameplay_checks(gameplayCheckOutput);CoUninitialize();return result;}
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}result=smokeOutput.empty() && combatCheckOutput.empty()?0:player.smokeResult;
    }catch(const std::exception& e){diagnostic("standalone player startup",e.what());}
    CoUninitialize();return result;
}
