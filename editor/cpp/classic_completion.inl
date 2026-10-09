// Native secondary surfaces share commands, never simulation or HWND ownership.
std::unique_ptr<GamePreview> gamePreview;
HWND morePanel{}, featureList{}, featureDetail{}, featureResult{}, musicPanel{}, musicTrack{}, musicToggle{},
    musicSlider{};
HWND projectWindow{}, projectHost{}, projectMessage{};
HWND savedInspector{};
HFONT projectHeading{};
bool projectOwnerEnabled = true, musicPlaying = false, cardDragging = false, cardMoved = false;
uint64_t inlinePlayFrame = 0;
bool inlineFinished = false;
POINT cardStart{};
int dockScroll = 0, dockViewportWidth = 0;
std::map<std::string, HWND> projectInputs;
std::vector<HWND> projectControls;
std::vector<RECT> projectRects;
const nlohmann::json catalogue = nlohmann::json::parse(feature_catalog);
static constexpr int ShowMusic = 4000, FeatureList = 4001, FeatureCheck = 4002, CloseMore = 4003, CloseMusic = 4004,
                     MusicToggle = 4005, MusicVolume = 4006, CloseProject = 4007;
static constexpr const wchar_t *floorNames[] = {L"Dark blue", L"Black", L"Purple", L"Pink", L"Green", L"Sand"};
static constexpr const char *floorColors[] = {"#182033", "#050505", "#592D80", "#C65394", "#356F45", "#B29B6D"};

