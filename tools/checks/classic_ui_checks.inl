// Runs the real form/command code against an isolated, newly created editor scene.
// No user's window or scene is inspected or modified by this regression harness.
int ui_checks(const fs::path& output) {
    AuditWindowTrace windowTrace;
    AuditInputRuntime inputRuntime;
    // Visibility-dependent checks must not inherit a shell's hidden-start flag.
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    KillTimer(window, 1);
    EnableWindow(window, FALSE); // An isolated automated test, not an interactive project.
    nlohmann::json report = {{"checks", nlohmann::json::array()}};
    const auto auditStarted = GetTickCount64();
    size_t progressWriteFailures = 0;
    auto writeProgress = [&](const fs::path& path, const nlohmann::json& value) {
        try { atomic_write(path, value.dump(2)); }
        catch (const std::exception& e) {
            ++progressWriteFailures;
            report["progress_write_failures"] = progressWriteFailures;
            report["last_progress_write_error"] = e.what();
        }
    };
    auto progress = [&](const char* stage, int current, int total) {
        std::string message = std::string("Starlight editor checks — ") + stage + " " + std::to_string(current) + "/" + std::to_string(total);
        SetWindowTextW(window, wide(message).c_str());
        SetWindowTextW(status, wide(message).c_str());
        writeProgress(output.string() + ".status.json", nlohmann::json({{"state","running"},{"stage",stage},{"current",current},{"total",total},{"elapsed_ms",GetTickCount64()-auditStarted}}));
    };
    int failures = 0;
    auto check = [&](const char* name, bool okay) {
        report["checks"].push_back({{"name", name}, {"pass", okay}});
        if (!okay) ++failures;
        progress("functional checks", int(report["checks"].size()), 0);
        writeProgress(output.string()+".progress.json",report);
    };
    if(GetEnvironmentVariableW(L"STARLIGHT_UI_WAVE2_ONLY",nullptr,0)) {
        report["scope"]="editor wave-2 audit checks on an isolated fixture scene";
        #include "editor_wave2_ui_checks.inl"
        check("isolated Windows input runtime shuts down cleanly",inputRuntime.finish());
        report["pass"]=failures==0;atomic_write(output,report.dump(2));
        dirty=false;SendMessageW(window,WM_CLOSE,0,0);return failures ? 1 : 0;
    }
    if(GetEnvironmentVariableW(L"STARLIGHT_UI_RULES_ONLY",nullptr,0)) {
        report["scope"]="focused gameplay-rule authoring, save, Undo and Play checks";
        #include "gameplay_rule_ui_checks.inl"
        check("isolated Windows input runtime shuts down cleanly",inputRuntime.finish());
        report["pass"]=failures==0;atomic_write(output,report.dump(2));
        dirty=false;SendMessageW(window,WM_CLOSE,0,0);return failures ? 1 : 0;
    }
    RECT initial; GetClientRect(view, &initial);
    {
        auto library = output.parent_path() / ("ui-library-" + new_id());
        Character stone; stone.Shape = "rock"; stone.Name = "Browser stone";
        save_object_template(stone, library / "stone.starobject");
        auto before = scene.Characters.size();
        libraryFolder = library;
        command(BrowseAssets);
        libraryIndex = libraryTask.valid() ? libraryTask.get() : scan_asset_library(library); filter_asset_browser();
        check("asset browser lists reusable object and enables insertion", IsWindowVisible(libraryPanel) &&
            SendMessage(libraryList, LB_GETCOUNT, 0, 0) == 1 && IsWindowEnabled(GetDlgItem(libraryPanel, InsertAsset)));
        command(InsertAsset);
        check("asset browser inserts independent object through real editor command", scene.Characters.size() == before + 1 && actor() && actor()->Name == stone.Name && actor()->Id != stone.Id);
        command(Undo);
        check("asset browser insertion supports one Undo", scene.Characters.size() == before);
        SetWindowTextW(librarySearch, L"no matching asset");
        check("asset browser search clears results and disables insertion", SendMessage(libraryList, LB_GETCOUNT, 0, 0) == 0 && !IsWindowEnabled(GetDlgItem(libraryPanel, InsertAsset)));
        command(CloseAssets); SetWindowTextW(librarySearch, L""); history.clear(); dirty = false;
        SendMessage(GetDlgItem(libraryPanel, AssetType), CB_SETCURSEL, 1, 0); command(AssetType);
        check("asset browser type selector excludes saved objects from models", libraryMatches.empty());
        SendMessage(GetDlgItem(libraryPanel, AssetType), CB_SETCURSEL, 2, 0); command(AssetType);
        check("asset browser type selector restores saved object matches", libraryMatches.size() == 1);
        SendMessage(GetDlgItem(libraryPanel, AssetType), CB_SETCURSEL, 0, 0); command(AssetType);
    }
    {
        const auto originalScene = scene;
        Character a, b; a.Shape = b.Shape = "cube";
        a.X = 1; a.Route = {{2,1}}; b.X = 3; b.Route = {{4,2}};
        scene.Characters = {a,b}; selected = 0; selectionIds = {a.Id,b.Id}; history.clear(); refresh();
        command(ViewFocus);
        auto firstBounds = object_bounds(a), secondBounds = object_bounds(b);
        check("Focus frames both selected objects around their combined bounds", std::abs(focusX -
            (std::min(firstBounds.low.x, secondBounds.low.x) + std::max(firstBounds.high.x, secondBounds.high.x)) / 2) < .000001);
        command(Duplicate);
        check("Duplicate operates on the full selection and translates both routes", scene.Characters.size() == 4 &&
            scene.Characters[2].X == 1.5 && scene.Characters[3].X == 3.5 &&
            scene.Characters[2].Route[0].X == 2.5 && scene.Characters[3].Route[0].X == 4.5 && selectionIds.size() == 2);
        command(Undo);
        check("One Undo removes all duplicated objects", scene.Characters.size() == 2);
        command(Redo);
        check("One Redo restores all duplicated objects", scene.Characters.size() == 4);
        selected = 2; selectionIds = {scene.Characters[2].Id, scene.Characters[3].Id};
        command(Delete);
        check("Delete removes the full selected set and preserves originals", scene.Characters.size() == 2 &&
            scene.Characters[0].Id == a.Id && scene.Characters[1].Id == b.Id);
        command(Undo);
        check("One Undo restores the full deleted selection", scene.Characters.size() == 4);
        selected = 0; selectionIds = {a.Id,b.Id};
        command(CopyObjects);
        scene.Characters.clear(); selected = -1; selectionIds.clear(); history.clear(); refresh();
        command(PasteObjects);
        check("Copy and Paste retain a group across scene replacement with fresh identities and routes", scene.Characters.size() == 2 &&
            scene.Characters[0].Id != a.Id && scene.Characters[1].Id != b.Id &&
            scene.Characters[1].X - scene.Characters[0].X == 2 &&
            scene.Characters[0].Route[0].X - scene.Characters[0].X == 1 && selectionIds.size() == 2);
        command(Undo);
        check("One Undo removes the complete pasted group", scene.Characters.empty());
        command(Redo);
        check("One Redo restores the complete pasted group", scene.Characters.size() == 2);
        copiedObjects.front().Shape.reset(); copiedObjects.front().Sculpt.reset();
        copiedObjects.front().Asset = "model-" + std::string(64, 'e');
        const auto beforeInvalidPaste = scene.Characters.size(); bool rejectedPaste = false;
        try { command(PasteObjects); } catch (const std::exception&) { rejectedPaste = true; }
        check("Paste rejects missing models without inserting part of the group", rejectedPaste && scene.Characters.size() == beforeInvalidPaste);
        copiedObjects.clear();
        scene = originalScene; selected = -1; selectionIds.clear(); history.clear(); dirty = false; refresh();
    }
    yaw = 1; pitch = .1; focusX = 3; focusY = 2; focusZ = 4;
    command(Frame);
    check("Frame world restores reference orientation and distance", yaw == 0 && pitch == .67 && distance == 19 && focusX == 0 && focusY == 0 && focusZ == 0);
    auto foregroundBeforeFeedback = GetForegroundWindow();
    edgeFeedback.update(window, view, -1, 1);
    check("edge feedback shades only the active left and top edges", edgeFeedback.visible_edges() == 5);
    edgeFeedback.update(window, view, 1, -1);
    check("edge feedback switches to right and bottom without taking focus", edgeFeedback.visible_edges() == 10 && GetForegroundWindow() == foregroundBeforeFeedback);
    edgeFeedback.hide();
    check("edge feedback clears when navigation stops", edgeFeedback.visible_edges() == 0);
    command(ViewLow);
    check("Low view keeps the camera above an empty floor", camera_state().eye.y >= .199999 && pitch < 0);
    command(Frame);
    toggle_menu(MenuFile);
    check("reference main menu exposes atmosphere, ray tracing and camera together",
          mainInputs.contains("fog") && mainInputs.contains("tier") && mainInputs.contains("pan"));
    const auto originalFog = scene.FogDensity;
    command(ApplyAtmosphere);
    check("main-menu atmosphere preserves millesimal fog precision", std::abs(scene.FogDensity - originalFog) < .00051);
    SendMessage(mainInputs.at("pan"), TBM_SETPOS, TRUE, 175);
    SendMessage(window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(mainInputs.at("pan")));
    check("main-menu camera speed updates without applying scene settings", panSpeed == 1.75);
    panSpeed = 1;
    scroll_main(INT_MAX);
    RECT lastControl, menuBounds;
    GetWindowRect(mainInputs.at("arrows"), &lastControl);
    GetWindowRect(menuPanel, &menuBounds);
    check("main-menu scrolling reaches final camera control", lastControl.top >= menuBounds.top && lastControl.bottom <= menuBounds.bottom);
    scroll_main(0);
    const double cameraBeforeWheel = distance;
    SendMessage(view, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
                MAKELPARAM(menuBounds.left + 25, menuBounds.top + 25));
    check("wheel over main menu scrolls controls without zooming scene", mainScroll > 0 && distance == cameraBeforeWheel);
    toggle_menu(0);
    toggle_menu(MenuWorld);
    command(2005);
    check("environment adds light and keeps its editing controls open", openMenu == MenuWorld && actor() && actor()->Shape == "light" && mainInputs.contains("cone"));
    SetWindowTextW(mainInputs.at("intensity"), L"42");
    SetWindowTextW(mainInputs.at("range"), L"7");
    SetWindowTextW(mainInputs.at("cone"), L"90");
    command(ApplySelectedEffect);
    check("environment Apply updates selected light", actor()->LightIntensity == 42 && actor()->LightRange == 7 && actor()->LightCone == 90);
    SetWindowTextW(mainInputs.at("range"), L"-1");
    bool invalidEffectRejected = false;
    try { command(ApplySelectedEffect); } catch (const std::exception&) { invalidEffectRejected = true; }
    check("invalid effect input preserves previous scene values", invalidEffectRejected && actor()->LightRange == 7);
    toggle_menu(0);
    command(Undo); command(Undo);
    command(BuildShapes); command(2000); command(ObjectProperties);
    check("beginner hides transforms, physics and irrelevant lightning", inputs.contains("size") && !inputs.contains("x") && !inputs.contains("dynamic") && !inputs.contains("tx"));
    const auto liveActorId = actor()->Id;
    const double originalLiveSize = actor()->Size;
    auto liveSize = inputs.at("size");
    SendMessage(liveSize, TBM_SETPOS, TRUE, 170);
    SendMessage(window, WM_HSCROLL, TB_THUMBTRACK, reinterpret_cast<LPARAM>(liveSize));
    check("size slider updates the scene immediately without replacing its control", actor()->Size == 1.7 && inputs.at("size") == liveSize && IsWindow(liveSize));
    command(Undo);
    // Undo deliberately clears selection; locate the restored object before inspecting it.
    for (size_t i = 0; i < scene.Characters.size(); ++i)
        if (scene.Characters[i].Id == liveActorId) selected = int(i);
    check("live size edit is undoable", actor() && actor()->Size == originalLiveSize);
    inspector_fields();
    auto liveSpeed = inputs.at("speed");
    SendMessage(liveSpeed, TBM_SETPOS, TRUE, 230);
    SendMessage(window, WM_HSCROLL, TB_THUMBTRACK, reinterpret_cast<LPARAM>(liveSpeed));
    check("walking speed slider updates without Apply", actor()->Speed == 2.3 && inputs.at("speed") == liveSpeed);
    SetWindowTextW(inputs.at("name"), L"  Live name  ");
    SendMessage(window, WM_COMMAND, MAKEWPARAM(0, EN_KILLFOCUS), reinterpret_cast<LPARAM>(inputs.at("name")));
    check("character name commits and trims on focus loss", actor()->Name == "Live name");
    SetWindowTextW(inputs.at("name"), L"   ");
    SendMessage(window, WM_COMMAND, MAKEWPARAM(0, EN_KILLFOCUS), reinterpret_cast<LPARAM>(inputs.at("name")));
    check("blank name does not replace the character name", actor()->Name == "Live name");
    actor()->TargetX = 4.5; actor()->Density = 950;
    SetWindowTextW(inputs.at("name"), L"Classic control test");
    SendMessage(inputs.at("size"), TBM_SETPOS, TRUE, 150);
    command(Apply);
    check("beginner edits preserve hidden properties", actor()->Name == "Classic control test" && actor()->Size == 1.5 && actor()->TargetX == 4.5 && actor()->Density == 950);
    command(BodyDetails);
    check("rigid-body controls remain available without advanced exact values", !advanced && inputs.contains("dynamic") && inputs.contains("density") && !inputs.contains("x"));
    command(BodyDetails);
    command(Advanced); command(BuildDetails); command(BodyDetails);
    check("advanced build/body expand independently", inputs.contains("x") && inputs.contains("width") && inputs.contains("density") && !inputs.contains("tx"));
    SetWindowTextW(inputs.at("x"), L"invalid unrelated field");
    SetWindowTextW(inputs.at("density"), L"1200");
    command(ApplyBody);
    check("body Apply ignores unrelated exact-value input", actor()->Density == 1200);
    const auto beforeBodyFailure = actor()->Density;
    SetWindowTextW(inputs.at("density"), L"-1");
    bool invalidBodyRejected = false;
    try { command(ApplyBody); } catch (const std::exception&) { invalidBodyRejected = true; }
    check("invalid body settings leave the object unchanged", invalidBodyRejected && actor()->Density == beforeBodyFailure);
    SetWindowTextW(inputs.at("y"), L"-5");
    SendMessage(inputs.at("floor"), BM_SETCHECK, BST_CHECKED, 0);
    command(ApplyBuild);
    check("build Apply clamps to the floor and ignores unrelated body input", actor()->Density == beforeBodyFailure && object_bounds(*actor()).low.y >= -.000001);
    const double oldExactX = actor()->X;
    actor()->Route = {{oldExactX, actor()->Z}};
    SetWindowTextW(inputs.at("x"), wide(std::to_string(oldExactX + 1)).c_str());
    SetWindowTextW(inputs.at("density"), L"invalid unrelated field");
    command(ApplyExact);
    check("exact Apply translates the route without applying body fields", actor()->X == oldExactX + 1 && actor()->Route[0].X == actor()->X && actor()->Density == beforeBodyFailure);
    command(BuildTerrain); command(DockSettings);
    check("terrain gear opens its own form", inputs.contains("paint") && !inputs.contains("pan") && !inputs.contains("gridSnap"));
    SendMessage(inputs.at("paint"), CB_SETCURSEL, 3, 0); command(Apply);
    command(MenuCamera); command(BuildTerrain); command(DockSettings);
    check("paint choice survives menu changes", integer("paint") == 3 && paintMaterial == 3);
    command(PourLava);
    check("pour is armed without camera-centered placement", pendingPour == true && scene.LiquidSources.empty());
    RECT bounds; GetClientRect(view, &bounds);
    pointer_down(bounds.right / 2, bounds.bottom / 2, WM_LBUTTONDOWN); end_stroke();
    check("ground click commits one lava pour", !pendingPour && scene.LiquidSources.size() == 1 && scene.LiquidSources[0].Lava);
    command(Undo); check("pour undo", scene.LiquidSources.empty());
    command(Redo); check("pour redo", scene.LiquidSources.size() == 1);
    command(BuildSculpt); command(2300);
    check("draw route supersedes sculpt mode", currentMode == "Route" && !stroke);
    command(BuildShapes); command(DockSettings);
    check("shapes gear exposes snapping and views", inputs.contains("gridSnap") && !inputs.contains("paint"));
    RECT after; GetClientRect(view, &after);
    check("opening controls preserves full canvas size", EqualRect(&initial, &after));
    #include "scroll_layout_checks.inl"
    #include "interface_scale_checks.inl"
    if(GetEnvironmentVariableW(L"STARLIGHT_UI_LAYOUT_ONLY",nullptr,0)) {
        report["success"]=failures==0;report["failures"]=failures;
        atomic_write(output,report.dump(2));return failures?1:0;
    }
    #include "classic_completion_checks.inl"
    bool projectDialogStayedInactive = true;
    auto cycle = [&] {
        auto tipCheckpoint = [&](const char* stage) {
            if (!report.contains("tooltip_cycle_trace")) report["tooltip_cycle_trace"] = nlohmann::json::array();
            if (report["tooltip_cycle_trace"].size() < 40)
                report["tooltip_cycle_trace"].push_back({{"stage",stage},{"count",SendMessage(tips,TTM_GETTOOLCOUNT,0,0)}});
        };
        tipCheckpoint("start");
        for (int section : {BuildTerrain, BuildShapes, BuildNature, BuildSculpt}) command(section);
        tipCheckpoint("dock");
        for (int menu : {MenuFile, MenuBuilder, MenuWorld, MenuMore}) { toggle_menu(menu); toggle_menu(0); }
        tipCheckpoint("menus");
        for (int page : {0, 1, 2, 3, 4, 5}) show_inspector(page);
        tipCheckpoint("inspector");
        command(ShowMusic);command(CloseMusic);open_project();
        projectDialogStayedInactive &= !projectOwnerEnabled && GetForegroundWindow()!=projectWindow;
        close_project();
        tipCheckpoint("project");
        command(BuildShapes); command(DockSettings);
        tipCheckpoint("end");
        // Service paints and the Windows message queue between cycles. Otherwise
        // Windows can create a hung-window ghost and inflate the process USER count.
        MSG message;
        const auto serviceUntil = GetTickCount64() + 20;
        unsigned serviced = 0;
        while (serviced++ < 256 && GetTickCount64() < serviceUntil && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    };
    cycle();
    const auto warmDeadline = GetTickCount64() + 30000;
    while (renderer->status().frames < 3 && renderer->status().error.empty() && GetTickCount64() < warmDeadline) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    check("renderer finishes startup before lifetime baseline", renderer->status().frames >= 3 && renderer->status().error.empty());
    renderer->suspend_rendering(true);
    const auto parkDeadline = GetTickCount64() + 10000;
    while (!renderer->status().suspended && GetTickCount64() < parkDeadline) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    check("renderer parks between frames for isolated UI lifetime check", renderer->status().suspended);
    cycle();
    report["renderer_frames_before"] = renderer->status().frames;
    auto proc = GetCurrentProcess();
    auto windowInventory = [&] {
        std::map<std::string,int> classes;
        auto add = [](HWND h, LPARAM data) -> BOOL {
            DWORD pid=0; GetWindowThreadProcessId(h,&pid);
            if (pid == GetCurrentProcessId()) {
                wchar_t name[128]{}; GetClassNameW(h,name,128);
                ++(*reinterpret_cast<std::map<std::string,int>*>(data))[utf8(name)];
            }
            return TRUE;
        };
        struct Context { decltype(add) callback; std::map<std::string,int>* classes; } context{add,&classes};
        EnumWindows([](HWND h, LPARAM data) -> BOOL {
            auto& context=*reinterpret_cast<Context*>(data);
            DWORD pid=0; GetWindowThreadProcessId(h,&pid);
            if (pid == GetCurrentProcessId()) {
                context.callback(h,reinterpret_cast<LPARAM>(context.classes));
                EnumChildWindows(h,context.callback,reinterpret_cast<LPARAM>(context.classes));
            }
            return TRUE;
        },reinterpret_cast<LPARAM>(&context));
        for (HWND h=nullptr;(h=FindWindowExW(HWND_MESSAGE,h,nullptr,nullptr))!=nullptr;)
            add(h,reinterpret_cast<LPARAM>(&classes));
        return classes;
    };
    // Complete lazy Windows text/input/display initialization before sampling.
    // Preserve warm-up inventories and synchronous creation stacks. No class is
    // excluded and no foreign/system-owned window is destroyed by the test.
    report["warmup_windows"] = nlohmann::json::array();
    // This is a bounded resource-lifetime check, not a UI speed benchmark.
    // Allow slower systems to complete the same 24 warm-up and 80 measured cycles.
    const auto churnStarted = GetTickCount64();
    constexpr ULONGLONG churnBudgetMs = 300000;
    const auto churnDeadline = churnStarted + churnBudgetMs;
    int warmupCycles = 0;
    for(int i=0;i<24 && GetTickCount64()<churnDeadline;++i){progress("warming up menus",i+1,24);cycle();report["warmup_windows"].push_back(windowInventory());++warmupCycles;}
    report["windows_before"] = windowInventory();
    const auto beforeGdi = GetGuiResources(proc, GR_GDIOBJECTS), beforeUser = GetGuiResources(proc, GR_USEROBJECTS);
    const auto beforeTips = SendMessage(tips, TTM_GETTOOLCOUNT, 0, 0);
    DWORD userPeak[2]{}, gdiPeak[2]{};
    report["handle_samples"] = nlohmann::json::array();
    int completedCycles = 0;
    for (int i = 0; i < 80 && GetTickCount64()<churnDeadline; ++i) {
        progress("checking menu resources",i+1,80);
        cycle();
        const auto user = GetGuiResources(proc, GR_USEROBJECTS), gdi = GetGuiResources(proc, GR_GDIOBJECTS);
        userPeak[i/40] = std::max(userPeak[i/40],user); gdiPeak[i/40] = std::max(gdiPeak[i/40],gdi);
        report["handle_samples"].push_back({{"user",user},{"gdi",gdi}});
        ++completedCycles;
    }
    check("all warmup and measured UI cycles finish within the stage deadline", warmupCycles == 24 && completedCycles == 80);
    check("automated project dialogs do not take foreground from other windows", projectDialogStayedInactive);
    report["churnTiming"]={{"budgetMs",churnBudgetMs},{"elapsedMs",GetTickCount64()-churnStarted},{"warmupCycles",warmupCycles},{"measuredCycles",completedCycles}};
    const auto afterGdi = GetGuiResources(proc, GR_GDIOBJECTS), afterUser = GetGuiResources(proc, GR_USEROBJECTS);
    const auto afterTips = SendMessage(tips, TTM_GETTOOLCOUNT, 0, 0);
    report["renderer_frames_after"] = renderer->status().frames;
    report["windows_after"] = windowInventory();
    report["lifetime"] = {{"cycles",completedCycles},{"gdi_before",beforeGdi},{"gdi_after",afterGdi},
        {"user_before",beforeUser},{"user_after",afterUser},{"tooltips_before",beforeTips},{"tooltips_after",afterTips}};
    report["lifetime"]["first_40_user_peak"] = userPeak[0]; report["lifetime"]["last_40_user_peak"] = userPeak[1];
    report["lifetime"]["first_40_gdi_peak"] = gdiPeak[0]; report["lifetime"]["last_40_gdi_peak"] = gdiPeak[1];
    check("owned window classes and counts remain identical", report["windows_before"] == report["windows_after"]);
    // USER includes Windows/IME helper objects, not just our controls. Compare
    // equal batches and retain every sample: a rising resource high-water mark fails.
    check("second churn batch does not increase USER or GDI high-water marks", userPeak[1] <= userPeak[0] && gdiPeak[1] <= gdiPeak[0] && afterGdi <= beforeGdi);
    check("tooltip registrations exist and retire with controls", beforeTips > 0 && beforeTips == afterTips);
    check("UI churn does not submit frames while renderer is parked", report["renderer_frames_before"] == report["renderer_frames_after"]);
    const auto parkedFrames = renderer->status().frames;
    renderer->suspend_rendering(false);
    const auto resumeDeadline = GetTickCount64() + 10000;
    while (renderer->status().frames < parkedFrames + 2 && GetTickCount64() < resumeDeadline) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    check("renderer resumes after UI changes", renderer->status().frames >= parkedFrames + 2 && renderer->status().error.empty());
    #include "editor_wave2_ui_checks.inl"
    check("isolated Windows input runtime shuts down cleanly", inputRuntime.finish());
    report["window_creation_trace"] = windowTrace.report();
    report["pass"] = failures == 0;
    report["elapsed_ms"] = GetTickCount64() - auditStarted;
    atomic_write(output, report.dump(2));
    writeProgress(output.string()+".status.json", nlohmann::json({{"state",failures ? "failed" : "passed"},{"checks",report["checks"].size()},{"failures",failures},{"elapsed_ms",GetTickCount64()-auditStarted}}));
    dirty = false;
    SendMessage(window, WM_CLOSE, 0, 0);
    return failures ? 1 : 0;
}
