// Contract risks and small leads from the core audit: a flow is never assigned, and the break bar's event set is bounded.
#include "flow.hpp"
#include "combat_control.hpp"
#include <type_traits>
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
}
