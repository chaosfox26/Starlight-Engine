#pragma once
#include "editor_style.hpp"
#include "native_session.hpp"
#include <functional>
#include <numbers>
#include <windowsx.h>

namespace starlight::editor
{
// One owned HWND/session per editor, retained while hidden. Closing parks the
// worker; final destruction joins it before destroying its presentation HWND.
class GamePreview
{
    HWND owner_{}, window_{}, view_{}, play_{}, reset_{};
    HFONT font_{};
    std::unique_ptr<NativeSession> session_;
    std::function<void(bool)> changed_;
    std::function<void(std::string)> error_;
    CameraState camera_;
    POINT last_{};
    bool orbit_ = false, playing_ = false;
    bool finished_ = false;
    uint64_t playFrame_ = 0;
    double pan_ = 1, tilt_ = 1;
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        auto p = reinterpret_cast<GamePreview *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE)
        {
            p = static_cast<GamePreview *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            p->window_ = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(p));
        }
        try
        {
            if (p)
                return p->message(m, w, l);
        }
        catch (const std::exception &e)
        {
            if (p)
            {
                p->hide();
                p->error_(e.what());
            }
        }
        return DefWindowProcW(h, m, w, l);
    }
    static LRESULT CALLBACK input(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data)
    {
        auto &p = *reinterpret_cast<GamePreview *>(data);
        if (m == WM_MBUTTONDOWN)
        {
            p.orbit_ = true;
            p.last_ = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            SetFocus(h);
            SetCapture(h);
            return 0;
        }
        if (m == WM_MBUTTONUP || m == WM_CAPTURECHANGED)
        {
            p.orbit_ = false;
            if (GetCapture() == h)
                ReleaseCapture();
            return 0;
        }
        if (m == WM_LBUTTONDOWN)
        {
            SetFocus(h);
            return 0;
        }
        if (m == WM_MOUSEWHEEL)
        {
            auto d = p.camera_.eye - p.camera_.target;
            auto length = std::clamp(d.length() * std::pow(.85, GET_WHEEL_DELTA_WPARAM(w) / 120.), 1., 150.);
            p.camera_.eye = p.camera_.target + d.unit() * length;
            p.session_->camera(p.camera_);
            return 0;
        }
        if (m == WM_MOUSEMOVE && p.orbit_)
        {
            POINT q{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            auto d = p.camera_.eye - p.camera_.target;
            double r = d.length();
            double yaw = std::atan2(d.x, d.z) + (q.x - p.last_.x) * .006 * p.tilt_;
            double pitch = std::clamp(std::asin(d.y / r) + (q.y - p.last_.y) * .006 * p.tilt_, -1.4, 1.48);
            p.camera_.eye = p.camera_.target + Vec3{std::sin(yaw) * std::cos(pitch) * r, std::sin(pitch) * r,
                                                    std::cos(yaw) * std::cos(pitch) * r};
            p.last_ = q;
            p.session_->camera(p.camera_);
            return 0;
        }
        if (m == WM_KEYDOWN)
        {
            if (w == VK_ESCAPE)
            {
                if (p.orbit_)
                {
                    p.orbit_ = false;
                    ReleaseCapture();
                }
                else
                    p.hide();
                return 0;
            }
            if (w == VK_SPACE)
            {
                p.playback(!p.playing_);
                return 0;
            }
            auto f = (p.camera_.target - p.camera_.eye);
            f.y = 0;
            f = f.unit();
            auto right = f.cross({0, 1, 0});
            Vec3 move{};
            if (w == VK_UP)
                move = f;
            else if (w == VK_DOWN)
                move = f * -1.;
            else if (w == VK_LEFT)
                move = right * -1.;
            else if (w == VK_RIGHT)
                move = right;
            else
                return DefSubclassProc(h, m, w, l);
            move = move * (p.camera_.distance * .04 * p.pan_);
            p.camera_.eye = p.camera_.eye + move;
            p.camera_.target = p.camera_.target + move;
            p.session_->camera(p.camera_);
            return 0;
        }
        return DefSubclassProc(h, m, w, l);
    }
    LRESULT message(UINT m, WPARAM w, LPARAM l)
    {
        if (m == WM_SIZE && view_)
        {
            RECT r;
            GetClientRect(window_, &r);
            int vh = std::max(1L, r.bottom - 56);
            MoveWindow(view_, 0, 0, std::max(1L, r.right), vh, TRUE);
            MoveWindow(play_, std::max(8L, r.right - 234), vh + 10, 110, 34, TRUE);
            MoveWindow(reset_, std::max(126L, r.right - 116), vh + 10, 108, 34, TRUE);
            if (session_)
            {
                session_->size(r.right, vh);
                session_->suspend_rendering(w == SIZE_MINIMIZED);
            }
            return 0;
        }
        if (m == WM_GETMINMAXINFO)
        {
            reinterpret_cast<MINMAXINFO *>(l)->ptMinTrackSize = {480, 360};
            return 0;
        }
        if (m == WM_COMMAND)
        {
            if (LOWORD(w) == 1)
                playback(!playing_);
            if (LOWORD(w) == 2)
                playback(false, true);
            return 0;
        }
        if (m == WM_CLOSE)
        {
            hide();
            return 0;
        }
        if (m == WM_ACTIVATE && LOWORD(w) == WA_INACTIVE)
        {
            orbit_ = false;
            if (GetCapture() == view_)
                ReleaseCapture();
        }
        if (m == WM_ERASEBKGND)
        {
            RECT r;
            GetClientRect(window_, &r);
            FillRect(reinterpret_cast<HDC>(w), &r, style::panelBrush());
            return 1;
        }
        if (m == WM_DRAWITEM)
        {
            auto &d = *reinterpret_cast<DRAWITEMSTRUCT *>(l);
            style::button(d, font_);
            return TRUE;
        }
        if (m == WM_TIMER && session_)
        {
            auto s = session_->status();
            if (!s.error.empty())
            {
                hide();
                error_(s.error);
                return 0;
            }
            if (playing_ && s.frames > playFrame_ + 1 && !s.playing && s.seconds > 0)
            {
                finished_ = true;
                playing_ = false;
                SetWindowTextW(play_, L"Play again");
                changed_(false);
            }
            return 0;
        }
        if (m == WM_DPICHANGED)
        {
            auto r = reinterpret_cast<RECT *>(l);
            SetWindowPos(window_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        return DefWindowProcW(window_, m, w, l);
    }

  public:
    GamePreview(HWND owner, HINSTANCE instance, HFONT font, std::function<void(bool)> changed,
                std::function<void(std::string)> error)
        : owner_(owner), font_(font), changed_(std::move(changed)), error_(std::move(error))
    {
        WNDCLASSW wc{};
        wc.hInstance = instance;
        wc.lpfnWndProc = proc;
        wc.lpszClassName = L"StarlightGamePreview";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = style::panelBrush();
        RegisterClassW(&wc);
        RECT r;
        GetWindowRect(owner, &r);
        window_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Starlight — native scene playback",
                                  WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, r.left + 60, r.top + 60, 960, 640, owner,
                                  nullptr, instance, this);
        if (!window_)
            throw std::runtime_error("Cannot create playback window");
        try
        {
            view_ = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOTIFY, 0, 0, 960, 560, window_,
                                    nullptr, instance, nullptr);
            play_ = CreateWindowExW(0, L"BUTTON", L"Pause", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 110, 34, window_,
                                    reinterpret_cast<HMENU>(1), instance, nullptr);
            reset_ = CreateWindowExW(0, L"BUTTON", L"Reset", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 108, 34, window_,
                                     reinterpret_cast<HMENU>(2), instance, nullptr);
            if (!view_ || !play_ || !reset_)
                throw std::runtime_error("Cannot create playback controls");
            for (auto h : {play_, reset_})
            {
                SendMessage(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                style::theme(h, false);
            }
            SetWindowSubclass(view_, input, 1, reinterpret_cast<DWORD_PTR>(this));
            BOOL dark = TRUE;
            DwmSetWindowAttribute(window_, 20, &dark, sizeof(dark));
            session_ = std::make_unique<NativeSession>(view_, 960, 560);
            SendMessage(window_, WM_SIZE, 0, 0);
        }
        catch (...)
        {
            session_.reset();
            DestroyWindow(window_);
            window_ = nullptr;
            throw;
        }
    }
    ~GamePreview()
    {
        if (window_)
            KillTimer(window_, 1);
        session_.reset();
        if (window_)
            DestroyWindow(window_);
    }
    GamePreview(const GamePreview &) = delete;
    void show(const SceneData &scene, CameraState camera, bool repeat, std::string follow, double pan, double tilt)
    {
        camera_ = camera;
        pan_ = pan;
        tilt_ = tilt;
        session_->scene(scene);
        session_->camera(camera);
        session_->playback_options(repeat, std::move(follow));
        session_->tools({}, {});
        session_->suspend_rendering(false);
        ShowWindow(window_, SW_SHOW);
        SetTimer(window_, 1, 100, nullptr);
        SetFocus(view_);
        finished_ = false;
    }
    void sync(const SceneData &scene)
    {
        session_->scene(scene);
        finished_ = false;
    }
    void options(bool repeat, std::string follow)
    {
        session_->playback_options(repeat, std::move(follow));
    }
    void playback(bool play, bool reset = false)
    {
        playFrame_ = session_->status().frames;
        session_->playback(play, reset || (play && finished_));
        finished_ = false;
        playing_ = play;
        SetWindowTextW(play_, play ? L"Pause" : L"Play");
        changed_(play);
    }
    void hide()
    {
        playback(false, true);
        session_->suspend_rendering(true);
        KillTimer(window_, 1);
        ShowWindow(window_, SW_HIDE);
        orbit_ = false;
        if (GetCapture() == view_)
            ReleaseCapture();
        SetFocus(owner_);
    }
    void escape()
    {
        if (orbit_)
        {
            orbit_ = false;
            ReleaseCapture();
        }
        else
            hide();
    }
    bool visible() const
    {
        return IsWindowVisible(window_) != FALSE;
    }
    HWND window() const
    {
        return window_;
    }
    NativeSession &session()
    {
        return *session_;
    }
};
} // namespace starlight::editor
