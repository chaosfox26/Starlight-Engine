#include "motion_trace.hpp"
#include "solid_fixture.hpp"
#include "../../tools/audit/display_test_sizes.hpp"
#include "../../deep/engine/crash_diagnostics.h"

#include "commands.hpp"
#include "audit_window_trace.hpp"
#include "audit_input_runtime.hpp"

#include "interactions.hpp"
#include "mesh.hpp"
#include "mesh_reduce.hpp"
#include "music.hpp"

#include "native_session.hpp"
#include "game_preview.hpp"
#include "edge_feedback.hpp"
#include "features.hpp"

#include "projects.hpp"
#include "object_template.hpp"
#include "asset_library.hpp"
#include "engine_assets.hpp"
#include "error_throttle.hpp"
#include "adventure.hpp"
#include "scene_preparation.hpp"
#include "player_presentation.hpp"
#include "gameplay_audio.hpp"
#include "movement_hud.hpp"
#include "pacing_hud.hpp"
#include "combat_hud.hpp"
#include "quest_tracker.hpp"
#include "object_behaviors.hpp"
#include "startup_activation.hpp"
#include "boot_splash.hpp"
#include "adventure_template.hpp"
#include "tutorial_progress.hpp"

#include <commctrl.h>

#include <functional>
#include <cwctype>
#include <fstream>
#include <psapi.h>
#include <atomic>
#include <thread>
#include <filesystem>
#include <iomanip>

#include <future>

#include <map>
#include <unordered_set>

#include <numbers>

#include <sstream>

#include "editor_style.hpp"
#include "menu_timing.hpp"
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")

using namespace starlight::editor;

namespace {

enum : int {
    New = 100,
    Open,
    Save,
    SaveAs,
    Import,
    Undo,
    Redo,
    Delete,
    Duplicate,
    Play,
    Pause,
    Stop,
    Frame,
    Add,
    Apply,
    Mode,
    Shape,
    Brush,
    ObjectList,
    InspectorTab,
    Repeat,
    Follow,
    Projects,
    ChooseProject,
    ChoosePreset,
    CheckPreset,
    SavePreset,
    LaunchProject,
    MusicOpen,
    MusicPlay,
    MusicPause,
    MusicStop,
    MusicNext,
    MusicQuieter,
    MusicLouder,
    FullScreen,
    TogglePacing,
    PourWater,
    PourLava,
    Stamp,
    ViewAbove,
    ViewSide,
    ViewLow,
    ViewFocus,
    MenuFile = 1000,
    MenuBuilder,
    MenuAssets,
    MenuWorld,
    MenuCamera,
    MenuMore,
    CloseInspector,
    CloseDock,
    ObjectProperties,
    BuildTerrain,
    BuildShapes,
    BuildNature,
    BuildSculpt,
    DockRadius,
    DockStrength, Advanced, BuildDetails, BodyDetails, PlaceCharacter, ClearRoute,
    ClearPours, DockSettings, ApplyExact, ApplyBuild, ApplyBody, ApplyLightning, ApplyParticles, ApplyExtras,
    SaveObject, LoadObject, ExportPortable, SaveGroup, LoadGroup, ReplaceModel, MergeScene, ExportSelection,
    BrowseAssets, CloseAssets, ChooseAssets, RefreshAssets, SearchAssets, AssetList, AssetType, InsertAsset,
    CopyObjects, PasteObjects, AdventurePreset, ApplyGameplay, ExportGame, NewAdventure, InteractiveBuild, CheckGameplay, HelpOpen, HelpClose, HelpLesson, HelpBack, HelpNext, HelpRestart, HelpSearch, AddGameplayRule, RemoveGameplayRule, SaveQuest, LoadQuest, RemoveQuest, NewQuestAdventure, ObjectBehaviorMenu, BehaviorOpen, BehaviorBreak, BehaviorCollect, BehaviorNone, BehaviorQuest, BehaviorStandQuest, LearnOpen, LearnBreak, EndCreationGuide, BehaviorCarry, LearnWorkshop
};

static const char* volatile gCrumb="";static volatile unsigned gMsg=0;
template <class F> auto open_with_context(const fs::path& path, const char* kind, F&& action) -> decltype(action()) {
    const std::string name = utf8(path.filename().wstring());
    try {
        return action();
    } catch (const ConversionRequired&) {
        throw std::runtime_error(std::string("Cannot open the ") + kind + " \"" + name + "\". It uses the old Starlight file format, where the geometry is stored inside the file. Convert it first with Starlight.SceneMigration, then open the converted copy. Opening never changes the file on disk, and nothing in the editor was changed.");
    } catch (const AssetMissing& missing) {
        throw std::runtime_error(std::string("Cannot open the ") + kind + " \"" + name + "\". An engine asset it needs is missing or damaged. Details: " + missing.what() + ". " + EngineAssets::describe_roots() + ". Register the library that holds the missing asset with Starlight.NativeCommands --asset-root-add <folder> (or reconnect the drive it lives on) and open it again. The file on disk and the editor were not changed.");
    }
}
struct Editor {
    std::optional<AdventurePlayer> adventure;
    std::optional<AdventurePlayer> explorer;
    bool explorerLook=false;
    bool explorerNeedsRefresh=false;
    bool adventureLook=false;
    std::string adventureHint;
    std::string gameplayReport;
    HWND adventureHud{},adventureHudPanel{};
    QuestTracker questTracker;
    MovementHud movementHud;
    PacingHud pacingHud;
    bool pacingShown=true;
    bool gameplayPresentationActive=false;
    std::chrono::steady_clock::time_point motionTick=std::chrono::steady_clock::now();
    ULONGLONG hudTick=0;
    GameplayAudio gameplayAudio;
    CombatHud combatHud;
    bool mockBattle=false,mockClimb=false,mockPush=false;
    PreparationReport lastPreparation{};
    bool loadingActive=false;
    uint64_t installedScene=0,verifiedScene=0;
    bool mockStress=false;std::string stressTarget;double stressAmp=.5,stressAmpBreak=.1,stressHeading=0,stressFireSim=0,stressPhaseA=25,stressHomeX=0,stressHomeZ=0;uint64_t stressLastBlocked=0;bool stressAimRocks=false;double stressCam[8]={.55,41,-.62,.2,29,9,2.5,53};uint64_t stressFrame=0;
    struct StressKey{double t,yaw,pitch,dist;};std::vector<StressKey> stressRoute;int stressExpected=0,stressBreaksSeen=0,stressSeg=-1;std::vector<std::string> stressIds;
    double stressLastBreakSim=-1;ULONGLONG stressLastBreakWall=0;
    struct StressStats{double simStart=-1,simLast=0,pathLength=0,bossStartX=0,bossStartZ=0,bossMaxDisp=0,bossMinHealth=1e18,bossStartHealth=0,playerMinHealth=1e18,playerMaxHealth=0;
        size_t fragPeak=0,dustPeak=0,fragPeakLate=0;int telegraphTicks=0,ticks=0;bool havePos=false;Vec3 last{};}stressStats;

    CreationGuide creationGuide;std::string behaviorTarget,carriedId;Vec3 carryPoint{};
    bool startupActivated=false;
    bool deferStartupActivation=false;
    ULONG_PTR graphicsToken = 0;
    std::unique_ptr<Gdiplus::Image> logo;
    HWND tips = nullptr, dock = nullptr, playbackPanel = nullptr, propertiesPanel = nullptr,
         assetPanel = nullptr, menuPanel = nullptr;
    std::vector<HWND> menuControls, dockControls;
    HWND playButton = nullptr, playState = nullptr, radiusSlider = nullptr, strengthSlider = nullptr,
         speedReadout = nullptr;
    int dockSection = BuildTerrain, dockWidth = 642;
    uint64_t telemetryFrames = 0;
    ULONGLONG telemetryTick = 0;
    bool inspectorShown = false, assetsShown = false, dockShown = false;
    int openMenu = 0;
    bool advanced = false, buildDetails = false, bodyDetails = false, arrowPan = false;
    int paintMaterial = 0, formRows = 0;
    bool rebuildingInspector = false;
    std::optional<bool> pendingPour;
    fs::path characterAsset;
    std::string characterModelId;
    std::string defaultCharacterProblem;
    HWND characterCard = nullptr, inspectorTitle = nullptr;
    std::unordered_set<std::string> choices;
    std::unordered_map<std::string, double> sliders;
    LocalMusic music;
    std::vector<fs::path> songs;
    size_t songIndex = 0;
    double musicVolume = .5, flattenHeight = 0;
    std::optional<TerrainData> terrainStrokeSurface;
    std::optional<Vec3> terrainVolumeStamp;
    double terrainRadius = 2, terrainStrength = 1, sculptRadius = .45, sculptStrength = .65;
    bool fullScreen = false, gridSnap = false, objectSnap = false, groundSnap = false;
    bool rotationSnap=false;
    double gridStep=.5,rotationStep=15;
    WINDOWPLACEMENT placement{sizeof(placement)};
    int dragAxis = -1;
    std::unordered_set<std::string> selectionIds;
    std::unordered_map<std::string, Character> dragOriginals;
    bool boxSelecting = false;
    POINT boxStart{}, boxEnd{};
    Vec3 dragFloor{};

    HWND window = nullptr, view = nullptr, list = nullptr, mode = nullptr, shape = nullptr, brush = nullptr,
         status = nullptr, inspector = nullptr, tab = nullptr;

    HINSTANCE instance;
    HFONT font = nullptr, smallFont = nullptr, brandFont = nullptr, cardFont = nullptr;
    HFONT settingsFont = nullptr;
    int settingsScale = 100;
    int interfacePercent = 100;
    int manualInterfacePercent = 100;
    bool changingInterfaceScale = false;
    int requested_interface_scale(UINT dpi) const {
        if(interfacePercent)return MulDiv(interfacePercent,int(dpi),96);
        RECT bounds{0,0,width,height};if(window)GetClientRect(window,&bounds);
        const double fit=std::min(bounds.right*96./dpi/1280.,bounds.bottom*96./dpi/800.);
        const int previous=MulDiv(settingsScale,96,int(dpi));
        // Five-percent bands and hysteresis avoid jitter while resizing.
        int percent=std::clamp(int(std::lround(fit*20))*5,25,100);
        if(previous>=25 && previous<=100 && std::abs(fit*100-previous)<4)percent=previous;
        return MulDiv(percent,int(dpi),96);
    }
    void apply_interface_scale(int next) {
        if(next==settingsScale || changingInterfaceScale)return;
        const int previous=settingsScale;settingsScale=next;
        changingInterfaceScale=true;
        try { scale_settings_controls(previous);scale_interface_controls(previous); }
        catch(...) { changingInterfaceScale=false;throw; }
        changingInterfaceScale=false;
    }
    fs::path default_character_file() const { return application_data()/L"default-character.json"; }
    void load_default_character() {
        characterAsset.clear();
        characterModelId.clear();
        defaultCharacterProblem.clear();
        std::vector<fs::path> files{default_character_file()};
        try {
            for (const auto &status : EngineAssets::root_status())
                if (status.Origin == "shipped library") files.push_back(status.Path.parent_path() / L"default-character.json");
        } catch (const std::exception &) {}
        std::string problem;
        for (const auto &file : files) {
            std::error_code ec;
            if (!fs::is_regular_file(file, ec)) continue;
            std::string id;
            try {
                const auto settings = json::parse(read_file(file));
                if (settings.at("format").get<std::string>() != "starlight-default-character" || settings.at("version").get<int>() != 1)
                    throw std::runtime_error("unsupported format or version");
                id = settings.at("model").get<std::string>();
                if (!EngineAssets::valid_id(AssetKind::Model, id)) throw std::runtime_error("\"model\" is not a model asset id");
                if (!model_available(id)) {
                    std::string roots;
                    try { roots = EngineAssets::describe_roots(); } catch (const std::exception &e) { roots = e.what(); }
                    throw std::runtime_error("the model " + id + " is not in any asset library on this machine. Register the library that holds it with Starlight.NativeCommands --asset-root-add <folder>, or choose another character with Import character. " + roots);
                }
                characterModelId = id;
                characterAsset = wide(settings.value("name", std::string("Character")));
                problem.clear();
                break;
            } catch (const std::exception &e) {
                problem = "The default character setting " + utf8(file.wstring()) + " cannot be used: " + e.what();
            }
        }
        defaultCharacterProblem = problem;
    }
    void save_default_character() {
        if (interface_audit() || characterModelId.empty()) return;
        try {
            atomic_write(default_character_file(), json{{"format", "starlight-default-character"}, {"version", 1}, {"model", characterModelId}, {"name", utf8(characterAsset.stem().wstring())}}.dump(2));
            defaultCharacterProblem.clear();
        } catch (const std::exception &e) { diagnostic("default character", e.what()); }
    }
    bool interface_audit() const { return wcsstr(GetCommandLineW(),L"--ui-")!=nullptr; }
    fs::path interfacePreferenceOverride; // Isolated process-relaunch acceptance fixture.
    fs::path interface_preference_path() const { return interfacePreferenceOverride.empty()?application_data()/L"interface-size.json":interfacePreferenceOverride; }
    void save_interface_size() {
        if(!interface_audit())atomic_write(interface_preference_path(),json{{"version",1},{"percent",interfacePercent},{"manualPercent",manualInterfacePercent}}.dump());
    }
    int settings_px(int value) const { return MulDiv(value, settingsScale, 100); }
    std::map<HWND,POINT> panelScroll;
    void unscroll_panel(HWND host) {
        auto pos=panelScroll[host];style::scrollChildren(host,pos.x,pos.y);
    }
    void scroll_panel(HWND host,int x,int y) {
        SCROLLINFO sx{sizeof(sx),SIF_ALL},sy{sizeof(sy),SIF_ALL};
        GetScrollInfo(host,SB_HORZ,&sx);GetScrollInfo(host,SB_VERT,&sy);
        POINT next{std::clamp(x,0,std::max(0,sx.nMax-int(sx.nPage)+1)),std::clamp(y,0,std::max(0,sy.nMax-int(sy.nPage)+1))};
        auto old=panelScroll[host];style::scrollChildren(host,old.x-next.x,old.y-next.y);
        panelScroll[host]=next;SetScrollPos(host,SB_HORZ,next.x,TRUE);SetScrollPos(host,SB_VERT,next.y,TRUE);
    }
    void finish_scroll_panel(HWND host,int x,int y,int contentWidth,int contentHeight) {
        // Large controls keep their real size; constrained panels expose overflow.
        const auto old=panelScroll[host];panelScroll[host]={};
        SetWindowLongPtrW(host,GWL_STYLE,GetWindowLongPtrW(host,GWL_STYLE)|WS_VSCROLL|WS_HSCROLL);
        MoveWindow(host,x,y,std::min(contentWidth+GetSystemMetrics(SM_CXVSCROLL),std::max(1,width-x-12)),std::min(contentHeight+GetSystemMetrics(SM_CYHSCROLL),std::max(1,height-y-settings_px(40))),TRUE);
        RECT area{};GetClientRect(host,&area);
        SCROLLINFO sx{sizeof(sx),SIF_RANGE|SIF_PAGE},sy{sizeof(sy),SIF_RANGE|SIF_PAGE};
        sx.nMax=contentWidth-1;sx.nPage=area.right;sy.nMax=contentHeight-1;sy.nPage=area.bottom;
        SetScrollInfo(host,SB_HORZ,&sx,TRUE);SetScrollInfo(host,SB_VERT,&sy,TRUE);
        GetClientRect(host,&area);sx.nPage=area.right;sy.nPage=area.bottom;
        SetScrollInfo(host,SB_HORZ,&sx,TRUE);SetScrollInfo(host,SB_VERT,&sy,TRUE);
        scroll_panel(host,old.x,old.y);
    }
    void scale_interface_controls(int previousScale) {
        std::map<HFONT,HFONT> replacements;
        for(auto handle:{&font,&smallFont,&brandFont,&cardFont,&projectHeading}) {
            if(!*handle)continue;
            LOGFONTW description{};GetObjectW(*handle,sizeof(description),&description);
            description.lfHeight=-settings_px(handle==&brandFont?15:handle==&cardFont?32:handle==&projectHeading?27:12);
            auto replacement=CreateFontIndirectW(&description);
            if(!replacement)throw std::runtime_error("Cannot scale interface font");
            replacements[*handle]=replacement;*handle=replacement;
        }
        std::vector<HWND> children;
        auto collect=[](HWND h,LPARAM data)->BOOL {
            reinterpret_cast<std::vector<HWND>*>(data)->push_back(h);return TRUE;
        };
        EnumChildWindows(window,collect,reinterpret_cast<LPARAM>(&children));
        if(projectWindow)EnumChildWindows(projectWindow,collect,reinterpret_cast<LPARAM>(&children));
        for(auto child:children) {
            if(!GetPropW(child,L"sl.uiControl") || child==view || IsChild(inspector,child))continue;
            auto oldFont=reinterpret_cast<HFONT>(SendMessage(child,WM_GETFONT,0,0));
            if(replacements.contains(oldFont))SendMessage(child,WM_SETFONT,reinterpret_cast<WPARAM>(replacements.at(oldFont)),TRUE);
            RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,GetParent(child),reinterpret_cast<POINT*>(&r),2);
            wchar_t cls[32]{};GetClassNameW(child,cls,32);
            const bool combo=std::wstring_view(cls)==L"COMBOBOX";
            MoveWindow(child,MulDiv(r.left,settingsScale,previousScale),MulDiv(r.top,settingsScale,previousScale),
                MulDiv(r.right-r.left,settingsScale,previousScale),combo?settings_px(260):MulDiv(r.bottom-r.top,settingsScale,previousScale),TRUE);
            if(combo){SendMessage(child,CB_SETITEMHEIGHT,-1,settings_px(26));SendMessage(child,CB_SETITEMHEIGHT,0,settings_px(28));}
            if(std::wstring_view(cls)==L"LISTBOX")SendMessage(child,LB_SETITEMHEIGHT,0,settings_px(child==list?42:24));
        }
        for(auto [oldFont,replacement]:replacements)DeleteObject(oldFont);
        dockWidth=MulDiv(dockWidth,settingsScale,previousScale);
        dockScroll=MulDiv(dockScroll,settingsScale,previousScale);
        assetScroll=MulDiv(assetScroll,settingsScale,previousScale);
        mainScroll=MulDiv(mainScroll,settingsScale,previousScale);
        projectScroll=MulDiv(projectScroll,settingsScale,previousScale);
        for(auto& [host,offset]:panelScroll){offset.x=MulDiv(offset.x,settingsScale,previousScale);offset.y=MulDiv(offset.y,settingsScale,previousScale);}
        layout();if(projectWindow)layout_project();
    }
    void scale_settings_controls(int previousScale) {
        auto nextFont = CreateFontW(-settings_px(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
            DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if(!nextFont)throw std::runtime_error("Cannot create settings font");
        const int nextScroll=MulDiv(scroll,settingsScale,previousScale);
        for(auto child=GetWindow(inspector,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            RECT r{};GetWindowRect(child,&r);MapWindowPoints(nullptr,inspector,reinterpret_cast<POINT*>(&r),2);
            wchar_t cls[32]{};GetClassNameW(child,cls,32);
            const bool combo=std::wstring_view(cls)==L"COMBOBOX";
            MoveWindow(child,MulDiv(r.left,settingsScale,previousScale),
                MulDiv(r.top+scroll,settingsScale,previousScale)-nextScroll,
                MulDiv(r.right-r.left,settingsScale,previousScale),
                combo?settings_px(330):MulDiv(r.bottom-r.top,settingsScale,previousScale),TRUE);
            SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(nextFont),TRUE);
            if(combo) {
                SendMessage(child,CB_SETITEMHEIGHT,-1,settings_px(26));
                SendMessage(child,CB_SETITEMHEIGHT,0,settings_px(28));
            }
        }
        if(settingsFont)DeleteObject(settingsFont);
        settingsFont=nextFont;scroll=nextScroll;
        SCROLLINFO range{sizeof(range),SIF_RANGE|SIF_POS};
        range.nMax=settings_px(formRows*35+16);range.nPos=scroll;
        SetScrollInfo(inspector,SB_VERT,&range,TRUE);
    }
    HICON appIcon = nullptr;
    HBRUSH background = CreateSolidBrush(RGB(19, 25, 40));
    std::vector<HWND> top, fields;
    std::map<std::string, HWND> inputs;

    std::optional<ProjectData> project;
    std::optional<ResolvedPreset> preset;
    std::future<ResolvedPreset> presetTask;
    int presetAction = 0;

    SceneData scene;
    SceneHistory history;
    std::unique_ptr<NativeSession> renderer;
    fs::path filename;
    std::optional<std::string> diskHash;
    bool userMessage = false;         // the message being handled comes from a person's action, so its errors are never throttled
    ErrorThrottle errorThrottle;      // a failure that repeats on every tick is reported once per window (audit ED-06)
    bool errorDialogOpen = false;     // the modal error box runs its own message loop, which delivers timer ticks that can fail again
    int selected = -1;
    bool dirty = false, playing = false, repeat = false, follow = false, stroke = false, orbit = false;
    POINT last{};
    int width = 1380, height = 900, scroll = 0, assetScroll = 0;
    double yaw = 0, pitch = std::atan(.72), distance = 13, focusX = 0, focusY = 0, focusZ = 0, panSpeed = 1,
           tiltSpeed = 1, radius = 2, strength = 1;
    std::string currentMode = "Select", currentBrush = "pull";
    ULONGLONG lastTick = GetTickCount64(), brushTick = 0, watchTick = 0;
    bool reverseBrush = false;
    std::optional<fs::file_time_type> watchedTime;
    bool watchMissing = false;        // the open scene file has disappeared: said once, not re-reported every second

    #define playback_command playback_command_prepared
    #include "classic_completion.inl"
    #undef playback_command
    void playback_command(int id) {
        if (id == Play && !playing && !scene.Game && verifiedScene != installedScene) {
            LoadingScreen loading;
            if (!settle_scene(loading, "Getting your scene ready")) return;
        }
        LoadingGuard guard(*this);
        playback_command_prepared(id);
    }
    #include "asset_browser.inl"
    #include "selection_clipboard.inl"
    #include "reference_menu.inl"
    #include "tutorials.inl"
    EdgeFeedback edgeFeedback;

    explicit Editor(HINSTANCE h) : instance(h) {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&graphicsToken, &input, nullptr) != Gdiplus::Ok)
            throw std::runtime_error("Cannot initialize native interface drawing");
    }

    ~Editor() {
        libraryCancel = true;
        if (libraryTask.valid()) libraryTask.wait();
        gamePreview.reset();
        if(projectWindow && IsWindow(projectWindow)) DestroyWindow(projectWindow);
        renderer.reset();
        if(projectHeading)DeleteObject(projectHeading);
        if (font)
            DeleteObject(font);
        if (settingsFont)
            DeleteObject(settingsFont);
        if (smallFont)
            DeleteObject(smallFont);
        if (brandFont)
            DeleteObject(brandFont);
        if (cardFont)
            DeleteObject(cardFont);
        if (appIcon)
            DestroyIcon(appIcon);
        DeleteObject(background);
        logo.reset();
        Gdiplus::GdiplusShutdown(graphicsToken);
    }

    // Messages that mean a person just did something: their errors are always shown, while a failure on any other message (timer, frame, paint) can repeat by itself and stays throttled (audit ED-06).
    static bool user_action(UINT m, LPARAM l = 0) {
        if (m == WM_KEYDOWN || m == WM_SYSKEYDOWN) return !(l & (LPARAM(1) << 30));   // a held key repeating is not a new action
        return m == WM_COMMAND || m == WM_SYSCOMMAND || m == WM_CLOSE || m == WM_LBUTTONDOWN || m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_DROPFILES;
    }

