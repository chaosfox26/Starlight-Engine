// Editor wave-2 audit findings exercised on the real editor, on an isolated fixture scene (included inside ui_checks, where `check` exists).
{
    auto fixture = [&] {
        scene = SceneData{};
        selected = -1; selectionIds.clear(); history.clear(); dirty = false;
        for (int i = 0; i < 4; ++i) {
            Character c; c.Shape = "rock"; c.Name = std::string(1, char('A' + i)); c.X = i * 3.;
            scene.Characters.push_back(c);
        }
        refresh();
    };
    auto names = [&] { std::string all; for (const auto &c : scene.Characters) all += c.Name; return all; };
    // EDIT-2: the multi-selection and the single active object stay in step.
    fixture();
    selectionIds = {scene.Characters[0].Id, scene.Characters[1].Id, scene.Characters[2].Id}; selected = 0;
    select_hit(1);
    check("EDIT-2: clicking an object inside the multi-selection keeps the multi-selection", selected == 1 && selectionIds.size() == 3);
    select_hit(3);
    check("EDIT-2: clicking an object outside the multi-selection makes it the only selection", selected == 3 && selectionIds.size() == 1 && selectionIds.contains(scene.Characters[3].Id));
    selectionIds = {scene.Characters[0].Id, scene.Characters[1].Id, scene.Characters[2].Id}; selected = 0;
    select_hit(3);
    command(Delete);
    check("EDIT-2: Delete after clicking one object removes only that object", names() == "ABC");
    selectionIds = {scene.Characters[0].Id, scene.Characters[1].Id}; selected = 0;
    command(Undo);
    check("EDIT-2: Undo leaves no stale multi-selection", selected == -1 && selectionIds.empty() && names() == "ABCD");
    selectionIds = {scene.Characters[0].Id}; selected = 0;
    command(Redo);
    check("EDIT-2: Redo leaves no stale multi-selection", selected == -1 && selectionIds.empty());
    fixture();
    auto listNotify = [&] { SendMessage(window, WM_COMMAND, MAKEWPARAM(ObjectList, LBN_SELCHANGE), reinterpret_cast<LPARAM>(list)); };
    SendMessage(list, LB_SETSEL, TRUE, 0); SendMessage(list, LB_SETSEL, TRUE, 1); SendMessage(list, LB_SETCARETINDEX, 1, FALSE);
    listNotify();
    check("EDIT-2: the object list selects the clicked row and every selected row", selected == 1 && selectionIds.size() == 2);
    SendMessage(list, LB_SETSEL, FALSE, 1);
    listNotify();
    check("EDIT-2: deselecting the caret row makes a selected row the active object", selected == 0 && selectionIds.size() == 1 && selectionIds.contains(scene.Characters[0].Id));
    SendMessage(list, LB_SETSEL, FALSE, 0);
    listNotify();
    check("EDIT-2: deselecting the last row leaves no active object", selected == -1 && selectionIds.empty());
    fixture();
    // EDIT-4: a double-click on the 3D view is two presses, not a command.
    currentMode = "Select"; last = {0, 0};
    SendMessage(view, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(7, 9));
    ReleaseCapture();
    check("EDIT-4: the second click of a double-click on the 3D view is handled as a press", last.x == 7 && last.y == 9);
    playing = true;
    bool doubleClickThrew = false;
    try { message(WM_COMMAND, MAKEWPARAM(0, STN_DBLCLK), reinterpret_cast<LPARAM>(view)); } catch (const std::exception&) { doubleClickThrew = true; }
    playing = false;
    check("EDIT-4: the 3D view's double-click notification is not run as an editing command while playing", !doubleClickThrew);
    // EDIT-5: Paste drops joint and lightning links to objects that are not in the scene it pastes into.
    fixture();
    {
        Character joined; joined.Shape = "rock"; joined.Name = "joined"; joined.Dynamic = true; joined.JointKind = 1; joined.JointTargetId = "object-from-another-scene";
        Character kept; kept.Shape = "rock"; kept.Name = "kept"; kept.Dynamic = true; kept.JointKind = 1; kept.JointTargetId = scene.Characters[0].Id;
        copiedObjects = {joined, kept};
        command(PasteObjects);
        const auto &pastedJoined = scene.Characters[scene.Characters.size() - 2], &pastedKept = scene.Characters.back();
        check("EDIT-5: pasting drops a joint to an object that is not in this scene", pastedJoined.Name == "joined" && pastedJoined.JointKind == 0 && pastedJoined.JointTargetId.empty());
        check("EDIT-5: pasting keeps a joint to an object that is in this scene", pastedKept.Name == "kept" && pastedKept.JointKind == 1 && pastedKept.JointTargetId == scene.Characters[0].Id);
        copiedObjects.clear();
    }
    // EDIT-6: a Route stroke snaps the selected object by default; with STARLIGHT_SNAP_ONLY_MOVED=1 it leaves an object it did not move alone.
    {
        auto routeStroke = [&](const char *only) {
            fixture();
            _putenv_s("STARLIGHT_SNAP_ONLY_MOVED", only);
            const bool oldGrid = gridSnap; gridSnap = true;
            scene.Characters[0].X = .3; selected = 0; selectionIds.clear(); currentMode = "Route";
            history.begin(scene); begin_group(); stroke = true;
            end_stroke();
            gridSnap = oldGrid; _putenv_s("STARLIGHT_SNAP_ONLY_MOVED", "");
            return scene.Characters[0].X;
        };
        check("EDIT-6: by default a Route stroke still snaps the selected object to the grid", routeStroke("") == .5);
        check("EDIT-6: with STARLIGHT_SNAP_ONLY_MOVED a Route stroke leaves the selected object where it was", routeStroke("1") == .3);
        currentMode = "Select";
    }
    // EDIT-7: per-scene state (creation lesson, armed pour) does not follow a different scene.
    fixture();
    creationGuide.active = true; pendingPour = true;
    reset_document_state();
    check("EDIT-7: replacing the scene ends the creation lesson and disarms a pour", !creationGuide.active && !pendingPour);
    pendingPour = true; creationGuide.active = true;
    command(New);
    check("EDIT-7: New also disarms a pour", !pendingPour && !creationGuide.active);
    // EDIT-8: a second mouse button pressed during a gesture is ignored.
    {
        fixture();
        currentMode = "Terrain";
        history.begin(scene); stroke = true;
        bool secondPressThrew = false;
        try { pointer_down(5, 5, WM_RBUTTONDOWN); } catch (const std::exception&) { secondPressThrew = true; }
        check("EDIT-8: pressing the right button during a left-button brush stroke does not start a second edit", !secondPressThrew && stroke);
        end_stroke();
        currentMode = "Select"; orbit = true;
        pointer_down(5, 5, WM_RBUTTONDOWN);
        check("EDIT-8: pressing the right button during a middle-button orbit does not start a box selection", !boxSelecting && orbit);
        end_stroke();
        check("EDIT-8: releasing the buttons ends the orbit", !orbit && !boxSelecting && !stroke);
        currentMode = "Select";
    }
    fixture();
}
