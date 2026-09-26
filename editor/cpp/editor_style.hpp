#pragma once
#include "editor_icons.hpp"
#include <dwmapi.h>
#include <gdiplus.h>
#include <uxtheme.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

namespace starlight::editor::style {
// Windows has no public Win32 app-wide dark-mode switch. Resolve the optional
// modern theme exports only on builds with the matching ABI; custom controls
// below keep their own palette if the operating system does not expose them.
inline HMODULE darkThemeModule() {
    static HMODULE module = [] {
        using Version = LONG(WINAPI*)(OSVERSIONINFOW*);
        auto version = reinterpret_cast<Version>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        OSVERSIONINFOW os{sizeof(os)};
        if (!version || version(&os) != 0 || os.dwMajorVersion != 10 || os.dwBuildNumber < 18362)
            return HMODULE(nullptr);
        return LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }();
    return module;
}
inline void enableDarkApplication() {
    if (auto module = darkThemeModule()) {
        using PreferredMode = int(WINAPI*)(int);
        auto preferred = reinterpret_cast<PreferredMode>(GetProcAddress(module, MAKEINTRESOURCEA(135)));
        if (preferred) preferred(2); // ForceDark, process-local; does not change Windows settings.
    }
}
inline void darkWindow(HWND h) {
    if (auto module = darkThemeModule()) {
        using AllowDark = BOOL(WINAPI*)(HWND, BOOL);
        auto allow = reinterpret_cast<AllowDark>(GetProcAddress(module, MAKEINTRESOURCEA(133)));
        if (allow) allow(h, TRUE);
    }
    SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
}
inline constexpr COLORREF ink = RGB(239, 242, 250), muted = RGB(154, 170, 201), field = RGB(12, 16, 26),
                          panel = RGB(26, 31, 49);
inline HBRUSH fieldBrush() {
    static HBRUSH b = CreateSolidBrush(field);
    return b;
}
inline HBRUSH panelBrush() {
    static HBRUSH b = CreateSolidBrush(panel);
    return b;
}
inline void panelStops(Gdiplus::LinearGradientBrush& brush) {
    Gdiplus::Color colors[] = {Gdiplus::Color(255, 37, 44, 69),
                              Gdiplus::Color(255, 19, 25, 42),
                              Gdiplus::Color(255, 28, 24, 48)};
    Gdiplus::REAL stops[] = {0.f, .48f, 1.f};
    brush.SetInterpolationColors(colors, stops, 3);
}
inline void roundedPath(Gdiplus::GraphicsPath& path, Gdiplus::RectF r, float radius) {
    float d = std::min(radius * 2, std::min(r.Width, r.Height));
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
}
inline void round(Gdiplus::Graphics &g, Gdiplus::RectF r, float radius, Gdiplus::Color top,
                  Gdiplus::Color bottom, Gdiplus::Color edge, bool panelGradient = false) {
    Gdiplus::GraphicsPath path;
    roundedPath(path, r, radius);
    Gdiplus::LinearGradientBrush fill(r, top, bottom,
        panelGradient ? float(std::atan2(double(r.Height), double(r.Width)) * 180 / 3.141592653589793) : 90.f);
    if (panelGradient) panelStops(fill);
    g.FillPath(&fill, &path);
    Gdiplus::Pen border(edge, 1.f);
    border.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawPath(&border, &path);
}
// Move every direct child, including controls wholly outside the viewport.
// ScrollWindowEx(SW_SCROLLCHILDREN) does not reliably reposition clipped children.
inline void scrollChildren(HWND host, int dx, int dy) {
    if (!dx && !dy) return;
    for (auto child = GetWindow(host, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        RECT r{}; GetWindowRect(child, &r);
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&r), 2);
        SetWindowPos(child, nullptr, r.left + dx, r.top + dy, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS);
    }
    RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
inline void text(HDC dc, HFONT font, std::wstring value, RECT r, COLORREF color = ink,
                 UINT flags = DT_SINGLELINE | DT_VCENTER | DT_CENTER) {
    auto old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), int(value.size()), &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}
