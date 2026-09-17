#include "../../deep/engine/crash_diagnostics.h"

#include "commands.hpp"

#include "interactions.hpp"
#include "mesh.hpp"
#include "music.hpp"

#include "native_session.hpp"

#include "projects.hpp"

#include <commctrl.h>

#include <functional>
#include <iomanip>

#include <future>

#include <map>
#include <unordered_set>

#include <numbers>

#include <sstream>

#include "editor_style.hpp"
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
    ClearPours, DockSettings
};

struct Editor {
    ULONG_PTR graphicsToken = 0;
    std::unique_ptr<Gdiplus::Image> logo;
    HWND tips = nullptr, dock = nullptr, playbackPanel = nullptr, propertiesPanel = nullptr,
         assetPanel = nullptr, menuPanel = nullptr;
    std::vector<HWND> menuControls, dockControls;
    HWND playButton = nullptr, playState = nullptr, radiusSlider = nullptr, strengthSlider = nullptr;
    int dockSection = BuildTerrain, dockWidth = 642;
    uint64_t telemetryFrames = 0;
    ULONGLONG telemetryTick = 0;
    bool inspectorShown = false, assetsShown = false, dockShown = false;
    int openMenu = 0;
    bool advanced = false, buildDetails = false, bodyDetails = false, arrowPan = false;
    int paintMaterial = 0, formRows = 0;
    std::optional<bool> pendingPour;
    fs::path characterAsset;
    HWND characterCard = nullptr, inspectorTitle = nullptr;
    std::unordered_set<std::string> choices;
    std::unordered_map<std::string, double> sliders;
    LocalMusic music;
    std::vector<fs::path> songs;
    size_t songIndex = 0;
    double musicVolume = .5, flattenHeight = 0;
    double terrainRadius = 2, terrainStrength = 1, sculptRadius = .45, sculptStrength = .65;
    bool fullScreen = false, gridSnap = false, objectSnap = false, groundSnap = false;
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
    HFONT font = nullptr, smallFont = nullptr, brandFont = nullptr;
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
    int selected = -1;
    bool dirty = false, playing = false, repeat = false, follow = false, stroke = false, orbit = false;
    POINT last{};
    int width = 1380, height = 900, scroll = 0;
    double yaw = 0, pitch = std::atan(.72), distance = 13, focusX = 0, focusY = 0, focusZ = 0, panSpeed = 1,
           tiltSpeed = 1, radius = 2, strength = 1;
    std::string currentMode = "Select", currentBrush = "pull";
    ULONGLONG lastTick = GetTickCount64(), brushTick = 0, watchTick = 0;
    bool reverseBrush = false;
    std::optional<fs::file_time_type> watchedTime;

