#pragma once
#include "editor_icons.hpp"
#include <dwmapi.h>
#include <gdiplus.h>
#include <uxtheme.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")

namespace starlight::editor::style {
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
inline void round(Gdiplus::Graphics &g, Gdiplus::RectF r, float radius, Gdiplus::Color top,
                  Gdiplus::Color bottom, Gdiplus::Color edge) {
    Gdiplus::GraphicsPath path;
    float d = radius * 2;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
    path.CloseFigure();
    Gdiplus::LinearGradientBrush fill(r, top, bottom, 90.f);
    g.FillPath(&fill, &path);
    Gdiplus::Pen border(edge, 1.f);
    g.DrawPath(&border, &path);
}
inline void text(HDC dc, HFONT font, std::wstring value, RECT r, COLORREF color = ink,
                 UINT flags = DT_SINGLELINE | DT_VCENTER | DT_CENTER) {
    auto old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, value.c_str(), int(value.size()), &r, flags | DT_NOPREFIX);
    SelectObject(dc, old);
}
// Paint child backgrounds in the parent's coordinate space so rounded controls
// and text do not leave opaque rectangular patches across the glass gradient.
inline void childBackground(HWND h, HDC dc) {
    HWND parent = GetParent(h);
    RECT r;
    GetClientRect(parent, &r);
    POINT offset{0, 0};
    MapWindowPoints(h, parent, &offset, 1);
    Gdiplus::Graphics g(dc);
    g.TranslateTransform(float(-offset.x), float(-offset.y));
    Gdiplus::Rect bounds(0, 0, std::max(1L, r.right), std::max(1L, r.bottom));
    Gdiplus::LinearGradientBrush bg(bounds, Gdiplus::Color(255, 41, 51, 77), Gdiplus::Color(255, 22, 20, 37),
                                    90.f);
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
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        childBackground(h, dc);
        RECT r, thumb;
        GetClientRect(h, &r);
        SendMessage(h, TBM_GETTHUMBRECT, 0, reinterpret_cast<LPARAM>(&thumb));
        Gdiplus::Graphics g(dc);
        Gdiplus::SolidBrush ink(Gdiplus::Color(255, 214, 221, 233));
        g.FillRectangle(&ink, 5, r.bottom / 2 - 2, std::max(1L, r.right - 10), 4);
        g.FillRectangle(&ink, (thumb.left + thumb.right) / 2 - 5, 0, 10, r.bottom);
        EndPaint(h, &ps);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}
inline void theme(HWND h, bool checkbox = false) {
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    SetWindowTheme(h, L"", L"");
    if (std::wstring_view(cls) == TRACKBAR_CLASSW)
        SetWindowSubclass(h, sliderProc, 8, 0);
    if (std::wstring_view(cls) == L"ComboBox")
        SetWindowSubclass(h, comboProc, 4, 0);
    if (std::wstring_view(cls) == L"Button") {
        auto bits = GetWindowLongPtr(h, GWL_STYLE);
        SetWindowLongPtr(h, GWL_STYLE,
                         (bits & ~BS_TYPEMASK) | (checkbox ? BS_AUTOCHECKBOX : BS_OWNERDRAW) | WS_TABSTOP);
        if (checkbox)
            SetPropW(h, L"sl.check", reinterpret_cast<HANDLE>(1));
        SetWindowSubclass(h, buttonProc, 2, 0);
    }
}
} // namespace starlight::editor::style