inline void panelSurface(HWND panelWindow, float radius) {
    auto surface = reinterpret_cast<HWND>(GetPropW(panelWindow, L"sl.panelSurface"));
    if (!surface) {
        surface = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            L"STATIC", L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, GetParent(panelWindow), nullptr,
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(panelWindow, GWLP_HINSTANCE)), nullptr);
        if (!surface) return;
        SetPropW(panelWindow, L"sl.panelSurface", surface);
    }
    RECT r; GetWindowRect(panelWindow, &r);
    const int width = r.right - r.left, height = r.bottom - r.top;
    if (width <= 0 || height <= 0) return;
    RECT previous; GetWindowRect(surface, &previous);
    const bool resize = previous.right - previous.left != width || previous.bottom - previous.top != height;
    POINT local{r.left, r.top}; ScreenToClient(GetParent(panelWindow), &local);
    SetWindowPos(surface, panelWindow, local.x, local.y, width, height,
        SWP_NOACTIVATE | (IsWindowVisible(panelWindow) ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    const auto radiusKey = static_cast<INT_PTR>(radius * 256.f);
    if (!resize && GetPropW(surface, L"sl.painted") &&
        reinterpret_cast<INT_PTR>(GetPropW(surface, L"sl.radius")) == radiusKey) return;
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HDC memory = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (memory && dib && pixels) {
        auto old = SelectObject(memory, dib);
        {
            Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(pixels));
            Gdiplus::Graphics g(&bitmap);
            Gdiplus::RectF bounds(.5f, .5f, float(width - 1), float(height - 1));
            Gdiplus::LinearGradientBrush fill(bounds, Gdiplus::Color(255, 37, 44, 69),
                Gdiplus::Color(255, 28, 24, 48),
                float(std::atan2(double(height - 1), double(width - 1)) * 180 / 3.141592653589793));
            panelStops(fill);
            g.FillRectangle(&fill, Gdiplus::Rect(0, 0, width, height));
        }
        // Compute both halves from identical mirrored pixel coordinates. The
        // border is the coverage difference of two concentric rounded shapes,
        // avoiding arc/straight-line joins and asymmetric rasterizer endpoints.
        auto* rgba = static_cast<BYTE*>(pixels);
        const float outerRadius = std::min(radius + .5f, .5f * float(std::min(width, height)));
        constexpr int samples = 8;
        for (int y = 0; y < height; ++y) {
            const int my = std::min(y, height - 1 - y);
            for (int x = 0; x < width; ++x) {
                const int mx = std::min(x, width - 1 - x);
                if ((mx >= outerRadius && my >= 1) || (my >= outerRadius && mx >= 1)) continue;
                int outer = 0, inner = 0;
                for (int sy = 0; sy < samples; ++sy) for (int sx = 0; sx < samples; ++sx) {
                    const float px = float(mx) + (float(sx) + .5f) / samples;
                    const float py = float(my) + (float(sy) + .5f) / samples;
                    const float dx = std::max(0.f, outerRadius - px);
                    const float dy = std::max(0.f, outerRadius - py);
                    const float distanceSquared = dx * dx + dy * dy;
                    outer += distanceSquared <= outerRadius * outerRadius;
                    const float innerRadius = std::max(0.f, outerRadius - 1.f);
                    inner += px >= 1.f && py >= 1.f && distanceSquared <= innerRadius * innerRadius;
                }
                const int border = outer - inner;
                BYTE* pixel = rgba + (size_t(y) * width + x) * 4;
                constexpr int count = samples * samples;
                pixel[0] = BYTE((int(pixel[0]) * inner + 156 * border + count / 2) / count);
                pixel[1] = BYTE((int(pixel[1]) * inner + 120 * border + count / 2) / count);
                pixel[2] = BYTE((int(pixel[2]) * inner + 100 * border + count / 2) / count);
                pixel[3] = BYTE((255 * outer + count / 2) / count);
            }
        }

        POINT source{}; SIZE size{width, height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        // Layout already positioned this child. Painting must never reposition it.
        if (UpdateLayeredWindow(surface, nullptr, nullptr, &size, memory, &source, 0, &blend, ULW_ALPHA)) {
            SetPropW(surface, L"sl.painted", reinterpret_cast<HANDLE>(1));
            SetPropW(surface, L"sl.radius", reinterpret_cast<HANDLE>(radiusKey));
        }
        SelectObject(memory, old);
    }
    if (dib) DeleteObject(dib);
    if (memory) DeleteDC(memory);
}
// Paint child backgrounds in the parent's coordinate space so rounded controls
// and text do not leave opaque rectangular patches across the glass gradient.
inline void childBackground(HWND h, HDC dc) {
    HWND parent = GetParent(h);
    if (GetPropW(parent, L"sl.backgroundProxy")) parent = GetParent(parent);
    RECT r;
    GetClientRect(parent, &r);
    POINT offset{0, 0};
    MapWindowPoints(h, parent, &offset, 1);
    Gdiplus::Graphics g(dc);
    g.TranslateTransform(float(-offset.x), float(-offset.y));
    Gdiplus::Rect bounds(0, 0, std::max(1L, r.right), std::max(1L, r.bottom));
    Gdiplus::LinearGradientBrush bg(bounds, Gdiplus::Color(255, 37, 44, 69), Gdiplus::Color(255, 28, 24, 48),
                                    float(std::atan2(double(r.bottom), double(r.right)) * 180 / 3.141592653589793));
    panelStops(bg);
    if (GetPropW(parent, L"sl.root")) {
        Gdiplus::LinearGradientBrush root(
            bounds, Gdiplus::Color(255, 20, 33, 59), Gdiplus::Color(255, 36, 19, 46),
            float(std::atan2(double(r.bottom), double(r.right)) * 180 / 3.141592653589793));
        Gdiplus::Color colors[] = {Gdiplus::Color(255, 20, 33, 59), Gdiplus::Color(255, 9, 13, 24),
                                   Gdiplus::Color(255, 36, 19, 46)};
        Gdiplus::REAL stops[] = {0, .45f, 1};
        root.SetInterpolationColors(colors, stops, 3);
        g.FillRectangle(&root, bounds);
    } else
        g.FillRectangle(&bg, bounds);
}
inline LRESULT CALLBACK labelProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_ERASEBKGND) return 1; // WM_PAINT covers the complete client area.
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        childBackground(h, dc);
        RECT r;
        GetClientRect(h, &r);
        wchar_t label[1024];
        GetWindowTextW(h, label, 1024);
        text(dc, reinterpret_cast<HFONT>(SendMessage(h, WM_GETFONT, 0, 0)), label, r, GetPropW(h,L"sl.textColor") ? COLORREF(INT_PTR(GetPropW(h,L"sl.textColor"))-1) : muted,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        EndPaint(h, &ps);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
inline void button(DRAWITEMSTRUCT &d, HFONT font) {
    auto h = d.hwndItem;
    RECT r = d.rcItem;
    childBackground(h, d.hDC);
    bool hover = GetPropW(h, L"sl.hover"), check = GetPropW(h, L"sl.check"),
         selected = GetPropW(h, L"sl.checked") || (d.itemState & ODS_SELECTED),
         accent = GetPropW(h, L"sl.accent");
    {
        Gdiplus::Graphics g(d.hDC);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        auto edge = selected ? Gdiplus::Color(255, 231, 118, 233)
                    : hover  ? Gdiplus::Color(255, 118, 201, 255)
                             : Gdiplus::Color(255, 63, 72, 101);
        if (!check)
            round(g, {1.f, 1.f, float(r.right - 2), float(r.bottom - 2)}, 7,
                  accent     ? Gdiplus::Color(255, 121, 70, 173)
                  : selected ? Gdiplus::Color(255, 81, 44, 83)
                             : Gdiplus::Color(255, 37, 43, 67),
                  accent     ? Gdiplus::Color(255, 96, 52, 144)
                  : selected ? Gdiplus::Color(255, 81, 44, 83)
                             : Gdiplus::Color(255, 37, 43, 67),
                  edge);
        if (auto icon = GetPropW(h, L"sl.icon"))
            drawDockIcon(g, int(reinterpret_cast<INT_PTR>(icon)) - 1, float(r.right), float(r.bottom));
        if (check) {
            Gdiplus::SolidBrush b(selected ? Gdiplus::Color(255, 231, 118, 233)
                                           : Gdiplus::Color(255, 100, 113, 146));
            g.FillRectangle(&b, 10, (r.bottom - 10) / 2, 10, 10);
        }
    }
    wchar_t label[512];
    GetWindowTextW(h, label, 512);
    if (check)
        r.left += 19;
    if (!GetPropW(h, L"sl.icon"))
        text(d.hDC, font, label, r, IsWindowEnabled(h) ? ink : muted);
    if (d.itemState & ODS_FOCUS) {
        InflateRect(&r, -4, -4);
        DrawFocusRect(d.hDC, &r);
    }
}
inline void combo(DRAWITEMSTRUCT &d, HFONT font) {
    FillRect(d.hDC, &d.rcItem, fieldBrush());
    std::wstring label;
    if (d.itemID != UINT(-1)) {
        auto n=SendMessageW(d.hwndItem,CB_GETLBTEXTLEN,d.itemID,0);
        if(n>=0){label.resize(size_t(n)+1);SendMessageW(d.hwndItem,CB_GETLBTEXT,d.itemID,reinterpret_cast<LPARAM>(label.data()));label.resize(size_t(n));}
    }
    RECT r = d.rcItem;
    r.left += 8;
    if (d.itemState & ODS_SELECTED) {
        auto brush = CreateSolidBrush(RGB(50, 57, 85));
        FillRect(d.hDC, &d.rcItem, brush);
        DeleteObject(brush);
    }
    text(d.hDC, font, label.c_str(), r, ink, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
}
inline LRESULT CALLBACK buttonProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_SIZE && GetPropW(GetParent(h), L"sl.root")) {
        const auto result = DefSubclassProc(h, m, w, l);
        // A layered window retains its old bitmap when resized. Repaint at the
        // new dimensions so fullscreen transitions cannot crop the lower curve.
        InvalidateRect(h, nullptr, FALSE);
        return result;
    }
    if (m == WM_PAINT && GetPropW(GetParent(h), L"sl.root")) {
        // Root buttons overlap the native viewport. A parent-colored rectangle
        // cannot represent that background: composite premultiplied alpha instead.
        PAINTSTRUCT ps; BeginPaint(h, &ps);
        RECT r; GetClientRect(h, &r);
        const int width = r.right, height = r.bottom;
        if (width > 0 && height > 0) {
            BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
            info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            void* pixels = nullptr;
            HDC memory = CreateCompatibleDC(nullptr);
            HBITMAP dib = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
            if (memory && dib && pixels) {
                auto old = SelectObject(memory, dib);
                {
                    Gdiplus::Bitmap bitmap(width, height, width * 4, PixelFormat32bppPARGB,
                                             static_cast<BYTE*>(pixels));
                    Gdiplus::Graphics g(&bitmap);
                    g.Clear(Gdiplus::Color(0, 0, 0, 0));
                    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                    const bool hover = GetPropW(h, L"sl.hover") != nullptr;
                    const bool focus = GetFocus() == h;
                    round(g, {1.f, 1.f, float(width - 2), float(height - 2)}, 7.f,
                        Gdiplus::Color(255, 37, 43, 67), Gdiplus::Color(255, 37, 43, 67),
                        focus || hover ? Gdiplus::Color(255, 118, 201, 255) : Gdiplus::Color(255, 63, 72, 101));
                    if (auto icon = GetPropW(h, L"sl.icon"))
                        drawDockIcon(g, int(reinterpret_cast<INT_PTR>(icon)) - 1, float(width), float(height));
                    else {
                        wchar_t caption[512]; GetWindowTextW(h, caption, 512);
                        Gdiplus::Font font(memory, reinterpret_cast<HFONT>(SendMessageW(h, WM_GETFONT, 0, 0)));
                        Gdiplus::SolidBrush inkBrush(Gdiplus::Color(255, 239, 242, 250));
                        Gdiplus::StringFormat format;
                        format.SetAlignment(Gdiplus::StringAlignmentCenter);
                        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
                        g.DrawString(caption, -1, &font, Gdiplus::RectF(0, 0, float(width), float(height)), &format, &inkBrush);
                    }
                }
                POINT source{}; SIZE size{width, height};
                BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
                // Hover/focus repaints update pixels only. Passing screen-space
                // child coordinates here causes cumulative windowed-mode drift.
                UpdateLayeredWindow(h, nullptr, nullptr, &size, memory, &source, 0, &blend, ULW_ALPHA);
                SelectObject(memory, old);
            }
            if (dib) DeleteObject(dib);
            if (memory) DeleteDC(memory);
        }
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_ERASEBKGND) return 1; // Owner drawing includes the background.
    if (m == WM_PAINT && GetPropW(h, L"sl.check")) {
        SetPropW(h, L"sl.checked",
                 reinterpret_cast<HANDLE>(INT_PTR(DefSubclassProc(h, BM_GETCHECK, 0, 0) == BST_CHECKED)));
        PAINTSTRUCT ps;
        DRAWITEMSTRUCT d{};
        d.hDC = BeginPaint(h, &ps);
        d.hwndItem = h;
        GetClientRect(h, &d.rcItem);
        d.itemState = GetFocus() == h ? ODS_FOCUS : 0;
        button(d, reinterpret_cast<HFONT>(SendMessage(h, WM_GETFONT, 0, 0)));
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_MOUSEMOVE && !GetPropW(h, L"sl.hover")) {
        SetPropW(h, L"sl.hover", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, h, 0};
        TrackMouseEvent(&track);
        InvalidateRect(h, nullptr, FALSE);
    }
    if (m == WM_MOUSELEAVE) {
        RemovePropW(h, L"sl.hover");
        InvalidateRect(h, nullptr, FALSE);
    }
    return DefSubclassProc(h, m, w, l);
}
inline LRESULT CALLBACK comboProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    auto result = DefSubclassProc(h, m, w, l);
    if (m == WM_PAINT) {
        HDC dc = GetDC(h);
        RECT r;
        GetClientRect(h, &r);
        auto border = CreateSolidBrush(RGB(63, 72, 101));
        FrameRect(dc, &r, border);
        DeleteObject(border);
        RECT arrow{r.right - 19, 1, r.right - 1, r.bottom - 1};
        FillRect(dc, &arrow, panelBrush());
        auto pen = CreatePen(PS_SOLID, 1, ink);
        auto old = SelectObject(dc, pen);
        MoveToEx(dc, r.right - 14, r.bottom / 2 - 2, nullptr);
        LineTo(dc, r.right - 10, r.bottom / 2 + 2);
        LineTo(dc, r.right - 6, r.bottom / 2 - 2);
        SelectObject(dc, old);
        DeleteObject(pen);
        ReleaseDC(h, dc);
    }
    return result;
}
inline LRESULT CALLBACK sliderProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (m == WM_ERASEBKGND) return 1; // Avoid a default erase before custom painting.
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        childBackground(h, dc);
        RECT r, thumb;
        GetClientRect(h, &r);
        SendMessage(h, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        const float left = 6.f, right = float(std::max(7L, r.right - 6));
        const float center = float(r.bottom) / 2;
        const float position = std::clamp(float(thumb.left + thumb.right) / 2, left, right);
        Gdiplus::Pen track(Gdiplus::Color(255, 62, 72, 96), 4.f);
        track.SetStartCap(Gdiplus::LineCapRound); track.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&track, left, center, right, center);
        Gdiplus::Pen active(Gdiplus::Color(255, 161, 117, 214), 4.f);
        active.SetStartCap(Gdiplus::LineCapRound); active.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&active, left, center, position, center);
        Gdiplus::SolidBrush knob(Gdiplus::Color(255, 171, 148, 209));
        Gdiplus::Pen outline(GetFocus() == h ? Gdiplus::Color(255, 118, 201, 255)
                                          : Gdiplus::Color(255, 205, 186, 236), 1.f);
        g.FillEllipse(&knob, position - 6.f, center - 6.f, 12.f, 12.f);
        g.DrawEllipse(&outline, position - 6.f, center - 6.f, 12.f, 12.f);
        EndPaint(h, &ps);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
