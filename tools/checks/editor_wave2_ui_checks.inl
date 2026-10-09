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
}