    explicit Editor(HINSTANCE h) : instance(h) {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&graphicsToken, &input, nullptr) != Gdiplus::Ok)
            throw std::runtime_error("Cannot initialize native interface drawing");
    }

    ~Editor() {
        renderer.reset();
        if (font)
            DeleteObject(font);
        if (smallFont)
            DeleteObject(smallFont);
        if (brandFont)
            DeleteObject(brandFont);
        if (appIcon)
            DestroyIcon(appIcon);
        DeleteObject(background);
        logo.reset();
        Gdiplus::GdiplusShutdown(graphicsToken);
    }

    static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto self = reinterpret_cast<Editor *>(GetWindowLongPtr(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            self = static_cast<Editor *>(reinterpret_cast<CREATESTRUCT *>(l)->lpCreateParams);
            self->window = h;
            SetWindowLongPtr(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        try {
            if (self)
                return self->message(m, w, l);
        } catch (const std::exception &e) {
            if (self)
                self->error(e.what());
        }
        return DefWindowProc(h, m, w, l);
    }

    static LRESULT CALLBACK viewport_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Editor *>(data);
        try {
            if (m == WM_MOUSEWHEEL) {
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
                if (w == VK_ESCAPE) {
                    self->pendingPour.reset();
                    self->history.cancel(self->scene);
                    self->stroke = self->orbit = false;
                    self->refresh();
                    ReleaseCapture();
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
            self->error(e.what());
        }
        return DefSubclassProc(h, m, w, l);
    }

    static LRESULT CALLBACK inspector_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Editor *>(data);
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
            int next = self->scroll;
            if (m == WM_MOUSEWHEEL)
                next -= GET_WHEEL_DELTA_WPARAM(w) / 120 * 60;
            else {
                SCROLLINFO si{sizeof(si), SIF_TRACKPOS};
                GetScrollInfo(h, SB_VERT, &si);
                switch (LOWORD(w)) {
                case SB_LINEUP:
                    next -= 30;
                    break;
                case SB_LINEDOWN:
                    next += 30;
                    break;
                case SB_PAGEUP:
                    next -= 300;
                    break;
                case SB_PAGEDOWN:
                    next += 300;
                    break;
                case SB_THUMBTRACK:
                    next = si.nTrackPos;
                    break;
                }
            }
            SCROLLINFO range{sizeof(range), SIF_RANGE | SIF_PAGE};
            GetScrollInfo(h, SB_VERT, &range);
            int maximum = std::max(0, range.nMax - int(range.nPage) + 1);
            next = std::clamp(next, 0, maximum);
            ScrollWindowEx(h, 0, self->scroll - next, nullptr, nullptr, nullptr, nullptr,
                           SW_SCROLLCHILDREN | SW_INVALIDATE);
            self->scroll = next;
            SetScrollPos(h, SB_VERT, next, TRUE);
            return 0;
        }
        return DefSubclassProc(h, m, w, l);
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

    HWND control(HWND parent, const wchar_t *cls, const wchar_t *text, int id, int x, int y, int w, int h,
                 DWORD style = 0) {
        bool checkbox = std::wstring_view(cls) == L"BUTTON" && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX;
        if (std::wstring_view(cls) == L"COMBOBOX")
            style |= CBS_OWNERDRAWFIXED | CBS_HASSTRINGS;
        auto child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                                     reinterpret_cast<HMENU>(INT_PTR(id)), instance, nullptr);
        if (!child)
            throw std::runtime_error("Cannot create editor control");
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
            tip = L"Play preview";
            break;
        case Pause:
            tip = L"Pause preview";
            break;
        case Stop:
            tip = L"Reset preview";
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
            TOOLINFOW tool{sizeof(tool)};
            tool.cbSize = TTTOOLINFOW_V2_SIZE;
            tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tool.hwnd = window;
            tool.uId = reinterpret_cast<UINT_PTR>(child);
            tool.lpszText = const_cast<wchar_t *>(tip);
            SendMessage(tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
        }
        if (std::wstring_view(cls) == L"COMBOBOX") {
            SendMessage(child, CB_SETITEMHEIGHT, -1, 26);
            SendMessage(child, CB_SETITEMHEIGHT, 0, 28);
        }
        return child;
    }

    void create() {
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
        InitCommonControlsEx(&common);
        font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        smallFont =
            CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        brandFont =
            CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
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
        ShowWindow(window, SW_SHOW);
        UpdateWindow(window);
    }

    static LRESULT CALLBACK panel_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto self = reinterpret_cast<Editor *>(data);
        if (m == WM_SIZE) InvalidateRect(h, nullptr, FALSE);
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
            FillRect(dc, &r, style::panelBrush());
            Gdiplus::Graphics g(dc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            style::round(g, {.5f, .5f, float(r.right - 1), float(r.bottom - 1)},
                         h == self->dock ? 29.f : 12.f, Gdiplus::Color(255, 41, 51, 77),
                         Gdiplus::Color(255, 22, 20, 37), Gdiplus::Color(255, 100, 120, 156));
            EndPaint(h, &ps);
            return 0;
        }
        return DefSubclassProc(h, m, w, l);
    }
    HWND panel(int x, int y, int w, int h) {
        auto result = control(window, L"STATIC", L"", 0, x, y, w, h, WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        SetWindowSubclass(result, panel_proc, 3, reinterpret_cast<DWORD_PTR>(this));
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
            if (id >= 2100 && id < 2110)
                active = currentBrush == std::array<std::string, 10>{"pull",   "push",   "smooth", "flatten",
                                                                     "raise",  "lower",  "paint",  "hill",
                                                                     "crater", "plateau"}[id - 2100];
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
        radiusSlider = strengthSlider = nullptr;
        dockSection = section;
        int x = 14;
        auto button = [&](int id, const wchar_t *label, int icon = -1, int w = 40) {
            auto h = control(dock, L"BUTTON", label, id, x, 11, w, 38);
            if (icon >= 0)
                SetPropW(h, L"sl.icon", reinterpret_cast<HANDLE>(INT_PTR(icon + 1)));
            TOOLINFOW tip{sizeof(tip)};
            tip.cbSize = TTTOOLINFOW_V2_SIZE;
            tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
            tip.hwnd = window;
            tip.uId = reinterpret_cast<UINT_PTR>(h);
            tip.lpszText = const_cast<wchar_t *>(label);
            SendMessage(tips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip));
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
        button(Undo, L"↶");
        button(Redo, L"↷");
        if (section == BuildShapes || section == BuildTerrain)
            button(DockSettings, L"⚙");
        button(CloseDock, L"×");
        dockWidth = x + 10;
        dock_selection();
    }
    void toggle_menu(int id) {
        if (openMenu == id)
            id = 0;
        for (auto h : menuControls)
            destroy_control(h);
        menuControls.clear();
        openMenu = id;
        auto row = [&](int command, const wchar_t *label) {
            menuControls.push_back(control(menuPanel, L"BUTTON", label, command, 24,
                                           52 + (int(menuControls.size()) - 1) * 44, 195, 36));
        };
        if (id)
            menuControls.push_back(control(menuPanel, L"STATIC",
                                           id == MenuBuilder ? L"BUILD"
                                           : id == MenuFile  ? L"STARLIGHT"
                                           : id == MenuWorld ? L"ENVIRONMENT"
                                                             : L"MORE",
                                           0, 24, 18, 195, 24));
        if (id == MenuFile) {
            row(FullScreen, L"Full screen");
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
            row(MusicOpen, L"Choose music…");
            row(MusicPlay, L"Play music");
            row(MusicPause, L"Pause music");
            row(MusicNext, L"Next track");
            row(MusicStop, L"Stop music");
            row(MusicQuieter, L"Volume down");
            row(MusicLouder, L"Volume up");
            row(FullScreen, L"Full screen");
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
                           L"crater", L"plateau"})
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
        playState = control(playbackPanel, L"STATIC", L"Ready", 0, 76, 12, 48, 36);
        control(playbackPanel, L"BUTTON", L"↺", Stop, 126, 10, 42, 40);
        playButton = control(playbackPanel, L"BUTTON", L"▶", Play, 176, 10, 42, 40);
        SetPropW(playButton, L"sl.accent", reinterpret_cast<HANDLE>(1));
        characterAsset = root / L"projects/GameProject/build/models/fox-lead.glb";
        if (!fs::exists(characterAsset)) characterAsset.clear();
        assetPanel = panel(24, 76, 260, 500);
        control(assetPanel, L"STATIC", L"CHARACTERS", 0, 16, 15, 220, 22);
        characterCard = control(assetPanel, L"STATIC", characterAsset.empty() ? L"Choose a character" : L"★  Fox", 0, 16, 47, 228, 32);
        control(assetPanel, L"BUTTON", L"Place character", PlaceCharacter, 14, 84, 232, 34);
        control(assetPanel, L"BUTTON", L"Import character…", Import, 14, 124, 232, 34);
        control(assetPanel, L"BUTTON", L"Character settings", ObjectProperties, 14, 164, 232, 34);
        control(assetPanel, L"BUTTON", L"Advanced", Advanced, 14, 204, 142, 30, BS_AUTOCHECKBOX);
        control(assetPanel, L"BUTTON", L"Follow", Follow, 166, 204, 78, 30, BS_AUTOCHECKBOX);
        control(assetPanel, L"BUTTON", L"Move character", 2200, 14, 240, 232, 34);
        control(assetPanel, L"BUTTON", L"Draw route", 2300, 14, 280, 232, 34);
        list = control(assetPanel, L"LISTBOX", L"", ObjectList, 14, 324, 232, 160,
                       LBS_NOTIFY | LBS_EXTENDEDSEL | WS_VSCROLL);
        propertiesPanel = panel(1000, 76, 318, 650);
        tab = control(propertiesPanel, L"COMBOBOX", L"", InspectorTab, 12, 14, 254, 260,
                      CBS_DROPDOWNLIST | WS_VSCROLL);
        for (auto label : {L"Object settings", L"World & lighting", L"Camera settings", L"Projects & presets", L"Terrain settings", L"Shape settings"})
            SendMessage(tab, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessage(tab, CB_SETCURSEL, 0, 0);
        ShowWindow(tab, SW_HIDE);
        inspectorTitle = control(propertiesPanel, L"STATIC", L"Character settings", 0, 16, 18, 250, 26);
        control(propertiesPanel, L"BUTTON", L"×", CloseInspector, 274, 12, 32, 30);
        inspector =
            control(propertiesPanel, L"STATIC", L"", 0, 8, 54, 302, 550, WS_VSCROLL | WS_CLIPCHILDREN);
        SetWindowSubclass(inspector, inspector_proc, 1, reinterpret_cast<DWORD_PTR>(this));
        menuPanel = panel(18, 58, 262, 360);
        status = control(window, L"STATIC", L"Ready • Native performance first", 0, 14, 840, 1100, 20);
        renderer = std::make_unique<NativeSession>(view, 1200, 750);
        SetTimer(window, 1, 16, nullptr);
        refresh();
        layout();
    }
    void layout() {
        RECT r;
        GetClientRect(window, &r);
        width = r.right;
        height = r.bottom;
        if (!view)
            return;
        int vw = std::max(1, fullScreen ? width : width - 28),
            vh = std::max(1, fullScreen ? height : height - 104);
        MoveWindow(view, fullScreen ? 0 : 14, fullScreen ? 0 : 62, vw, vh, TRUE);
        renderer->size(vw, vh);
        for (size_t i = 1; i < top.size(); i++)
            MoveWindow(top[i], width - 184 + int(i - 1) * 44, 18, 36, 30, TRUE);
        int dockX = std::max(18, (width - dockWidth) / 2);
        int dockBottom = dockX + dockWidth > width - 292 ? height - 192 : height - 120;
        MoveWindow(dock, dockX, std::max(110, dockBottom), dockWidth, 60, TRUE);
        SetWindowRgn(dock, CreateRoundRectRgn(0, 0, dockWidth + 1, 61, 60, 60), TRUE);
        MoveWindow(playbackPanel, width - 264, height - 120, 232, 60, TRUE);
        SetWindowRgn(playbackPanel, CreateRoundRectRgn(0, 0, 233, 61, 24, 24), TRUE);
        int page = int(SendMessage(tab, CB_GETCURSEL, 0, 0));
        int ph = std::min(std::max(250, height - 190), formRows * 35 + 90);
        const bool dockSettings = page == 4 || page == 5;
        const int px = dockSettings ? std::clamp(dockX + dockWidth - 318, 18, width - 336) : width - 342;
        const int py = dockSettings ? std::max(66, dockBottom - ph - 10) : 76;
        MoveWindow(propertiesPanel, px, py, 318, ph, TRUE);
        MoveWindow(inspector, 8, 54, 302, std::max(100, ph - 64), TRUE);
        SCROLLINFO si{sizeof(si), SIF_PAGE};
        si.nPage = std::max(100, ph - 64);
        SetScrollInfo(inspector, SB_VERT, &si, TRUE);
        MoveWindow(assetPanel, width - 408, 58, 260, std::min(550, height - 145), TRUE);
        MoveWindow(list, 14, 324, 232, std::max(90, std::min(210, height - 485)), TRUE);
        MoveWindow(menuPanel, openMenu == MenuFile ? 18 : width - 267, 58, 245,
                   74 + std::max(0, int(menuControls.size()) - 1) * 44, TRUE);
        MoveWindow(status, 14, height - 25, width - 28, 20, TRUE);
        ShowWindow(status, fullScreen ? SW_HIDE : SW_SHOW);
        ShowWindow(dock, dockShown ? SW_SHOW : SW_HIDE);
        ShowWindow(propertiesPanel, inspectorShown ? SW_SHOW : SW_HIDE);
        ShowWindow(assetPanel, assetsShown ? SW_SHOW : SW_HIDE);
        ShowWindow(menuPanel, openMenu ? SW_SHOW : SW_HIDE);
        for (auto h : top)
            SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        for (auto h : {dock, playbackPanel, assetPanel, propertiesPanel, menuPanel})
            SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
                g.DrawImage(logo.get(), 66, 17, 32, 32);
        }
        if (!fullScreen) {
        style::text(dc, brandFont, L"STARLIGHT", {108, 15, 200, 50}, style::ink,
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        style::text(dc, font, wide(scene.Name), {200, 15, width - 200, 50}, RGB(175, 191, 219),
                    DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
        EndPaint(window, &ps);
    }

    void error(std::string_view text) {
        diagnostic("native editor", text);
        SetWindowTextW(status, wide(text).c_str());
        MessageBoxW(window, wide(text).c_str(), L"Starlight", MB_OK | MB_ICONERROR);
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

    void field(std::string key, std::string label, std::string value, bool checkbox = false) {
        int y = 8 + formRows++ * 35;
        fields.push_back(control(inspector, L"STATIC", wide(label).c_str(), 0, 8, y + 4, 118, 24));
        auto edit =
            control(inspector, checkbox ? L"BUTTON" : L"EDIT", checkbox ? L"" : wide(value).c_str(), 0, 153,
                    y, 112, 26, checkbox ? BS_AUTOCHECKBOX : WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP);
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
        auto h = control(inspector, L"COMBOBOX", L"", 0, 128, r.top, 150, 330, CBS_DROPDOWNLIST | WS_VSCROLL);
        for (auto name : names) SendMessageW(h, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        SendMessage(h, CB_SETCURSEL, selectedValue, 0);
        inputs[key] = h; choices.insert(key); fields.push_back(h);
    }
    void slider(std::string key, std::string label, double value, double low, double high) {
        field(key, label, "");
        auto old = inputs.at(key);
        RECT r; GetWindowRect(old, &r); MapWindowPoints(nullptr, inspector, reinterpret_cast<POINT*>(&r), 2);
        destroy_control(old); fields.pop_back();
        auto h = control(inspector, TRACKBAR_CLASSW, L"", 3000 + int(sliders.size()), 145, r.top, 132, 28, TBS_NOTICKS | WS_TABSTOP);
        SendMessage(h, TBM_SETRANGE, TRUE, MAKELPARAM(int(low * 100), int(high * 100)));
        SendMessage(h, TBM_SETPOS, TRUE, int(value * 100));
        inputs[key] = h; sliders[key] = 100; fields.push_back(h);
    }
    void form_button(int id, const wchar_t* label) {
        fields.push_back(control(inspector, L"BUTTON", label, id, 8, 8 + formRows++ * 35, 270, 29));
    }
    void inspector_fields() {
        for (auto h : fields) destroy_control(h);
        fields.clear(); inputs.clear(); choices.clear(); sliders.clear();
        scroll = 0; formRows = 0;
        int page = int(SendMessage(tab, CB_GETCURSEL, 0, 0));
        const wchar_t* titles[] = {L"Character settings", L"Atmosphere & ray tracing", L"Camera settings",
                                  L"Projects & presets", L"Terrain settings", L"Shape settings"};
        SetWindowTextW(inspectorTitle, titles[page]);
        if (page == 0) {
            if (auto c = actor()) {
                field("name", "Name", c->Name);
                slider("speed", "Walk speed", c->Speed, .2, 5);
                slider("size", "Size", c->Size, .3, 3);
                form_button(Advanced, advanced ? L"− Advanced settings" : L"+ Advanced settings");
                if (advanced) {
                    field("x", "Position X", c->X); field("z", "Position Z", c->Z);
                    field("exactSpeed", "Exact speed", c->Speed); field("exactSize", "Exact size", c->Size);
                    form_button(BuildDetails, buildDetails ? L"− Build settings" : L"+ Build settings");
                    if (buildDetails) {
                        field("y", "Height", c->Y); field("width", "Width", c->Width);
                        field("height", "Height scale", c->Height); field("depth", "Depth", c->Depth);
                        field("rx", "Rotate X", c->RotationX); field("ry", "Rotate Y", c->RotationY);
                        field("rz", "Rotate Z", c->RotationZ);
                        field("color", "Color", c->Color);
                        choice("surface", "Surface", c->Surface, {L"Plain", L"Grass", L"Earth", L"Sand", L"Stone", L"Snow", L"Rock", L"Water", L"Lava", L"Lightning", L"Chaos rock", L"Light-bound"});
                        field("floor", "Keep above floor", c->KeepAboveFloor ? "1" : "0", true);
                    }
                    form_button(BodyDetails, bodyDetails ? L"− Body settings" : L"+ Body settings");
                    if (bodyDetails) {
                        field("dynamic", "Simulate physics", c->Dynamic ? "1" : "0", true);
                        field("density", "Density", c->Density);
                    }
                }
                auto kind = c->Shape.value_or("");
                if (kind == "chaos-lightning") {
                    field("tx", "Target X", c->TargetX); field("ty", "Target Y", c->TargetY);
                    field("tz", "Target Z", c->TargetZ); field("spread", "Branch spread", c->BranchSpread);
                }
                if (kind == "light" || kind == "fire" || kind == "chaos-lightning" || kind == "chaos-rock" || kind == "light-shard") {
                    field("intensity", "Brightness", c->LightIntensity); field("range", "Light reach", c->LightRange);
                    if (kind == "light") field("cone", "Light cone", c->LightCone);
                }
                form_button(ClearRoute, (L"Clear route (" + std::to_wstring(c->Route.size()) + L" points)").c_str());
                form_button(Delete, L"Remove character");
            } else form_button(MenuAssets, L"Choose a character first");
        } else if (page == 1) {
            field("name", "Scene name", scene.Name);
            slider("hour", "Time of day", scene.Hour, 0, 24);
            field("cycle", "Day/night cycle", scene.DayCycle ? "1" : "0", true);
            field("daylength", "Day duration (s)", scene.DayLength);
            field("fog", "Fog density", scene.FogDensity);
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
        } else if (page == 5) {
            field("gridSnap", "Grid snap (0.5 m)", gridSnap ? "1" : "0", true);
            field("groundSnap", "Snap to ground", groundSnap ? "1" : "0", true);
            field("objectSnap", "Snap near objects", objectSnap ? "1" : "0", true);
            field("ground", "Floor color", scene.GroundColor);
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

        if (!inputs.empty() && page != 3) form_button(Apply, L"Apply");
        SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMax = formRows * 35 + 16;
        RECT bounds;
        GetClientRect(inspector, &bounds);
        si.nPage = std::max(1L, bounds.bottom);
        SetScrollInfo(inspector, SB_VERT, &si, TRUE);
        layout();
    }

    void refresh() {
        SendMessage(list, LB_RESETCONTENT, 0, 0);
        for (const auto &c : scene.Characters)
            SendMessage(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wide(c.Name).c_str()));
        for (size_t i = 0; i < scene.Characters.size(); i++)
            SendMessage(list, LB_SETSEL, selectionIds.contains(scene.Characters[i].Id) || int(i) == selected,
                        i);
        inspector_fields();
        renderer->scene(scene);
        renderer->tools(actor() ? actor()->Id : std::string{}, currentMode);
        camera();
        SetWindowTextW(window, (std::wstring(L"Starlight Engine Editor") + (dirty ? L" *" : L"")).c_str());
    }

    template <class F> void edit(F f) {
        history.begin(scene);
        try {
            f();
            scene.validate();
            render_scene(scene); // Validate assets and renderer capacity before committing the edit.
            history.commit();
            dirty = true;
            refresh();
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
    bool save(bool choose) {
        auto target = filename;
        if (choose || target.empty()) {
            auto files = file_dialog(window, "scenes", {{L"Starlight scene", L"*.starscene"}}, true);
            if (files.empty()) return false;
            target = files[0];
        }
        save_scene(scene, target, target == filename ? diskHash : std::nullopt);
        filename = target; diskHash = hash_file(target); dirty = false; refresh();
        return true;
    }
    void apply() {
        int page = int(SendMessage(tab, CB_GETCURSEL, 0, 0));
        if (page == 2) {
            panSpeed = number("pan"); tiltSpeed = number("tilt"); arrowPan = checked("arrows"); return;
        }
        if (page == 4) { radius = number("radius"); strength = number("strength"); paintMaterial = integer("paint"); }
        if (page == 5) { gridSnap = checked("gridSnap"); groundSnap = checked("groundSnap"); objectSnap = checked("objectSnap"); }
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
                b("floor", c->KeepAboveFloor); b("dynamic", c->Dynamic); n("density", c->Density);
                n("intensity", c->LightIntensity); n("range", c->LightRange); n("cone", c->LightCone);
                n("tx", c->TargetX); n("ty", c->TargetY); n("tz", c->TargetZ); n("spread", c->BranchSpread);
            } else if (page == 1) {
                scene.Name = text(inputs.at("name"));
                n("hour", scene.Hour); b("cycle", scene.DayCycle); n("daylength", scene.DayLength); n("fog", scene.FogDensity);
                b("rt", scene.RaytracingEnabled); scene.RaytracingQuality = integer("tier");
                scene.PathBounces = integer("bounces"); b("rr", scene.RayReconstructionEnabled); b("freeze", scene.FreezeRaytracingAnimation);
            } else if (page == 4) {
                if (!scene.Terrain) scene.Terrain.emplace();
                b("water", scene.Terrain->WaterEnabled); n("level", scene.Terrain->WaterLevel); b("lava", scene.Terrain->Lava);
            } else if (page == 5) scene.GroundColor = text(inputs.at("ground"));
        });
    }

    void command(int id) {
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
        if (id == PlaceCharacter) {
            if (playing) throw std::runtime_error("Pause playback before editing");
            if (characterAsset.empty()) throw std::runtime_error("Import a GLB character first");
            edit([&] {
                Character c; c.Asset = utf8(characterAsset.wstring()); c.Name = utf8(characterAsset.stem().wstring());
                c.X = std::clamp(focusX, -12., 12.); c.Z = std::clamp(focusZ, -12., 12.);
                c.Y = scene.Terrain ? scene.Terrain->height_at(c.X, c.Z) : 0;
                scene.Characters.push_back(c); selected = int(scene.Characters.size()) - 1;
            }); return;
        }
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
        if (id >= 2100 && id < 2110) {
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
            music.play();
            return;
        }
        if (id == MusicPause) {
            music.pause();
            return;
        }
        if (id == MusicStop) {
            music.stop();
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
            music.volume(musicVolume);
            return;
        }
        if (id == FullScreen) {
            if (!fullScreen) {
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
            fullScreen = !fullScreen;
            layout();
            return;
        }
        if (id >= ViewAbove && id <= ViewFocus) {
            if (auto c = actor()) {
                auto b = object_bounds(*c);
                focusX = (b.low.x + b.high.x) / 2;
                focusY = (b.low.y + b.high.y) / 2;
                focusZ = (b.low.z + b.high.z) / 2;
                if (id == ViewFocus)
                    distance = std::clamp((b.high - b.low).length() * 3, 3., 30.);
            }
            pitch = id == ViewAbove ? 1.48 : id == ViewSide ? 0 : id == ViewLow ? -.06 : .35;
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
            show_inspector(3);
            return;
        }
        if (id >= ChooseProject && id <= LaunchProject) {
            if (presetTask.valid())
                throw std::runtime_error("Wait for the current settings check");
            if (id == ChooseProject) {
                auto files = file_dialog(window, "projects", {{L"Starlight project", L"*.star"}});
                if (files.empty())
                    return;
                auto next = ProjectData::load(files[0]);
                project = std::move(next);
                preset.reset();
                inspector_fields();
                return;
            }
            if (!project)
                throw std::runtime_error("Choose a project first");
            project->selected = text(inputs.at("scene"));
            if (id == ChoosePreset) {
                auto files = file_dialog(window, "presets", {{L"Starlight test preset", L"*.startest"}});
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
            return;
        }
        if (id == Play || id == Pause || id == Stop) {
            playing = id == Play ? !playing : false;
            SetWindowTextW(playButton, playing ? L"Ⅱ" : L"▶");
            SetWindowTextW(playState, playing ? L"Playing" : id == Stop ? L"Ready" : L"Paused");
            renderer->playback(playing, id == Stop);
            return;
        }
        if (playing)
            throw std::runtime_error("Pause playback before editing");
        if (id == New) {
            if (!discard())
                return;
            scene = SceneData{};
            history.clear();
            filename.clear();
            diskHash.reset();
            selected = -1;
            dirty = false;
            refresh();
        } else if (id == Open) {
            if (!discard())
                return;
            auto files = file_dialog(window, "scenes", {{L"Starlight scene", L"*.starscene"}});
            if (files.empty())
                return;
            auto next = SceneData::decode(read_file(files[0]));
            render_scene(next);
            scene = std::move(next);
            filename = files[0];
            diskHash = hash_file(filename);
            history.clear();
            selected = -1;
            dirty = false;
            refresh();
        } else if (id == Import) {
            auto files = file_dialog(window, "models", {{L"GLB model", L"*.glb"}});
            if (files.empty())
                return;
            load_glb(files[0]);
            characterAsset = files[0];
            SetWindowTextW(characterCard, wide("★  " + utf8(characterAsset.stem().wstring())).c_str());
        } else if (id == Add) {
            edit([&] {
                Character c;
                c.Shape = text(shape);
                c.Name = *c.Shape + " " + std::to_string(scene.Characters.size() + 1);
                c.X = std::clamp(focusX, -12., 12.);
                c.Z = std::clamp(focusZ, -12., 12.);
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
            bool changed = id == Undo ? history.undo(scene) : history.redo(scene);
            if (changed) {
                selected = -1;
                dirty = true;
                refresh();
            }
        } else if (id == Delete && actor()) {
            edit([&] {
                scene.Characters.erase(scene.Characters.begin() + selected);
                selected = -1;
            });
        } else if (id == Duplicate && actor()) {
            edit([&] {
                auto copy = *actor();
                copy.Id = new_id();
                copy.X += .5;
                copy.Name += " copy";
                scene.Characters.push_back(copy);
                selected = int(scene.Characters.size()) - 1;
            });
        } else if (id == Frame) {
            focusX = focusY = focusZ = 0;
            distance = 22;
            camera();
        }
    }

    CameraState camera_state() {
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
        renderer->scene(scene);
    }

    int pick(int x, int y) {
        int result = -1;
        double nearest = INFINITY;
        for (size_t i = 0; i < scene.Characters.size(); i++) {
            const auto &c = scene.Characters[i];
            auto hit = mesh_hit(object_mesh(c), c, camera_state().eye, ray(x, y));
            if (hit && hit->distance < nearest) {
                nearest = hit->distance;
                result = int(i);
            }
        }
        return result;
    }

    void pointer_down(int x, int y, UINT message) {
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
            auto hit = terrain_hit(scene.Terrain ? &*scene.Terrain : nullptr, camera_state().eye, ray(x, y));
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
            scene.Terrain->brush(p.x, p.z, std::clamp(radius, .4, 5.), strength, tool, paint, flattenHeight,
                                 .035);
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
        renderer->scene(scene);
    }

    void pointer_move(int x, int y, WPARAM flags) {
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
        if (boxSelecting) {
            finish_box();
            return;
        }
        orbit = false;
        dragAxis = -1;
        if (stroke) {
            stroke = false;
            if (auto c = actor())
                snap_object(scene, *c, gridSnap, objectSnap, groundSnap);
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
        music.play();
    }

    void tick() {
        if (music.ended() && !songs.empty()) {
            songIndex = (songIndex + 1) % songs.size();
            play_song();
        }
        if (presetTask.valid() && presetTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                preset = presetTask.get();
                if (presetAction == SavePreset) {
                    auto files =
                        file_dialog(window, "presets", {{L"Starlight test preset", L"*.startest"}}, true);
                    if (!files.empty())
                        atomic_write(files[0], read_file(preset->path), true);
                } else if (presetAction == LaunchProject)
                    launch_project(engine_path(), *project, preset->path);
                inspector_fields();
            } catch (const std::exception &e) {
                error(e.what());
            }
        }
        auto now = GetTickCount64();
        double dt = std::min(.05, (now - lastTick) * .001);
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
                auto modified = fs::last_write_time(filename);
                if (!watchedTime || *watchedTime != modified) {
                    watchedTime = modified;
                    auto hash = hash_file(filename);
                    if (diskHash && hash != *diskHash) {
                        if (dirty)
                            SetWindowTextW(status,
                                           L"Scene changed outside the editor. Unsaved edits are preserved.");
                        else {
                            auto next = SceneData::decode(read_file(filename));
                            render_scene(next);
                            scene = std::move(next);
                            diskHash = hash;
                            history.clear();
                            selected = -1;
                            refresh();
                        }
                    }
                }
            } catch (const std::exception &e) {
                diagnostic("scene watcher", e.what());
            }
        }

        if (GetForegroundWindow() == window && !stroke && !orbit && GetFocus() == view) {
            double x = ((arrowPan && (GetAsyncKeyState(VK_RIGHT) & 0x8000)) ? 1. : 0) -
                       ((arrowPan && (GetAsyncKeyState(VK_LEFT) & 0x8000)) ? 1. : 0),
                   z = ((arrowPan && (GetAsyncKeyState(VK_UP) & 0x8000)) ? 1. : 0) -
                       ((arrowPan && (GetAsyncKeyState(VK_DOWN) & 0x8000)) ? 1. : 0);
            if (x == 0 && z == 0) {
                POINT cursor;
                GetCursorPos(&cursor);
                if (WindowFromPoint(cursor) == view) {
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
            camera();
        }
        if (renderer && now % 500 < 20) {
            auto s = renderer->status();
            if (playing && !s.playing && s.seconds > 0) {
                playing = false;
                SetWindowTextW(playButton, L"▶");
                SetWindowTextW(playState, L"Ready");
            }
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
            SetWindowTextW(status, wide(out.str()).c_str());
        }
    }

    #include "classic_ui_checks.inl"

    LRESULT message(UINT m, WPARAM w, LPARAM l) {
        switch (m) {
        case WM_HSCROLL:
            for (const auto& [key, scale] : sliders) {
                if (inputs.at(key) != reinterpret_cast<HWND>(l)) continue;
                const double value = number(key);
                const std::string exact = key == "speed" ? "exactSpeed" : key == "size" ? "exactSize" : "";
                if (!exact.empty() && inputs.contains(exact)) SetWindowTextW(inputs.at(exact), wide(std::to_string(value)).c_str());
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
            if (draw.CtlType == ODT_BUTTON)
                style::button(draw, reinterpret_cast<HFONT>(SendMessage(draw.hwndItem, WM_GETFONT, 0, 0)));
            else if (draw.CtlType == ODT_COMBOBOX)
                style::combo(draw, font);
            return TRUE;
        }
        case WM_MEASUREITEM:
            reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight = 28;
            return TRUE;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            SetTextColor(reinterpret_cast<HDC>(w), style::ink);
            SetBkColor(reinterpret_cast<HDC>(w), style::field);
            return reinterpret_cast<LRESULT>(style::fieldBrush());
        case WM_CREATE:
            initialize();
            return 0;
        case WM_SIZE:
            layout();
            return 0;
        case WM_DPICHANGED: {
            auto bounds = reinterpret_cast<RECT *>(l);
            SetWindowPos(window, nullptr, bounds->left, bounds->top, bounds->right - bounds->left,
                         bounds->bottom - bounds->top, SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {1050, 720};
            return 0;
        case WM_TIMER:
            tick();
            return 0;
        case WM_COMMAND:
            if (HIWORD(w) == BN_CLICKED || HIWORD(w) == CBN_SELCHANGE || HIWORD(w) == LBN_SELCHANGE)
                command(LOWORD(w));
            return 0;
        case WM_CTLCOLORSTATIC:
            SetTextColor(reinterpret_cast<HDC>(w), RGB(225, 232, 246));
            SetBkColor(reinterpret_cast<HDC>(w), style::panel);
            return reinterpret_cast<LRESULT>(style::panelBrush());
        case WM_CLOSE:
            if (!discard())
                return 0;
            KillTimer(window, 1);
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
    try {
        starlight_diagnostics::initialize(L"NativeEditor");
        int commandArgc = 0;
        auto commandArgv = CommandLineToArgvW(GetCommandLineW(), &commandArgc);
        bool commands = false;
        for (int i = 1; i < commandArgc; i++) {
            std::wstring_view a = commandArgv[i];
            commands |= a == L"--commands" || a == L"--inspect" || a == L"--describe" || a == L"--feature";
        }
        if (commands) {
            int code = run_commands(commandArgc, commandArgv);
            LocalFree(commandArgv);
            CoUninitialize();
            return code;
        }
        LocalFree(commandArgv);
        Editor editor(instance);
        editor.create();
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i + 1 < argc; ++i) {
            if (std::wstring_view(argv[i]) == L"--ui-check") {
                const fs::path output = argv[i + 1]; LocalFree(argv);
                const int code = editor.ui_checks(output); CoUninitialize(); return code;
            }
        }
        for (int i = 1; i < argc; i++)
            if (std::wstring_view(argv[i]) == L"--show-terrain") {
                editor.command(BuildTerrain);
            } else if (std::wstring_view(argv[i]) == L"--scene" && i + 1 < argc) {
                auto path = fs::absolute(argv[++i]);
                auto next = SceneData::decode(read_file(path));
                render_scene(next);
                editor.scene = std::move(next);
                editor.filename = path;
                editor.diskHash = hash_file(path);
                editor.refresh();
            }
        LocalFree(argv);
        ACCEL keys[] = {{FCONTROL | FVIRTKEY, 'N', New},       {FCONTROL | FVIRTKEY, 'O', Open},
                        {FCONTROL | FVIRTKEY, 'S', Save},      {FCONTROL | FSHIFT | FVIRTKEY, 'S', SaveAs},
                        {FCONTROL | FVIRTKEY, 'Z', Undo},      {FCONTROL | FVIRTKEY, 'Y', Redo},
                        {FCONTROL | FVIRTKEY, 'D', Duplicate}, {FVIRTKEY, VK_F11, FullScreen}};
        auto accelerators = CreateAcceleratorTableW(keys, int(std::size(keys)));
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0) > 0) {
            if (!TranslateAcceleratorW(editor.window, accelerators, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        DestroyAcceleratorTable(accelerators);
    } catch (const std::exception &e) {
        diagnostic("native editor startup", e.what());
        MessageBoxW(nullptr, wide(e.what()).c_str(), L"Starlight startup error", MB_OK | MB_ICONERROR);
        result = 1;
    }
    CoUninitialize();
    return result;
}
