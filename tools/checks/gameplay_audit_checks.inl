// Gameplay audit repairs (08#1 camera boom, 08#4 lightning chain, 08#5 tab-targeting of routed breakables).
static void gameplay_audit_checks(const auto& require){
    {
        // 08#1: effect shapes are not solid, so the camera boom ignores them as it ignores keys; a real wall still stops it.
        for(const char* shape:{"fire","light","craxis-smoke","grass","lava"}){
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;
            Character effect;effect.Shape=shape;effect.Width=effect.Height=effect.Depth=5;effect.Z=2;level.Characters={effect};
            AdventurePlayer p;p.start(level);
            require(p.camera_eye().z>p.view_position().z+4.99,(std::string("08#1: The camera boom is not shortened by a non-solid ")+shape+" object behind the player").c_str());
        }
        SceneData level;level.Game.emplace();level.Game->SpawnZ=0;
        Character wall;wall.Shape="cube";wall.Width=5;wall.Height=5;wall.Depth=.2;wall.Z=2;level.Characters={wall};
        AdventurePlayer p;p.start(level);
        require(p.camera_eye().z<2,"08#1: A solid wall behind the player still stops the camera boom");
    }
    {
        // 08#5: tab-targeting measures a routed breakable where it is now, not where it started.
        SceneData level;level.Game.emplace();level.Game->SpawnZ=0;
        Character platform;platform.Shape="cube";platform.Breakable=true;platform.Z=-100;platform.Speed=50;platform.Route={{0,-4}};level.Characters={platform};
        AdventurePlayer p;p.start(level);
        for(int i=0;i<240;++i)p.move(0,0,1./120.);
        p.select_target();
        require(p.selected_target()==platform.Id,"08#5: A breakable that has moved along its route into view is selected from its current position");
    }
    {
        // 08#4: a chain hop goes to the nearest body that is actually reachable; a body hidden behind a nearer one is only chained to after it.
        SceneData storm;storm.Game.emplace();
        Character bolt;bolt.Shape="chaos-lightning";bolt.X=3;bolt.Y=3;bolt.TargetX=0;bolt.TargetY=-3;bolt.TargetZ=0;bolt.LightningChains=2;bolt.BranchSpread=0;bolt.LightningChainRadius=10;
        Character first;first.Shape="cube";first.X=3;Character second=first;second.Id=new_id();second.X=5;Character third=first;third.Id=new_id();third.X=7;
        storm.Characters={bolt,third,first,second};
        auto path=trace_lightning(bolt,storm,0);
        require(path.size()==3&&path[0].hit==first.Id&&path[1].hit==second.Id&&path[2].hit==third.Id,"08#4: Lightning chains to the nearest reachable body on each hop and reaches the one behind it afterwards");
    }
    {
        // GAME-1: without collision a Launch must still end.
        SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
        AdventurePlayer p;p.start(level);
        p.apply_control(ControlEffect::Launch,{1,true},true,false,{0,1.7,-3});
        for(int i=0;i<720;++i)p.move(0,0,1./120.);
        require(!p.crowd_control().active,"GAME-1: A launch on a player in a no-collision scene ends instead of freezing the player");
    }
    {
        // GAME-2: forced movement (knockback, pull) must move the player when collision is off.
        SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
        AdventurePlayer p;p.start(level);auto start=p.position;
        p.apply_control(ControlEffect::Knockback,{1,true},true,false,{0,1.7,-3});
        for(int i=0;i<60;++i)p.move(0,0,1./120.);
        require(p.position.z>start.z+2.5,"GAME-2: Knockback moves the player in a no-collision scene");
        p.start(level);start=p.position;
        p.apply_control(ControlEffect::Pull,{1,true},true,false,{0,1.7,-5});
        for(int i=0;i<60;++i)p.move(0,0,1./120.);
        require(p.position.z<start.z-3,"GAME-2: Pull moves the player in a no-collision scene");
    }
    {
        // GAME-4: line of sight ignores objects that never block movement; a real wall still blocks it.
        for(const char* shape:{"grass","fire","light","lava","craxis-smoke","key"}){
            SceneData level;level.Game.emplace();
            Character c;c.Shape=std::string(shape)=="key"?"cube":shape;if(std::string(shape)=="key"){c.GameplayRole="key";c.GameplayKey="k";}c.Width=c.Height=c.Depth=2;c.Z=-3;level.Characters={c};
            AdventurePlayer p;p.start(level);
            require(p.unobstructed({0,1,0},{0,1,-6},"","far"),(std::string("GAME-4: Line of sight passes through a non-blocking ")+shape+" object").c_str());
        }
        SceneData level;level.Game.emplace();
        Character wall;wall.Shape="cube";wall.Width=wall.Height=wall.Depth=2;wall.Z=-3;level.Characters={wall};
        AdventurePlayer p;p.start(level);
        require(!p.unobstructed({0,1,0},{0,1,-6},"","far"),"GAME-4: Line of sight is still blocked by a solid cube");
    }
}
