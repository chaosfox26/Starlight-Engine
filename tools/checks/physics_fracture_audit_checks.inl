// Regression checks for the physics, fracture and destruction audit findings (audit 03).
extern "C" __declspec(dllimport) void slp_debug_fail_adds(int);
template<class Require> void physics_fracture_audit_checks(Require require){
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
}
