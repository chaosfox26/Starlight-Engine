// The asset browser scan can be cancelled (audit ED-12): a std::future from std::async waits for its task when it is destroyed, so a scan that cannot be
// stopped holds up closing the editor for as long as it runs.
#include "asset_library.hpp"
static void asset_scan_checks(const auto& require,const fs::path& folder){
    const fs::path dir=folder/"asset-scan-checks";fs::create_directories(dir);
    for(int i=0;i<400;++i){std::ofstream out(dir/("model-"+std::to_string(i)+".glb"),std::ios::binary);out<<"x";}
    const auto full=starlight::editor::scan_asset_library(dir);
    require(full.entries.size()==400&&!full.partial,"ED-12: An uncancelled scan finds every model in the folder");
    std::atomic<bool> stop{true};
    const auto cancelled=starlight::editor::scan_asset_library(dir,&stop);
    require(cancelled.partial&&cancelled.entries.size()<full.entries.size(),"ED-12: A scan that is already cancelled stops at once and reports a partial result");
    std::atomic<bool> go{false};
    const auto notCancelled=starlight::editor::scan_asset_library(dir,&go);
    require(notCancelled.entries.size()==400&&!notCancelled.partial,"ED-12: A cancel flag that is not set changes nothing");
}
