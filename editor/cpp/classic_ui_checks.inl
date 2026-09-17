// Runs the real form/command code against an isolated, newly created editor scene.
// No user's window or scene is inspected or modified by this regression harness.
int ui_checks(const fs::path& output) {
    KillTimer(window, 1);
    EnableWindow(window, FALSE); // An isolated automated test, not an interactive project.
    nlohmann::json report = {{"checks", nlohmann::json::array()}};
    int failures = 0;
    auto check = [&](const char* name, bool okay) {
        report["checks"].push_back({{"name", name}, {"pass", okay}});
        if (!okay) ++failures;
        atomic_write(output.string()+".progress.json",report.dump(2));
    };
    RECT initial; GetClientRect(view, &initial);
    command(BuildShapes); command(2000); command(ObjectProperties);
    check("beginner hides transforms, physics and irrelevant lightning", inputs.contains("size") && !inputs.contains("x") && !inputs.contains("dynamic") && !inputs.contains("tx"));
    actor()->TargetX = 4.5; actor()->Density = 950;
    SetWindowTextW(inputs.at("name"), L"Classic control test");
    SendMessage(inputs.at("size"), TBM_SETPOS, TRUE, 150);
    command(Apply);
    check("beginner edits preserve hidden properties", actor()->Name == "Classic control test" && actor()->Size == 1.5 && actor()->TargetX == 4.5 && actor()->Density == 950);
    command(Advanced); command(BuildDetails); command(BodyDetails);
    check("advanced build/body expand independently", inputs.contains("x") && inputs.contains("width") && inputs.contains("density") && !inputs.contains("tx"));
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
    #include "classic_completion_checks.inl"
    auto cycle = [&] {
        for (int section : {BuildTerrain, BuildShapes, BuildNature, BuildSculpt}) command(section);
        for (int menu : {MenuFile, MenuBuilder, MenuWorld, MenuMore}) { toggle_menu(menu); toggle_menu(0); }
        for (int page : {0, 1, 2, 3, 4, 5}) show_inspector(page);
        command(ShowMusic);command(CloseMusic);open_project();close_project();
        command(BuildShapes); command(DockSettings);
        // Service paints and the Windows message queue between cycles. Otherwise
        // Windows can create a hung-window ghost and inflate the process USER count.
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
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
    report["windows_before"] = windowInventory();
    const auto beforeGdi = GetGuiResources(proc, GR_GDIOBJECTS), beforeUser = GetGuiResources(proc, GR_USEROBJECTS);
    const auto beforeTips = SendMessage(tips, TTM_GETTOOLCOUNT, 0, 0);
    DWORD userPeak[2]{}, gdiPeak[2]{};
    report["handle_samples"] = nlohmann::json::array();
    for (int i = 0; i < 80; ++i) {
        cycle();
        const auto user = GetGuiResources(proc, GR_USEROBJECTS), gdi = GetGuiResources(proc, GR_GDIOBJECTS);
        userPeak[i/40] = std::max(userPeak[i/40],user); gdiPeak[i/40] = std::max(gdiPeak[i/40],gdi);
        report["handle_samples"].push_back({{"user",user},{"gdi",gdi}});
    }
    const auto afterGdi = GetGuiResources(proc, GR_GDIOBJECTS), afterUser = GetGuiResources(proc, GR_USEROBJECTS);
    const auto afterTips = SendMessage(tips, TTM_GETTOOLCOUNT, 0, 0);
    report["renderer_frames_after"] = renderer->status().frames;
    report["windows_after"] = windowInventory();
    report["lifetime"] = {{"cycles",80},{"gdi_before",beforeGdi},{"gdi_after",afterGdi},
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
    report["pass"] = failures == 0;
    atomic_write(output, report.dump(2));
    dirty = false;
    SendMessage(window, WM_CLOSE, 0, 0);
    return failures ? 1 : 0;
}
