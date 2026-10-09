// Copies get fresh identities, and links between copied objects follow the copies (audit CORE-01).
#include "../../editor/cpp/object_template.hpp"
#include "source_text.hpp"
static void id_remap_checks(const auto& require){
    auto linked=[&](){
        std::vector<Character> group(3);
        group[0].Id="a";group[1].Id="b";group[2].Id="c";
        group[0].JointTargetId="b";group[1].LightningTargetId="c";group[2].JointTargetId="outside";
        return group;
    };
    auto group=linked();
    renew_object_ids(group);
    require(group[0].Id!="a"&&group[1].Id!="b"&&group[2].Id!="c"&&group[0].Id!=group[1].Id,"CORE-01: copied objects get fresh identities");
    require(group[0].JointTargetId==group[1].Id,"CORE-01: a copied joint follows the copy of its target, not the original");
    require(group[1].LightningTargetId==group[2].Id,"CORE-01: a copied lightning emitter follows the copy of its target");
    require(group[2].JointTargetId=="outside","CORE-01: a link to an object outside the copied set is kept as it was");
    auto twice=linked();twice[1].Id="a";
    bool rejected=false;
    try{renew_object_ids(twice);}catch(const std::runtime_error&){rejected=true;}
    require(rejected,"CORE-01: duplicate source identities are rejected instead of silently overwriting a link");
    // The paste, duplicate, group placement and asset-browser placement paths run inside the editor window, which this program cannot drive: each must still renew ids through the shared function.
    require(source_occurrences("editor/cpp/editor_main.cpp","renew_object_ids(")>=2&&source_occurrences("editor/cpp/selection_clipboard.inl","renew_object_ids(")>=1&&source_occurrences("editor/cpp/asset_browser.inl","renew_object_ids(")>=1,"CORE-01: paste, duplicate, group and asset-browser placement renew ids through renew_object_ids");
    require(source_occurrences("editor/cpp/editor_main.cpp","Id = new_id()")+source_occurrences("editor/cpp/selection_clipboard.inl","Id = new_id()")+source_occurrences("editor/cpp/asset_browser.inl","Id = new_id()")==0,"CORE-01: no copy path hands out ids one by one, which would drop the links between the copies");
}
