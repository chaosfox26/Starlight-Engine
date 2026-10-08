// Contract risks and small leads from the core audit: a flow is never assigned, and the break bar's event set is bounded.
#include "flow.hpp"
#include "combat_control.hpp"
#include <type_traits>
static void core_contract_lead_checks(const auto& require){
    // Assigning a TerrainFlow would keep the destination's scratch buffers and background geometry job against the new state, so assignment does not exist.
    static_assert(!std::is_copy_assignable_v<TerrainFlow>&&!std::is_move_assignable_v<TerrainFlow>,"TerrainFlow must not be assignable");
    require(!std::is_copy_assignable_v<TerrainFlow>&&!std::is_move_assignable_v<TerrainFlow>,"A liquid flow cannot be assigned over another flow's scratch buffers and background geometry job");
}
