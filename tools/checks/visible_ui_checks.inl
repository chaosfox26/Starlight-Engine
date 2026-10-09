// Visible, paced checks in a new editor process. This complements the fast
// regression suite; it never opens a user's saved scene or invokes file dialogs.
int visible_ui_checks(const fs::path& output) {
    fs::create_directories(output.parent_path());
    nlohmann::json report{{"checks",nlohmann::json::array()}, {"scope","visible editor menu walkthrough"},
                          {"isolatedScene",true}, {"allEditorFeaturesCertified",false}};
    auto pump = [&](bool waiting) {
        MSG msg{};const auto budgetEnd=GetTickCount64()+12;
        while (GetTickCount64()<budgetEnd && PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            if (msg.message==WM_QUIT || msg.message==WM_CLOSE || (msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE))
                throw std::runtime_error("Visible walkthrough cancelled");
            // Automation owns this scratch scene. Do not mix unrelated manual
            // edits into test actions; normal paint/timer/activation still run.
            if ((msg.message>=WM_MOUSEFIRST && msg.message<=WM_MOUSELAST) ||
                (msg.message>=WM_KEYFIRST && msg.message<=WM_KEYLAST)) continue;
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
    };
    auto foreground = [&] {
        DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        return IsWindowVisible(window) && !IsIconic(window) && pid==GetCurrentProcessId();
    };
    auto capture = [&](const fs::path& path) {
        HWND target=projectWindow && IsWindowVisible(projectWindow) ? projectWindow : window;
        RECT r{}; GetClientRect(target,&r); const int w=r.right,h=r.bottom;
        // Capture the presented desktop pixels inside our client bounds. A GDI
        // window DC can omit the GPU swapchain and falsely show a blank canvas.
        POINT origin{}; ClientToScreen(target,&origin);
        HDC source=GetDC(nullptr), memory=source?CreateCompatibleDC(source):nullptr;
        HBITMAP bitmap=source?CreateCompatibleBitmap(source,w,h):nullptr;
        bool okay=false;
        if (memory && bitmap) {
            auto previous=SelectObject(memory,bitmap);
            okay=BitBlt(memory,0,0,w,h,source,origin.x,origin.y,SRCCOPY|CAPTUREBLT)!=0;
            SelectObject(memory,previous);
            BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h;
            info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
            std::vector<unsigned char> pixels(size_t(w)*h*4);
            okay &= GetDIBits(memory,bitmap,0,h,pixels.data(),&info,DIB_RGB_COLORS)==h;
            if (okay) {
                BITMAPFILEHEADER header{}; header.bfType=0x4D42;
                header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
                header.bfSize=header.bfOffBits+static_cast<DWORD>(pixels.size());
                std::ofstream image(path,std::ios::binary);
                image.write(reinterpret_cast<const char*>(&header),sizeof(header));
                image.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(BITMAPINFOHEADER));
                image.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
                okay=bool(image);
            }
        }
        if (bitmap) DeleteObject(bitmap);
        if (memory) DeleteDC(memory);
        if (source) ReleaseDC(nullptr,source);
        return okay;
    };
    auto step = [&](const char* name, auto action, auto valid) {
        const auto started=GetTickCount64();
        starlight_diag::evlog("STEP.BEGIN",double(report["checks"].size()));
        action();
        const auto initialStatus=renderer->status();
        const auto initialFrames=initialStatus.frames;
        SetWindowTextW(window,wide(std::string("Starlight visible audit — ")+name+" | Esc cancels").c_str());
        auto end=GetTickCount64()+900;
        bool stayedForeground=true;
        nlohmann::json firstFocusLoss=nullptr;
        auto observeForeground=[&] {
            const bool present=foreground();
            if(!present && firstFocusLoss.is_null()) {
                const auto active=GetForegroundWindow();
                DWORD owner=0;GetWindowThreadProcessId(active,&owner);
                wchar_t windowClass[128]{};GetClassNameW(active,windowClass,128);
                firstFocusLoss={{"elapsedMs",GetTickCount64()-started},{"foregroundProcess",owner},
                    {"foregroundClass",utf8(windowClass)},{"editorProcess",GetCurrentProcessId()},
                    {"editorVisible",IsWindowVisible(window)!=FALSE},{"editorMinimized",IsIconic(window)!=FALSE},
                    {"editorHung",IsHungAppWindow(window)!=FALSE},{"foregroundHung",IsHungAppWindow(active)!=FALSE}};
            }
            stayedForeground &= present;
        };
        ULONGLONG newFramesMs=0;
        while (GetTickCount64()<end) { pump(false); observeForeground();
            if(!newFramesMs && renderer->status().frames>=initialFrames+3) newFramesMs=GetTickCount64()-started;
            MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE); }
        // Require new rendered frames after a transition,
        // rather than capturing a blank intermediate surface and calling it done.
        const auto renderDeadline=GetTickCount64()+10000;
        while(renderer->status().frames<initialFrames+3 && renderer->status().error.empty() &&
              GetTickCount64()<renderDeadline) { pump(false); observeForeground(); MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE); }
        if(!newFramesMs && renderer->status().frames>=initialFrames+3) newFramesMs=GetTickCount64()-started;
        auto screenshot=output.parent_path()/("ui-"+std::to_string(report["checks"].size())+".bmp");
        static const bool skipCapture=GetEnvironmentVariableW(L"STARLIGHT_UI_SKIP_CAPTURE",nullptr,0)>0; // measurement only: removes the test's own per-step screenshot
        starlight_diag::evlog("STEP.CAPTURE",double(report["checks"].size()));
        const bool behavior=valid(), captured=stayedForeground && (skipCapture||capture(screenshot));
        starlight_diag::evlog("STEP.END",double(report["checks"].size()));
        const bool rendered=renderer->status().frames>=initialFrames+3;
        const bool passed=behavior && stayedForeground && captured && rendered && renderer->status().error.empty();
        report["checks"].push_back({{"name",name},{"pass",passed},{"behavior",behavior},
          {"foreground",stayedForeground},{"firstFocusLoss",firstFocusLoss},
          {"captureAttempted",stayedForeground},{"screenshot",utf8(screenshot.wstring())},{"captureSucceeded",captured},
          {"rendererFrames",renderer->status().frames},
          {"sceneUpdates",renderer->status().scene_updates-initialStatus.scene_updates},{"sceneCpuMs",renderer->status().scene_cpu_total_ms-initialStatus.scene_cpu_total_ms},{"frameCpuMs",renderer->status().frame_cpu_total_ms-initialStatus.frame_cpu_total_ms},{"gpuMs",renderer->status().gpu_ms},{"frameCount",renderer->status().frames-initialFrames},{"renderedNewFrames",rendered},
          {"newFramesMs",newFramesMs},{"elapsedMs",GetTickCount64()-started},{"rendererError",renderer->status().error}});
        atomic_write(output.string()+".progress.json",report.dump(2));
        if (!passed) throw std::runtime_error(std::string("Visible editor step failed: ")+name);
    };
    auto interactionFixture = [&] {
        step("Interaction fixture starts",[&]{
            scene=SceneData{};scene.Game.emplace();scene.Game->SpawnY=.5;scene.Game->SpawnZ=2;
            Character key;key.Shape="cube";key.GameplayRole="key";key.GameplayKey="audit-key";
            Character door=key;door.Id=new_id();door.X=3;door.GameplayRole="door";
            Character exit=key;exit.Id=new_id();exit.X=6;exit.GameplayRole="exit";
            scene.Characters={key,door,exit};selected=-1;refresh();command(Play);
        },[&]{return playing && adventure && IsWindowVisible(adventureHud);});
        // Each interaction starts from its explicit fixture pose; prior physics or
        // input must not turn this into an accidental out-of-range test.
        step("Collect key through editor input",[&]{adventure->position={0,.5,2};adventure->yaw=adventure->pitch=0;SendMessageW(view,WM_KEYDOWN,'E',0);},[&]{return adventure->scene().Characters.size()==2 && scene.Characters.size()==3;});
        step("Open matching door through editor input",[&]{adventure->position={3,.5,2};adventure->yaw=adventure->pitch=0;SendMessageW(view,WM_KEYDOWN,'E',0);},[&]{return adventure->scene().Characters.size()==1 && adventureHint=="Door opened.";});
        step("Complete exit through editor input",[&]{adventure->position={6,.5,2};adventure->yaw=adventure->pitch=0;
            report["exitInteractionBefore"]={{"position",{adventure->position.x,adventure->position.y,adventure->position.z}},{"pitch",adventure->pitch},{"yaw",adventure->yaw}};
            SendMessageW(view,WM_KEYDOWN,'E',0);
            report["exitInteractionImmediate"]={{"finished",adventure->finished},{"hint",adventureHint}};
        },[&]{report["exitInteractionAfter"]={{"finished",adventure->finished},{"hint",adventureHint}};return adventure->finished && adventureHint=="Adventure complete!";});
        step("Reset restores interactive scene",[&]{command(Stop);},[&]{return !adventure && scene.Characters.size()==3 && !IsWindowVisible(adventureHud);});
    };
    try {
        report["activationRequested"] = startupActivated;
        if(!foreground()) {
            report["failureCategory"]="environment-precondition";
            throw std::runtime_error("Startup focus unavailable; stopped without retry. User direction required.");
        }
        SetWindowTextW(window,L"Starlight visible editor audit — automatically verifying foreground; Esc cancels");
        auto deadline=GetTickCount64()+30000, stableSince=GetTickCount64();
        while (GetTickCount64()<deadline) {
            pump(true);
            if(!foreground())throw std::runtime_error("Test window lost focus; stopped without retry. User direction required.");
            if(renderer->status().frames<3)stableSince=GetTickCount64();
            if (GetTickCount64()-stableSince>=1500) break;
            MsgWaitForMultipleObjectsEx(0,nullptr,10,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        const auto startup = renderer->status();
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        report["startup"] = {{"visible",IsWindowVisible(window)!=FALSE},{"minimized",IsIconic(window)!=FALSE},
            {"foreground",foreground()},{"foregroundProcess",foregroundProcess},{"editorProcess",GetCurrentProcessId()},
            {"rendererFrames",startup.frames},{"rendererError",startup.error},{"stableMs",GetTickCount64()-stableSince}};
        if (!foreground() || startup.frames<3 || GetTickCount64()-stableSince<1500) {
            report["failureCategory"]="environment-precondition";
            throw std::runtime_error("Automatic visible editor foreground precondition unavailable");
        }
        report["automaticStart"]=true;
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_INTERACTION_ONLY",nullptr,0)){
            interactionFixture();report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_PHYSICS_AUTHORING_ONLY",nullptr,0)){
            scene=SceneData{};scene.Game.emplace();Character body;body.Shape="cube";Character fire;fire.Shape="fire";fire.X=3;Character bolt;bolt.Shape="chaos-lightning";bolt.X=-3;scene.Characters={body,fire,bolt};selected=0;selectionIds={body.Id};history.clear();dirty=false;bodyDetails=true;refresh();show_inspector(0);
            auto editCheck=[&](const char* name,auto action,auto valid){auto before=scene.encode();step(name,action,valid);auto after=scene.encode();step("Undo physics authoring",[&]{command(Undo);},[&]{return scene.encode()==before;});step("Redo physics authoring",[&]{command(Redo);},[&]{return scene.encode()==after;});};
            editCheck("Author physical material, velocity and rope",[&]{SendMessage(inputs.at("dynamic"),BM_SETCHECK,BST_CHECKED,0);SetWindowTextW(inputs.at("density"),L"650");SetWindowTextW(inputs.at("friction"),L"0.6");SetWindowTextW(inputs.at("restitution"),L"0.4");SetWindowTextW(inputs.at("VelocityX"),L"2");SetWindowTextW(inputs.at("AngularVelocityY"),L"90");SendMessage(inputs.at("jointKind"),CB_SETCURSEL,3,0);SetWindowTextW(inputs.at("JointSegments"),L"12");SetWindowTextW(inputs.at("JointRadius"),L"0.04");command(ApplyBody);},[&]{auto c=scene.Characters[0];return c.Dynamic&&c.Density==650&&c.Friction==.6&&c.Restitution==.4&&c.VelocityX==2&&c.AngularVelocityY==90&&c.JointKind==3&&c.JointSegments==12&&c.JointRadius==.04;});
            selected=1;selectionIds={fire.Id};show_inspector(0);
            editCheck("Author independent particle settings",[&]{SetWindowTextW(inputs.at("ParticleRate"),L"120");SetWindowTextW(inputs.at("ParticleLifetime"),L"3");SetWindowTextW(inputs.at("ParticleDrag"),L"1.2");SetWindowTextW(inputs.at("ParticleLift"),L"4");SetWindowTextW(inputs.at("ParticleWindX"),L"2");command(ApplyParticles);},[&]{auto c=scene.Characters[1];return c.ParticleRate==120&&c.ParticleLifetime==3&&c.ParticleDrag==1.2&&c.ParticleLift==4&&c.ParticleWindX==2;});
            selected=2;selectionIds={bolt.Id};show_inspector(0);
            editCheck("Author lightning target, damage and chaining",[&]{SendMessage(inputs.at("lightningTarget"),CB_SETCURSEL,1,0);SetWindowTextW(inputs.at("lightningDamage"),L"12");SetWindowTextW(inputs.at("lightningChains"),L"3");SetWindowTextW(inputs.at("lightningRadius"),L"5");command(ApplyLightning);},[&]{auto c=scene.Characters[2];return c.LightningTargetId==body.Id&&c.LightningDamage==12&&c.LightningChains==3&&c.LightningChainRadius==5;});
            selected=0;selectionIds={body.Id};show_inspector(0);
            editCheck("Author projectile combat settings",[&]{SetWindowTextW(inputs.at("CombatProjectileSpeed"),L"25");command(ApplyGameplay);},[&]{return scene.Characters[0].CombatProjectileSpeed==25;});
            auto saved=scene.encode();step("Save and reopen physics controls",[&]{save_scene(scene,output.parent_path()/"physics-authored.starscene");scene=load_scene(output.parent_path()/"physics-authored.starscene");refresh();},[&]{return scene.encode()==saved;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_FOG_BOUNDARY_ONLY",nullptr,0)){
            MONITORINFO m{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&m);if(m.rcMonitor.right-m.rcMonitor.left!=2560||m.rcMonitor.bottom-m.rcMonitor.top!=1440)throw std::runtime_error("Fog boundary test requires 1440p");SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,m.rcMonitor.left,m.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            scene=SceneData{};scene.FogDensity=0;Character smoke;smoke.Shape="craxis-smoke";smoke.X=100;smoke.Y=3;scene.Characters={smoke};history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            step("Warm exterior fog source",[&]{},[&]{return renderer->status().playing;});auto probes=renderer->status().fog_probes;step("Exterior source cannot wrap into simulated field",[&]{renderer->probe_fog();},[&]{return renderer->status().fog_probes>probes&&renderer->status().fog_sample[1]<.001;});
            step("Move source inside positive field edge",[&]{scene.Characters[0].X=15;renderer->scene(scene);renderer->playback(true,true);},[&]{return renderer->status().playing;});probes=renderer->status().fog_probes;step("Read finite interior edge density",[&]{renderer->probe_fog();},[&]{auto a=renderer->status().fog_sample;return renderer->status().fog_probes>probes&&a[1]>.01&&std::all_of(a.begin(),a.end(),[](double v){return std::isfinite(v);});});
            step("Fill fog obstacle routing beyond capacity",[&]{for(int i=0;i<130;++i){Character c;c.Shape="cube";c.X=-10+(i%13)*1.5;c.Z=-10+(i/13)*1.5;c.Y=1;scene.Characters.push_back(c);}renderer->scene(scene);renderer->playback(true,true);},[&]{return renderer->status().fog_bodies==128;});probes=renderer->status().fog_probes;step("Overflow preserves valid GPU obstacle field",[&]{renderer->probe_fog();},[&]{auto a=renderer->status().fog_sample;return renderer->status().fog_probes>probes&&a[5]<.001&&std::all_of(a.begin(),a.end(),[](double v){return std::isfinite(v);});});
            step("Remove excess obstacles and clear routed slots",[&]{scene.Characters.resize(1);renderer->scene(scene);renderer->playback(true,true);},[&]{return renderer->status().fog_bodies==0;});report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_FOG_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560||monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Fog test requires a 2560x1440 monitor");SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            scene=SceneData{};scene.Name="Physical fog transport and terrain exclusion";scene.FogDensity=0;scene.Terrain.emplace();if(GetEnvironmentVariableW(L"STARLIGHT_UI_FOG_WATER",nullptr,0)){scene.Terrain->WaterEnabled=true;scene.Terrain->WaterLevel=2;report["liquidAirBoundary"]=true;}for(int z=0;z<TerrainData::N;++z)for(int x=0;x<TerrainData::N;++x)scene.Terrain->Heights[z*TerrainData::N+x]=1.+.035*(x*TerrainData::Step-12);Character smoke;smoke.Shape=GetEnvironmentVariableW(L"STARLIGHT_UI_FOG_FIRE",nullptr,0)?"fire":"craxis-smoke";smoke.Y=2;smoke.Size=2;if(smoke.Shape=="fire")report["fireSmokeSource"]=true;Character obstacle;obstacle.Shape="cube";obstacle.Height=3;scene.Characters={smoke,obstacle};if(GetEnvironmentVariableW(L"STARLIGHT_UI_FOG_DENSE",nullptr,0)){scene.Characters.clear();for(int i=0;i<16;++i){auto emitter=smoke;emitter.Id="moving-smoke-"+std::to_string(i);emitter.X=-6+(i%4)*2;emitter.Z=-3+(i/4)*2;emitter.Route={{emitter.X+7,emitter.Z}};emitter.Speed=.7;scene.Characters.push_back(emitter);}for(int i=0;i<8;++i){auto body=obstacle;body.Id="fog-obstacle-"+std::to_string(i);body.X=-5+i*1.5;body.Z=-1;body.Width=.6;body.Route={{body.X,4}};body.Speed=.3;scene.Characters.push_back(body);}report["denseMovingSources"]=16;}
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            step("Warm transported smoke density",[&]{},[&]{return renderer->status().playing;});auto probes=renderer->status().fog_probes;step("Read emitted smoke field",[&]{renderer->probe_fog();},[&]{return renderer->status().fog_probes>probes;});auto first=renderer->status().fog_sample;report["emittedField"]=first;if(first[1]<1||first[5]>.001||first[6]<.1||first[3]<1.)throw std::runtime_error("Fog source, obstacle exclusion or velocity field is missing");
            step("Pause physical smoke transport",[&]{renderer->playback(false);},[&]{return !renderer->status().playing;});probes=renderer->status().fog_probes;
            step("Read paused density",[&]{renderer->probe_fog();},[&]{return renderer->status().fog_probes>probes;});auto paused=renderer->status().fog_sample;probes=renderer->status().fog_probes;
            step("Verify paused density stays unchanged",[&]{renderer->probe_fog();},[&]{return renderer->status().fog_probes>probes;});auto held=renderer->status().fog_sample;for(int i=0;i<7;++i)if(std::abs(held[i]-paused[i])>1e-8)throw std::runtime_error("Paused atmosphere continued evolving on wall-clock time");report["pausedFieldUnchanged"]=true;
            step("Resume physical smoke transport",[&]{renderer->playback(true);},[&]{return renderer->status().playing;});probes=renderer->status().fog_probes;
            step("Read resumed density",[&]{renderer->probe_fog();},[&]{return renderer->status().fog_probes>probes;});first=renderer->status().fog_sample;report["resumedField"]=first;
            step("Remove emitter while preserving transported smoke",[&]{std::erase_if(scene.Characters,[](const auto& c){return c.Shape=="craxis-smoke"||c.Shape=="fire";});renderer->scene(scene,true);},[&]{return renderer->status().error.empty();});probes=renderer->status().fog_probes;step("Read drifting and dissipating smoke",[&]{renderer->probe_fog();},[&]{return renderer->status().fog_probes>probes;});auto after=renderer->status().fog_sample;report["advectedField"]=after;if(after[1]<=.01||after[1]>=first[1]||after[2]<=first[2]+.02||after[5]>.001)throw std::runtime_error("Smoke did not persist, drift, dissipate or avoid solids after emitter removal");
            step("Reset smoke runtime",[&]{renderer->scene(scene);renderer->playback(false,true);},[&]{return !renderer->status().playing;});probes=renderer->status().fog_probes;step("Read cleared smoke field",[&]{renderer->probe_fog();},[&]{return renderer->status().fog_probes>probes;});auto reset=renderer->status().fog_sample;report["resetField"]=reset;if(reset[1]>.001)throw std::runtime_error("Fog reset retained old density");report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_MOTION_ONLY",nullptr,0)||GetEnvironmentVariableW(L"STARLIGHT_UI_RT_MOTION_ONLY",nullptr,0)){
            bool rtMotion=GetEnvironmentVariableW(L"STARLIGHT_UI_RT_MOTION_ONLY",nullptr,0)!=0;
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560 || monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Motion test requires a 2560x1440 monitor");
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            scene=SceneData{};scene.Name="GPU rigid motion-vector verification";scene.RaytracingEnabled=rtMotion;scene.RaytracingQuality=1;scene.RayReconstructionEnabled=GetEnvironmentVariableW(L"STARLIGHT_UI_RR_MOTION",nullptr,0)!=0;Character mover;mover.Shape="cube";mover.Name="Moving object";mover.X=-4;mover.Route={{10,0}};mover.Speed=.6;Character still;still.Shape="cube";still.X=3;still.Z=-2;scene.Characters={mover,still};history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            step("Warm motion history",[&]{},[&]{return renderer->status().playing;});
            auto probes=renderer->status().motion_probes;
            step("Read moving GPU vectors",[&]{renderer->probe_motion();},[&]{return renderer->status().motion_probes>probes;});
            if(rtMotion&&renderer->status().tier==0)throw std::runtime_error("Ray-traced motion test fell back to raster");if(GetEnvironmentVariableW(L"STARLIGHT_UI_RR_MOTION",nullptr,0)&&!renderer->status().rr)throw std::runtime_error("Ray Reconstruction motion test used a fallback denoiser");report["rayReconstructionActive"]=renderer->status().rr;report["raytracedMotion"]=rtMotion;auto moving=renderer->status().motion_sample;report["movingVectors"]=moving;if(moving[2]<10||moving[0]<.001||moving[0]>30)throw std::runtime_error("GPU motion texture does not contain bounded object movement");
            probes=renderer->status().motion_probes;
            step("Reorder objects with preserved history",[&]{std::reverse(scene.Characters.begin(),scene.Characters.end());renderer->scene(scene,true);renderer->probe_motion();},[&]{return renderer->status().motion_probes>probes;});
            auto reordered=renderer->status().motion_sample;report["reorderedVectors"]=reordered;if(reordered[2]<10||reordered[0]>30)throw std::runtime_error("Reordering changed or erased object motion history");
            step("Pause scene motion",[&]{renderer->playback(false);},[&]{return !renderer->status().playing;});probes=renderer->status().motion_probes;
            step("Read stationary GPU vectors",[&]{renderer->probe_motion();},[&]{return renderer->status().motion_probes>probes;});
            auto stationary=renderer->status().motion_sample;report["stationaryVectors"]=stationary;if(stationary[0]>.001)throw std::runtime_error("Stationary objects retain incorrect motion vectors");
            step("Warm independent particle motion",[&]{scene.Characters.clear();Character fire;fire.Shape="fire";fire.ParticleRate=100;scene.Characters.push_back(fire);renderer->scene(scene);renderer->playback(true,true);},[&]{return renderer->status().fire_particles>0;});
            probes=renderer->status().motion_probes;
            step("Read particle GPU vectors",[&]{renderer->probe_motion();},[&]{return renderer->status().motion_probes>probes;});auto particles=renderer->status().motion_sample;report["particleVectors"]=particles;if(particles[2]<10||particles[0]<.001||particles[0]>100)throw std::runtime_error("Particle vertices do not carry bounded GPU motion vectors");
            step("Warm physically bending grass",[&]{scene.Characters.clear();Character grass;grass.Shape="grass";grass.Size=2;scene.Characters={grass};renderer->scene(scene);renderer->playback(true,true);},[&]{return renderer->status().playing;});probes=renderer->status().motion_probes;
            step("Read grass deformation vectors",[&]{renderer->probe_motion();},[&]{return renderer->status().motion_probes>probes;});auto grassVectors=renderer->status().motion_sample;report["grassVectors"]=grassVectors;if(grassVectors[2]<10||grassVectors[0]<.001||grassVectors[0]>100)throw std::runtime_error("Physical grass deformation is missing bounded rendered motion vectors");
            if(rtMotion){report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;}
            auto fill=[&](float progress){MeshPart part;part.gpuTelegraph=GpuTelegraph{0,.05f,0,0,-1,3,.785398163f,progress,.2f,.8f,.3f};part.gpuTelegraph->identity=vertex_motion_identity(11,991,UINT64_MAX);return part;};
            step("Warm resident attack fill",[&]{scene.Characters.clear();renderer->scene(scene);renderer->playback(false);renderer->gameplay_overlays({fill(.25f)});},[&]{return renderer->status().error.empty();});probes=renderer->status().motion_probes;
            step("Read expanding GPU fill vectors",[&]{renderer->gameplay_overlays({fill(.5f)},true);},[&]{return renderer->status().motion_probes>probes;});auto attack=renderer->status().motion_sample;report["attackFillVectors"]=attack;if(attack[2]<100||attack[0]<.01||attack[0]>500)throw std::runtime_error("Resident attack fill does not preserve GPU motion history");
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_LIGHTNING_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560 || monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Lightning test requires a 2560x1440 monitor");
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            scene=SceneData{};scene.Name="Lightning contact, chains and damage";scene.Game.emplace();scene.Game->SpawnZ=0;
            Character bolt;bolt.Shape="chaos-lightning";bolt.X=-1;bolt.Z=-5;bolt.Y=3;bolt.TargetX=0;bolt.TargetY=-3;bolt.TargetZ=0;bolt.LightningDamage=7;bolt.LightningChains=1;bolt.LightningChainRadius=3;bolt.CombatTeam=1;
            Character body;body.Shape="cube";body.X=-1;body.Z=-5;body.CombatHealth=1000;Character second=body;second.Id=new_id();second.X=1;scene.Characters={bolt,body,second};auto authored=scene.encode();history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);adventure->cameraPitch=-.2;adventure->cameraDistance=8;bool sparks=false;
            for(int i=0;i<6;++i)step(("Lightning discharge "+std::to_string(i+1)).c_str(),[&]{},[&]{sparks|=renderer->status().effect_sparks>0;return scene.encode()==authored;});
            auto& actors=adventure->combat_actors();if(actors.at(body.Id).health>=1000||actors.at(second.Id).health>=1000||!sparks||adventure->presentation().lightning==0)throw std::runtime_error("Lightning did not produce chain damage, sparks and sound events");
            report["primaryHealth"]=actors.at(body.Id).health;report["chainHealth"]=actors.at(second.Id).health;report["strikeEvents"]=adventure->presentation().lightning;report["impactSparksObserved"]=sparks;report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_JOINTS_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560 || monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Joint test requires a 2560x1440 monitor");
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            scene=SceneData{};scene.Name="Fixed, hinge, rope and spring bodies";scene.Game.emplace();scene.Game->SpawnZ=0;
            for(int kind=1;kind<=4;++kind){Character c;c.Shape="cube";c.Name=std::vector<std::string>{"","Fixed","Hinge","Rope","Spring"}[kind];c.Dynamic=true;c.Size=.5;c.X=(kind-2.5)*2;c.Y=3;c.Z=-5;c.JointKind=kind;c.JointTargetY=2;c.JointLength=kind==4?1:2;if(kind==3){c.JointSegments=16;c.JointY=1;c.JointTargetY=2.5;c.Density=1000;}if(kind==2){c.JointX=1;c.JointY=1;c.JointAxisY=0;c.JointAxisZ=1;}scene.Characters.push_back(c);}
            auto authored=scene.encode();history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);adventure->cameraPitch=-.2;adventure->cameraDistance=8;
            for(int i=0;i<6;++i)step(("Constrained bodies "+std::to_string(i+1)).c_str(),[&]{},[&]{return scene.encode()==authored;});
            auto runtime=adventure->scene();if(std::abs(runtime.Characters[0].Y-3)>.05||std::abs(runtime.Characters[2].Y-3)>.1||runtime.Characters[3].Y<3.7)throw std::runtime_error("Visible constrained bodies did not remain attached");
            if(runtime.Characters[2].JointRuntimePoints.size()!=17)throw std::runtime_error("Visible physical rope has no simulated link path");report["physicalRopeLinks"]=16;report["fixedHeld"]=true;report["ropeHeld"]=true;report["springResponded"]=true;report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_ENEMY_PROJECTILE_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560||monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Enemy projectile test requires a 2560x1440 monitor");
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            scene=SceneData{};scene.Name="Enemy traveling shots and player impacts";scene.Game.emplace();scene.Game->SpawnZ=0;Character enemy;enemy.Shape="cube";enemy.Name="Projectile attacker";enemy.Z=-6;enemy.RotationY=180;enemy.CombatHealth=100;enemy.CombatAutoAttack=true;enemy.CombatProjectileSpeed=5;enemy.CombatProjectileGravity=.25;enemy.CombatProjectileMass=.1;scene.Characters={enemy};auto authored=scene.encode();history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);adventure->cameraPitch=-.2;adventure->cameraDistance=6;bool flying=false,monitored=false;
            for(int i=0;i<7;++i)step(("Enemy shot and impact "+std::to_string(i+1)).c_str(),[&]{},[&]{for(const auto& shot:adventure->presentation().projectiles)flying|=shot.source==enemy.Id&&shot.team==2;for(const auto& object:renderer->pacing_snapshot().objects)monitored|=object.id.starts_with("projectile-");return scene.encode()==authored;});
            if(!flying||!monitored||adventure->health()>=100||adventure->combat_actors().at(enemy.Id).health!=100)throw std::runtime_error("Enemy shot flight, player damage or owner immunity failed");report["enemyShotFlight"]=flying;report["projectileMonitor"]=monitored;report["playerHealth"]=adventure->health();report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_PROJECTILE_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560 || monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Projectile test requires a 2560x1440 monitor");
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            scene=SceneData{};scene.Name="Traveling projectile and physical impact";scene.Game.emplace();scene.Game->SpawnZ=0;scene.Game->ProjectileSpeed=5;
            Character target;target.Shape="cube";target.Name="Projectile target";target.GameplayRole="breakable";target.Z=-8;target.Height=2;target.Color="#C58542";scene.Characters={target};auto authored=scene.encode();history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            adventure->cameraPitch=-.2;adventure->cameraDistance=6;adventure->fire_button(true);adventure->fire_button(false);bool flying=false,monitored=false,fractured=false;
            for(int i=0;i<7;++i)step(("Traveling shot and impact "+std::to_string(i+1)).c_str(),[&]{},[&]{auto p=adventure->presentation();flying|=!p.projectiles.empty()&&!adventure->scene().Characters.empty();fractured|=p.fragments.size()>=16;for(const auto& object:renderer->pacing_snapshot().objects)monitored|=object.id.starts_with("projectile-");return scene.encode()==authored;});
            if(!flying||!monitored||!fractured||!adventure->scene().Characters.empty())throw std::runtime_error("Visible projectile did not travel, register motion and fracture its target on impact");
            report["projectileFlightObserved"]=flying;report["projectileMonitoringObserved"]=monitored;report["impactFractureObserved"]=fractured;report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_LIQUID_PHYSICS_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560 || monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Liquid physics test requires a 2560x1440 monitor");
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            scene=SceneData{};scene.Name="Shared gameplay liquid forces";scene.Game.emplace();scene.Game->SpawnZ=0;scene.Terrain.emplace();scene.Terrain->WaterEnabled=true;scene.Terrain->WaterLevel=1.5;
            Character light;light.Shape="cube";light.Name="Floating low-density prop";light.Dynamic=true;light.Density=500;light.X=-2;light.Z=-4;light.Y=2;light.Color="#D2AC60";
            Character heavy=light;heavy.Id="heavy-liquid-prop";heavy.Name="Sinking dense prop";heavy.Density=5000;heavy.X=2;heavy.Color="#646B78";
            scene.Characters={light,heavy};auto authored=scene.encode();history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            adventure->cameraPitch=-.25;adventure->cameraDistance=8;
            for(int i=0;i<8;++i)step(("Shared liquid forces "+std::to_string(i+1)).c_str(),[&]{},[&]{return scene.encode()==authored && renderer->liquid_snapshot()!=nullptr;});
            auto runtime=adventure->scene();report["lightY"]=runtime.Characters[0].Y;report["heavyY"]=runtime.Characters[1].Y;
            if(runtime.Characters[0].Y<.5 || runtime.Characters[0].Y>2.2 || runtime.Characters[1].Y>.2)throw std::runtime_error("Shared liquid forces did not produce stable floating and sinking props");
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_HOLLOW_FRACTURE_ONLY",nullptr,0)||GetEnvironmentVariableW(L"STARLIGHT_UI_PANEL_FRACTURE_ONLY",nullptr,0)||GetEnvironmentVariableW(L"STARLIGHT_UI_U_FRACTURE_ONLY",nullptr,0)||GetEnvironmentVariableW(L"STARLIGHT_UI_CONCAVE_FRACTURE_ONLY",nullptr,0)||GetEnvironmentVariableW(L"STARLIGHT_UI_FRACTURE_ONLY",nullptr,0)||GetEnvironmentVariableW(L"STARLIGHT_UI_SCULPT_FRACTURE_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560 || monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Fracture test requires a 2560x1440 monitor");
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            scene=SceneData{};scene.Name="Physical cube fracture";scene.Game.emplace();scene.Game->SpawnZ=0;if(GetEnvironmentVariableW(L"STARLIGHT_UI_FRACTURE_WATER",nullptr,0)){scene.Terrain.emplace();scene.Terrain->WaterEnabled=true;scene.Terrain->WaterLevel=.5;report["liquidFracture"]=true;}
            Character block;block.Shape="cube";block.Name="Breakable block";block.GameplayRole="breakable";block.Z=-4;block.Y=.5;block.Width=1.5;block.Height=1.5;block.Color="#C58542";
            if(GetEnvironmentVariableW(L"STARLIGHT_UI_SCULPT_FRACTURE_ONLY",nullptr,0)){block.Sculpt=prepare_sculpt(object_mesh(block));block.Shape.reset();report["sculptFracture"]=true;}
            if(GetEnvironmentVariableW(L"STARLIGHT_UI_CONCAVE_FRACTURE_ONLY",nullptr,0)){block.Sculpt=sculpt_from(concave_solid_fixture());block.Shape.reset();block.X=-.5;block.Width=block.Height=1;report["concaveFracture"]=true;}
            if(GetEnvironmentVariableW(L"STARLIGHT_UI_U_FRACTURE_ONLY",nullptr,0)){block.Sculpt=sculpt_from(u_solid_fixture());block.Shape.reset();block.X=-1.5;block.Width=block.Height=1;scene.Name="Physical U-shaped fracture";report["uShapedFracture"]=true;}
            if(GetEnvironmentVariableW(L"STARLIGHT_UI_PANEL_FRACTURE_ONLY",nullptr,0)){block.Sculpt=sculpt_from(panel_fixture());block.Shape.reset();block.Width=block.Height=1;scene.Name="Physical open-panel fracture";report["panelFracture"]=true;}
            if(GetEnvironmentVariableW(L"STARLIGHT_UI_HOLLOW_FRACTURE_ONLY",nullptr,0)){block.Sculpt=sculpt_from(torus_fixture());block.Shape.reset();block.Width=block.Height=1;scene.Name="Physical hollow-object fracture";report["hollowFracture"]=true;}
            scene.Characters={block};auto authored=scene.encode();history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            adventure->cameraPitch=-.3;adventure->cameraDistance=7;adventure->fire_button(true);adventure->fire_button(false);
            bool sawPieces=false,tumbled=false,monitored=false,environmentBodies=false,liquidResponse=false;
            for(int i=0;i<6;++i)step(("Physical fracture "+std::to_string(i+1)).c_str(),[&]{},[&]{auto fragments=adventure->presentation().fragments;liquidResponse|=renderer->status().liquid_splashes>0;environmentBodies|=fragments.size()>0&&renderer->status().fog_bodies>=fragments.size();for(const auto& object:renderer->pacing_snapshot().objects)monitored|=object.id.starts_with("debris-");sawPieces|=GetEnvironmentVariableW(L"STARLIGHT_UI_HOLLOW_FRACTURE_ONLY",nullptr,0)?fragments.size()>1:fragments.size()==(GetEnvironmentVariableW(L"STARLIGHT_UI_PANEL_FRACTURE_ONLY",nullptr,0)?2:GetEnvironmentVariableW(L"STARLIGHT_UI_U_FRACTURE_ONLY",nullptr,0)?3:GetEnvironmentVariableW(L"STARLIGHT_UI_CONCAVE_FRACTURE_ONLY",nullptr,0)?20:16);for(const auto& f:fragments)tumbled|=bool(f.part)&&(std::abs(f.rotation[0])+std::abs(f.rotation[1])+std::abs(f.rotation[2])>.01);return scene.encode()==authored;});
            if(!sawPieces || !tumbled || !monitored || !environmentBodies || !adventure->scene().Characters.empty())throw std::runtime_error("Visible fracture did not produce tumbling source pieces");
            if(GetEnvironmentVariableW(L"STARLIGHT_UI_FRACTURE_WATER",nullptr,0)&&!liquidResponse)throw std::runtime_error("Fracture bodies did not create liquid entry splashes");report["liquidResponseObserved"]=liquidResponse;
            report["debrisEnvironmentBodiesObserved"]=environmentBodies;report["debrisMonitoringObserved"]=monitored;report["cutPiecesObserved"]=sawPieces;report["rigidRotationObserved"]=tumbled;report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_PROP_PHYSICS_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560 || monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Prop physics test requires a 2560x1440 monitor");
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            scene=SceneData{};scene.Name="Shared player and prop physics";scene.Game.emplace();scene.Game->SpawnZ=0;
            Character prop;prop.Shape="cube";prop.Name="Pushable prop";prop.Dynamic=true;prop.Density=50;prop.Width=prop.Depth=.4;prop.Z=-1;
            Character rock;rock.Shape="rock";rock.Name="Falling rock";rock.Dynamic=true;rock.X=2;rock.Z=-3;rock.Y=3;
            scene.Characters={prop,rock};auto authored=scene.encode();history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            adventure->cameraPitch=-.4;adventure->cameraDistance=7;mockPush=true;
            for(int i=0;i<5;++i)step(("Player pushes physical prop "+std::to_string(i+1)).c_str(),[&]{if(i==3)mockPush=false;},[&]{return adventure->scene().Characters.size()==2 && scene.encode()==authored;});
            mockPush=false;const auto& runtime=adventure->scene();
            if(runtime.Characters[0].Z>=-1.1 || runtime.Characters[1].Y>=1)throw std::runtime_error("Shared collision world did not push and drop its props");
            auto timing=renderer->pacing_snapshot();bool displayed=false;
            for(const auto& object:timing.objects)if(object.id==prop.Id)displayed=std::abs(object.position.z-runtime.Characters[0].Z)<.2;
            if(!displayed)throw std::runtime_error("Rendered prop did not follow authoritative physics position");
            report["propZ"]=runtime.Characters[0].Z;report["rockY"]=runtime.Characters[1].Y;report["renderedPhysicsPositionMatches"]=displayed;
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_ENVIRONMENT_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560 || monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Environment test requires a 2560x1440 monitor");
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;report["scope"]="Dynamic environment integration; visual quality acceptance remains open";
            scene=SceneData{};scene.Name="Moving environment verification";scene.FogDensity=.08;
            scene.Terrain.emplace();scene.Terrain->WaterEnabled=true;scene.Terrain->WaterLevel=.08;
            Character block;block.Shape="cube";block.Width=2;block.Height=1.2;block.X=-2;block.Route={{2,0}};block.Speed=.4;
            Character bolt;bolt.Shape="chaos-lightning";bolt.X=-2;bolt.Y=.3;bolt.LightningTargetId=block.Id;bolt.TargetX=0;bolt.TargetY=.5;bolt.TargetZ=0;
            Character smoke;smoke.Shape="craxis-smoke";smoke.X=-3;smoke.Z=-2;smoke.Size=2;
            Character lava;lava.Shape="lava";lava.X=4;lava.Z=2;lava.Y=.18;
            Character falling;falling.Shape="rock";falling.Name="Falling rigid body";falling.X=2;falling.Z=-3;falling.Y=3;falling.Dynamic=true;
            Character flame;flame.Shape="fire";flame.Name="Moving fire particles";flame.X=3;flame.Z=-2;flame.Color="#FF7433";
            scene.Characters={block,bolt,smoke,lava,falling,flame};
            for(int i=0;i<8;++i){Character grass;grass.Shape="grass";grass.Name="Wind grass "+std::to_string(i+1);grass.Color="#4C8F30";grass.X=-3+i*.8;grass.Z=3;scene.Characters.push_back(grass);}
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            double firstVolume=0;bool sparks=false,flames=false,movingFog=false;
            for(int i=0;i<8;++i)step(("Dynamic fog, lightning, sparks and liquids "+std::to_string(i+1)).c_str(),[&]{},[&]{auto s=renderer->status();sparks|=s.effect_sparks>0;flames|=s.fire_particles>0;movingFog|=s.fog_moving_bodies>0;if(!i)firstVolume=s.liquid_volume;return s.playing && s.liquid_volume>0 && std::abs(s.liquid_volume-firstVolume)<1e-7;});
            report["environmentThunderBuffers"]=gameplayAudio.submitted.load();report["environmentAudioErrors"]=gameplayAudio.failed.load();if(gameplayAudio.submitted==0||gameplayAudio.failed)throw std::runtime_error("Ordinary environment thunder did not reach the audio device");
            report["movingFogObstaclesObserved"]=movingFog;if(!movingFog)throw std::runtime_error("Moving bodies were not delivered to fog transport");
            report["fireParticlesObserved"]=flames;if(!flames)throw std::runtime_error("Fire particles were not emitted");report["impactSparksObserved"]=sparks;report["liquidVolume"]=firstVolume;
            if(!sparks)throw std::runtime_error("No lightning impact sparks during environment run");
            auto observed=renderer->pacing_snapshot();bool fell=false,animated=false;double effectRate=0;
            for(const auto& object:observed.objects){if(object.id==falling.Id)fell=object.position.y<falling.Y;animated|=object.effectUpdatesPerSecond>0;effectRate=std::max(effectRate,object.effectUpdatesPerSecond);}
            report["rigidBodyFell"]=fell;report["effectUpdatesObserved"]=animated;report["renderFps"]=observed.fps;report["effectUpdatesPerSecond"]=effectRate;
            if(effectRate<observed.fps*.85)throw std::runtime_error("Effect time is not advancing at the rendered frame rate");
            if(!fell || !animated)throw std::runtime_error("Missing rigid-body fall or effect-update telemetry");
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_NAVIGATION_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="NPC obstacle routing";scene.Game.emplace();scene.Game->SpawnZ=0;
            Character a;a.Shape="cube";a.Id="routing-enemy";a.Z=-8;a.CombatHealth=100;a.CombatPursuit=true;
            Character wall;wall.Shape="cube";wall.Width=4;wall.Height=1;wall.Depth=.5;wall.Z=-4;scene.Characters={a,wall};
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);adventure->cameraPitch=-.6;adventure->cameraDistance=12;
            bool around=false;
            for(int i=0;i<9;i++)step(("Obstacle route "+std::to_string(i+1)).c_str(),[&]{},[&]{const auto& c=adventure->scene().Characters[0];around|=std::abs(c.X)>2.4;auto b=object_bounds(c),w=object_bounds(wall);return !(b.low.x<w.high.x && b.high.x>w.low.x && b.low.z<w.high.z && b.high.z>w.low.z);});
            report["pass"]=around && std::hypot(adventure->scene().Characters[0].X,adventure->scene().Characters[0].Z)<2.2;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return report["pass"].get<bool>()?0:1;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_NPC_CONTROL_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="NPC launch and tether";scene.Game.emplace();scene.Game->SpawnZ=0;
            Character a;a.Shape="cube";a.Id="control-target";a.Z=-3;a.CombatHealth=100;scene.Characters={a};
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);adventure->cameraPitch=-.35;adventure->cameraDistance=12;
            step("Launch throws NPC into air",[&]{adventure->control_actor(a.Id,ControlEffect::Launch,{1,true});},[&]{return adventure->scene().Characters[0].Y>.1;});
            step("NPC lands with recovery active",[&]{},[&]{return std::abs(adventure->scene().Characters[0].Y)<.05 && adventure->combat_actors().at(a.Id).control.active==ControlEffect::Launch;});
            step("Launch recovery ends",[&]{},[&]{return !adventure->combat_actors().at(a.Id).control.active;});
            step("Tether pulls NPC back to boundary",[&]{scene.Characters[0].Z=-8;adventure->start(scene);adventure->cameraPitch=-.35;adventure->cameraDistance=12;adventure->control_actor(a.Id,ControlEffect::Tether,{1,true});},[&]{return adventure->scene().Characters[0].Z>=-5.6;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_NPC_EXCHANGE_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Friendly and enemy combat";scene.Game.emplace();scene.Game->SpawnZ=4;
            Character a;a.Shape="cube";a.Id="friendly-fighter";a.X=-1;a.Z=-3;a.CombatTeam=1;a.CombatHealth=100;a.CombatAutoAttack=true;
            Character b=a;b.Id="enemy-fighter";b.X=1;b.CombatTeam=2;scene.Characters={a,b};
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);adventure->cameraPitch=-.5;adventure->cameraDistance=9;
            step("Friendly and enemy aim at each other",[&]{},[&]{auto p=adventure->presentation();bool blue=false,red=false;for(auto t:p.actorTelegraphs){blue|=t.kind==TelegraphKind::Friendly;red|=t.kind==TelegraphKind::Enemy;}return blue && red;});
            step("Both NPCs take damage from exchanged swipes",[&]{},[&]{return adventure->combat_actors().at(a.Id).health<100 && adventure->combat_actors().at(b.Id).health<100 && adventure->health()==100;});
            step("Taunt redirects hostile attention to player",[&]{adventure->control_actor(b.Id,ControlEffect::Taunt,{1,true});},[&]{auto target=adventure->actor_target(b,adventure->combat_actors().at(b.Id));return target && target->id.empty();});
            step("Reset restores NPC health",[&]{adventure->start(scene);adventure->cameraPitch=-.5;},[&]{return adventure->combat_actors().at(a.Id).health==100 && adventure->combat_actors().at(b.Id).health==100;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_COMBAT_AUTHORING_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Combat authoring verification";scene.Game.emplace();Character actor;actor.Shape="cube";actor.Z=-5;scene.Characters={actor};selected=0;selectionIds={actor.Id};history.clear();dirty=false;refresh();show_inspector(0);auto original=scene.encode();std::string configured;
            step("Inspector authors boss combat and grapple override",[&]{SetWindowTextW(inputs.at("combatHealth"),L"250");SendMessage(inputs.at("combatBoss"),BM_SETCHECK,BST_CHECKED,0);SendMessage(inputs.at("combatAutoAttack"),BM_SETCHECK,BST_CHECKED,0);SendMessage(inputs.at("combatPursuit"),BM_SETCHECK,BST_CHECKED,0);SendMessage(inputs.at("grappleMode"),CB_SETCURSEL,2,0);SendMessage(inputs.at("combatControl"),CB_SETCURSEL,1,0);command(ApplyGameplay);configured=scene.encode();},[&]{auto c=scene.Characters[0];return c.CombatHealth==250 && c.CombatBoss && c.CombatAutoAttack && c.CombatPursuit && c.CombatControlEffect==0 && c.GrappleMode==2;});
            step("Undo restores original actor settings",[&]{command(Undo);},[&]{return scene.encode()==original;});
            step("Redo restores the complete combat edit",[&]{command(Redo);},[&]{return scene.encode()==configured;});
            step("Player settings save reduced camera motion",[&]{show_inspector(6);SendMessage(inputs.at("reducedCameraMotion"),BM_SETCHECK,BST_CHECKED,0);command(Apply);},[&]{return scene.Game->ReducedCameraMotion;});
            step("Actual scene save and reload preserve settings",[&]{save_scene(scene,output.parent_path()/"combat-authored.starscene");},[&]{return load_scene(output.parent_path()/"combat-authored.starscene").encode()==scene.encode();});
            auto saved=scene.encode();
            step("Play starts separate boss runtime state",[&]{toggle_menu(0);playback_command(Play);SetFocus(view);},[&]{return adventure->combat_actors().at(actor.Id).maximum==250 && scene.encode()==saved;});
            step("Stop preserves authored settings",[&]{playback_command(Stop);},[&]{return scene.encode()==saved;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_MOVING_GRAPPLE_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Moving character grapple";scene.Game.emplace();scene.Game->SpawnZ=0;scene.Terrain.emplace();
            Character anchor;anchor.Shape="cube";anchor.Width=8;anchor.Height=5;anchor.Z=-5;anchor.CombatHealth=100;anchor.CombatTeam=1;anchor.Route={{8,-5}};anchor.Speed=1;scene.Characters={anchor};auto authored=scene.encode();
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);double attachedX=0;
            step("Grapple attaches to moving friendly actor",[&]{adventure->grapple_button(true);},[&]{return adventure->grapple_motion().state==GrappleMotion::State::Wall && adventure->presentation().showTether;});
            step("Released G keeps wall attachment",[&]{adventure->grapple_button(false);attachedX=adventure->position.x;},[&]{return adventure->grapple_motion().state==GrappleMotion::State::Wall && adventure->position.x>attachedX+.5;});
            step("Attached player channels without moving the friendly actor",[&]{adventure->fire_button(true);adventure->fire_button(false);},[&]{return adventure->basic_attack().winding && adventure->grapple_motion().state==GrappleMotion::State::Wall && adventure->combat_actors().at(anchor.Id).health==100 && scene.encode()==authored;});
            step("First person retains moving grapple",[&]{adventure->cameraDistance=0;},[&]{return !adventure->presentation().showBody && adventure->presentation().showTether;});
            step("Wall jump detaches and removes tether",[&]{adventure->jump();},[&]{return adventure->grapple_motion().state==GrappleMotion::State::Free && !adventure->presentation().showTether;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_GRAPPLE_EDGE_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Grapple top-edge traversal";scene.Game.emplace();scene.Game->SpawnZ=0;scene.Terrain.emplace();
            Character wall;wall.Shape="cube";wall.Width=8;wall.Height=2;wall.Depth=3;wall.Z=-5;scene.Characters={wall};
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            step("Grapple reaches low wall",[&]{adventure->grapple_button(true);adventure->cameraPitch=-.35;},[&]{return adventure->grapple_motion().state==GrappleMotion::State::Wall;});
            step("Climb raises character toward ledge",[&]{adventure->grapple_button(false);mockClimb=true;},[&]{return adventure->position.y>2.5;});
            step("Climb passes over wall top",[&]{},[&]{return adventure->grapple_motion().state==GrappleMotion::State::Free && adventure->position.y>3.5;});
            mockClimb=false;
            step("Terrain grapple uses floor without wall latch",[&]{adventure->start(scene);adventure->cameraPitch=-.8;adventure->grapple_button(true);},[&]{return adventure->movement_resources().grappleCharges==1 && adventure->grapple_motion().state!=GrappleMotion::State::Wall;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_PERCEPTION_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Blind and taunt test";scene.Game.emplace();scene.Game->SpawnZ=0;
            Character a;a.Shape="cube";a.X=-2;a.Z=-5;a.CombatHealth=100;Character b=a;b.Id="second-perception-enemy";b.X=2;scene.Characters={a,b};
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);adventure->cameraPitch=-.4;
            step("Both enemies visible before taunt",[&]{},[&]{return adventure->visible_scene().Characters.size()==2;});
            step("Taunt hides everyone except its source",[&]{adventure->apply_control(ControlEffect::Taunt,{combat_identity(a.Id),false},true);},[&]{return adventure->visible_scene().Characters.size()==1 && adventure->scene().Characters.size()==2;});
            step("Cleanse restores visibility and grants immunity",[&]{adventure->cleanse_control();},[&]{return adventure->visible_scene().Characters.size()==2 && adventure->crowd_control().immunity>0;});
            step("Blind reduces sight and shows status",[&]{adventure->start(scene);adventure->cameraPitch=-.4;adventure->apply_control(ControlEffect::Blind,{7,true},true);},[&]{return adventure->sight_range()==30 && adventure->combat_display().effect==ControlEffect::Blind;});
            step("Reset restores normal sight",[&]{adventure->start(scene);adventure->cameraPitch=-.4;},[&]{return adventure->sight_range()==60 && !adventure->crowd_control().active;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_STRESS_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            auto envPath=[](const wchar_t* name){std::wstring v(32768,0);v.resize(GetEnvironmentVariableW(name,v.data(),DWORD(v.size())));return std::filesystem::path(v);};
            const auto scenePath=envPath(L"STARLIGHT_STRESS_SCENE"),planPath=envPath(L"STARLIGHT_STRESS_PLAN");
            const bool barScene=GetEnvironmentVariableW(L"STARLIGHT_STRESS_BAR",nullptr,0)>0;
            json plan;if(!barScene){std::ifstream in(planPath);if(!in)throw std::runtime_error("Stress plan unreadable");in>>plan;}
            auto stage=[&](const std::string& m){{std::ofstream f(output.string()+".stage",std::ios::app);f<<m<<" @"<<GetTickCount64()<<"\n";}};
            stage("begin");
            std::atomic<bool> progressed{false};
            HANDLE mainThread=nullptr;DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&mainThread,0,FALSE,DUPLICATE_SAME_ACCESS);
            std::thread([&,mainThread]{
                for(int i=0;i<120&&!progressed;++i)Sleep(100);
                if(progressed)return;
                {std::map<std::string,int> hist;for(int i=0;i<600;++i){hist[std::string(gCrumb)]++;Sleep(1);}
                 std::ofstream h(output.string()+".crumbs");for(auto& [k,v]:hist)h<<k<<" "<<v<<"\n";}
                SuspendThread(mainThread);CONTEXT ctx{};ctx.ContextFlags=CONTEXT_FULL;GetThreadContext(mainThread,&ctx);
                HANDLE process=GetCurrentProcess();SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS);SymInitialize(process,nullptr,TRUE);
                STACKFRAME64 f{};f.AddrPC.Offset=ctx.Rip;f.AddrPC.Mode=AddrModeFlat;f.AddrFrame.Offset=ctx.Rbp;f.AddrFrame.Mode=AddrModeFlat;f.AddrStack.Offset=ctx.Rsp;f.AddrStack.Mode=AddrModeFlat;
                std::ofstream out(output.string()+".stack");out<<"base "<<std::hex<<reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))<<std::dec<<"\n";out<<"msg "<<gMsg<<" crumb "<<gCrumb<<"\n";
                for(int i=0;i<48&&StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,mainThread,&f,&ctx,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr);++i){
                    char buffer[sizeof(SYMBOL_INFO)+256]{};auto* sym=reinterpret_cast<SYMBOL_INFO*>(buffer);sym->SizeOfStruct=sizeof(SYMBOL_INFO);sym->MaxNameLen=255;DWORD64 disp=0;
                    HMODULE module=nullptr;wchar_t name[260]{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(f.AddrPC.Offset),&module);if(module)GetModuleFileNameW(module,name,260);
                    out<<i<<" "<<std::hex<<f.AddrPC.Offset<<std::dec<<" "<<(SymFromAddr(process,f.AddrPC.Offset,&disp,sym)?sym->Name:"?")<<" +"<<disp<<" "<<utf8(name)<<"\n";
                }
                out.flush();ResumeThread(mainThread);
            }).detach();
            if(GetEnvironmentVariableW(L"STARLIGHT_STRESS_NOHUD",nullptr,0)){pacingShown=false;renderer->pacing_enabled(false);}
            std::vector<std::pair<std::uint64_t,std::string>> profileSamples;profileSamples.reserve(400000);
            const bool profiling=GetEnvironmentVariableW(L"STARLIGHT_STRESS_PROFILE",nullptr,0)>0;
            std::atomic<bool> profileOn{false};
            std::atomic<bool> sampling{true};
            std::thread sampler([&]{while(sampling){PROCESS_MEMORY_COUNTERS_EX m{};m.cb=sizeof(m);if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m)))starlight_diag::evlog("mem",double(m.WorkingSetSize)/1048576.,double(m.PrivateUsage)/1048576.,double(m.PeakWorkingSetSize)/1048576.);Sleep(500);}});
            std::thread profiler([&]{
                if(!profiling)return;
                while(sampling){
                    if(profileOn){SuspendThread(mainThread);CONTEXT ctx{};ctx.ContextFlags=CONTEXT_CONTROL;if(GetThreadContext(mainThread,&ctx))profileSamples.emplace_back(ctx.Rip,std::string(gCrumb));ResumeThread(mainThread);}
                    Sleep(1);
                }
            });
            struct StopSamplers{std::atomic<bool>& run;std::thread& first;std::thread& second;~StopSamplers(){run=false;if(first.joinable())first.join();if(second.joinable())second.join();}} stopSamplers{sampling,sampler,profiler};
            starlight_diag::evlog("STRESS.LOAD.BEGIN");
            {static bool shot=false;if(GetEnvironmentVariableW(L"STARLIGHT_STRESS_VISUAL",nullptr,0)>0)loading_observer()=[&](double f){if(!shot&&f>.4){shot=true;capture(output.parent_path()/"loading-screen.bmp");}};}
            if(barScene){plan=build_bar_scene_through_editor();history.clear();dirty=false;refresh();toggle_menu(0);}
            else{bool repaired=false;auto next=open_scene_data(scenePath,repaired);if(!next)throw std::runtime_error("Stress scene failed to load");
            render_scene(*next);scene=std::move(*next);history.clear();dirty=false;refresh();toggle_menu(0);}
            starlight_diag::evlog("STRESS.LOAD.END");stage("loaded");
            playback_command(Play);SetFocus(view);starlight_diag::evlog("STRESS.PLAY");stage(std::string("play adventure=")+(adventure?"1":"0"));
            const auto firstFrames=renderer->status().frames;
            bool ready=false;
            {const auto giveUp=GetTickCount64()+20000;while(GetTickCount64()<giveUp){if(adventure && renderer->status().frames>=firstFrames+30){ready=true;break;}pump(false);MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}}
            progressed=true;
            report["loading"]={{"geometryMs",lastPreparation.geometryMs},{"collisionMs",lastPreparation.collisionMs},{"breakableRocksMs",lastPreparation.fractureMs},{"uploadMs",lastPreparation.uploadMs},{"totalMs",lastPreparation.totalMs},{"breakablesPrepared",lastPreparation.breakablesPrepared},{"breakablesQueued",lastPreparation.breakablesQueued},{"counters",lastPreparation.counters},{"summary",lastPreparation.summary},{"note","measured inside the shared scene preparation; gameplay numbers start after it"}};
            report["readiness"]={{"ready",ready},{"framesAfterPlay",renderer->status().frames-firstFrames},{"adventureCreated",bool(adventure)}};
            if(!ready){
                starlight_diag::evlog("STRESS.READY.TIMEOUT");sampling=false;sampler.join();profiler.join();
                report["pass"]=false;report["setupValid"]=false;report["failure"]="Readiness wait timed out after 20 s: the adventure did not exist or fewer than 30 frames were rendered after Play";
                atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 2;
            }
            if(GetEnvironmentVariableW(L"STARLIGHT_STRESS_DUMP",nullptr,0)){json all=json::array();for(const auto& c:adventure->scene().Characters){auto b=object_bounds(c);all.push_back({{"id",c.Id},{"shape",c.Shape.value_or("")},{"low",{b.low.x,b.low.y,b.low.z}},{"high",{b.high.x,b.high.y,b.high.z}}});}std::ofstream(output.string()+".bounds.json")<<all.dump();}
            starlight_diag::evlog("STRESS.READY");
            stressTarget=plan.value("boss",std::string{});stressAmp=plan.value("amp",.5);stressAmpBreak=plan.value("ampBreak",.1);
            if(plan.contains("camera")){const auto& c=plan["camera"];const char* k[8]={"yawAmp","yawPeriod","pitchBase","pitchAmp","pitchPeriod","distBase","distAmp","distPeriod"};for(int i=0;i<8;++i)stressCam[i]=c.value(k[i],stressCam[i]);}
            stressRoute.clear();if(plan.contains("settleRoute"))for(const auto& key:plan["settleRoute"])stressRoute.push_back({key[0].get<double>(),key[1].get<double>(),key[2].get<double>(),key[3].get<double>()});
            stressIds.clear();if(plan.contains("rocks"))for(const auto& r:plan["rocks"])if(r.value("expect",false))stressIds.push_back(r["id"].get<std::string>());stressExpected=int(stressIds.size());
            const bool hasBoss=!stressTarget.empty();
            const bool visual=GetEnvironmentVariableW(L"STARLIGHT_STRESS_VISUAL",nullptr,0)>0;
            adventure->cameraPitch=-.62;
            const double simAtFire=adventure->seconds();const ULONGLONG wallAtFire=GetTickCount64();
            // The scripted player always fights the boss; breakable rocks lie in its line of fire and the run is invalid unless they break.
            stressHeading=plan.value("heading",0.);stressAimRocks=false;if(plan.contains("player")){stressHomeX=plan["player"].value("x",0.);stressHomeZ=plan["player"].value("z",0.);}stressPhaseA=plan.value("phaseA",25.);stressFireSim=simAtFire;mockStress=true;if(hasBoss)adventure->fire_button(true);
            starlight_diag::evlog("STRESS.DETAIL",mesh_detail());starlight_diag::evlog("STRESS.FIRE");const auto bypassFire=std::make_pair(bypass_counters().staticBounds.load(),bypass_counters().staticMeshes.load());
            profileOn=true;
            const int minBeats=plan.value("beats",80),maxBeats=minBeats+plan.value("extraBeats",60);
            const std::uint64_t settleMs=plan.value("settleMs",35000);
            int beat=0,healBeats=0,attackBeats=0,dodgesOk=0,dodgesRefused=0,shots=0;ULONGLONG settleEnd=0;bool allBrokenLogged=false;int lastShotSeg=-2,lastShotBreaks=-1;
            std::vector<std::string> shotNames;
            auto snapshot=[&](const std::string& label){if(!visual||shots>=40)return;auto file=output.parent_path()/("shot-"+std::to_string(shots++)+"-"+label+".bmp");if(capture(file))shotNames.push_back(utf8(file.filename().wstring()));};
            while(beat<maxBeats){
                step(("Stress: exchange "+std::to_string(beat+1)).c_str(),[&]{
                    if(!hasBoss)return;
                    // The scripted player also heals whenever its health is low, like a person would.
                    bool heal=(beat%8)>=4 || adventure->health()<50;
                    adventure->healing_cast("",heal);adventure->healing_cast(stressTarget,heal);
                    if(beat%6==2){if(adventure->dodge(beat%12<6?-1:1,0))++dodgesOk;else ++dodgesRefused;}
                    adventure->fire_button(!heal);
                    if(heal)++healBeats;else ++attackBeats;
                },[&]{return true;});
                ++beat;
                if(adventure->health()<=0){starlight_diag::evlog("STRESS.PLAYER.DEAD");break;}
                if(stressSeg!=lastShotSeg || stressBreaksSeen!=lastShotBreaks){snapshot("seg"+std::to_string(stressSeg)+"-breaks"+std::to_string(stressBreaksSeen));lastShotSeg=stressSeg;lastShotBreaks=stressBreaksSeen;}
                else if(visual && beat%6==3)snapshot("beat"+std::to_string(beat));
                if(beat<minBeats)continue;
                const bool allBroken=stressExpected==0 || stressBreaksSeen>=stressExpected;
                if(!allBroken)continue;
                if(!allBrokenLogged){allBrokenLogged=true;starlight_diag::evlog("STRESS.ALLBROKEN");starlight_diag::evlog("STRESS.SETTLE.BEGIN");if(stressExpected==0)stressLastBreakWall=GetTickCount64();}
                settleEnd=stressLastBreakWall+settleMs;
                if(GetTickCount64()>=settleEnd)break;
            }
            const ULONGLONG endWall=GetTickCount64();const double simAtEnd=adventure->seconds();
            starlight_diag::evlog("STRESS.END");
            if(allBrokenLogged)starlight_diag::evlog("STRESS.SETTLE.END");
            mockStress=false;if(hasBoss)adventure->fire_button(false);
            const auto bypassEnd=std::make_pair(bypass_counters().staticBounds.load(),bypass_counters().staticMeshes.load());
            profileOn=false;sampling=false;sampler.join();profiler.join();
            if(profiling){std::ofstream out(output.string()+".rip");for(const auto& [rip,crumb]:profileSamples){HMODULE module=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(rip),&module);wchar_t name[260]{};if(module)GetModuleFileNameW(module,name,260);out<<std::hex<<(rip-reinterpret_cast<std::uint64_t>(module))<<std::dec<<" "<<std::filesystem::path(name).filename().string()<<" "<<crumb<<'\n';}}
            const auto& st=stressStats;
            std::vector<std::string> missed;if(plan.contains("rocks")){const auto& live=adventure->scene().Characters;for(const auto& r:plan["rocks"])if(r.value("expect",false)&&std::any_of(live.begin(),live.end(),[&](const auto& c){return c.Id==r["id"].get<std::string>();}))missed.push_back(r["id"].get<std::string>());}
            const double wallSeconds=double(endWall-wallAtFire)/1000.,simSeconds=simAtEnd-simAtFire;
            const double settleAfterLast=stressLastBreakWall&&allBrokenLogged?double(endWall-stressLastBreakWall)/1000.:0;
            auto check=[&](const char* name,bool ok,json value){return json{{"name",name},{"pass",ok},{"value",std::move(value)}};};
            json checks=json::array();
            checks.push_back(check("readiness",ready,report["readiness"]));
            checks.push_back(check("simulation-advanced",simSeconds>=wallSeconds*.25&&simSeconds>2,json{{"simSeconds",simSeconds},{"wallSeconds",wallSeconds},{"simulationTicks",st.ticks},{"rendererFrames",renderer->status().frames}}));
            if(hasBoss){
                checks.push_back(check("player-moved",st.pathLength>1.,json{{"pathLengthM",st.pathLength}}));
                if(plan.value("bossPursuit",false))checks.push_back(check("boss-pursued",st.bossMaxDisp>.5,json{{"maxDisplacementM",st.bossMaxDisp}}));else checks.push_back(check("boss-anchored-as-designed",st.bossMaxDisp<.05,json{{"maxDisplacementM",st.bossMaxDisp}}));
                checks.push_back(check("boss-damaged",st.bossMinHealth<st.bossStartHealth,json{{"start",st.bossStartHealth},{"min",st.bossMinHealth}}));
                checks.push_back(check("player-attacked-and-healed",st.playerMinHealth<100&&healBeats>0,json{{"minHealth",st.playerMinHealth},{"maxHealth",st.playerMaxHealth},{"attackBeats",attackBeats},{"healBeats",healBeats}}));
                checks.push_back(check("player-survived",adventure->health()>0&&st.playerMinHealth>0,json{{"minHealth",st.playerMinHealth},{"endHealth",adventure->health()}}));
                checks.push_back(check("telegraphs-seen",st.telegraphTicks>0,json{{"ticksWithTelegraph",st.telegraphTicks}}));
                checks.push_back(check("dodges",dodgesOk>0,json{{"accepted",dodgesOk},{"refused",dodgesRefused}}));
            }
            if(stressExpected>0){
                checks.push_back(check("all-expected-rocks-broken",missed.empty(),json{{"expected",stressExpected},{"broken",stressBreaksSeen},{"stillIntact",missed}}));
                checks.push_back(check("debris-appeared",st.fragPeak>0,json{{"peakFragments",st.fragPeak},{"peakDustParticles",st.dustPeak}}));
                checks.push_back(check("settle-30s-after-final-break",settleAfterLast>=30.,json{{"secondsObservedAfterFinalBreak",settleAfterLast}}));
                checks.push_back(check("debris-cleaned-up",st.fragPeakLate<=16,json{{"peakFragmentsMoreThan20sAfterFinalBreak",st.fragPeakLate},{"fragmentsAtEnd",adventure->fragment_count()},{"dustAtEnd",adventure->dust_count()},{"note","what remains late is boss projectile impact sparks, not rock rubble"}}));
            }
            checks.push_back(check("renderer-error-free",renderer->status().error.empty(),renderer->status().error));
            bool all=true;for(const auto& c:checks)all&=c["pass"].get<bool>();
            report["stressChecks"]=checks;report["screenshots"]=shotNames;report["beats"]=beat;
            report["breaks"]=adventure->break_count();report["fragmentsAtEnd"]=adventure->fragment_count();report["playerHealth"]=adventure->health();
            {const auto cs=ObjectCache::shared().stats();report["objectCache"]={{"boundsHits",cs.hits},{"boundsMisses",cs.misses},{"meshHits",cs.meshHits},{"meshMisses",cs.meshMisses},{"entries",cs.entries}};report["uncachedStaticQueries"]={{"bruteForceRayTests",brute_force_ray_tests().load()},{"boundsDuringRun",bypassEnd.first-bypassFire.first},{"meshesDuringRun",bypassEnd.second-bypassFire.second},{"note","bounds/meshes of non-cube objects that do not move, computed outside the shared cache"}};if(bypass_trace()){json sites=json::array();const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));for(const auto& site:bypass_counters().sites)if(site.count.load())sites.push_back({{"rva",site.address.load()-base},{"count",site.count.load()},{"kind",site.kind.load()?"mesh":"bounds"}});report["uncachedCallers"]=sites;}}
            report["runtimeCounters"]=runtime_counters_json(*adventure,renderer->status());
            report["setupValid"]=all;report["pass"]=all;
            atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return all?0:3;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_BATTLE_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            const bool pacingTest=GetEnvironmentVariableW(L"STARLIGHT_UI_PACING",nullptr,0)>0;
            if(pacingTest){
                if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560 || monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Pacing test requires a 2560x1440 monitor; display settings were not changed");
                pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;
            }
            scene=SceneData{};scene.Name="Mock battle - live movement and cones";scene.Game.emplace();scene.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.X=1;enemy.Z=-2.5;enemy.CombatHealth=1000;enemy.CombatBoss=true;enemy.CombatPursuit=true;enemy.CombatAutoAttack=true;scene.Characters={enemy};
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            adventure->cameraPitch=-.65;adventure->fire_button(true);mockBattle=true;starlight_diag::evlog("BATTLE.START");
            bool both=false;double startingEnemy=1000;
            for(int beat=0;beat<44;beat++){
                step(("Mock battle: exchange "+std::to_string(beat+1)).c_str(),[&]{
                    // Alternate attack-only exchanges and healing exchanges so each color is readable.
                    bool heal=(beat%8)>=4;
                    adventure->healing_cast("",heal);adventure->healing_cast(enemy.Id,heal);
                    if(beat%6==2)adventure->dodge(beat%12<6?1:-1,0);
                    adventure->fire_button(!heal);
                },[&]{auto p=adventure->presentation();both|=p.showTelegraph && !p.actorTelegraphs.empty();return adventure->health()>0 && adventure->combat_actors().at(enemy.Id).health>0;});
            }
            mockBattle=false;starlight_diag::evlog("BATTLE.END");
            if(pacingTest){auto timing=renderer->pacing_snapshot();json objects=json::array();for(const auto& o:timing.objects)objects.push_back({{"name",o.name},{"id",o.id},{"poseUpdatesPerSecond",o.updatesPerSecond},{"unchangedMs",o.unchangedMs},{"moving",o.moving}});
                report["pacing"]={{"renderFps",timing.fps},{"p50Ms",timing.p50},{"p95Ms",timing.p95},{"p99Ms",timing.p99},{"worstMs",timing.worst},{"frameMs",timing.frameMs},{"objects",objects},{"scope","Last two seconds; CPU render completion and object transforms"}};
                if(timing.frameMs.size()<10 || timing.objects.size()<3)throw std::runtime_error("Pacing monitor did not collect frames and objects");
            }
            report["simultaneousTelegraphs"]=both;
            report["playerDamaged"]=adventure->health()<100;
            report["enemyDamaged"]=adventure->combat_actors().at(enemy.Id).health<startingEnemy;
            report["pass"]=both;
            atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return both?0:1;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_PURSUIT_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Smooth pursuit test";scene.Game.emplace();scene.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.X=2;enemy.Z=-6;enemy.CombatHealth=100;enemy.CombatPursuit=true;enemy.CombatAutoAttack=true;scene.Characters={enemy};auto authored=scene.encode();
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            step("Enemy approaches at continuous heading",[&]{adventure->start(scene);adventure->cameraPitch=-.65;},[&]{auto c=adventure->scene().Characters[0];return c.X<2 && c.Z>-6 && std::hypot(c.X,c.Z)>3;});
            step("Approach continues without snapping",[&]{},[&]{auto c=adventure->scene().Characters[0];return std::hypot(c.X,c.Z)>1.8 && std::hypot(c.X,c.Z)<4;});
            step("Enemy brakes at attack distance",[&]{},[&]{auto c=adventure->scene().Characters[0];return std::hypot(c.X,c.Z)>1.8 && std::hypot(c.X,c.Z)<2.2;});
            Vec3 before{};
            step("Moving player causes smooth repositioning",[&]{auto c=adventure->scene().Characters[0];before={c.X,c.Y,c.Z};for(int i=0;i<90;i++)adventure->move(1,0,1./120.);},[&]{auto c=adventure->scene().Characters[0];return c.X>before.x && (Vec3{c.X,c.Y,c.Z}-before).length()<4.3;});
            step("Reset restores authored position",[&]{adventure->start(scene);adventure->apply_control(ControlEffect::Stun,{1,true},true);adventure->control_actor(enemy.Id,ControlEffect::Stun,{1,true});},[&]{auto c=adventure->scene().Characters[0];return c.X==2 && c.Z==-6 && scene.encode()==authored;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_SWIPE_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Enemy swipe test";scene.Game.emplace();scene.Game->SpawnZ=0;scene.Game->Collision=false;
            Character enemy;enemy.Shape="cube";enemy.Z=-2;enemy.CombatHealth=100;enemy.CombatBoss=true;enemy.CombatAutoAttack=true;scene.Characters={enemy};auto authored=scene.encode();
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            step("Enemy red swipe fills before impact",[&]{adventure->start(scene);adventure->cameraPitch=-.65;adventure->select_target();},[&]{return adventure->health()==100 && adventure->presentation().actorTelegraphs.size()==1;});
            step("Swipe resolves damage after channel",[&]{},[&]{return adventure->health()==90;});
            step("Leaving melee range avoids repeat",[&]{adventure->position.x=4;},[&]{return adventure->health()==90;});
            step("Stun interrupts enemy attack",[&]{adventure->start(scene);adventure->cameraPitch=-.65;adventure->select_target();for(int i=0;i<4;i++)adventure->control_actor(enemy.Id,ControlEffect::Stun,{1,true});},[&]{return adventure->health()==100 && adventure->presentation().actorTelegraphs.empty();});
            step("Runtime attacks preserve authored scene",[&]{},[&]{return scene.encode()==authored;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_CONTROL_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Crowd control presentation test";scene.Game.emplace();scene.Game->SpawnZ=0;history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            auto reset=[&]{adventure->start(scene);};
            step("Stun shows orbiting birds and blocks movement",[&]{reset();adventure->apply_control(ControlEffect::Stun,{1,true},true);},[&]{return adventure->crowd_control().active==ControlEffect::Stun && adventure->presentation().control==ControlEffect::Stun;});
            step("Early cleanse restores control and immunity",[&]{adventure->cleanse_control();},[&]{return !adventure->crowd_control().active && adventure->crowd_control().immunity>0;});
            step("Sleep lowers the placeholder",[&]{reset();adventure->apply_control(ControlEffect::Sleep,{1,true},true);},[&]{return adventure->crowd_control().active==ControlEffect::Sleep;});
            step("Damage starts waking without instant recovery",[&]{adventure->take_damage(1);},[&]{return adventure->crowd_control().waking && adventure->crowd_control().immunity==0;});
            step("Waking completes before immunity",[&]{},[&]{return !adventure->crowd_control().active && adventure->crowd_control().immunity>0;});
            step("Knockdown lowers the body without displacement",[&]{reset();adventure->apply_control(ControlEffect::Knockdown,{1,true},true);},[&]{return std::hypot(adventure->position.x,adventure->position.z)<.01;});
            step("First-person pull reaction recovers",[&]{reset();adventure->cameraDistance=0;adventure->apply_control(ControlEffect::Pull,{1,true},true,false,{0,1.7,-5});},[&]{return adventure->position.z<-3.8 && !adventure->crowd_control().active && std::abs(adventure->camera_direction().y)<.01;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_COMBAT_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Manual aimed combat test";scene.Game.emplace();scene.Game->SpawnZ=0;
            const bool slopeCone=GetEnvironmentVariableW(L"STARLIGHT_UI_SLOPE_CONE",nullptr,0)!=0;
            if(slopeCone){scene.Name="Sloped ground cone test";scene.Terrain.emplace();for(int z=0;z<TerrainData::N;z++)for(int x=0;x<TerrainData::N;x++)scene.Terrain->Heights[z*TerrainData::N+x]=-(z*TerrainData::Step-TerrainData::Half)*.2;}
            Character a;a.Shape="cube";a.GameplayRole="breakable";a.X=-.6;a.Y=1;a.Z=-4;a.Width=.4;
            if(slopeCone){a.Z=-9;a.Y=scene.Terrain->height_at(a.X,a.Z)+.1;}
            Character b=a;b.Id="second-combat-cube";b.X=.6;scene.Characters={a,b};auto authored=scene.encode();
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            step("Tab highlights without firing",[&]{SendMessage(view,WM_KEYDOWN,VK_TAB,0);},[&]{return !adventure->selected_target().empty() && adventure->scene().Characters.size()==2;});
            step("Tap starts one complete firing cycle",[&]{SendMessage(view,WM_KEYDOWN,'1',0);SendMessage(view,WM_KEYUP,'1',0);},[&]{return !adventure->basic_attack().held && adventure->basic_attack().winding && adventure->scene().Characters.size()==2;});
            step("Released attack continues its telegraph",[&]{},[&]{return adventure->basic_attack().winding && adventure->presentation().showTelegraph;});
            step("Completed cycle shatters both cubes",[&]{},[&]{return adventure->scene().Characters.empty() && !adventure->presentation().fragments.empty() && !adventure->basic_attack().winding && scene.encode()==authored;});
            step("Released cycle never repeats",[&]{},[&]{return !adventure->basic_attack().held && !adventure->basic_attack().winding;});
            step("Fragment cleanup remains cosmetic",[&]{},[&]{return adventure->scene().Characters.empty() && scene.encode()==authored;});
            step("Fragments expire without gameplay objects",[&]{},[&]{return adventure->presentation().fragments.empty() && adventure->scene().Characters.empty();});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_WEAPONS_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Weapon and sound presentation";scene.Game.emplace();scene.Game->SpawnZ=0;scene.Terrain.emplace();
            Character target;target.Shape="cube";target.Width=2;target.Height=2;target.Depth=1;target.Z=-4;target.GameplayRole="breakable";scene.Characters={target};
            auto authored=scene.encode();history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            step("Third-person dual pistols",[&]{adventure->cameraDistance=4;adventure->cameraPitch=-.25;},[&]{return adventure->presentation().showBody && adventure->presentation().showWeapons;});
            step("First-person pistols below aim",[&]{adventure->cameraDistance=0;},[&]{return !adventure->presentation().showBody && adventure->presentation().showWeapons;});
            auto soundsBefore=gameplayAudio.submitted.load();
            step("Committed attack begins",[&]{adventure->fire_button(true);adventure->fire_button(false);},[&]{return adventure->basic_attack().winding && adventure->presentation().shots==0;});
            step("Committed attack continues",[&]{},[&]{return adventure->basic_attack().winding;});
            step("Shot and shatter submit original sounds",[&]{},[&]{return adventure->presentation().shots==1 && adventure->presentation().shatters==1 && gameplayAudio.submitted>=soundsBefore+2 && gameplayAudio.failed==0 && scene.encode()==authored;});
            soundsBefore=gameplayAudio.submitted.load();
            step("Stun birds submit chirps",[&]{adventure->cameraDistance=4;adventure->apply_control(ControlEffect::Stun,{2,true},true);},[&]{return gameplayAudio.submitted>soundsBefore && gameplayAudio.failed==0;});
            report["audioSubmitted"]=gameplayAudio.submitted.load();report["audioErrors"]=gameplayAudio.failed.load();
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_MOVEMENT_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Movement presentation test";scene.Game.emplace();scene.Terrain.emplace();
            Character wall;wall.Shape="cube";wall.Width=8;wall.Height=8;wall.Depth=1;wall.Z=-5;scene.Characters={wall};
            history.clear();dirty=false;refresh();inspectorShown=false;ShowWindow(inspector,SW_HIDE);toggle_menu(0);command(InteractiveBuild);SetFocus(view);
            step("Third-person placeholder",[&]{explorer->cameraDistance=5;camera();},[&]{return explorer->presentation().showBody;});
            step("First-person body hidden",[&]{explorer->cameraDistance=0;camera();},[&]{return player_presentation(explorer->presentation()).empty();});
            step("Hook travels with a visible tether",[&]{explorer->cameraDistance=5;explorer->cameraYaw=0;explorer->cameraPitch=0;SendMessage(view,WM_KEYDOWN,'G',0);},[&]{return explorer->presentation().showTether;});
            step("Wall grapple completes after hook travel",[&]{},[&]{return explorer->grapple_motion().state==GrappleMotion::State::Wall && explorer->presentation().showTether;});
            step("Release keeps wall attachment",[&]{SendMessage(view,WM_KEYUP,'G',0);},[&]{return explorer->grapple_motion().state==GrappleMotion::State::Wall;});
            step("Wall jump removes tether",[&]{SendMessage(view,WM_KEYDOWN,VK_SPACE,0);},[&]{return !explorer->presentation().showTether;});
            auto fresh=[&]{explorer->start(scene,false);for(int i=0;i<60;i++)explorer->move(0,0,1./120.);};
            step("Sprint drains stamina",[&]{fresh();explorer->sprint(true);for(int i=0;i<480;i++)explorer->move(1,0,1./120.);explorer->sprint(false);},[&]{return explorer->movement_resources().stamina<500 && explorer->movement_resources().stamina>430;});
            step("Dodge spends one bar",[&]{fresh();explorer->dodge(1,0);},[&]{return explorer->movement_resources().dodgeCharges==1;});
            step("Lunge spends both bars",[&]{fresh();explorer->mobility_button(true,0,1,.23);},[&]{return explorer->movement_resources().dodgeCharges==0;});
            step("Slide ends crouched",[&]{fresh();explorer->posture_button(true,0,1,.01);},[&]{return explorer->posture()==1;});
            step("Prone lowers placeholder",[&]{fresh();explorer->posture_button(true,0,0,.4);explorer->posture_button(false,0,0);},[&]{return explorer->posture()==2 && explorer->presentation().bodyHeight==.5;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_WORKSHOP_ONLY",nullptr,0)){
            scene=SceneData{};scene.Name="Three blocks, three possibilities";scene.Game.emplace();scene.Terrain.emplace();history.clear();dirty=false;refresh();inspectorShown=false;ShowWindow(inspector,SW_HIDE);toggle_menu(0);command(LearnWorkshop);
            step("Three separate cube objectives",[&]{},[&]{return questTracker.views()[0].objectives.size()==3 && !questTracker.views()[0].complete;});
            for(int i=0;i<3;++i)step(("Place distinct cube "+std::to_string(i+1)).c_str(),[&]{explorer->position={double((i-1)*4),1.7,3};explorer->yaw=0;SetWindowTextW(shape,L"cube");command(Add);},[&]{return scene.Characters.size()==size_t(i+1) && questTracker.views()[0].objectives[i].text.find("Press F")!=std::string::npos;});
            auto aim=[&](int index){const auto& c=scene.Characters[index];auto b=object_bounds(c);explorer->position={c.X,1.7,c.Z+2};explorer->pitch=std::atan2((b.low.y+b.high.y)*.5-1.7,2.);explorer->yaw=0;camera();SendMessage(view,WM_KEYDOWN,'F',0);};
            auto click=[&](int id){SendMessage(GetDlgItem(menuPanel,id),BM_CLICK,0,0);};
            step("F changes second cube instruction only",[&]{aim(1);},[&]{return questTracker.views()[0].objectives[1].text.find("Set ")!=std::string::npos && questTracker.views()[0].objectives[0].text.find("Press F")!=std::string::npos;});
            step("Breakable completes second objective first",[&]{click(BehaviorBreak);},[&]{return questTracker.views()[0].objectives[1].complete && !questTracker.views()[0].complete;});
            step("Carry moves third cube",[&]{aim(2);click(BehaviorCarry);explorer->position.x+=1;update_carry();SendMessage(view,WM_KEYDOWN,'F',0);},[&]{return questTracker.views()[0].objectives[2].complete && !questTracker.views()[0].objectives[0].complete;});
            step("Open completes first cube and whole workshop",[&]{aim(0);click(BehaviorOpen);toggle_menu(0);command(InteractiveBuild);command(Frame);},[&]{return questTracker.views()[0].complete && scene.Characters.size()==3;});
            step("Completed workshop opens detailed journal",[&]{UpdateWindow(questTracker.handle());auto rect=questTracker.action_bounds(creationGuide.definition.Quests[0].Id);if(!rect)throw std::runtime_error("Workshop title missing");SendMessage(questTracker.handle(),WM_LBUTTONUP,0,MAKELPARAM((rect->left+rect->right)/2,(rect->top+rect->bottom)/2));},[&]{return questTracker.details_open() && questTracker.views()[0].complete;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_OBJECTS_ONLY",nullptr,0)) {
            scene=SceneData{};scene.Name="Object behavior workshop";scene.Game.emplace();scene.Game->SpawnZ=2.5;scene.Terrain.emplace();Character block;block.Shape="cube";block.Name="Workshop block";block.Height=2;scene.Characters={block};history.clear();dirty=false;refresh();inspectorShown=false;toggle_menu(0);ShowWindow(inspector,SW_HIDE);command(InteractiveBuild);explorer->position={0,1.7,2.5};explorer->pitch=explorer->yaw=0;camera();SetFocus(view);
            auto pressF=[&]{SendMessage(view,WM_KEYDOWN,'F',0);};
            auto click=[&](int id){auto button=GetDlgItem(menuPanel,id);if(!button)throw std::runtime_error("Object menu button missing");SendMessage(button,BM_CLICK,0,0);};
            step("F opens nearby block options",[&]{pressF();},[&]{return openMenu==ObjectBehaviorMenu && behaviorTarget==block.Id;});
            const auto original=scene.encode();
            step("Carry picks up the block without changing its saved position",[&]{click(BehaviorCarry);},[&]{return carriedId==block.Id && scene.encode()==original;});
            step("Walking carries the block preview",[&]{explorer->position.x=2;update_carry();},[&]{return carryPoint.x==2 && scene.encode()==original;});
            step("F places block with original identity",[&]{pressF();},[&]{return carriedId.empty() && scene.Characters[0].Id==block.Id && scene.Characters[0].X==2;});
            step("Undo restores the original block",[&]{command(Undo);explorer->position={0,1.7,2.5};explorer->pitch=explorer->yaw=0;camera();},[&]{return scene.encode()==original;});
            step("Choose Breakable from F menu",[&]{pressF();click(BehaviorBreak);},[&]{return scene.Characters[0].GameplayRole=="breakable";});
            step("Attach quest to this block",[&]{click(BehaviorQuest);},[&]{return scene.Quests.size()==1 && scene.Rules.back().Subject==block.Id;});
            step("Play uses F to break the actual block",[&]{playback_command(Play);adventure->position={0,1.7,2};adventure->pitch=adventure->yaw=0;pressF();questTracker.update(scene,adventure->rules(),true);},[&]{return adventure->scene().Characters.empty() && questTracker.views()[0].complete;});
            step("Stop restores authored block",[&]{playback_command(Stop);},[&]{return scene.Characters.size()==1;});
            step("Creation lesson asks for a new cube",[&]{command(LearnOpen);},[&]{return creationGuide.active && creationGuide.target().empty();});
            step("Placing cube completes only placement objective",[&]{explorer->position.x=4;SetWindowTextW(shape,L"cube");command(Add);},[&]{return !creationGuide.target().empty() && questTracker.views()[0].objectives[0].complete && !questTracker.views()[0].objectives[1].complete;});
            step("F on the lesson cube opens its options",[&]{const auto& c=scene.Characters.back();explorer->position={c.X,1.7,c.Z+2};auto b=object_bounds(c);explorer->pitch=std::atan2((b.low.y+b.high.y)*.5-1.7,2.);explorer->yaw=0;camera();pressF();},[&]{return behaviorTarget==creationGuide.target() && questTracker.views()[0].objectives[1].complete;});
            step("Open property completes the correct cube lesson",[&]{click(BehaviorOpen);},[&]{return questTracker.views()[0].complete;});
            step("Undo property change restores unfinished lesson",[&]{command(Undo);},[&]{return !questTracker.views()[0].complete;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_QUESTS_ONLY",nullptr,0)) {
            scene=quest_template();history.clear();dirty=false;refresh();toggle_menu(0);ShowWindow(inspector,SW_HIDE);
            save_scene(scene,output.parent_path()/"Quest-adventure.starscene");const auto authored=scene.encode();
            auto clickAction=[&](const std::string& id){UpdateWindow(questTracker.handle());auto r=questTracker.action_bounds(id);if(!r)throw std::runtime_error("Tracker action missing: "+id);SendMessage(questTracker.handle(),WM_LBUTTONUP,0,MAKELPARAM((r->left+r->right)/2,(r->top+r->bottom)/2));UpdateWindow(questTracker.handle());};
            auto clickQuest=[&](bool pin=false){auto r=questTracker.action_bounds(scene.Quests[0].Id,pin);if(!r)throw std::runtime_error("Quest click target missing");SendMessage(questTracker.handle(),WM_LBUTTONUP,0,MAKELPARAM((r->left+r->right)/2,(r->top+r->bottom)/2));};
            auto trackerFits=[&]{RECT r{},host{};GetWindowRect(questTracker.handle(),&r);GetClientRect(window,&host);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&r),2);return r.left>=0 && r.top>=0 && r.right<=host.right && r.bottom<=host.bottom;};
            step("Side tracker shows authored quest and0/3",[&]{playback_command(Play);},[&]{return IsWindowVisible(questTracker.handle()) && questTracker.views()[0].objectives[0].current==0;});
            step("Cogwheel opens settings",[&]{clickAction("__settings");},[&]{return questTracker.settings_open() && questTracker.action_bounds("__lock").has_value();});
            step("Tracker background becomes transparent while text remains",[&]{for(int i=0;i<7;++i)clickAction("__opacity_less");},[&]{return questTracker.opacity()==0 && trackerFits();});
            step("Tracker background can become opaque",[&]{for(int i=0;i<10;++i)clickAction("__opacity_more");},[&]{return questTracker.opacity()==100 && trackerFits();});
            step("Tracker returns to70 percent background",[&]{for(int i=0;i<3;++i)clickAction("__opacity_less");},[&]{return questTracker.opacity()==70 && trackerFits();});
            step("Clicking quest opens details",[&]{clickQuest();},[&]{return questTracker.details_open();});
            step("Unpin changes only tracker visibility",[&]{clickQuest(true);},[&]{return !questTracker.tracked(scene.Quests[0].Id) && scene.encode()==authored;});
            step("Pin restores tracked quest",[&]{clickQuest(true);},[&]{return questTracker.tracked(scene.Quests[0].Id);});
            step("Back returns to compact tracker",[&]{SendMessage(questTracker.handle(),WM_LBUTTONUP,0,MAKELPARAM(60,18));},[&]{return !questTracker.details_open();});
            step("Compact tracker hides journal heading and settings",[&]{},[&]{return !questTracker.details_open() && !questTracker.settings_open() && !questTracker.action_bounds("__opacity_less");});
            step("Unlock movement from cogwheel",[&]{clickAction("__settings");clickAction("__lock");},[&]{return !questTracker.locked();});
            RECT beforeMove{};GetWindowRect(questTracker.handle(),&beforeMove);
            step("Unlocked tracker drags to a new position",[&]{SendMessage(questTracker.handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(80,18));SendMessage(questTracker.handle(),WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-120,98));SendMessage(questTracker.handle(),WM_LBUTTONUP,0,MAKELPARAM(80,18));},[&]{RECT r{};GetWindowRect(questTracker.handle(),&r);return r.left<beforeMove.left && r.top>beforeMove.top && trackerFits();});
            step("Lock preserves new position",[&]{clickAction("__lock");},[&]{return questTracker.locked();});
            RECT lockedPlace{};GetWindowRect(questTracker.handle(),&lockedPlace);
            step("Locked tracker cannot drag",[&]{SendMessage(questTracker.handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(80,18));SendMessage(questTracker.handle(),WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(180,118));SendMessage(questTracker.handle(),WM_LBUTTONUP,0,MAKELPARAM(80,18));},[&]{RECT r{};GetWindowRect(questTracker.handle(),&r);return EqualRect(&r,&lockedPlace)!=FALSE;});
            step("Collapse preserves quest state",[&]{clickAction("__fold");},[&]{return questTracker.collapsed() && questTracker.views()[0].objectives[0].current==0;});
            step("Expand restores objective display",[&]{clickAction("__fold");},[&]{return !questTracker.collapsed();});
            step("Close cogwheel settings",[&]{clickAction("__settings");},[&]{return !questTracker.settings_open() && !questTracker.action_bounds("__opacity_less");});
            for(int percent:{25,200,100})step(("Quest panel fits at "+std::to_string(percent)+" percent").c_str(),[&]{
                interfacePercent=manualInterfacePercent=percent;apply_interface_scale(requested_interface_scale(GetDpiForWindow(window)));layout();
            },[&]{return trackerFits() && questTracker.views()[0].objectives[0].current==0 && scene.encode()==authored;});
            auto collect=[&](double x){adventure->position={x,1.7,5};adventure->yaw=adventure->pitch=0;adventure->interact();renderer->scene(adventure->scene());questTracker.update(scene,adventure->rules(),true);};
            step("Collecting first cell shows1/3",[&]{collect(-1.5);},[&]{return questTracker.views()[0].objectives[0].current==1;});
            step("Full set shows3/3 and unlocks room stage",[&]{collect(0);collect(1.5);},[&]{return questTracker.views()[0].objectives[0].complete && !questTracker.views()[0].objectives[1].locked;});
            step("Entering room advances objective",[&]{adventure->position={0,1.7,-2};adventure->update_rules();questTracker.update(scene,adventure->rules(),true);},[&]{return questTracker.views()[0].objectives[1].complete;});
            step("Return interaction completes quest",[&]{adventure->yaw=adventure->pitch=0;adventure->interact();questTracker.update(scene,adventure->rules(),true);},[&]{return questTracker.views()[0].complete;});
            step("Restart resets progress and preserves authored scene",[&]{playback_command(Stop);playback_command(Play);},[&]{return questTracker.views()[0].objectives[0].current==0 && scene.encode()==authored;});
            playback_command(Stop);
            SceneData grouped;grouped.Game.emplace();QuestDefinition multi;multi.Title="Build your first objects";multi.Description="Five independent objectives, followed by a final test.";multi.StepSizes={5,1};
            for(int i=0;i<6;++i){GameplayRule rule;rule.Name="Try object "+std::to_string(i+1);rule.Event="interact";rule.Subject="block-"+std::to_string(i);rule.Actions={{"complete-objective",rule.Subject}};multi.RuleIds.push_back(rule.Id);grouped.Rules.push_back(rule);}grouped.Quests={multi};
            GameplayRulesRuntime groupedRuntime;groupedRuntime.reset(grouped.Rules);
            auto updateGrouped=[&]{questTracker.update(grouped,groupedRuntime,true);};
            step("Grouped step shows five independent objectives",[&]{updateGrouped();},[&]{return questTracker.views()[0].stepCount==2 && questTracker.views()[0].currentStep==0;});
            for(int index:{4,1,3,0})step(("Out of order completion "+std::to_string(index+1)).c_str(),[&]{groupedRuntime.emit("interact","block-"+std::to_string(index),grouped);updateGrouped();},[&]{return questTracker.views()[0].objectives[index].complete && questTracker.views()[0].currentStep==0;});
            step("Fifth completion advances to next step",[&]{groupedRuntime.emit("interact","block-2",grouped);updateGrouped();},[&]{return questTracker.views()[0].currentStep==1 && !questTracker.views()[0].complete;});
            step("Journal preserves green crossed-out objectives",[&]{auto rect=questTracker.action_bounds(multi.Id);if(!rect)throw std::runtime_error("Grouped title missing");SendMessage(questTracker.handle(),WM_LBUTTONUP,0,MAKELPARAM((rect->left+rect->right)/2,(rect->top+rect->bottom)/2));},[&]{return questTracker.details_open() && questTracker.views()[0].objectives[0].complete;});
            step("Final step completes grouped quest",[&]{groupedRuntime.emit("interact","block-5",grouped);updateGrouped();},[&]{return questTracker.views()[0].complete;});
            report["pass"]=true;atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_ZOOM_ONLY",nullptr,0)) {
            const auto original=json(scene).dump();
            step("Interface size offers Auto-fit checkbox and manual slider",[&]{
                toggle_menu(0);toggle_menu(MenuFile);
            },[&]{
                auto picker=GetDlgItem(menuPanel,SettingsScale);
                return IsWindowVisible(picker) && text(picker)=="Auto-fit window" &&
                    SendMessage(picker,BM_GETCHECK,0,0)==BST_UNCHECKED &&
                    IsWindowEnabled(GetDlgItem(menuPanel,InterfaceSlider));
            });
            for(int percent:{25,50,75,100,150,200}) {
                step(("Settings size "+std::to_string(percent)+" percent").c_str(),[&]{
                    toggle_menu(0);toggle_menu(MenuFile);
                    SendMessage(GetDlgItem(menuPanel,SettingsScale),BM_SETCHECK,BST_UNCHECKED,0);
                    command(SettingsScale);
                    auto slider=GetDlgItem(menuPanel,InterfaceSlider);SendMessage(slider,TBM_SETPOS,TRUE,percent);
                    SendMessage(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(slider));toggle_menu(0);
                    SetWindowPos(window,nullptr,0,0,850,650,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
                    show_inspector(5);scroll_inspector(INT_MAX);
                },[&]{return settingsScale==percent && json(scene).dump()==original;});
                step(("Help at "+std::to_string(percent)+" percent").c_str(),[&]{
                    show_help();scroll_panel(helpPanel,0,0);SetWindowTextW(helpSearch,L"save");
                },[&]{return IsWindowVisible(helpSearchLabel) && text(helpSearchLabel)=="Search" && json(scene).dump()==original;});
                command(HelpClose);
            }
            step("Help bottom controls at 200 percent",[&]{show_help();scroll_panel(helpPanel,0,INT_MAX);},[&]{
                RECT action{},host{};GetWindowRect(GetDlgItem(helpPanel,HelpRestart),&action);GetWindowRect(helpPanel,&host);
                return action.top>=host.top && action.bottom<=host.bottom;
            });
            command(HelpClose);
            step("Manual slider selects 63 percent",[&]{
                toggle_menu(0);toggle_menu(MenuFile);
                auto slider=GetDlgItem(menuPanel,InterfaceSlider);SendMessage(slider,TBM_SETPOS,TRUE,63);
                SendMessage(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(slider));scroll_main(0);
            },[&]{return settingsScale==63 && text(GetDlgItem(menuPanel,InterfaceReadout))=="63%";});
            for(int percent:{25,200}) {
                step(("Manual slider and label visible at "+std::to_string(percent)+" percent").c_str(),[&]{
                    auto slider=GetDlgItem(menuPanel,InterfaceSlider);
                    SendMessage(slider,TBM_SETPOS,TRUE,percent);
                    SendMessage(window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(slider));scroll_main(0);
                },[&]{
                    auto mode=GetDlgItem(menuPanel,SettingsScale),slider=GetDlgItem(menuPanel,InterfaceSlider);
                    RECT label{},host{};GetWindowRect(mode,&label);GetWindowRect(menuPanel,&host);
                    return settingsScale==percent && text(mode)=="Auto-fit window" && IsWindowVisible(mode) &&
                        label.top>=host.top && label.bottom<=host.bottom &&
                        SendMessage(slider,TBM_GETRANGEMIN,0,0)==25 && SendMessage(slider,TBM_GETRANGEMAX,0,0)==200 &&
                        SendMessage(slider,TBM_GETLINESIZE,0,0)==1 &&
                        text(GetDlgItem(menuPanel,InterfaceReadout))==std::to_string(percent)+"%" && json(scene).dump()==original;
                });
            }
            step("Auto fits a small window",[&]{
                SendMessage(GetDlgItem(menuPanel,SettingsScale),BM_SETCHECK,BST_CHECKED,0);command(SettingsScale);
                SetWindowPos(window,nullptr,0,0,720,540,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            },[&]{return interfacePercent==0 && settingsScale<100 && json(scene).dump()==original;});
            step("Auto restores size in a larger window",[&]{
                SetWindowPos(window,nullptr,0,0,1380,900,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            },[&]{return interfacePercent==0 && settingsScale==100 && json(scene).dump()==original;});
            report["pass"]=true;report["scope"]="Application-wide interface zoom and Help; actual rendered controls, Windows display scale unchanged";
            atomic_write(output,report.dump(2));dirty=false;SendMessageW(window,WM_CLOSE,0,0);return 0;
        }
        if(GetEnvironmentVariableW(L"STARLIGHT_SHOWCASE_ONLY",nullptr,0)) {
            scene=adventure_template();scene.Name="Starlight systems you can try";history.clear();dirty=false;
            const auto item=std::find_if(scene.Characters.begin(),scene.Characters.end(),[](const auto& c){return c.GameplayRole=="key";})->GameplayKey;
            GameplayRule collect;collect.Name="Find the gold token";collect.Event="item-collected";collect.Subject=item;
            collect.Conditions={{"has-key",item}};collect.Actions={{"complete-objective","Find the gold token"}};scene.Rules={collect};
            save_scene(scene,output.parent_path()/"Try-the-systems.starscene");refresh();
            step("Real saved gameplay-rule controls",[&]{
                show_inspector(6);SetWindowTextW(inputs.at("ruleName"),L"Find the gold token");
                SetWindowTextW(inputs.at("ruleKeys"),wide(item).c_str());
                SetWindowTextW(inputs.at("ruleObjective"),L"Find the gold token");
                RECT r{},host{};GetWindowRect(inputs.at("ruleName"),&r);GetWindowRect(inspector,&host);scroll_inspector(std::max(0L,r.top-host.top-115));
            },[&]{return scene.Rules.size()==1 && IsWindowVisible(inputs.at("ruleName"));});
            step("Rule area and action controls",[&]{scroll_inspector(INT_MAX);},[&]{return IsWindowVisible(fields.back());});
            step("Walk-and-build preview, existing implementation",[&]{command(InteractiveBuild);},[&]{return explorer.has_value() && !playing;});
            step("Play uses the same authored scene",[&]{command(Play);},[&]{return adventure.has_value() && playing && adventure->scene().Rules.size()==1;});
            report["pass"]=true;report["scope"]="Actual implemented rule controls, walk-and-build preview and Play; full Interactive mode is pending Tasks 5 and 6";
            atomic_write(output,report.dump(2));dirty=false;SendMessageW(window,WM_CLOSE,0,0);return 0;
        }
        std::string helpAuthored;
        const bool layoutOnly=GetEnvironmentVariableW(L"STARLIGHT_UI_LAYOUT_ONLY",nullptr,0)!=0;
        if(!layoutOnly) {
        for (auto [id,name] : {std::pair{MenuFile,"File menu"}, {MenuBuilder,"Builder menu"},
                              {MenuWorld,"Environment menu"}})
            step(name,[&]{toggle_menu(0);toggle_menu(id);},[&]{return openMenu==id && IsWindowVisible(menuPanel);});
        toggle_menu(0);
        step("Characters panel",[&]{command(MenuAssets);},[&]{return assetsShown && IsWindowVisible(assetPanel);});
        command(MenuAssets);
        step("More catalogue",[&]{command(MenuMore);},[&]{return IsWindowVisible(morePanel) && SendMessage(featureList,LB_GETCOUNT,0,0)==LRESULT(catalogue.size());});
        command(CloseMore);
        step("Place shape",[&]{command(BuildShapes);command(2000);command(ObjectProperties);},[&]{return actor()!=nullptr && inputs.contains("size");});
        step("Advanced build and body controls",[&]{command(Advanced);command(BuildDetails);command(BodyDetails);},[&]{return inputs.contains("x")&&inputs.contains("density");});
        step("Terrain settings",[&]{command(BuildTerrain);command(DockSettings);},[&]{return inputs.contains("paint");});
        step("Shapes and snapping",[&]{command(BuildShapes);command(DockSettings);},[&]{return inputs.contains("gridSnap");});
        step("Nature tools",[&]{command(BuildNature);},[&]{return dockSection==BuildNature && IsWindowVisible(dock);});
        step("Sculpt tools",[&]{command(BuildSculpt);},[&]{return dockSection==BuildSculpt && IsWindowVisible(dock);});
        step("Camera controls",[&]{command(MenuCamera);},[&]{return inputs.contains("pan") && inputs.contains("tilt");});
        step("Music panel",[&]{command(ShowMusic);},[&]{return IsWindowVisible(musicPanel);});
        command(CloseMusic);
        step("Project selection",[&]{open_project();},[&]{return projectWindow && IsWindowVisible(projectWindow);});
        close_project();
        auto telemetryVisible = [&] {
            wchar_t text[512]{}; GetWindowTextW(status,text,512);
            RECT r{}; GetWindowRect(status,&r); POINT point{r.left+4,r.top+4};
            // WindowFromPoint deliberately skips STATIC text controls. Query
            // the parent's child ordering instead, retaining occlusion checks.
            ScreenToClient(window,&point);
            const auto covering=ChildWindowFromPointEx(window,point,CWP_SKIPINVISIBLE);
            const bool unobstructed = covering==status;
            wchar_t coveringClass[128]{};if(covering)GetClassNameW(covering,coveringClass,128);
            RECT coveringRect{};if(covering)GetWindowRect(covering,&coveringRect);
            report["telemetryHitTest"][fullScreen ? "fullscreen" : "windowed"] = {
                {"coveringClass",utf8(coveringClass)},{"coveringIsParent",covering==window},
                {"coveringId",covering?GetDlgCtrlID(covering):0},
                {"coveringExStyle",covering?uint64_t(GetWindowLongPtrW(covering,GWL_EXSTYLE)):0},
                {"sampleClient",{point.x,point.y}},
                {"coveringRect",{coveringRect.left,coveringRect.top,coveringRect.right,coveringRect.bottom}},
                {"statusRect",{r.left,r.top,r.right,r.bottom}},
                {"statusParentMatches",GetParent(status)==window}};
            report["telemetry"][fullScreen ? "fullscreen" : "windowed"] = {
                {"visible",IsWindowVisible(status)!=FALSE},{"unobstructed",unobstructed},
                {"text",utf8(text)},{"width",r.right-r.left},{"height",r.bottom-r.top}};
            return IsWindowVisible(status) && unobstructed && r.right>r.left && r.bottom>r.top &&
                   std::wstring(text).find(L"FPS")!=std::wstring::npos;
        };
        const auto assetFixture=output.parent_path()/L"asset-fixture";
        fs::create_directories(assetFixture);
        save_object_template(scene.Characters.at(0),assetFixture/L"Audit block.starobject");
        libraryFolder=assetFixture; // Never scan or alter the user's remembered folder.
        libraryIndex=scan_asset_library(assetFixture);
        step("Asset browser windowed",[&]{command(BrowseAssets);filter_asset_browser();},
             [&]{return IsWindowVisible(libraryPanel) && libraryMatches.size()==1;});
        step("Asset browser type filter",[&]{
            SendMessage(GetDlgItem(libraryPanel,AssetType),CB_SETCURSEL,1,0);filter_asset_browser();
        },[&]{return libraryMatches.empty() && !IsWindowEnabled(GetDlgItem(libraryPanel,InsertAsset));});
        step("Asset browser saved objects",[&]{
            SendMessage(GetDlgItem(libraryPanel,AssetType),CB_SETCURSEL,2,0);filter_asset_browser();
        },[&]{return libraryMatches.size()==1 && IsWindowEnabled(GetDlgItem(libraryPanel,InsertAsset));});
        const auto originalCount=scene.Characters.size();
        step("Asset browser insertion",[&]{command(InsertAsset);},[&]{return scene.Characters.size()==originalCount+1;});
        command(CloseAssets);
        step("Copy and paste selection",[&]{command(CopyObjects);command(PasteObjects);},
             [&]{return !copiedObjects.empty() && scene.Characters.size()==originalCount+2;});
        step("Paste undo",[&]{command(Undo);},[&]{return scene.Characters.size()==originalCount+1;});
        step("Fullscreen live telemetry",[&]{command(FullScreen);},[&]{return fullScreen && telemetryVisible();});
        step("Asset browser fullscreen",[&]{command(BrowseAssets);},[&]{return fullScreen && IsWindowVisible(libraryPanel);});
        command(CloseAssets);
        step("Windowed live telemetry restored",[&]{command(FullScreen);},[&]{return !fullScreen && telemetryVisible();});
        step("Ray-traced scene before resize",[&]{scene.RaytracingEnabled=true;scene.RaytracingQuality=3;scene.RayReconstructionEnabled=false;renderer->scene(scene);},[&]{return renderer->status().tier>0;});
        for(int cycle=0;cycle<3;cycle++){
            step("Ray-traced fullscreen resize",[&]{command(FullScreen);},[&]{return fullScreen && telemetryVisible();});
            step("Ray-traced windowed resize",[&]{command(FullScreen);},[&]{return !fullScreen && telemetryVisible();});
        }
        step("Adventure preset preserves scene objects",[&]{command(AdventurePreset);},[&]{return scene.Game && scene.Characters.size()==originalCount+1;});
        step("Adventure advanced settings",[&]{
            if(!advanced)command(Advanced);
            SetWindowTextW(inputs.at("playerRadius"),L"0.45");command(Apply);
        },[&]{return scene.Game->PlayerRadius==.45 && inputs.contains("spawnX");});
        step("Adventure beginner preserves advanced tuning",[&]{
            if(advanced)command(Advanced);
            SetWindowTextW(inputs.at("moveSpeed"),L"5.5");command(Apply);
        },[&]{return scene.Game->MoveSpeed==5.5 && scene.Game->PlayerRadius==.45 && !inputs.contains("spawnX");});
        const auto authoredAdventure=scene.encode();
        step("Adventure play starts in editor",[&]{command(Play);},[&]{return playing && adventure.has_value();});
        step("Adventure pause preserves authoring",[&]{command(Pause);},[&]{return !playing && adventure && scene.encode()==authoredAdventure;});
        step("Adventure resume",[&]{command(Play);},[&]{return playing && adventure.has_value();});
        step("Adventure reset clears play state",[&]{command(Stop);},[&]{return !playing && !adventure && scene.encode()==authoredAdventure;});
        interactionFixture();
        step("New starter adventure",[&]{dirty=false;command(NewAdventure);},[&]{return dirty && scene.Game && scene.Characters.size()==9;});
        step("Advanced snapping increments",[&]{
            command(BuildShapes);command(DockSettings);if(!advanced)command(Advanced);
            SetWindowTextW(inputs.at("gridStep"),L"0.25");SetWindowTextW(inputs.at("rotationStep"),L"30");command(Apply);
        },[&]{return gridStep==.25 && rotationStep==30;});
        step("Beginner preserves snapping increments",[&]{command(Advanced);command(Apply);},
             [&]{return !inputs.contains("gridStep") && gridStep==.25 && rotationStep==30;});
        step("Walk and build enters shared scene",[&]{command(InteractiveBuild);},
             [&]{return explorer && !playing && scene.Characters.size()==9 && IsWindowVisible(adventureHudPanel);});
        auto buildPosition=explorer->position;
        step("Build placement from explorer",[&]{command(2000);},
             [&]{return scene.Characters.size()==10 && actor() && std::abs(actor()->Z-(buildPosition.z-3))<.01;});
        step("Build placement undo",[&]{command(Undo);},
             [&]{return scene.Characters.size()==9 && explorer && explorer->position.z==buildPosition.z;});
        const auto buildAuthored=scene.encode();
        step("Build mode gameplay test",[&]{command(Play);},
             [&]{return playing && adventure && explorer && adventure->position.z==explorer->position.z;});
        step("Gameplay returns to authored building",[&]{command(Pause);},
             [&]{return !playing && !adventure && explorer && scene.encode()==buildAuthored;});
        step("Return to ordinary editor view",[&]{command(InteractiveBuild);},
             [&]{return !explorer && !playing && !IsWindowVisible(adventureHudPanel) && scene.encode()==buildAuthored;});
        step("Gameplay setup report",[&]{command(CheckGameplay);},
             [&]{return gameplayReport.find("Basic setup checks passed")!=std::string::npos;});
        helpProgressOverride=output.parent_path()/L"tutorial-progress.txt";
        helpProgressLoaded=true;helpLesson=0;helpSteps.assign(lessons().size(),0);
        helpAuthored=scene.encode();
        step("Help opens without editing scene",[&]{command(HelpOpen);},
             [&]{return helpShown && IsWindowVisible(helpButton) && IsWindowVisible(helpPanel) && scene.encode()==helpAuthored;});
        step("Help direct lesson selection",[&]{SendMessageW(helpPicker,CB_SETCURSEL,4,0);command(HelpLesson);command(HelpNext);},
             [&]{return helpLesson==4 && helpSteps[4]==1 && scene.encode()==helpAuthored;});
        step("Help resumes saved section",[&]{command(HelpClose);helpProgressLoaded=false;helpLesson=0;helpSteps.assign(lessons().size(),0);command(HelpOpen);},
             [&]{return helpLesson==4 && helpSteps[4]==1;});
        step("Help restart is optional",[&]{command(HelpRestart);},[&]{return helpSteps[4]==0 && scene.encode()==helpAuthored;});
        step("Help search finds relevant lessons",[&]{SetWindowTextW(helpSearch,L"door");},
             [&]{return !helpMatches.empty() && helpMatches.size()<lessons().size() && helpLesson==4;});
        step("Help search empty results preserve reading position",[&]{SetWindowTextW(helpSearch,L"no-matching-lesson-xyz");},
             [&]{return helpMatches.empty() && !IsWindowEnabled(helpPicker) && helpLesson==4 && scene.encode()==helpAuthored;});
        step("Help search clears to full catalog",[&]{SetWindowTextW(helpSearch,L"");},
             [&]{return helpMatches.size()==lessons().size() && IsWindowEnabled(helpPicker);});
        step("Help survives advanced disclosure",[&]{command(Advanced);},
             [&]{return helpShown && IsWindowVisible(helpButton) && scene.encode()==helpAuthored;});
        step("Help survives walk and build",[&]{command(InteractiveBuild);command(HelpOpen);},
             [&]{return explorer && helpShown && scene.encode()==helpAuthored;});
        command(HelpClose);command(InteractiveBuild);command(HelpOpen);
        step("Help available fullscreen",[&]{command(FullScreen);command(HelpOpen);},
             [&]{return fullScreen && IsWindowVisible(helpButton) && IsWindowVisible(helpPanel);});
        step("Help closes without altering scene",[&]{escape();command(FullScreen);},
             [&]{return !helpShown && !fullScreen && scene.encode()==helpAuthored;});
        } else {
            report["scope"]="focused small-window layout verification";
            dirty=false;command(NewAdventure);
            helpAuthored=scene.encode();
            helpProgressOverride=output.parent_path()/L"tutorial-progress.txt";
            helpProgressLoaded=true;helpLesson=0;helpSteps.assign(lessons().size(),0);
        }
        auto fullyInside = [](HWND child, HWND host) {
            RECT r{}, b{}; GetWindowRect(child,&r); MapWindowPoints(nullptr,host,reinterpret_cast<POINT*>(&r),2);
            GetClientRect(host,&b); return r.left>=0 && r.top>=0 && r.right<=b.right && r.bottom<=b.bottom;
        };
        RECT originalWindow{}; GetWindowRect(window,&originalWindow);
        step("Small-window Shape settings bottom controls",[&]{
            SetWindowPos(window,nullptr,0,0,720,540,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
            command(BuildShapes); command(DockSettings); scroll_inspector(INT_MAX);
        },[&]{return fullyInside(fields.back(),inspector) && fullyInside(propertiesPanel,window);});
        step("Small-window Shape settings returns to top",[&]{scroll_inspector(0);},
             [&]{return fullyInside(fields.front(),inspector) && scroll==0;});
        step("Small-window character list is reachable",[&]{assetsShown=true;layout();scroll_assets(INT_MAX);},
             [&]{
                 RECT assets{};GetWindowRect(assetPanel,&assets); bool separate=true;
                 for(auto panel:{propertiesPanel,dock,playbackPanel}) {RECT r{},overlap{};GetWindowRect(panel,&r);separate &= !IntersectRect(&overlap,&assets,&r);}
                 return fullyInside(list,assetPanel) && separate;
             });
        step("Persistent Help Search label with typed query",[&]{
            assetsShown=false;layout();command(HelpOpen);SetWindowTextW(helpSearch,L"door");
        },[&]{return text(helpSearchLabel)=="Search" && IsWindowVisible(helpSearchLabel) && fullyInside(helpSearchLabel,helpPanel) && fullyInside(helpSearch,helpPanel);});
        step("Resize preserves scene and restores complete controls",[&]{
            command(HelpClose);scroll_assets(0);
            SetWindowPos(window,nullptr,originalWindow.left,originalWindow.top,originalWindow.right-originalWindow.left,originalWindow.bottom-originalWindow.top,SWP_NOZORDER|SWP_NOACTIVATE);
            command(BuildShapes);command(DockSettings);scroll_inspector(INT_MAX);
        },[&]{return scene.encode()==helpAuthored && fullyInside(fields.back(),inspector);});
        report["layoutDpiObserved"]=GetDpiForWindow(window);
        report["pass"]=true;
    } catch(const std::exception& e) { report["pass"]=false; report["error"]=e.what(); }
    atomic_write(output,report.dump(2));
    dirty=false;
    SendMessageW(window,WM_CLOSE,0,0);
    return report["pass"].get<bool>() ? 0 : 1;
}
