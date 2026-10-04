#pragma once
#include "player_presentation.hpp"
// Hidden integration: actual solid carving, prop fall, fluid rebase and reset.
inline int terrain_destruction_preview(const fs::path& scenePath,const fs::path& output){
    fs::create_directories(output);HWND window=nullptr;
    try{
        auto scene=load_scene(scenePath);const auto authored=scene.encode();
        if(!scene.Terrain||!scene.Game||!scene.Terrain->WaterEnabled||scene.Characters.size()!=1||!scene.Characters[0].Dynamic)throw std::runtime_error("Expected terrain, water and a supported dynamic prop");
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=graphics_preview::window_proc;cls.lpszClassName=L"StarlightTerrainDamageProbe";RegisterClassW(&cls);
        window=CreateWindowW(cls.lpszClassName,L"Starlight terrain damage probe",WS_POPUP,0,0,2560,1440,nullptr,nullptr,cls.hInstance,nullptr);if(!window)throw std::runtime_error("Cannot create terrain probe");
        bool closed=false;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&closed));
        RenderStatus last;uint64_t captureTarget=0,previousRevision=0;int stage=0;double beginVolume=0,endVolume=0,bodyBefore=0,bodyAfter=0,carveMs=0,damageClock=0;bool resetOkay=false;
        auto player=std::make_unique<AdventurePlayer>();player->start(scene);auto start=std::chrono::steady_clock::now(),tick=start;
        {
            NativeSession session(window,2560,1440);session.scene(scene);session.camera({{5,4.5,6},{0,-.5,0},9});session.playback(true,true);
            while(!closed){
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                auto now=std::chrono::steady_clock::now();if(std::chrono::duration<double>(now-start).count()>45)throw std::runtime_error("Terrain integration deadline");
                last=session.status();if(!last.error.empty())throw std::runtime_error(last.error);
                double dt=std::clamp(std::chrono::duration<double>(now-tick).count(),0.,.05);tick=now;
                if(last.frames<5){std::this_thread::sleep_for(std::chrono::milliseconds(10));continue;}
                player->move(0,0,dt);session.gameplay_time(player->seconds());
                if(player->rules().revision!=previousRevision){session.scene(player->visible_scene(),true);previousRevision=player->rules().revision;}
                session.player_body(player->body_bounds());session.runtime_objects(player->dynamic_objects());session.collision_objects(player->environment_objects());player->liquids(session.liquid_snapshot());
                auto p=player->presentation();p.showWeapons=p.showBody=p.showTelegraph=false;session.gameplay_overlays(player_presentation(p));
                if(stage==0&&player->seconds()>.6&&last.liquid_volume>0){beginVolume=last.liquid_volume;bodyBefore=player->scene().Characters[0].Y;captureTarget=last.captures+1;session.capture(output/L"terrain-intact.png");stage=1;}
                if(stage==1&&last.captures>=captureTarget){auto before=std::chrono::steady_clock::now();if(!player->damage_terrain({0,.04,0},{0,1,0},scene.Terrain->DestructionHealth))throw std::runtime_error("Terrain impact did not carve");carveMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();damageClock=player->seconds();stage=2;}
                if(stage==2&&player->seconds()>damageClock+2){endVolume=last.liquid_volume;bodyAfter=player->scene().Characters[0].Y;captureTarget=last.captures+1;session.capture(output/L"terrain-carved.png");stage=3;}
                if(stage==3&&last.captures>=captureTarget){player->start(scene);session.scene(scene);session.gameplay_overlays({});session.gameplay_time(0);session.playback(true,true);previousRevision=0;damageClock=last.frames;stage=4;}
                if(stage==4&&last.frames>damageClock+5){captureTarget=last.captures+1;session.capture(output/L"terrain-reset.png");stage=5;}
                if(stage==5&&last.captures>=captureTarget){resetOkay=player->scene().Terrain->Volumes.empty()&&player->presentation().dust.empty()&&std::abs(last.liquid_volume-beginVolume)<1e-7;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            session.suspend_rendering(true);
        }
        DestroyWindow(window);window=nullptr;
        bool pass=stage==5&&resetOkay&&bodyAfter<bodyBefore-.6&&std::abs(beginVolume-endVolume)<1e-7&&scene.encode()==authored;
        atomic_write(output/L"result.json",json{{"success",pass},{"width",2560},{"height",1440},{"frames",last.frames},{"gpuMs",last.gpu_ms},{"carveCpuMs",carveMs},{"initialVolume",beginVolume},{"carvedVolume",endVolume},{"bodyBefore",bodyBefore},{"bodyAfter",bodyAfter},{"resetRestored",resetOkay},{"authoredUnchanged",scene.encode()==authored},{"scope","Hidden runtime solid carving, supported prop fall, unchanged fluid amount and reset; not final appearance or full-scene performance"}}.dump(2));return pass?0:1;
    }catch(const std::exception& error){if(IsWindow(window))DestroyWindow(window);atomic_write(output/L"result.json",json{{"success",false},{"error",error.what()}}.dump(2));return 1;}
}
