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
}
