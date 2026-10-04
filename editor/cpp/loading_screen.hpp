#pragma once
// Starlight Engine loading screen: title, a progress bar directly beneath it, the sentence "We're loading your scene now." and the
// real current task. Painted by hand over the render window in the engine's colours (magenta, blue, black, white). The caller
// keeps pumping messages while it updates the bar, so the window stays responsive.
#include <windows.h>
#include <gdiplus.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "gdi32.lib")
namespace starlight::editor {
class LoadingScreen {
  public:
    LoadingScreen() = default;
    LoadingScreen(const LoadingScreen&) = delete;
    LoadingScreen& operator=(const LoadingScreen&) = delete;
    ~LoadingScreen() { close(); }
    bool visible() const { return hwnd_ != nullptr; }
    // True when the splash is actually on screen (a hidden or minimised host never draws frames, so nothing should wait for them).
    bool shown() const { return hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(GetAncestor(hwnd_, GA_ROOTOWNER)); }
    // Covers the client area of `view` (the render window); `owner` is the application window.
    void show(HWND owner, HWND view) {
        if (hwnd_) return;
        static const ATOM windowClass = [] {
            WNDCLASSEXW cls{sizeof(cls)};
            cls.lpfnWndProc = &LoadingScreen::procedure;
            cls.hInstance = GetModuleHandleW(nullptr);
            cls.lpszClassName = L"StarlightLoadingScreen";
            cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            cls.hbrBackground = nullptr;
            return RegisterClassExW(&cls);
        }();
        (void)windowClass;
        RECT client{};
        HWND target = view ? view : owner;
        GetClientRect(target, &client);
        POINT origin{0, 0};
        ClientToScreen(target, &origin);
        const int width = std::max<LONG>(320, client.right - client.left), height = std::max<LONG>(240, client.bottom - client.top);
        hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"StarlightLoadingScreen", L"Starlight Engine", WS_POPUP,
                                origin.x, origin.y, width, height, owner, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd_) return;
        SetWindowPos(hwnd_, HWND_TOP, origin.x, origin.y, width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        repaint();
    }
    // fraction is real completed work (0..1); task is the plain-English name of what is happening right now.
    void update(double fraction, const std::string& task) {
        if (!hwnd_) return;
        fraction_ = std::clamp(fraction, 0.0, 1.0);
        const std::wstring wide = widen(task);
        if (wide != task_ || std::abs(fraction_ - painted_) >= 0.002) {
            task_ = wide;
            repaint();
        }
    }
    void close() {
        if (hwnd_) {
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
        }
    }
    double fraction() const { return fraction_; }
    const std::wstring& task() const { return task_; }

  private:
    HWND hwnd_ = nullptr;
    double fraction_ = 0, painted_ = -1;
    std::wstring task_ = L"Starting";

