// Visible, paced checks in a new editor process. This complements the fast
// regression suite; it never opens a user's saved scene or invokes file dialogs.
int visible_ui_checks(const fs::path& output) {
    fs::create_directories(output.parent_path());
    nlohmann::json report{{"checks",nlohmann::json::array()}, {"scope","visible editor menu walkthrough"},
                          {"isolatedScene",true}, {"allEditorFeaturesCertified",false}};
    auto pump = [&](bool waiting) {
        MSG msg{};
        while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
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
        action();
        const auto initialStatus=renderer->status();
        const auto initialFrames=initialStatus.frames;
        SetWindowTextW(window,wide(std::string("Starlight visible audit — ")+name+" | Esc cancels").c_str());
        auto end=GetTickCount64()+900;
        bool stayedForeground=true;
        ULONGLONG newFramesMs=0;
        while (GetTickCount64()<end) { pump(false); stayedForeground &= foreground();
            if(!newFramesMs && renderer->status().frames>=initialFrames+3) newFramesMs=GetTickCount64()-started;
            Sleep(10); }
        // Require new rendered frames after a transition,
        // rather than capturing a blank intermediate surface and calling it done.
        const auto renderDeadline=GetTickCount64()+10000;
        while(renderer->status().frames<initialFrames+3 && renderer->status().error.empty() &&
              GetTickCount64()<renderDeadline) { pump(false); stayedForeground &= foreground(); Sleep(10); }
        if(!newFramesMs && renderer->status().frames>=initialFrames+3) newFramesMs=GetTickCount64()-started;
        auto screenshot=output.parent_path()/("ui-"+std::to_string(report["checks"].size())+".bmp");
        const bool behavior=valid(), captured=stayedForeground && capture(screenshot);
        const bool rendered=renderer->status().frames>=initialFrames+3;
        const bool passed=behavior && stayedForeground && captured && rendered && renderer->status().error.empty();
        report["checks"].push_back({{"name",name},{"pass",passed},{"behavior",behavior},
          {"foreground",stayedForeground},{"screenshot",utf8(screenshot.wstring())},{"captureSucceeded",captured},
          {"rendererFrames",renderer->status().frames},
          {"sceneUpdates",renderer->status().scene_updates-initialStatus.scene_updates},{"sceneCpuMs",renderer->status().scene_cpu_total_ms-initialStatus.scene_cpu_total_ms},{"frameCpuMs",renderer->status().frame_cpu_total_ms-initialStatus.frame_cpu_total_ms},{"gpuMs",renderer->status().gpu_ms},{"frameCount",renderer->status().frames-initialFrames},{"renderedNewFrames",rendered},
          {"newFramesMs",newFramesMs},{"elapsedMs",GetTickCount64()-started},{"rendererError",renderer->status().error}});
        atomic_write(output.string()+".progress.json",report.dump(2));
        if (!passed) throw std::runtime_error(std::string("Visible editor step failed: ")+name);
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
            Sleep(10);
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
        if(GetEnvironmentVariableW(L"STARLIGHT_UI_BATTLE_ONLY",nullptr,0)){
            MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);
            SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,monitor.rcMonitor.right-monitor.rcMonitor.left,monitor.rcMonitor.bottom-monitor.rcMonitor.top,SWP_FRAMECHANGED|SWP_SHOWWINDOW);
            scene=SceneData{};scene.Name="Mock battle - live movement and cones";scene.Game.emplace();scene.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.X=1;enemy.Z=-2.5;enemy.CombatHealth=1000;enemy.CombatBoss=true;enemy.CombatPursuit=true;enemy.CombatAutoAttack=true;scene.Characters={enemy};
            history.clear();dirty=false;refresh();toggle_menu(0);playback_command(Play);SetFocus(view);
            adventure->cameraPitch=-.65;adventure->fire_button(true);mockBattle=true;
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
            mockBattle=false;
            report["simultaneousTelegraphs"]=both;
            report["playerDamaged"]=adventure->health()<100;
            report["enemyDamaged"]=adventure->combat_actors().at(enemy.Id).health<startingEnemy;
            report["pass"]=both;
            atomic_write(output,report.dump(2));dirty=false;SendMessage(window,WM_CLOSE,0,0);return 0;
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
            const bool unobstructed = ChildWindowFromPointEx(window,point,CWP_SKIPINVISIBLE)==status;
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
        step("Ray-traced scene before resize",[&]{scene.RaytracingEnabled=true;scene.RaytracingQuality=3;scene.RayReconstructionEnabled=false;renderer->scene(scene);},[&]{return true;});
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
        step("Interaction fixture starts",[&]{
            scene=SceneData{};scene.Game.emplace();scene.Game->SpawnY=.5;scene.Game->SpawnZ=2;
            Character key;key.Shape="cube";key.GameplayRole="key";key.GameplayKey="audit-key";
            Character door=key;door.Id=new_id();door.X=3;door.GameplayRole="door";
            Character exit=key;exit.Id=new_id();exit.X=6;exit.GameplayRole="exit";
            scene.Characters={key,door,exit};selected=-1;refresh();command(Play);
        },[&]{return playing && adventure && IsWindowVisible(adventureHud);});
        step("Collect key through editor input",[&]{SendMessageW(view,WM_KEYDOWN,'E',0);},[&]{return adventure->scene().Characters.size()==2 && scene.Characters.size()==3;});
        step("Open matching door through editor input",[&]{adventure->position.x=3;SendMessageW(view,WM_KEYDOWN,'E',0);},[&]{return adventure->scene().Characters.size()==1 && adventureHint=="Door opened.";});
        step("Complete exit through editor input",[&]{adventure->position.x=6;SendMessageW(view,WM_KEYDOWN,'E',0);},[&]{return adventure->finished && adventureHint=="Adventure complete!";});
        step("Reset restores interactive scene",[&]{command(Stop);},[&]{return !adventure && scene.Characters.size()==3 && !IsWindowVisible(adventureHud);});
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

