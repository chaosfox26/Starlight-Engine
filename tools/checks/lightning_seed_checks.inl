// Seed 0 is a fixed point of the xorshift generator (audit EL-21): every bolt made with it was the same straight, unjittered line.
#include "../../deep/engine/chaos_lightning.h"
static void lightning_seed_checks(const auto& require){
    const starlight::ArcPoint target{4,0,1};
    const auto withDefault=starlight::Lightning(target,1.f);
    const auto withZero=starlight::Lightning(target,1.f,0);
    auto same=[](const std::vector<starlight::ArcVertex>& a,const std::vector<starlight::ArcVertex>& b){return a.size()==b.size()&&std::memcmp(a.data(),b.data(),a.size()*sizeof(starlight::ArcVertex))==0;};
    require(!withZero.empty()&&same(withZero,withDefault),"EL-21: Seed 0 gives the same bolt as the default seed instead of a degenerate straight line");
    require(!same(starlight::Lightning(target,1.f,1),withDefault)&&!same(starlight::Lightning(target,1.f,2),starlight::Lightning(target,1.f,1)),"EL-21: Different non-zero seeds still give different bolts");
}
