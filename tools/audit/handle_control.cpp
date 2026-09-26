// Small diagnostic control: no engine, renderer, GPU, Jolt, or scene lifecycle.
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>
#include <vector>
#include "../../deep/src/json.hpp"
int wmain(int argc, wchar_t **argv) {
  if (argc != 3) return 2;
  const bool async = std::wstring_view(argv[2]) == L"async";
  auto sample = [] {
    DWORD handles = 0;
    PROCESS_MEMORY_COUNTERS_EX m{}; m.cb = sizeof(m);
    if (!GetProcessHandleCount(GetCurrentProcess(), &handles) ||
        !GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&m), sizeof(m)))
      throw std::runtime_error("Cannot measure own process");
    return nlohmann::json{{"handles",handles},{"privateBytes",m.PrivateUsage},{"tickMs",GetTickCount64()}};
  };
  nlohmann::json report{{"diagnosticOnly",true},{"async",async},{"before",sample()}};
  if (async) {
    std::vector<std::future<void>> tasks;
    for (int i = 0; i < 36; ++i)
      tasks.push_back(std::async(std::launch::async, [] { Sleep(10); }));
    for (auto &task : tasks) task.get();
  }
  report["samples"] = nlohmann::json::array();
  for (int i = 0; i < 90; ++i) { report["samples"].push_back(sample()); Sleep(500); }
  report["final"] = sample();
  std::ofstream(std::filesystem::path(argv[1])) << report.dump(2);
  return 0; // An observation, not a leak-free acceptance test.
}
