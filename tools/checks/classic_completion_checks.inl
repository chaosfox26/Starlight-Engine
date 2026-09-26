auto pump_until = [&](auto ready, int timeout = 15000) {
    auto deadline = GetTickCount64() + timeout;
    while (!ready() && GetTickCount64() < deadline)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    return ready();
};
ensure_secondary_panels();
check("More restores every registered catalogue entry", catalogue.size()>=20 && SendMessage(featureList, LB_GETCOUNT, 0, 0) == LRESULT(catalogue.size()));
bool details = true;
for (size_t i = 0; i < catalogue.size(); ++i)
{
    SendMessage(featureList, LB_SETCURSEL, i, 0);
    command(FeatureCheck);
    auto detail = text(featureDetail);
    details &=
        detail.find(catalogue[i]["Proof"].get<std::string>()) != std::string::npos && !text(featureResult).empty();
}
check("every catalogue item exposes dependencies proof and availability", details);
command(ShowMusic);
check("dedicated music panel has empty-state button gating and volume",
      IsWindowVisible(musicPanel) && !IsWindowEnabled(musicToggle) && SendMessage(musicSlider, TBM_GETPOS, 0, 0) == 50);
SendMessage(musicSlider, TBM_SETPOS, TRUE, 32);
SendMessage(window, WM_HSCROLL, TB_THUMBPOSITION, reinterpret_cast<LPARAM>(musicSlider));
check("music volume slider dispatches independently of inspector sliders", musicVolume == .32);
std::string wave;
auto u16 = [&](unsigned v) {
    wave.push_back(char(v));
    wave.push_back(char(v >> 8));
};
auto u32 = [&](unsigned v) {
    u16(v);
    u16(v >> 16);
};
wave += "RIFF";
u32(32036);
wave += "WAVEfmt ";
u32(16);
u16(1);
u16(1);
u32(8000);
u32(16000);
u16(2);
u16(16);
wave += "data";
u32(32000);
wave.append(32000, '\0');
auto silent = output.parent_path() / L"classic-music-check.wav";
atomic_write(silent, wave);
songs = {silent, silent};
songIndex = 0;
musicVolume = 0;
play_song();
check("native music decoder plays selected local WAV", music.state() == L"playing");
command(MusicToggle);
check("dedicated music toggle pauses playback", music.state() == L"paused");
command(MusicToggle);
command(MusicNext);
check("Next advances playlist and starts track", songIndex == 1 && music.state() == L"playing");
command(MusicStop);
check("Stop resets native music playback", music.state() == L"stopped");
music.close();
songs.clear();
musicPlaying = false;
musicVolume = .5;
music_controls();
command(CloseMusic);
command(Projects);
check("project window is separate modal with scene selector",
      projectWindow != window && IsWindowVisible(projectWindow) && !IsWindowEnabled(window) &&
          projectInputs.contains("scene"));
check("empty project disables preset checks and launch", !IsWindowEnabled(GetDlgItem(projectHost, ChoosePreset)) &&
                                                             !IsWindowEnabled(GetDlgItem(projectHost, LaunchProject)));
close_project();
check("modal restores previously disabled test owner without enabling it",
      !IsWindowVisible(projectWindow) && !IsWindowEnabled(window));
auto root = engine_path().parent_path().parent_path().parent_path().parent_path();
project = ProjectData::load(root / L"deep/projects/Empty/Empty.star");
preset = resolve_preset(engine_path(), *project, root / L"deep/settings/native-1440p.startest");
open_project();
check("loaded project populates scene choices and enables valid preset actions",
      SendMessage(projectInputs.at("scene"), CB_GETCOUNT, 0, 0) == project->scenes.size() &&
          IsWindowEnabled(GetDlgItem(projectHost, CheckPreset)));
SetWindowTextW(projectInputs.at("width"), L"not a number");
SendMessage(projectWindow, WM_COMMAND, MAKEWPARAM(CheckPreset, BN_CLICKED), 0);
check("invalid preset field stays in modal with inline error",
      !text(projectMessage).empty() && !presetTask.valid() && IsWindowVisible(projectWindow));
SetWindowTextW(projectInputs.at("width"), L"1920");
SetWindowTextW(projectInputs.at("height"), L"1080");
SendMessage(projectWindow, WM_COMMAND, MAKEWPARAM(CheckPreset, BN_CLICKED), 0);
check("checking preset disables duplicate submission and close",
      presetTask.valid() && !IsWindowEnabled(GetDlgItem(projectHost, CheckPreset)) &&
          !IsWindowEnabled(GetDlgItem(projectWindow, CloseProject)));
