#pragma once
#include "platform.hpp"
#include <mmsystem.h>
namespace starlight::editor {
class LocalMusic {
    std::wstring alias_ = L"starlight" + wide(new_id());
    bool opened_ = false, playing_ = false;
    double volume_ = .5;
    std::wstring command(const std::wstring &text) {
        wchar_t output[512]{};
        auto error = mciSendStringW(text.c_str(), output, 512, nullptr);
        if (error) {
            mciGetErrorStringW(error, output, 512);
            throw std::runtime_error(utf8(output));
        }
        return output;
    }

  public:
    ~LocalMusic() { close(); }
    LocalMusic() = default;
    LocalMusic(const LocalMusic &) = delete;
    LocalMusic &operator=(const LocalMusic &) = delete;
    void close() noexcept {
        if (opened_)
            mciSendStringW((L"close " + alias_).c_str(), nullptr, 0, nullptr);
        opened_ = playing_ = false;
    }
    void open(const fs::path &path) {
        close();
        auto file = path.wstring();
        if (file.find(L'"') != std::wstring::npos)
            throw std::runtime_error("Invalid music path");
        try {
            command(L"open \"" + file + L"\" type mpegvideo alias " + alias_);
            opened_ = true;
            volume(volume_);
        } catch (...) {
            close();
            throw;
        }
    }
    void play() {
        if (opened_) {
            command(L"play " + alias_);
            playing_ = true;
        }
    }
    void pause() {
        if (opened_) {
            command(L"pause " + alias_);
            playing_ = false;
        }
    }
    void stop() {
        if (opened_) {
            command(L"stop " + alias_);
            command(L"seek " + alias_ + L" to start");
            playing_ = false;
        }
    }
    void volume(double value) {
        volume_ = std::clamp(value, 0., 1.);
        if (opened_)
            command(L"setaudio " + alias_ + L" volume to " + std::to_wstring(int(volume_ * 1000)));
    }
    bool ended() {
        if (opened_ && playing_ && command(L"status " + alias_ + L" mode") == L"stopped") {
            playing_ = false;
            return true;
        }
        return false;
    }
    std::wstring state() { return opened_ ? command(L"status " + alias_ + L" mode") : L"closed"; }
};
} // namespace starlight::editor
