#pragma once
#include "player_presentation.hpp"
// Bounded hidden gameplay/render integration; no foreground activation.
inline int destruction_dust_preview(const fs::path& scenePath,const fs::path& output){
    fs::create_directories(output);HWND window=nullptr;
    try{
        auto scene=load_scene(scenePath);const auto authored=scene.encode();
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=graphics_preview::window_proc;cls.lpszClassName=L"StarlightDestructionDustProbe";RegisterClassW(&cls);
        window=CreateWindowW(cls.lpszClassName,L"Starlight destruction dust probe",WS_POPUP,0,0,2560,1440,nullptr,nullptr,cls.hInstance,nullptr);if(!window)throw std::runtime_error("Cannot create fracture probe");
        bool closed=false;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&closed));
        RenderStatus last;bool sourceRemoved=false,capturedEarly=false,capturedLater=false,capturedReset=false,capturedFade=false;double breakTime=-1;uint64_t captureTarget=0;int stage=0;double lastClock=0;bool fired=false;uint64_t previousRevision=0;
        auto player=std::make_unique<AdventurePlayer>();player->start(scene);auto start=std::chrono::steady_clock::now(),tick=start;
        {
            NativeSession session(window,2560,1440);session.scene(scene);session.camera({{6,4.5,6},{0,1,-3},11});session.playback(true,true);
            while(!closed){
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                auto now=std::chrono::steady_clock::now();double elapsed=std::chrono::duration<double>(now-start).count();if(elapsed>45)throw std::runtime_error("Destruction dust integration deadline");
                last=session.status();if(!last.error.empty())throw std::runtime_error(last.error);
                double dt=std::clamp(std::chrono::duration<double>(now-tick).count(),0.,.05);tick=now;
                if(last.frames<5){std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
                if(!fired){player->fire_button(true);player->fire_button(false);fired=true;}
                player->move(0,0,dt);session.gameplay_time(player->seconds());
                if(player->rules().revision!=previousRevision){session.scene(player->visible_scene(),true);previousRevision=player->rules().revision;}
                session.player_body(player->body_bounds());session.collision_objects(player->environment_objects());player->liquids(session.liquid_snapshot());session.runtime_objects(player->dynamic_objects());session.pacing_debris(player->debris_poses());
                auto p=player->presentation();p.showWeapons=false;p.showBody=false;p.showTelegraph=false;
                session.gameplay_overlays(player_presentation(p));
                if(stage==0&&!p.dust.empty()&&!p.grit.empty()){sourceRemoved=player->scene().Characters.empty();breakTime=player->seconds();stage=1;}
                if(stage==1&&player->seconds()>breakTime+.2){captureTarget=last.captures+1;session.capture(output/L"fracture-early.png");stage=2;}
                if(stage==2&&last.captures>=captureTarget){capturedEarly=true;lastClock=player->seconds();stage=3;}
                if(stage==3&&player->seconds()>lastClock+.75){captureTarget=last.captures+1;session.capture(output/L"fracture-later.png");stage=4;}
                if(stage==4&&last.captures>=captureTarget){capturedLater=true;stage=7;}
                if(stage==7&&player->seconds()>breakTime+4.5&&p.dust.empty()&&p.grit.empty()){captureTarget=last.captures+1;session.capture(output/L"dust-faded.png");stage=8;}
                if(stage==8&&last.captures>=captureTarget){capturedFade=true;player->start(scene);session.scene(scene);session.gameplay_overlays({});session.gameplay_time(0);session.playback(true,true);captureTarget=last.captures+1;stage=5;lastClock=last.frames;}
                if(stage==5&&last.frames>lastClock+5){session.capture(output/L"fracture-reset.png");stage=6;}
                if(stage==6&&last.captures>=captureTarget){capturedReset=player->presentation().fragments.empty()&&player->presentation().dust.empty()&&player->presentation().grit.empty()&&player->scene().Characters.size()==scene.Characters.size();break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            session.suspend_rendering(true);
        }
        DestroyWindow(window);window=nullptr;
        bool pass=sourceRemoved&&capturedEarly&&capturedLater&&capturedReset&&capturedFade&&scene.encode()==authored;
        atomic_write(output/L"result.json",json{{"success",pass},{"width",2560},{"height",1440},{"frames",last.frames},{"gpuMs",last.gpu_ms},{"sourceRemoved",sourceRemoved},{"capturedEarly",capturedEarly},{"capturedLater",capturedLater},{"capturedReset",capturedReset},{"capturedFade",capturedFade},{"authoredUnchanged",scene.encode()==authored},{"scope","Bounded hidden real gameplay dust/grit burst, motion, fading and reset through NativeSession; not full performance or final visual acceptance"}}.dump(2));return pass?0:1;
    }catch(const std::exception& error){if(IsWindow(window))DestroyWindow(window);atomic_write(output/L"result.json",json{{"success",false},{"error",error.what()}}.dump(2));return 1;}
}
