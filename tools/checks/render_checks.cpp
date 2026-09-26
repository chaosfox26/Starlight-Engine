#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <shellapi.h>
#include <string>
#include <vector>
extern "C" __declspec(dllimport) void *sl_create(HWND, int, int);
extern "C" __declspec(dllimport) int sl_scene(void *, const unsigned char *,
                                              int);
extern "C" __declspec(dllimport) int sl_frame(void *, double, float);
extern "C" __declspec(dllimport) void sl_camera(void *, float, float, float,
                                                float, float, float);
extern "C" __declspec(dllimport) void sl_destroy(void *);
extern "C" __declspec(dllimport) void sl_shot(void *, const wchar_t *);
extern "C" __declspec(dllimport) double sl_gpu_ms(void *);
extern "C" __declspec(dllimport) const char *sl_error();
extern "C" __declspec(dllimport) int sl_render_width(void *);
extern "C" __declspec(dllimport) void sl_rt(void *, int, int);
extern "C" __declspec(dllimport) void sl_freeze(void *, int);
extern "C" __declspec(dllimport) int sl_rt_supported(void *);
extern "C" __declspec(dllimport) int sl_rt_active(void *);
extern "C" __declspec(dllimport) int sl_rt_samples(void *);
extern "C" __declspec(dllimport) double sl_ray_ms(void *);
extern "C" __declspec(dllimport) double sl_as_ms(void *);
extern "C" __declspec(dllimport) unsigned long long sl_blas_builds(void *);
extern "C" __declspec(dllimport) unsigned long long sl_tlas_refits(void *);
extern "C" __declspec(dllimport) double sl_denoise_ms(void *);
extern "C" __declspec(dllimport) int sl_rr_active(void *);
extern "C" __declspec(dllimport) const char *sl_rr_status(void *);
extern "C" __declspec(dllimport) void sl_denoiser(void *, int);