    // The approved Starlight Engine title artwork (design/branding), found the same way the startup splash finds it. Missing artwork falls
    // back to a drawn title, so a scene always loads.
    static Gdiplus::Bitmap* title_artwork() {
        static std::unique_ptr<Gdiplus::Bitmap> image = [] () -> std::unique_ptr<Gdiplus::Bitmap> {
            static ULONG_PTR token = 0;
            Gdiplus::GdiplusStartupInput input;
            if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return nullptr;
            wchar_t buffer[32768];
            std::filesystem::path root;
            const DWORD n = GetEnvironmentVariableW(L"STARLIGHT_RESOURCE_ROOT", buffer, 32768);
            if (n && n < 32768 && std::filesystem::path(buffer).is_absolute()) root = buffer;
            else {
                GetModuleFileNameW(nullptr, buffer, 32768);
                root = std::filesystem::path(buffer);
                for (int i = 0; i < 4; ++i) root = root.parent_path();
            }
            auto file = root / "design" / "branding" / "starlight-alpha-one-cursive-v3.png";
            std::error_code ignored;
            if (!std::filesystem::exists(file, ignored)) return nullptr;
            auto bitmap = std::make_unique<Gdiplus::Bitmap>(file.wstring().c_str());
            if (bitmap->GetLastStatus() != Gdiplus::Ok) return nullptr;
            return bitmap;
        }();
        return image.get();
    }
    static std::wstring widen(const std::string& text) {
        if (text.empty()) return {};
        const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
        std::wstring out(size_t(std::max(0, length)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), length);
        return out;
    }
    void repaint() {
        if (!hwnd_) return;
        painted_ = fraction_;
        InvalidateRect(hwnd_, nullptr, FALSE);
        UpdateWindow(hwnd_);
    }
    static COLORREF mix(COLORREF a, COLORREF b, double t) {
        t = std::clamp(t, 0.0, 1.0);
        return RGB(int(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t), int(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
                   int(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
    }
    static HFONT font(int pixels, int weight, const wchar_t* face) {
        return CreateFontW(-pixels, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, face);
    }
    static void text(HDC dc, const std::wstring& value, RECT box, UINT format, COLORREF color) {
        SetTextColor(dc, color);
        DrawTextW(dc, value.c_str(), int(value.size()), &box, format | DT_NOPREFIX | DT_SINGLELINE);
    }
    void paint(HDC target, int width, int height) {
        const COLORREF magenta = RGB(255, 43, 214), blue = RGB(59, 130, 255), white = RGB(255, 255, 255), black = RGB(2, 1, 10),
                       deepBlue = RGB(9, 14, 46), paleBlue = RGB(156, 194, 255);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(target, width, height);
        HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
        SetBkMode(dc, TRANSPARENT);
        // Background: black to deep blue, with faint magenta and blue diagonals.
        for (int y = 0; y < height; y += 4) {
            HBRUSH brush = CreateSolidBrush(mix(black, deepBlue, double(y) / height));
            RECT band{0, y, width, std::min(height, y + 4)};
            FillRect(dc, &band, brush);
            DeleteObject(brush);
        }
        HPEN faintMagenta = CreatePen(PS_SOLID, 2, mix(deepBlue, magenta, .16)), faintBlue = CreatePen(PS_SOLID, 2, mix(deepBlue, blue, .2));
        HGDIOBJ oldPen = SelectObject(dc, faintMagenta);
        for (int x = -height; x < width; x += 140) {
            MoveToEx(dc, x, height, nullptr);
            LineTo(dc, x + height, 0);
        }
        SelectObject(dc, faintBlue);
        for (int x = 70; x < width + height; x += 280) {
            MoveToEx(dc, x, 0, nullptr);
            LineTo(dc, x - height, height);
        }
        SelectObject(dc, oldPen);
        DeleteObject(faintMagenta);
        DeleteObject(faintBlue);

        const int centerX = width / 2;
        int titleSize = std::clamp(height / 9, 40, 120), titleTop = height / 4, titleBottom = 0;
        HFONT titleFont = font(titleSize, FW_BLACK, L"Segoe UI"), sentenceFont = font(std::clamp(height / 36, 18, 34), FW_SEMIBOLD, L"Segoe UI"),
              taskFont = font(std::clamp(height / 44, 15, 26), FW_NORMAL, L"Segoe UI");
        HGDIOBJ oldFont = SelectObject(dc, titleFont);
        if (Gdiplus::Bitmap* art = title_artwork()) {
            // Approved title artwork, centred, drawn with its own transparency over the gradient.
            const double aspect = double(art->GetWidth()) / double(art->GetHeight());
            int artHeight = std::min(int(height * .46), int(width * .62 / aspect));
            int artWidth = int(artHeight * aspect);
            titleTop = std::max(8, int(height * .06));
            Gdiplus::Graphics graphics(dc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(art, Gdiplus::Rect(centerX - artWidth / 2, titleTop, artWidth, artHeight));
            titleBottom = titleTop + artHeight;
        } else {
            RECT title{0, titleTop, width, titleTop + titleSize + 12};
            RECT shadowBlue = title, shadowMagenta = title;
            OffsetRect(&shadowBlue, -4, 3);
            OffsetRect(&shadowMagenta, 4, -3);
            text(dc, L"STARLIGHT ENGINE", shadowBlue, DT_CENTER | DT_VCENTER, blue);
            text(dc, L"STARLIGHT ENGINE", shadowMagenta, DT_CENTER | DT_VCENTER, magenta);
            text(dc, L"STARLIGHT ENGINE", title, DT_CENTER | DT_VCENTER, white);
            titleBottom = titleTop + titleSize + 12;
        }

        // Progress bar directly below the title.
        const int barWidth = std::min(width * 6 / 10, 1100), barHeight = std::clamp(height / 40, 14, 30);
        const int barLeft = centerX - barWidth / 2, barTop = titleBottom + 22;
        HBRUSH border = CreateSolidBrush(blue), track = CreateSolidBrush(mix(black, deepBlue, .8));
        RECT outer{barLeft - 3, barTop - 3, barLeft + barWidth + 3, barTop + barHeight + 3};
        FillRect(dc, &outer, border);
        RECT inner{barLeft, barTop, barLeft + barWidth, barTop + barHeight};
        FillRect(dc, &inner, track);
        DeleteObject(border);
        DeleteObject(track);
        const int filled = int(barWidth * fraction_);
        for (int x = 0; x < filled; ++x) {
            HBRUSH column = CreateSolidBrush(mix(magenta, blue, double(x) / std::max(1, barWidth - 1)));
            RECT strip{barLeft + x, barTop, barLeft + x + 1, barTop + barHeight};
            FillRect(dc, &strip, column);
            DeleteObject(column);
        }
        // The sentence and the current task, directly under the bar.
        SelectObject(dc, sentenceFont);
        RECT sentence{0, barTop + barHeight + 22, width, barTop + barHeight + 22 + std::clamp(height / 30, 24, 44)};
        text(dc, L"We’re loading your scene now.", sentence, DT_CENTER | DT_VCENTER, white);
        SelectObject(dc, taskFont);
        RECT task{0, sentence.bottom + 8, width, sentence.bottom + 8 + std::clamp(height / 36, 20, 36)};
        const std::wstring percent = std::to_wstring(int(fraction_ * 100 + .5)) + L"%";
        text(dc, task_ + L"   " + percent, task, DT_CENTER | DT_VCENTER, paleBlue);

        SelectObject(dc, oldFont);
        DeleteObject(titleFont);
        DeleteObject(sentenceFont);
        DeleteObject(taskFont);
        BitBlt(target, 0, 0, width, height, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l) {
        if (message == WM_NCCREATE) {
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
            return DefWindowProcW(window, message, w, l);
        }
        auto* self = reinterpret_cast<LoadingScreen*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT && self) {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            self->paint(dc, client.right, client.bottom);
            EndPaint(window, &paint);
            return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
};
}
