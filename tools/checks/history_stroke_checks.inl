// Undo during an open edit cancels that edit and nothing more (audit: Ctrl+Z during a drag undid two steps).
#include "source_text.hpp"
static void history_stroke_checks(const auto& require){
    SceneData scene;scene.ShadowQuality=1;
    SceneHistory history;
    history.begin(scene);scene.ShadowQuality=2;history.commit();
    history.begin(scene);scene.ShadowQuality=3;
    require(history.undo(scene)&&scene.ShadowQuality==2,"Undo during an open edit cancels only that edit");
    history.commit();
    require(history.undo(scene)&&scene.ShadowQuality==1,"The committed edit before it is still one undo step away");
    require(!history.undo(scene),"There is nothing further to undo");
    // The editor window ends a drag or brush stroke itself before Undo or Redo reaches the history; this program cannot press Ctrl+Z in it, so the call must still be there.
    require(source_span("editor/cpp/editor_main.cpp","id == Undo || id == Redo","history.undo(scene)").find("end_stroke();")!=std::string::npos,"Undo and Redo in the editor end the open stroke before they reach the history");
}
