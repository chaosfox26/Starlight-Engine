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
}
