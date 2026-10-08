#pragma once
#include "startup_activation.hpp"

// Use the live editor/player session for visual acceptance. A frozen packet
// cannot demonstrate procedural vegetation motion or simulation updates.
namespace graphics_preview {
inline LRESULT CALLBACK window_proc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_CLOSE||(message==WM_KEYDOWN&&w==VK_ESCAPE)){auto closed=reinterpret_cast<bool*>(GetWindowLongPtrW(window,GWLP_USERDATA));if(closed)*closed=true;return 0;}
    return DefWindowProcW(window,message,w,l);
}
inline int run(const fs::path& scenePath,const fs::path& output,bool visible){
    fs::create_directories(output);
    HWND window=nullptr;
    try{
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        MONITORINFO monitor{sizeof(monitor)};
        if(!GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&monitor))throw std::runtime_error("Cannot determine preview monitor resolution");
        const auto bounds=monitor.rcMonitor;const int width=visible?bounds.right-bounds.left:2560,height=visible?bounds.bottom-bounds.top:1440;
        auto scene=load_scene(scenePath);
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=window_proc;cls.lpszClassName=L"StarlightLiveGraphicsPreview";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
        window=CreateWindowW(cls.lpszClassName,L"Starlight live graphics preview - Escape closes",WS_POPUP,bounds.left,bounds.top,width,height,nullptr,nullptr,cls.hInstance,nullptr);
        if(!window)throw std::runtime_error("Cannot create live graphics preview");
        bool closed=false;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&closed));if(visible)activate_startup_window(window);
        const bool grassContact=GetEnvironmentVariableW(L"STARLIGHT_PREVIEW_GRASS_CONTACT",nullptr,0)>0;
        RenderStatus first,last;bool capturedFirst=false,capturedMotion=false;uint64_t measuredMilliseconds=0,firstWall=0,lastPublished=0;
        {
            NativeSession session(window,width,height);session.scene(scene);CameraState camera{{1.2,1.65,3.9},{0,.25,-3.3},7.4};wchar_t setting[256]{};
            if(GetEnvironmentVariableW(L"STARLIGHT_BENCH_CAMERA",setting,256)){std::wistringstream input(setting);std::array<double,6> values{};for(auto& value:values)if(!(input>>value)||!std::isfinite(value))throw std::runtime_error("Invalid live preview camera");std::wstring extra;if(input>>extra)throw std::runtime_error("Invalid live preview camera suffix");camera.eye={values[0],values[1],values[2]};camera.target={values[3],values[4],values[5]};camera.distance=(camera.eye-camera.target).length();if(camera.distance<.01)throw std::runtime_error("Live preview camera must look toward a different point");}
            session.camera(camera);session.playback(true,true);
            const auto started=GetTickCount64();auto deadline=started+45000;uint64_t captureTarget=0;int captureStage=0;
            while(!closed){
                MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
                if(closed)break;
                last=session.status();if(!last.error.empty())throw std::runtime_error(last.error);
                if(grassContact&&last.frames>=5){double x=1.4*std::sin(last.seconds*1.3);session.player_body(Bounds{{x-.35,-.2,-3.35},{x+.35,1.7,-2.65}});}
                if(last.frames<5&&GetTickCount64()>deadline)throw std::runtime_error("Live graphics preview startup timeout");
                if(!visible&&GetTickCount64()>started+120000)throw std::runtime_error("Live graphics preview did not finish its captures within two minutes");
                if(captureStage==0&&last.frames>=5){first=last;firstWall=GetTickCount64();captureTarget=last.captures+1;session.capture(output/L"field-start.png");captureStage=1;}
                if(captureStage==1&&last.captures>=captureTarget){capturedFirst=true;captureStage=2;}
                if(captureStage==2&&last.seconds>first.seconds+2){captureTarget=last.captures+1;session.capture(output/L"field-motion.png");captureStage=3;}
                if(captureStage==3&&last.captures>=captureTarget){capturedMotion=true;captureStage=4;
                    if(!visible)break;
                }
                auto now=GetTickCount64();if(firstWall&&now-lastPublished>=1000){measuredMilliseconds=now-firstWall;lastPublished=now;auto frames=last.frames-first.frames;auto updates=last.scene_updates-first.scene_updates;
                    atomic_write(output/L"preview-progress.json",json{{"width",width},{"height",height},{"frames",last.frames},{"seconds",last.seconds},{"playing",last.playing},{"captures",last.captures},{"sceneUpdates",last.scene_updates},{"liveSession",true},{"visible",IsWindowVisible(window)!=0},{"foreground",GetForegroundWindow()==window},{"measuredMs",measuredMilliseconds},{"measuredFps",measuredMilliseconds?frames*1000./measuredMilliseconds:0},{"sceneCpuMs",updates?(last.scene_cpu_total_ms-first.scene_cpu_total_ms)/updates:0},{"frameCpuMs",frames?(last.frame_cpu_total_ms-first.frame_cpu_total_ms)/frames:0},{"bloomLevels",last.bloom_levels},{"hdrPostAllocationBytes",last.hdr_post_bytes},{"gpuMs",last.gpu_ms}}.dump(2));
                }
                if(visible&&firstWall&&now-firstWall>=30000)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            // Stop rendering before destroying the HWND used by the worker.
            measuredMilliseconds=firstWall?GetTickCount64()-firstWall:0;
            session.suspend_rendering(true);
        }
        if(IsWindow(window))DestroyWindow(window);
        const bool pass=capturedFirst&&capturedMotion&&last.playing&&last.seconds>first.seconds+2&&(!grassContact||(last.grass_instances>0&&last.grass_contacts>0&&last.grass_dispatches>first.grass_dispatches));
        const auto frames=last.frames-first.frames,updates=last.scene_updates-first.scene_updates;
        atomic_write(output/L"preview-results.json",json{{"success",pass},{"width",width},{"height",height},{"frames",last.frames},{"firstSeconds",first.seconds},{"lastSeconds",last.seconds},{"sceneUpdates",last.scene_updates},{"measuredMs",measuredMilliseconds},{"measuredFps",measuredMilliseconds?frames*1000./measuredMilliseconds:0},{"sceneCpuMs",updates?(last.scene_cpu_total_ms-first.scene_cpu_total_ms)/updates:0},{"frameCpuMs",frames?(last.frame_cpu_total_ms-first.frame_cpu_total_ms)/frames:0},{"bloomLevels",last.bloom_levels},{"hdrPostAllocationBytes",last.hdr_post_bytes},{"gpuMs",last.gpu_ms},{"grassContactProbe",grassContact},{"grassInstances",last.grass_instances},{"grassContacts",last.grass_contacts},{"grassDispatches",last.grass_dispatches},{"grassAllocationBytes",last.grass_bytes},{"firstLiquidVolume",first.liquid_volume},{"lastLiquidVolume",last.liquid_volume},{"firstWaveEnergy",first.liquid_wave_energy},{"lastWaveEnergy",last.liquid_wave_energy},{"liveSession",true},{"scope","Live authored scene simulation and rendering, not final visual acceptance or full-scene performance"}}.dump(2));
        return pass?0:1;
    }catch(const std::exception& error){if(IsWindow(window))DestroyWindow(window);atomic_write(output/L"preview-results.json",json{{"success",false},{"error",error.what()}}.dump(2));return 1;}
}
}
