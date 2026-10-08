// Opt-in foreground acceptance in the actual exported executable. Uses scratch
// runtime scenes; never saves or modifies the package's authored content.
int gameplay_checks(const fs::path& output){
    json report{{"pass",false},{"scope","Packaged foreground movement, combat, control and lifecycle; not clean-Windows certification"},{"checks",json::array()}};
    fs::create_directories(output.parent_path());gameplayCheck=true;
    const auto original=authored.encode();
    auto pump=[&]{MSG msg{};for(int n=0;n<32 && PeekMessageW(&msg,nullptr,0,0,PM_REMOVE);++n){
        if(msg.message==WM_QUIT || msg.message==WM_CLOSE || (msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE))throw std::runtime_error("Gameplay acceptance cancelled");
        TranslateMessage(&msg);DispatchMessageW(&msg);
    }};
    auto step=[&](const char* name,auto action,auto valid,int ms=650){
        if(GetForegroundWindow()!=window)throw std::runtime_error("Foreground lost; stopped without retry");
        action();auto frames=renderer->status().frames;auto deadline=GetTickCount64()+ms;
        while(GetTickCount64()<deadline){pump();if(GetForegroundWindow()!=window)throw std::runtime_error("Foreground lost; stopped without retry");MsgWaitForMultipleObjectsEx(0,nullptr,8,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
        const auto renderDeadline=GetTickCount64()+10000;
        while(renderer->status().frames<frames+3 && renderer->status().error.empty() && GetTickCount64()<renderDeadline){
            pump();if(GetForegroundWindow()!=window)throw std::runtime_error("Foreground lost; stopped without retry");MsgWaitForMultipleObjectsEx(0,nullptr,8,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        }
        auto state=renderer->status();bool pass=valid() && state.frames>=frames+3 && state.error.empty();
        auto screenshot=output.parent_path()/("gameplay-"+std::to_string(report["checks"].size())+".bmp");
        const auto captures=state.captures;renderer->capture(screenshot);
        auto captureDeadline=GetTickCount64()+5000;
        while(renderer->status().captures==captures && GetTickCount64()<captureDeadline){pump();if(GetForegroundWindow()!=window)throw std::runtime_error("Foreground lost; stopped without retry");MsgWaitForMultipleObjectsEx(0,nullptr,8,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
        pass=pass && renderer->status().captures>captures && fs::exists(screenshot);
        report["checks"].push_back({{"name",name},{"pass",pass},{"foreground",true},{"frames",state.frames},{"rendererError",state.error}});
        report["checks"].back()["screenshot"]=utf8(screenshot.wstring());
        if(!pass){const auto& tether=gameplay.grapple_motion();report["failureState"]={{"simulationSeconds",gameplay.seconds()},{"position",{gameplay.position.x,gameplay.position.y,gameplay.position.z}},{"grappleState",int(tether.state)},{"grappleHeld",tether.held},{"hookFlightRemaining",tether.flightRemaining},{"anchor",{tether.anchor.x,tether.anchor.y,tether.anchor.z}},{"velocity",{tether.velocity.x,tether.velocity.y,tether.velocity.z}}};}
        atomic_write(output,report.dump(2));if(!pass)throw std::runtime_error(name);
    };
    auto load=[&](const SceneData& scene){
        checkRight=checkForward=0;gameplayAudio.stop();
        SceneData staged=scene;std::swap(authored,staged);
        struct Restore{SceneData& authored;SceneData& staged;~Restore(){std::swap(authored,staged);}} restore{authored,staged};
        paused=true;gameplay.cancel_mobility_input();loadingActive=true;
        struct Active{bool& flag;~Active(){flag=false;}} active{loadingActive};
        LoadingScreen loading;loading.show(window,view);loading.update(0,"Starting your scene");
        if(!prepare_and_start(loading))throw std::runtime_error("Scene preparation did not finish: "+lastPreparation.reason);
        loading.close();loadingActive=false;paused=false;release_deferred_close();tick=GetTickCount64();
    };
    SceneData empty;empty.Game.emplace();empty.Game->SpawnZ=0;empty.Terrain.emplace();
    auto key=[&](int code){SendMessageW(window,WM_KEYDOWN,code,0);SendMessageW(window,WM_KEYUP,code,0);};
    try{
        MONITORINFO monitor{sizeof(MONITORINFO)};GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST),&monitor);if(monitor.rcMonitor.right-monitor.rcMonitor.left!=2560||monitor.rcMonitor.bottom-monitor.rcMonitor.top!=1440)throw std::runtime_error("Exported gameplay acceptance requires a 2560x1440 monitor");SetWindowLongPtrW(window,GWL_STYLE,WS_POPUP|WS_VISIBLE);SetWindowPos(window,HWND_TOP,monitor.rcMonitor.left,monitor.rcMonitor.top,2560,1440,SWP_FRAMECHANGED|SWP_SHOWWINDOW);pacingShown=true;renderer->pacing_enabled(true);report["fullscreen1440p"]=true;report["overlayEnabled"]=true;
        if(GetEnvironmentVariableW(L"STARLIGHT_MIXED_PHYSICS_ONLY",nullptr,0)){
            load(authored);bool fragments=false,fire=false,splash=false,shot=false,rope=false,fall=false,lightning=false,grass=false;
            step("Saved mixed world starts with every required system",[&]{},[&]{return authored.Characters.size()>=22&&renderer->status().liquid_volume>0;},1200);
            step("Mixed combat, destruction, liquids, ropes and atmosphere",[&]{gameplay.fire_button(true);gameplay.fire_button(false);gameplayObserver=[&]{auto p=gameplay.presentation();auto state=renderer->status();fragments|=p.fragments.size()>=16;fire|=state.fire_particles>0;splash|=state.liquid_splashes>0;shot|=!p.projectiles.empty();lightning|=p.lightning>0;for(const auto& c:gameplay.scene().Characters){if(c.Id=="mixed-rope")rope|=c.JointRuntimePoints.size()==13;if(c.Id=="mixed-fall")fall|=c.Y<1;}for(const auto& o:renderer->pacing_snapshot().objects)if(o.id=="mixed-grass")grass|=o.effectUpdatesPerSecond>0;};},[&]{gameplayObserver={};report["observed"]={{"fragments",fragments},{"fire",fire},{"splashes",splash},{"projectiles",shot},{"rope",rope},{"fall",fall},{"lightning",lightning},{"grass",grass}};return fragments&&fire&&splash&&shot&&rope&&fall&&lightning&&grass;},6500);
            auto probes=renderer->status().fog_probes;step("Mixed-world GPU fog remains finite and excludes bodies",[&]{renderer->probe_fog();},[&]{auto a=renderer->status().fog_sample;report["fogField"]=a;return renderer->status().fog_probes>probes&&a[1]>0&&a[5]<.001&&std::all_of(a.begin(),a.end(),[](double v){return std::isfinite(v);});});
            double at=0;step("Mixed-world pause freezes simulation",[&]{key(VK_ESCAPE);at=gameplay.seconds();},[&]{return paused&&gameplay.seconds()==at;});
            step("Mixed-world restart restores broken object and authored settings",[&]{key(VK_F5);},[&]{return !paused&&gameplay.presentation().fragments.empty()&&gameplay.scene().Characters.size()==authored.Characters.size()&&authored.encode()==original;},100);
            load(empty);step("Mixed-world replacement clears runtime effects",[&]{},[&]{auto st=renderer->status();return gameplay.presentation().fragments.empty()&&gameplay.presentation().projectiles.empty()&&st.fire_particles==0&&st.liquid_volume==0&&st.liquid_splashes==0;});
            report["authoredUnchanged"]=authored.encode()==original;report["pass"]=true;atomic_write(output,report.dump(2));gameplayCheck=false;SendMessageW(window,WM_CLOSE,0,0);return 0;
        }
        step("Packaged scene renders with third-person weapons",[&]{},[&]{return gameplay.presentation().showBody && gameplay.presentation().showWeapons;},1600);
        load(empty);double startZ=gameplay.position.z;
        step("Continuous forward movement",[&]{checkForward=1;},[&]{checkForward=0;return gameplay.position.z<startZ-1;});
        double facing=gameplay.yaw;
        step("Orbit preserves character facing",[&]{gameplay.orbit_camera(100,0);},[&]{return gameplay.yaw==facing && gameplay.cameraYaw!=facing;});
        step("Right look changes facing with camera",[&]{gameplay.look(100,0);},[&]{return gameplay.yaw!=facing && gameplay.yaw==gameplay.cameraYaw;});
        step("Wheel reaches first person with pistols",[&]{SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,120*24),0);},[&]{return !gameplay.presentation().showBody && gameplay.presentation().showWeapons;});
        load(empty);double firstY=0;bool secondJump=false;
        step("First jump from Space",[&]{key(VK_SPACE);},[&]{firstY=gameplay.position.y;return firstY>1.9;},200);
        step("Second distinct Space jumps again",[&]{key(VK_SPACE);},[&]{secondJump=gameplay.position.y>firstY;return secondJump;},200);
        step("Landing restores grounded movement",[&]{},[&]{return gameplay.grounded();},1600);
        step("Sprint consumes its own stamina",[&]{checkForward=1;gameplay.sprint(true);},[&]{checkForward=0;gameplay.sprint(false);return gameplay.movement_resources().stamina<500 && gameplay.movement_resources().dodgeCharges==2;});
        bool protectedDodge=false;
        step("Dodge consumes one charge and blocks damage",[&]{gameplay.dodge(1,0);protectedDodge=gameplay.take_damage(10)==0;},[&]{return protectedDodge && gameplay.health()==100 && gameplay.movement_resources().dodgeCharges==1;});
        load(empty);step("Grounded start",[&]{},[&]{return gameplay.grounded();},250);
        bool protectedLunge=false;
        step("Lunge spends both charges atomically",[&]{gameplay.mobility_button(true,0,1,.23);protectedLunge=gameplay.take_damage(10)==0;},[&]{gameplay.mobility_button(false,0,1);return protectedLunge && gameplay.movement_resources().dodgeCharges==0;});
        step("Control selects crouch",[&]{key(VK_CONTROL);},[&]{return gameplay.posture()==1;});
        step("Prone changes physical height",[&]{gameplay.set_posture(0);gameplay.posture_button(true,0,0,.4);gameplay.posture_button(false,0,0);},[&]{return gameplay.posture()==2 && gameplay.presentation().bodyHeight==.5;});
        load(empty);Character wall;wall.Shape="cube";wall.Width=8;wall.Height=8;wall.Depth=1;wall.Z=-5;auto wallScene=empty;wallScene.Characters={wall};load(wallScene);
        step("Grapple hook and wall attachment",[&]{gameplay.cameraYaw=0;gameplay.cameraPitch=0;SendMessageW(window,WM_KEYDOWN,'G',0);},[&]{return gameplay.grapple_motion().state==GrappleMotion::State::Wall && gameplay.presentation().showTether;},1400);
        step("Releasing G retains wall attachment",[&]{SendMessageW(window,WM_KEYUP,'G',0);gameplay.cameraDistance=0;},[&]{return gameplay.grapple_motion().state==GrappleMotion::State::Wall && !gameplay.presentation().showBody;});
        step("Wall jump detaches",[&]{key(VK_SPACE);},[&]{return gameplay.grapple_motion().state==GrappleMotion::State::Free;});
        auto swingScene=empty;Character ceiling;ceiling.Shape="cube";ceiling.Width=8;ceiling.Depth=8;ceiling.Height=.3;ceiling.Y=6;swingScene.Characters={ceiling};load(swingScene);
        step("Overhead grapple enters swing",[&]{gameplay.cameraPitch=1.2;SendMessageW(window,WM_KEYDOWN,'G',0);},[&]{return gameplay.grapple_motion().state==GrappleMotion::State::Swing;},900);
        step("Swing steering moves beneath anchor",[&]{checkRight=1;},[&]{return gameplay.grapple_motion().state==GrappleMotion::State::Swing && gameplay.grapple_motion().velocity.length()>.1;});
        Vec3 releasedVelocity{};
        step("Swing release retains travel momentum",[&]{releasedVelocity=gameplay.grapple_motion().velocity;checkRight=0;SendMessageW(window,WM_KEYUP,'G',0);},[&]{return releasedVelocity.length()>.1 && gameplay.grapple_motion().state==GrappleMotion::State::Free;});
        load(empty);auto impactBefore=gameplayAudio.submitted.load();
        step("Launch visibly lifts the player",[&]{gameplay.apply_control(ControlEffect::Launch,{2,true},true);},[&]{return gameplay.position.y>2 && gameplay.presentation().impacts==0;},400);
        step("Launch landing emits one impact sound",[&]{},[&]{return gameplay.presentation().impacts==1 && gameplayAudio.submitted>impactBefore && gameplayAudio.failed==0;},1500);
        load(empty);bool blocked=false;
        step("Stun blocks attack and emits chirps",[&]{gameplay.apply_control(ControlEffect::Stun,{2,true},true);key('1');blocked=!gameplay.basic_attack().winding;},[&]{return blocked && gameplay.crowd_control().active==ControlEffect::Stun && gameplayAudio.submitted>0 && gameplayAudio.failed==0;});
        step("Stun expiry starts player immunity",[&]{},[&]{return !gameplay.crowd_control().active && gameplay.crowd_control().immunity>0;},1700);
        bool friendlyRejected=false;
        step("Friendly control stays rejected",[&]{load(empty);friendlyRejected=gameplay.apply_control(ControlEffect::Stun,{2,true},false)==CombatControl::Result::Rejected;},[&]{return friendlyRejected && !gameplay.crowd_control().active;});
        auto combat=empty;Character target;target.Id="acceptance-target";target.Shape="cube";target.Z=-4;target.Width=2;target.Height=2;target.Depth=1;target.GameplayRole="breakable";combat.Characters={target};load(combat);
        step("Target selection never fires",[&]{key(VK_TAB);},[&]{return gameplay.presentation().targetBounds.has_value() && !gameplay.basic_attack().winding;});
        auto audioBefore=gameplayAudio.submitted.load();
        step("Released attack resolves and shatters",[&]{key('1');},[&]{return gameplay.scene().Characters.empty() && gameplay.presentation().shots==1 && gameplayAudio.submitted>=audioBefore+2;},3000);
        step("Released attack does not repeat",[&]{},[&]{return gameplay.presentation().shots==1 && !gameplay.basic_attack().winding;});
        load(empty);
        step("Healing restores runtime health",[&]{gameplay.take_damage(40);gameplay.healing_cast("",true);gameplay.healing_cast("",false);},[&]{return gameplay.health()==80;},1800);
        auto bossScene=empty;Character boss;boss.Id="acceptance-boss";boss.Shape="cube";boss.Width=2;boss.Height=2;boss.Z=-4;boss.CombatHealth=1000;boss.CombatBoss=true;bossScene.Characters={boss};load(bossScene);
        step("Repeated solo control breaks boss bar",[&]{gameplay.select_target();for(int i=0;i<4;++i)gameplay.control_actor(boss.Id,ControlEffect::Stun,{1,true});},[&]{return gameplay.combat_actors().at(boss.Id).bar.phase==BossBreakBar::Phase::Staggered;});
        step("Boss vulnerability modifies resolved damage",[&]{key('1');},[&]{return gameplay.combat_actors().at(boss.Id).health==987.5;},2800);
        auto physicsScene=empty;Character loadBody;loadBody.Shape="cube";loadBody.Dynamic=true;loadBody.Size=.2;loadBody.Density=1000;loadBody.X=3;loadBody.Y=3;loadBody.Z=-4;loadBody.JointKind=3;loadBody.JointY=1;loadBody.JointTargetY=2.2;loadBody.JointLength=2;loadBody.JointSegments=12;physicsScene.Characters={loadBody};load(physicsScene);
        step("Packaged segmented rope supports its moving load",[&]{},[&]{auto c=gameplay.scene().Characters[0];return c.JointRuntimePoints.size()==13&&c.Y>2.85;},1800);
        auto ranged=empty;Character shooter;shooter.Shape="cube";shooter.Z=-6;shooter.RotationY=180;shooter.CombatHealth=100;shooter.CombatAutoAttack=true;shooter.CombatProjectileSpeed=5;ranged.Characters={shooter};load(ranged);bool travelingShot=false;
        step("Packaged enemy projectile travels before impact",[&]{gameplayObserver=[&]{travelingShot|=!gameplay.presentation().projectiles.empty()&&gameplay.health()==100;};},[&]{gameplayObserver={};return travelingShot;},3000);
        step("Packaged enemy impact applies player damage",[&]{},[&]{return gameplay.health()<100&&gameplay.combat_actors().at(shooter.Id).health==100;},1800);
        auto pool=empty;pool.Terrain->WaterEnabled=true;pool.Terrain->WaterLevel=2.5;load(pool);
        step("Packaged liquid buoyancy raises the player",[&]{},[&]{return gameplay.position.y>2.2;},6000);
        auto storm=empty;Character bolt;bolt.Shape="chaos-lightning";bolt.X=4;bolt.TargetX=0;bolt.TargetY=0;storm.Characters={bolt};load(storm);auto beforeThunder=gameplayAudio.submitted.load();
        step("Packaged lightning produces spatial thunder without audio errors",[&]{},[&]{return gameplay.presentation().lightning>0&&gameplayAudio.submitted>beforeThunder&&gameplayAudio.failed==0;},1000);
        auto dynamicCombat=empty;Character drivenEnemy;drivenEnemy.Shape="cube";drivenEnemy.Dynamic=true;drivenEnemy.Size=.5;drivenEnemy.X=3;drivenEnemy.Y=4;drivenEnemy.Z=-4;drivenEnemy.CombatHealth=100;drivenEnemy.CombatPursuit=true;dynamicCombat.Characters={drivenEnemy};load(dynamicCombat);
        step("Packaged pursuing dynamic enemy retains falling and contact physics",[&]{},[&]{auto c=gameplay.scene().Characters.front();return c.Y<.3&&std::hypot(c.X-3,c.Z+4)>.2&&c.PhysicsRotation.has_value();},2400);
        auto living=empty;living.Terrain->WaterEnabled=true;living.Terrain->WaterLevel=.5;Character breakable;breakable.Shape="cube";breakable.GameplayRole="breakable";breakable.Z=-4;breakable.Y=.5;breakable.Width=breakable.Height=1.5;Character flame;flame.Shape="fire";flame.X=3;flame.Y=1;flame.Z=-3;living.Characters={breakable,flame};load(living);
        bool sawFragments=false,sawSplash=false,sawFire=false,sawDebrisObstacles=false;
        step("Packaged fracture, fire and water run together",[&]{gameplay.cameraPitch=-.3;gameplay.cameraDistance=7;gameplay.fire_button(true);gameplay.fire_button(false);gameplayObserver=[&]{auto state=renderer->status();auto pieces=gameplay.presentation().fragments.size();sawFragments|=pieces>=16;sawSplash|=state.liquid_splashes>0;sawFire|=state.fire_particles>0;sawDebrisObstacles|=pieces>0&&state.fog_bodies>=pieces;};},[&]{gameplayObserver={};return sawFragments&&sawSplash&&sawFire&&sawDebrisObstacles;},4000);
        load(empty);
        step("Packaged scene replacement clears fragments, liquid and emitted particles",[&]{},[&]{auto state=renderer->status();return gameplay.presentation().fragments.empty()&&state.fire_particles==0&&state.liquid_splashes==0&&state.liquid_volume==0;},800);
        double pausedTime=0;
        step("Pause freezes gameplay",[&]{key(VK_ESCAPE);pausedTime=gameplay.seconds();},[&]{return paused && gameplay.seconds()==pausedTime;});
        step("Restart restores original package scene",[&]{key(VK_F5);},[&]{return !paused && gameplay.health()==100 && gameplay.movement_resources().dodgeCharges==2 && gameplay.presentation().shots==0 && authored.encode()==original;});
        report["audioSubmitted"]=gameplayAudio.submitted.load();report["audioErrors"]=gameplayAudio.failed.load();report["authoredUnchanged"]=authored.encode()==original;report["pass"]=true;
    }catch(const std::exception& e){report["error"]=e.what();}
    atomic_write(output,report.dump(2));bool pass=report["pass"].get<bool>();gameplayCheck=false;gameplayObserver={};SendMessageW(window,WM_CLOSE,0,0);return pass?0:1;
}
