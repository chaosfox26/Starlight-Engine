#pragma once
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace host_notice {

struct Model {
    std::vector<std::string> notices;
    std::vector<std::string> footer;
    bool expanded = false;
    bool dismissed = false;

    bool set(const std::vector<std::string>& list, const std::vector<std::string>& info) {
        if (list == notices && info == footer) return false;
        const bool grew = list != notices;
        notices = list;
        footer = info;
        if (grew) { dismissed = false; expanded = false; }
        return true;
    }
    bool visible() const { return !notices.empty() && !dismissed; }
    void toggle() {
        if (notices.empty()) return;
        if (dismissed) { dismissed = false; expanded = true; return; }
        expanded = !expanded;
    }
    void dismiss() { dismissed = true; expanded = false; }
    std::string headline() const {
        return std::to_string(notices.size()) + (notices.size() == 1 ? " content notice" : " content notices") + " - the room runs with reduced features";
    }
    std::string hint() const { return expanded ? "F1 or click: hide details" : "F1 or click: show details"; }
    std::vector<std::string> details(size_t maxItems, size_t maxChars) const {
        std::vector<std::string> lines;
        size_t shown = 0;
        for (const std::string& notice : notices) {
            if (shown == maxItems) break;
            lines.push_back(std::to_string(shown + 1) + ". " + (notice.size() > maxChars ? notice.substr(0, maxChars - 3) + "..." : notice));
            ++shown;
        }
        if (notices.size() > shown) lines.push_back("... and " + std::to_string(notices.size() - shown) + " more (see the log)");
        for (const std::string& info : footer) lines.push_back(info);
        return lines;
    }
};

inline std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    std::wstring wide((size_t)(std::max)(0, count), L'\0');
    if (count > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), count);
    return wide;
}

class Layer {
  public:
    Layer() = default;
    Layer(const Layer&) = delete;
    Layer& operator=(const Layer&) = delete;
    ~Layer() { close(); }

    void attach(HWND owner, HINSTANCE instance) { owner_ = owner; instance_ = instance; }
    const Model& model() const { return model_; }

