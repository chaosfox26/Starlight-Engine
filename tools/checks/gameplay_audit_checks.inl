// Gameplay audit repairs (08#1 camera boom, 08#2 boom smoothing, 08#4 lightning chain, 08#5 tab-targeting of routed breakables, 08#7 deterministic rubble, 08#8 presentation cube).
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
    {
        // 08#2: with STARLIGHT_CAMERA_BOOM_SMOOTHING=1 the boom closes at once, opens over about 0.15 s, and keeps a minimum length; unset, it is exactly as before.
        require(AdventurePlayer::eased_boom_length(-1,3,1./60,5)==3,"08#2: The first boom sample is taken as it is");
        require(AdventurePlayer::eased_boom_length(4,1,1./60,5)==1,"08#2: A boom that has to shorten does so at once");
        require(AdventurePlayer::eased_boom_length(1,5,0,5)==1,"08#2: No time passing means no outward easing");
        require(AdventurePlayer::eased_boom_length(1,5,5,5)==5,"08#2: A long gap restarts at the target");
        require(AdventurePlayer::eased_boom_length(-1,0,1./60,5)==.25&&AdventurePlayer::eased_boom_length(-1,0,1./60,.1)==.1,"08#2: The boom never drops below 0.25 m, or below the requested length when that is shorter");
        double length=1;bool rising=true;
        for(int tick=0;tick<9;++tick){const double next=AdventurePlayer::eased_boom_length(length,5,1./60,5);rising&=next>length&&next<5;length=next;}
        require(rising&&length>1+.94*4&&length<5,"08#2: The boom opens smoothly and is nearly open after 0.15 s");
        auto wall_at=[](double z){SceneData level;level.Game.emplace();level.Game->SpawnZ=0;Character wall;wall.Shape="cube";wall.Width=5;wall.Height=5;wall.Depth=.2;wall.Z=z;level.Characters={wall};return level;};
        auto boom=[](AdventurePlayer& p){return (p.camera_eye()-p.view_position()).length();};
        for(const char* setting:{"","1"}){
            _putenv_s("STARLIGHT_CAMERA_BOOM_SMOOTHING",setting);
            const bool on=setting[0]=='1';
            AdventurePlayer close;close.start(wall_at(.25));
            const double collapsed=boom(close);
            require(on?collapsed>=.25-1e-9:collapsed<1e-9,on?"08#2: With smoothing on, a wall almost touching the player leaves a 0.25 m boom":"08#2: With smoothing off, a wall almost touching the player collapses the boom as before");
            AdventurePlayer turning;turning.start(wall_at(2));
            const double blocked=boom(turning);
            turning.cameraYaw=3.14159265358979;turning.move(0,0,1./60);
            const double first=boom(turning);
            for(int tick=0;tick<60;++tick)turning.move(0,0,1./60);
            const double settled=boom(turning);
            require(blocked<2&&std::abs(settled-5)<1e-6,"08#2: The boom reaches its full length once the wall is no longer behind the camera");
            require(on?first>blocked+.01&&first<4.9:std::abs(first-5)<1e-9,on?"08#2: With smoothing on, the boom opens gradually after the wall is passed":"08#2: With smoothing off, the boom opens in one frame as before");
        }
        _putenv_s("STARLIGHT_CAMERA_BOOM_SMOOTHING","");
    }
    {
        // 08#7: STARLIGHT_DETERMINISTIC_RUBBLE=1 gives the same pieces for the same break id in every run; unset, each run still differs.
        auto rubble_signature=[](const char* setting){
            _putenv_s("STARLIGHT_DETERMINISTIC_RUBBLE",setting);
            auto scene=std::make_unique<SceneData>();scene->Game.emplace();scene->Game->SpawnZ=0;
            Character stone;stone.Shape="boulder";stone.Id="deterministic-boulder";stone.Size=2;stone.Z=-3;stone.Color="#555D67";stone.DestructionHealth=10;scene->Characters={stone};
            auto player=std::make_unique<AdventurePlayer>();player->start(*scene);player->move(0,0,1./120.);
            player->apply_attack_hits({stone.Id},1000);
            double sum=0;size_t pieces=0;
            for(const auto& f:player->presentation().fragments)if(f.part){++pieces;for(const auto& v:f.part->mesh.vertices)sum+=v.position.x*1.3+v.position.y*1.7+v.position.z*1.9;}
            _putenv_s("STARLIGHT_DETERMINISTIC_RUBBLE","");
            return std::pair<size_t,double>{pieces,sum};
        };
        const auto first=rubble_signature("1"),second=rubble_signature("1"),varied=rubble_signature("0"),other=rubble_signature("0");
        require(first.first>0&&first==second,"08#7: With STARLIGHT_DETERMINISTIC_RUBBLE=1 the same break gives the same pieces in every run");
        require(varied!=other,"08#7: By default the same break still gives different pieces from run to run");
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
    {
        // GAME-5: a routed actor that fell far behind its route point keeps walking back to it.
        SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=200;
        Character a;a.Shape="cube";a.Id="lagging-npc";a.CombatHealth=100;a.Speed=30;a.Route={{100,0}};level.Characters={a};
        AdventurePlayer p;p.start(level);
        for(int i=0;i<120*15;++i)p.move(0,0,1./120.);
        require(p.scene().Characters[0].X>90,"GAME-5: A fast routed actor is not stranded once it lags more than twenty metres behind its route point");
    }
    {
        // GAME-6: a heightmap actor pushing uphill into a wall stays on the terrain.
        SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;level.Terrain.emplace();
        for(int z=0;z<TerrainData::N;z++)for(int x=0;x<TerrainData::N;x++)level.Terrain->Heights[z*TerrainData::N+x]=2+(x*TerrainData::Step-TerrainData::Half)*.1;
        Character enemy;enemy.Shape="cube";enemy.Id="wall-pusher";enemy.X=-5;enemy.Y=1.5;enemy.CombatHealth=100;enemy.CombatPursuit=true;
        Character wall;wall.Shape="cube";wall.X=-3.5;wall.Y=0;wall.Width=.2;wall.Depth=60;wall.Height=10;level.Characters={enemy,wall};
        AdventurePlayer p;p.start(level);for(int i=0;i<600;i++)p.move(0,0,1./120.);
        const auto& c=p.scene().Characters[0];
        require(c.X<-3.5&&std::abs(c.Y-level.Terrain->height_at(c.X,c.Z))<.03,"GAME-6: An actor blocked by a wall on a slope does not climb off the terrain");
    }
    {
        // GAME-7: effect shapes do not block the player, so they cannot make the start position invalid.
        for(const char* shape:{"grass","lava","chaos-lightning","light-shard"}){
            SceneData level;level.Game.emplace();
            Character c;c.Shape=shape;c.Width=c.Height=c.Depth=4;c.Z=6;level.Characters={c};
            bool inside=false;for(const auto& f:adventure_readiness(level))inside=inside||f.find("starts inside a solid")!=std::string::npos;
            require(!inside,(std::string("GAME-7: Spawning inside a ")+shape+" object is not reported as standing in a solid").c_str());
        }
        SceneData level;level.Game.emplace();
        Character c;c.Shape="cube";c.Width=c.Height=c.Depth=4;c.Z=6;level.Characters={c};
        bool inside=false;for(const auto& f:adventure_readiness(level))inside=inside||f.find("starts inside a solid")!=std::string::npos;
        require(inside,"GAME-7: Spawning inside a solid cube is still reported");
    }
    {
        // GAME-9: a scene that fails validation leaves the running session untouched.
        SceneData level;level.Game.emplace();level.Game->Collision=false;
        Character enemy;enemy.Shape="cube";enemy.CombatHealth=100;enemy.Z=-3;level.Characters={enemy};
        AdventurePlayer p;p.start(level);for(int i=0;i<30;++i)p.move(0,0,1./120.);
        auto bad=level;bad.Characters.clear();bad.Game->MoveSpeed=0;
        bool threw=false;try{p.start(bad);}catch(...){threw=true;}
        require(threw&&p.combat_actors().size()==1&&p.seconds()>0&&p.scene().Characters.size()==1,"GAME-9: A scene that fails validation does not reset the running session");
    }
    {
        // P2A3-1: without collision there is no airborne state, so walking reports Walk and standing still reports Idle, not Fall.
        SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
        AdventurePlayer p;p.start(level);
        for(int i=0;i<60;++i)p.move(0,1,1./120.);
        require(p.presentation().motion==AdventurePlayer::Presentation::Motion::Walk,"P2A3-1: Walking without collision reports Walk, not Fall");
        for(int i=0;i<60;++i)p.move(0,0,1./120.);
        require(p.presentation().motion==AdventurePlayer::Presentation::Motion::Idle,"P2A3-1: Standing still without collision reports Idle, not Fall");
    }
}
