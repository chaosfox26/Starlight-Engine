// A top-level acceleration structure may be refitted only when every instance slot still names the geometry key it was built with (a reordered or replaced instance list must rebuild).
#include "../../deep/engine/rt_refit.h"
static void rt_refit_checks(const auto& require){
    struct Item {uint64_t key;};
    const std::vector<Item> built={{1},{2},{3}};
    require(same_instance_keys(built,std::vector<Item>{{1},{2},{3}}),"rt refit accepts an unchanged instance list");
    require(!same_instance_keys(built,std::vector<Item>{{2},{1},{3}}),"rt refit rejects reordered instances with the same keys");
    require(!same_instance_keys(built,std::vector<Item>{{1},{2}}),"rt refit rejects a changed instance count");
    require(!same_instance_keys(built,std::vector<Item>{{1},{2},{4}}),"rt refit rejects a replaced geometry key");
    require(same_instance_keys(std::vector<Item>{},std::vector<Item>{}),"rt refit accepts two empty instance lists");
}
