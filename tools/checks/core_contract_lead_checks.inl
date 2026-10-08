// Contract risks and small leads from the core audit: a flow is never assigned, and the break bar's event set is bounded.
#include "flow.hpp"
#include "combat_control.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <type_traits>
#include <unordered_set>
#include "adventure.hpp"
static void core_contract_lead_checks(const auto& require){
    // Assigning a TerrainFlow would keep the destination's scratch buffers and background geometry job against the new state, so assignment does not exist.
    static_assert(!std::is_copy_assignable_v<TerrainFlow>&&!std::is_move_assignable_v<TerrainFlow>,"TerrainFlow must not be assignable");
    require(!std::is_copy_assignable_v<TerrainFlow>&&!std::is_move_assignable_v<TerrainFlow>,"A liquid flow cannot be assigned over another flow's scratch buffers and background geometry job");
    {
        // A boss that is broken again and again for a whole session must not keep every event id it ever saw; a repeat delivery inside one Ready period is still refused.
        BossBreakBar bar;size_t largest=0;bool counted=true,repeatRefused=true,cycled=true;
        for(uint64_t cycle=0;cycle<300;++cycle){
            for(uint64_t i=1;bar.phase==BossBreakBar::Phase::Ready;++i){
                const uint64_t event=cycle*1000+i;
                counted&=bar.apply(ControlEffect::Slow,event);
                repeatRefused&=!bar.apply(ControlEffect::Slow,event);
                largest=std::max(largest,bar.resolvedEvents.size());
            }
            cycled&=bar.phase==BossBreakBar::Phase::Staggered;
            bar.advance(8);bar.advance(90);
            cycled&=bar.phase==BossBreakBar::Phase::Ready&&bar.strength==bar.capacity;
        }
        require(counted&&repeatRefused&&cycled,"Boss break events still count once each and a repeat delivery is refused while the bar is Ready");
        require(largest<=14&&bar.resolvedEvents.empty(),"The boss break bar's resolved-event set stops growing across repeated break cycles");
    }
#ifdef STARLIGHT_CHARACTER_CONTROLLER
    {
        // The frame loops of the editor and the player call take_prefractured_parts every frame and hand a non-empty answer to the renderer's incremental preload, so a fracture that
        // finishes after the loading screen's one bulk preload still reaches the GPU before its object is hit. That relies on this call reporting later completions.
        SceneData scene;scene.Game.emplace();scene.Game->SpawnX=0;scene.Game->SpawnY=1.7;scene.Game->SpawnZ=12;scene.Game->Collision=true;
        for(int i=0;i<2;++i){Character rock;rock.Id=i?"lead-rock-b":"lead-rock-a";rock.Name=rock.Id;rock.Shape="boulder";rock.X=-3+6*i;rock.Z=0;rock.Y=.7;rock.Size=1.2;rock.KeepAboveFloor=false;rock.Dynamic=false;scene.Characters.push_back(rock);}
        AdventurePlayer player;player.start(scene);
        std::atomic<double> fraction{0};player.prepare_geometry(fraction);player.prepare_collision(fraction);
        std::unordered_set<const MeshPart*> delivered;std::vector<std::shared_ptr<const MeshPart>> batch;
        size_t reports=0;AdventurePlayer::FractureProgress progress;
        for(int poll=0;poll<12000;++poll){
            if(player.take_prefractured_parts(batch)){++reports;for(const auto& part:batch)delivered.insert(part.get());}
            progress=player.fracture_progress();if(progress.settled())break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if(player.take_prefractured_parts(batch)){++reports;for(const auto& part:batch)delivered.insert(part.get());}
        require(progress.settled()&&progress.prepared==2,"Both break-ahead results finish");
        require(reports>0&&delivered.size()>=2,"Break-ahead pieces that finish while the game is running are reported for preloading");
        require(!player.take_prefractured_parts(batch),"Nothing is reported again until another break-ahead result changes");
    }
#endif
}