    static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<Editor *>(GetWindowLongPtr(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<Editor *>(reinterpret_cast<CREATESTRUCT *>(l)->lpCreateParams);
            self->window = h;
            SetWindowLongPtr(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        try {
            if (self) {
                struct Mark { bool &flag; bool saved; ~Mark() { flag = saved; } } mark{self->userMessage, self->userMessage};
                self->userMessage = user_action(m, l);
                return self->message(m, w, l);
            }
        } catch (const std::exception &e) {
            if (self)
                self->report(e.what(), m, l);
        }
        return DefWindowProc(h, m, w, l);
    }

    static LRESULT CALLBACK viewport_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Editor *>(data);
        try {
            if(m==WM_KILLFOCUS){if(self->adventure)self->adventure->cancel_mobility_input();if(self->explorer)self->explorer->cancel_mobility_input();}
            if(m==WM_KEYUP && w=='G'){auto mover=self->adventure?&*self->adventure:self->explorer?&*self->explorer:nullptr;if(mover)mover->grapple_button(false);return 0;}
            if(m==WM_KEYUP && AbilitySlots::recognizes(static_cast<unsigned>(w)) && self->playing && self->adventure){self->adventure->ability_button(static_cast<unsigned>(w),false);return 0;}
            if(m==WM_KEYUP && w=='V'){
                auto& mover=self->playing?self->adventure:self->explorer;
                if(mover)mover->mobility_button(false,0,0);
                return 0;
            }
            if(m==WM_KEYUP && w==VK_CONTROL){
                auto& mover=self->playing?self->adventure:self->explorer;
                if(mover)mover->posture_button(false,0,0);
                return 0;
            }
            if (m == WM_MOUSEWHEEL) {
                POINT pointer{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                RECT popup{};
                if ((self->openMenu == MenuFile || self->openMenu == MenuWorld || self->openMenu == ObjectBehaviorMenu || self->openMenu == MenuBuilder) && IsWindowVisible(self->menuPanel) &&
                    GetWindowRect(self->menuPanel, &popup) && PtInRect(&popup, pointer)) {
                    self->scroll_main(self->mainScroll - GET_WHEEL_DELTA_WPARAM(w) / 120 * 60);
                    return 0;
                }
                if(self->explorer || (self->playing && self->adventure)){
                    auto& mover=self->playing?self->adventure:self->explorer;
                    if(mover)mover->zoom(GET_WHEEL_DELTA_WPARAM(w)/120.);
                    self->camera();return 0;
                }
                self->distance =
                    std::clamp(self->distance * std::pow(.85, GET_WHEEL_DELTA_WPARAM(w) / 120.), 1., 150.);
                self->camera();
                return 0;
            }
            if (m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN || m == WM_MBUTTONDOWN) {
                if (self->openMenu) {
                    self->toggle_menu(0);
                    return 0;
                }
                SetFocus(h);
                SetCapture(h);
                self->pointer_down(GET_X_LPARAM(l), GET_Y_LPARAM(l), m);
                return 0;
            }
            if (m == WM_MOUSEMOVE) {
                self->pointer_move(GET_X_LPARAM(l), GET_Y_LPARAM(l), w);
                return 0;
            }
            if (m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP) {
                self->end_stroke();
                ReleaseCapture();
                return 0;
            }
            if (m == WM_CAPTURECHANGED) {
                self->end_stroke();
                return 0;
            }
            if (m == WM_KEYDOWN) {
                if(!(l & (1LL<<30))){
                    auto& activeMover=self->playing?self->adventure:self->explorer;
                    auto mover=activeMover?&*activeMover:nullptr;
                    if(mover){
                        mover->movement_press(int(w),GetTickCount64()*.001);
                        if(w==VK_CONTROL){mover->posture_button(true,((GetAsyncKeyState('D')&0x8000)?1.:0)-((GetAsyncKeyState('A')&0x8000)?1.:0),((GetAsyncKeyState('W')&0x8000)?1.:0)-((GetAsyncKeyState('S')&0x8000)?1.:0));return 0;}
                        if(w=='G'){mover->grapple_button(true);return 0;}
                        if(self->playing && AbilitySlots::recognizes(static_cast<unsigned>(w))){mover->ability_button(static_cast<unsigned>(w),true);return 0;}
                        if(self->playing && w==VK_TAB){mover->select_target();return 0;}
                        if(w=='V'){mover->mobility_button(true,((GetAsyncKeyState('D')&0x8000)?1.:0)-((GetAsyncKeyState('A')&0x8000)?1.:0),((GetAsyncKeyState('W')&0x8000)?1.:0)-((GetAsyncKeyState('S')&0x8000)?1.:0));return 0;}
                    }
                }
                if(w==VK_SPACE && !(l & (1LL<<30))){
                    if(self->playing && self->adventure){self->adventure->jump();return 0;}
                    if(self->explorer){self->explorer->jump();return 0;}
                }
                if(w=='F' && !self->playing && !(l & (1LL<<30))){self->open_object_behavior();return 0;}
                if((w=='E' || w=='F') && self->playing && self->adventure && !(l & (1LL<<30))){
                    const auto before=self->adventure->scene().Characters.size();
                    self->adventureHint=self->adventure->interact();
                    if(self->adventure->scene().Characters.size()!=before)
                        self->renderer->scene(self->adventure->visible_scene(),true);
                    SetWindowTextW(self->adventureHud,wide(self->adventureHint).c_str());
                    return 0;
                }
                if (w == VK_ESCAPE) {
                    self->escape();
                    return 0;
                }
                if (w == VK_DELETE) {
                    self->command(Delete);
                    return 0;
                }
            }
        } catch (const std::exception &e) {
            self->history.cancel(self->scene);
            self->stroke = self->orbit = false;
            ReleaseCapture();
            self->report(e.what(), m, l);
        }
        return DefSubclassProc(h, m, w, l);
    }

    static LRESULT CALLBACK inspector_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Editor *>(data);
        if (m == WM_ERASEBKGND) return 1;
        if (m == WM_PAINT) {
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            style::childBackground(h, dc);
            EndPaint(h, &ps); return 0;
        }
        if (m == WM_COMMAND || m == WM_HSCROLL)
            return SendMessage(self->window, m, w, l);
        if (m == WM_DRAWITEM || m == WM_MEASUREITEM || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX)
            return SendMessage(self->window, m, w, l);
        if (m == WM_CTLCOLORSTATIC) {
            SetTextColor(reinterpret_cast<HDC>(w), RGB(225, 232, 246));
            SetBkColor(reinterpret_cast<HDC>(w), style::panel);
            return reinterpret_cast<LRESULT>(style::panelBrush());
        }
        if (m == WM_VSCROLL || m == WM_MOUSEWHEEL) {
            MenuTiming timing("inspector-scroll");
            int next = self->scroll;
            if (m == WM_MOUSEWHEEL)
                next -= GET_WHEEL_DELTA_WPARAM(w) / 120 * 60;
            else {
                SCROLLINFO si{sizeof(si), SIF_ALL};
                GetScrollInfo(h, SB_VERT, &si);
                switch (LOWORD(w)) {
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
                case SB_TOP: next = 0; break;
                case SB_BOTTOM: next = si.nMax; break;
                case SB_THUMBTRACK:
                    next = si.nTrackPos;
                    break;
                }
            }
            self->scroll_inspector(next);
            return 0;
        }
        return DefSubclassProc(h, m, w, l);
    }

    void scroll_inspector(int position) {
        SCROLLINFO range{sizeof(range), SIF_RANGE | SIF_PAGE};
        GetScrollInfo(inspector, SB_VERT, &range);
        const int next = std::clamp(position, 0, std::max(0, range.nMax - int(range.nPage) + 1));
        style::scrollChildren(inspector, 0, scroll - next);
        scroll = next;
        SetScrollPos(inspector, SB_VERT, next, TRUE);
    }

    void scroll_assets(int position) {
        SCROLLINFO range{sizeof(range), SIF_RANGE | SIF_PAGE};
        GetScrollInfo(assetPanel, SB_VERT, &range);
        int next = std::clamp(position, 0, std::max(0, range.nMax - int(range.nPage) + 1));
        style::scrollChildren(assetPanel, 0, assetScroll - next);
        assetScroll = next;
        SetScrollPos(assetPanel, SB_VERT, next, TRUE);
    }

    fs::path engine_path() {
        wchar_t path[32768];
        GetModuleFileNameW(nullptr, path, 32768);
        return fs::path(path).parent_path() / L"Starlight.exe";
    }

    void begin_preset(int action, const fs::path &source) {
        if (!project)
            throw std::runtime_error("Choose a project first");
        if (presetTask.valid())
            throw std::runtime_error("Preset check is already running");
        presetAction = action;
        auto engine = engine_path();
        auto p = *project;
        presetTask =
            std::async(std::launch::async, [engine, p, source] { return resolve_preset(engine, p, source); });
        SetWindowTextW(status, L"Checking project settings…");
    }

    void set_tooltip(HWND child, const wchar_t* text) {
        TOOLINFOW tool{};
        tool.cbSize = TTTOOLINFOW_V2_SIZE;
        tool.hwnd = window;
        tool.uId = reinterpret_cast<UINT_PTR>(child);
        const bool exists = SendMessageW(tips, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&tool)) != 0;
        tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        tool.lpszText = const_cast<wchar_t*>(text);
        SendMessageW(tips, exists ? TTM_UPDATETIPTEXTW : TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    HWND control(HWND parent, const wchar_t *cls, const wchar_t *text, int id, int x, int y, int w, int h,
                 DWORD style = 0) {
        bool checkbox = std::wstring_view(cls) == L"BUTTON" && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX;
        if (std::wstring_view(cls) == L"COMBOBOX")
            style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
        if (std::wstring_view(cls) != L"STATIC") style |= WS_TABSTOP;
        const bool scaleControl=parent!=inspector;
        auto child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                                     scaleControl?settings_px(x):x,scaleControl?settings_px(y):y,
                                     scaleControl?settings_px(w):w,scaleControl?settings_px(h):h,parent,
                                     reinterpret_cast<HMENU>(INT_PTR(id)), instance, nullptr);
        if (!child)
            throw std::runtime_error("Cannot create editor control");
        SetPropW(child,L"sl.uiControl",reinterpret_cast<HANDLE>(1));
        auto controlFont = (parent == dock || parent == playbackPanel || parent == menuPanel ||
                            (std::wstring_view(cls) == L"STATIC" && wcslen(text) > 0))
                               ? smallFont
                               : font;
        SendMessage(child, WM_SETFONT, reinterpret_cast<WPARAM>(controlFont), TRUE);
        if (std::wstring_view(cls) == L"STATIC" && wcslen(text) > 0)
            SetWindowSubclass(child, style::labelProc, 7, 0);
        starlight::editor::style::theme(child, checkbox);
        const wchar_t *tip = nullptr;
        switch (id) {
        case MenuFile:
            tip = L"Scene menu • open, save, projects";
            break;
        case MenuBuilder:
            tip = L"Builder • terrain, shapes, nature, sculpting";
            break;
        case MenuAssets:
            tip = L"Characters and objects in your scene";
            break;
        case MenuWorld:
            tip = L"Environment • fire, water, lava and lights";
            break;
        case MenuCamera:
            tip = L"Camera pan and tilt settings";
            break;
        case MenuMore:
            tip = L"More • music and full screen";
            break;
        case Play:
            tip = L"Preview your characters moving along the paths you drew.";
            break;
        case Pause:
            tip = L"Pause preview";
            break;
        case Stop:
            tip = L"Stop the preview and return characters to their starting positions.";
            break;
        case Repeat:
            tip = L"Start all routes again after the longest one finishes. Characters return to their starting positions.";
            break;
        case PlaceCharacter:
            tip = L"Add the chosen character to the middle of your scene.";
            break;
        case Import:
            tip = L"Add a character from a .glb file on your computer.";
            break;
        case ObjectProperties:
            tip = L"Change the selected character's name, size, or movement speed.";
            break;
        case Advanced:
            tip = L"Show or hide number controls for precise changes.";
            break;
        case 2200:
            tip = L"Move a character by dragging it across the floor. Its path moves too.";
            break;
        case 2300:
            tip = L"Choose a character, then drag across the floor to draw where it should go.";
            break;
        case ObjectList:
            tip = L"Choose which character you want to change.";
            break;
        case ClearRoute:
            tip = L"Erase this character's path. The character stays in the scene.";
            break;
        case Undo:
            tip = L"Undo";
            break;
        case Redo:
            tip = L"Redo";
            break;
        case CloseDock:
            tip = L"Hide building bar";
            break;
        case CloseInspector:
            tip = L"Close settings";
            break;
        }
        if (tips && tip) {
            set_tooltip(child, tip);
        }
        if (std::wstring_view(cls) == L"COMBOBOX") {
            SendMessage(child, CB_SETITEMHEIGHT, -1, scaleControl?settings_px(26):26);
            SendMessage(child, CB_SETITEMHEIGHT, 0, scaleControl?settings_px(28):28);
        }
        return child;
    }

    void create() {
        if(!interface_audit()) {
            interfacePercent=0;
            try {
                auto preference=json::parse(read_file(interface_preference_path()));
                int value=preference.at("percent").get<int>();
                if(preference.at("version")==1 && (value==0 || (value>=25 && value<=200)))interfacePercent=value;
                manualInterfacePercent=std::clamp(preference.value("manualPercent",interfacePercent?interfacePercent:100),25,200);
            }catch(...){}
        }
        settingsScale=requested_interface_scale(GetDpiForSystem());
        style::enableDarkApplication();
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&common);
        font = CreateFontW(-settings_px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        smallFont =
            CreateFontW(-settings_px(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        brandFont =
            CreateFontW(-settings_px(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        cardFont = CreateFontW(-settings_px(32), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"Segoe UI");
        WNDCLASSW wc{};
        wc.hInstance = instance;
        wc.lpfnWndProc = procedure;
        wc.lpszClassName = L"StarlightNativeEditor";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = background;
        RegisterClassW(&wc);
        window =
            CreateWindowW(wc.lpszClassName, L"Starlight Engine Editor", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                          CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr, instance, this);
        if (!window)
            throw std::runtime_error("Cannot create native editor");
        SetPropW(window, L"sl.root", reinterpret_cast<HANDLE>(1));
        BOOL dark = TRUE;
        DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        COLORREF caption = RGB(14, 20, 35);
        DwmSetWindowAttribute(window, 35, &caption, sizeof(caption));
        if(!deferStartupActivation)startupActivated=activate_startup_window(window);
        UpdateWindow(window);
    }

    static LRESULT CALLBACK panel_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Editor *>(data);
        if(self->panelScroll.contains(h) && (m==WM_VSCROLL || m==WM_HSCROLL || m==WM_MOUSEWHEEL)) {
            const bool horizontal=m==WM_HSCROLL || (m==WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(w)&MK_SHIFT));
            auto pos=self->panelScroll[h];int next=horizontal?pos.x:pos.y;
            SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(h,horizontal?SB_HORZ:SB_VERT,&si);
            if(m==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(w)/120*self->settings_px(60);
            else switch(LOWORD(w)) {
                case SB_LINEUP:next-=self->settings_px(30);break;case SB_LINEDOWN:next+=self->settings_px(30);break;
                case SB_PAGEUP:next-=si.nPage;break;case SB_PAGEDOWN:next+=si.nPage;break;
                case SB_THUMBTRACK:next=si.nTrackPos;break;case SB_TOP:next=0;break;case SB_BOTTOM:next=si.nMax;break;
            }
            self->scroll_panel(h,horizontal?next:pos.x,horizontal?pos.y:next);return 0;
        }
        if (m == WM_NCDESTROY) {
            self->panelScroll.erase(h);
            if (auto surface = reinterpret_cast<HWND>(RemovePropW(h, L"sl.panelSurface"))) DestroyWindow(surface);
        }
        if (m == WM_WINDOWPOSCHANGED)
            style::panelSurface(h, h == self->dock ? 29.f : 12.f);
        if (h == self->menuPanel && (self->openMenu == MenuFile || self->openMenu == MenuWorld || self->openMenu == ObjectBehaviorMenu || self->openMenu == MenuBuilder) && (m == WM_VSCROLL || m == WM_MOUSEWHEEL)) {
            SCROLLINFO si{sizeof(si), SIF_ALL}; GetScrollInfo(h, SB_VERT, &si);
            int next = self->mainScroll;
            if (m == WM_MOUSEWHEEL) next -= GET_WHEEL_DELTA_WPARAM(w) / 120 * 60;
            else switch (LOWORD(w)) {
                case SB_LINEUP: next -= 30; break; case SB_LINEDOWN: next += 30; break;
                case SB_PAGEUP: next -= si.nPage; break; case SB_PAGEDOWN: next += si.nPage; break;
                case SB_THUMBTRACK: next = si.nTrackPos; break;
                case SB_TOP: next = 0; break; case SB_BOTTOM: next = si.nMax; break;
            }
            self->scroll_main(next); return 0;
        }
        if (h == self->assetPanel && (m == WM_VSCROLL || m == WM_MOUSEWHEEL)) {
            SCROLLINFO si{sizeof(si), SIF_ALL}; GetScrollInfo(h, SB_VERT, &si);
            int next = self->assetScroll;
            if (m == WM_MOUSEWHEEL) next -= GET_WHEEL_DELTA_WPARAM(w) / 120 * 60;
            else switch (LOWORD(w)) {
                case SB_LINEUP: next -= 30; break; case SB_LINEDOWN: next += 30; break;
                case SB_PAGEUP: next -= int(si.nPage); break; case SB_PAGEDOWN: next += int(si.nPage); break;
                case SB_THUMBTRACK: next = si.nTrackPos; break;
                case SB_TOP: next = 0; break; case SB_BOTTOM: next = si.nMax; break;
            }
            self->scroll_assets(next); return 0;
        }
        if (h == self->dock && ((m == WM_HSCROLL && !l) || m == WM_MOUSEWHEEL)) {
            SCROLLINFO si{sizeof(si),SIF_ALL};GetScrollInfo(h,SB_HORZ,&si);int next=self->dockScroll;
            if(m==WM_MOUSEWHEEL)next-=GET_WHEEL_DELTA_WPARAM(w)/120*80;
            else switch(LOWORD(w)){case SB_LINELEFT:next-=44;break;case SB_LINERIGHT:next+=44;break;case SB_PAGELEFT:next-=int(si.nPage);break;case SB_PAGERIGHT:next+=int(si.nPage);break;case SB_THUMBTRACK:next=si.nTrackPos;break;case SB_LEFT:next=0;break;case SB_RIGHT:next=si.nMax;break;}
            self->scroll_dock(next);return 0;
        }
        if (m == WM_SIZE) {
            // Window regions include nonclient scrollbars; using the client
            // rectangle here clips away their entire bottom/right strip.
            RECT bounds; GetWindowRect(h, &bounds);
            OffsetRect(&bounds, -bounds.left, -bounds.top);
            // Native child controls occupy the opaque interior. The separate
            // alpha surface owns the entire antialiased outer border. Derive
            // clipping from the same path with a safe inset; integer Win32
            // round-rect rasterization has a different bottom-edge convention.
            Gdiplus::GraphicsPath interior;
            style::roundedPath(interior,
                {3.5f, 3.5f, float(std::max(1L, bounds.right - 7)),
                             float(std::max(1L, bounds.bottom - 7))},
                (h == self->dock ? 29.f : 12.f) - 3.f);
            HDC regionDc = GetDC(h);
            HRGN region = nullptr;
            if (regionDc) {
                Gdiplus::Graphics regionGraphics(regionDc);
                Gdiplus::Region interiorRegion(&interior);
                region = interiorRegion.GetHRGN(&regionGraphics);
            }
            ReleaseDC(h, regionDc);
            if (region && !SetWindowRgn(h, region, TRUE)) DeleteObject(region);
            style::panelSurface(h, h == self->dock ? 29.f : 12.f);
            InvalidateRect(h, nullptr, FALSE);
        }
        if (m == WM_HSCROLL || m == WM_COMMAND || m == WM_DRAWITEM || m == WM_MEASUREITEM ||
            m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT || m == WM_CTLCOLORLISTBOX)
            return SendMessage(self->window, m, w, l);
        if (m == WM_ERASEBKGND)
            return 1;
        if (m == WM_PAINT) {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT r;
            GetClientRect(h, &r);
            Gdiplus::Graphics g(dc);
            // The alpha sibling owns the rounded silhouette and border. Drawing
            // another antialiased outline into this integer-clipped host blends
            // against an opaque dark matte and leaves a cusp at the lower curve.
            // Paint only the matching interior gradient here.
            Gdiplus::RectF bounds(.5f, .5f, float(std::max(1L, r.right - 1)),
                                  float(std::max(1L, r.bottom - 1)));
            Gdiplus::LinearGradientBrush fill(bounds, Gdiplus::Color(255, 37, 44, 69),
                Gdiplus::Color(255, 28, 24, 48),
                float(std::atan2(double(bounds.Height), double(bounds.Width)) * 180 / 3.141592653589793));
            style::panelStops(fill);
            g.FillRectangle(&fill, Gdiplus::Rect(0, 0, r.right, r.bottom));
            EndPaint(h, &ps);
            return 0;
        }
        return DefSubclassProc(h, m, w, l);
    }
    HWND panel(int x, int y, int w, int h) {
        auto result = control(window, L"STATIC", L"", 0, x, y, w, h, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        SetWindowLongPtrW(result,GWL_EXSTYLE,GetWindowLongPtrW(result,GWL_EXSTYLE)|WS_EX_CONTROLPARENT);
        SetWindowSubclass(result, panel_proc, 3, reinterpret_cast<DWORD_PTR>(this));
        style::darkWindow(result); // Includes scrollbars added later by layout.
        // Creation's WM_SIZE predates the subclass. Establish the silhouette now,
        // including panels whose first layout keeps their original dimensions.
        SendMessageW(result, WM_SIZE, SIZE_RESTORED, MAKELPARAM(w, h));
        return result;
    }
    void show_inspector(int page) {
        inspectorShown = true;
        SendMessage(tab, CB_SETCURSEL, page, 0);
        inspector_fields();
        layout();
    }
    void dock_selection() {
        for (auto h : dockControls) {
            int id = GetDlgCtrlID(h);
            bool active = false;
            if (id >= 2200 && id < 2203)
                active = currentMode == std::array<std::string, 3>{"Move", "Rotate", "Scale"}[id - 2200];
            if (id >= 2100 && id < 2112)
                active = currentBrush == std::array<std::string, 12>{"pull",   "push",   "smooth", "flatten",
                                                                     "raise",  "lower",  "paint",  "hill",
                                                                     "crater", "plateau", "carve", "build"}[id - 2100];
            if (active)
                SetPropW(h, L"sl.checked", reinterpret_cast<HANDLE>(1));
            else
                RemovePropW(h, L"sl.checked");
            InvalidateRect(h, nullptr, FALSE);
        }
    }
    void destroy_control(HWND h) {
        // The tooltip subclass can normalize hwnd to the tool's parent. Enumerate
        // its stored identity rather than guessing it when nested panels retire.
        for (LRESULT i = SendMessageW(tips, TTM_GETTOOLCOUNT, 0, 0); i-- > 0;) {
            TOOLINFOW info{}; info.cbSize = TTTOOLINFOW_V2_SIZE;
            if (SendMessageW(tips, TTM_ENUMTOOLSW, i, reinterpret_cast<LPARAM>(&info)) &&
                (info.uFlags & TTF_IDISHWND) && info.uId == reinterpret_cast<UINT_PTR>(h))
                SendMessageW(tips, TTM_DELTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        }
        DestroyWindow(h);
    }
    void build_dock(int section) {
        if (dockSection == BuildTerrain) { terrainRadius = radius; terrainStrength = strength; }
        if (dockSection == BuildSculpt) { sculptRadius = radius; sculptStrength = strength; }
        if (section == BuildTerrain) { radius = terrainRadius; strength = terrainStrength; }
        if (section == BuildSculpt) { radius = sculptRadius; strength = sculptStrength; }
        for (auto h : dockControls)
            destroy_control(h);
        dockControls.clear();
        dockScroll=0;
        radiusSlider = strengthSlider = nullptr;
        dockSection = section;
        int x = 14;
        auto button = [&](int id, const wchar_t *label, int icon = -1, int w = 40) {
            auto h = control(dock, L"BUTTON", label, id, x, 11, w, 38);
            if (icon >= 0)
                SetPropW(h, L"sl.icon", reinterpret_cast<HANDLE>(INT_PTR(icon + 1)));
            // control() may already supply a semantic tooltip for this command.
            // Update that entry, rather than appending a second registration.
            set_tooltip(h, label);
            dockControls.push_back(h);
            x += w + 4;
        };
        if (section == BuildShapes) {
            button(2000, L"Place a cube", 10);
            button(2001, L"Place a rectangular block", 11);
            button(2002, L"Place a trapezoid", 12);
            button(2003, L"Place an eight-sided column", 13);
            button(2200, L"Move", 14);
            button(2201, L"Rotate", 15);
            button(2202, L"Resize", 16);
        } else if (section == BuildTerrain) {
            button(2104, L"Raise / lower", 0);
            button(2102, L"Smooth", 1);
            button(2103, L"Flatten", 2);
            button(2106, L"Paint", 3);
            button(2107, L"Hill", 4);
            button(2109, L"Plateau", 5);
            button(2108, L"Crater", 6);
            button(2110,L"Carve solid terrain",-1,135);
            button(2111,L"Build solid terrain",-1,135);
            button(PourWater, L"Place water pour on the ground", 7);
            button(PourLava, L"Place lava pour on the ground", 8);
            button(MenuWorld + 100, L"Water and lava settings", 9);
        } else if (section == BuildNature) {
            const wchar_t *names[] = {
                L"Rock",       L"Boulder",           L"Pebbles",     L"Grass tuft", L"Chaos Lightning",
                L"Chaos Rock", L"Light-Bound Shard", L"Craxis Smoke"};
            for (int n = 0; n < 8; n++)
                button(2007 + n, names[n], -1, wcslen(names[n]) > 12 ? 126 : 86);
        } else {
            button(2100, L"Pull", -1, 65);
            button(2101, L"Push", -1, 65);
            button(2102, L"Smooth", -1, 65);
            button(2103, L"Flatten", -1, 65);
            x += 8;
            dockControls.push_back(control(dock, L"STATIC", L"Brush radius / strength", 0, x, 6, 160, 18));
            radiusSlider = control(dock, TRACKBAR_CLASSW, L"Brush radius", DockRadius, x, 24, 145, 15,
                                   TBS_HORZ | TBS_NOTICKS);
            strengthSlider = control(dock, TRACKBAR_CLASSW, L"Brush strength", DockStrength, x, 40, 145, 15,
                                     TBS_HORZ | TBS_NOTICKS);
            SendMessage(radiusSlider, TBM_SETRANGE, TRUE, MAKELPARAM(8, 300));
            SendMessage(radiusSlider, TBM_SETPOS, TRUE, int(radius * 100));
            SendMessage(strengthSlider, TBM_SETRANGE, TRUE, MAKELPARAM(5, 200));
            SendMessage(strengthSlider, TBM_SETPOS, TRUE, int(strength * 100));
            dockControls.push_back(radiusSlider);
            dockControls.push_back(strengthSlider);
            x += 165;
        }
        button(Undo, L"Undo", 17);
        button(Redo, L"Redo", 18);
        if (section == BuildShapes || section == BuildTerrain)
            button(DockSettings, L"Settings", 19);
        button(CloseDock, L"Close", 20);
        dockWidth = settings_px(x + 10);
        dock_selection();
    }
    void toggle_menu(int id) {
        MenuTiming timing("menu-switch");
        if (openMenu == id)
            id = 0;
        for (auto h : menuControls)
            destroy_control(h);
        menuControls.clear();
        mainInputs.clear(); mainRects.clear();
        ShowScrollBar(menuPanel, SB_VERT, FALSE);
        openMenu = id;
        if (id == MenuFile) { build_reference_main(); layout(); return; }
        if (id == MenuWorld) { build_reference_effects(); layout(); return; }
        auto row = [&](int command, const wchar_t *label) {
            menuControls.push_back(control(menuPanel, L"BUTTON", label, command, 24,
                                           52 + (int(menuControls.size()) - 1) * 44, 195, 36));
        };
        if (id)
            menuControls.push_back(control(menuPanel, L"STATIC",
                                           id == ObjectBehaviorMenu ? L"OBJECT BEHAVIOR"
                                           : id == MenuBuilder ? L"BUILD"
                                           : id == MenuFile  ? L"STARLIGHT"
                                           : id == MenuWorld ? L"ENVIRONMENT"
                                                             : L"MORE",
                                           0, 24, 18, 195, 24));
        if(id==ObjectBehaviorMenu){
            row(BehaviorCarry,L"Carry / move");
            row(BehaviorOpen,L"Open (remove barrier)");row(BehaviorBreak,L"Breakable");row(BehaviorCollect,L"Collect into inventory");row(BehaviorNone,L"No interaction");
            row(BehaviorQuest,L"Add quest objective");row(BehaviorStandQuest,L"Add stand-on objective");row(ObjectProperties,L"More object properties");
        } else if (id == MenuFile) {
            row(FullScreen, L"Full screen");
            row(TogglePacing,L"Motion & frame timing (F8)");
            row(Frame, L"Frame world");
            row(MenuCamera, L"Camera settings");
            row(MenuWorld + 100, L"Atmosphere & ray tracing");
            row(New, L"New scene");
            row(Open, L"Open scene…");
            row(Save, L"Save");
            row(SaveAs, L"Save as…");
            row(Projects, L"Projects & presets");

        } else if (id == MenuBuilder) {
            row(BuildTerrain, L"Terrain");
            row(BuildShapes, L"Shapes");
            row(BuildNature, L"Nature");
            row(BuildSculpt, L"Sculpting");
            row(CloseDock, L"Hide building bar");
            row(LearnWorkshop,L"Learn: three blocks");
            row(LearnOpen,L"Learn: opening block");row(LearnBreak,L"Learn: breakable block");row(EndCreationGuide,L"Close creation lesson");
        } else if (id == BuildNature) {
            const wchar_t *names[] = {
                L"Rock",       L"Boulder",           L"Pebbles",     L"Grass tuft", L"Chaos Lightning",
                L"Chaos Rock", L"Light-Bound Shard", L"Craxis smoke"};
            for (int n = 0; n < 8; n++)
                row(2007 + n, names[n]);
        } else if (id == MenuWorld) {
            row(2004, L"Add fire");
            row(2006, L"Add lava pool");
            row(2005, L"Add light");
            row(PourWater, L"Pour water");
            row(PourLava, L"Pour lava");
            row(Stamp, L"Stamp selected shape");
            row(MenuWorld + 100, L"Lighting & world settings");
        } else if (id == MenuMore) {
            openMenu=0;ensure_secondary_panels();ShowWindow(morePanel,IsWindowVisible(morePanel)?SW_HIDE:SW_SHOW);
        }

        if(id==ObjectBehaviorMenu || id==MenuBuilder){
            mainScroll=0;mainContentHeight=74+std::max(0,int(menuControls.size())-1)*44;
            mainRects.push_back({24,18,219,42});
            for(size_t i=1;i<menuControls.size();++i){int y=52+int(i-1)*44;mainRects.push_back({24,y,219,y+36});}
        }
        layout();
    }
    void initialize() {
        tips = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP, 0, 0, 0, 0,
                               window, nullptr, instance, nullptr);
        if (!tips) throw std::runtime_error("Cannot create editor tooltips");
        SendMessage(tips, TTM_SETDELAYTIME, TTDT_INITIAL, 800);
        wchar_t executable[32768];
        GetModuleFileNameW(nullptr, executable, 32768);
        auto root = fs::path(executable);
        for (int n = 0; n < 4; n++)
            root = root.parent_path();
        wchar_t resourceRoot[32768];
        auto resourceLength = GetEnvironmentVariableW(L"STARLIGHT_RESOURCE_ROOT", resourceRoot, 32768);
        if (resourceLength && resourceLength < 32768 && fs::path(resourceRoot).is_absolute())
            root = resourceRoot;
        appIcon =
            static_cast<HICON>(LoadImageW(nullptr, (root / L"design/branding/starlight-editor.ico").c_str(),
                                          IMAGE_ICON, 32, 32, LR_LOADFROMFILE));
        if (appIcon) {
            SendMessage(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(appIcon));
            SendMessage(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(appIcon));
        }
        logo.reset(Gdiplus::Image::FromFile((root / L"design/branding/starlight-glass-icon-v1.png").c_str()));
        view = control(window, L"STATIC", L"", 0, 14, 62, 1200, 750, SS_NOTIFY | WS_CLIPSIBLINGS);
        SetWindowSubclass(view, viewport_proc, 1, reinterpret_cast<DWORD_PTR>(this));
        top.push_back(control(window, L"BUTTON", L"☰", MenuFile, 18, 18, 34, 30));
        for (auto [id, label] : std::vector<std::pair<int, const wchar_t *>>{
                 {MenuBuilder, L"▲"}, {MenuAssets, L"♟"}, {MenuWorld, L"☀"}, {MenuMore, L"◇"}})
            top.push_back(control(window, L"BUTTON", label, id, 0, 18, 36, 30));
        helpButton=control(window,L"BUTTON",L"Help / Tutorials",HelpOpen,60,18,136,30);
        dock = panel(300, 700, 636, 58);
        mode = control(dock, L"COMBOBOX", L"", Mode, 12, 14, 110, 280, CBS_DROPDOWNLIST | WS_VSCROLL);
        for (auto label : {L"Select", L"Move", L"Rotate", L"Scale", L"Route", L"Terrain", L"Sculpt"})
            SendMessage(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessage(mode, CB_SETCURSEL, 0, 0);
        shape = control(dock, L"COMBOBOX", L"", Shape, 128, 14, 143, 350, CBS_DROPDOWNLIST | WS_VSCROLL);
        for (const auto &s : shapes)
            SendMessage(shape, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(s).c_str()));
        SendMessage(shape, CB_SETCURSEL, 0, 0);
        control(dock, L"BUTTON", L"＋ Add", Add, 277, 12, 66, 34);
        brush = control(dock, L"COMBOBOX", L"", Brush, 350, 14, 105, 330, CBS_DROPDOWNLIST | WS_VSCROLL);
        for (auto label : {L"pull", L"push", L"smooth", L"flatten", L"raise", L"lower", L"paint", L"hill",
                           L"crater", L"plateau", L"carve", L"build"})
            SendMessage(brush, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessage(brush, CB_SETCURSEL, 0, 0);
        // Preserve internal selection controls while the original icon dock drives them.
        for (auto h : {mode, shape, brush})
            ShowWindow(h, SW_HIDE);
        // Remove the transitional Add dropdown button.
        DestroyWindow(GetDlgItem(dock, Add));
        build_dock(BuildTerrain);
        playbackPanel = panel(1000, 760, 232, 60);
        control(playbackPanel, L"BUTTON", L"Repeat", Repeat, 6, 12, 70, 36, BS_AUTOCHECKBOX);
        playState = control(playbackPanel, L"STATIC", L"Loading", 0, 76, 12, 48, 36);
        control(playbackPanel, L"BUTTON", L"↺", Stop, 126, 10, 42, 40);
        playButton = control(playbackPanel, L"BUTTON", L"▶", Play, 176, 10, 42, 40);
        SetPropW(playButton, L"sl.accent", reinterpret_cast<HANDLE>(1));
        load_default_character();
        assetPanel = panel(24, 76, 260, 500);
        SetWindowLongPtrW(assetPanel, GWL_STYLE, GetWindowLongPtrW(assetPanel, GWL_STYLE) | WS_VSCROLL);
        control(assetPanel, L"STATIC", L"YOUR CHARACTERS", 0, 20, 20, 220, 22);
        characterCard = control(assetPanel, L"STATIC", L"", 0, 20, 52, 220, 120, SS_NOTIFY | WS_TABSTOP);
        SetWindowSubclass(characterCard,card_proc,8,reinterpret_cast<DWORD_PTR>(this));
        control(assetPanel, L"BUTTON", L"＋ Place character", PlaceCharacter, 24, 184, 212, 36);
        control(assetPanel, L"BUTTON", L"Import character…", Import, 24, 228, 212, 36);
        control(assetPanel, L"BUTTON", L"Character settings", ObjectProperties, 20, 278, 220, 30);
        control(assetPanel, L"BUTTON", L"Advanced controls", Advanced, 20, 312, 220, 30, BS_AUTOCHECKBOX);
        auto followControl = control(assetPanel, L"BUTTON", L"Follow", Follow, 166, 312, 74, 30, BS_AUTOCHECKBOX);
        ShowWindow(followControl, SW_HIDE);
        control(assetPanel, L"BUTTON", L"Move character", 2200, 24, 350, 212, 36);
        control(assetPanel, L"BUTTON", L"Draw character route", 2300, 24, 394, 212, 36);
        control(assetPanel, L"STATIC", L"IN YOUR SCENE", 0, 20, 438, 220, 20);
        list = control(assetPanel, L"LISTBOX", L"", ObjectList, 20, 466, 220, 160,
                       LBS_NOTIFY | LBS_EXTENDEDSEL | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | WS_VSCROLL);
        SendMessage(list, LB_SETITEMHEIGHT, 0, 42);
        propertiesPanel = panel(1000, 76, 318, 650);
        tab = control(propertiesPanel, L"COMBOBOX", L"", InspectorTab, 12, 14, 254, 260,
                      CBS_DROPDOWNLIST | WS_VSCROLL);
        for (auto label : {L"Object settings", L"World & lighting", L"Camera settings", L"Projects & presets", L"Terrain settings", L"Shape settings", L"Adventure controls"})
            SendMessage(tab, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessage(tab, CB_SETCURSEL, 0, 0);
        ShowWindow(tab, SW_HIDE);
        inspectorTitle = control(propertiesPanel, L"STATIC", L"Character settings", 0, 16, 18, 250, 26);
        control(propertiesPanel, L"BUTTON", L"×", CloseInspector, 274, 12, 32, 30);
        inspector =
            control(propertiesPanel, L"STATIC", L"", 0, 8, 54, 302, 550, WS_VSCROLL | WS_CLIPCHILDREN);
        SetWindowSubclass(inspector, inspector_proc, 1, reinterpret_cast<DWORD_PTR>(this));
        SetPropW(inspector, L"sl.backgroundProxy", reinterpret_cast<HANDLE>(1));
        menuPanel = panel(18, 58, 262, 360);
        questTracker.create(window);
        movementHud.create(window);combatHud.create(window);pacingHud.create(window);
        adventureHudPanel=panel(14,104,620,46);
        adventureHud=control(adventureHudPanel,L"STATIC",L"WASD: walk | Space: jump | Right-drag: look | F / E: use | Escape: pause",0,12,12,596,22);
        ShowWindow(adventureHudPanel,SW_HIDE);
        status = control(window, L"STATIC", L"Starting the graphics engine...", 0, 14, 840, 1100, 20);
        renderer = std::make_unique<NativeSession>(view, 1200, 750);
        renderer->pacing_enabled(pacingShown);
        SetTimer(window, 1, 16, nullptr);
        refresh();
        layout();
    }
    static int property_panel_width(int page, UINT dpi) {
        return ((page == 0 || page == 5) ? 270 : 310) +
            std::max(0, GetSystemMetricsForDpi(SM_CXVSCROLL, dpi) - 17);
    }
    void layout() {
        RECT r;
        GetClientRect(window, &r);
        width = r.right;
        height = r.bottom;
        if (!view)
            return;
        if(interfacePercent==0 && inspector && !rebuildingInspector && !changingInterfaceScale) {
            const int next=requested_interface_scale(GetDpiForWindow(window));
            if(next!=settingsScale){apply_interface_scale(next);return;}
        }
        auto pxs=[&](int n){return settings_px(n);};
        int vw=std::max(1,fullScreen?width:width-pxs(28)), vh=std::max(1,fullScreen?height:height-pxs(104));
        MoveWindow(view,fullScreen?0:pxs(14),fullScreen?0:pxs(62),vw,vh,TRUE);
        renderer->size(vw,vh);
        MoveWindow(top[0],pxs(fullScreen?14:18),pxs(fullScreen?12:18),pxs(fullScreen?32:34),pxs(fullScreen?28:30),TRUE);
        for(size_t i=1;i<top.size();++i)
            MoveWindow(top[i],width-pxs(fullScreen?180:184)+int(i-1)*pxs(44),pxs(fullScreen?12:18),pxs(fullScreen?32:36),pxs(fullScreen?28:30),TRUE);
        const int assetWidth=pxs(260)+std::max(0,GetSystemMetricsForDpi(SM_CXVSCROLL,GetDpiForWindow(window))-17);
        const int controlWidth=std::max(pxs(270),width-(assetsShown?assetWidth+pxs(12):0));
        dockViewportWidth=std::min(dockWidth,std::max(1,std::min(width-pxs(36),controlWidth-pxs(36))));
        const int dockX=std::max(pxs(18),(controlWidth-dockViewportWidth)/2);
        const int canvasBottom=fullScreen?height:height-pxs(42);
        const int dockHeight=pxs(dockWidth>dockViewportWidth?80:60);
        const int dockBottom=std::max(pxs(62),canvasBottom-pxs(78)-dockHeight-pxs(12));
        MoveWindow(dock,dockX,dockBottom,dockViewportWidth,dockHeight,TRUE);
        SCROLLINFO horizontal{sizeof(horizontal),SIF_RANGE|SIF_PAGE|SIF_POS};horizontal.nMax=dockWidth-1;horizontal.nPage=dockViewportWidth;horizontal.nPos=dockScroll;
        SetScrollInfo(dock,SB_HORZ,&horizontal,TRUE);scroll_dock(dockScroll);
        MoveWindow(playbackPanel,std::max(pxs(18),std::min(width-pxs(250),controlWidth-pxs(fullScreen?250:264))),canvasBottom-pxs(78),pxs(232),pxs(60),TRUE);
        int page=int(SendMessage(tab,CB_GETCURSEL,0,0));
        const int propertyWidth=pxs(property_panel_width(page,GetDpiForWindow(window)));
        const bool dockSettings=page==4 || page==5;
        const int panelTop=pxs(dockSettings?66:76);
        const int available=std::max(pxs(100),height-panelTop-pxs(30));
        int ph=std::min({pxs(610),available,pxs(formRows*35+90)});
        const int preferredBottom=dockSettings?dockBottom-pxs(10):canvasBottom-pxs(88);
        if(preferredBottom-panelTop>=pxs(164))ph=std::min(ph,preferredBottom-panelTop);
        const int panelX=std::clamp(controlWidth-propertyWidth-pxs(24),pxs(18),std::max(pxs(18),width-propertyWidth-pxs(18)));
        MoveWindow(propertiesPanel,panelX,panelTop,propertyWidth,ph,TRUE);
        MoveWindow(inspectorTitle,pxs(20),pxs(18),propertyWidth-pxs(60),pxs(26),TRUE);
        MoveWindow(GetDlgItem(propertiesPanel,CloseInspector),propertyWidth-pxs(38),pxs(12),pxs(28),pxs(28),TRUE);
        MoveWindow(inspector,pxs(12),pxs(54),propertyWidth-pxs(24),std::max(1,ph-pxs(64)),TRUE);
        SCROLLINFO si{sizeof(si),SIF_PAGE};si.nPage=std::max(1,ph-pxs(64));SetScrollInfo(inspector,SB_VERT,&si,TRUE);scroll_inspector(scroll);
        const int listHeight=pxs(std::min(220,std::max(0,int(scene.Characters.size())*42)));
        const int assetHeight=std::min(pxs(486)+listHeight,height-pxs(fullScreen?80:98));
        MoveWindow(assetPanel,width-assetWidth,pxs(fullScreen?40:58),assetWidth,assetHeight,TRUE);
        MoveWindow(list,pxs(20),pxs(466)-assetScroll,pxs(220),std::max(1,listHeight),TRUE);
        SCROLLINFO assetRange{sizeof(assetRange),SIF_RANGE|SIF_PAGE|SIF_POS};assetRange.nMax=pxs(486)+listHeight-1;assetRange.nPage=assetHeight;assetRange.nPos=assetScroll;
        SetScrollInfo(assetPanel,SB_VERT,&assetRange,TRUE);scroll_assets(assetScroll);
        const int menuWidth=pxs(openMenu==MenuFile?250:openMenu==MenuWorld?310:245);
        const int menuHeight=std::min(height-pxs(fullScreen?88:98),pxs(openMenu==MenuFile?660:openMenu==MenuWorld?mainContentHeight:74+std::max(0,int(menuControls.size())-1)*44));
        MoveWindow(menuPanel,(openMenu==MenuFile || openMenu==ObjectBehaviorMenu)?pxs(fullScreen?14:18):width-menuWidth-pxs(14),pxs(fullScreen?48:58),menuWidth,menuHeight,TRUE);
        if(openMenu==MenuFile || openMenu==MenuWorld || openMenu==ObjectBehaviorMenu || openMenu==MenuBuilder)scroll_main(mainScroll);
        questTracker.layout(width,height,pxs(fullScreen?44:104),settingsScale);
        MoveWindow(adventureHudPanel,pxs(14),pxs(fullScreen?44:104),std::min(pxs(620),width-pxs(28)),pxs(46),TRUE);
        MoveWindow(adventureHud,pxs(12),pxs(12),std::min(pxs(596),width-pxs(52)),pxs(22),TRUE);
        MoveWindow(status,pxs(14),height-pxs(25),width-pxs(28),pxs(20),TRUE);
        SetWindowPos(status, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(dock, dockShown ? SW_SHOW : SW_HIDE);
        ShowWindow(propertiesPanel, inspectorShown ? SW_SHOW : SW_HIDE);
        ShowWindow(assetPanel, assetsShown ? SW_SHOW : SW_HIDE);
        ShowWindow(menuPanel, openMenu ? SW_SHOW : SW_HIDE);
        for (auto h : top)
            SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        for (auto h : {dock, playbackPanel, assetPanel, propertiesPanel, menuPanel})
            SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        layout_secondary();
        layout_help();
        if(interfacePercent==0 && menuPanel)SetWindowTextW(GetDlgItem(menuPanel,InterfaceReadout),
            (L"Auto: "+std::to_wstring(MulDiv(settingsScale,96,GetDpiForWindow(window)))+L"%").c_str());
        InvalidateRect(window, nullptr, FALSE);
    }
    void paint_chrome() {
        PAINTSTRUCT ps;
        auto dc = BeginPaint(window, &ps);
        RECT r;
        GetClientRect(window, &r);
        {
            Gdiplus::Graphics g(dc);
            Gdiplus::LinearGradientBrush bg(
                Gdiplus::Rect(0, 0, std::max(1, width), std::max(1, height)), Gdiplus::Color(255, 20, 33, 59),
                Gdiplus::Color(255, 30, 15, 38),
                float(std::atan2(double(height), double(width)) * 180 / std::numbers::pi));
            Gdiplus::Color colors[] = {Gdiplus::Color(255, 20, 33, 59), Gdiplus::Color(255, 9, 13, 24),
                                       Gdiplus::Color(255, 36, 19, 46)};
            Gdiplus::REAL stops[] = {0, .45f, 1};
            bg.SetInterpolationColors(colors, stops, 3);
            g.FillRectangle(&bg, 0, 0, width, height);
            if (!fullScreen && logo && logo->GetLastStatus() == Gdiplus::Ok)
                g.DrawImage(logo.get(),settings_px(66),settings_px(17),settings_px(32),settings_px(32));
        }
        if (!fullScreen && width>settings_px(640)) {
        style::text(dc, brandFont, L"STARLIGHT", {settings_px(108),settings_px(15),settings_px(200),settings_px(50)}, style::ink,
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        style::text(dc, font, wide(scene.Name), {settings_px(200),settings_px(15),std::max(settings_px(200),width-settings_px(340)),settings_px(50)}, RGB(175, 191, 219),
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
        EndPaint(window, &ps);
    }

    void report(std::string_view text, UINT m, LPARAM l = 0) {
        const bool saved = userMessage;
        userMessage = user_action(m, l);
        error(text);
        userMessage = saved;
    }

    void error(std::string_view text) {
        if (!errorThrottle.admit(text, std::chrono::steady_clock::now(), !userMessage)) return;
        diagnostic("native editor", text);
        std::fprintf(stderr,"EDITOR ERROR DIALOG: %.*s%c",int(text.size()),text.data(),10);
        SetWindowTextW(status, wide(text).c_str());
        static const bool quiet = [] { char v[8]{}; return GetEnvironmentVariableA("STARLIGHT_SUPPRESS_DIALOGS", v, 8) && v[0] == '1'; }(); // automated runs: report, do not block on a modal box
        if (!quiet && !errorDialogOpen) {
            errorDialogOpen = true;
            struct Reset { bool &flag; ~Reset() { flag = false; } } reset{errorDialogOpen};
            MessageBoxW(window, wide(text).c_str(), L"Starlight", MB_OK | MB_ICONERROR);
        }
    }

    struct LoadingGuard {
        Editor& editor;
        explicit LoadingGuard(Editor& owner) : editor(owner) { editor.loadingActive = true; }
        ~LoadingGuard() { editor.loadingActive = false; release_deferred_close(); }
        LoadingGuard(const LoadingGuard&) = delete;
        LoadingGuard& operator=(const LoadingGuard&) = delete;
    };
    uint64_t install_scene(const SceneData& data, bool live = false) {
        return installedScene = renderer->scene(data, live);
    }
    void drop_preparation() {
        if (!playing) lastPreparation = PreparationReport{};
    }
    bool settle_scene(LoadingScreen& loading, const std::string& label) {
        loading.show(window, view);
        loading.update(0, label);
        PreparationReport report;
        report.sceneGeneration = installedScene;
        auto idle = std::make_unique<AdventurePlayer>();
        const auto before = renderer->status();
        {
            LoadingGuard guard(*this);
            finish_scene_upload(*renderer, before, *idle, loading, report);
        }
        loading.close();
        if (report.ready()) {
            verifiedScene = report.sceneGeneration;
            return true;
        }
        if (!report.cancelled)
            error("The scene is open, but the graphics engine did not finish preparing it. " + report.reason + ". Nothing in your scene was changed. Play checks again before it starts and will not start until the graphics are ready.");
        return false;
    }
    bool start_walk_mode() {
        LoadingScreen loading;
        loading.show(window, view);
        loading.update(0, "Getting the walk-through ready");
        const auto statusBefore = renderer->status();
        PreparationReport report;
        {
            LoadingGuard guard(*this);
            explorer.emplace();
            try {
                explorer->start(scene, false);
                report = prepare_scene(*explorer, loading);
                if (!report.stopped()) {
                    report.sceneGeneration = installedScene;
                    finish_scene_upload(*renderer, statusBefore, *explorer, loading, report);
                }
            } catch (...) {
                explorer.reset();
                throw;
            }
        }
        loading.close();
        if (report.ready()) {
            verifiedScene = report.sceneGeneration;
            return true;
        }
        explorer.reset();
        explorerLook = false;
        ShowWindow(adventureHudPanel, SW_HIDE);
        if (!report.cancelled)
            error("Walk mode could not start. " + report.reason + ". Your scene was not changed.");
        return false;
    }
    void await_startup(BootSplash& splash, bool automated) {
        LoadingScreen loading;
        PreparationReport report;
        report.sceneGeneration = installedScene;
        auto idle = std::make_unique<AdventurePlayer>();
        loading_observer() = [&](double) {
            if (!splash.finished() || loading.visible()) return;
            if (!startupActivated) startupActivated = activate_startup_window(window);
            loading.show(window, view);
        };
        {
            LoadingGuard guard(*this);
            finish_scene_upload(*renderer, renderer->status(), *idle, loading, report);
        }
        loading_observer() = nullptr;
        loading.close();
        splash.dismiss();
        if (report.cancelled) return;
        if (!startupActivated) startupActivated = activate_startup_window(window);
        if (report.ready()) {
            verifiedScene = report.sceneGeneration;
            SetWindowTextW(status, L"Ready • Native performance first");
            SetWindowTextW(playState, L"Ready");
            return;
        }
        const std::string text = "The editor is open, but the graphics engine did not finish getting ready. " + report.reason + ". You can keep working on your scene. Play checks again before it starts and will not start until the graphics are ready.";
        if (automated) {
            diagnostic("editor startup", text);
            SetWindowTextW(status, wide(text).c_str());
        } else error(text);
    }

    Character *actor() {
        return selected >= 0 && selected < int(scene.Characters.size()) ? &scene.Characters[selected]
                                                                        : nullptr;
    }

    std::string text(HWND h) {
        int n = GetWindowTextLengthW(h);
        std::wstring s(n + 1, 0);
        GetWindowTextW(h, s.data(), n + 1);
        s.resize(n);
        return utf8(s);
    }

    double number(std::string key) {
        if (choices.contains(key)) return double(SendMessage(inputs.at(key), CB_GETCURSEL, 0, 0));
        if (sliders.contains(key)) return SendMessage(inputs.at(key), TBM_GETPOS, 0, 0) / sliders.at(key);
        auto s = text(inputs.at(key));
        size_t consumed = 0;
        double v = std::stod(s, &consumed);
        if (consumed != s.size() || !std::isfinite(v))
            throw std::runtime_error("Enter a valid number for " + key);
        return v;
    }

    int integer(std::string key) {
        auto value = number(key);
        if (value != std::trunc(value) || value < INT_MIN || value > INT_MAX)
            throw std::runtime_error("Enter a whole number for " + key);
        return int(value);
    }

    bool checked(std::string key) { return SendMessage(inputs.at(key), BM_GETCHECK, 0, 0) == BST_CHECKED; }
    int form_width() const {
        int page = int(SendMessage(tab, CB_GETCURSEL, 0, 0));
        return (page == 0 || page == 5) ? 214 : 254;
    }

    void field(std::string key, std::string label, std::string value, bool checkbox = false) {
        int y = 8 + formRows * 35;
        formRows += checkbox ? 1 : 2;
        if (!checkbox) fields.push_back(control(inspector, L"STATIC", wide(label).c_str(), 0, 8, y, form_width(), 22));
        auto edit =
            control(inspector, checkbox ? L"BUTTON" : L"EDIT", wide(checkbox ? label : value).c_str(), 0, 8,
                    y + (checkbox ? 0 : 26), form_width(), 30, checkbox ? BS_AUTOCHECKBOX : WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
        if (checkbox)
            SendMessage(edit, BM_SETCHECK, value == "1" ? BST_CHECKED : BST_UNCHECKED, 0);
        inputs[key] = edit;
        fields.push_back(edit);
    }

    void field(std::string key, std::string label, double value) {
        std::ostringstream out;
        out.precision(7);
        out << value;
        field(key, label, out.str());
    }

    void choice(std::string key, std::string label, int selectedValue,
                std::initializer_list<const wchar_t*> names) {
        field(key, label, "");
        auto old = inputs.at(key);
        RECT r; GetWindowRect(old, &r); MapWindowPoints(nullptr, inspector, reinterpret_cast<POINT*>(&r), 2);
        destroy_control(old); fields.pop_back();
        auto h = control(inspector, L"COMBOBOX", L"", 0, r.left, r.top, r.right - r.left, 330, CBS_DROPDOWNLIST | WS_VSCROLL);
        for (auto name : names) SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessage(h, CB_SETCURSEL, selectedValue, 0);
        inputs[key] = h; choices.insert(key); fields.push_back(h);
    }
    void slider(std::string key, std::string label, double value, double low, double high) {
        field(key, label, "");
        auto old = inputs.at(key);
        RECT r; GetWindowRect(old, &r); MapWindowPoints(nullptr, inspector, reinterpret_cast<POINT*>(&r), 2);
        destroy_control(old); fields.pop_back();
        auto h = control(inspector, TRACKBAR_CLASSW, L"", 3000 + int(sliders.size()), r.left, r.top, r.right - r.left, 28, TBS_NOTICKS | WS_TABSTOP);
        SendMessage(h, TBM_SETRANGE, TRUE, MAKELPARAM(int(low * 100), int(high * 100)));
        SendMessage(h, TBM_SETPOS, TRUE, int(value * 100));
        inputs[key] = h; sliders[key] = 100; fields.push_back(h);
    }
    void form_button(int id, const wchar_t* label) {
        fields.push_back(control(inspector, L"BUTTON", label, id, 8, 8 + formRows++ * 35, form_width(), 30));
    }
    void number_row(std::initializer_list<std::tuple<const char*, const char*, double>> values) {
        const int row = formRows, y = 8 + row * 35;
        const int count = int(values.size()), gap = 12;
        const int width = (form_width() - gap * (count - 1)) / count;
        int column = 0;
        for (auto [key, label, value] : values) {
            const auto start = fields.size();
            field(key, label, value);
            MoveWindow(fields[start], 8 + column * (width + gap), y, width, 22, TRUE);
            MoveWindow(inputs.at(key), 8 + column * (width + gap), y + 26, width, 30, TRUE);
            ++column;
        }
        formRows = row + 2;
    }
    std::vector<std::string> ruleSources,ruleDoors;
    void gameplay_rule_fields() {
        fields.push_back(control(inspector,L"STATIC",L"QUESTS · SIDE TRACKER",0,8,8+formRows++*35,form_width(),22));
        choice("savedQuest","Quest to edit",0,{L"Create a new quest"});
        for(const auto& q:scene.Quests)SendMessageW(inputs.at("savedQuest"),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(q.Title).c_str()));
        form_button(LoadQuest,L"Load selected quest");
        field("questTitle","Quest title",std::string("My quest"));
        fields.push_back(control(inspector,L"STATIC",L"Quest story / details",0,8,8+formRows++*35,form_width(),22));
        inputs["questDescription"]=control(inspector,L"EDIT",L"",0,8,8+formRows*35,form_width(),95,WS_BORDER|WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_WANTRETURN);
        fields.push_back(inputs["questDescription"]);formRows+=3;
        fields.push_back(control(inspector,L"STATIC",L"Objectives (Ctrl-click to select)",0,8,8+formRows++*35,form_width(),22));
        inputs["questRules"]=control(inspector,L"LISTBOX",L"",0,8,8+formRows*35,form_width(),95,WS_BORDER|WS_TABSTOP|WS_VSCROLL|LBS_EXTENDEDSEL|LBS_NOINTEGRALHEIGHT);
        fields.push_back(inputs["questRules"]);formRows+=3;
        for(const auto& rule:scene.Rules)SendMessageW(inputs["questRules"],LB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(rule.Name).c_str()));
        field("questStepSizes","Objectives per step (e.g. 3,2)",std::string{});
        field("questTracked","Show in tracker initially","1",true);
        form_button(SaveQuest,L"Save quest");
        form_button(RemoveQuest,L"Remove selected quest");
        fields.push_back(control(inspector,L"STATIC",L"GAMEPLAY RULES",0,8,8+formRows++*35,form_width(),22));
        choice("savedRule","Saved rules",0,{L"Choose a rule to remove"});
        for(size_t i=0;i<scene.Rules.size();++i)SendMessageW(inputs.at("savedRule"),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(std::to_string(i+1)+". "+scene.Rules[i].Name).c_str()));
        form_button(RemoveGameplayRule,L"Remove selected rule");
        field("ruleName","Objective / rule title",std::string("New rule"));
        field("ruleDescription","Objective details / hints",std::string{});
        choice("ruleEvent","When",0,{L"An item is collected",L"The player enters an area",L"The player interacts"});
        field("ruleItem","Collected item (blank = any)",std::string{});
        choice("ruleSource","Interaction object",0,{L"Any object"});
        choice("ruleDoor","Linked door",0,{L"Choose a door"});
        ruleSources.clear();ruleDoors.clear();
        for(const auto& c:scene.Characters) {
            ruleSources.push_back(c.Id);SendMessageW(inputs.at("ruleSource"),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(c.Name).c_str()));
            if(c.GameplayRole=="door"){ruleDoors.push_back(c.Id);SendMessageW(inputs.at("ruleDoor"),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(wide(c.Name).c_str()));}
        }
        field("ruleKeys","Required items, comma separated",std::string{});
        field("rulePrior","Required objective (optional)",std::string{});
        choice("ruleAction","Then",0,{L"Complete an objective",L"Open the linked door",L"Finish the game"});
        field("ruleObjective","Objective name",std::string("My objective"));
        const auto start=scene.Game.value_or(GameSettings{});
        field("ruleX","Area centre X",actor()?actor()->X:start.SpawnX);
        field("ruleY","Area centre height",actor()?actor()->Y+1.7:start.SpawnY);
        field("ruleZ","Area centre Z",actor()?actor()->Z:start.SpawnZ);
        field("ruleRadius","Area radius (metres)",2.);
        field("ruleOnce","Run only once per Play session","1",true);
        form_button(AddGameplayRule,L"Add gameplay rule");
    }
    bool gameplay_rule_command(int id) {
        if(id==LoadQuest || id==SaveQuest || id==RemoveQuest){
            if(playing)throw std::runtime_error("Pause before editing quests");
            int selected=integer("savedQuest")-1;
            if(id==LoadQuest || id==RemoveQuest){
                if(selected<0 || size_t(selected)>=scene.Quests.size())throw std::runtime_error("Choose a saved quest");
                if(id==RemoveQuest){edit([&]{scene.Quests.erase(scene.Quests.begin()+selected);});return true;}
                const auto& q=scene.Quests[selected];SetWindowTextW(inputs.at("questTitle"),wide(q.Title).c_str());SetWindowTextW(inputs.at("questDescription"),wide(q.Description).c_str());
                SendMessage(inputs.at("questRules"),LB_SETSEL,FALSE,-1);
                for(const auto& link:q.RuleIds)for(size_t i=0;i<scene.Rules.size();++i)if(scene.Rules[i].Id==link)SendMessage(inputs.at("questRules"),LB_SETSEL,TRUE,i);
                std::string steps;for(int count:q.StepSizes){if(!steps.empty())steps+=",";steps+=std::to_string(count);}SetWindowTextW(inputs.at("questStepSizes"),wide(steps).c_str());
                SendMessage(inputs.at("questTracked"),BM_SETCHECK,q.Tracked?BST_CHECKED:BST_UNCHECKED,0);return true;
            }
            QuestDefinition q;if(selected>=0 && size_t(selected)<scene.Quests.size())q.Id=scene.Quests[selected].Id;
            q.Title=text(inputs.at("questTitle"));q.Description=text(inputs.at("questDescription"));q.Tracked=checked("questTracked");
            if(selected>=0 && size_t(selected)<scene.Quests.size())for(const auto& link:scene.Quests[selected].RuleIds)
                for(size_t i=0;i<scene.Rules.size();++i)if(scene.Rules[i].Id==link && SendMessage(inputs.at("questRules"),LB_GETSEL,i,0)>0)q.RuleIds.push_back(link);
            for(size_t i=0;i<scene.Rules.size();++i)if(SendMessage(inputs.at("questRules"),LB_GETSEL,i,0)>0 && std::find(q.RuleIds.begin(),q.RuleIds.end(),scene.Rules[i].Id)==q.RuleIds.end())q.RuleIds.push_back(scene.Rules[i].Id);
            auto stepText=text(inputs.at("questStepSizes"));
            if(!stepText.empty()){
                std::string token;
                auto append=[&]{auto begin=token.find_first_not_of(" \t"),end=token.find_last_not_of(" \t");
                    if(begin==std::string::npos || end!=begin || token[begin]<'1' || token[begin]>'5')throw std::runtime_error("Enter one to five objectives per step, separated by commas");
                    q.StepSizes.push_back(token[begin]-'0');token.clear();};
                for(char c:stepText){if(c==',')append();else token+=c;}append();
            }
            auto candidate=scene;if(selected>=0 && size_t(selected)<candidate.Quests.size())candidate.Quests[selected]=q;else candidate.Quests.push_back(q);candidate.validate();
            edit([&]{scene=std::move(candidate);});return true;
        }
        if(id!=AddGameplayRule && id!=RemoveGameplayRule)return false;
        if(playing)throw std::runtime_error("Pause before editing gameplay rules");
        if(id==RemoveGameplayRule) {
            int index=integer("savedRule")-1;
            if(index<0 || size_t(index)>=scene.Rules.size())throw std::runtime_error("Choose a saved rule first");
            for(const auto& q:scene.Quests)if(std::find(q.RuleIds.begin(),q.RuleIds.end(),scene.Rules[index].Id)!=q.RuleIds.end())throw std::runtime_error("Remove this objective from its quest before removing the rule");
            edit([&]{scene.Rules.erase(scene.Rules.begin()+index);});return true;
        }
        GameplayRule rule;rule.Name=text(inputs.at("ruleName"));rule.Description=text(inputs.at("ruleDescription"));
        int event=integer("ruleEvent"),action=integer("ruleAction");
        if(event<0 || event>2 || action<0 || action>2)throw std::runtime_error("Choose a rule event and action");
        rule.Event=std::array<std::string,3>{"item-collected","enter-area","interact"}[event];
        if(event==0)rule.Subject=text(inputs.at("ruleItem"));
        if(event==2){int index=integer("ruleSource");if(index>0 && size_t(index)<=ruleSources.size())rule.Subject=ruleSources[index-1];}
        rule.X=number("ruleX");rule.Y=number("ruleY");rule.Z=number("ruleZ");rule.Radius=number("ruleRadius");rule.Once=checked("ruleOnce");
        auto trim=[](std::string value){auto a=value.find_first_not_of(" \t\r\n");if(a==std::string::npos)return std::string{};return value.substr(a,value.find_last_not_of(" \t\r\n")-a+1);};
        auto names=text(inputs.at("ruleKeys"));if(names.size()>2048)throw std::runtime_error("Use at most 16 short item names");
        std::istringstream stream(names);std::string name;
        while(std::getline(stream,name,',')){name=trim(name);if(!name.empty())rule.Conditions.push_back({"has-key",name});}
        auto prior=trim(text(inputs.at("rulePrior")));if(!prior.empty())rule.Conditions.push_back({"objective-complete",prior});
        GameplayAction result;
        if(action==0){result.Type="complete-objective";result.Target=trim(text(inputs.at("ruleObjective")));}
        if(action==1){int index=integer("ruleDoor");if(index<=0 || size_t(index)>ruleDoors.size())throw std::runtime_error("Choose a linked door");result.Type="open-door";result.Target=ruleDoors[index-1];}
        if(action==2)result.Type="finish-game";
        rule.Actions.push_back(result);rule.validate();
        edit([&]{if(!scene.Game)scene.Game.emplace();scene.Rules.push_back(rule);});return true;
    }

    void inspector_fields() {
        MenuTiming timing("inspector-rebuild");
        rebuildingInspector = true;
        for (auto h : fields) destroy_control(h);
        speedReadout = nullptr;
        fields.clear(); inputs.clear(); choices.clear(); sliders.clear();
        scroll = 0; formRows = 0;
        int page = int(SendMessage(tab, CB_GETCURSEL, 0, 0));
        const wchar_t* titles[] = {L"Character settings", L"Atmosphere & ray tracing", L"Camera settings",
                                  L"Projects & presets", L"Terrain settings", L"Shape settings", L"Adventure controls"};
        SetWindowTextW(inspectorTitle, titles[page]);
        if (page == 0) {
            SetWindowTextW(inspectorTitle, advanced ? L"ADVANCED · EXACT CONTROL" : L"BEGINNER · MAKE IT YOURS");
            if (auto c = actor()) {
                field("name", "Character name", c->Name);
                int role=c->GameplayRole=="key"?1:c->GameplayRole=="door"?2:c->GameplayRole=="exit"?3:c->GameplayRole=="breakable"?4:0;
                choice("gameRole","Gameplay interaction",role,{L"None",L"Collectible key",L"Locked door",L"Finish / exit",L"Breakable"});
                choice("grappleMode","Grapple attachment",c->GrappleMode,{L"Automatic",L"Wall attachment",L"Swing anchor"});
                field("gameKey","Key name (shared by key and door)",c->GameplayKey);
                field("breakable","Can be broken",c->Breakable?"1":"0",true);
                field("destructionHealth","Durability (scenery)",c->DestructionHealth);
                field("combatHealth","Combat health (0 = scenery)",std::to_string(c->CombatHealth));field("combatSight","Sight range (m)",c->CombatSightRange);
                choice("combatTeam","Combat affiliation",c->CombatTeam==1?1:c->CombatTeam==0?0:2,{L"Neutral",L"Friendly",L"Enemy"});
                field("combatBoss","Boss break bar",c->CombatBoss?"1":"0",true);
                field("CombatProjectileSpeed","Shot speed (0 = contact attack)",c->CombatProjectileSpeed);
                field("CombatProjectileGravity","Shot gravity (m/s squared)",c->CombatProjectileGravity);
                field("CombatProjectileMass","Shot mass (kg)",c->CombatProjectileMass);
                field("combatAutoAttack","Attack nearby enemies",c->CombatAutoAttack?"1":"0",true);
                field("combatPursuit","Follow nearby enemies",c->CombatPursuit?"1":"0",true);
                choice("combatControl","Swipe control effect",c->CombatControlEffect+1,{L"None",L"Stun",L"Knockback",L"Pull",L"Launch",L"Knockdown",L"Root",L"Slow",L"Silence",L"Fear",L"Taunt",L"Sleep",L"Blind",L"Disorient",L"Tether"});
                form_button(ApplyGameplay,L"Apply interaction");
                slider("speed", "Walking speed", c->Speed, .2, 5);
                std::wostringstream speedText;
                speedText << std::fixed << std::setprecision(1) << c->Speed << L" metres / second";
                speedReadout = control(inspector, L"STATIC", speedText.str().c_str(), 0,
                                       8, 8 + formRows++ * 35, form_width(), 22);
                fields.push_back(speedReadout);
                slider("size", "Size", c->Size, .3, 3);
                if (advanced) {
                    number_row({{"x", "Position X (m)", c->X}, {"z", "Position Z (m)", c->Z}});
                    number_row({{"exactSpeed", "Speed (m/s)", c->Speed}, {"exactSize", "Size multiplier", c->Size}});
                    field("follow", "Follow selected character", follow ? "1" : "0", true);
                    form_button(ApplyExact, L"Apply exact values");
                }
                    form_button(BuildDetails, buildDetails ? L"− Build: height, size and floor" : L"+ Build: height, size and floor");
                    if (buildDetails) {
                        field("y", "Height above floor", c->Y);
                        number_row({{"width", "Width", c->Width}, {"height", "Height", c->Height}, {"depth", "Depth", c->Depth}});
                        field("rx", "Rotate X", c->RotationX); field("ry", "Rotate Y", c->RotationY);
                        field("rz", "Rotate Z", c->RotationZ);
                        field("color", "Color", c->Color);
                        choice("surface", "Surface", c->Surface, {L"Plain", L"Grass", L"Earth", L"Sand", L"Stone", L"Snow", L"Rock", L"Water", L"Lava", L"Lightning", L"Chaos rock", L"Light-bound", L"Bark", L"Leaf"});
                        if(c->Surface==12||c->Surface==13){auto f=c->Foliage.value_or(FoliageMaterial{});
                            if(c->Surface==12){field("barkScale","Bark scale",f.BarkScale);field("barkRelief","Bark relief",f.BarkRelief);field("barkRough","Bark roughness",f.BarkRoughness);}
                            else{field("leafTransmission","Leaf thin light",f.LeafTransmission);field("leafVeins","Leaf vein contrast",f.LeafVeins);field("leafRough","Leaf roughness",f.LeafRoughness);}
                        }
                        field("floor", "Keep above floor", c->KeepAboveFloor ? "1" : "0", true);
                        form_button(ApplyBuild, L"Apply changes");
                    }
                    form_button(BodyDetails, bodyDetails ? L"− Rigid body" : L"+ Rigid body");
                    if (bodyDetails) {
                        field("dynamic", "Simulate during Play", c->Dynamic ? "1" : "0", true);
                        field("density", "Density (kg/m³; water = 1000)", c->Density);
                        field("friction", "Friction (0 to 2)", c->Friction);
                        field("restitution", "Bounce (0 to 1)", c->Restitution);
                        choice("jointKind","Joint",c->JointKind,{L"None",L"Fixed",L"Hinge",L"Rope",L"Spring",L"Ball",L"Slider"});
                        choice("jointTarget","Attach to",0,{});
                        SendMessageW(inputs.at("jointTarget"),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"World"));int jointTargetIndex=0;
                        for(size_t i=0;i<scene.Characters.size();++i){auto name=wide(scene.Characters[i].Name);SendMessageW(inputs.at("jointTarget"),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));if(scene.Characters[i].Id==c->JointTargetId)jointTargetIndex=int(i)+1;}
                        SendMessageW(inputs.at("jointTarget"),CB_SETCURSEL,jointTargetIndex,0);
                        number_row({{"jointX","Anchor X",c->JointX},{"jointY","Anchor Y",c->JointY},{"jointZ","Anchor Z",c->JointZ}});
                        number_row({{"jointTargetX","Target X",c->JointTargetX},{"jointTargetY","Target Y",c->JointTargetY},{"jointTargetZ","Target Z",c->JointTargetZ}});
                        number_row({{"jointAxisX","Axis X",c->JointAxisX},{"jointAxisY","Axis Y",c->JointAxisY},{"jointAxisZ","Axis Z",c->JointAxisZ}});
                        field("VelocityX","Velocity X (m/s)",c->VelocityX);
                        field("VelocityY","Velocity Y (m/s)",c->VelocityY);
                        field("VelocityZ","Velocity Z (m/s)",c->VelocityZ);
                        field("AngularVelocityX","Spin X (degrees/s)",c->AngularVelocityX);
                        field("AngularVelocityY","Spin Y (degrees/s)",c->AngularVelocityY);
                        field("AngularVelocityZ","Spin Z (degrees/s)",c->AngularVelocityZ);
                        field("JointAngleMin","Hinge minimum angle",c->JointAngleMin);
                        field("JointAngleMax","Hinge maximum angle",c->JointAngleMax);
                        field("JointSegments","Physical rope segments (0 = distance only)",c->JointSegments);
                        field("JointRadius","Rope collision radius (m)",c->JointRadius);
                        field("jointLength","Rope / spring length or slider limit (m)",c->JointLength);
                        field("jointFrequency","Spring frequency (Hz)",c->JointFrequency);
                        field("jointDamping","Spring damping",c->JointDamping);
                        form_button(ApplyBody, L"Apply body settings");
                    }
                auto kind = c->Shape.value_or("");
                if(kind=="fire"){
                    field("ParticleRate","Particles per second",c->ParticleRate);
                    field("ParticleLifetime","Particle lifetime (s)",c->ParticleLifetime);
                    field("ParticleDrag","Particle drag",c->ParticleDrag);
                    field("ParticleLift","Upward acceleration",c->ParticleLift);
                    field("ParticleWindX","Wind X",c->ParticleWindX);
                    field("ParticleWindY","Wind Y",c->ParticleWindY);
                    field("ParticleWindZ","Wind Z",c->ParticleWindZ);
                    form_button(ApplyParticles,L"Apply particles");
                }
                if (kind == "chaos-lightning") {
                    std::vector<std::wstring> targets{L"Fixed offset from emitter"};int targetIndex=0;
                    for(size_t i=0;i<scene.Characters.size();++i){targets.push_back(wide(scene.Characters[i].Name));if(scene.Characters[i].Id==c->LightningTargetId)targetIndex=int(i)+1;}
                    choice("lightningTarget", "Follow target object",0,{});
                    for(const auto& name:targets)SendMessageW(inputs.at("lightningTarget"),CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
                    SendMessageW(inputs.at("lightningTarget"),CB_SETCURSEL,targetIndex,0);
                    number_row({{"tx", "Target X", c->TargetX}, {"ty", "Target Y", c->TargetY}, {"tz", "Target Z", c->TargetZ}});
                    field("lightningDamage","Strike damage (0 = visual only)",c->LightningDamage);
                    field("lightningChains","Additional chain targets (0 to 16)",c->LightningChains);
                    field("lightningRadius","Chain radius (m)",c->LightningChainRadius);
                    field("spread", "Branch spread (0–5 m)", c->BranchSpread);
                    form_button(ApplyLightning, L"Apply lightning");
                }
                if (kind == "light" || kind == "fire" || kind == "chaos-lightning" || kind == "chaos-rock" || kind == "light-shard") {
                    field("intensity", "Brightness", c->LightIntensity); field("range", "Light reach", c->LightRange);
                    if (kind == "light") field("cone", "Light cone", c->LightCone);
                }
                fields.push_back(control(inspector, L"STATIC",
                    (std::to_wstring(c->Route.size()) + L" route points").c_str(), 0,
                    8, 8 + formRows++ * 35, form_width(), 22));
                form_button(ClearRoute, L"Clear route");
                form_button(Delete, L"Remove character");
            } else form_button(MenuAssets, L"Choose a character first");
        } else if (page == 1) {
            field("name", "Scene name", scene.Name);
            slider("hour", "Time of day", scene.Hour, 0, 24);
            field("cycle", "Day/night cycle", scene.DayCycle ? "1" : "0", true);
            field("daylength", "Day duration (s)", scene.DayLength);
            field("fog", "Fog density", scene.FogDensity);
            choice("bloomquality", "Glow quality", scene.BloomQuality, {L"Off",L"Low",L"Medium",L"High"});
            choice("shadowquality", "Shadow quality", scene.ShadowQuality, {L"Off",L"Low",L"Medium",L"High"});
            slider("bloomstrength", "Glow intensity", scene.BloomStrength, 0, .5);
            slider("rain", "Rain intensity", scene.RainIntensity, 0, 1);
            field("rainwindx", "Rain wind X (m/s)", scene.RainWindX);
            field("rainwindz", "Rain wind Z (m/s)", scene.RainWindZ);
            field("rt", "Ray tracing", scene.RaytracingEnabled ? "1" : "0", true);
            choice("tier", "Quality", scene.RaytracingQuality, {L"Off", L"Low · Shadows", L"Medium · AO / reflections", L"High · Hybrid", L"Path tracing"});
            field("bounces", "Bounces (2–4)", double(scene.PathBounces));
            field("rr", "Ray reconstruction", scene.RayReconstructionEnabled ? "1" : "0", true);
            field("freeze", "Freeze effects", scene.FreezeRaytracingAnimation ? "1" : "0", true);
        } else if (page == 2) {
            slider("pan", "Pan speed", panSpeed, .25, 6);
            slider("tilt", "Tilt speed", tiltSpeed, .2, 4);
            field("arrows", "Arrow key pan", arrowPan ? "1" : "0", true);
        } else if (page == 4) {
            slider("radius", "Brush radius", radius, .4, 5);
            slider("strength", "Brush strength", strength, .2, 4);
            choice("paint", "Surface", paintMaterial, {L"Detailed grass", L"Rich earth", L"Sand", L"Weathered stone", L"Snow white"});
            form_button(Stamp, L"Stamp selected shape");
            form_button(PourWater, L"Place water pour"); form_button(PourLava, L"Place lava pour");
            form_button(ClearPours, L"Remove all pours");
            field("lava", "Lava", scene.Terrain && scene.Terrain->Lava ? "1" : "0", true);
            field("water", "Show water", scene.Terrain && scene.Terrain->WaterEnabled ? "1" : "0", true);
            field("level", "Water level", scene.Terrain ? scene.Terrain->WaterLevel : -.5);
            field("terrainBreakable","Terrain can be damaged",!scene.Terrain||scene.Terrain->Breakable?"1":"0",true);
            field("terrainDurability","Terrain durability",scene.Terrain?scene.Terrain->DestructionHealth:10);
            field("terrainDamageRadius","Impact carve radius (m)",scene.Terrain?scene.Terrain->DestructionRadius:.65);
            field("waterwindx","Water wind X (m/s)",scene.WaterWindX);
            field("waterwindz","Water wind Z (m/s)",scene.WaterWindZ);
            slider("waterwaves","Wind wave strength",scene.WaterWaveStrength,0,1);
            slider("lavaheat","Initial lava heat",scene.LavaInitialHeat,0,1);
            slider("lavacooling","Lava cooling rate",scene.LavaCoolingRate,0,1);
            field("lavacrust","Lava crust depth (m)",scene.LavaCrustDepth);
            slider("lavalight","Lava emitted light",scene.LavaLightStrength,0,8);
        } else if (page == 6) {
            form_button(CheckGameplay,L"Check gameplay setup");
            if(!gameplayReport.empty()){
                fields.push_back(control(inspector,L"EDIT",wide(gameplayReport).c_str(),0,8,8+formRows*35,form_width(),180,ES_MULTILINE|ES_READONLY|WS_VSCROLL));
                formRows+=6;
            }
            auto settings=scene.Game.value_or(GameSettings{});
            field("moveSpeed","Walking speed (m/s)",settings.MoveSpeed);
            field("projectileSpeed","Shot speed (0 = instant area attack)",settings.ProjectileSpeed);
            field("projectileMass","Shot mass (kg)",settings.ProjectileMass);
            field("projectileGravity","Shot gravity (m/s squared)",settings.ProjectileGravity);
            field("lookSpeed","Look sensitivity",settings.LookSensitivity);field("sightRange","Sight range (m)",settings.SightRange);
            field("reducedCameraMotion","Reduce camera motion",settings.ReducedCameraMotion ? "1" : "0",true);
            form_button(Advanced,advanced ? L"Show beginner controls" : L"Show advanced controls");
            if(advanced){
                field("spawnX","Start position X",settings.SpawnX);field("spawnY","Start position Y",settings.SpawnY);
                field("spawnZ","Start position Z",settings.SpawnZ);field("playerRadius","Collision radius",settings.PlayerRadius);
                field("playerCollision","Block movement through objects",settings.Collision ? "1" : "0",true);
            }
            form_button(Apply,L"Apply player settings");
            gameplay_rule_fields();
        } else if (page == 5) {
            field("gridSnap", "Grid snap", gridSnap ? "1" : "0", true);
            field("rotationSnap", "Snap rotation on release",rotationSnap ? "1" : "0",true);
            form_button(Advanced,advanced ? L"Show beginner controls" : L"Show advanced controls");
            if(advanced){
                field("gridStep", "Grid spacing (metres)",gridStep);
                field("rotationStep", "Rotation step (degrees)",rotationStep);
            }
            field("groundSnap", "Snap to ground", groundSnap ? "1" : "0", true);
            field("objectSnap", "Snap near objects", objectSnap ? "1" : "0", true);
            int colorIndex=6;for(int i=0;i<6;++i)if(scene.GroundColor==floorColors[i])colorIndex=i;
            choice("ground", "Floor color", colorIndex, {L"Dark blue",L"Black",L"Purple",L"Pink",L"Green",L"Sand",L"Custom color"});
            form_button(ViewSide, L"Side view"); form_button(ViewAbove, L"Above view");
            form_button(ViewLow, L"Low view"); form_button(ViewFocus, L"Focus selected");
        }
        else {
            field("project", "Project",
                  project ? utf8(project->path.filename().wstring()) : std::string("Choose project"));
            field("scene", "Scene", project ? project->selected : std::string(""));
            if (preset) {
                auto v = preset->data.at("settings");
                field("width", "Width", v.at("resolution")[0].get<double>());
                field("height", "Height", v.at("resolution")[1].get<double>());
                field("fps", "Frame limit", v.at("frameLimit").get<double>());
                field("duration", "Test duration (ms)", v.at("durationMs").get<double>());
                field("display", "Display mode", v.at("displayMode").get<std::string>());
                field("vsync", "VSync", v.at("vsync").get<bool>() ? "1" : "0", true);
                field("culling", "Culling", v.at("culling").get<bool>() ? "1" : "0", true);
                field("foreground", "Foreground", v.at("foreground").get<bool>() ? "1" : "0", true);
            }
            int y = 8 + formRows++ * 35;
            for (auto [id, label] :
                 std::vector<std::pair<int, const wchar_t *>>{{ChooseProject, L"Open project…"},
                                                              {ChoosePreset, L"Load preset…"},
                                                              {CheckPreset, L"Check settings"},
                                                              {SavePreset, L"Save preset as…"},
                                                              {LaunchProject, L"Launch project"}}) {
                fields.push_back(control(inspector, L"BUTTON", label, id, 8, y, 257, 28));
                y += 35; ++formRows;
            }
        }

        if (!inputs.empty() && page != 3 && page != 0 && page != 6) form_button(Apply, L"Apply");
        if (page == 0 && (inputs.contains("follow") || inputs.contains("intensity")))
            form_button(ApplyExtras, L"Apply additional settings");
        SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMax = formRows * 35 + 16;
        RECT bounds;
        GetClientRect(inspector, &bounds);
        si.nPage = std::max(1L, bounds.bottom);
        SetScrollInfo(inspector, SB_VERT, &si, TRUE);
        scale_settings_controls(100);
        layout();
        rebuildingInspector = false;
    }

    void update_creation_guide(){
        if(!playing && creationGuide.active){creationGuide.update(scene);questTracker.update(creationGuide.definition,creationGuide.runtime,true);}
    }
    void update_carry(){
        if(carriedId.empty() || playing)return;
        auto next=placement_point();
        if(!explorer){POINT p{};GetCursorPos(&p);ScreenToClient(view,&p);RECT bounds{};GetClientRect(view,&bounds);if(PtInRect(&bounds,p)){auto hit=terrain_hit(scene.Terrain?&*scene.Terrain:nullptr,camera_state().eye,ray(p.x,p.y));if(hit)next=*hit;}}
        if((next-carryPoint).length()<.02)return;carryPoint=next;
        auto preview=scene;for(auto& c:preview.Characters)if(c.Id==carriedId){c.X=next.x;c.Y=next.y;c.Z=next.z;snap_object(preview,c,gridSnap,objectSnap,groundSnap,gridStep);}
        install_scene(preview);
    }
    void finish_carry(bool place){
        if(carriedId.empty())return;const auto id=carriedId;carriedId.clear();
        if(place)edit([&]{auto c=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==id;});if(c==scene.Characters.end())throw std::runtime_error("Carried object no longer exists");c->X=carryPoint.x;c->Y=carryPoint.y;c->Z=carryPoint.z;snap_object(scene,*c,gridSnap,objectSnap,groundSnap,gridStep);});
        else {install_scene(scene);if(explorer)explorer->rebuild_for_authoring(scene);}
        if(place){creationGuide.carried(id);update_creation_guide();}
        SetWindowTextW(status,place?L"Block placed. Undo restores its previous position.":L"Move cancelled; original position preserved.");
    }
    void open_object_behavior(){
        if(playing)return;if(!carriedId.empty()){finish_carry(true);return;}RECT bounds{};GetClientRect(view,&bounds);POINT point{bounds.right/2,bounds.bottom/2};
        if(!explorer){GetCursorPos(&point);ScreenToClient(view,&point);if(!PtInRect(&bounds,point)){SetWindowTextW(status,L"Point at an object in the scene and press F.");return;}}
        int hit=pick(point.x,point.y);if(hit<0){SetWindowTextW(status,L"Point at a block and press F to choose its behavior.");return;}
        if(explorer){auto mesh=ObjectCache::shared().raycast(scene.Characters[hit],scene.Characters[hit],camera_state().eye,ray(point.x,point.y));if(!mesh || mesh->distance>3.){SetWindowTextW(status,L"Move within three metres of the object.");return;}}
        selected=hit;behaviorTarget=scene.Characters[hit].Id;selectionIds={behaviorTarget};
        creationGuide.opened(behaviorTarget);update_creation_guide();
        if(openMenu==ObjectBehaviorMenu)toggle_menu(0);toggle_menu(ObjectBehaviorMenu);
        SetWindowTextW(status,wide("Behavior for "+scene.Characters[hit].Name+". Choose an action; test it with F in Play.").c_str());
        SetFocus(GetDlgItem(menuPanel,BehaviorOpen));
    }
    void refresh() {
        if(!carriedId.empty())finish_carry(false);
        gameplayReport.clear();
        update_creation_guide();
        if(!playing)adventure.reset();
        if(explorer){
            if(scene.Game)explorer->rebuild_for_authoring(scene);
            else {explorer.reset();explorerLook=false;ShowWindow(adventureHudPanel,SW_HIDE);}
            explorerNeedsRefresh=false;
        }
        SendMessage(list, LB_RESETCONTENT, 0, 0);
        for (const auto &c : scene.Characters)
            SendMessage(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(c.Name).c_str()));
        for (size_t i = 0; i < scene.Characters.size(); i++)
            SendMessage(list, LB_SETSEL, selectionIds.contains(scene.Characters[i].Id) || int(i) == selected,
                        i);
        inspector_fields();
        install_scene(scene);drop_preparation();
        if(gamePreview)gamePreview->sync(scene);
        renderer->tools(actor() ? actor()->Id : std::string{}, currentMode);
        camera();
        SetWindowTextW(window, (std::wstring(L"Starlight Engine Editor") + (dirty ? L" *" : L"")).c_str());
    }

    template <class F> void edit(F f, bool rebuildControls = true) {
        if(playing && adventure)throw std::runtime_error("Pause the gameplay test before changing the world");
        history.begin(scene);
        try {
            f();
            scene.validate();
            render_scene(scene); // Validate assets and renderer capacity before committing the edit.
            history.commit();
            dirty = true;
            if (rebuildControls) refresh();
            else {
                explorerNeedsRefresh=bool(explorer);
                // Live slider edits must preserve the control and its mouse capture.
                install_scene(scene);drop_preparation();
                renderer->tools(actor() ? actor()->Id : std::string{}, currentMode);
                camera();
                SetWindowTextW(window, L"Starlight Engine Editor *");
            }
        } catch (...) {
            history.cancel(scene);
            throw;
        }
    }

    bool discard() {
        if (!dirty) return true;
        auto answer = MessageBoxW(window, L"Save your changes before continuing?\nYes: Save   No: Discard   Cancel: Keep editing", L"Starlight", MB_YESNOCANCEL | MB_ICONQUESTION);
        return answer == IDNO || (answer == IDYES && save(false));
    }
    std::optional<SceneData> open_scene_data(const fs::path& path, bool& repaired, LoadingScreen* loading = nullptr, std::string* hash = nullptr) {
        if (loading) {
            loading->show(window, view);
            loading->update(0, "Reading " + utf8(path.filename().wstring()));
        }
        auto next = open_with_context(path, "scene", [&] { return load_scene(path, hash); });
        auto result = repair_scene_models(next, [&](const std::string&, const std::string& objectName) -> std::optional<fs::path> {
            auto title = L"Locate missing model: " + wide(objectName);
            if (loading) loading->close();
            auto files = file_dialog(window, "repair-models", {{L"Replacement GLB model", L"*.glb"}},
                false, false, path.parent_path(), title);
            if (loading) loading->show(window, view);
            if (files.empty()) return std::nullopt;
            return files.front();
        });
        if (!result) return std::nullopt;
        repaired = *result != 0;
        return next;
    }
    bool save(bool choose) {
        auto target = filename;
        if (choose || target.empty()) {
            auto files = file_dialog(window, "scenes", {{L"Starlight scene", L"*.starscene"}}, true);
            if (files.empty()) return false;
            target = files[0];
        }
        std::string savedHash;
        try {
            savedHash = save_scene(scene, target, target == filename ? diskHash : std::nullopt);
        } catch (const MissingAssetsError &missing) {
            const auto question = std::string(missing.what()) + ".\nSave anyway? The scene will not open fully until those assets are restored.";
            if (MessageBoxW(window, wide(question).c_str(), L"Starlight", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return false;
            savedHash = save_scene(scene, target, target == filename ? diskHash : std::nullopt, true);
        }
        filename = target; diskHash = savedHash; dirty = false; refresh();
        return true;
    }
    void apply() {
        int page = int(SendMessage(tab, CB_GETCURSEL, 0, 0));
        if (page == 2) {
            panSpeed = number("pan"); tiltSpeed = number("tilt"); arrowPan = checked("arrows"); return;
        }
        if (page == 4) { radius = number("radius"); strength = number("strength"); paintMaterial = integer("paint"); }
        if (page == 5) {
            double spacing=inputs.contains("gridStep") ? number("gridStep") : gridStep;
            double angle=inputs.contains("rotationStep") ? number("rotationStep") : rotationStep;
            if(spacing<.01 || spacing>100 || angle<.1 || angle>180)
                throw std::runtime_error("Grid spacing must be 0.01 to 100 metres; rotation step must be 0.1 to 180 degrees");
            gridStep=spacing;rotationStep=angle;rotationSnap=checked("rotationSnap");
            gridSnap = checked("gridSnap"); groundSnap = checked("groundSnap"); objectSnap = checked("objectSnap");
        }
        if (page == 0 && inputs.contains("follow") && checked("follow") != follow) command(Follow);
        edit([&] {
            auto n = [&](const char* key, double& value) { if (inputs.contains(key)) value = number(key); };
            auto b = [&](const char* key, bool& value) { if (inputs.contains(key)) value = checked(key); };
            if (page == 0) {
                auto c = actor(); if (!c) return;
                double x = c->X, z = c->Z; n("x", x); n("z", z);
                for (auto& p : c->Route) { p.X += x - c->X; p.Z += z - c->Z; }
                c->Name = text(inputs.at("name")); c->X = x; c->Z = z;
                n("y", c->Y); n("size", c->Size); n("speed", c->Speed);
                n("exactSize", c->Size); n("exactSpeed", c->Speed);
                n("width", c->Width); n("height", c->Height); n("depth", c->Depth);
                n("rx", c->RotationX); n("ry", c->RotationY); n("rz", c->RotationZ);
                if (inputs.contains("color")) c->Color = text(inputs.at("color"));
                if (inputs.contains("surface")) c->Surface = integer("surface");
                if(c->Surface==12||c->Surface==13){auto f=c->Foliage.value_or(FoliageMaterial{});n("barkScale",f.BarkScale);n("barkRelief",f.BarkRelief);n("barkRough",f.BarkRoughness);n("leafTransmission",f.LeafTransmission);n("leafVeins",f.LeafVeins);n("leafRough",f.LeafRoughness);f.validate();c->Foliage=f;}else c->Foliage.reset();
                if(inputs.contains("jointKind")){c->JointKind=integer("jointKind");int target=integer("jointTarget");c->JointTargetId=target>0&&size_t(target)<=scene.Characters.size()?scene.Characters[target-1].Id:"";}
                n("jointX", c->JointX);
                n("jointY", c->JointY);
                n("jointZ", c->JointZ);
                n("jointTargetX", c->JointTargetX);
                n("jointTargetY", c->JointTargetY);
                n("jointTargetZ", c->JointTargetZ);
                n("jointAxisX", c->JointAxisX);
                n("jointAxisY", c->JointAxisY);
                n("jointAxisZ", c->JointAxisZ);
                n("VelocityX",c->VelocityX);
                n("VelocityY",c->VelocityY);
                n("VelocityZ",c->VelocityZ);
                n("AngularVelocityX",c->AngularVelocityX);
                n("AngularVelocityY",c->AngularVelocityY);
                n("AngularVelocityZ",c->AngularVelocityZ);
                n("JointAngleMin",c->JointAngleMin);
                n("JointAngleMax",c->JointAngleMax);
                if(inputs.contains("JointSegments"))c->JointSegments=integer("JointSegments");
                n("JointRadius",c->JointRadius);
                n("jointLength", c->JointLength);
                n("jointFrequency", c->JointFrequency);
                n("jointDamping", c->JointDamping);
                b("floor", c->KeepAboveFloor); b("dynamic", c->Dynamic); n("density", c->Density); n("friction", c->Friction); n("restitution", c->Restitution);
                n("intensity", c->LightIntensity); n("range", c->LightRange); n("cone", c->LightCone);
                n("ParticleRate",c->ParticleRate);
                n("ParticleLifetime",c->ParticleLifetime);
                n("ParticleDrag",c->ParticleDrag);
                n("ParticleLift",c->ParticleLift);
                n("ParticleWindX",c->ParticleWindX);
                n("ParticleWindY",c->ParticleWindY);
                n("ParticleWindZ",c->ParticleWindZ);
                n("lightningDamage",c->LightningDamage);n("lightningRadius",c->LightningChainRadius);if(inputs.contains("lightningChains"))c->LightningChains=integer("lightningChains");
                if(inputs.contains("lightningTarget")){int target=integer("lightningTarget");c->LightningTargetId=target>0&&size_t(target)<=scene.Characters.size()?scene.Characters[target-1].Id:"";}
                n("tx", c->TargetX); n("ty", c->TargetY); n("tz", c->TargetZ); n("spread", c->BranchSpread);
            } else if (page == 1) {
                scene.Name = text(inputs.at("name"));
                n("hour", scene.Hour); b("cycle", scene.DayCycle); n("daylength", scene.DayLength); n("fog", scene.FogDensity);
                n("rain", scene.RainIntensity); n("rainwindx", scene.RainWindX); n("rainwindz", scene.RainWindZ);
                scene.BloomQuality=integer("bloomquality");n("bloomstrength",scene.BloomStrength);
                scene.ShadowQuality=integer("shadowquality");
                b("rt", scene.RaytracingEnabled); scene.RaytracingQuality = integer("tier");
                scene.PathBounces = integer("bounces"); b("rr", scene.RayReconstructionEnabled); b("freeze", scene.FreezeRaytracingAnimation);
            } else if(page==6){
                if(!scene.Game)scene.Game.emplace();
                n("projectileMass",scene.Game->ProjectileMass);n("projectileSpeed",scene.Game->ProjectileSpeed);n("projectileGravity",scene.Game->ProjectileGravity);n("sightRange",scene.Game->SightRange);n("moveSpeed",scene.Game->MoveSpeed);n("lookSpeed",scene.Game->LookSensitivity);
                b("reducedCameraMotion",scene.Game->ReducedCameraMotion);
                n("spawnX",scene.Game->SpawnX);n("spawnY",scene.Game->SpawnY);n("spawnZ",scene.Game->SpawnZ);
                n("playerRadius",scene.Game->PlayerRadius);b("playerCollision",scene.Game->Collision);
            } else if (page == 4) {
                if (!scene.Terrain) scene.Terrain.emplace();
                b("water", scene.Terrain->WaterEnabled); n("level", scene.Terrain->WaterLevel); b("lava", scene.Terrain->Lava);
                scene.Terrain->Breakable=SendMessage(inputs.at("terrainBreakable"),BM_GETCHECK,0,0)==BST_CHECKED;
                n("terrainDurability",scene.Terrain->DestructionHealth);n("terrainDamageRadius",scene.Terrain->DestructionRadius);
                n("waterwindx",scene.WaterWindX);n("waterwindz",scene.WaterWindZ);n("waterwaves",scene.WaterWaveStrength);
                n("lavaheat",scene.LavaInitialHeat);n("lavacooling",scene.LavaCoolingRate);n("lavacrust",scene.LavaCrustDepth);n("lavalight",scene.LavaLightStrength);
            } else if (page == 5 && integer("ground") < 6) scene.GroundColor = floorColors[std::clamp(integer("ground"),0,5)];
        });
    }

    void command(int id) {
        if(id==BehaviorCarry){
            if(playing)throw std::runtime_error("Pause to move authored objects");
            auto c=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==behaviorTarget;});if(c==scene.Characters.end())throw std::runtime_error("Object no longer exists");
            carriedId=c->Id;carryPoint={c->X,c->Y,c->Z};
            if(explorer){auto collisionScene=scene;std::erase_if(collisionScene.Characters,[&](const auto& c){return c.Id==carriedId;});explorer->rebuild_for_authoring(collisionScene);}
            toggle_menu(0);SetFocus(view);SetWindowTextW(status,L"Carrying: move or point at the ground. F places; Escape cancels.");return;
        }
        if(id==LearnOpen || id==LearnBreak || id==LearnWorkshop){if(playing)throw std::runtime_error("Pause before starting a creation lesson");creationGuide.start(scene,id==LearnWorkshop?"workshop":id==LearnOpen?"door":"breakable");if(!explorer)command(InteractiveBuild);update_creation_guide();toggle_menu(0);SetFocus(view);return;}
        if(id==EndCreationGuide){creationGuide.active=false;if(!playing)questTracker.hide();toggle_menu(0);return;}
        if(id>=BehaviorOpen && id<=BehaviorStandQuest){
            if(playing)throw std::runtime_error("Pause before changing object behavior");
            auto object=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==behaviorTarget;});
            if(object==scene.Characters.end())throw std::runtime_error("That object no longer exists");
            edit([&]{if(id==BehaviorQuest || id==BehaviorStandQuest)add_object_quest(scene,behaviorTarget,id==BehaviorStandQuest);
                else assign_object_behavior(scene,behaviorTarget,id==BehaviorOpen?"door":id==BehaviorBreak?"breakable":id==BehaviorCollect?"key":"none");});
            SetWindowTextW(status,id==BehaviorQuest || id==BehaviorStandQuest?L"Added to Object interactions quest. Save the scene to keep it.":L"Behavior saved in the scene. Press Play and F to test it; Undo is available.");
            return;
        }
        if (help_command(id)) return;
        if (selection_clipboard_command(id)) return;
        if (asset_browser_command(id)) return;
        if (reference_main_command(id)) return;
        if(gameplay_rule_command(id))return;
        if(id==ApplyGameplay){
            if(playing)throw std::runtime_error("Pause before changing gameplay rules");
            if(!actor())return;
            auto candidate=*actor();int role=integer("gameRole");
            if(role<0||role>4)throw std::runtime_error("Choose an interaction");
            candidate.GameplayRole=std::array<std::string,5>{"none","key","door","exit","breakable"}[role];
            candidate.Breakable=checked("breakable");candidate.DestructionHealth=number("destructionHealth");candidate.GameplayKey=text(inputs.at("gameKey"));candidate.GrappleMode=integer("grappleMode");candidate.CombatHealth=number("combatHealth");candidate.CombatSightRange=number("combatSight");candidate.CombatTeam=integer("combatTeam");candidate.CombatBoss=checked("combatBoss");candidate.CombatAutoAttack=checked("combatAutoAttack");candidate.CombatPursuit=checked("combatPursuit");candidate.CombatControlEffect=integer("combatControl")-1;candidate.CombatProjectileSpeed=number("CombatProjectileSpeed");candidate.CombatProjectileGravity=number("CombatProjectileGravity");candidate.CombatProjectileMass=number("CombatProjectileMass");candidate.validate();
            edit([&]{*actor()=candidate;});return;
        }
        if (id == ApplyExact || id == ApplyBuild || id == ApplyBody || id == ApplyLightning || id == ApplyParticles || id == ApplyExtras) {
            if (!actor()) return;
            auto candidate = *actor();
            const bool requestedFollow = id == ApplyExtras && inputs.contains("follow") ? checked("follow") : follow;
            if (id == ApplyExact) {
                const double x = number("x"), z = number("z");
                const double speed = number("exactSpeed"), size = number("exactSize");
                if (std::abs(x) > 12 || std::abs(z) > 12)
                    throw std::runtime_error("Position must be between -12 and 12 metres on this preview floor.");
                if (speed < .2 || speed > 5)
                    throw std::runtime_error("Speed must be between 0.2 and 5 metres per second.");
                if (size < .3 || size > 3)
                    throw std::runtime_error("Size must be between 0.3 and 3.");
                for (auto& point : candidate.Route) {
                    point.X += x - candidate.X; point.Z += z - candidate.Z;
                }
                candidate.X = x; candidate.Z = z;
                candidate.Speed = speed; candidate.Size = size;
            } else if (id == ApplyBuild) {
                candidate.Y = number("y"); candidate.Width = number("width");
                candidate.Height = number("height"); candidate.Depth = number("depth");
                candidate.RotationX = number("rx"); candidate.RotationY = number("ry");
                candidate.RotationZ = number("rz"); candidate.Color = text(inputs.at("color"));
                candidate.Surface = integer("surface"); candidate.KeepAboveFloor = checked("floor");
                candidate.validate();
                snap_object(scene, candidate, false, false, false);
            } else if (id == ApplyBody) {
                candidate.JointKind=integer("jointKind");int target=integer("jointTarget");candidate.JointTargetId=target>0&&size_t(target)<=scene.Characters.size()?scene.Characters[target-1].Id:"";
                candidate.JointX=number("jointX");
                candidate.JointY=number("jointY");
                candidate.JointZ=number("jointZ");
                candidate.JointTargetX=number("jointTargetX");
                candidate.JointTargetY=number("jointTargetY");
                candidate.JointTargetZ=number("jointTargetZ");
                candidate.JointAxisX=number("jointAxisX");
                candidate.JointAxisY=number("jointAxisY");
                candidate.JointAxisZ=number("jointAxisZ");
                candidate.VelocityX=number("VelocityX");
                candidate.VelocityY=number("VelocityY");
                candidate.VelocityZ=number("VelocityZ");
                candidate.AngularVelocityX=number("AngularVelocityX");
                candidate.AngularVelocityY=number("AngularVelocityY");
                candidate.AngularVelocityZ=number("AngularVelocityZ");
                candidate.JointAngleMin=number("JointAngleMin");
                candidate.JointAngleMax=number("JointAngleMax");
                candidate.JointSegments=integer("JointSegments");
                candidate.JointRadius=number("JointRadius");
                candidate.JointLength=number("jointLength");
                candidate.JointFrequency=number("jointFrequency");
                candidate.JointDamping=number("jointDamping");
                candidate.Dynamic = checked("dynamic"); candidate.Density = number("density"); candidate.Friction=number("friction"); candidate.Restitution=number("restitution");
            } else if(id==ApplyParticles){
                candidate.ParticleRate=number("ParticleRate");
                candidate.ParticleLifetime=number("ParticleLifetime");
                candidate.ParticleDrag=number("ParticleDrag");
                candidate.ParticleLift=number("ParticleLift");
                candidate.ParticleWindX=number("ParticleWindX");
                candidate.ParticleWindY=number("ParticleWindY");
                candidate.ParticleWindZ=number("ParticleWindZ");
            } else if (id == ApplyLightning) {
                candidate.LightningDamage=number("lightningDamage");candidate.LightningChainRadius=number("lightningRadius");candidate.LightningChains=integer("lightningChains");
                int target=integer("lightningTarget");candidate.LightningTargetId=target>0&&size_t(target)<=scene.Characters.size()?scene.Characters[target-1].Id:"";
                candidate.TargetX = number("tx"); candidate.TargetY = number("ty");
                candidate.TargetZ = number("tz"); candidate.BranchSpread = number("spread");
            } else {
                if (inputs.contains("intensity")) candidate.LightIntensity = number("intensity");
                if (inputs.contains("range")) candidate.LightRange = number("range");
                if (inputs.contains("cone")) candidate.LightCone = number("cone");
            }
            candidate.validate();
            edit([&] { *actor() = candidate; });
            if (requestedFollow != follow) command(Follow);
            return;
        }
        if (id == Advanced || id == BuildDetails || id == BodyDetails) {
            if (id == Advanced) advanced = !advanced;
            if (id == BuildDetails) buildDetails = !buildDetails;
            if (id == BodyDetails) bodyDetails = !bodyDetails;
            SendMessage(GetDlgItem(assetPanel, Advanced), BM_SETCHECK, advanced ? BST_CHECKED : BST_UNCHECKED, 0);
            inspector_fields(); layout(); return;
        }
        if (id == DockSettings) { show_inspector(dockSection == BuildTerrain ? 4 : 5); return; }
        if (id == ClearRoute || id == ClearPours) {
            if (playing) throw std::runtime_error("Pause playback before editing");
            edit([&] { if (id == ClearPours) scene.LiquidSources.clear(); else if (actor()) actor()->Route.clear(); }); return;
        }
        if (id == PlaceCharacter) {place_character(placement_point());return;}
        if(id==ShowMusic){ensure_secondary_panels();ShowWindow(morePanel,SW_HIDE);ShowWindow(musicPanel,SW_SHOW);music_controls();return;}
        if(id==CloseMore){ShowWindow(morePanel,SW_HIDE);return;}
        if(id==CloseMusic){ShowWindow(musicPanel,SW_HIDE);return;}
        if(id==FeatureList||id==FeatureCheck){feature_selection(id==FeatureCheck);return;}
        if(id==MusicToggle){command(musicPlaying?MusicPause:MusicPlay);return;}
        if (id == MenuFile || id == MenuBuilder || id == MenuWorld || id == MenuMore) {
            toggle_menu(id);
            return;
        }
        if (id == MenuAssets) {
            assetsShown = !assetsShown;
            toggle_menu(0);
            layout();
            return;
        }
        if (id == MenuCamera) {
            show_inspector(2);
            toggle_menu(0);
            return;
        }
        if (id == ObjectProperties) {
            assetsShown = false; show_inspector(0);
            return;
        }
        if (id == MenuWorld + 100) {
            show_inspector(dockSection == BuildTerrain && !openMenu ? 4 : 1);
            toggle_menu(0);
            return;
        }
        if (id == CloseInspector) {
            inspectorShown = false;
            layout();
            return;
        }
        if (id == CloseDock) {
            dockShown = false;
            SendMessage(mode, CB_SETCURSEL, 0, 0);
            command(Mode);
            toggle_menu(0);
            layout();
            return;
        }
        if (id == BuildTerrain || id == BuildShapes || id == BuildSculpt || id == BuildNature) {
            end_stroke(); pendingPour.reset();
            dockShown = true;
            build_dock(id);
            int selection = id == BuildTerrain ? 5 : id == BuildSculpt ? 6 : 1;
            SendMessage(mode, CB_SETCURSEL, selection, 0);
            currentMode = text(mode);
            toggle_menu(0);
            renderer->tools(actor() ? actor()->Id : std::string{}, currentMode);
            dock_selection();
            layout();
            return;
        }
        if (id == 2300 || id == 2301) {
            toggle_menu(0);
            SendMessage(mode, CB_SETCURSEL, id == 2300 ? 4 : 0, 0);
            command(Mode);
            dock_selection();
            return;
        }
        if (id >= 2200 && id < 2203) {
            SendMessage(mode, CB_SETCURSEL, id - 2199, 0);
            command(Mode);
            dock_selection();
            return;
        }
        if (id >= 2100 && id < 2112) {
            SendMessage(brush, CB_SETCURSEL, id - 2100, 0);
            command(Brush);
            dock_selection();
            return;
        }
        if (id >= 2000 && id < 2015) {
            SendMessage(shape, CB_SETCURSEL, id - 2000, 0);
            toggle_menu(0);
            dockShown = true;
            layout();
            command(Add);
            return;
        }
        if (openMenu)
            toggle_menu(0);

        if (id == MusicOpen) {
            auto files = file_dialog(window, "music", {{L"Audio", L"*.mp3;*.wav;*.wma;*.m4a"}}, false, true);
            if (!files.empty()) {
                songs = std::move(files);
                songIndex = 0;
                play_song();
            }
            return;
        }
        if (id == MusicPlay) {
            music.play();musicPlaying=!songs.empty();music_controls();
            return;
        }
        if (id == MusicPause) {
            music.pause();musicPlaying=false;music_controls();
            return;
        }
        if (id == MusicStop) {
            music.stop();musicPlaying=false;music_controls();
            return;
        }
        if (id == MusicNext) {
            if (!songs.empty()) {
                songIndex = (songIndex + 1) % songs.size();
                play_song();
            }
            return;
        }
        if (id == MusicQuieter || id == MusicLouder) {
            musicVolume = std::clamp(musicVolume + (id == MusicLouder ? .1 : -.1), 0., 1.);
            music.volume(musicVolume);music_controls();
            return;
        }
        if(id==TogglePacing){pacingShown=!pacingShown;renderer->pacing_enabled(pacingShown);toggle_menu(0);SetFocus(view);pacingHud.update(pacingShown,*renderer,view,camera_state(),settingsScale);return;}
        if (id == FullScreen) {
            // Set the target mode before SetWindowPos dispatches WM_SIZE.
            // Otherwise layout requests windowed dimensions for the fullscreen
            // window (or vice versa), forcing another renderer recreation.
            fullScreen = !fullScreen;
            if (fullScreen) {
                GetWindowPlacement(window, &placement);
                MONITORINFO monitor{sizeof(monitor)};
                GetMonitorInfo(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
                SetWindowLongPtr(window, GWL_STYLE,
                                 GetWindowLongPtr(window, GWL_STYLE) & ~WS_OVERLAPPEDWINDOW);
                SetWindowPos(window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                             monitor.rcMonitor.right - monitor.rcMonitor.left,
                             monitor.rcMonitor.bottom - monitor.rcMonitor.top, SWP_FRAMECHANGED);
            } else {
                SetWindowLongPtr(window, GWL_STYLE,
                                 GetWindowLongPtr(window, GWL_STYLE) | WS_OVERLAPPEDWINDOW);
                SetWindowPlacement(window, &placement);
                SetWindowPos(window, nullptr, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            }
            layout();
            return;
        }
        if (id >= ViewAbove && id <= ViewFocus) {
            end_stroke();
            if (auto c = actor()) {
                auto b = object_bounds(*c);
                if (id == ViewFocus) {
                    for (const auto &object : scene.Characters) {
                        if (object.Id == c->Id || !selectionIds.contains(object.Id)) continue;
                        auto other = object_bounds(object);
                        b.low = {std::min(b.low.x, other.low.x), std::min(b.low.y, other.low.y), std::min(b.low.z, other.low.z)};
                        b.high = {std::max(b.high.x, other.high.x), std::max(b.high.y, other.high.y), std::max(b.high.z, other.high.z)};
                    }
                }
                focusX = (b.low.x + b.high.x) / 2;
                focusY = (b.low.y + b.high.y) / 2;
                focusZ = (b.low.z + b.high.z) / 2;
                if (id == ViewFocus) {
                    auto extent = b.high - b.low;
                    distance = std::clamp(std::max({extent.x, extent.y, extent.z}) * 3, 3., 150.);
                }
            } else focusY = 1;
            pitch = id == ViewAbove ? 1.48 : id == ViewSide ? 0 : id == ViewLow ? -.06 : .35;
            if (id == ViewLow) {
                double radius = distance * std::sqrt(1 + .72 * .72);
                focusY = std::max(focusY, .2 - std::sin(pitch) * radius + (scene.Terrain ? scene.Terrain->height_at(focusX, focusZ) : 0));
            }
            camera();
            return;
        }
        if (id == PourWater || id == PourLava) {
            if (playing) throw std::runtime_error("Pause playback before editing");
            end_stroke(); pendingPour = id == PourLava;
            inspectorShown = false; layout();
            SetWindowTextW(status, L"Click the ground to place the pour · Escape cancels");
            SetFocus(view); return;
        }
        if (id == Stamp) {
            if (!actor())
                return;
            edit([&] {
                if (!scene.Terrain)
                    scene.Terrain.emplace();
                stamp_shape(*scene.Terrain, *actor());
            });
            return;
        }

        if (id == Projects) {
            open_project();
            return;
        }
        if (id >= ChooseProject && id <= LaunchProject) {
            if (presetTask.valid())
                throw std::runtime_error("Wait for the current settings check");
            if (id == ChooseProject) {
                auto files = file_dialog(projectWindow && IsWindowVisible(projectWindow)?projectWindow:window, "projects", {{L"Starlight project", L"*.star"}});
                if (files.empty())
                    return;
                auto next = ProjectData::load(files[0]);
                project = std::move(next);
                preset.reset();
                if(projectWindow && IsWindowVisible(projectWindow))project_form();else inspector_fields();
                return;
            }
            if (!project)
                throw std::runtime_error("Choose a project first");
            project->selected = text(inputs.at("scene"));
            if (id == ChoosePreset) {
                auto files = file_dialog(projectWindow && IsWindowVisible(projectWindow)?projectWindow:window, "presets", {{L"Starlight test preset", L"*.startest"}});
                if (!files.empty())
                    begin_preset(id, files[0]);
                return;
            }
            if (!preset)
                throw std::runtime_error("Load a preset first");
            auto draft = preset->data;
            auto &v = draft["settings"];
            v["resolution"] = {integer("width"), integer("height")};
            v["frameLimit"] = integer("fps");
            v["durationMs"] = integer("duration");
            v["displayMode"] = text(inputs.at("display"));
            v["vsync"] = checked("vsync");
            v["culling"] = checked("culling");
            v["foreground"] = checked("foreground");
            v["upscaler"] = "off";
            v["quality"] = "native";
            v["frameGeneration"] = false;
            auto path =
                application_data() / L"Diagnostics" / L"NativeEditor" / (wide(new_id()) + L".startest");
            atomic_write(path, draft.dump(2));
            begin_preset(id, path);
            return;
        }
        if (id == Mode) {
            end_stroke(); pendingPour.reset();
            currentMode = text(mode);
            renderer->tools(actor() ? actor()->Id : std::string{}, currentMode);
            return;
        }
        if (id == Brush) {
            currentBrush = text(brush);
            return;
        }
        if (id == InspectorTab) {
            inspector_fields();
            layout();
            return;
        }
        if (id == ObjectList) {
            selected = int(SendMessage(list, LB_GETCARETINDEX, 0, 0));
            selectionIds.clear();
            for (size_t i = 0; i < scene.Characters.size(); i++)
                if (SendMessage(list, LB_GETSEL, i, 0) > 0)
                    selectionIds.insert(scene.Characters[i].Id);
            inspector_fields();
            return;
        }
        if (id == Save) {
            save(false);
            return;
        }
        if (id == SaveAs) {
            save(true);
            return;
        }
        if (id == Apply) {
            apply();
            return;
        }
        if (id == Repeat || id == Follow) {
            if (id == Repeat)
                repeat = !repeat;
            else
                follow = !follow;
            renderer->playback_options(repeat, follow && actor() ? actor()->Id : std::string{});
            if(gamePreview)gamePreview->options(repeat,follow&&actor()?actor()->Id:std::string{});
            return;
        }
        if (id == Play || id == Pause || id == Stop) {playback_command(id);return;}
        if (playing)
            throw std::runtime_error("Pause playback before editing");
        if (id == SaveObject) {
            if (!actor()) throw std::runtime_error("Select a shape or sculpt to save first.");
            encode_object_template(*actor()); // Reject unsupported objects before asking for a destination.
            auto files = file_dialog(window, "reusable-objects", {{L"Starlight reusable object", L"*.starobject"}}, true);
            if (!files.empty()) {
                save_object_template(*actor(), files.front());
                SetWindowTextW(status, L"Reusable object saved. Add it to any scene from the scene menu.");
            }
            return;
        }
        if (id == ReplaceModel) {
            auto object = actor();
            if (!object || object->Shape || object->Sculpt)
                throw std::runtime_error("Select an imported model to replace. Shapes and sculpts keep their own geometry.");
            auto files = file_dialog(window, "replacement-models", {{L"GLB model", L"*.glb"}});
            if (files.empty()) return;
            auto asset = import_model(fs::absolute(files.front()).lexically_normal());
            edit([&] { actor()->Asset = asset; });
            SetWindowTextW(status, L"Model replaced. Name, placement and route kept. Undo restores the previous model.");
            return;
        }
        if (id == SaveGroup) {
            std::vector<Character> objects;
            for (size_t i = 0; i < scene.Characters.size(); ++i)
                if (int(i) == selected || selectionIds.contains(scene.Characters[i].Id)) objects.push_back(scene.Characters[i]);
            encode_object_group(objects);
            auto files = file_dialog(window, "reusable-groups", {{L"Starlight reusable group", L"*.stargroup"}}, true);
            if (!files.empty()) {
                save_object_group(objects, files.front());
                SetWindowTextW(status, L"Selected objects saved together, including their spacing and routes.");
            }
            return;
        }
        if (id == LoadGroup || id == MergeScene) {
            auto files = id == MergeScene
                ? file_dialog(window, "scene-objects", {{L"Starlight scene objects", L"*.starscene"}})
                : file_dialog(window, "reusable-groups", {{L"Starlight reusable group", L"*.stargroup"}});
            if (files.empty()) return;
            std::vector<Character> objects;
            if (id == MergeScene) {
                bool repaired = false;
                auto source = open_scene_data(files.front(), repaired);
                if (!source) return;
                objects = std::move(source->Characters);
            } else objects = open_with_context(files.front(), "reusable group", [&] { return load_object_group(files.front()); });
            renew_object_ids(objects);
            place_object_group(objects, scene, placement_point().x, placement_point().z, gridSnap, objectSnap, groundSnap,gridStep);
            const auto oldSelected = selected;
            const auto oldSelectionIds = selectionIds;
            try {
                edit([&] {
                    selected = int(scene.Characters.size()); selectionIds.clear();
                    for (auto &object : objects) {
                        selectionIds.insert(object.Id); scene.Characters.push_back(std::move(object));
                    }
                });
            } catch (...) { selected = oldSelected; selectionIds = oldSelectionIds; throw; }
            SetWindowTextW(status, L"Objects added and selected. Current world settings kept. Undo removes the entire insertion.");
            return;
        }
        if (id == ExportPortable || id == ExportSelection) {
            std::vector<std::string> ids;
            if (id == ExportSelection) {
                for (size_t i = 0; i < scene.Characters.size(); ++i)
                    if (int(i) == selected || selectionIds.contains(scene.Characters[i].Id)) ids.push_back(scene.Characters[i].Id);
                if (ids.empty()) throw std::runtime_error("Select objects to export first");
            }
            auto files = file_dialog(window, "portable-scenes", {{L"New portable scene folder", L"*.starbundle"}}, true);
            if (files.empty()) return;
            if (id == ExportSelection) export_scene_selection(scene, ids, files.front());
            else export_portable_scene(scene, files.front());
            SetWindowTextW(status, L"Portable scene exported. Open scene.starscene inside that folder; copy the whole folder to share.");
            return;
        }
        if (id == LoadObject) {
            auto files = file_dialog(window, "reusable-objects", {{L"Starlight reusable object", L"*.starobject"}});
            if (files.empty()) return;
            auto object = open_with_context(files.front(), "reusable object", [&] { return load_object_template(files.front()); });
            place_object_template(object, placement_point().x, placement_point().z);
            const auto oldSelected = selected;
            const auto oldSelectionIds = selectionIds;
            try {
                edit([&] {
                    snap_object(scene, object, gridSnap, objectSnap, groundSnap,gridStep);
                    scene.Characters.push_back(std::move(object));
                    selected = int(scene.Characters.size()) - 1;
                    selectionIds.clear();
                });
            } catch (...) { selected = oldSelected; selectionIds = oldSelectionIds; throw; }
            SetWindowTextW(status, L"Saved object added. Undo removes this copy without changing the saved object.");
            return;
        }
        if(id==CheckGameplay){
            auto findings=adventure_readiness(scene);
            std::string message=findings.empty() ? "Basic setup checks passed. Test the whole adventure to check reachability and playability." : "Gameplay setup: "+std::to_string(findings.size())+" item(s) to review.";
            for(const auto& finding:findings)message+="\r\n\r\n"+finding;
            gameplayReport="Applied scene settings\r\n\r\n"+message;
            show_inspector(6);
            SetWindowTextW(status,wide(findings.empty() ? "Basic gameplay setup checks passed; playthrough still required." : findings.front()).c_str());
            return;
        }
        if(id==InteractiveBuild){
            end_stroke();
            if(explorer){
                explorer.reset();explorerLook=false;ShowWindow(adventureHudPanel,SW_HIDE);camera();
                SetWindowTextW(status,L"Returned to editor view. World edits are preserved.");
            }else{
                if(!scene.Game)edit([&]{scene.Game.emplace();});
                if(!start_walk_mode()){toggle_menu(0);return;}
                camera();
                explorer_hint();SetFocus(view);
                SetWindowTextW(status,L"Walk and build: W A S D, right-drag to look, use Builder tools with left click. Play tests your world.");
            }
            toggle_menu(0);return;
        }
        if(id==ExportGame){
            if(!scene.Game)throw std::runtime_error("Choose adventure controls first");
            auto folders=file_dialog(window,"game-exports",{},false,false,{},L"Choose where to create the StarlightGame folder",true);
            if(folders.empty())return;
            auto report=export_playable_game(scene,folders[0]/L"StarlightGame",engine_path().parent_path());
            auto message="Playable game exported: "+report.at("folder").get<std::string>();
            if(!report.at("gameplayFindings").empty())message+=" | "+std::to_string(report.at("gameplayFindings").size())+" setup notes: see gameplay-check.json or Check gameplay setup.";
            SetWindowTextW(status,wide(message).c_str());return;
        }
        if(id==AdventurePreset){
            if(playing)throw std::runtime_error("Stop playing before choosing a game preset");
            edit([&]{if(!scene.Game)scene.Game.emplace();});
            adventure.reset();show_inspector(6);toggle_menu(0);
            SetWindowTextW(status,L"Adventure ready: Play, then W A S D to walk, right-drag to look. Escape pauses.");
            return;
        }
        if (id == New || id == NewAdventure || id == NewQuestAdventure) {
            if(playing)throw std::runtime_error("Stop playing before creating a new scene");
            if (!discard())
                return;
            creationGuide.active=false;questTracker.hide();
            scene = id==NewQuestAdventure ? quest_template() : id==NewAdventure ? adventure_template() : SceneData{};
            explorer.reset();explorerLook=false;ShowWindow(adventureHudPanel,SW_HIDE);
            history.clear();
            filename.clear();
            diskHash.reset();
            selected = -1;
            selectionIds.clear();
            dirty = id==NewAdventure || id==NewQuestAdventure;
            refresh();
            if(id==NewAdventure || id==NewQuestAdventure){
                show_inspector(6);toggle_menu(0);
                SetWindowTextW(status,id==NewQuestAdventure?L"Quest adventure: Play to see objectives. Click a quest title for details.":L"Your adventure: Play, collect the gold token with E, open the door, then reach the green exit.");
            }
        } else if (id == Open) {
            if (!discard())
                return;
            auto files = file_dialog(window, "scenes", {{L"Starlight scene", L"*.starscene"}});
            if (files.empty())
                return;
            bool repaired = false;
            LoadingScreen loading;
            std::string loadedHash;
            auto next = open_scene_data(files[0], repaired, &loading, &loadedHash);
            if (!next) return;
            open_with_context(files[0], "scene", [&] { render_scene(*next); });
            scene = std::move(*next);
            explorer.reset();explorerLook=false;ShowWindow(adventureHudPanel,SW_HIDE);
            filename = files[0];
            diskHash = loadedHash;
            history.clear();
            selected = -1;
            dirty = repaired;
            refresh();
            const bool settled = settle_scene(loading, "Preparing " + utf8(files[0].filename().wstring()));
            if (settled && repaired) SetWindowTextW(status, L"Missing models located. Save to keep the repaired paths; original scene file is unchanged.");
        } else if (id == Import) {
            auto files = file_dialog(window, "models", {{L"GLB model", L"*.glb"}});
            if (files.empty())
                return;
            characterModelId = import_model(files[0]);
            characterAsset = files[0];
            save_default_character();
            SetWindowTextW(characterCard, wide("★  " + utf8(characterAsset.stem().wstring())).c_str());
        } else if (id == Add) {
            edit([&] {
                Character c;
                c.Shape = text(shape);
                c.Name = *c.Shape + " " + std::to_string(scene.Characters.size() + 1);
                auto placement=placement_point();c.X=placement.x;c.Z=placement.z;
                c.Y = scene.Terrain ? scene.Terrain->height_at(c.X, c.Z) : 0;
                auto k = *c.Shape;
                c.Dynamic = k == "rock" || k == "boulder";
                c.Color = k == "grass"         ? "#659540"
                          : k == "chaos-rock"  ? "#36323F"
                          : k == "light-shard" ? "#AB7CFF"
                                               : "#8A8E87";
                c.Surface = k == "grass" ? 1 : k == "chaos-rock" ? 10 : k == "light-shard" ? 11 : 6;
                c.LightIntensity = k == "chaos-rock" ? 2 : k == "light-shard" ? 4 : 12;
                scene.Characters.push_back(c);
                selected = int(scene.Characters.size()) - 1;
            });
        } else if (id == Undo || id == Redo) {
            end_stroke();
            bool changed = id == Undo ? history.undo(scene) : history.redo(scene);
            if (changed) {
                selected = -1;
                dirty = true;
                refresh();
            }
        } else if (id == Delete && actor()) {
            auto ids = selectionIds;
            ids.insert(actor()->Id);
            const auto oldSelected = selected;
            const auto oldSelectionIds = selectionIds;
            try {
                edit([&] {
                    std::erase_if(scene.Characters, [&](const Character &object) { return ids.contains(object.Id); });
                    for(auto& object:scene.Characters)if(ids.contains(object.JointTargetId)){object.JointKind=0;object.JointTargetId.clear();}for(auto& object:scene.Characters)if(ids.contains(object.LightningTargetId))object.LightningTargetId.clear();
                    selected = -1; selectionIds.clear();
                });
            } catch (...) { selected = oldSelected; selectionIds = oldSelectionIds; throw; }
            if (auto stale = rules_naming_objects(scene, ids); !stale.empty()) {
                std::string list;
                for (size_t i = 0; i < stale.size() && i < 3; ++i) list += (i ? ", " : "") + stale[i];
                if (stale.size() > 3) list += ", and " + std::to_string(stale.size() - 3) + " more";
                SetWindowTextW(status, wide("Deleted. These gameplay rules still point at a deleted object: " + list + ". Edit or remove them before playing.").c_str());
            }
        } else if (id == Duplicate && actor()) {
            std::vector<Character> copies;
            for (size_t i = 0; i < scene.Characters.size(); ++i)
                if (int(i) == selected || selectionIds.contains(scene.Characters[i].Id)) {
                    auto copy = scene.Characters[i]; copy.Name += " copy";
                    copies.push_back(std::move(copy));
                }
            renew_object_ids(copies);
            place_object_group(copies, scene, copies.front().X + .5, copies.front().Z);
            const auto oldSelected = selected;
            const auto oldSelectionIds = selectionIds;
            try {
                edit([&] {
                    selected = int(scene.Characters.size()); selectionIds.clear();
                    for (auto &copy : copies) { selectionIds.insert(copy.Id); scene.Characters.push_back(std::move(copy)); }
                });
            } catch (...) { selected = oldSelected; selectionIds = oldSelectionIds; throw; }
        } else if (id == Frame) {
            end_stroke();
            focusX = focusY = focusZ = 0;
            yaw = 0;
            pitch = .67;
            distance = 19;
            camera();
        }
    }

    void explorer_hint(){
        SetWindowTextW(adventureHud,L"BUILD: W A S D walk | Right-drag look | F: object menu / place | Play tests");
        ShowWindow(adventureHudPanel,SW_SHOW);
        SetWindowPos(adventureHudPanel,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    }
    Vec3 placement_point() const {
        double x=focusX,z=focusZ;
        if(explorer){x=explorer->position.x+std::sin(explorer->yaw)*3;z=explorer->position.z-std::cos(explorer->yaw)*3;}
        x=std::clamp(x,-12.,12.);z=std::clamp(z,-12.,12.);
        return {x,scene.Terrain ? scene.Terrain->height_at(x,z) : 0,z};
    }
    CameraState camera_state() {
        if(playing && adventure)return {adventure->camera_eye(),adventure->view_position()+adventure->camera_direction(),1,adventure->camera_up()};
        if(explorer)return {explorer->camera_eye(),explorer->view_position()+explorer->camera_direction(),1,explorer->camera_up()};
        double r = distance * std::sqrt(1 + .72 * .72);
        return {{focusX + std::sin(yaw) * std::cos(pitch) * r, focusY + std::sin(pitch) * r,
                 focusZ + std::cos(yaw) * std::cos(pitch) * r},
                {focusX, focusY, focusZ},
                distance};
    }

    void camera() { renderer->camera(camera_state()); }

    Vec3 ray(int x, int y) {
        RECT r;
        GetClientRect(view, &r);
        auto c = camera_state();
        auto forward = (c.target - c.eye).unit(), right = forward.cross({0, 1, 0}).unit(),
             up = right.cross(forward);
        double tan = std::tan(48 * std::numbers::pi / 360);
        return (forward + right * ((x * 2. / std::max(1L, r.right) - 1) * tan) +
                up * ((1 - y * 2. / std::max(1L, r.bottom)) * tan * r.bottom / std::max(1L, r.right)))
            .unit();
    }

    Vec3 floor_hit(int x, int y) {
        auto hit = terrain_hit(scene.Terrain ? &*scene.Terrain : nullptr, camera_state().eye, ray(x, y));
        if (!hit)
            throw std::runtime_error("Point the brush at the ground");
        return *hit;
    }
    std::optional<std::array<double, 2>> project_point(Vec3 p) {
        RECT r;
        GetClientRect(view, &r);
        auto camera = camera_state();
        auto f = (camera.target - camera.eye).unit(), right = f.cross({0, 1, 0}).unit(), up = right.cross(f),
             delta = p - camera.eye;
        double depth = delta.dot(f);
        if (depth <= .01)
            return {};
        double scale = r.right / (2 * std::tan(48 * std::numbers::pi / 360) * depth);
        return std::array<double, 2>{r.right * .5 + delta.dot(right) * scale,
                                     r.bottom * .5 - delta.dot(up) * scale};
    }
    bool try_handle(int x, int y) {
        if (!actor() || playing ||
            (currentMode != "Move" && currentMode != "Rotate" && currentMode != "Scale"))
            return false;
        for (auto h : gizmo_segments(*actor(), currentMode)) {
            auto a = project_point(h.a), b = project_point(h.b);
            if (!a || !b)
                continue;
            double dx = (*b)[0] - (*a)[0], dy = (*b)[1] - (*a)[1], length = dx * dx + dy * dy,
                   t = length > 0 ? std::clamp(((x - (*a)[0]) * dx + (y - (*a)[1]) * dy) / length, 0., 1.)
                                  : 0;
            if (t > .22 && std::hypot(x - (*a)[0] - dx * t, y - (*a)[1] - dy * t) < 9) {
                history.begin(scene);
                begin_group();
                stroke = true;
                dragAxis = h.axis;
                last = {x, y};
                return true;
            }
        }
        return false;
    }
    void drag_handle(int x, int y) {
        auto c = actor();
        if (!c)
            return;
        double dx = x - last.x, dy = y - last.y;
        if (currentMode == "Rotate") {
            double angle = (dx - dy) * .6;
            if (dragAxis == 0)
                c->RotationX += angle;
            else if (dragAxis == 1)
                c->RotationY += angle;
            else
                c->RotationZ += angle;
        } else {
            Vec3 axis = dragAxis == 0   ? Vec3{1, 0, 0}
                        : dragAxis == 1 ? Vec3{0, 1, 0}
                                        : Vec3{0, 0, 1},
                 origin{c->X, c->Y, c->Z};
            auto a = project_point(origin), b = project_point(origin + axis);
            double change = 0;
            if (a && b) {
                double vx = (*b)[0] - (*a)[0], vy = (*b)[1] - (*a)[1], length = vx * vx + vy * vy;
                if (length > 1)
                    change = (dx * vx + dy * vy) / length;
            }
            if (currentMode == "Scale") {
                auto &dimension = dragAxis == 0 ? c->Width : dragAxis == 1 ? c->Height : c->Depth;
                dimension = std::clamp(dimension + change, .05, 100.);
            } else if (dragAxis == 0) {
                c->X += change;
                for (auto &p : c->Route)
                    p.X += change;
            } else if (dragAxis == 1)
                c->Y += change;
            else {
                c->Z += change;
                for (auto &p : c->Route)
                    p.Z += change;
            }
        }
        snap_object(scene, *c, false, false, false);
        move_group(scene, *c, dragOriginals);
        dirty = true;
        install_scene(scene);drop_preparation();
    }

    int pick(int x, int y) {
        int result = -1;
        double nearest = INFINITY;
        for (size_t i = 0; i < scene.Characters.size(); i++) {
            const auto &c = scene.Characters[i];
            auto hit = ObjectCache::shared().raycast(c, c, camera_state().eye, ray(x, y));
            if (hit && hit->distance < nearest) {
                nearest = hit->distance;
                result = int(i);
            }
        }
        return result;
    }

    void pointer_down(int x, int y, UINT message) {
        if(!carriedId.empty() && message==WM_LBUTTONDOWN)return;
        if(playing && adventure){last={x,y};adventureLook=message==WM_RBUTTONDOWN || message==WM_LBUTTONDOWN;return;}
        if(explorer && message==WM_RBUTTONDOWN){last={x,y};explorerLook=true;return;}
        if(explorer && message==WM_MBUTTONDOWN)return;

        if (pendingPour && message == WM_LBUTTONDOWN && !playing) {
            auto p = terrain_hit(scene.Terrain ? &*scene.Terrain : nullptr, camera_state().eye, ray(x, y));
            if (!p) return;
            const bool lava = *pendingPour;
            edit([&] {
                if (scene.LiquidSources.size() >= 32) throw std::runtime_error("Maximum 32 liquid sources");
                if (!scene.Terrain) scene.Terrain.emplace();
                scene.Terrain->WaterEnabled = false;
                scene.LiquidSources.push_back({std::clamp(p->x, -12., 12.), std::clamp(p->z, -12., 12.), lava, 2});
            });
            pendingPour.reset(); return;
        }
        last = {x, y};
        terrainStrokeSurface.reset();terrainVolumeStamp.reset();
        reverseBrush = message == WM_RBUTTONDOWN;
        brushTick = GetTickCount64();
        if (message == WM_RBUTTONDOWN && currentMode != "Sculpt" && currentMode != "Terrain") {
            boxSelecting = true;
            boxStart = boxEnd = {x, y};
            return;
        }
        if (message == WM_MBUTTONDOWN) {
            orbit = true;
            return;
        }
        if (playing)
            return;
        if (currentMode == "Select" || currentMode == "Move" || currentMode == "Rotate" ||
            currentMode == "Scale" || currentMode == "Route" || currentMode == "Sculpt") {
            if (try_handle(x, y))
                return;
            int hit = pick(x, y);
            if (hit >= 0)
                selected = hit;
            for (size_t i = 0; i < scene.Characters.size(); i++)
                SendMessage(list, LB_SETSEL,
                            selectionIds.contains(scene.Characters[i].Id) || int(i) == selected, i);
            inspector_fields();
        }
        renderer->tools(actor() ? actor()->Id : std::string{}, currentMode);
        if (currentMode == "Select" || currentMode == "Rotate" || currentMode == "Scale")
            return;
        if (currentMode != "Sculpt") {
            auto ground =
                terrain_hit(scene.Terrain ? &*scene.Terrain : nullptr, camera_state().eye, ray(x, y));
            if (!ground)
                return;
            dragFloor = *ground;
            flattenHeight = ground->y;
            if(currentMode=="Terrain")terrainStrokeSurface=scene.Terrain.value_or(TerrainData{});
        }
        history.begin(scene);
        begin_group();
        stroke = true;
        if (currentMode == "Sculpt") {
            auto c = actor();
            if (!c)
                throw std::runtime_error("Select an object to sculpt");
            auto k = c->Shape.value_or("");
            if (k == "fire" || k == "light" || k == "lava" || k == "grass" || k == "chaos-lightning" ||
                k == "craxis-smoke")
                throw std::runtime_error("Select a solid shape, rock, or imported mesh to sculpt");
            if (!c->Sculpt)
                c->Sculpt = prepare_sculpt(object_mesh(*c));
        }
        if (currentMode == "Route" && actor())
            actor()->Route.clear();
        brush_at(x, y, message == WM_RBUTTONDOWN);
    }

    void brush_at(int x, int y, bool reverse) {
        Vec3 p;
        if (currentMode != "Sculpt") {
            auto hit = terrain_hit(currentMode=="Terrain" && terrainStrokeSurface ? &*terrainStrokeSurface : scene.Terrain ? &*scene.Terrain : nullptr, camera_state().eye, ray(x, y));
            if (!hit)
                return;
            p = *hit;
        }
        if (currentMode == "Terrain") {
            if (!scene.Terrain)
                scene.Terrain.emplace();
            std::string tool = currentBrush == "pull"   ? "raise"
                               : currentBrush == "push" ? "lower"
                                                        : currentBrush;
            int paint = paintMaterial;
            if (reverse && tool == "raise")
                tool = "lower";
            if(tool=="carve" || tool=="build"){
                double stampRadius=std::clamp(radius,.4,5.);
                if(terrainVolumeStamp && (p-*terrainVolumeStamp).length()<stampRadius*.35)return;
                auto terrain=*scene.Terrain;
                terrain.Volumes.push_back({std::clamp(p.x,-12.,12.),std::clamp(p.y,-20.,20.),std::clamp(p.z,-12.,12.),stampRadius,(tool=="carve")!=reverse});
                terrain.validate();scene.Terrain=std::move(terrain);terrainVolumeStamp=p;
            }else {
            double appliedStrength=(tool=="hill" || tool=="crater")?strength*.07:strength;
            scene.Terrain->brush(p.x, p.z, std::clamp(radius, .4, 5.), appliedStrength, tool, paint, flattenHeight,
                                 .035);
            }
        } else if (auto c = actor()) {
            if (currentMode == "Move") {
                auto before = dragOriginals.at(c->Id);
                double nextX = before.X + p.x - dragFloor.x, nextZ = before.Z + p.z - dragFloor.z;
                double dx = nextX - c->X, dz = nextZ - c->Z;
                c->X = nextX;
                c->Z = nextZ;
                for (auto &q : c->Route) {
                    q.X += dx;
                    q.Z += dz;
                }
                if (c->KeepAboveFloor)
                    c->Y = scene.Terrain ? scene.Terrain->height_at(c->X, c->Z) : 0;
            } else if (currentMode == "Route") {
                if (c->Route.empty() || std::hypot(c->Route.back().X - p.x, c->Route.back().Z - p.z) > .15)
                    c->Route.push_back({p.x, p.z});
            } else if (currentMode == "Sculpt") {
                auto hit = mesh_hit(mesh_from(*c->Sculpt), *c, camera_state().eye, ray(x, y));
                if (hit)
                    sculpt_brush(*c, hit->point, hit->normal, radius, strength, currentBrush, reverse, .035);
            }
        }
        if (auto c = actor())
            move_group(scene, *c, dragOriginals);
        dirty = true;
        install_scene(scene);drop_preparation();
    }

    void pointer_move(int x, int y, WPARAM flags) {
        if(playing && adventure){
            if(adventureLook && (flags&(MK_RBUTTON|MK_LBUTTON))){
                if(flags&MK_RBUTTON)adventure->look(x-last.x,y-last.y);
                else adventure->orbit_camera(x-last.x,y-last.y);
                last={x,y};
            }
            return;
        }
        if(explorer && explorerLook){
            if(flags&MK_RBUTTON){explorer->look(x-last.x,y-last.y);last={x,y};camera();}
            return;
        }

        if (boxSelecting) {
            boxEnd = {x, y};
            return;
        }
        if (orbit) {
            yaw -= (x - last.x) * .006 * tiltSpeed;
            pitch = std::clamp(pitch + (y - last.y) * .006 * tiltSpeed, -1.4, 1.5);
            camera();
        } else if (stroke && dragAxis >= 0 && (flags & MK_LBUTTON)) {
            drag_handle(x, y);
        } else if (stroke && currentMode != "Sculpt" && currentMode != "Terrain" &&
                   (flags & (MK_LBUTTON | MK_RBUTTON)))
            brush_at(x, y, (flags & MK_RBUTTON) != 0);
        last = {x, y};
    }

    void begin_group() {
        dragOriginals.clear();
        if (!actor())
            return;
        if (!selectionIds.contains(actor()->Id)) {
            selectionIds.clear();
            selectionIds.insert(actor()->Id);
        }
        for (const auto &c : scene.Characters)
            if (selectionIds.contains(c.Id))
                dragOriginals[c.Id] = c;
    }
    void finish_box() {
        boxSelecting = false;
        selectionIds.clear();
        selected = -1;
        double left = std::min(boxStart.x, boxEnd.x), right = std::max(boxStart.x, boxEnd.x),
               top = std::min(boxStart.y, boxEnd.y), bottom = std::max(boxStart.y, boxEnd.y);
        if (right - left > 3 && bottom - top > 3)
            for (size_t i = 0; i < scene.Characters.size(); i++) {
                auto b = object_bounds(scene.Characters[i]);
                double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
                for (double x : {b.low.x, b.high.x})
                    for (double y : {b.low.y, b.high.y})
                        for (double z : {b.low.z, b.high.z})
                            if (auto p = project_point({x, y, z})) {
                                minX = std::min(minX, (*p)[0]);
                                minY = std::min(minY, (*p)[1]);
                                maxX = std::max(maxX, (*p)[0]);
                                maxY = std::max(maxY, (*p)[1]);
                            }
                if (minX <= right && maxX >= left && minY <= bottom && maxY >= top) {
                    selectionIds.insert(scene.Characters[i].Id);
                    if (selected < 0)
                        selected = int(i);
                }
            }
        refresh();
    }
    void end_stroke() {
        adventureLook=false;
        explorerLook=false;
        if (boxSelecting) {
            finish_box();
            return;
        }
        orbit = false;
        const int finishedAxis=dragAxis;
        dragAxis = -1;
        if (stroke) {
            stroke = false;
            terrainStrokeSurface.reset();
            if (auto c = actor()) {
                if(rotationSnap && currentMode=="Rotate" && finishedAxis>=0){
                    double& angle=finishedAxis==0 ? c->RotationX : finishedAxis==1 ? c->RotationY : c->RotationZ;
                    angle=snap_increment(angle,rotationStep);
                }
                snap_object(scene, *c, gridSnap, objectSnap, groundSnap,gridStep);
            }
            if (auto c = actor())
                move_group(scene, *c, dragOriginals);
            dragOriginals.clear();
            history.commit();
            refresh();
        }
    }

    void play_song() {
        if (songs.empty())
            return;
        music.open(songs[songIndex]);
        music.volume(musicVolume);
        music.play();musicPlaying=true;music_controls();
    }

    void tick(bool frameDriven=false) {
        const bool btOn=starlight_diag::events_enabled();const double btT0=btOn?starlight_diag::evnow():0;double btMove=0,btScene=0,btPublish=0,btHud=0;
        if(!frameDriven){
        poll_asset_browser();
        if (music.ended() && !songs.empty()) {
            songIndex = (songIndex + 1) % songs.size();
            play_song();
        }
        if (presetTask.valid() && presetTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                preset = presetTask.get();
                if (presetAction == SavePreset) {
                    auto files =
                        file_dialog(projectWindow && IsWindowVisible(projectWindow)?projectWindow:window, "presets", {{L"Starlight test preset", L"*.startest"}}, true);
                    if (!files.empty())
                        atomic_write(files[0], read_file(preset->path), true);
                } else if (presetAction == LaunchProject)
                    launch_project(engine_path(), *project, preset->path);
                if(projectWindow && IsWindowVisible(projectWindow)){project_form();SetWindowTextW(projectMessage,L"Settings checked successfully.");}else inspector_fields();
            } catch (const std::exception &e) {
                if(projectWindow && IsWindowVisible(projectWindow)){SetWindowTextW(projectMessage,wide(e.what()).c_str());project_buttons();diagnostic("project dialog",e.what());}else error(e.what());
            }
        }
        }
        gCrumb="tick.start";auto now = GetTickCount64();
        auto preciseNow=std::chrono::steady_clock::now();
        double dt=std::clamp(std::chrono::duration<double>(preciseNow-motionTick).count(),0.,.05);motionTick=preciseNow;
        lastTick = now;
        if (stroke && (currentMode == "Sculpt" || currentMode == "Terrain") && now - brushTick >= 35) {
            brushTick = now;
            if (GetForegroundWindow() != window)
                end_stroke();
            else
                brush_at(last.x, last.y, reverseBrush);
        }
        if (!filename.empty() && !playing && !stroke && now - watchTick >= 1000) {
            watchTick = now;
            try {
                std::error_code missing;
                auto modified = fs::last_write_time(filename, missing);
                if (missing && !watchMissing) SetWindowTextW(status, L"The scene file is no longer where it was opened from. Use Save As to keep your work.");
                watchMissing = bool(missing);
                if (!missing && (!watchedTime || *watchedTime != modified)) {
                    watchedTime = modified;
                    auto hash = hash_file(filename);
                    if (diskHash && hash != *diskHash) {
                        if (dirty)
                            SetWindowTextW(status,
                                           L"Scene changed outside the editor. Unsaved edits are preserved.");
                        else {
                            LoadingScreen loading;
                            std::optional<SceneData> next;
                            try {
                                loading.show(window, view);
                                loading.update(0, "Reloading " + utf8(filename.filename().wstring()));
                                next = open_with_context(filename, "scene", [&] { return load_scene(filename); });
                                open_with_context(filename, "scene", [&] { render_scene(*next); });
                            } catch (const std::exception &failure) {
                                loading.close();
                                next.reset();
                                error(failure.what());
                            }
                            if (next) {
                                creationGuide.active=false;questTracker.hide();
                                scene = std::move(*next);
                                diskHash = hash;
                                history.clear();
                                selected = -1;
                                refresh();
                                settle_scene(loading, "Preparing " + utf8(filename.filename().wstring()));
                            }
                        }
                    }
                }
            } catch (const std::exception &e) {
                diagnostic("scene watcher", e.what());
            }
        }

        gCrumb="tick.hud";if(now-hudTick>=33){hudTick=now;
        update_creation_guide();update_carry();
        combatHud.update(playing && adventure?&*adventure:nullptr,view,settingsScale);
        movementHud.update(playing && adventure?&adventure->movement_resources():explorer?&explorer->movement_resources():nullptr,view,settingsScale);
        }
        gCrumb="tick.play";if(playing && adventure){
            if(GetForegroundWindow()==window && GetFocus()==view){
                double right=((GetAsyncKeyState('D')&0x8000)?1.:0)-((GetAsyncKeyState('A')&0x8000)?1.:0);
                double forward=((GetAsyncKeyState('W')&0x8000)?1.:0)-((GetAsyncKeyState('S')&0x8000)?1.:0);
                if(mockClimb || mockPush){right=0;forward=1;}
                if(mockStress){
                    const auto& all=adventure->scene().Characters;
                    auto boss=std::find_if(all.begin(),all.end(),[&](const auto& c){return c.Id==stressTarget;});
                    const double t=adventure->seconds();
                    {
                        const Character* aim=boss!=all.end()?&*boss:nullptr;
                        if(aim){auto delta=Vec3{aim->X,aim->Y,aim->Z}-adventure->position;adventure->yaw=std::atan2(delta.x,-delta.z);}
                        else adventure->yaw=stressHeading;
                        adventure->pitch=0;
                        adventure->cameraYaw=adventure->yaw+stressCam[0]*std::sin(6.283185307*t/stressCam[1]);
                        adventure->cameraPitch=stressCam[2]+stressCam[3]*std::sin(6.283185307*t/stressCam[4]);
                        adventure->cameraDistance=stressCam[5]+stressCam[6]*std::sin(6.283185307*t/stressCam[7]);
                    }
                    {const double amp=(stressExpected>0&&stressBreaksSeen<stressExpected)?stressAmpBreak:stressAmp;right=std::sin(t*.6)*amp;forward=std::sin(t*.45)*amp*.43;
                     // Tether: the scripted player drifts back toward its start so dodges and strafing never walk it off the map.
                     const double ox=adventure->position.x-stressHomeX,oz=adventure->position.z-stressHomeZ,od=std::hypot(ox,oz);
                     if(od>1.5){const double pull=std::min(1.,(od-1.5)*.5);right+=-(ox*std::cos(adventure->yaw)+oz*std::sin(adventure->yaw))/od*pull;forward+=-(ox*std::sin(adventure->yaw)-oz*std::cos(adventure->yaw))/od*pull;right=std::clamp(right,-1.,1.);forward=std::clamp(forward,-1.,1.);}}
                    {
                        auto& st=stressStats;++st.ticks;if(st.simStart<0)st.simStart=t;st.simLast=t;
                        if(st.havePos)st.pathLength+=(adventure->position-st.last).length();st.last=adventure->position;st.havePos=true;
                        st.fragPeak=std::max(st.fragPeak,adventure->fragment_count());if(stressLastBreakSim>=0&&stressBreaksSeen>=stressExpected&&t-stressLastBreakSim>20)st.fragPeakLate=std::max(st.fragPeakLate,adventure->fragment_count());st.dustPeak=std::max(st.dustPeak,adventure->dust_count());
                        const double ph=adventure->health();st.playerMinHealth=std::min(st.playerMinHealth,ph);st.playerMaxHealth=std::max(st.playerMaxHealth,ph);
                        if(adventure->presentation().showTelegraph)++st.telegraphTicks;
                        if(boss!=all.end()){
                            auto actor=adventure->combat_actors().find(stressTarget);const double bh=actor!=adventure->combat_actors().end()?actor->second.health:0;
                            if(st.bossStartHealth==0){st.bossStartHealth=bh;st.bossStartX=boss->X;st.bossStartZ=boss->Z;}
                            st.bossMinHealth=std::min(st.bossMinHealth,bh);st.bossMaxDisp=std::max(st.bossMaxDisp,std::hypot(boss->X-st.bossStartX,boss->Z-st.bossStartZ));
                        }
                        int bc=0;for(const auto& id:stressIds)if(std::none_of(all.begin(),all.end(),[&](const auto& c){return c.Id==id;}))++bc;
                        if(bc!=stressBreaksSeen){stressBreaksSeen=bc;stressLastBreakSim=t;stressLastBreakWall=GetTickCount64();starlight_diag::evlog("stress.brk",double(bc),t,double(adventure->fragment_count()));}
                        if(!stressRoute.empty()&&(stressExpected==0?t>=stressFireSim+stressPhaseA:(bc>=stressExpected&&stressLastBreakSim>=0))){
                            const double u=t-(stressExpected==0?stressFireSim+stressPhaseA:stressLastBreakSim);size_t i=0;while(i+1<stressRoute.size()&&stressRoute[i+1].t<=u)++i;
                            StressKey k=stressRoute[i];if(i+1<stressRoute.size()){const auto& n=stressRoute[i+1];const double f=std::clamp((u-k.t)/std::max(1e-6,n.t-k.t),0.,1.);const double e=f*f*(3-2*f);k={u,k.yaw+(n.yaw-k.yaw)*e,k.pitch+(n.pitch-k.pitch)*e,k.dist+(n.dist-k.dist)*e};}
                            adventure->cameraYaw=adventure->yaw+k.yaw;adventure->cameraPitch=k.pitch;adventure->cameraDistance=std::min(12.,k.dist);
                            if(int(i)!=stressSeg){stressSeg=int(i);starlight_diag::evlog("stress.cam",double(i),k.yaw,u);}
                        }
                        if(!(++stressFrame%20)){
                            starlight_diag::evlog("stress.s",double(adventure->fragment_count()),double(adventure->break_count()),t);
                            starlight_diag::evlog("stress.p",adventure->position.x,adventure->position.z,ph);
                            if(boss!=all.end()){auto actor=adventure->combat_actors().find(stressTarget);starlight_diag::evlog("stress.b",boss->X,boss->Z,actor!=adventure->combat_actors().end()?actor->second.health:0);}
                            starlight_diag::evlog("stress.d",double(adventure->dust_count()),double(all.size()),0);
                            {const auto bd=adventure->boss_debug(stressTarget);if(bd.found){starlight_diag::evlog("stress.f",bd.rotationY,bd.facingErrorDeg,bd.distance);starlight_diag::evlog("stress.n",double(bd.pathSize),double(bd.astarCalls),bd.astarMs);starlight_diag::evlog("stress.t",double(bd.turnApplied),double(bd.turnBlocked),bd.winding?bd.windup:-1.);
                                {static uint64_t lastPursuit=0;if(bd.pursuitBlocked!=lastPursuit&&!bd.pursuitBlocker.empty()){lastPursuit=bd.pursuitBlocked;{std::string blockerName,blockerPose;for(const auto& o:scene.Characters)if(o.Id==bd.pursuitBlocker){blockerName=o.Name;blockerPose=std::to_string(o.X)+","+std::to_string(o.Y)+","+std::to_string(o.Z);}std::fprintf(stderr,"boss movement blocked by %s [%s at %s] (count %llu)%c",bd.pursuitBlocker.c_str(),blockerName.c_str(),blockerPose.c_str(),(unsigned long long)bd.pursuitBlocked,10);}}}
                                if(bd.turnBlocked!=stressLastBlocked&&!bd.turnBlocker.empty()){stressLastBlocked=bd.turnBlocked;std::fprintf(stderr,"boss turn blocked by %s (blocked=%llu applied=%llu)%c",bd.turnBlocker.c_str(),(unsigned long long)bd.turnBlocked,(unsigned long long)bd.turnApplied,10);}}}
                        }
                    }
                }
                if(mockBattle && !adventure->scene().Characters.empty()){
                    const auto& enemy=adventure->scene().Characters[0];auto delta=Vec3{enemy.X,enemy.Y,enemy.Z}-adventure->position;
                    adventure->yaw=std::atan2(delta.x,-delta.z);adventure->pitch=0;adventure->cameraYaw=adventure->yaw;adventure->cameraPitch=-.75;adventure->cameraDistance=9;
                    right=std::sin(adventure->seconds()*.6)*.7;forward=std::sin(adventure->seconds()*.45)*.3;
                }
                auto revision=adventure->rules().revision;
                adventure->posture_button((GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,right,forward,dt);adventure->mobility_button((GetAsyncKeyState('V')&0x8000)!=0,right,forward,dt);adventure->sprint((GetAsyncKeyState(VK_SHIFT)&0x8000)!=0);
                gCrumb="tick.move";const double btM0=btOn?starlight_diag::evnow():0;adventure->move(right,forward,dt);{static MotionTrace gameTrace;gameTrace.sample(adventure->scene(),"mot.game");}const double btM1=btOn?starlight_diag::evnow():0;btMove=btM1-btM0;if(btOn)starlight_diag::evlog("bt3",adventure->lastTiming.simulationMs,adventure->lastTiming.combatAiMs,adventure->lastTiming.physicsMs);
                gCrumb="tick.moved";renderer->gameplay_time(adventure->seconds());
                gCrumb="tick.scene";if(adventure->rules().revision!=revision)renderer->scene(adventure->visible_scene(),true);
                btScene=btOn?starlight_diag::evnow()-btM1:0;
                if(!adventure->rules().message.empty())adventureHint=adventure->rules().message;
                questTracker.update(scene,adventure->rules(),playing);
            }
            gCrumb="tick.camera";renderer->camera(camera_state());
            edgeFeedback.hide();
        } else if(explorer){
            if(explorerNeedsRefresh && !stroke && GetFocus()==view){explorer->rebuild_for_authoring(scene);explorerNeedsRefresh=false;}
            if(GetForegroundWindow()==window && GetFocus()==view && !stroke){
                double right=((GetAsyncKeyState('D')&0x8000)?1.:0)-((GetAsyncKeyState('A')&0x8000)?1.:0);
                double forward=((GetAsyncKeyState('W')&0x8000)?1.:0)-((GetAsyncKeyState('S')&0x8000)?1.:0);
                explorer->posture_button((GetAsyncKeyState(VK_CONTROL)&0x8000)!=0,right,forward,dt);explorer->mobility_button((GetAsyncKeyState('V')&0x8000)!=0,right,forward,dt);explorer->sprint((GetAsyncKeyState(VK_SHIFT)&0x8000)!=0);
                explorer->move(right,forward,dt);
            }
            camera();edgeFeedback.hide();
        } else if (GetForegroundWindow() == window && !stroke && !orbit && GetFocus() == view) {
            double x = ((arrowPan && (GetAsyncKeyState(VK_RIGHT) & 0x8000)) ? 1. : 0) -
                       ((arrowPan && (GetAsyncKeyState(VK_LEFT) & 0x8000)) ? 1. : 0),
                   z = ((arrowPan && (GetAsyncKeyState(VK_UP) & 0x8000)) ? 1. : 0) -
                       ((arrowPan && (GetAsyncKeyState(VK_DOWN) & 0x8000)) ? 1. : 0);
            if (x == 0 && z == 0) {
                POINT cursor;
                GetCursorPos(&cursor);
                auto hit = WindowFromPoint(cursor);
                if (hit == view || edgeFeedback.owns(hit)) {
                    ScreenToClient(view, &cursor);
                    RECT r;
                    GetClientRect(view, &r);
                    x = cursor.x < 14 ? -1 : cursor.x > r.right - 14 ? 1 : 0;
                    z = cursor.y < 14 ? 1 : cursor.y > r.bottom - 14 ? -1 : 0;
                }
            }
            double step = distance * .6 * panSpeed * dt / std::max(1., std::hypot(x, z));
            focusX += step * (x * std::cos(yaw) - z * std::sin(yaw));
            focusZ += step * (-x * std::sin(yaw) - z * std::cos(yaw));
            edgeFeedback.update(window, view, x, z);
            camera();
        } else edgeFeedback.hide();
        if(pacingShown){auto mover=playing && adventure?&*adventure:explorer?&*explorer:nullptr;
            if(mover){auto p=mover->presentation();renderer->pacing_player(p.feet,{p.feet.x,p.feet.y,p.feet.z,p.facing,p.bodyHeight});}
            else renderer->pacing_player({},{});
        }
        auto publishPresentation=[&](AdventurePlayer& mover){gameplayPresentationActive=true;const double pq0=starlight_diag::evnow();renderer->player_body(mover.body_bounds());renderer->collision_objects(mover.environment_objects());mover.liquids(renderer->liquid_snapshot());const double pq1=starlight_diag::evnow();renderer->runtime_objects(mover.dynamic_objects());renderer->pacing_debris(mover.debris_poses());const double pq2=starlight_diag::evnow();double started=timing_seconds();auto p=mover.presentation();auto timing=mover.lastTiming;const double pq3=starlight_diag::evnow();{std::vector<std::shared_ptr<const MeshPart>> preloadParts;if(mover.take_prefractured_parts(preloadParts))renderer->rubble_preload(!scene.RaytracingEnabled?std::move(preloadParts):std::vector<std::shared_ptr<const MeshPart>>{});}const double pq4=starlight_diag::evnow();renderer->rubble_instances(resident_rubble(p,!scene.RaytracingEnabled));const double pq5=starlight_diag::evnow();renderer->gameplay_overlays(battle_presentation(p,timing.battleWorkers,!scene.RaytracingEnabled,!scene.RaytracingEnabled));const double pq6=starlight_diag::evnow();starlight_diag::evlog("pub1",pq1-pq0,pq2-pq1,pq3-pq2);starlight_diag::evlog("pub2",pq4-pq3,pq5-pq4,pq6-pq5);timing.presentationMs=(timing_seconds()-started)*1000;renderer->runtime_timing(timing,int(p.actorTelegraphs.size())+int(p.showTelegraph));};
        gCrumb="tick.publish";const double btP0=btOn?starlight_diag::evnow():0;
        if(playing && adventure)publishPresentation(*adventure);
        else if(explorer)publishPresentation(*explorer);
        else if(gameplayPresentationActive){renderer->gameplay_overlays({});renderer->rubble_instances({});renderer->rubble_preload({});renderer->player_body({});renderer->collision_objects({});renderer->runtime_objects({});renderer->pacing_debris({});gameplayPresentationActive=false;}
        {auto mover=playing && adventure?&*adventure:explorer?&*explorer:nullptr;
         auto presentation=mover?mover->presentation():AdventurePlayer::Presentation{};
         if(!mover&&playing){auto status=renderer->status();auto viewState=camera_state();presentation.audioActive=true;presentation.epoch=(uint64_t(1)<<63)|status.environment_epoch;presentation.poseTime=status.seconds;presentation.eye=viewState.eye;presentation.viewAim=(viewState.target-viewState.eye).unit();presentation.viewUp=viewState.up;presentation.lightning=status.environment_lightning;presentation.lightningSounds=std::move(status.environment_sounds);}
         const double au0=starlight_diag::evnow();gameplayAudio.update(mover||playing?&presentation:nullptr,GetForegroundWindow()==window && GetFocus()==view);starlight_diag::evlog("pub3",starlight_diag::evnow()-au0,0,0);}
        const double btP1=btOn?starlight_diag::evnow():0;btPublish=btP1-btP0;
        gCrumb="tick.frameupdates";renderer->frame_updates((playing && adventure) || explorer?window:nullptr);gCrumb="tick.after";
        if (renderer) {
            gCrumb="tick.hud2";const double btH0=btOn?starlight_diag::evnow():0;pacingHud.update(pacingShown,*renderer,view,camera_state(),settingsScale);btHud=btOn?starlight_diag::evnow()-btH0:0;
            if(btOn){starlight_diag::evlog("bt1",btMove,btScene,btPublish);starlight_diag::evlog("bt2",btHud,starlight_diag::evnow()-btT0,0);}
            // The transparent full-viewport timing overlay must remain below
            // the persistent status control, including in fullscreen mode.
            if(pacingShown)SetWindowPos(status,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            gCrumb="tick.status";auto s = renderer->status();gCrumb="tick.status2";
            if ((!gamePreview || !gamePreview->visible()) && playing && !adventure && s.frames>inlinePlayFrame+1 && !s.playing && s.seconds > 0) {
                playing = false;inlineFinished=true;
                SetWindowTextW(playButton, L"▶");
                SetWindowTextW(playState, L"Ready");
            }
            // Playback completion must not wait for the human-readable FPS label.
            // Sample that label separately without delaying input or rendering.
            if (telemetryTick && now - telemetryTick < 500) return;
            std::ostringstream out;
            if (!s.error.empty())
                out << s.error;
            else
                out << "Native engine · "
                    << (telemetryTick && now > telemetryTick
                            ? (s.frames - telemetryFrames) * 1000 / (now - telemetryTick)
                            : 0)
                    << " FPS · GPU " << std::fixed << std::setprecision(2) << s.gpu_ms
                    << " ms · native resolution";
            telemetryFrames = s.frames;
            telemetryTick = now;
            SetWindowTextW(status, wide(out.str()).c_str());gCrumb="tick.end";
        }
    }

    #include "classic_ui_checks.inl"
    #include "bar_scene_builder.inl"
    #include "visible_ui_checks.inl"

    LRESULT message(UINT m, WPARAM w, LPARAM l) {
        gMsg=m;
        if(renderer && ((m>=WM_KEYFIRST && m<=WM_KEYLAST)||(m>=WM_MOUSEFIRST && m<=WM_MOUSELAST)))renderer->input_event();
        switch (m) {
        case WM_HSCROLL:
            if (rebuildingInspector) return 0;
            if(l && GetDlgCtrlID(reinterpret_cast<HWND>(l))==InterfaceSlider) {
                manualInterfacePercent=int(SendMessage(reinterpret_cast<HWND>(l),TBM_GETPOS,0,0));
                SetWindowTextW(GetDlgItem(menuPanel,InterfaceReadout),(std::to_wstring(manualInterfacePercent)+L"%").c_str());
                // Keep the slider stationary under the pointer until release.
                if(LOWORD(w)!=TB_THUMBTRACK) {
                    interfacePercent=manualInterfacePercent;
                    apply_interface_scale(requested_interface_scale(GetDpiForWindow(window)));save_interface_size();
                }
                return 0;
            }
            for (auto key : {"pan", "tilt"}) {
                auto it = mainInputs.find(key);
                if (it != mainInputs.end() && it->second == reinterpret_cast<HWND>(l)) {
                    double value = SendMessage(it->second, TBM_GETPOS, 0, 0) / 100.;
                    if (std::string_view(key) == "pan") panSpeed = value; else tiltSpeed = value;
                    return 0;
                }
            }
            if(reinterpret_cast<HWND>(l)==musicSlider && musicSlider){musicVolume=SendMessage(musicSlider,TBM_GETPOS,0,0)/100.;music.volume(musicVolume);return 0;}
            for (const auto& [key, scale] : sliders) {
                if (inputs.at(key) != reinterpret_cast<HWND>(l)) continue;
                const double value = number(key);
                if ((key == "speed" || key == "size") && actor()) {
                    if ((key == "speed" ? actor()->Speed : actor()->Size) != value)
                        edit([&] {
                            if (key == "speed") actor()->Speed = value;
                            else actor()->Size = value;
                        }, false);
                }
                const std::string exact = key == "speed" ? "exactSpeed" : key == "size" ? "exactSize" : "";
                if (!exact.empty() && inputs.contains(exact)) SetWindowTextW(inputs.at(exact), wide(std::to_string(value)).c_str());
                if (key == "speed" && speedReadout) {
                    std::wostringstream caption;
                    caption << std::fixed << std::setprecision(1) << value << L" metres / second";
                    SetWindowTextW(speedReadout, caption.str().c_str());
                }
                if (key == "pan") panSpeed = value;
                if (key == "tilt") tiltSpeed = value;
                if (key == "radius") radius = value;
                if (key == "strength") strength = value;
            }
            if (reinterpret_cast<HWND>(l) == radiusSlider)
                radius = SendMessage(radiusSlider, TBM_GETPOS, 0, 0) / 100.;
            if (reinterpret_cast<HWND>(l) == strengthSlider)
                strength = SendMessage(strengthSlider, TBM_GETPOS, 0, 0) / 100.;
            return 0;
        case WM_PAINT:
            paint_chrome();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_DRAWITEM: {
            auto &draw = *reinterpret_cast<DRAWITEMSTRUCT *>(l);
            if (draw.CtlType == ODT_LISTBOX && draw.hwndItem == list) {
                auto brush = CreateSolidBrush(draw.itemState & ODS_SELECTED ? RGB(65, 69, 78) : RGB(18, 21, 34));
                FillRect(draw.hDC, &draw.rcItem, brush); DeleteObject(brush);
                if (draw.itemID != UINT(-1) && draw.itemID < scene.Characters.size()) {
                    RECT label = draw.rcItem; label.left += 12; label.right -= 12;
                    style::text(draw.hDC, font, wide(scene.Characters[draw.itemID].Name), label, style::ink,
                                DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
                if (draw.itemState & ODS_FOCUS) DrawFocusRect(draw.hDC, &draw.rcItem);
                return TRUE;
            }
            if (draw.CtlType == ODT_BUTTON)
                style::button(draw, reinterpret_cast<HFONT>(SendMessage(draw.hwndItem, WM_GETFONT, 0, 0)));
            else if (draw.CtlType == ODT_COMBOBOX)
                style::combo(draw, reinterpret_cast<HFONT>(SendMessage(draw.hwndItem,WM_GETFONT,0,0)));
            return TRUE;
        }
        case WM_MEASUREITEM:
            reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight =
                settings_px(reinterpret_cast<MEASUREITEMSTRUCT *>(l)->CtlType == ODT_LISTBOX ? 42 : 28);
            return TRUE;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor(reinterpret_cast<HDC>(w), style::ink);
            SetBkColor(reinterpret_cast<HDC>(w), style::field);
            return reinterpret_cast<LRESULT>(style::fieldBrush());
        case WM_CREATE:
            // Controls are constructed here, before CreateWindowW returns.
            // Root styling must be available when their themes are installed.
            SetPropW(window, L"sl.root", reinterpret_cast<HANDLE>(1));
            initialize();
            return 0;
        case WM_SIZE:
            layout();
            return 0;
        case WM_DPICHANGED: {
            if(inspector)apply_interface_scale(requested_interface_scale(HIWORD(w)));
            auto bounds = reinterpret_cast<RECT *>(l);
            SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                         bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {MulDiv(720,GetDpiForWindow(window),96),MulDiv(540,GetDpiForWindow(window),96)};
            return 0;
        case NativeSession::frameUpdateMessage:
            if(loadingActive){renderer->frame_update_handled();return 0;}
            renderer->frame_update_handled();tick(true);
            return 0;
        case WM_TIMER:
            if(loadingActive)return 0;
            tick();
            return 0;
        case WM_COMMAND:
            if (LOWORD(w) == HelpSearch && HIWORD(w) == EN_CHANGE) { filter_help(); return 0; }
            if (LOWORD(w) == SearchAssets && HIWORD(w) == EN_CHANGE) { filter_asset_browser(); return 0; }
            if (!rebuildingInspector && HIWORD(w) == EN_KILLFOCUS &&
                int(SendMessage(tab, CB_GETCURSEL, 0, 0)) == 0 && actor() &&
                inputs.contains("name") && inputs.at("name") == reinterpret_cast<HWND>(l)) {
                auto name = text(inputs.at("name"));
                auto first = name.find_first_not_of(" \t\r\n");
                if (first != std::string::npos) {
                    name = name.substr(first, name.find_last_not_of(" \t\r\n") - first + 1);
                    if (name != actor()->Name) {
                        edit([&] { actor()->Name = name; }, false);
                        SetWindowTextW(inputs.at("name"), wide(name).c_str());
                        SendMessage(list, LB_RESETCONTENT, 0, 0);
                        for (const auto& c : scene.Characters)
                            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(c.Name).c_str()));
                        for (size_t i = 0; i < scene.Characters.size(); ++i)
                            SendMessage(list, LB_SETSEL, selectionIds.contains(scene.Characters[i].Id) || int(i) == selected, i);
                    }
                }
                return 0;
            }
            if (HIWORD(w) == BN_CLICKED || HIWORD(w) == CBN_SELCHANGE || HIWORD(w) == LBN_SELCHANGE)
                command(LOWORD(w));
            return 0;
        case WM_CTLCOLORSTATIC:
            SetTextColor(reinterpret_cast<HDC>(w), RGB(225, 232, 246));
            SetBkColor(reinterpret_cast<HDC>(w), style::panel);
            return reinterpret_cast<LRESULT>(style::panelBrush());
        case WM_CLOSE:
            if (loadingActive) {
                preparation_cancel() = true;
                auto &waiting = preparation_deferred_close();
                if (std::find(waiting.begin(), waiting.end(), window) == waiting.end()) waiting.push_back(window);
                return 0;
            }
            if (!discard())
                return 0;
            KillTimer(window, 1);
            gamePreview.reset();
            if(projectWindow){DestroyWindow(projectWindow);projectWindow=nullptr;}
            renderer.reset();
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(window, m, w, l);
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 0;
    bool automatedCheck = false;
    fs::path automatedReport;
    try {
        starlight_diagnostics::initialize(L"NativeEditor");
        (void)mesh_detail();   // logs the effective STARLIGHT_MESH_DETAIL (event log tag cfg.meshdet) at startup
        int commandArgc = 0;
        auto commandArgv = CommandLineToArgvW(GetCommandLineW(), &commandArgc);
        bool commands = false;
        for (int i = 1; i < commandArgc; i++) {
            std::wstring_view a = commandArgv[i];
            if (a == L"--ui-check" || a == L"--ui-walkthrough" || a == L"--interface-preference-check") {
                automatedCheck = true;
                if (i + 1 < commandArgc) automatedReport = commandArgv[i + 1];
            }
            commands |= a == L"--commands" || a == L"--inspect" || a == L"--describe" || a == L"--feature" || a == L"--export-portable" || a == L"--verify-portable";
        }
        if (commands) {
            int code = run_commands(commandArgc, commandArgv);
            LocalFree(commandArgv);
            CoUninitialize();
            return code;
        }
        BootSplash splash(!automatedCheck);
        Editor editor(instance);
        editor.deferStartupActivation=splash.active();
        for(int i=1;i+1<commandArgc;++i)if(std::wstring_view(commandArgv[i])==L"--interface-preference-check") {
            const fs::path requestPath=commandArgv[i+1];
            auto request=json::parse(read_file(requestPath));
            editor.interfacePreferenceOverride=requestPath.parent_path()/L"interface-size.json";
            editor.questTracker.preferenceOverride=requestPath.parent_path()/L"quest-tracker.json";
            editor.create();
            const int expected=request.at("expectedPercent"), expectedManual=request.at("expectedManual");
            if(editor.interfacePercent!=expected || editor.manualInterfacePercent!=expectedManual)
                throw std::runtime_error("Interface preference did not survive editor relaunch");
            editor.toggle_menu(MenuFile);
            auto slider=GetDlgItem(editor.menuPanel,Editor::InterfaceSlider);
            SendMessage(slider,TBM_SETPOS,TRUE,request.at("saveManual").get<int>());
            SendMessage(editor.window,WM_HSCROLL,TB_ENDTRACK,reinterpret_cast<LPARAM>(slider));
            SendMessage(GetDlgItem(editor.menuPanel,Editor::SettingsScale),BM_SETCHECK,
                request.at("saveAuto").get<bool>()?BST_CHECKED:BST_UNCHECKED,0);
            editor.command(Editor::SettingsScale);
            if(request.contains("questExpected") && editor.questTracker.preferences()!=request.at("questExpected"))throw std::runtime_error("Quest tracker preferences did not survive relaunch");
            if(request.value("questSetup",false)){
                editor.toggle_menu(0);editor.scene=quest_template();editor.refresh();editor.playback_command(Play);
                auto click=[&](const char* id){UpdateWindow(editor.questTracker.handle());auto r=editor.questTracker.action_bounds(id);if(!r)throw std::runtime_error("Quest preference control missing");SendMessage(editor.questTracker.handle(),WM_LBUTTONUP,0,MAKELPARAM((r->left+r->right)/2,(r->top+r->bottom)/2));};
                click("__settings");for(int j=0;j<3;++j)click("__opacity_less");click("__lock");
                SendMessage(editor.questTracker.handle(),WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(80,18));SendMessage(editor.questTracker.handle(),WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(-120,98));SendMessage(editor.questTracker.handle(),WM_LBUTTONUP,0,MAKELPARAM(80,18));click("__lock");
                if(editor.questTracker.opacity()!=40 || !editor.questTracker.locked() || !editor.questTracker.preferences().at("customPosition").get<bool>())throw std::runtime_error("Quest preference setup failed");
            }
            atomic_write(requestPath.wstring()+L".result.json",json{{"pass",true},{"loadedPercent",expected},{"loadedManual",expectedManual},{"savedPercent",editor.interfacePercent},{"savedManual",editor.manualInterfacePercent},{"questPreferences",editor.questTracker.preferences()}}.dump(2));
            editor.dirty=false;SendMessage(editor.window,WM_CLOSE,0,0);
            LocalFree(commandArgv);CoUninitialize();return 0;
        }
        LocalFree(commandArgv);
        editor.create();
        editor.await_startup(splash, automatedCheck);
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::wstring_view(argv[i]) == L"--ui-walkthrough") {
                const fs::path output = argv[i + 1]; LocalFree(argv);
                const int code = editor.visible_ui_checks(output); CoUninitialize(); return code;
            }
            if (std::wstring_view(argv[i]) == L"--ui-check") {
                const fs::path output = argv[i + 1]; LocalFree(argv);
                const int code = editor.ui_checks(output); CoUninitialize(); return code;
            }
        }
        for (int i = 1; i < argc; i++)
            if (std::wstring_view(argv[i]) == L"--show-terrain") {
                editor.command(BuildTerrain);
            } else if (std::wstring_view(argv[i]) == L"--show-gameplay") {
                editor.show_inspector(6);
            } else if (std::wstring_view(argv[i]) == L"--play") {
                editor.playback_command(Play);
            } else if (std::wstring_view(argv[i]) == L"--walk-build") {
                editor.command(InteractiveBuild);
            } else if (std::wstring_view(argv[i]) == L"--scene" && i + 1 < argc) {
                auto path = fs::absolute(argv[++i]);
                bool repaired = false;
                LoadingScreen loading;
                try {
                    std::string loadedHash;
                    auto next = editor.open_scene_data(path, repaired, &loading, &loadedHash);
                    if (!next) continue;
                    open_with_context(path, "scene", [&] { render_scene(*next); });
                    editor.scene = std::move(*next);
                    editor.filename = path;
                    editor.diskHash = loadedHash;
                    editor.dirty = repaired;
                    editor.refresh();
                    editor.settle_scene(loading, "Preparing " + utf8(path.filename().wstring()));
                } catch (const std::exception &failure) {
                    loading.close();
                    editor.error(failure.what());
                }
            }
        LocalFree(argv);
        ACCEL keys[] = {{FCONTROL | FVIRTKEY, 'N', New},       {FCONTROL | FVIRTKEY, 'O', Open},
                        {FCONTROL | FVIRTKEY, 'S', Save},      {FCONTROL | FSHIFT | FVIRTKEY, 'S', SaveAs},
                        {FCONTROL | FVIRTKEY, 'Z', Undo},      {FCONTROL | FVIRTKEY, 'Y', Redo},
                        {FCONTROL | FVIRTKEY, 'D', Duplicate}, {FVIRTKEY, VK_F8, TogglePacing}, {FVIRTKEY, VK_F11, FullScreen}, {FVIRTKEY, VK_F1, HelpOpen}};
        auto accelerators = CreateAcceleratorTableW(keys, int(std::size(keys)));
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0) > 0) {
            const HWND root=GetAncestor(msg.hwnd,GA_ROOT);
            if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE && (root==editor.window || root==editor.projectWindow)){editor.escape();continue;}
            if(editor.projectWindow && IsWindowVisible(editor.projectWindow) && IsDialogMessageW(editor.projectWindow,&msg)){editor.reveal_focus();continue;}
            if(editor.gamePreview && root==editor.gamePreview->window()) {
                if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE){editor.gamePreview->escape();continue;}
                if(msg.message==WM_KEYDOWN && msg.wParam==VK_TAB && IsDialogMessageW(root,&msg))continue;
                TranslateMessage(&msg);DispatchMessage(&msg);continue;
            }
            if(msg.message==WM_KEYDOWN && msg.wParam==VK_TAB && IsDialogMessageW(editor.window,&msg)){editor.reveal_focus();continue;}
            // Text undo belongs to the focused field, not the authored scene.
            wchar_t focusClass[32]{};GetClassNameW(GetFocus(),focusClass,32);
            const bool textUndo=msg.message==WM_KEYDOWN && (GetKeyState(VK_CONTROL)&0x8000) &&
                (msg.wParam=='Z' || msg.wParam=='Y') && std::wstring_view(focusClass)==L"Edit";
            if (textUndo || !TranslateAcceleratorW(editor.window, accelerators, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        DestroyAcceleratorTable(accelerators);
    } catch (const std::exception &e) {
        diagnostic("native editor startup", e.what());
        if (automatedCheck) {
            if (!automatedReport.empty()) {
                try { atomic_write(automatedReport, json{{"pass",false},{"error",e.what()},{"interrupted",true}}.dump(2)); }
                catch (...) {}
            }
        } else MessageBoxW(nullptr, wide(e.what()).c_str(), L"Starlight startup error", MB_OK | MB_ICONERROR);
        result = 1;
    }
    CoUninitialize();
    return result;
}


