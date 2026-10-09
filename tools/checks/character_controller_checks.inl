#ifdef STARLIGHT_CHARACTER_CONTROLLER
{
    json motion=json::object();
    auto advance=[](AdventurePlayer& p,int ticks,double x=0,double z=0){for(int i=0;i<ticks;i++)p.move(x,z,1./120);};
    SceneData level;level.Game.emplace();level.Game->SpawnZ=0;
    {
        auto reduced=level;reduced.Game->ReducedCameraMotion=true;
        auto decoded=SceneData::decode(reduced.encode());require(decoded.Game->ReducedCameraMotion,"Reduced camera setting survives save and load");
        AdventurePlayer p;p.start(decoded);advance(p,30);p.zoom(1);require(p.cameraDistance==4.5,"Camera zoom uses the specified half-metre wheel step");
        p.zoom(100);p.dodge(1,0);advance(p,15);
        require(p.camera_up().y>.999 && p.dodging() && p.take_damage(10)==0,"Reduced camera motion removes roll without changing dodge or protection");
        advance(p,30);p.apply_control(ControlEffect::Pull,{7,true},true,false,{0,1.7,-5});advance(p,5);
        require(std::abs(p.camera_direction().y)<1e-9 && p.crowd_control().active==ControlEffect::Pull,"Reduced motion removes forced camera jolt without changing crowd control");
    }
    {
        auto clearance=level;Character ceiling;ceiling.Shape="cube";ceiling.Width=3;ceiling.Depth=1;ceiling.Height=.2;ceiling.Y=3.4;ceiling.Z=-1;clearance.Characters={ceiling};
        AdventureController controller(clearance,{0,3.7,0});
        require(!controller.traverse({0,3.7,0},{0,.2,-.8}),"Standing mantle sweep rejects a low ceiling along the travel path");
        require(controller.traverse({0,3.7,0},{0,.2,.8}),"Standing mantle sweep allows clear travel away from the ceiling");
    }
    {
        AdventurePlayer p;p.start(level);advance(p,30);auto start=p.position;
        p.apply_control(ControlEffect::Stun,{1,true},true);p.jump();p.fire_button(true);advance(p,60,1,1);
        require((p.position-start).length()<.02 && !p.basic_attack().winding && !p.dodge(1,0),"Applied stun blocks actual controller movement jump dodge and firing");
        p.cleanse_control();advance(p,30,0,1);
        require(p.position.z<start.z-.5 && p.crowd_control().immunity>4,"Cleanse restores movement and starts immunity immediately");
        p.start(level);advance(p,30);start=p.position;p.apply_control(ControlEffect::Root,{1,true},true);p.fire_button(true);advance(p,30,1,0);
        require((p.position-start).length()<.02 && p.basic_attack().winding && !p.lunge(),"Applied root locks movement but permits the active attack cycle");
        p.start(level);advance(p,30);p.apply_control(ControlEffect::Slow,{1,true},true);advance(p,60,1,0);
        require(std::abs(p.position.x-level.Game->MoveSpeed*.67*.5)<.08,"Applied slow reduces actual controller travel by thirty-three percent");
        p.start(level);advance(p,30);p.apply_control(ControlEffect::Silence,{1,true},true);p.fire_button(true);advance(p,30,1,0);
        require(p.position.x>.5 && p.basic_attack().winding && p.dodge(1,0),"Applied silence preserves actual movement basic attack and dodge");
        p.start(level);advance(p,30);p.apply_control(ControlEffect::Disorient,{1,true},true);p.fire_button(true);advance(p,30,1,0);
        require(p.position.x>.5 && !p.basic_attack().winding && p.dodge(1,0),"Applied disorient allows movement and dodge but blocks basic attack");
        p.start(level);advance(p,30);p.dodge(1,0);
        require(p.take_damage(20)==0 && p.health()==100,"Active dodge prevents health damage");
        advance(p,40);require(p.take_damage(20)==20 && p.health()==80,"Damage applies after dodge protection ends");
        p.start(level);advance(p,30);p.lunge();
        require(p.take_damage(20)==0,"Active lunge prevents health damage");
        p.start(level);advance(p,30);p.apply_control(ControlEffect::Knockback,{1,true},true,false,{0,1.7,-3});advance(p,60);
        require(p.position.z>2.8 && p.position.z<3.1,"Knockback moves the actual controller backward about three metres");
        p.start(level);advance(p,30);p.apply_control(ControlEffect::Pull,{1,true},true,false,{0,1.7,-5});advance(p,60);
        require(p.position.z<-3.8 && p.position.z>=-4.01,"Pull stops the actual controller one metre from its source");
        p.start(level);advance(p,30);p.apply_control(ControlEffect::Fear,{1,true},true,false,{0,1.7,-3});advance(p,60);
        require(p.position.z>1.8 && p.crowd_control().blocks_basic(),"Fear drives actual movement away from its source");
        p.take_damage(1);require(!p.crowd_control().active && p.crowd_control().immunity==5,"Damage breaks applied fear and starts immunity");
        p.take_damage(1000);auto deadPosition=p.position;advance(p,30,1,1);
        require(p.health()==0 && (p.position-deadPosition).length()<.02,"Dead player cannot keep moving under held controls");
        p.start(level);require(p.health()==100 && !p.crowd_control().active,"Restart restores health and clears control effects");
        advance(p,30);p.apply_control(ControlEffect::Launch,{1,true},true,false,{0,1.7,-3});advance(p,60);
        require(p.position.y>3 && p.position.z>2.8 && p.crowd_control().blocks_basic(),"Launch lifts and throws player backward while blocking fire");
        advance(p,105);
        require(p.grounded() && p.crowd_control().active==ControlEffect::Launch,"Launch remains controlled through landing recovery");
        advance(p,150);require(!p.crowd_control().active && p.crowd_control().immunity>0,"Launch recovery ends with player immunity");
        p.start(level);advance(p,30);p.apply_control(ControlEffect::Tether,{1,true},true,false,{0,1.7,0});advance(p,230,1,0);
        require(p.position.x<6.4 && p.crowd_control().active==ControlEffect::Tether,"Tether yanks the actual controller inside its allowed range");
    }
    {
        AdventurePlayer cameraPlayer;cameraPlayer.start(level);
        auto facing=cameraPlayer.yaw;cameraPlayer.orbit_camera(30,0);
        require(cameraPlayer.yaw==facing && cameraPlayer.cameraYaw!=facing,"Independent camera orbit preserves character facing");
        cameraPlayer.look(10,0);
        require(cameraPlayer.yaw==cameraPlayer.cameraYaw,"Right look aligns character and camera facing");
        cameraPlayer.zoom(100);
        require((cameraPlayer.camera_eye()-cameraPlayer.position).length()<1e-8,"Wheel zoom reaches first person");
        cameraPlayer.zoom(-100);
        require(cameraPlayer.cameraDistance==12,"Third-person zoom has a bounded maximum distance");
        Character wall;wall.Shape="cube";wall.Width=5;wall.Height=5;wall.Depth=.2;wall.Z=2;
        auto cameraLevel=level;cameraLevel.Characters={wall};cameraPlayer.start(cameraLevel);
        require(cameraPlayer.camera_eye().z<2,"Third-person camera boom stops before obstructing geometry");
        cameraPlayer.start(level);advance(cameraPlayer,30);cameraPlayer.zoom(100);cameraPlayer.dodge(1,0);advance(cameraPlayer,15);
        require(cameraPlayer.camera_up().y<-.99,"First-person dodge rotates the camera through its roll");
        cameraPlayer.zoom(-2);
        require(cameraPlayer.camera_up().y>.99,"Third-person dodge camera remains upright");
        cameraPlayer.zoom(100);advance(cameraPlayer,20);
        require(cameraPlayer.camera_up().y>.99,"Completed dodge clears first-person camera roll");
    }
    {
        auto grappleLevel=level;
        Character wall;wall.Shape="cube";wall.Width=8;wall.Height=8;wall.Depth=1;wall.Z=-5;
        grappleLevel.Characters={wall};
        AdventurePlayer g;g.start(grappleLevel);advance(g,30);
        g.grapple_button(true);g.apply_control(ControlEffect::Root,{9,true},true);advance(g,30);
        require(g.grapple_motion().state==GrappleMotion::State::Free && g.position.z>-.1,"Root cancels grapple pull before wall arrival");
        g.start(grappleLevel);advance(g,30);
        g.grapple_button(true);advance(g,120);
        require(g.grapple_motion().state==GrappleMotion::State::Wall && g.position.z>-4.5 && g.position.z<-3.5,"Grapple reaches wall through collision controller");
        g.apply_control(ControlEffect::Root,{9,true},true);auto rooted=g.position;advance(g,30,0,1);
        require(g.grapple_motion().state==GrappleMotion::State::Wall && (g.position-rooted).length()<.03,"Root keeps passive wall attachment but blocks climbing");
        g.cleanse_control();
        g.grapple_button(false);auto attached=g.position;advance(g,30);
        require((g.position-attached).length()<.03,"Releasing G preserves actual wall attachment");
        advance(g,60,0,1);
        require(g.position.y>attached.y+.8,"Attached W climbs the wall");
        g.jump();advance(g,15);
        require(g.position.z>attached.z+.2 && g.grapple_motion().state==GrappleMotion::State::Free,"Space wall jump detaches and moves outward");
    }
    {
        auto grappleLevel=level;Character ceiling;ceiling.Shape="cube";ceiling.Width=12;ceiling.Depth=12;ceiling.Height=1;ceiling.Y=7;
        grappleLevel.Characters={ceiling};AdventurePlayer g;g.start(grappleLevel);advance(g,30);g.cameraPitch=1.4;
        g.grapple_button(true);
        require(g.grapple_motion().state==GrappleMotion::State::Swing,"Overhead surface selects actual swing traversal");
        advance(g,60,1,0);auto before=g.position;g.grapple_button(false);advance(g,12);
        require(g.grapple_motion().state==GrappleMotion::State::Free && g.position.x>before.x,"Controller preserves swing momentum after release");
        g.start(level);advance(g,30);g.grapple_button(true);auto charges=g.movement_resources().grappleCharges;g.grapple_button(true);
        require(charges==1 && g.movement_resources().grappleCharges==1,"In-game missed G spends one charge without held repeat");
    }
    {
        auto grappleLevel=level;Character wall;wall.Shape="cube";wall.Width=8;wall.Height=2;wall.Depth=3;wall.Z=-5;grappleLevel.Characters={wall};
        AdventurePlayer g;g.start(grappleLevel);advance(g,30);g.grapple_button(true);advance(g,120);g.grapple_button(false);
        advance(g,240,0,1);
        require(g.grapple_motion().state==GrappleMotion::State::Free && g.position.y>3.5 && g.position.z<-3.5,"Wall climb crosses top edge through controller movement");
        auto terrainLevel=level;terrainLevel.Terrain.emplace();g.start(terrainLevel);advance(g,30);g.cameraPitch=-.8;g.grapple_button(true);
        require(g.grapple_motion().state==GrappleMotion::State::Pulling && g.movement_resources().grappleCharges==1,"Grapple picks terrain surface and spends one charge");
        advance(g,180);require(g.grapple_motion().state!=GrappleMotion::State::Wall,"Terrain floor never becomes an attached climbable wall");
    }
    {
        AdventurePlayer p;p.start(level);advance(p,30);p.jump();advance(p,12);auto charges=p.movement_resources().dodgeCharges;
        require(!p.lunge() && p.movement_resources().dodgeCharges==charges,"Airborne lunge rejected without consuming charges");
        p.start(level);advance(p,30);p.set_posture(2);p.jump();advance(p,12);
        require(p.posture()==0 && p.position.y>2,"Jump requests safe stand from prone before leaving ground");
        p.position.y=-200;advance(p,1);
        require(p.health()==0 && !p.basic_attack().winding && !p.presentation().showTether,"Deep fall defeats player and clears transient actions");
        p.jump();p.grapple_button(true);p.posture_button(true,0,0,.5);
        require(!p.lunge() && p.grapple_motion().state==GrappleMotion::State::Free,"Defeated player cannot restart traversal");
        p.start(level);require(p.health()==100 && p.movement_resources().grappleCharges==2,"Reset restores player after fall defeat");
    }
    {
        auto displacedLevel=level;Character enemy;enemy.Shape="cube";enemy.Id="displaced-body";enemy.Z=-2;enemy.Width=2;enemy.Height=3;enemy.CombatHealth=100;displacedLevel.Characters={enemy};
        AdventurePlayer p;p.start(displacedLevel);advance(p,30);p.control_actor(enemy.Id,ControlEffect::Knockback,{1,true});advance(p,60);advance(p,90,0,1);
        require(p.position.z<-2.2,"Displaced non-pursuing NPC collision body leaves its old location");
    }
    {
        AdventurePlayer p;p.start(level);advance(p,30);p.apply_control(ControlEffect::Slow,{9,true},true);p.dodge(0,1);advance(p,30);
        atomic_write(folder/"slow-dodge.json",json{{"z",p.position.z},{"dodging",p.dodging()}}.dump(2));
        require(p.position.z<-1.9 && p.position.z>-2.1 && !p.dodging(),"Slow scales dodge distance while keeping quarter-second duration");
        p.start(level);advance(p,30);p.apply_control(ControlEffect::Slow,{9,true},true);p.lunge();advance(p,60);
        require(p.position.z<-3.9 && p.position.z>-4.1 && !p.lunging(),"Slow scales lunge distance while keeping half-second duration");
    }
    {
        auto tetherLevel=level;Character wall;wall.Shape="cube";wall.Width=8;wall.Height=8;wall.Z=-5;tetherLevel.Characters={wall};
        AdventurePlayer p;p.start(tetherLevel);advance(p,30);p.grapple_button(true);advance(p,120);
        p.apply_control(ControlEffect::Tether,{9,true},true,false,{0,1.7,8});advance(p,1);
        require(p.grapple_motion().state==GrappleMotion::State::Free,"Hostile tether yank detaches voluntary wall grapple");
    }
    {
        auto movingLevel=level;Character anchor;anchor.Shape="cube";anchor.Width=8;anchor.Height=5;anchor.Z=-5;anchor.CombatHealth=100;anchor.Route={{6,-5}};anchor.Speed=1;movingLevel.Characters={anchor};
        AdventurePlayer p;p.start(movingLevel);advance(p,30);p.grapple_button(true);advance(p,120);p.grapple_button(false);auto before=p.position;
        advance(p,120);
        require(p.grapple_motion().state==GrappleMotion::State::Wall && p.position.x>before.x+.8 && p.position.x<before.x+1.2,"Wall grapple follows moving character anchor without doubling its velocity");
        p.fire_button(true);p.fire_button(false);advance(p,30);
        require(p.basic_attack().winding && p.grapple_motion().state==GrappleMotion::State::Wall,"Moving character attachment permits firing");
        auto deathLevel=level;auto fragile=anchor;fragile.Route.clear();fragile.CombatHealth=10;deathLevel.Characters={fragile};p.start(deathLevel);advance(p,30);p.grapple_button(true);advance(p,120);p.grapple_button(false);p.fire_button(true);p.fire_button(false);advance(p,320);
        require(p.scene().Characters.empty() && p.grapple_motion().state==GrappleMotion::State::Free,"Destroying the attached actor clears the grapple instead of following a dead anchor");
        auto overrideLevel=level;anchor.Route.clear();anchor.GrappleMode=2;overrideLevel.Characters={anchor};p.start(overrideLevel);advance(p,30);p.grapple_button(true);
        require(p.grapple_motion().state==GrappleMotion::State::Swing,"Creator swing override applies to side surface");
        anchor.GrappleMode=1;anchor.Y=7;anchor.Z=0;anchor.Width=12;anchor.Depth=12;overrideLevel.Characters={anchor};p.start(overrideLevel);advance(p,30);p.cameraPitch=1.4;p.grapple_button(true);
        require(p.grapple_motion().state==GrappleMotion::State::Pulling,"Creator wall override changes overhead automatic swing selection");
    }
    AdventurePlayer player;player.start(level);advance(player,30);
    require(player.grounded(),"Capsule controller settles onto terrain");
    double ground=player.position.y,peak=ground;player.jump();
    for(int i=0;i<150;i++){player.move(0,0,1./120);peak=std::max(peak,player.position.y);}
    motion["jump"]={{"ground",ground},{"peak",peak},{"landed",player.position.y}};
    require(peak>ground+1 && peak<ground+1.5 && std::abs(player.position.y-ground)<.04 && player.grounded(),"Grounded jump rises and lands with bounded gravity");
    player.jump();advance(player,20);double airborne=player.position.y;player.jump();advance(player,20);
    require(player.position.y>ground+1.3 && player.position.y>airborne,"Second press produces a real midair jump");
    AdventurePlayer control;control.start(level);advance(control,30);control.jump();advance(control,20);control.jump();advance(control,20);
    player.jump();advance(player,10);advance(control,10);
    require(std::abs(player.position.y-control.position.y)<.001,"Third airborne press cannot grant another jump");
    advance(player,240);ground=player.position.y;player.jump();advance(player,20);player.jump();advance(player,20);
    require(player.position.y>ground+1.3,"Landing restores the double jump");
    Character ceiling;ceiling.Shape="cube";ceiling.Width=4;ceiling.Depth=4;ceiling.Height=.2;ceiling.Y=2.1;
    level.Characters={ceiling};player.start(level);advance(player,30);ground=player.position.y;peak=ground;player.jump();
    for(int i=0;i<150;i++){player.move(0,0,1./120);peak=std::max(peak,player.position.y);}
    motion["ceilingPeak"]=peak;
    require(peak<ground+.4 && player.grounded(),"Capsule jump is stopped by a low ceiling");
    Character step;step.Shape="cube";step.Width=4;step.Depth=1;step.Height=.25;
    level.Game->SpawnZ=2;level.Characters={step};player.start(level);advance(player,30);advance(player,75,0,1);
    motion["step"]={{"z",player.position.z},{"eyeY",player.position.y}};
    atomic_write(folder/"character-controller-measurements.json",motion.dump(2));
    require(player.position.z<.5 && player.position.y>1.85,"Capsule controller climbs a 25 centimetre step");
    step.Height=.7;level.Characters={step};player.start(level);advance(player,30);advance(player,100,0,1);
    require(player.position.z>.7 && player.position.y<1.8,"Step above the 40 centimetre limit remains a wall");
    require(player.dodge(0,1),"Directional dodge starts with an available charge");
    advance(player,40);
    require(player.position.z>.7,"Dodge respects the capsule wall collision");
    level.Characters.clear();level.Game->SpawnZ=0;player.start(level);advance(player,30);
    auto dodgeStart=player.position;
    player.dodge(1,1);advance(player,30);
    require(std::abs((player.position-dodgeStart).length()-3)<.1,"Diagonal dodge travels three metres without diagonal speed advantage");
    player.start(level);advance(player,30);player.movement_press('A',1);player.movement_press('A',1.2);advance(player,30);
    require(player.position.x<-2.9 && player.movement_resources().dodgeCharges==1,"Double-tap triggers the matching directional dodge");
    player.start(level);advance(player,30);
    require(player.presentation().motion==AdventurePlayer::Presentation::Motion::Idle,"Presentation reports settled idle without a character rig");
    player.cameraDistance=0;
    require(!player.presentation().showBody,"First-person presentation hides the placeholder body");
    player.cameraDistance=5;player.jump();advance(player,3);
    require(player.presentation().motion==AdventurePlayer::Presentation::Motion::Jump && player.presentation().velocity.y>0,"Presentation follows actual upward controller movement");
    player.start(level);advance(player,30);player.mobility_button(true,0,1,.1);
    require(player.movement_resources().dodgeCharges==2,"V press waits for tap versus hold without spending a charge");
    player.mobility_button(false,0,1);advance(player,30);
    require(player.position.z<-2.9 && player.movement_resources().dodgeCharges==1,"Short V tap triggers one dodge");
    require(!player.lunge() && player.movement_resources().dodgeCharges==1,"Lunge with one charge is rejected without partial spending");
    player.start(level);advance(player,30);player.mobility_button(true,0,1,.23);
    require(player.lunging() && player.movement_resources().dodgeCharges==0,"Held V starts lunge and spends both charges");
    advance(player,60);player.mobility_button(false,0,1);
    require(player.position.z<-5.9 && player.movement_resources().dodgeCharges==0,"Lunge travels farther than dodge and release does not add a dodge");
    player.start(level);advance(player,30);player.posture_button(true,0,0,.1);player.posture_button(false,0,0);
    require(player.posture()==1,"Short stationary Control selects crouch");
    player.posture_button(true,0,0,.4);player.posture_button(false,0,0);
    require(player.posture()==2,"Held stationary Control selects prone");
    auto crawlStart=player.position;advance(player,60,0,1);
    require(player.position.z<crawlStart.z && player.position.z>crawlStart.z-1,"Prone permits slower crawling");
    player.posture_button(true,0,0,.1);player.posture_button(false,0,0);
    require(player.posture()==0,"Short Control exits prone when standing clearance is available");
    player.posture_button(true,0,0);player.cancel_mobility_input();player.posture_button(false,0,0);
    require(player.posture()==0,"Focus cancellation does not replay a pending crouch tap");
    player.mobility_button(true,0,1);player.cancel_mobility_input();player.mobility_button(false,0,1);
    require(player.movement_resources().dodgeCharges==2,"Focus cancellation does not spend a pending dodge");
    player.posture_button(true,0,1,.01);advance(player,80);
    require(player.posture()==1 && player.position.z<crawlStart.z-2,"Running Control slides and settles into crouch");
    {
        auto lowRoom=level;Character roof;roof.Shape="cube";roof.Width=4;roof.Depth=4;roof.Height=.2;roof.Y=1.2;roof.Z=-3;lowRoom.Characters={roof};
        AdventurePlayer croucher;croucher.start(lowRoom);advance(croucher,30);require(croucher.set_posture(1),"Crouch collision shape can shrink");
        advance(croucher,180,0,1);
        require(croucher.position.z<-1.5 && !croucher.set_posture(0) && croucher.posture()==1,"Low ceiling prevents standing up through geometry");
    }
    Character platform;platform.Shape="cube";platform.Width=4;platform.Depth=4;platform.Height=.5;platform.Speed=1;platform.Route={{3,0}};
    level.Game->SpawnZ=0;level.Game->SpawnY=2.22;level.Characters={platform};auto authored=level.encode();player.start(level);advance(player,240);
    motion["platform"]={{"x",player.position.x},{"eyeY",player.position.y},{"time",player.seconds()}};
    atomic_write(folder/"character-controller-measurements.json",motion.dump(2));
    require(player.position.x>1.7 && player.position.x<2.2 && player.position.y>2.15 && player.grounded(),"Idle character is carried by a route-driven platform");
    require(level.encode()==authored,"Moving platform and character leave authored data unchanged");
    auto carried=player.position.x;player.jump();advance(player,40);
    require(player.position.y>2.6 && player.position.x>=carried-.05,"Character can jump off a moving platform");
    player.start(level);advance(player,10);
    require(player.position.x<.2 && player.seconds()<.1,"Reset restarts platform time and player state");
    player.start(level,false);advance(player,120);
    require(std::abs(player.position.x)<.05 && player.seconds()==0,"Authoring exploration keeps route platforms stationary");
    atomic_write(folder/"character-controller-measurements.json",motion.dump(2));
    {
        // GAME-3: standing on an object must not erase the message an enter-area rule produced in the same frame.
        SceneData stand;stand.Game.emplace();stand.Game->SpawnZ=0;
        Character floor;floor.Shape="cube";floor.Width=20;floor.Depth=20;floor.Height=1;floor.Y=-1;
        GameplayRule finish;finish.Event="enter-area";finish.X=3;finish.Y=1.7;finish.Z=0;finish.Radius=.5;finish.Actions={{"finish-game",""}};
        stand.Characters={floor};stand.Rules={finish};
        AdventurePlayer p;p.start(stand);advance(p,30);
        bool shown=false;for(int i=0;i<240&&!shown;++i){p.move(1,0,1./120);shown=p.rules().message=="Adventure complete!";}
        require(p.finished&&shown,"GAME-3: The message from an area rule survives standing on an object");
    }
}
#endif
