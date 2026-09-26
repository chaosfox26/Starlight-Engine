#pragma once
#include <windows.h>
#include <array>
#include <algorithm>
#include <stdexcept>

namespace starlight::editor {
// Click-through edge shading matching the reference's 48px, alpha-70 fade.
class EdgeFeedback {
    std::array<HWND, 4> shades_{};
    std::array<RECT, 4> rectangles_{};
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        if (m == WM_NCHITTEST) return HTTRANSPARENT;
        if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        return DefWindowProcW(h, m, w, l);
    }
public:
    ~EdgeFeedback() { for (auto h : shades_) if (h) DestroyWindow(h); }
    EdgeFeedback() = default;
    EdgeFeedback(const EdgeFeedback&) = delete;
    EdgeFeedback& operator=(const EdgeFeedback&) = delete;
    bool owns(HWND h) const { for (auto s : shades_) if (s && s == h) return true; return false; }
    unsigned visible_edges() const { unsigned mask = 0; for (unsigned i = 0; i < shades_.size(); ++i) if (shades_[i] && IsWindowVisible(shades_[i])) mask |= 1u << i; return mask; }
    void hide() { for (auto h : shades_) if (h) ShowWindow(h, SW_HIDE); }
    void update(HWND owner, HWND canvas, double x, double z) {
        if (x == 0 && z == 0) { hide(); return; }
        static const ATOM atom = [] {
            WNDCLASSW wc{}; wc.hInstance = GetModuleHandleW(nullptr); wc.lpfnWndProc = proc;
            wc.lpszClassName = L"Starlight.EdgeFeedback";
            return RegisterClassW(&wc);
        }();
        if (!atom) throw std::runtime_error("Cannot register edge feedback");
        RECT r; GetClientRect(canvas, &r); MapWindowPoints(canvas, nullptr, reinterpret_cast<POINT*>(&r), 2);
        const bool active[] = {x < 0, x > 0, z > 0, z < 0};
        for (int i = 0; i < 4; ++i) {
            if (!active[i]) { if (shades_[i]) ShowWindow(shades_[i], SW_HIDE); continue; }
            RECT bounds = r;
            if (i == 0) bounds.right = std::min(r.right, r.left + 48);
            if (i == 1) bounds.left = std::max(r.left, r.right - 48);
            if (i == 2) bounds.bottom = std::min(r.bottom, r.top + 48);
            if (i == 3) bounds.top = std::max(r.top, r.bottom - 48);
            if (!shades_[i]) {
                shades_[i] = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                    MAKEINTATOM(atom), L"", WS_POPUP, 0, 0, 0, 0, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (!shades_[i]) throw std::runtime_error("Cannot create edge feedback");
            }
            if (!EqualRect(&bounds, &rectangles_[i])) {
                int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
                if (width <= 0 || height <= 0) continue;
                BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = width;
                bi.bmiHeader.biHeight = -height; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                void* bits = nullptr;
                HDC dc = CreateCompatibleDC(nullptr);
                HBITMAP bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
                if (!dc || !bitmap || !bits) {
                    if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc);
                    throw std::runtime_error("Cannot allocate edge feedback bitmap");
                }
                auto pixels = static_cast<DWORD*>(bits);
                for (int py = 0; py < height; ++py) for (int px = 0; px < width; ++px) {
                    int offset = i == 0 ? px : i == 1 ? width - 1 - px : i == 2 ? py : height - 1 - py;
                    pixels[py * width + px] = DWORD(70 * (47 - std::min(47, offset)) / 47) << 24;
                }
                auto previous = SelectObject(dc, bitmap);
                POINT target{bounds.left, bounds.top}, source{}; SIZE size{width, height};
                BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
                BOOL okay = UpdateLayeredWindow(shades_[i], nullptr, &target, &size, dc, &source, 0, &blend, ULW_ALPHA);
                SelectObject(dc, previous); DeleteObject(bitmap); DeleteDC(dc);
                if (!okay) throw std::runtime_error("Cannot present edge feedback");
                rectangles_[i] = bounds;
            }
            ShowWindow(shades_[i], SW_SHOWNOACTIVATE);
        }
    }
};
}