inline void theme(HWND h, bool checkbox = false) {
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    SetWindowTheme(h, L"", L"");
    if (std::wstring_view(cls) == L"Edit" || std::wstring_view(cls) == L"ListBox" ||
        (GetWindowLongPtrW(h, GWL_STYLE) & (WS_VSCROLL | WS_HSCROLL))) darkWindow(h);
    if (std::wstring_view(cls) == TRACKBAR_CLASSW)
        SetWindowSubclass(h, sliderProc, 8, 0);
    if (std::wstring_view(cls) == L"ComboBox")
        SetWindowSubclass(h, comboProc, 4, 0);
    if (std::wstring_view(cls) == L"Button") {
        if (GetPropW(GetParent(h), L"sl.root"))
            SetWindowLongPtrW(h, GWL_EXSTYLE, GetWindowLongPtrW(h, GWL_EXSTYLE) | WS_EX_LAYERED);
        auto bits = GetWindowLongPtr(h, GWL_STYLE);
        SetWindowLongPtr(h, GWL_STYLE,
                         (bits & ~BS_TYPEMASK) | (checkbox ? BS_AUTOCHECKBOX : BS_OWNERDRAW) | WS_TABSTOP);
        if (checkbox)
            SetPropW(h, L"sl.check", reinterpret_cast<HANDLE>(1));
        SetWindowSubclass(h, buttonProc, 2, 0);
        // Layered windows need their initial surface submitted explicitly;
        // invalidation alone does not establish a compositor surface.
        if (GetPropW(GetParent(h), L"sl.root")) SendMessageW(h, WM_PAINT, 0, 0);
    }
}
} // namespace starlight::editor::style