    void update(const std::vector<std::string>& notices, const std::vector<std::string>& info) {
        if (model_.set(notices, info)) refresh();
    }
    void toggle() { model_.toggle(); refresh(); }
    void close() { if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; } }

    void follow() {
        if (!hwnd_) return;
        if (!owner_ || IsIconic(owner_) || !IsWindowVisible(owner_)) { if (IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE); return; }
        place(false);
    }

  private:
    HWND owner_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HFONT font_ = nullptr, bold_ = nullptr;
    Model model_;
    int width_ = 0, height_ = 0, dpi_ = 96, headerHeight_ = 0;
    RECT placed_{};

    static LRESULT CALLBACK procedure(HWND h, UINT m, WPARAM w, LPARAM l) {
        Layer* self = reinterpret_cast<Layer*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        switch (m) {
        case WM_NCCREATE: SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); break;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ERASEBKGND: return 1;
        case WM_SETCURSOR: SetCursor(LoadCursorW(nullptr, MAKEINTRESOURCEW(32649))); return TRUE;
        case WM_LBUTTONDOWN:
            if (self) {
                const int x = (short)LOWORD(l), y = (short)HIWORD(l);
                const int box = self->scale(22);
                if (x >= self->width_ - box - self->scale(6) && y <= box + self->scale(6)) self->model_.dismiss(); else self->model_.toggle();
                self->refresh();
            }
            return 0;
        case WM_PAINT:
            if (self) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); self->paint(dc); EndPaint(h, &ps); return 0; }
            break;
        }
        return DefWindowProcW(h, m, w, l);
    }

    int scale(int value) const { return MulDiv(value, dpi_, 96); }

    void makeFonts(HDC dc) {
        const int logical = GetDeviceCaps(dc, LOGPIXELSY);
        if (font_ && logical == dpi_) return;
        dpi_ = logical > 0 ? logical : 96;
        if (font_) DeleteObject(font_);
        if (bold_) DeleteObject(bold_);
        font_ = CreateFontW(-MulDiv(10, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        bold_ = CreateFontW(-MulDiv(11, dpi_, 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    }

    std::wstring bodyText() const {
        std::wstring text;
        for (const std::string& line : model_.details(12, 300)) { if (!text.empty()) text += L"\n"; text += widen(line); }
        return text;
    }

    void layout(HDC dc) {
        makeFonts(dc);
        RECT client{}; GetClientRect(owner_, &client);
        width_ = (std::max)(scale(320), (std::min)(scale(780), (int)(client.right - client.left) - scale(40)));
        const int pad = scale(10), inner = width_ - 2 * pad - scale(28);
        HGDIOBJ old = SelectObject(dc, bold_);
        RECT head{ 0, 0, inner, 0 };
        const std::wstring headline = widen(model_.headline());
        DrawTextW(dc, headline.c_str(), -1, &head, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, font_);
        RECT hint{ 0, 0, inner, 0 };
        const std::wstring hintText = widen(model_.hint());
        DrawTextW(dc, hintText.c_str(), -1, &hint, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        headerHeight_ = (head.bottom - head.top) + (hint.bottom - hint.top) + 2 * pad;
        height_ = headerHeight_;
        if (model_.expanded) {
            RECT body{ 0, 0, width_ - 2 * pad, 0 };
            const std::wstring text = bodyText();
            DrawTextW(dc, text.c_str(), -1, &body, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
            height_ += (body.bottom - body.top) + pad;
        }
        SelectObject(dc, old);
    }

    void paint(HDC dc) {
        makeFonts(dc);
        RECT area{}; GetClientRect(hwnd_, &area);
        HBRUSH fill = CreateSolidBrush(RGB(10, 6, 20)); FillRect(dc, &area, fill); DeleteObject(fill);
        HBRUSH edge = CreateSolidBrush(RGB(255, 40, 200)); FrameRect(dc, &area, edge); DeleteObject(edge);
        const int pad = scale(10), box = scale(22);
        SetBkMode(dc, TRANSPARENT);
        HGDIOBJ old = SelectObject(dc, bold_);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT head{ pad, pad, width_ - pad - box - pad, height_ };
        const std::wstring headline = widen(model_.headline());
        DrawTextW(dc, headline.c_str(), -1, &head, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, font_);
        SetTextColor(dc, RGB(120, 170, 255));
        RECT calc = head; DrawTextW(dc, headline.c_str(), -1, &calc, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        RECT hint{ pad, calc.bottom + scale(2), width_ - pad, height_ };
        const std::wstring hintText = widen(model_.hint());
        DrawTextW(dc, hintText.c_str(), -1, &hint, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT close{ width_ - box - scale(6), scale(6), width_ - scale(6), scale(6) + box };
        DrawTextW(dc, L"x", -1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (model_.expanded) {
            HPEN rule = CreatePen(PS_SOLID, 1, RGB(255, 40, 200));
            HGDIOBJ oldPen = SelectObject(dc, rule);
            MoveToEx(dc, pad, headerHeight_ - scale(2), nullptr); LineTo(dc, width_ - pad, headerHeight_ - scale(2));
            SelectObject(dc, oldPen); DeleteObject(rule);
            SetTextColor(dc, RGB(235, 235, 245));
            RECT body{ pad, headerHeight_ + scale(2), width_ - pad, height_ };
            const std::wstring text = bodyText();
            DrawTextW(dc, text.c_str(), -1, &body, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        }
        SelectObject(dc, old);
    }

    void place(bool force) {
        if (!hwnd_ || !owner_) return;
        RECT client{}; GetClientRect(owner_, &client);
        POINT origin{ 0, 0 }; ClientToScreen(owner_, &origin);
        const int x = origin.x + scale(20), y = origin.y + scale(20);
        if (!force && placed_.left == x && placed_.top == y && placed_.right == width_ && placed_.bottom == height_ && IsWindowVisible(hwnd_)) return;
        placed_ = RECT{ x, y, width_, height_ };
        SetWindowPos(hwnd_, nullptr, x, y, width_, height_, SWP_NOACTIVATE | SWP_NOZORDER | SWP_SHOWWINDOW);
    }

    void refresh() {
        if (!owner_) return;
        if (!model_.visible()) { if (hwnd_ && IsWindowVisible(hwnd_)) ShowWindow(hwnd_, SW_HIDE); return; }
        if (!hwnd_) {
            static ATOM registered = 0;
            if (!registered) {
                WNDCLASSW wc{}; wc.lpfnWndProc = &Layer::procedure; wc.hInstance = instance_; wc.lpszClassName = L"StarlightHostNotice"; wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32649));
                registered = RegisterClassW(&wc);
            }
            hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"StarlightHostNotice", L"", WS_POPUP, 0, 0, 10, 10, owner_, nullptr, instance_, this);
            if (!hwnd_) return;
        }
        HDC dc = GetDC(hwnd_);
        layout(dc);
        ReleaseDC(hwnd_, dc);
        place(true);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
};

}
