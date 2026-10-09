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
        // With two bodies in the clear on either side of the struck one, the hop goes to the nearer of them whatever order the bodies are stored in.
        Character closer=first;closer.Id=new_id();closer.X=3;closer.Z=4;Character farther=first;farther.Id=new_id();farther.X=3;farther.Z=-7;
        Character single=bolt;single.LightningChains=1;
        const std::vector<std::vector<Character>> orders{{single,first,farther,closer},{single,first,closer,farther}};
        for(const auto& order:orders){
            storm.Characters=order;
            auto pair=trace_lightning(single,storm,0);
            require(pair.size()==2&&pair[0].hit==first.Id&&pair[1].hit==closer.Id,"08#4: With two reachable bodies the chain hop goes to the nearer one");
        }
    }
}
