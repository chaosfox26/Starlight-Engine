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
    // EDIT-5 on the reusable-object path: a single-object file cannot name a joint or lightning target that is in its scene, so it loads without those links; a group keeps its own.
    {
        Character anchor;anchor.Id="kc-link-anchor";anchor.Shape="cube";
        Character pendulum;pendulum.Id="kc-link-pendulum";pendulum.Shape="cube";pendulum.Dynamic=true;pendulum.JointKind=2;pendulum.JointTargetId=anchor.Id;
        const auto group=decode_object_group(encode_object_group({pendulum,anchor}));
        require(group.size()==2&&group[0].JointKind==2&&group[0].JointTargetId==group[1].Id,"EDIT-5: a reusable group keeps a joint between its own members");
        auto single=json::parse(encode_object_template(anchor));
        single["Object"]["Dynamic"]=true;single["Object"]["JointKind"]=2;single["Object"]["JointTargetId"]="kc-gone";single["Object"]["LightningTargetId"]="kc-gone";
        const auto loaded=decode_object_template(single.dump());
        require(loaded.JointKind==0&&loaded.JointTargetId.empty()&&loaded.LightningTargetId.empty(),"EDIT-5: a single reusable object does not keep joint or lightning links that name nothing in its file");
    }
}