close_project();
check("busy modal cannot be prematurely closed", IsWindowVisible(projectWindow));
bool completed = pump_until(
    [&] {
        tick();
        return !presetTask.valid();
    },
    25000);
check("native preset reconciliation preserves draft resolution",
      completed && preset && preset->data["settings"]["resolution"][0] == 1920 &&
          preset->data["settings"]["resolution"][1] == 1080);
close_project();
command(BuildShapes);
command(DockSettings);
SendMessage(inputs.at("ground"), CB_SETCURSEL, 3, 0);
command(Apply);
check("named Pink floor choice uses original classic hex color", scene.GroundColor == "#C65394");
RECT originalWindow;
GetWindowRect(window, &originalWindow);
bool bounded = true, ends = true;
for (int w : {1050, 1200, 1380, 1800})
{
    SetWindowPos(window, nullptr, 0, 0, w, 720, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (int section : {BuildTerrain, BuildShapes, BuildNature, BuildSculpt})
    {
        command(section);
        RECT r;
        GetWindowRect(dock, &r);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT *>(&r), 2);
        bounded &= r.left >= 0 && r.right <= width;
        scroll_dock(INT_MAX);
        RECT close;
        GetWindowRect(GetDlgItem(dock, CloseDock), &close);
        MapWindowPoints(nullptr, dock, reinterpret_cast<POINT *>(&close), 2);
        ends &= close.left >= 0 && close.right <= dockViewportWidth;
        scroll_dock(0);
    }
}
check("every dock is bounded across narrow and wide window sizes", bounded);
check("horizontal scrolling reaches the last close button in every dock", ends);
SetWindowPos(window, nullptr, 0, 0, originalWindow.right - originalWindow.left,
             originalWindow.bottom - originalWindow.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
if (!characterAsset.empty())
{
    gridSnap = true;
    place_character({1.23, 0, 2.27});
    check("starter placement applies grid snap and scene bounds", actor() && actor()->X == 1. && actor()->Z == 2.5);
    place_character({200, 0, -200});
    check("starter placement remains inside terrain", actor()->X <= 12 && actor()->Z >= -12);
    auto count = scene.Characters.size();
    SendMessage(characterCard, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
    escape();
    SendMessage(characterCard, WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
    check("Escape cancels captured starter drag without placement", !cardDragging && scene.Characters.size() == count);
    command(Undo);
    command(Undo);
}
else
    check("bundled starter asset is available", false);
inspectorShown = assetsShown = false;
toggle_menu(0);
ShowWindow(morePanel, SW_HIDE);
ShowWindow(musicPanel, SW_HIDE);
SendMessage(mode, CB_SETCURSEL, 0, 0);
command(Mode);
command(FullScreen);
pendingPour = false;
escape();
check("Escape cancels armed pour before fullscreen", fullScreen && !pendingPour);
toggle_menu(MenuBuilder);
escape();
check("Escape closes popup before fullscreen", fullScreen && !openMenu);
show_inspector(5);
escape();
check("Escape closes settings before fullscreen", fullScreen && !inspectorShown);
escape();
check("Escape exits fullscreen only after suboperations are closed", !fullScreen);
auto authored = scene.encode();
auto canvas = view;
auto session = renderer.get();
command(Play);
check("windowed Play retains editor canvas without allocating a preview", !gamePreview && view == canvas && renderer.get() == session);
bool live = pump_until([&] { return renderer->status().frames >= 3 && renderer->status().seconds > .05; }, 30000);
check("inline playback advances without changing authored scene", live && scene.encode() == authored);
command(Pause);
pump_until([&] { return !renderer->status().playing; });
double paused = renderer->status().seconds;
auto frame = renderer->status().frames;
pump_until([&] { return renderer->status().frames > frame + 2; });
check("inline pause freezes simulation time", renderer->status().seconds == paused);
command(Play);
check("inline resume advances from paused time", pump_until([&] { return renderer->status().seconds > paused; }));
command(FullScreen);
check("fullscreen transition retains active canvas and session", view == canvas && renderer.get() == session && !gamePreview && renderer->status().playing);
command(Stop);
check("inline reset restores zero simulation time", pump_until([&] { return renderer->status().seconds == 0 && !renderer->status().playing; }));
for (int n = 0; n < 4; ++n) {
    command(Play);
    command(Pause);
}
check("repeated inline Play preserves scene and renderer", !gamePreview && renderer.get() == session && scene.encode() == authored);
command(Stop);
command(FullScreen);
show_inspector(5);
