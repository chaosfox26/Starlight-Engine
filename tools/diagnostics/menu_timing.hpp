#pragma once
#include <windows.h>
#include <array>
#include <filesystem>
#include <fstream>

namespace starlight::editor {
// Opt-in, bounded samples. No file I/O or allocation in the measured UI path.
// These are CPU handler durations, not input-to-display latency.
class MenuTiming {
    struct Sample { const char* stage; double microseconds; };
    struct Samples {
        std::array<Sample, 4096> values{};
        size_t count = 0;
        wchar_t path[32768]{};
        LARGE_INTEGER frequency{};
        Samples() {
            const auto length = GetEnvironmentVariableW(L"STARLIGHT_MENU_TIMING", path, 32768);
            if (!length || length >= 32768) path[0] = 0;
            QueryPerformanceFrequency(&frequency);
        }
        ~Samples() {
            if (!path[0]) return;
            try {
                std::ofstream out{std::filesystem::path(path)};
                out << "stage,cpu_microseconds,target_100us_met\n";
                for (size_t i = 0; i < count; ++i)
                    out << values[i].stage << ',' << values[i].microseconds << ','
                        << (values[i].microseconds <= 100.0 ? "true" : "false") << '\n';
            } catch (...) {} // Diagnostics must not disrupt editor shutdown.
        }
    };
    static Samples& samples() { static Samples result; return result; }
    const char* stage_;
    LARGE_INTEGER start_{};
    bool enabled_;
public:
    explicit MenuTiming(const char* stage) : stage_(stage), enabled_(samples().path[0] != 0) {
        if (enabled_) QueryPerformanceCounter(&start_);
    }
    ~MenuTiming() {
        if (!enabled_) return;
        LARGE_INTEGER end; QueryPerformanceCounter(&end);
        auto& data = samples();
        if (data.count < data.values.size())
            data.values[data.count++] = {stage_, (end.QuadPart - start_.QuadPart) * 1e6 / data.frequency.QuadPart};
    }
};
}
