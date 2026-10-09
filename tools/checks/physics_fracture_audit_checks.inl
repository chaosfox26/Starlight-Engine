// Regression checks for the physics, fracture and destruction audit findings (audit 03).
extern "C" __declspec(dllimport) void slp_debug_fail_adds(int);
extern "C" __declspec(dllimport) int slp_present(void *, void *, int);
extern "C" __declspec(dllimport) void *slp_hull_create(const float *, int, float);
extern "C" __declspec(dllimport) void slp_hull_release(void *);
extern "C" __declspec(dllimport) uint32_t slp_add_hull(void *, void *, float, float, float, float, float, float, float);
extern "C" __declspec(dllimport) int slp_character_start(void *, float, float, float, float);
extern "C" __declspec(dllimport) int slp_character_step(void *, float, float, float, float, float, float, int, float *);
extern "C" __declspec(dllimport) int slp_body_motion(void *, uint32_t, float, float, float, float *);
template<class Require> void physics_fracture_audit_checks(Require require){
    {   // 03#1: fracture helpers run below normal priority, but the pool thread they borrowed must go back to normal afterwards.
        std::mutex lock;std::set<DWORD> helperThreads;
        rock_fracture_parallel_for(48,8,[&](size_t){
            if(GetThreadPriority(GetCurrentThread())==THREAD_PRIORITY_BELOW_NORMAL){std::lock_guard guard(lock);helperThreads.insert(GetCurrentThreadId());}
            std::this_thread::sleep_for(std::chrono::milliseconds(3));});
        require(!helperThreads.empty(),"03#1: Fracture helper threads run below normal priority while they work");
        bool leaked=false;
        for(int round=0;round<4;++round){
            std::vector<std::future<void>> probes;
            for(int i=0;i<16;++i)probes.push_back(std::async(std::launch::async,[&]{
                if(GetThreadPriority(GetCurrentThread())==THREAD_PRIORITY_BELOW_NORMAL){std::lock_guard guard(lock);if(helperThreads.contains(GetCurrentThreadId()))leaked=true;}
                std::this_thread::sleep_for(std::chrono::milliseconds(3));}));
            for(auto& probe:probes)probe.get();
        }
        require(!leaked,"03#1: A thread that helped with a fracture is back at normal priority for the next task it is given");
    }
    {   // 03#6: removing one body must not cancel the presentation interpolation of the bodies after it.
        struct State{uint32_t id;float x,y,z,qx,qy,qz,qw;};
        const float cube[24]={-.5f,-.5f,-.5f, .5f,-.5f,-.5f, .5f,.5f,-.5f, -.5f,.5f,-.5f, -.5f,-.5f,.5f, .5f,-.5f,.5f, .5f,.5f,.5f, -.5f,.5f,.5f};
        const uint32_t faces[36]={0,1,2,0,2,3, 4,6,5,4,7,6, 0,4,5,0,5,1, 1,5,6,1,6,2, 2,6,7,2,7,3, 3,7,4,3,4,0};
        const std::vector<float> flat(97*97,0.f);
        void* world=slp_create();int ready=0;
        if(world&&slp_terrain(world,flat.data())==1)for(int i=0;i<5000&&ready==0;++i){ready=slp_ready(world);if(ready==0)std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        require(world&&ready==1,"03#6: A physics world is ready for the presentation check");
        if(world&&ready==1){
            uint32_t ids[3];for(int i=0;i<3;++i){ids[i]=slp_add(world,cube,8,faces,36,1,1,float(i)*3,20,0,0,0,0,1);slp_body_velocity(world,ids[i],6,0,0,0,0,0);}
            State stepped[4]{};slp_step(world,1./60+1./120,nullptr,0,stepped,4); // one fixed tick, half a tick left over
            State before[4]{},after[4]{};
            const int countBefore=slp_present(world,before,4);
            slp_character_remove(world,ids[0]);
            const int countAfter=slp_present(world,after,4);
            require(countBefore==3&&countAfter==2&&after[0].id==ids[1]&&before[1].id==ids[1],"03#6: The presentation lists the bodies that remain");
            require(std::memcmp(&before[1],&after[0],sizeof(State))==0&&std::memcmp(&before[2],&after[1],sizeof(State))==0,"03#6: Bodies after a removed one keep their interpolated pose");
            slp_destroy(world);
        }
    }
    {   // 03#4: a stop request ends a fracture early instead of making the caller wait for the whole rock.
        Character rock;rock.Shape="boulder";auto source=shape_geometry(rock).front();
        auto plain=fracture_rock(source.mesh,source.color,false,117);
        std::stop_source idle;std::string unused;
        auto withToken=fracture_rock(source.mesh,source.color,false,117,&unused,idle.get_token());
        bool same=withToken.size()==plain.size();for(size_t i=0;same&&i<plain.size();++i)same&=(plain[i].center-withToken[i].center).length()<1e-12;
        require(!plain.empty()&&same,"03#4: A fracture whose stop token is never triggered gives exactly the same pieces");
        std::stop_source stopped;stopped.request_stop();std::string reason;
        auto cut=fracture_rock(source.mesh,source.color,false,117,&reason,stopped.get_token());
        require(cut.empty()&&reason=="Stopped","03#4: A fracture asked to stop returns no pieces and says it was stopped");
        auto object=compute_fracture(rock,rock,7,stopped.get_token());
        require(!object.supported&&object.pieces.empty(),"03#4: A stopped object fracture is reported as unsupported with no pieces, not completed another way");
    }
    {   // TEST-1: a stop requested while a fracture is running (not before the call) ends it early with "Stopped" instead of letting it run to completion.
        Character rock;rock.Shape="boulder";auto source=shape_geometry(rock).front();
        const auto began=std::chrono::steady_clock::now();auto whole=fracture_rock(source.mesh,source.color,false,117);
        const auto wholeTime=std::chrono::steady_clock::now()-began;
        std::stop_source stop;std::string reason;
        auto running=std::async(std::launch::async,[&]{return fracture_rock(source.mesh,source.color,false,117,&reason,stop.get_token());});
        std::this_thread::sleep_for(wholeTime/4);stop.request_stop();
        auto cut=running.get();
        require(!whole.empty()&&cut.empty()&&reason=="Stopped","TEST-1: A fracture stopped part-way through returns no pieces and says it was stopped, rather than finishing the whole rock");
    }
    {   // 03#3: a scene with very many breakables does not hold a prepared fracture for every one of them at once.
        auto scene=std::make_unique<SceneData>();scene->Game.emplace();scene->Game->SpawnZ=40;
        for(int i=0;i<300;++i){Character crate;crate.Shape="cube";crate.Id="held-crate-"+std::to_string(i);crate.X=-30+(i%20)*3.;crate.Z=-30+(i/20)*3.;crate.Width=.5;crate.Height=.5;crate.Depth=.5;crate.DestructionHealth=10;scene->Characters.push_back(crate);}
        auto player=std::make_unique<AdventurePlayer>();player->start(*scene);
        require(player->fracture_progress().total==256,"03#3: Break-ahead holds at most 256 prepared breakables at once, the nearest first");
        player->start(*scene);
    }
    {   // 03#2: a physics world that cannot take the rubble must not leave a break half done.
        auto scene=std::make_unique<SceneData>();scene->Game.emplace();scene->Game->SpawnZ=0;
        Character stone;stone.Shape="boulder";stone.Id="full-world-boulder";stone.Size=2;stone.Z=-3;stone.Color="#555D67";stone.DestructionHealth=10;
        GameplayRule broken;broken.Event="object-broken";broken.Subject=stone.Id;broken.Actions={{"complete-objective","full-world-broken"}};
        scene->Characters={stone};scene->Rules={broken};
        auto player=std::make_unique<AdventurePlayer>();player->start(*scene);player->move(0,0,1./120.); // the first step creates the physics world
        slp_debug_fail_adds(100000);bool threw=false;
        try{player->apply_attack_hits({stone.Id},1000);}catch(...){threw=true;}
        slp_debug_fail_adds(0);
        auto presented=player->presentation();
        require(!threw,"03#2: A break whose rubble cannot get physics bodies does not throw out of the simulation");
        require(player->scene().Characters.empty()&&player->rules().removed.contains(stone.Id)&&player->rules().objectives.contains("full-world-broken"),"03#2: Such a break still removes the object and fires the object-broken event once");
        require(presented.fragments.size()>=32,"03#2: Pieces that got no physics body are still shown and fall as plain particles");
    }
    {   // 03#7: a fragment rolling in the adventure world (advanced only by character steps) slows from rolling resistance when STARLIGHT_ROLLING_RESISTANCE=1, and rolls on as before when it is off.
        std::vector<float> points;
        for(int ring=1;ring<8;++ring){const double phi=3.14159265358979*ring/8;for(int k=0;k<16;++k){const double theta=6.28318530717959*k/16;points.insert(points.end(),{float(.25*std::sin(phi)*std::cos(theta)),float(.25*std::cos(phi)),float(.25*std::sin(phi)*std::sin(theta))});}}
        points.insert(points.end(),{0.f,.25f,0.f,0.f,-.25f,0.f});
        const std::vector<float> flat(97*97,0.f);
        auto spin_after_rolling=[&](const char* setting)->double{
            _putenv_s("STARLIGHT_ROLLING_RESISTANCE",setting);
            double result=-1;void* world=slp_create();int ready=0;
            if(world&&slp_terrain(world,flat.data())==1)for(int i=0;i<5000&&ready==0;++i){ready=slp_ready(world);if(ready==0)std::this_thread::sleep_for(std::chrono::milliseconds(2));}
            void* hull=slp_hull_create(points.data(),int(points.size()/3),1.f);
            if(world&&ready==1&&hull&&slp_character_start(world,.3f,-8,0,-8)==1){
                const uint32_t ball=slp_add_hull(world,hull,0,.26f,0,0,0,0,1);
                slp_body_velocity(world,ball,2,0,0,0,0,-8);
                float output[4]{};bool stepped=true;
                for(int i=0;i<90&&stepped;++i)stepped=slp_character_step(world,1.f/60,-8,0,-8,0,0,0,output)==1;
                float motion[6]{};
                if(stepped&&slp_body_motion(world,ball,0,.26f,0,motion)==1)result=std::sqrt(double(motion[3])*motion[3]+double(motion[4])*motion[4]+double(motion[5])*motion[5]);
            }
            if(hull)slp_hull_release(hull);
            if(world)slp_destroy(world);
            return result;
        };
        const double without=spin_after_rolling("0"),with=spin_after_rolling("1");
        _putenv_s("STARLIGHT_ROLLING_RESISTANCE","");
        require(without>0&&with>=0,"03#7: A rolling fragment can be simulated through character steps");
        require(without>0&&with<without*.9,"03#7: With STARLIGHT_ROLLING_RESISTANCE=1 a fragment rolling in the adventure world slows faster than without it");
    }
}