void preview_state(bool state)
{
    playing = state;
    SetWindowTextW(playButton, state ? L"Ⅱ" : L"▶");
    SetWindowTextW(playState, state ? L"Playing" : L"Paused");
}
void playback_command(int id)
{
    if(!carriedId.empty())finish_carry(false);
    if(openMenu==ObjectBehaviorMenu)toggle_menu(0);
    end_stroke();
    // Play belongs to the existing editor canvas in both window modes.
    // Keep the same session so entering full screen cannot restart simulation.
    bool next = id == Play ? !playing : false;
    std::optional<LoadingScreen> loading;RenderStatus statusBeforeInstall{};
    if(scene.Game && next && !adventure){
        // Every scene starts through the same preparation: loading screen, cache warm-up, collision, break-ahead fracture, upload.
        loading.emplace();loading->show(window,view);loading->update(0,"Starting your scene");statusBeforeInstall=renderer->status();
        renderer->playback(false);
        questTracker.reset();adventure.emplace();adventure->start(scene);
        lastPreparation=prepare_scene(*adventure,*loading);
        if(lastPreparation.stopped()){ // never reveal a scene that did not finish preparing
            loading->update(loading->fraction(),"Loading did not finish: "+lastPreparation.reason);
            const std::string reason=lastPreparation.reason;const bool cancelled=lastPreparation.cancelled;loading.reset();adventure.reset();adventureHint.clear();
            release_deferred_close();
            if(!cancelled)error("Play could not start. "+reason);
            return;
        }
        if(explorer){adventure->position=explorer->position;adventure->yaw=explorer->yaw;adventure->pitch=explorer->pitch;}
    }
    if(!next && explorer){
        if(adventure){explorer->position=adventure->position;explorer->yaw=adventure->yaw;explorer->pitch=adventure->pitch;}
        explorer->rebuild_for_authoring(scene);
        if(id==Stop)explorer->start(scene,false);
        renderer->scene(scene);adventure.reset();adventureHint.clear();
    }
    if(!next){adventureLook=false;camera();}
    if(id==Stop){if(adventure)renderer->scene(scene);adventure.reset();adventureHint.clear();}
    struct PrewarmEnd{NativeSession* session;~PrewarmEnd(){if(session)session->prewarm_play(false);}} prewarmEnd{loading?renderer.get():nullptr}; // an exception in the barrier must not leave the session in prewarm mode
    if(next && adventure){
        if(loading)renderer->prewarm_play(true); // the barrier installs the scene play will use; playback(true) below ends the prewarm
        renderer->gameplay_time(adventure->seconds());
        renderer->camera({adventure->position,adventure->position+adventure->direction(),1});
        adventure->update_rules();
        const uint64_t installedScene=renderer->scene(adventure->scene());
        if(loading)lastPreparation.sceneGeneration=installedScene;
        SetFocus(view);
    }
    {
        if(!adventure)renderer->gameplay_time(std::nullopt);
        inlinePlayFrame = renderer->status().frames;
        renderer->playback_options(repeat, follow && actor() ? actor()->Id : std::string{});
        const bool startAfterReady = loading && adventure && next;
        renderer->playback(next && !startAfterReady, id == Stop || (next && (inlineFinished || explorer.has_value())));
        inlineFinished = false;
        if(!startAfterReady)preview_state(next);
    }
    if(loading&&adventure){
        finish_scene_upload(*renderer,statusBeforeInstall,*adventure,*loading,lastPreparation);
        if(!lastPreparation.ready()){
            const std::string reason=lastPreparation.reason;const bool cancelled=lastPreparation.cancelled;loading.reset();
            renderer->prewarm_play(false);
            playback_command(Stop);
            release_deferred_close();
            if(!cancelled)error("Play stopped because loading did not finish. "+reason);
            return;
        }
        inlinePlayFrame = renderer->status().frames;
        renderer->playback(true);
        preview_state(true);
    }
    loading.reset();
    release_deferred_close();
    if(adventure)questTracker.update(scene,adventure->rules(),next);else questTracker.hide();
    ShowWindow(adventureHudPanel,next && adventure ? SW_SHOW : SW_HIDE);
    if(!next && explorer)explorer_hint();
    if(next && adventure){
        SetWindowTextW(adventureHud,adventureHint.empty() ? L"WASD: walk | Space: jump | Right-drag: look | F / E: use | Escape: pause" : wide(adventureHint).c_str());
        SetWindowPos(adventureHudPanel,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
    if (id == Stop)
        SetWindowTextW(playState, L"Ready");
}
void feature_selection(bool invoke = false)
{
    int i = int(SendMessage(featureList, LB_GETCURSEL, 0, 0));
    if (i < 0 || i >= int(catalogue.size()))
        return;
    const auto &item = catalogue[i];
    auto s = item.at("Name").get<std::string>() + "\r\n" + item.at("Label").get<std::string>() + "\r\n\r\n" +
             item.at("Description").get<std::string>() + "\r\n\r\nWhat needs to come first\r\n" +
             item.at("DependsOn").get<std::string>() + "\r\n\r\nHow we will know it works\r\n" +
             item.at("Proof").get<std::string>();
    SetWindowTextW(featureDetail, wide(s).c_str());
    SetWindowTextW(featureResult,
                   invoke ? (item.at("Status") == "experimental"
                                 ? L"Experimental controls are available in the editor. No scene changes made."
                                 : L"Coming soon — not implemented. No scene changes made.")
                          : L"");
}
void ensure_secondary_panels()
{
    if (morePanel)
        return;
    morePanel = panel(20, 58, 740, 540);
    control(morePanel, L"STATIC", L"MORE", 0, 20, 16, 280, 26);
    control(morePanel, L"BUTTON", L"×", CloseMore, 690, 12, 32, 30);
    control(morePanel, L"BUTTON", L"Music player", ShowMusic, 20, 52, 220, 36);
    auto coming = control(morePanel, L"STATIC", L"COMING SOON", 0, 20, 102, 690, 24);
    SetPropW(coming, L"sl.textColor", reinterpret_cast<HANDLE>(INT_PTR(RGB(231, 118, 233) + 1)));
    control(morePanel, L"STATIC", L"Explore future tools. Availability checks never change your scene.", 0, 20, 132,
            690, 26);
    featureList = control(morePanel, L"LISTBOX", L"", FeatureList, 20, 170, 250, 330, LBS_NOTIFY | WS_VSCROLL);
    featureDetail = control(morePanel, L"EDIT", L"", 0, 284, 170, 432, 265, ES_MULTILINE | ES_READONLY | WS_VSCROLL);
    control(morePanel, L"BUTTON", L"Check availability", FeatureCheck, 284, 444, 200, 32);
    featureResult = control(morePanel, L"STATIC", L"", 0, 284, 480, 432, 48);
    for (const auto &item : catalogue)
        SendMessage(featureList, LB_ADDSTRING, 0,
                    reinterpret_cast<LPARAM>(wide(item.at("Name").get<std::string>()).c_str()));
    SendMessage(featureList, LB_SETCURSEL, 0, 0);
    feature_selection();
    musicPanel = panel(20, 58, 280, 250);
    control(musicPanel, L"STATIC", L"YOUR MUSIC", 0, 18, 18, 215, 25);
    control(musicPanel, L"BUTTON", L"×", CloseMusic, 236, 12, 30, 30);
    musicTrack = control(musicPanel, L"STATIC", L"Choose songs to listen locally", 0, 18, 52, 244, 42);
    control(musicPanel, L"BUTTON", L"Choose songs…", MusicOpen, 18, 100, 244, 34);
    musicToggle = control(musicPanel, L"BUTTON", L"Play", MusicToggle, 18, 144, 80, 32);
    control(musicPanel, L"BUTTON", L"Next", MusicNext, 104, 144, 76, 32);
    control(musicPanel, L"BUTTON", L"Stop", MusicStop, 186, 144, 76, 32);
    control(musicPanel, L"STATIC", L"Volume", 0, 18, 194, 60, 24);
    musicSlider = control(musicPanel, TRACKBAR_CLASSW, L"Volume", MusicVolume, 82, 190, 180, 30, TBS_NOTICKS);
    SendMessage(musicSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(musicSlider, TBM_SETPOS, TRUE, int(musicVolume * 100));
    ShowWindow(morePanel, SW_HIDE);
    ShowWindow(musicPanel, SW_HIDE);
    music_controls();
}
void music_controls()
{
    if (!musicPanel)
        return;
    SetWindowTextW(musicToggle, musicPlaying ? L"Pause" : L"Play");
    SetWindowTextW(musicTrack, songs.empty() ? L"Choose songs to listen locally" : songs[songIndex].filename().c_str());
    for (int id : std::array<int, 3>{MusicToggle, MusicNext, MusicStop})
        EnableWindow(GetDlgItem(musicPanel, id), !songs.empty());
    SendMessage(musicSlider, TBM_SETPOS, TRUE, int(musicVolume * 100));
}
void layout_secondary()
{
    layout_asset_browser();
    if (morePanel)
    {
        auto p=[&](int n){return settings_px(n);};
        unscroll_panel(morePanel);unscroll_panel(musicPanel);
        const int mw=p(740),mh=p(540),left=p(250),right=mw-left-p(54),bottom=mh-p(14);
        MoveWindow(featureList,p(20),p(170),left,bottom-p(170),TRUE);
        MoveWindow(featureDetail,left+p(34),p(170),right,bottom-p(260),TRUE);
        MoveWindow(GetDlgItem(morePanel,FeatureCheck),left+p(34),bottom-p(82),p(200),p(32),TRUE);
        MoveWindow(featureResult,left+p(34),bottom-p(46),right,p(46),TRUE);
        MoveWindow(GetDlgItem(morePanel,CloseMore),mw-p(50),p(12),p(32),p(30),TRUE);
        finish_scroll_panel(morePanel,18,p(58),mw,mh);
        finish_scroll_panel(musicPanel,std::max(18,width-p(302)),p(70),p(280),p(250));
        for (auto h : {morePanel, musicPanel})
            SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}
void scroll_dock(int position)
{
    int next = std::clamp(position, 0, std::max(0, dockWidth - dockViewportWidth));
    style::scrollChildren(dock, dockScroll - next, 0);
    dockScroll = next;
    SetScrollPos(dock, SB_HORZ, next, TRUE);
}
void reveal_focus()
{
    HWND h = GetFocus();
    if (!h)
        return;
    for(const auto& [host,pos]:panelScroll)if(IsChild(host,h)) {
        RECT r{},area{};GetWindowRect(h,&r);MapWindowPoints(nullptr,host,reinterpret_cast<POINT*>(&r),2);GetClientRect(host,&area);
        const int dx=r.left<0?r.left:r.right>area.right?r.right-area.right:0;
        const int dy=r.top<0?r.top:r.bottom>area.bottom?r.bottom-area.bottom:0;
        if(dx || dy)scroll_panel(host,pos.x+dx,pos.y+dy);
    }
    if (IsChild(dock, h))
    {
        RECT r;
        GetWindowRect(h, &r);
        MapWindowPoints(nullptr, dock, reinterpret_cast<POINT *>(&r), 2);
        if (r.left < 14)
            scroll_dock(dockScroll + r.left - 14);
        else if (r.right > dockViewportWidth - 14)
            scroll_dock(dockScroll + r.right - dockViewportWidth + 14);
    }
    if (IsChild(assetPanel, h)) {
        RECT r{}, bounds{}; GetWindowRect(h, &r);
        MapWindowPoints(nullptr, assetPanel, reinterpret_cast<POINT*>(&r), 2);
        GetClientRect(assetPanel, &bounds);
        if (r.top < 8) scroll_assets(assetScroll + r.top - 8);
        else if (r.bottom > bounds.bottom - 8) scroll_assets(assetScroll + r.bottom - bounds.bottom + 8);
    }
    HWND host = projectWindow && IsWindowVisible(projectWindow) ? projectHost : inspector;
    if (IsChild(host, h))
    {
        RECT r, b;
        GetWindowRect(h, &r);
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT *>(&r), 2);
        GetClientRect(host, &b);
        int delta = r.top < 0 ? r.top : r.bottom > b.bottom ? r.bottom - b.bottom : 0;
        int &position = host == projectHost ? projectScroll : scroll;
        SCROLLINFO si{sizeof(si), SIF_ALL};
        GetScrollInfo(host, SB_VERT, &si);
        int next = std::clamp(position + delta, 0, std::max(0, si.nMax - int(si.nPage) + 1));
        style::scrollChildren(host, 0, position - next);
        position = next;
        SetScrollPos(host, SB_VERT, next, TRUE);
    }
}
static LRESULT CALLBACK card_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
{
    auto &e = *reinterpret_cast<Editor *>(data);
    try
    {
        if (m == WM_PAINT) {
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            RECT r; GetClientRect(h, &r);
            style::childBackground(h, dc);
            {
                Gdiplus::Graphics g(dc); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                style::round(g, {0, 0, float(r.right), float(r.bottom)}, 10,
                             Gdiplus::Color(255,38,53,80), Gdiplus::Color(255,38,53,80), Gdiplus::Color(255,38,53,80));
            }
            style::text(dc, e.cardFont, L"✦", {0, 10, r.right, 46}, RGB(118,201,255));
            style::text(dc, e.font, e.characterAsset.empty() ? L"Choose a character" : e.characterAsset.stem().wstring(), {4, 48, r.right-4, 77});
            style::text(dc, e.smallFont, L"Drag onto the floor", {4, 78, r.right-4, 106}, style::muted);
            EndPaint(h, &ps); return 0;
        }
        if (m == WM_LBUTTONDOWN && !e.playing && !e.characterAsset.empty())
        {
            e.cardDragging = true;
            e.cardMoved = false;
            e.cardStart = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ClientToScreen(h, &e.cardStart);
            SetCapture(h);
            SetFocus(h);
            return 0;
        }
        if (m == WM_MOUSEMOVE && e.cardDragging)
        {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ClientToScreen(h, &p);
            e.cardMoved |= std::abs(p.x - e.cardStart.x) >= GetSystemMetrics(SM_CXDRAG) ||
                           std::abs(p.y - e.cardStart.y) >= GetSystemMetrics(SM_CYDRAG);
            SetCursor(LoadCursor(nullptr, e.cardMoved ? IDC_CROSS : IDC_ARROW));
            return 0;
        }
        if (m == WM_LBUTTONUP && e.cardDragging)
        {
            bool moved = e.cardMoved;
            e.cardDragging = false;
            ReleaseCapture();
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ClientToScreen(h, &p);
            RECT r;
            GetWindowRect(e.view, &r);
            if (moved && PtInRect(&r, p) && WindowFromPoint(p) == e.view)
            {
                ScreenToClient(e.view, &p);
                e.place_character(e.floor_hit(p.x, p.y));
                e.assetsShown = false;
                e.layout();
            }
            return 0;
        }
        if (m == WM_CAPTURECHANGED)
        {
            e.cardDragging = false;
            return 0;
        }
        if (m == WM_KEYDOWN && (w == VK_RETURN || w == VK_SPACE))
        {
            e.command(PlaceCharacter);
            return 0;
        }
    }
    catch (const std::exception &ex)
    {
        e.cardDragging = false;
        ReleaseCapture();
        e.error(ex.what());
    }
    return DefSubclassProc(h, m, w, l);
}
void place_character(Vec3 p)
{
    if (playing)
        throw std::runtime_error("Pause playback before editing");
    if (characterAsset.empty())
        throw std::runtime_error(defaultCharacterProblem.empty() ? "Import a GLB character first" : defaultCharacterProblem);
    if (!model_available(characterModelId))
        throw std::runtime_error("The character model " + characterModelId + " is no longer in any asset library on this machine. Import the character again or register the library that holds it with Starlight.NativeCommands --asset-root-add <folder>. " + EngineAssets::describe_roots());
    edit([&] {
        Character c;
        c.Asset = characterModelId;
        c.Name = utf8(characterAsset.stem().wstring());
        c.X = std::clamp(p.x, -12., 12.);
        c.Z = std::clamp(p.z, -12., 12.);
        c.Y = scene.Terrain ? scene.Terrain->height_at(c.X, c.Z) : 0;
        snap_object(scene, c, gridSnap, objectSnap, groundSnap,gridStep);
        c.X = std::clamp(c.X, -12., 12.);
        c.Z = std::clamp(c.Z, -12., 12.);
        scene.Characters.push_back(c);
        selected = int(scene.Characters.size()) - 1;
    });
}
bool escape()
{
    if(!carriedId.empty()){finish_carry(false);return true;}
    if(helpShown){
        if(SendMessageW(helpPicker,CB_GETDROPPEDSTATE,0,0))SendMessageW(helpPicker,CB_SHOWDROPDOWN,FALSE,0);
        else command(HelpClose);
        return true;
    }
    if(playing && adventure){playback_command(Pause);return true;}
    if (libraryPanel && IsWindowVisible(libraryPanel)) { ShowWindow(libraryPanel, SW_HIDE); return true; }
    auto focus = GetFocus();
    wchar_t cls[32]{};
    GetClassNameW(focus, cls, 32);
    if (std::wstring_view(cls) == L"ComboBox" && SendMessage(focus, CB_GETDROPPEDSTATE, 0, 0))
    {
        SendMessage(focus, CB_SHOWDROPDOWN, FALSE, 0);
        return true;
    }
    if (projectWindow && IsWindowVisible(projectWindow))
    {
        close_project();
        return true;
    }
    if (cardDragging)
    {
        cardDragging = false;
        ReleaseCapture();
        return true;
    }
    if (stroke || orbit || boxSelecting || pendingPour)
    {
        pendingPour.reset();
        history.cancel(scene);
        stroke = orbit = boxSelecting = false;
        dragAxis = -1;
        dragOriginals.clear();
        ReleaseCapture();
        refresh();
        return true;
    }
    if (musicPanel && IsWindowVisible(musicPanel))
    {
        ShowWindow(musicPanel, SW_HIDE);
        return true;
    }
    if (morePanel && IsWindowVisible(morePanel))
    {
        ShowWindow(morePanel, SW_HIDE);
        return true;
    }
    if (openMenu)
    {
        toggle_menu(0);
        return true;
    }
    if (inspectorShown)
    {
        inspectorShown = false;
        layout();
        return true;
    }
    if (assetsShown)
    {
        assetsShown = false;
        layout();
        return true;
    }
    if (currentMode == "Route")
    {
        SendMessage(mode, CB_SETCURSEL, 0, 0);
        command(Mode);
        return true;
    }
    if (fullScreen)
    {
        command(FullScreen);
        return true;
    }
    return false;
}

static LRESULT CALLBACK project_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
{
    auto &e = *reinterpret_cast<Editor *>(data);
    try
    {
        if (m == WM_CLOSE)
        {
            e.close_project();
            return 0;
        }
        if (m == WM_SIZE)
        {
            e.layout_project();
            return 0;
        }
        if (m == WM_GETMINMAXINFO)
        {
            reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {560, 600};
            return 0;
        }
        if (m == WM_COMMAND)
        {
            if (LOWORD(w) == CloseProject)
            {
                e.close_project();
                return 0;
            }
            if (HIWORD(w) == BN_CLICKED || HIWORD(w) == CBN_SELCHANGE)
                e.project_command(LOWORD(w));
            return 0;
        }
        if (m == WM_DRAWITEM || m == WM_MEASUREITEM || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORSTATIC ||
            m == WM_CTLCOLORLISTBOX)
            return SendMessage(e.window, m, w, l);
        if (m == WM_ERASEBKGND)
        {
            RECT r;
            GetClientRect(h, &r);
            FillRect(reinterpret_cast<HDC>(w), &r, style::panelBrush());
            return 1;
        }
        if (m == WM_DPICHANGED)
        {
            auto r = reinterpret_cast<RECT *>(l);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
    }
    catch (const std::exception &ex)
    {
        SetWindowTextW(e.projectMessage, wide_lossy(ex.what()).c_str());
        diagnostic("project dialog", ex.what());
        e.project_buttons();
    }
    return DefSubclassProc(h, m, w, l);
}
void open_project()
{
    if (projectWindow && IsWindowVisible(projectWindow))
    {
        if (projectOwnerEnabled) SetForegroundWindow(projectWindow);
        return;
    }
    end_stroke();
    pendingPour.reset();
    if (!projectWindow)
    {
        WNDCLASSW wc{};
        wc.hInstance = instance;
        wc.lpfnWndProc = DefWindowProcW;
        wc.lpszClassName = L"StarlightProjectDialog";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = style::panelBrush();
        wc.hIcon = appIcon;
        RegisterClassW(&wc);
        projectWindow =
            CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Projects & presets · Starlight",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 630, 810, window, nullptr, instance, nullptr);
        if (!projectWindow)
            throw std::runtime_error("Cannot create project dialog");
        SetWindowSubclass(projectWindow, project_proc, 11, reinterpret_cast<DWORD_PTR>(this));
        BOOL dark = TRUE;
        DwmSetWindowAttribute(projectWindow, 20, &dark, sizeof(dark));
        projectHost = control(projectWindow, L"STATIC", L"", 0, 20, 20, 570, 650, WS_VSCROLL | WS_CLIPCHILDREN);
        SetWindowLongPtr(projectHost, GWL_EXSTYLE, WS_EX_CONTROLPARENT);
        SetWindowSubclass(projectHost, project_scroll_proc, 12, reinterpret_cast<DWORD_PTR>(this));
        projectMessage = control(projectWindow, L"STATIC", L"", 0, 26, 680, 540, 44);
        control(projectWindow, L"BUTTON", L"Close", CloseProject, 460, 730, 120, 32);
    }
    RECT r;
    GetWindowRect(window, &r);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
    int pw = std::min(LONG(settings_px(630)), monitor.rcWork.right - monitor.rcWork.left),
        ph = std::min(LONG(settings_px(810)), monitor.rcWork.bottom - monitor.rcWork.top);
    SetWindowPos(projectWindow, HWND_TOP,
                 std::clamp((r.left + r.right - pw) / 2, monitor.rcWork.left, monitor.rcWork.right - pw),
                 std::clamp((r.top + r.bottom - ph) / 2, monitor.rcWork.top, monitor.rcWork.bottom - ph), pw, ph,
                 SWP_NOACTIVATE);
    projectOwnerEnabled = IsWindowEnabled(window);
    EnableWindow(window, FALSE);
    project_form();
    ShowWindow(projectWindow, projectOwnerEnabled ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (projectOwnerEnabled) SetFocus(GetDlgItem(projectHost, ChooseProject));
}
int projectScroll = 0;
static LRESULT CALLBACK project_scroll_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
{
    auto &e = *reinterpret_cast<Editor *>(data);
    if (m == WM_VSCROLL || m == WM_MOUSEWHEEL)
    {
        SCROLLINFO si{sizeof(si), SIF_ALL};
        GetScrollInfo(h, SB_VERT, &si);
        int next = e.projectScroll;
        if (m == WM_MOUSEWHEEL)
            next -= GET_WHEEL_DELTA_WPARAM(w) / 120 * 60;
        else
            switch (LOWORD(w))
            {
            case SB_LINEUP:
                next -= 30;
                break;
            case SB_LINEDOWN:
                next += 30;
                break;
            case SB_PAGEUP:
                next -= int(si.nPage);
                break;
            case SB_PAGEDOWN:
                next += int(si.nPage);
                break;
            case SB_THUMBTRACK:
                next = si.nTrackPos;
                break;
            }
        next = std::clamp(next, 0, std::max(0, si.nMax - int(si.nPage) + 1));
        style::scrollChildren(h, 0, e.projectScroll - next);
        e.projectScroll = next;
        SetScrollPos(h, SB_VERT, next, TRUE);
        return 0;
    }
    if (m == WM_COMMAND || m == WM_DRAWITEM || m == WM_MEASUREITEM || m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT ||
        m == WM_CTLCOLORLISTBOX)
        return SendMessage(e.projectWindow, m, w, l);
    return DefSubclassProc(h, m, w, l);
}
void layout_project()
{
    if (!projectHost)
        return;
    auto p=[&](int n){return settings_px(n);};
    RECT r;GetClientRect(projectWindow,&r);
    MoveWindow(projectHost,p(26),p(20),r.right-p(52),std::max(p(100),int(r.bottom)-p(124)),TRUE);
    MoveWindow(projectMessage,p(26),r.bottom-p(94),r.right-p(52),p(46),TRUE);
    MoveWindow(GetDlgItem(projectWindow,CloseProject),r.right-p(146),r.bottom-p(42),p(120),p(32),TRUE);
    SCROLLINFO si{sizeof(si),SIF_RANGE|SIF_PAGE};si.nMax=p(690);si.nPage=std::max(p(100),int(r.bottom)-p(124));
    SetScrollInfo(projectHost,SB_VERT,&si,TRUE);
    RECT content;GetClientRect(projectHost,&content);
    const double factor=std::min(settingsScale/100.,content.right/500.);
    projectScroll=std::clamp(projectScroll,0,std::max(0,si.nMax-int(si.nPage)+1));SetScrollPos(projectHost,SB_VERT,projectScroll,TRUE);
    for(size_t i=0;i<projectControls.size();++i){auto b=projectRects[i];
        MoveWindow(projectControls[i],int(b.left*factor),p(b.top)-projectScroll,
            std::max(1,int((b.right-b.left)*factor)),p(b.bottom-b.top),TRUE);
    }

}
void close_project()
{
    if (presetTask.valid())
    {
        SetWindowTextW(projectMessage, L"Finishing the settings check…");
        return;
    }
    const bool restoreFocus = GetForegroundWindow() == projectWindow;
    // Re-enable the modal owner before hiding its active dialog so Windows can
    // return activation to it, rather than selecting an unrelated application.
    EnableWindow(window, projectOwnerEnabled);
    ShowWindow(projectWindow, SW_HIDE);
    if (projectOwnerEnabled && restoreFocus)
        SetFocus(view);
}
void project_buttons()
{
    if (!projectHost)
        return;
    bool busy = presetTask.valid();
    for (int id : {ChooseProject, ChoosePreset, CheckPreset, SavePreset, LaunchProject})
        EnableWindow(GetDlgItem(projectHost, id),
                     !busy && (id == ChooseProject || (project && (id == ChoosePreset || preset))));
    EnableWindow(GetDlgItem(projectWindow, CloseProject), !busy);
    for (auto &[key, h] : projectInputs)
        EnableWindow(h, !busy && project && (key == "scene" || preset.has_value()));
}
void project_form()
{
    for (auto h : projectControls)
        destroy_control(h);
    projectControls.clear();
    projectRects.clear();
    projectInputs.clear();
    projectScroll = 0;
    SetScrollPos(projectHost, SB_VERT, 0, TRUE);
    auto add = [&](const wchar_t *cls, const std::wstring &s, int id, int x, int y, int w, int h, DWORD flags = 0) {
        auto c = control(projectHost, cls, s.c_str(), id, x, y, w, h, flags);
        projectControls.push_back(c);
        projectRects.push_back({x, y, x + w, y + h});
        return c;
    };
    if (!projectHeading)
        projectHeading =
            CreateFontW(-settings_px(27), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto heading = add(L"STATIC", L"Choose your world", 0, 0, 0, 500, 38);
    SendMessage(heading, WM_SETFONT, reinterpret_cast<WPARAM>(projectHeading), TRUE);
    add(L"STATIC", L"Your project supplies the scene. Your preset supplies the test settings.", 0, 0, 44, 500, 42);
    add(L"STATIC", project ? project->path.wstring() : L"No project selected", 0, 0, 92, 490, 42);
    add(L"BUTTON", L"Open project…", ChooseProject, 0, 140, 220, 32);
    add(L"STATIC", L"Scene", 0, 0, 185, 140, 26);
    auto scenes = add(L"COMBOBOX", L"", 0, 154, 180, 328, 250, CBS_DROPDOWNLIST | WS_VSCROLL);
    projectInputs["scene"] = scenes;
    if (project)
    {
        for (const auto &s : project->scenes)
            SendMessage(scenes, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(s).c_str()));
        auto i = std::find(project->scenes.begin(), project->scenes.end(), project->selected) - project->scenes.begin();
        SendMessage(scenes, CB_SETCURSEL, i, 0);
    }
    add(L"STATIC", preset ? preset->path.wstring() : L"No preset selected", 0, 0, 222, 490, 42);
    add(L"BUTTON", L"Load preset…", ChoosePreset, 0, 270, 220, 32);
    json v = preset ? preset->data.at("settings")
                    : json{{"resolution", {2560, 1440}}, {"frameLimit", 120}, {"durationMs", 0},
                           {"displayMode", "windowed"},  {"vsync", false},    {"culling", true},
                           {"foreground", true}};
    auto edit = [&](std::string key, const wchar_t *label, std::string value, int x, int y, int w = 100) {
        add(L"STATIC", label, 0, x, y, 140, 24);
        projectInputs[key] = add(L"EDIT", wide(value), 0, x + 154, y - 3, w, 28, WS_BORDER | ES_AUTOHSCROLL);
    };
    edit("width", L"Width", std::to_string(v["resolution"][0].get<int>()), 0, 322);
    edit("height", L"Height", std::to_string(v["resolution"][1].get<int>()), 270, 322, 60);
    add(L"STATIC", L"Display", 0, 0, 364, 140, 26);
    auto display = add(L"COMBOBOX", L"", 0, 154, 360, 328, 200, CBS_DROPDOWNLIST);
    projectInputs["display"] = display;
    int di = 0;
    for (auto s : {L"fullscreen", L"windowed", L"borderless"})
    {
        SendMessage(display, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
        if (v["displayMode"] == utf8(s))
            SendMessage(display, CB_SETCURSEL, di, 0);
        ++di;
    }
    edit("fps", L"Maximum FPS", std::to_string(v["frameLimit"].get<int>()), 0, 406);
    edit("duration", L"Duration (ms)", std::to_string(v["durationMs"].get<int>()), 0, 448, 200);
    int y = 488;
    for (auto [key, label] : std::vector<std::pair<std::string, const wchar_t *>>{
             {"vsync", L"VSync"}, {"culling", L"Culling"}, {"foreground", L"Bring game to foreground"}})
    {
        auto c = add(L"BUTTON", label, 0, 0, y, 480, 28, BS_AUTOCHECKBOX);
        SendMessage(c, BM_SETCHECK, v[key].get<bool>() ? BST_CHECKED : BST_UNCHECKED, 0);
        projectInputs[key] = c;
        y += 34;
    }
    add(L"STATIC", L"Native resolution · no upscaling or frame generation", 0, 0, 594, 490, 28);
    add(L"BUTTON", L"Save preset as…", SavePreset, 0, 632, 154, 34);
    add(L"BUTTON", L"Check settings", CheckPreset, 164, 632, 154, 34);
    add(L"BUTTON", L"Launch project", LaunchProject, 328, 632, 154, 34);
    layout_project();
    project_buttons();
}
void project_command(int id)
{
    // Existing validation/launch commands use the active form's dictionary.
    // Exchange it only for dispatch, restoring the editor form even on failure.
    auto previous = std::move(inputs);
    inputs = projectInputs;
    try
    {
        command(id);
    }
    catch (...)
    {
        inputs = std::move(previous);
        throw;
    }
    inputs = std::move(previous);
    project_buttons();
}
