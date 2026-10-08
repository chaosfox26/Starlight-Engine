// Gameplay audit repairs (08#1 camera boom, 08#4 lightning chain, 08#5 tab-targeting of routed breakables, 08#8 presentation cube).
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
        // 08#8: the shared unit cube equals a freshly built one, and building presentation boxes does not alter it.
        Character c;c.Shape="cube";const auto fresh=shape_geometry(c);const auto& cached=unit_cube_parts();
        bool same=fresh.size()==cached.size();
        for(size_t i=0;same&&i<fresh.size();++i){
            const auto &a=fresh[i],&b=cached[i];
            same&=a.mesh.indices==b.mesh.indices&&a.mesh.vertices.size()==b.mesh.vertices.size()&&a.color.x==b.color.x&&a.color.y==b.color.y&&a.color.z==b.color.z&&a.emissive==b.emissive&&!a.gpuTelegraph&&!b.gpuTelegraph;
            for(size_t k=0;same&&k<a.mesh.vertices.size();++k){const auto &u=a.mesh.vertices[k],&v=b.mesh.vertices[k];
                same&=u.position.x==v.position.x&&u.position.y==v.position.y&&u.position.z==v.position.z&&u.normal.x==v.normal.x&&u.normal.y==v.normal.y&&u.normal.z==v.normal.z&&u.color.x==v.color.x&&u.color.y==v.color.y&&u.color.z==v.color.z&&u.kind==v.kind&&u.motionIdentity==v.motionIdentity&&u.materialCoordinate.has_value()==v.materialCoordinate.has_value();}
        }
        require(same&&!cached.empty(),"08#8: The cached presentation cube is identical to a freshly built one");
        AdventurePlayer::Presentation p;p.showBody=true;p.showWeapons=true;p.showTether=true;p.shotFlash=.5;p.feet={1,2,3};p.facing=.7;p.eye={1,3.5,3};p.viewAim={0,0,-1};p.viewUp={0,1,0};p.attackAim={0,0,-1};p.tetherAnchor={4,5,6};
        auto digest=[](const std::vector<MeshPart>& parts){double sum=0;size_t count=0;for(const auto& part:parts){count+=part.mesh.vertices.size();for(const auto& v:part.mesh.vertices)sum+=v.position.x*1.3+v.position.y*1.7+v.position.z*1.9+v.normal.x*.3+double(v.motionIdentity%977);}return std::pair<size_t,double>{count,sum};};
        const auto once=digest(player_presentation(p)),again=digest(player_presentation(p));
        require(once.first>0&&once==again,"08#8: Presenting twice gives identical geometry, so the shared cube is never altered");
    }
}
