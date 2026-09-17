#include "native_session.hpp"
#include "physics.hpp"
#include "projects.hpp"
#include <chrono>
#include <iostream>
#include <psapi.h>
using namespace starlight::editor;
int wmain(int argc, wchar_t **argv) {
    fs::path output = argc > 1 ? argv[1] : L"session-checks";
    fs::create_directories(output);
    json checks = json::array();
    auto require = [&](bool good, const char *name) {
        if (!good)
            throw std::runtime_error(name);
        checks.push_back(name);
    };
    try {
        SceneData scene;
        scene.Terrain.emplace();
        Character rock;
        rock.Shape = "rock";
        rock.Dynamic = true;
        rock.Y = 3;
        scene.Characters.push_back(rock);
        {
            PhysicsSession physics;
            physics.reset(scene);
            auto deadline = GetTickCount64() + 10000;
            while (!physics.ready() && GetTickCount64() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            require(physics.ready(), "Native asynchronous terrain collision cook completes");
            for (int i = 0; i < 180; i++)
                physics.step(scene, 1. / 60, nullptr);
            require(physics.ticks() == 180, "Native Jolt fixed-step clock");
            require(scene.Characters[0].Y < 1 && scene.Characters[0].Y > -.5 &&
                        scene.Characters[0].PhysicsRotation.has_value(),
                    "Dynamic rock falls and rests on terrain");
        }
        WNDCLASSW wc{};
        wc.hInstance = GetModuleHandle(nullptr);
        wc.lpfnWndProc = DefWindowProcW;
        wc.lpszClassName = L"StarlightNativeSessionCheck";
        RegisterClassW(&wc);
        HWND window = CreateWindowW(wc.lpszClassName, L"Native session check", WS_OVERLAPPEDWINDOW, 0, 0, 800,
                                    600, nullptr, nullptr, wc.hInstance, nullptr);
        require(window != nullptr, "Native host window");
        {
            NativeSession session(window, 640, 360);
            session.scene(scene);
            auto wait = [&](uint64_t target) {
                auto deadline = GetTickCount64() + 15000;
                for (;;) {
                    auto status = session.status();
                    if (!status.error.empty())
                        throw std::runtime_error(status.error);
                    if (status.frames >= target)
                        return status;
                    if (GetTickCount64() > deadline)
                        throw std::runtime_error("Native worker frame timeout");
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            };
            auto status = wait(5);
            require(status.width == 640, "Worker renders at requested native resolution");
            session.capture(output / L"native-session.bmp");
            DWORD initialHandles = 0, finalHandles = 0;
            PROCESS_MEMORY_COUNTERS_EX initialMemory{}, finalMemory{};
            GetProcessHandleCount(GetCurrentProcess(), &initialHandles);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&initialMemory), sizeof(initialMemory));
            for (int i = 0; i < 40; i++) {
                SceneData next;
                next.Terrain.emplace();
                if (i % 2)
                    next.Characters.push_back(rock);
                session.scene(next);
                wait(session.status().frames + 2);
            }
            GetProcessHandleCount(GetCurrentProcess(), &finalHandles);
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&finalMemory), sizeof(finalMemory));
            atomic_write(output / "lifecycle-memory.json", json{{"cycles",40},{"initialHandles",initialHandles},
                {"finalHandles",finalHandles},{"initialPrivateBytes",initialMemory.PrivateUsage},
                {"finalPrivateBytes",finalMemory.PrivateUsage}}.dump(2));
            require(finalHandles <= initialHandles + 8 && finalMemory.PrivateUsage <= initialMemory.PrivateUsage + 64ull*1024*1024,
                    "Forty scene replacements keep handles and private memory within warm-up bounds");
            session.scene(scene);
            session.playback(true);
            wait(session.status().frames + 15);
            session.playback(false, true);
            wait(session.status().frames + 3);
            require(true, "Native playback stop resets safely");
            SceneData route;
            Character walker;
            walker.Shape = "cube";
            walker.Route = {{.1, 0}};
            walker.Speed = 1;
            route.Characters.push_back(walker);
            session.scene(route);
            session.playback_options(false, walker.Id);
            session.playback(true);
            wait(session.status().frames + 12);
            require(!session.status().playing && std::abs(session.status().seconds - .1) < 1e-6,
                    "Path completion stops nonrepeating playback");
            session.playback_options(true, walker.Id);
            session.playback(true, true);
            wait(session.status().frames + 12);
            require(session.status().playing && session.status().seconds > .1,
                    "Repeat and character-follow continue beyond route duration");
        }
        DestroyWindow(window);
        if (argc > 2) {
            auto project = ProjectData::load(fs::path(argv[2]) / L"deep/projects/Empty/Empty.star");
            auto preset = fs::path(argv[2]) / L"deep/settings/native-1440p.startest";
            auto original = hash_file(preset);
            wchar_t exe[32768];
            GetModuleFileNameW(nullptr, exe, 32768);
            auto resolved = resolve_preset(fs::path(exe).parent_path() / L"Starlight.exe", project, preset);
            require(resolved.data.at("schemaVersion") == 2 && hash_file(preset) == original,
                    "Native project and preset reconciliation preserves original");
        }
        atomic_write(output / L"session-results.json", json{{"success", true}, {"checks", checks}}.dump(2));
        std::cout << "PASS " << checks.size() << " native session checks\n";
        return 0;
    } catch (const std::exception &e) {
        atomic_write(output / L"session-results.json",
                     json{{"success", false}, {"checks", checks}, {"error", e.what()}}.dump(2));
        std::cerr << e.what() << '\n';
        return 1;
    }
}
