#include "scene_preparation.hpp"
// Checks for the scene-preparation stage runner (audit CC-10): the worker is always joined, progress is not completion, and a display failure does not end the process.
static void scene_preparation_checks(const auto& require){
    {
        // The work finishes once the pump has served it five times after full progress (a handshake, not a wall-clock budget, so a starved pump thread cannot fail
        // the check); the limit only stops the work waiting forever when the runner no longer pumps.
        std::atomic<bool> returned{false};std::atomic<int> ticksAfterFull{0};int ticks=0;double last=-1;std::string failure;
        run_pumped_stage([&](std::atomic<double>& fraction){fraction=1.0;
                for(auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(10);ticksAfterFull.load()<5&&std::chrono::steady_clock::now()<limit;)std::this_thread::sleep_for(std::chrono::milliseconds(1));
                returned=true;},
            [&](double fraction){++ticks;last=fraction;if(fraction>=1.0)++ticksAfterFull;},failure);
        require(returned.load()&&failure.empty(),"CC-10: The stage runner returns only after the work has returned, even when the work reports full progress first");
        require(ticksAfterFull.load()>=5,"CC-10: The loading display keeps being served while work that already reported full progress is still finishing");
        require(last>=1.0&&ticks>=6,"CC-10: Progress reaches full only through the stage's own report");
    }
    {
        std::atomic<bool> returned{false};std::string failure;bool surfaced=false;
        try{run_pumped_stage([&](std::atomic<double>&){std::this_thread::sleep_for(std::chrono::milliseconds(150));returned=true;},
            [&](double){throw std::runtime_error("display failed");},failure);}
        catch(const std::runtime_error& e){surfaced=std::string(e.what())=="display failed";}
        require(surfaced,"CC-10: A failing loading display or observer surfaces as an error instead of terminating the process");
        require(returned.load(),"CC-10: The worker is joined (it ran to completion) before the display error leaves the stage runner");
    }
    {
        std::string failure;int ticks=0;
        run_pumped_stage([&](std::atomic<double>&){throw std::runtime_error("work failed");},[&](double){++ticks;},failure);
        require(failure=="work failed"&&ticks>=1,"CC-10: A failure inside the work is reported as the stage failure without throwing out of the runner");
        std::string unknown;
        run_pumped_stage([&](std::atomic<double>&){throw 7;},[&](double){},unknown);
        require(unknown=="unknown error","CC-10: A non-standard exception from the work is also reported, not lost");
    }
    require(prepare_limit_ms("90000")==90000&&prepare_limit_ms("2500.5")==2500.5&&prepare_limit_ms(" 1500 ")==1500,"04#6: A numeric preparation limit is used as given");
    require(prepare_limit_ms("abc")==60000&&prepare_limit_ms("60s")==60000&&prepare_limit_ms("")==60000&&prepare_limit_ms("nan")==60000&&prepare_limit_ms("inf")==60000,"04#6: An unparsable preparation limit keeps the 60 second default instead of becoming 1 ms");
    require(prepare_limit_ms("0")==60000&&prepare_limit_ms("-5")==60000&&prepare_limit_ms("0.5")==60000,"04#6: A preparation limit below 1 ms keeps the default");
    {
        // 04#5: once a stage has failed, the later stages do not run. A loading observer that throws fails the first stage; each later stage would call it again.
        auto scene=std::make_unique<SceneData>();scene->Game.emplace();scene->Game->SpawnZ=40;
        Character stone;stone.Shape="boulder";stone.Id="prep-stage-boulder";stone.Size=2;stone.Z=-3;stone.DestructionHealth=10;scene->Characters={stone};
        auto player=std::make_unique<AdventurePlayer>();player->start(*scene);
        int observed=0;loading_observer()=[&](double){++observed;throw std::runtime_error("observer failed");};
        LoadingScreen screen;PreparationReport report;
        try{report=prepare_scene(*player,screen);}catch(...){observed=-1;}
        loading_observer()=nullptr;
        require(report.state==PreparationReport::State::Failed&&report.reason.find("observer failed")!=std::string::npos,"04#5: A stage whose loading display fails ends the preparation as failed with that reason");
        require(observed==1,"04#5: The stages after a failed one do not run, so the failing display is not called again");
    }
    {
        // 04#6, as used by prepare_scene: a STARLIGHT_PREPARE_LIMIT_MS that is not a number leaves the 60 second default, so a breakable rock that needs more than a millisecond is waited for.
        auto scene=std::make_unique<SceneData>();scene->Game.emplace();scene->Game->SpawnZ=40;
        for(int i=0;i<2;++i){Character stone;stone.Shape="boulder";stone.Id="prep-limit-boulder-"+std::to_string(i);stone.Size=2;stone.X=i*6;stone.Z=-3;stone.DestructionHealth=10;scene->Characters.push_back(stone);}
        auto player=std::make_unique<AdventurePlayer>();player->start(*scene);
        SetEnvironmentVariableA("STARLIGHT_PREPARE_LIMIT_MS","abc");
        LoadingScreen screen;auto report=prepare_scene(*player,screen);
        SetEnvironmentVariableA("STARLIGHT_PREPARE_LIMIT_MS",nullptr);
        require(!report.stopped()&&report.breakablesQueued==2&&report.breakablesPrepared==2,"04#6: An unparsable STARLIGHT_PREPARE_LIMIT_MS does not cut breakable preparation short at 1 ms");
    }
    {
        // A close request that arrived before preparation started (the player pumps messages while it reads the scene) still cancels that preparation.
        auto scene=std::make_unique<SceneData>();scene->Game.emplace();scene->Game->SpawnZ=40;
        auto player=std::make_unique<AdventurePlayer>();player->start(*scene);
        preparation_cancel()=true;preparation_deferred_close().push_back(reinterpret_cast<HWND>(static_cast<uintptr_t>(0x5a5a)));
        LoadingScreen screen;auto report=prepare_scene(*player,screen);
        preparation_deferred_close().clear();preparation_cancel()=false;
        require(report.cancelled&&report.stopped(),"A close request that arrived before preparation started cancels the preparation instead of being dropped");
    }
}

