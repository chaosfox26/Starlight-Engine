// Native controls in the same order and grouping as the reference main menu.
std::map<std::string, HWND> mainInputs;
std::vector<RECT> mainRects;
int mainScroll = 0, mainContentHeight = 0;
static constexpr int ApplyAtmosphere = 4600, ApplyRaytracing = 4601, MainArrowPan = 4602;
static constexpr int ApplySelectedEffect = 4610;
static constexpr int SettingsScale = 4620;
static constexpr int InterfaceSlider = 4621, InterfaceReadout = 4622;

void build_reference_effects() {
    mainInputs.clear(); mainRects.clear(); mainScroll = 0;
    int y = 20;
    auto add = [&](const wchar_t* cls, const wchar_t* label, int id, int height, DWORD flags = 0) {
        auto h = control(menuPanel, cls, label, id, 20, y, 252, height, flags);
        menuControls.push_back(h); mainRects.push_back({20, y, 272, y + height}); y += height + 8;
        return h;
    };
    add(L"STATIC", L"FIRE, LAVA & LIGHT", 0, 24);
    add(L"BUTTON", L"Add fire", 2004, 36);
    add(L"BUTTON", L"Add lava pool", 2006, 36);
    add(L"BUTTON", L"Add light", 2005, 36);
    auto c = actor();
    auto field = [&](const char* key, const wchar_t* label, std::string value) {
        add(L"STATIC", label, 0, 22);
        mainInputs[key] = add(L"EDIT", wide(value).c_str(), 0, 30, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
    };
    field("color", L"Selected effect color (e.g. #FF651C)", c ? c->Color : "#FF651C");
    field("intensity", L"Brightness (0–100)", std::to_string(c ? c->LightIntensity : 25));
    field("range", L"Light reach in metres", std::to_string(c ? c->LightRange : 6));
    field("cone", L"Light cone (5–180°; 180 = point)", std::to_string(c ? c->LightCone : 180));
    add(L"BUTTON", L"Apply to selected effect", ApplySelectedEffect, 36);
    add(L"STATIC", L"Visual effects; no burning gameplay.", 0, 22);
    mainContentHeight = y + 12;
}

void scroll_main(int position) {
    MenuTiming timing("menu-scroll");
    RECT bounds; GetClientRect(menuPanel, &bounds);
    mainScroll = std::clamp(position, 0, std::max(0, settings_px(mainContentHeight) - int(bounds.bottom)));
    auto batch = BeginDeferWindowPos(int(mainRects.size()));
    for (size_t i = 0; i < mainRects.size(); ++i) {
        auto r = mainRects[i];
        r={settings_px(r.left),settings_px(r.top),settings_px(r.right),settings_px(r.bottom)};
        if (batch) batch = DeferWindowPos(batch, menuControls[i], nullptr, r.left, r.top - mainScroll,
            r.right - r.left, r.bottom-r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    }
    if (!batch || !EndDeferWindowPos(batch)) {
        for (size_t i = 0; i < mainRects.size(); ++i) {
            auto r = mainRects[i];
        r={settings_px(r.left),settings_px(r.top),settings_px(r.right),settings_px(r.bottom)};
            SetWindowPos(menuControls[i], nullptr, r.left, r.top - mainScroll,
                r.right - r.left, r.bottom-r.top, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
        }
    }
    SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
    si.nMax = std::max(0, settings_px(mainContentHeight) - 1); si.nPage = bounds.bottom; si.nPos = mainScroll;
    SetScrollInfo(menuPanel, SB_VERT, &si, FALSE);
    // Paint the completed layout once, including children whose gradient origin moved.
    RedrawWindow(menuPanel, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_NOERASE);
}
void build_reference_main() {
    mainInputs.clear(); mainRects.clear(); mainScroll = 0;
    int y = 20;
    auto add = [&](const wchar_t* cls, const wchar_t* label, int id, int height, DWORD flags = 0) {
        auto h = control(menuPanel, cls, label, id, 20, y, 192, height, flags);
        menuControls.push_back(h); mainRects.push_back({20, y, 212, y + height}); y += height + 6;
        return h;
    };
    auto label = [&](const wchar_t* text) { add(L"STATIC", text, 0, 20); };
    auto button = [&](int id, const wchar_t* text) { add(L"BUTTON", text, id, 36); };
    auto box = [&](const char* key, const wchar_t* text, bool value, int id = 0) {
        auto h = add(L"BUTTON", text, id, 25, BS_AUTOCHECKBOX);
        SendMessage(h, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0); mainInputs[key] = h;
    };
    auto slider = [&](const char* key, double value, double low, double high) {
        auto h = add(TRACKBAR_CLASSW, L"", 0, 26, TBS_NOTICKS | WS_TABSTOP);
        double scale = std::string_view(key) == "fog" ? 1000. : 100.;
        SendMessage(h, TBM_SETRANGE, TRUE, MAKELPARAM(int(low * scale), int(high * scale)));
        SendMessage(h, TBM_SETPOS, TRUE, int(std::lround(value * scale))); mainInputs[key] = h;
    };
    label(L"STARLIGHT");
    label(L"Interface size");
    auto settingsSize=add(L"BUTTON",L"Auto-fit window",SettingsScale,30,BS_AUTOCHECKBOX|WS_TABSTOP);
    SendMessage(settingsSize,BM_SETCHECK,interfacePercent==0?BST_CHECKED:BST_UNCHECKED,0);
    auto manualSlider=add(TRACKBAR_CLASSW,L"",InterfaceSlider,30,TBS_NOTICKS|WS_TABSTOP);
    SendMessage(manualSlider,TBM_SETRANGE,TRUE,MAKELPARAM(25,200));
    SendMessage(manualSlider,TBM_SETLINESIZE,0,1);SendMessage(manualSlider,TBM_SETPAGESIZE,0,5);
    SendMessage(manualSlider,TBM_SETPOS,TRUE,manualInterfacePercent);
    add(L"STATIC",(interfacePercent==0?L"Auto: "+std::to_wstring(MulDiv(settingsScale,96,GetDpiForWindow(window)))+L"%":std::to_wstring(manualInterfacePercent)+L"%").c_str(),InterfaceReadout,20);
    EnableWindow(manualSlider,interfacePercent!=0);
    button(FullScreen, fullScreen ? L"Exit full screen" : L"Enter full screen");
    button(New, L"New scene"); button(Open, L"Open scene…"); button(Save, L"Save scene");
    button(AdventurePreset,L"Use adventure controls");
    button(NewAdventure,L"New starter adventure...");
    button(NewQuestAdventure,L"New quest adventure...");
    button(InteractiveBuild,explorer ? L"Return to editor view" : L"Walk and build (preview)");
    button(ExportGame,L"Export playable game...");
    button(ExportPortable, L"Export portable scene…");
    button(ExportSelection, L"Export selected objects…");
    label(L"REUSABLE OBJECTS");
    button(BrowseAssets, L"Browse assets…");
    button(CopyObjects, L"Copy selected objects");
    button(PasteObjects, L"Paste copied objects");
    button(SaveObject, L"Save selected shape / sculpt…");
    button(LoadObject, L"Add saved object…");
    button(SaveGroup, L"Save selected objects as group…");
    button(LoadGroup, L"Add saved group…");
    button(MergeScene, L"Add objects from another scene…");
    button(ReplaceModel, L"Replace selected imported model…");
    button(Projects, L"Projects & presets…"); button(Frame, L"Frame world");
    label(L"Time of day (0–24)"); slider("hour", scene.Hour, 0, 24);
    box("cycle", L"Run day / night cycle", scene.DayCycle);
    label(L"Day duration (seconds)");
    mainInputs["daylength"] = add(L"EDIT", wide(std::to_string(scene.DayLength)).c_str(), 0, 30, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
    label(L"Fog density"); slider("fog", scene.FogDensity, 0, .3);
    button(ApplyAtmosphere, L"Apply atmosphere");
    label(L"Raytracing Quality"); box("rt", L"Enable DXR 1.1", scene.RaytracingEnabled);
    auto combo = add(L"COMBOBOX", L"", 0, 30, CBS_DROPDOWNLIST | WS_VSCROLL);
    mainRects.back().bottom = mainRects.back().top + 260;
    for (auto text : {L"0 · Off (raster)", L"1 · Low: shadows", L"2 · Medium: AO/reflections", L"3 · High: hybrid", L"4 · Path tracing"})
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    SendMessage(combo, CB_SETCURSEL, scene.RaytracingQuality, 0); mainInputs["tier"] = combo;
    label(L"Path bounces (2–4)"); slider("bounces", scene.PathBounces, 2, 4);
    box("freeze", L"Freeze reference animation", scene.FreezeRaytracingAnimation);
    box("rr", L"Ray Reconstruction", scene.RayReconstructionEnabled);
    button(ApplyRaytracing, L"Apply raytracing");
    label(L"Native resolution · no frame generation");
    label(L"Camera travel speed"); slider("pan", panSpeed, .25, 6);
    label(L"Camera tilt / turn speed"); slider("tilt", tiltSpeed, .2, 4);
    box("arrows", L"Arrow keys move camera", arrowPan, MainArrowPan);
    mainContentHeight = y + 14;
}
bool reference_main_command(int id) {
    if(id==SettingsScale) {
        auto picker=GetDlgItem(menuPanel,SettingsScale);
        const bool automatic=SendMessage(picker,BM_GETCHECK,0,0)==BST_CHECKED;
        interfacePercent=automatic?0:manualInterfacePercent;
        EnableWindow(GetDlgItem(menuPanel,InterfaceSlider),!automatic);
        apply_interface_scale(requested_interface_scale(GetDpiForWindow(window)));
        layout();
        save_interface_size();
        return true;
    }
    // Unnamed checkboxes and quality selection keep the popup open until Apply.
    if (id == 0 && (openMenu == MenuFile || openMenu == MenuWorld)) return true;
    if (openMenu == MenuWorld && id >= 2004 && id <= 2006) {
        SendMessage(shape, CB_SETCURSEL, id - 2000, 0);
        command(Add);
        toggle_menu(MenuWorld);
        return true;
    }
    if (id == ApplySelectedEffect) {
        auto c = actor();
        if (!c || (c->Shape != "fire" && c->Shape != "lava" && c->Shape != "light" && c->Shape != "chaos-lightning" && c->Shape != "chaos-rock" && c->Shape != "light-shard"))
            throw std::runtime_error("Select a fire, lava pool or light first");
        auto number = [&](const char* key, double low, double high) {
            auto input = text(mainInputs.at(key)); size_t used = 0; double v = std::stod(input, &used);
            if (used != input.size() || !std::isfinite(v) || v < low || v > high) throw std::runtime_error(std::string("Invalid effect ") + key);
            return v;
        };
        auto color = text(mainInputs.at("color"));
        double intensity = number("intensity", 0, 100), reach = number("range", .1, 50), cone = number("cone", 5, 180);
        edit([&] { c->Color = color; c->LightIntensity = intensity; c->LightRange = reach; c->LightCone = cone; });
        return true;
    }
    if (id == MainArrowPan) { arrowPan = SendMessage(mainInputs.at("arrows"), BM_GETCHECK, 0, 0) == BST_CHECKED; return true; }
    if (id != ApplyAtmosphere && id != ApplyRaytracing) return false;
    auto checked = [&](const char* key) { return SendMessage(mainInputs.at(key), BM_GETCHECK, 0, 0) == BST_CHECKED; };
    auto value = [&](const char* key) { return SendMessage(mainInputs.at(key), TBM_GETPOS, 0, 0) / (std::string_view(key) == "fog" ? 1000. : 100.); };
    if (id == ApplyAtmosphere) {
        auto input = text(mainInputs.at("daylength")); size_t consumed = 0;
        double seconds = std::stod(input, &consumed);
        if (consumed != input.size() || !std::isfinite(seconds) || seconds <= 0)
            throw std::runtime_error("Day duration must be a positive number of seconds");
        edit([&] { scene.Hour = value("hour"); scene.DayCycle = checked("cycle"); scene.DayLength = seconds; scene.FogDensity = value("fog"); });
    } else {
        edit([&] { scene.RaytracingEnabled = checked("rt"); scene.RaytracingQuality = int(SendMessage(mainInputs.at("tier"), CB_GETCURSEL, 0, 0));
            scene.PathBounces = std::clamp(int(std::lround(value("bounces"))), 2, 4);
            scene.FreezeRaytracingAnimation = checked("freeze"); scene.RayReconstructionEnabled = checked("rr"); });
    }
    return true;
}
