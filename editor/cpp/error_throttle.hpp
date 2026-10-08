#pragma once
#include <chrono>
#include <string>
#include <string_view>
// A persistent per-tick failure used to write a diagnostic file and open a modal dialog on every timer tick (audit ED-06). The first report of a text is
// always admitted; the same text inside the window is only counted, so the first error stays visible and the disk and desktop are not flooded.
class ErrorThrottle {
  public:
    explicit ErrorThrottle(std::chrono::milliseconds window = std::chrono::seconds(5)) : window_(window) {}
    bool admit(std::string_view text, std::chrono::steady_clock::time_point now) {
        if (any_ && text == last_ && now - at_ < window_) {
            ++suppressed_;
            return false;
        }
        any_ = true;
        last_.assign(text);
        at_ = now;
        return true;
    }
    // Only a failure raised by the timer or frame message can repeat on its own. An error from something the person just did (a second Save, Open or Paste) is always reported.
    bool admit(std::string_view text, std::chrono::steady_clock::time_point now, bool periodic) { return !periodic || admit(text, now); }
    size_t suppressed() const { return suppressed_; }

  private:
    std::chrono::milliseconds window_;
    std::string last_;
    std::chrono::steady_clock::time_point at_{};
    size_t suppressed_ = 0;
    bool any_ = false;
};