#include "../../deep/src/json.hpp"
using json = nlohmann::json;
static bool interactive = false, userStarted = false, userClosed = false;
static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  if (interactive && (msg == WM_LBUTTONUP || (msg == WM_KEYDOWN && w == VK_SPACE))) {
    userStarted = true;
    return 0;
  }
  if (msg == WM_CLOSE) {
    userClosed = true;
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, msg, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if (argc < 6 || argc > 10) {
    LocalFree(argv);
    return 2;
  }
  std::wstring packet = argv[1], report = argv[2], shot = argv[3],
               mode = argc > 7 ? argv[7] : L"focused";
  int width = _wtoi(argv[4]), height = _wtoi(argv[5]),
      tier = argc > 6 ? _wtoi(argv[6]) : 0,
      bounces = argc > 8 ? _wtoi(argv[8]) : 3,
      denoiser = argc > 9 ? _wtoi(argv[9]) : 0;
  LocalFree(argv);
  interactive = mode == L"interactive";
  const bool fullscreen = mode == L"fullscreen-auto";
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  wchar_t durationSetting[16]{};
  GetEnvironmentVariableW(L"STARLIGHT_BENCH_SECONDS", durationSetting, 16);
  const double measurementSeconds = durationSetting[0] ? std::clamp(_wtoi(durationSetting), 1, 60) : 5;
  auto dll = GetModuleHandleW(L"Starlight.Viewport.dll");
  auto benchmark = reinterpret_cast<void (*)(void *, int)>(
      GetProcAddress(dll, "sl_benchmark"));
  auto drain = reinterpret_cast<void (*)(void *)>(
      GetProcAddress(dll, "sl_benchmark_flush"));
  auto contract = reinterpret_cast<int (*)()>(GetProcAddress(dll, "sl_benchmark_contract"));
  const int benchmarkContract = contract ? contract() : 0;
  using QueryInt = int (*)(void*);
  auto renderHeight = reinterpret_cast<QueryInt>(GetProcAddress(dll, "sl_render_height"));
  auto debugActive = reinterpret_cast<QueryInt>(GetProcAddress(dll, "sl_debug_active"));
  auto nativeContract = reinterpret_cast<QueryInt>(GetProcAddress(dll, "sl_native_contract"));
  bool requested = mode == L"--disable-background-throttle",
       offscreen = requested && benchmark && drain && benchmarkContract >= 2;
  // The immutable legacy DLL has no offscreen ABI. Run it focused and label the
  // different presentation contract, rather than pretending it was uncapped.
  bool headless = offscreen || mode == L"headless";
  HWND hwnd = nullptr;
  void *context = nullptr;
  auto finish = [&](json data, int code) {
    data["success"] = code == 0;
    data["exitCode"] = code;
    data["userStarted"] = userStarted;
    data["interactive"] = interactive;
    data["fullscreen"] = fullscreen;
    data["automaticStart"] = !interactive;
    if (code == 9 || code == 10 || code == 11) data["failureCategory"] = "environment-precondition";
    if (hwnd) {
      DWORD foregroundPid=0;
      GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
      LASTINPUTINFO input{sizeof(input)}; GetLastInputInfo(&input);
      data["windowEvidence"]={{"visible",IsWindowVisible(hwnd)!=0},{"minimized",IsIconic(hwnd)!=0},
        {"foregroundOwnerPid",foregroundPid},{"testPid",GetCurrentProcessId()},
        {"lastInputAgeMs",DWORD(GetTickCount()-input.dwTime)}};
    }
    std::ofstream f(report);
    f << data.dump(2);
    f.close();
    if (interactive && hwnd && !userClosed) {
      auto title = code == 0 ? L"Starlight Gate 0 — COMPLETED; report saved. Close this window when ready."
                            : L"Starlight Gate 0 — TEST FAILED; report saved. Close this window when ready.";
      SetWindowTextW(hwnd, title);
      MSG message{};
      while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    if (context)
      sl_destroy(context);
    if (hwnd)
      DestroyWindow(hwnd);
    return code;
  };
  try {
    std::ifstream input(packet, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                     {});
    if (bytes.empty())
      return finish({{"error", "Empty packet"}}, 3);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = L"StarlightStandaloneCheck";
    RegisterClassW(&wc);
    RECT rect{0, 0, width, height};
    const DWORD style = fullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    if (fullscreen && (width != GetSystemMetrics(SM_CXSCREEN) || height != GetSystemMetrics(SM_CYSCREEN) || width < 2560 || height < 1440))
      return finish({{"error", "Full-screen audit requires a native primary display of at least 2560x1440"}}, 11);
    AdjustWindowRect(&rect, style, FALSE);
    hwnd =
        CreateWindowW(wc.lpszClassName, L"Starlight — Gate 0 visible rendering check",
                      style, 0, 0, rect.right, rect.bottom,
                      nullptr, nullptr, instance, nullptr);
    if (!hwnd)
      return finish({{"error", "Window creation failed"}}, 4);
    if (!headless) {
      ShowWindow(hwnd, SW_SHOW);
      // A launcher may supply SW_HIDE for the first ShowWindow call.
      // The explicitly visible test must not inherit that hidden state.
      ShowWindow(hwnd, SW_SHOW);
      HWND previous = GetForegroundWindow();
      DWORD other = previous ? GetWindowThreadProcessId(previous, nullptr) : 0;
      DWORD current = GetCurrentThreadId();
      bool attached = other && other != current && AttachThreadInput(current, other, TRUE);
      BringWindowToTop(hwnd);
      SetForegroundWindow(hwnd);
      SetActiveWindow(hwnd);
      SetFocus(hwnd);
      if (attached) AttachThreadInput(current, other, FALSE);
    }
    context = sl_create(hwnd, width, height);
    if (!context)
      return finish({{"error", sl_error()}}, 4);
    if (offscreen)
      benchmark(context, 1);
    if (!sl_scene(context, bytes.data(), int(bytes.size())))
      return finish({{"error", sl_error()}}, 5);
    sl_camera(context, 0, 9.36f, 13, 0, 0, 0);
    sl_rt(context, tier, bounces);
    sl_freeze(context, 1);
    sl_denoiser(context, denoiser);
    if (interactive)
      SetWindowTextW(hwnd, L"Starlight Gate 0 — READY: click the scene or press Space to start the visible test");
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now(), measure = start, focusSince = start;
    bool measuring = false, captured = false;
    long frames = 0, foregroundFrames = 0;
    std::vector<double> times;
    double gpu = 0, ray = 0, as = 0, noise = 0;
    MSG msg{};
    while (true) {
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT)
          return finish({{"error", "Benchmark closed"}}, 6);
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      auto now = Clock::now();
      // Interactive verification does not start or time out before the user
      // has actually seen and selected this window.
      if (interactive && !userStarted) { start = now; focusSince = now; }
      if (std::chrono::duration<double>(now - start).count() > measurementSeconds + 40)
        return finish({{"error", "Stable foreground focus unavailable"}}, 9);
      if (!headless && GetForegroundWindow() != hwnd) {
        if (measuring)
          return finish(
              {{"error", "Focus lost during measured presentation"}},
              10);
        focusSince = now;
      }
      double warm = std::chrono::duration<double>(now - focusSince).count();
      if (warm > 1 && !captured) {
        sl_shot(context, shot.c_str());
        captured = true;
      }
      if (warm > 2 && !measuring) {
        if (drain)
          drain(context);
        measure = Clock::now();
        measuring = true;
      }
      if (measuring &&
          std::chrono::duration<double>(now - measure).count() >= measurementSeconds)
        break;
      auto frameStart = Clock::now();
      if (!sl_frame(context, 0, 13))
        return finish({{"error", sl_error()}}, 7);
      if (measuring) {
        ++frames;
        if (GetForegroundWindow() == hwnd)
          ++foregroundFrames;
        times.push_back(
            std::chrono::duration<double, std::milli>(Clock::now() - frameStart)
                .count());
        gpu += sl_gpu_ms(context);
        ray += sl_ray_ms(context);
        as += sl_as_ms(context);
        noise += sl_denoise_ms(context);
        if (!headless && frames % 60 == 0) {
          auto seconds = std::chrono::duration<double>(Clock::now() - measure).count();
          auto title = L"Starlight Gate 0 — live native rendering | " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
            L" | " + std::to_wstring(static_cast<int>(seconds)) + L"/" + std::to_wstring(static_cast<int>(measurementSeconds)) +
            L" sec | GPU " + std::to_wstring(sl_gpu_ms(context)) + L" ms | frames " + std::to_wstring(frames);
          SetWindowTextW(hwnd, title.c_str());
        }
      }
    }
    // Include completion of all submitted GPU work in throughput, never just
    // CPU dispatch.
    if (drain)
      drain(context);
    double duration =
        std::chrono::duration<double>(Clock::now() - measure).count();
    std::sort(times.begin(), times.end());
    auto percentile = [&](double p) {
      return times.empty()
                 ? 0
                 : times[std::min(times.size() - 1, size_t(p * times.size()))];
    };
    return finish({{"fps", frames / duration},
                   {"frames", frames},
                   {"durationSeconds", duration},
                   {"width", width},
                   {"height", height},
                   {"renderWidth", sl_render_width(context)},
                   {"renderHeight", renderHeight ? json(renderHeight(context)) : json(nullptr)},
                   {"nativeContractVerified", nativeContract ? json(nativeContract(context) != 0) : json(nullptr)},
                   {"debugLayerActive", debugActive ? json(debugActive(context) != 0) : json(nullptr)},
                   {"gpuMs", gpu / std::max(1L, frames)},
                   {"p50Ms", percentile(.5)},
                   {"p95Ms", percentile(.95)},
                   {"p99Ms", percentile(.99)},
                   {"upscaling", false},
                   {"frameGeneration", false},
                   {"foreground", GetForegroundWindow() == hwnd},
                   {"foregroundFrames", foregroundFrames},
                   {"headless", headless},
                   {"backgroundThrottleBypassed", offscreen},
                   {"benchmarkContract", benchmarkContract},
                   {"debugLayerRequested", GetEnvironmentVariableW(L"STARLIGHT_D3D_DEBUG", nullptr, 0) != 0},
                   {"presentationMode", offscreen ? "offscreen-no-present"
                                                  : "focused-present"},
                   {"legacyFrameCap", offscreen ? 0 : 500},
                   {"requestedTier", tier},
                   {"activeTier", sl_rt_active(context)},
                   {"rtSupported", sl_rt_supported(context) != 0},
                   {"rayDispatchMs", ray / std::max(1L, frames)},
                   {"tlasMs", as / std::max(1L, frames)},
                   {"denoiseMs", noise / std::max(1L, frames)},
                   {"blasBuilds", sl_blas_builds(context)},
                   {"tlasRefits", sl_tlas_refits(context)},
                   {"samples", sl_rt_samples(context)},
                   {"bounces", bounces},
                   {"rrActive", sl_rr_active(context) != 0},
                   {"rrStatus", sl_rr_status(context)},
                   {"denoiserRequested", denoiser},
                   {"denoiserName",
                    denoiser == 0 ? "native-wavelet" : "ray-reconstruction"},
                   {"frozenSnapshot", true}},
                  0);
  } catch (const std::exception &e) {
    return finish({{"error", e.what()}}, 11);
  }
}
