// Native, detached audit controller. All audit products belong to a fresh run
// directory. A failing child is evidence, not permission to terminate an
// existing editor.
#include "commands.hpp"
#include "render_scene.hpp"
#include "render_contract.hpp"
#include <atomic>
#include <chrono>
#include <commctrl.h>
#include <dbghelp.h>
#include <dxgi1_6.h>
#include <fstream>
#include <intrin.h>
#include <iomanip>
#include <mutex>
#include <psapi.h>
#include <random>
#include <sstream>
#include <thread>
#include <windows.h>
#include <wrl/client.h>
#include "audit_theme.hpp"

using namespace starlight::editor;
using Clock = std::chrono::steady_clock;
using Microsoft::WRL::ComPtr;
namespace {
fs::path root, output, binaries, self, legacy;
std::atomic<bool> cancel{false}, finished{false};
bool batchMode = false;
bool gate0Only = false;
bool gate0Components = false;
bool visibleOnly = false;
json evidenceChecks = json::array();
std::atomic<int> percent{0};
std::mutex stateMutex;
std::wstring stage = L"Starting";
HWND window{}, bar{}, label{}, amount{};
std::jthread coordinator;
json runs = json::array(), findings = json::array(), inventory, cpu;
unsigned workers = std::max(1u, std::thread::hardware_concurrency());
double milliseconds(Clock::time_point t) {
  return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
void save(const fs::path &p, const json &j) { atomic_write(p, j.dump(2)); }
void status(int p, std::wstring s) {
  std::lock_guard lock(stateMutex);
  percent = p;
  stage = std::move(s);
  if(batchMode) save(output / L"progress.json", {{"percent",p},{"stage",utf8(stage)}});
}
std::wstring quote(std::wstring_view s) {
  std::wstring r = L"\"";
  size_t slashes = 0;
  for (wchar_t c : s) {
    if (c == L'\\') {
      ++slashes;
      continue;
    }
    r.append(slashes * (c == L'"' ? 2 : 1), L'\\');
    slashes = 0;
    if (c == L'"')
      r += L'\\';
    r += c;
  }
  r.append(slashes * 2, L'\\');
  return r + L'"';
}
json usage(HANDLE p) {
  PROCESS_MEMORY_COUNTERS_EX m{};
  m.cb = sizeof(m);
  DWORD h = 0;
  bool ok =
      GetProcessMemoryInfo(p, reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&m),
                           sizeof(m)) != 0;
  bool handles = GetProcessHandleCount(p, &h) != 0;
  return {{"memoryValid", ok},
          {"handlesValid", handles},
          {"handles", h},
          {"privateBytes", m.PrivateUsage},
          {"workingSetBytes", m.WorkingSetSize},
          {"peakWorkingSetBytes", m.PeakWorkingSetSize}};
}
LONG WINAPI crash(EXCEPTION_POINTERS *pointers) {
  auto path = output / L"audit-controller-crash.dmp";
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), pointers, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
                      MiniDumpWithThreadInfo, &info, nullptr, nullptr);
    CloseHandle(f);
  }
  return EXCEPTION_EXECUTE_HANDLER;
}
void finding(std::string kind, std::string file, std::string trigger,
             std::string recommendation) {
  findings.push_back({{"classification", kind},
                      {"file", file},
                      {"trigger", trigger},
                      {"recommendation", recommendation}});
}

// Each invocation owns a job. On cancellation/timeout only its own process tree
// is stopped.
json child(std::string name, const fs::path &exe,
           const std::vector<std::wstring> &args, int seconds = 180) {
  json record = {
      {"name", name}, {"exe", utf8(exe.wstring())}, {"args", args.size()}};
  if (cancel) {
    record["status"] = "cancelled";
    runs.push_back(record);
    return record;
  }
  if (!fs::exists(exe)) {
    record["status"] = "unavailable";
    runs.push_back(record);
    return record;
  }
  fs::path dir = output / wide(name);
  fs::create_directories(dir);
  std::wstring command = quote(exe.wstring());
  for (const auto &a : args)
    command += L" " + quote(a);
  record["command"] = utf8(command);
  SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
  Handle log(CreateFileW((dir / L"stdout.log").c_str(), GENERIC_WRITE,
                         FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
  Handle input(CreateFileW(L"NUL", GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           OPEN_EXISTING, 0, nullptr));
  Handle job(CreateJobObjectW(nullptr, nullptr));
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
  limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!log || !input || !job ||
      !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                               &limit, sizeof(limit)))
    throw std::runtime_error("Cannot establish isolated child job/log handles");
  STARTUPINFOEXW si{};
  si.StartupInfo.cb = sizeof(si);
  si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  const bool visibleChild = name == "visible-editor" || name == "visible-presentation";
  if (visibleChild) {
    si.StartupInfo.dwFlags |= STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_SHOWNORMAL;
  }
  si.StartupInfo.hStdOutput = si.StartupInfo.hStdError = log.value;
  si.StartupInfo.hStdInput = input.value;
  SIZE_T bytes = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
  std::vector<unsigned char> attrs(bytes);
  si.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
  if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &bytes))
    throw std::runtime_error("Process attribute initialization failed");
  HANDLE inherited[] = {log.value, input.value};
  bool configured =
      UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                sizeof(inherited), nullptr, nullptr) != 0;
  PROCESS_INFORMATION pi{};
  auto start = Clock::now();
  bool created =
      configured &&
      CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, TRUE,
                     CREATE_NO_WINDOW | CREATE_SUSPENDED |
                         EXTENDED_STARTUPINFO_PRESENT,
                     nullptr, dir.c_str(), &si.StartupInfo, &pi);
  DWORD launchError = GetLastError();
  DeleteProcThreadAttributeList(si.lpAttributeList);
  if (!created) {
    record["status"] = "launch-failed";
    record["win32Error"] = launchError;
    runs.push_back(record);
    return record;
  }
  Handle process(pi.hProcess), thread(pi.hThread);
  if (!AssignProcessToJobObject(job.value, process.value)) {
    TerminateProcess(process.value, 125);
    throw std::runtime_error("Cannot isolate child in job");
  }
  if (visibleChild) {
    // Only requested visible stages may activate the audit and receive foreground rights.
    ShowWindow(window, SW_RESTORE);
    bool activated = SetForegroundWindow(window) != FALSE;
    bool granted = SendMessageW(window, WM_APP+17, pi.dwProcessId, 0) != FALSE;
    if (!granted) {
      status(percent, L"Click this audit window to start the visible checks — waiting for Windows foreground permission");
      FLASHWINFO flash{sizeof(flash),window,FLASHW_TRAY,3,0};
      FlashWindowEx(&flash);
      const auto activationDeadline = GetTickCount64()+60000;
      while (!cancel && GetTickCount64()<activationDeadline) {
        DWORD owner=0;
        GetWindowThreadProcessId(GetForegroundWindow(),&owner);
        if (owner==GetCurrentProcessId()) {
          activated=true;
          granted=SendMessageW(window,WM_APP+17,pi.dwProcessId,0)!=FALSE;
          if(granted) break;
        }
        // Bounded audit-only activation wait; never part of engine frame pacing.
        Sleep(25);
      }
    }
    record["foregroundHandoff"] = {{"auditActivated",activated},{"childGranted",granted}};
  }
  ResumeThread(thread.value);
  std::ofstream samples(dir / L"process-samples.csv");
  samples << "elapsed_ms,handles,private_bytes,working_set_bytes\n";
  json first, last;
  uint64_t peakPrivate = 0, peakWorking = 0, peakHandles = 0;
  size_t count = 0;
  bool timeout = false;
  while (WaitForSingleObject(process.value, 50) == WAIT_TIMEOUT) {
    auto u = usage(process.value);
    if (first.is_null())
      first = u;
    last = u;
    ++count;
    peakPrivate = std::max(peakPrivate, u["privateBytes"].get<uint64_t>());
    peakWorking = std::max(peakWorking, u["workingSetBytes"].get<uint64_t>());
    peakHandles = std::max(peakHandles, u["handles"].get<uint64_t>());
    samples << milliseconds(start) << ',' << u["handles"] << ','
            << u["privateBytes"] << ',' << u["workingSetBytes"] << '\n';
    if (cancel || milliseconds(start) > seconds * 1000.) {
      timeout = !cancel;
      TerminateJobObject(job.value, 124);
      WaitForSingleObject(process.value, 5000);
      break;
    }
  }
  DWORD code = 0;
  GetExitCodeProcess(process.value, &code);
  FILETIME ct{}, et{}, kt{}, ut{};
  GetProcessTimes(process.value, &ct, &et, &kt, &ut);
  auto ticks = [](FILETIME f) {
    return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime;
  };
  record.update({{"status", cancel      ? "cancelled"
                            : timeout   ? "timeout"
                            : code == 0 ? "pass"
                                        : "fail"},
                 {"exitCode", code},
                 {"wallMs", milliseconds(start)},
                 {"cpuMs", (ticks(kt) + ticks(ut)) / 10000.},
                 {"sampleCount", count},
                 {"peakPrivateBytes", peakPrivate},
                 {"peakWorkingSetBytes", peakWorking},
                 {"peakHandles", peakHandles},
                 {"firstSample", first},
                 {"lastSample", last},
                 {"outputDirectory", utf8(dir.wstring())}});
  if (!count) {
    record["peakPrivateBytes"] = nullptr;
    record["peakWorkingSetBytes"] = nullptr;
    record["peakHandles"] = nullptr;
  }
  save(dir / L"process.json", record);
  runs.push_back(record);
  save(output / L"runs.partial.json", runs);
  if (code || timeout)
    finding(
        "reproduced test failure", utf8(exe.wstring()),
        utf8(command) + "; exit=" + std::to_string(code),
        "Inspect this run's stdout and structured result; reproduce the exact "
        "invocation in isolation before attributing a source defect.");
  return record;
}

json hardware() {
  int regs[4];
  char brand[49]{};
  __cpuid(regs, 0x80000000);
  if (static_cast<unsigned>(regs[0]) >= 0x80000004)
    for (int i = 0; i < 3; ++i) {
      __cpuid(regs, 0x80000002 + i);
      memcpy(brand + i * 16, regs, 16);
    }
  MEMORYSTATUSEX mem{sizeof(mem)};
  GlobalMemoryStatusEx(&mem);
  json h = {
      {"cpu", brand},
      {"logicalProcessors", GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)},
      {"workers", workers},
      {"physicalMemoryBytes", mem.ullTotalPhys},
      {"targetCPUDetected",
       std::string(brand).find("9950X3D") != std::string::npos},
      {"adapters", json::array()},
      {"targetGPUDetected", false}};
  ComPtr<IDXGIFactory6> factory;
  if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    for (UINT i = 0;; ++i) {
      ComPtr<IDXGIAdapter1> adapter;
      if (factory->EnumAdapterByGpuPreference(
              i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
              IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
        break;
      if (!adapter)
        break;
      DXGI_ADAPTER_DESC1 d{};
      adapter->GetDesc1(&d);
      json a = {{"name", utf8(d.Description)},
                {"vendorId", d.VendorId},
                {"deviceId", d.DeviceId},
                {"dedicatedBytes", d.DedicatedVideoMemory},
                {"software", bool(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)}};
      ComPtr<IDXGIAdapter3> modern;
      if (SUCCEEDED(adapter.As(&modern))) {
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        if (SUCCEEDED(modern->QueryVideoMemoryInfo(
                0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
          a["controllerVideoMemory"] = {{"budget", info.Budget},
                                        {"usage", info.CurrentUsage}};
      }
      if (std::wstring(d.Description).find(L"5090") != std::wstring::npos)
        h["targetGPUDetected"] = true;
      h["adapters"].push_back(a);
    }
  h["gpuSelection"] = "Existing renderer selects its hardware adapter; "
                      "high-performance adapter inventory is recorded. No "
                      "clock, priority, or overclock changes.";
  return h;
}

void scan() {
  std::vector<fs::path> files;
  json skipped = json::array();
  for (fs::recursive_directory_iterator
           it(root, fs::directory_options::skip_permission_denied),
       end;
       it != end; ++it) {
    if (cancel)
      return;
    auto name = it->path().filename().wstring();
    if (it->is_directory()) {
      if (name == L".git" || name == L"third_party" ||
          name == L"node_modules" || name == L"audit-runs" ||
          (GetFileAttributesW(it->path().c_str()) &
           FILE_ATTRIBUTE_REPARSE_POINT))
        it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file())
      continue;
    auto ext = it->path().extension().wstring();
    if (ext != L".cpp" && ext != L".h" && ext != L".hpp" && ext != L".inl" &&
        ext != L".hlsl" && ext != L".cs" && ext != L".json" && ext != L".md" &&
        ext != L".log" && ext != L".csv" && ext != L".txt")
      continue;
    if (it->file_size() > 16 * 1024 * 1024) {
      skipped.push_back(utf8(it->path().wstring()));
      continue;
    }
    files.push_back(it->path());
  }
  std::atomic<size_t> next = 0, done = 0;
  std::mutex mutex;
  json baselines = json::array(), sites = json::array(),
       manifest = json::array();
  std::vector<std::jthread> pool;
  for (unsigned t = 0; t < workers; ++t)
    pool.emplace_back([&] {
      while (!cancel) {
        auto n = next.fetch_add(1);
        if (n >= files.size())
          break;
        try {
          auto text = read_file(files[n]);
          auto relative = utf8(fs::relative(files[n], root).wstring());
          auto ext = files[n].extension().wstring();
          bool source = ext == L".cpp" || ext == L".h" || ext == L".hpp" ||
                        ext == L".inl" || ext == L".hlsl" || ext == L".cs";
          json metrics = json::array(), candidates = json::array();
          std::istringstream lines(text);
          std::string line;
          int lineNo = 0;
          while (std::getline(lines, line)) {
            ++lineNo;
            if (!source && (line.find("fps") != std::string::npos ||
                            line.find("FPS") != std::string::npos ||
                            line.find("p95Ms") != std::string::npos ||
                            line.find("gpuMs") != std::string::npos ||
                            line.find("privateBytes") != std::string::npos ||
                            line.find("Handles") != std::string::npos))
              metrics.push_back(
                  {{"line", lineNo}, {"text", line.substr(0, 1200)}});
            if (source &&
                (line.find(".detach()") != std::string::npos ||
                 line.find("TerminateThread(") != std::string::npos ||
                 line.find("WaitForSingleObject(") != std::string::npos ||
                 line.find("ResourceBarrier(") != std::string::npos ||
                 line.find("CreateThread(") != std::string::npos ||
                 line.find("catch (...)") != std::string::npos))
              candidates.push_back(
                  {{"line", lineNo}, {"text", line.substr(0, 1000)}});
          }
          std::lock_guard lock(mutex);
          manifest.push_back({{"file", relative},
                              {"bytes", text.size()},
                              {"sha256", hash_file(files[n])}});
          if (!metrics.empty())
            baselines.push_back(
                {{"file", relative},
                 {"metrics", metrics},
                 {"comparability", "unverified historical context"}});
          if (!candidates.empty())
            sites.push_back(
                {{"file", relative},
                 {"classification", "review site, NOT a confirmed bug"},
                 {"sites", candidates}});
        } catch (const std::exception &e) {
          std::lock_guard lock(mutex);
          skipped.push_back(
              {{"file", utf8(files[n].wstring())}, {"error", e.what()}});
        }
        auto d = ++done;
        status(3 + int(17 * d / std::max<size_t>(1, files.size())),
               L"Scanning source and historical measurements");
      }
    });
  pool.clear();
  save(output / L"source-manifest.json", manifest);
  save(output / L"legacy-baselines.json", baselines);
  save(output / L"source-review-sites.json", sites);
  save(output / L"scan-skipped.json", skipped);
  inventory = {{"files", files.size()},
               {"baselineFiles", baselines.size()},
               {"reviewFiles", sites.size()},
               {"skipped", skipped.size()},
               {"scope",
                "Repository first-party text <=16 MiB; no symlink traversal, "
                "vendored third_party, .git, or this tool's outputs. Review "
                "sites are not race/leak proofs."}};
}

void stress() {
  status(
      21,
      L"Parallel native serialization, command dispatch and allocation stress");
  auto before = usage(GetCurrentProcess());
  auto start = Clock::now();
  std::vector<json> results(workers);
  std::vector<std::jthread> pool;
  for (unsigned t = 0; t < workers; ++t)
    pool.emplace_back([&, t] {
      try {
        HANDLE heap = HeapCreate(0, 0, 0);
        if (!heap)
          throw std::bad_alloc();
        struct HeapOwner {
          HANDLE h;
          ~HeapOwner() { HeapDestroy(h); }
        } owner{heap};
        SceneData scene;
        for (int i = 0; i < 128; ++i) {
          Character c;
          c.Shape = "cube";
          c.X = i % 16;
          c.Z = i / 16;
          scene.Characters.push_back(c);
        }
        std::string encoded = scene.encode();
        std::mt19937 rng(7301 + t);
        uint64_t accepted = 0, rejected = 0, allocations = 0,
                 allocatedBytes = 0, checksum = 0, valid = 0;
        double allocationMs = 0;
        std::vector<double> dispatch, parse;
        auto begin = Clock::now();
        for (int n = 0; n < 200 && !cancel; ++n) {
          auto tick = Clock::now();
          auto decoded = SceneData::decode(encoded);
          decoded.validate();
          parse.push_back(milliseconds(tick));
          ++valid;
          auto mutated = encoded;
          for (int k = 0; k < 4; ++k)
            mutated[rng() % mutated.size()] = char(rng() % 128);
          try {
            auto bad = SceneData::decode(mutated);
            bad.validate();
            ++accepted;
          } catch (const std::exception &) {
            ++rejected;
          }
          tick = Clock::now();
          auto description = describe_commands();
          checksum += description.size();
          dispatch.push_back(milliseconds(tick));
          tick = Clock::now();
          for (int k = 0; k < 128; ++k) {
            size_t size = 64 + rng() % 16384;
            auto p = static_cast<unsigned char *>(HeapAlloc(heap, 0, size));
            if (!p)
              throw std::bad_alloc();
            memset(p, int(rng()), size);
            checksum += p[size - 1];
            if (!HeapFree(heap, 0, p))
              throw std::runtime_error("HeapFree rejected allocation");
            ++allocations;
            allocatedBytes += size;
          }
          allocationMs += milliseconds(tick);
        }
        auto percentile = [](std::vector<double> v, double p) {
          std::sort(v.begin(), v.end());
          return v.empty() ? 0.
                           : v[std::min(v.size() - 1, size_t(v.size() * p))];
        };
        results[t] = {{"worker", t},
                      {"validRoundTrips", valid},
                      {"mutationsRejected", rejected},
                      {"mutationsAccepted", accepted},
                      {"parseP50Ms", percentile(parse, .5)},
                      {"parseP95Ms", percentile(parse, .95)},
                      {"describeDispatchP50Ms", percentile(dispatch, .5)},
                      {"describeDispatchP95Ms", percentile(dispatch, .95)},
                      {"allocations", allocations},
                      {"allocatedBytes", allocatedBytes},
                      {"allocationAndTouchMs", allocationMs},
                      {"heapValid", HeapValidate(heap, 0, nullptr) != 0},
                      {"wallMs", milliseconds(begin)},
                      {"checksum", checksum}};
      } catch (const std::exception &e) {
        results[t] = {{"worker", t}, {"error", e.what()}};
      }
    });
  pool.clear();
  cpu = {
      {"workers", results},
      {"wallMs", milliseconds(start)},
      {"before", before},
      {"after", usage(GetCurrentProcess())},
      {"allocationScope",
       "Counted private Win32 heap microbenchmark, including memory touching; "
       "NOT instrumentation of all renderer or managed allocations."},
      {"raceScope", "Concurrent independent schema/describe workloads with "
                    "deterministic mutations; not a dynamic race detector."}};
  save(output / L"cpu-stress.json", cpu);
  for (const auto &r : results)
    if (r.contains("error") || !r.value("heapValid", false))
      finding("reproduced stress failure",
              "editor/cpp/scene_json.cpp; commands.cpp; tools/audit/audit.cpp",
              r.dump(),
              "Reproduce this worker seed (7301 + worker); inspect schema "
              "validation/ownership. Heap failures can originate in the "
              "harness as well.");
}

json readResult(const fs::path &file) {
  try {
    return json::parse(read_file(file));
  } catch (const std::exception &e) {
    return {{"unavailable", e.what()}};
  }
}
std::string cell(const json &j, const char *key) {
  return j.contains(key) ? j[key].dump() : "unavailable";
}
void report(const json &hw, const json &comparisons) {
  save(output / L"runs.json", runs);
  save(output / L"findings.json", findings);
  save(output / L"comparisons.json", comparisons);
  std::ostringstream md;
  md << "# Starlight native audit findings\n\n"
     << (cancel ? "Cancelled; partial results"
                : "Completed execution; test failures and coverage limits "
                  "remain explicit")
     << ". This report does not certify the absence of leaks, races, or "
        "rendering defects.\n\n"
     << "## Hardware and run identity\n\nRepository: `" << utf8(root.wstring())
     << "`\n\nOutput: `" << utf8(output.wstring()) << "`\n\n"
     << "```json\n"
     << hw.dump(2) << "\n```\n\n## Coverage\n\n"
     << inventory.dump(2) << "\n\n"
     << "All logical workers execute independent CPU workloads concurrently. "
        "GPU, editor lifetime and session passes run sequentially. "
     << "Process CSVs sample every 50 ms; short processes may have no samples. "
        "Working set is resident process RAM, private bytes are committed "
        "private memory. "
     << "First/last child samples include startup/shutdown and cannot alone "
        "establish a leak. Lifecycle assertions inside native checks are the "
        "stronger evidence. "
     << "Per-process GPU residency is not collected for children; adapter "
        "inventory is not renderer VRAM usage.\n\n"
     << "## Native validation and process measurements\n\n| Pass | Result | "
        "Wall ms | CPU ms | Peak handles | Peak private bytes | Peak resident "
        "bytes |\n|---|---|---:|---:|---:|---:|---:|\n";
  for (const auto &r : runs)
    md << "| " << r.value("name", "") << " | " << r.value("status", "") << " | "
       << cell(r, "wallMs") << " | " << cell(r, "cpuMs") << " | "
       << (r.value("sampleCount", 0u) ? cell(r, "peakHandles") : "unavailable")
       << " | "
       << (r.value("sampleCount", 0u) ? cell(r, "peakPrivateBytes")
                                      : "unavailable")
       << " | "
       << (r.value("sampleCount", 0u) ? cell(r, "peakWorkingSetBytes")
                                      : "unavailable")
       << " |\n";
  md << "\n## Legacy vs native editor command latency\n\nIdentical "
        "`--describe` and `--inspect` inputs, three paired repetitions. Times "
        "include process/runtime startup, file I/O and shutdown; these are not "
        "UI event latency. Order alternates.\n\n"
     << "| Pair | Legacy wall ms | Native wall ms | Legacy status | Native "
        "status |\n|---|---:|---:|---|---|\n";
  for (const auto &p : comparisons["commands"])
    md << "| " << p["name"].get<std::string>() << " | "
       << cell(p["old"], "wallMs") << " | " << cell(p["new"], "wallMs") << " | "
       << cell(p["old"], "status") << " | " << cell(p["new"], "status")
       << " |\n";
  md << "\n## Matched renderer throughput and stability\n\nSame current native "
        "harness executable, byte-identical packet, 3840 x 2160, fixed camera, "
        "frozen animation, 3 bounces, native denoiser, no upscaling/frame "
        "generation. "
     << "The legacy arm loads an isolated copy of the archived viewport DLL. "
        "This compares renderer revisions, not WPF vs Win32 UI composition. "
     << "New runs bypass presentation and pacing; legacy runs require "
        "foreground focus. "
        "FPS ratios across these modes are invalid. Legacy retains its 500 FPS "
        "cap; only new offscreen runs are uncapped. Comparable means "
        "native-denoiser GPU workload settings match; fpsComparable "
        "independently checks presentation equivalence. "
     << "Unsupported tiers and DLL ABI failures are recorded rather than "
        "counted as speedups. GPU timings are the existing harness's delayed "
        "timestamp averages.\n\n"
     << "| Tier/repeat | Old FPS | New FPS | Old GPU ms | New GPU ms | Old p50 "
        "ms | New p50 ms | Old p95 ms | New p95 ms | GPU settings match | FPS "
        "comparable |\n"
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|\n";
  for (const auto &p : comparisons["renderers"])
    md << "| " << p["name"].get<std::string>() << " | " << cell(p["old"], "fps")
       << " | " << cell(p["new"], "fps") << " | " << cell(p["old"], "gpuMs")
       << " | " << cell(p["new"], "gpuMs") << " | " << cell(p["old"], "p50Ms")
       << " | " << cell(p["new"], "p50Ms") << " | " << cell(p["old"], "p95Ms")
       << " | " << cell(p["new"], "p95Ms") << " | " << p["comparable"] << " | "
       << p["fpsComparable"] << " |\n";
  md << "\nHistorical metrics are preserved with original paths and line "
        "numbers in `legacy-baselines.json`. They are not combined into "
        "speedup ratios because their scenes, focus, duration and binaries may "
        "differ. "
     << "SHA-256 identities are recorded in `source-manifest.json` and "
        "`binary-identities.json`.\n\n## Failures and investigation "
        "recommendations\n\n";
  if (findings.empty())
    md << "No failure was detected by completed passes. This is bounded "
          "evidence, not a universal bug-free result.\n\n";
  for (const auto &f : findings)
    md << "### " << f["classification"].get<std::string>() << "\n\nLocation: `"
       << f["file"].get<std::string>()
       << "`\n\nTrigger/evidence: " << f["trigger"].get<std::string>()
       << "\n\nRecommendation: " << f["recommendation"].get<std::string>()
       << "\n\n";
  md << "## Detailed test artifacts\n\n";
  for (const auto &file : {output / L"native-core/result.json",
                           output / L"native-session/session-results.json",
                           output / L"native-session/lifecycle-memory.json",
                           output / L"native-classic-ui/result.json",
                           output / L"native-diagnostics/diagnostics.json",
                           output / L"native-textures/result.json"}) {
    if (fs::exists(file))
      md << "### " << utf8(fs::relative(file, output).wstring())
         << "\n\n```json\n"
         << readResult(file).dump(2) << "\n```\n\n";
  }
  md << "## CPU allocation, parsing and dispatch\n\n```json\n"
     << cpu.dump(2) << "\n```\n\n"
     << "## Limits and next reproducible checks\n\n"
     << "- Source review sites contain exact file/line locations for waits, "
        "barriers and thread-lifetime patterns. They are investigation leads, "
        "not inferred races or invalid states.\n"
     << "- No compiler sanitizer, Application Verifier or D3D12 debug/GPU "
        "validation layer is silently enabled in existing binaries. Use "
        "separately instrumented builds for race, use-after-free and barrier "
        "attribution; raw timing from those builds is not comparable.\n"
     << "- Native UI checks exercise preview lifetimes, commands and "
        "restoration interactions. Legacy interactive preview automation is "
        "not available through its command interface; no fabricated legacy UI "
        "parity result is emitted.\n"
     << "- No visual metric proves zero ghosting, no memory slope proves zero "
        "leaks, and deterministic schema mutations are not exhaustive fuzzing. "
        "Native session lifecycle memory and check failures require source "
        "investigation.\n"
     << "- Existing diagnostics checks intentionally crash an isolated child "
        "and verify a minidump. Unexpected audit-controller crashes write "
        "`audit-controller-crash.dmp`. Unexpected test exits are recorded with "
        "their command and exit code; a stack is only available if that "
        "executable generated a dump.\n"
     << "- Legacy processes may load their archived managed runtime for "
        "measurement. The audit executable itself is native C++ and has no "
        "CLR, P/Invoke or managed bridge.\n"
     << "- Missing archived binaries, unavailable hardware, unsupported RT and "
        "test timeouts are coverage failures, not passes. No "
        "driver/device-wide leak guarantee or cross-monitor manual UX "
        "certification is made.\n";
  atomic_write(output / L"engine_audit_findings.md", md.str());
}

void visibleAudit(const fs::path &packet) {
  const std::string name = "visible-presentation";
  fs::create_directories(output / wide(name));
  status(86, L"AUTOMATIC FULL-SCREEN TEST: native 1440p or above; keep foreground for 30 seconds");
  const int displayWidth=GetSystemMetrics(SM_CXSCREEN), displayHeight=GetSystemMetrics(SM_CYSCREEN);
  SetEnvironmentVariableW(L"STARLIGHT_D3D_DEBUG", nullptr);
  SetEnvironmentVariableW(L"STARLIGHT_BENCH_SECONDS", L"30");
  SetEnvironmentVariableW(L"STARLIGHT_NATIVE_LOG", (output / wide(name) / L"native.log").c_str());
  auto process = child(name, binaries / L"Starlight.NativeRenderChecks.exe", {packet.wstring(),
    (output / wide(name) / L"render.json").wstring(), (output / wide(name) / L"frame.bmp").wstring(),
    std::to_wstring(displayWidth),std::to_wstring(displayHeight),L"1",L"fullscreen-auto",L"3",L"0"}, 120);
  auto result = readResult(output / wide(name) / L"render.json");
  auto verdict = starlight::audit::validate_render(result,displayWidth,displayHeight,1,"focused-present",false);
  if (displayWidth < 2560 || displayHeight < 1440 || !result.value("fullscreen",false) || !result.value("automaticStart",false))
    verdict["errors"].push_back("Automatic full-screen native 1440p-or-higher contract not met");
  if (process.value("status", "") != "pass") verdict["errors"].push_back("Child process failed, timed out, or was cancelled");
  std::ifstream capture(output / wide(name) / L"frame.bmp", std::ios::binary);
  BITMAPFILEHEADER file{}; BITMAPINFOHEADER info{};
  capture.read(reinterpret_cast<char*>(&file), sizeof(file));
  capture.read(reinterpret_cast<char*>(&info), sizeof(info));
  if (!capture || file.bfType != 0x4D42 || info.biWidth != displayWidth || (info.biHeight != displayHeight && info.biHeight != -displayHeight))
    verdict["errors"].push_back("Missing or wrong-sized captured frame");
  verdict["accepted"] = verdict["errors"].empty();
  verdict["performanceEligible"] = verdict["accepted"];
  verdict["name"] = name;
  evidenceChecks.push_back(verdict);
  save(output / wide(name) / L"evidence-verdict.json", verdict);
  if (!verdict["accepted"].get<bool>()) finding("visible presentation evidence rejected",name,verdict["errors"].dump(),
    "Inspect focus, process and rendering evidence; do not count this as a visible pass.");
  SetEnvironmentVariableW(L"STARLIGHT_BENCH_SECONDS", nullptr);
  SetEnvironmentVariableW(L"STARLIGHT_NATIVE_LOG", nullptr);
}
void visibleEditorAudit() {
  const std::string name="visible-editor";
  status(82,L"VISIBLE EDITOR: starts automatically after foreground verification; Esc cancels");
  auto process=child(name,binaries/L"Starlight.NativeEditor.exe",
    {L"--ui-walkthrough",(output/wide(name)/L"result.json").wstring()},240);
  auto result=readResult(output/wide(name)/L"result.json");
  bool good=process.value("status","")=="pass" && starlight::audit::validate_visible_editor(result);
  if (result.contains("checks") && result["checks"].is_array())
    for (const auto &check : result["checks"]) good &= check.value("pass",false) && check.value("foreground",false) && check.value("captureSucceeded",false);
  auto verdict=json{{"name",name},{"accepted",good},{"performanceEligible",false},
    {"scope","Visible menu/control walkthrough; not all editor features"}};
  evidenceChecks.push_back(verdict);
  save(output/wide(name)/L"evidence-verdict.json",verdict);
  if (!good) finding("visible editor walkthrough rejected",name,"Failed, incomplete, or missing UI evidence","Inspect per-step assertions, focus and captures.");
}
void finishGate0() {
  int passed = 0, failed = 0, controls = 0;
  json detail = json::array();
  for (const auto &run : runs) {
    auto name = run.at("name").get<std::string>();
    auto dir = output / wide(name);
    bool control = name == "component-physics" || name == "lifecycle-idle-control";
    bool good = run.value("status", "") == "pass";
    if (fs::exists(dir / L"session-results.json")) good &= readResult(dir / L"session-results.json").value("success",false);
    if (fs::exists(dir / L"render.json")) good &= readResult(dir / L"render.json").value("success",false);
    if (name.starts_with("debug-tier-") || name.starts_with("measurement-")) {
      const bool debug=name.starts_with("debug-tier-");
      const int tier=debug ? std::stoi(name.substr(11)) : 1;
      auto verdict=starlight::audit::validate_render(readResult(dir/L"render.json"),3840,2160,tier,"offscreen-no-present",debug);
      std::ifstream log(dir/(debug?L"debug.log":L"native.log"));
      const std::string messages((std::istreambuf_iterator<char>(log)),{});
      if (!log || messages.find("d3d12 debug:")!=std::string::npos || messages.find("device removed HRESULT")!=std::string::npos)
        verdict["errors"].push_back("Missing diagnostic log or graphics validation message; review required");
      verdict["accepted"]=verdict["errors"].empty();
      verdict["performanceEligible"]=verdict["accepted"].get<bool>()&&!debug;
      verdict["name"]=name;
      evidenceChecks.push_back(verdict);
      save(dir/L"evidence-verdict.json",verdict);
      good &= verdict["accepted"].get<bool>();
    }
    if (name == "visible-presentation" || name == "visible-editor") good &= readResult(dir / L"evidence-verdict.json").value("accepted",false);
    if (control && good) ++controls;
    else if (good) ++passed;
    else ++failed;
    detail.push_back({{"name",name},{"outcome", !good ? "failed" : control ? "control-completed" : "bounded-check-passed"}});
  }
  save(output / L"gate0-runs.json", runs);
  save(output / L"audit-verdict.json", {{"runCompleted",!cancel}, {"passedChecks",passed}, {"failedChecks",failed},
    {"completedControls",controls}, {"gate0Certified",false}, {"checks",detail}, {"evidenceChecks",evidenceChecks},
    {"scope",visibleOnly ? "visible-presentation-only" : gate0Components ? "handle-component-isolation" : "gate0-diagnostics"}});
  std::ostringstream report;
  report << "# Gate 0 audit outcome\n\nPassed bounded checks: "<<passed<<". Failed checks: "<<failed
         <<". Completed controls (not acceptance passes): "<<controls<<".\n\n";
  for (const auto &row : detail) report << "- "<<row["name"].get<std::string>()<<": "<<row["outcome"].get<std::string>()<<"\n";
  report << "\nRun completion is not Gate 0 certification. Offscreen results do not substitute for visible presentation. Debug timings are not performance claims. Controls do not prove leak freedom. See audit-verdict.json and per-stage evidence.\n";
  atomic_write(output / L"GATE0-RESULTS.md", report.str());
  status(100, cancel ? L"Cancelled — partial evidence saved" : L"Finished: " + std::to_wstring(failed) + L" failed; " +
    std::to_wstring(passed) + L" bounded checks passed; " + std::to_wstring(controls) + L" controls");
}
void gate0Audit() {
  try {
    if (visibleOnly) {
      const auto packet = root / L"audit-runs/run-20260917-192850/comparison.bin";
      save(output / L"scope.json", {{"scope","visible presentation only"},{"packetSha256",hash_file(packet)},
        {"viewportSha256",hash_file(binaries / L"Starlight.Viewport.dll")},
        {"harnessSha256",hash_file(binaries / L"Starlight.NativeRenderChecks.exe")}});
      visibleEditorAudit();
      if (!cancel) visibleAudit(packet);
      finishGate0(); finished = true; return;
    }
    if (gate0Components) {
      SetEnvironmentVariableW(L"STARLIGHT_HANDLE_TRACE", L"1");
      for (const auto *component : {L"physics", L"renderer"}) {
        if (cancel) break;
        SetEnvironmentVariableW(L"STARLIGHT_GATE0_COMPONENT", component);
        auto name = "component-" + utf8(component);
        status(std::wstring_view(component) == L"physics" ? 5 : 50, L"Isolating handle ownership: " + std::wstring(component));
        child(name, binaries / L"Starlight.NativeSessionChecks.exe", {(output / wide(name)).wstring(), root.wstring()}, 180);
      }
      SetEnvironmentVariableW(L"STARLIGHT_HANDLE_TRACE", nullptr);
      SetEnvironmentVariableW(L"STARLIGHT_GATE0_COMPONENT", nullptr);
      finishGate0();
      finished = true;
      return;
    }
    save(output / L"scope.json", {{"scope", "Gate 0 only: lifecycle attribution and renderer diagnostics"},
      {"viewportSha256", hash_file(binaries / L"Starlight.Viewport.dll")},
      {"sessionSha256", hash_file(binaries / L"Starlight.NativeSessionChecks.exe")},
      {"harnessSha256", hash_file(binaries / L"Starlight.NativeRenderChecks.exe")}});
    SetEnvironmentVariableW(L"STARLIGHT_HANDLE_TRACE", L"1");
    for (int control = 0; control < 2 && !cancel; ++control) {
      const std::string name = control ? "lifecycle-idle-control" : "lifecycle-traced";
      status(control ? 22 : 4, control ? L"Handle attribution: matched idle control" : L"Handle attribution: 80 scene cycles");
      SetEnvironmentVariableW(L"STARLIGHT_LIFECYCLE_IDLE_CONTROL", control ? L"1" : nullptr);
      child(name, binaries / L"Starlight.NativeSessionChecks.exe", {(output / wide(name)).wstring(), root.wstring()}, 360);
    }
    SetEnvironmentVariableW(L"STARLIGHT_HANDLE_TRACE", nullptr);
    SetEnvironmentVariableW(L"STARLIGHT_LIFECYCLE_IDLE_CONTROL", nullptr);
    const auto packet = root / L"audit-runs/run-20260917-192850/comparison.bin";
    save(output / L"workload.json", {{"packetSha256", hash_file(packet)}, {"width",3840}, {"height",2160},
      {"purpose", "Repeated current-renderer measurements, not legacy speedup"}});
    for (int tier = 0; tier <= 4 && !cancel; ++tier) {
      auto name = "debug-tier-" + std::to_string(tier);
      status(40 + tier * 5, L"4K renderer debug validation, tier " + std::to_wstring(tier));
      fs::create_directories(output / wide(name));
      SetEnvironmentVariableW(L"STARLIGHT_D3D_DEBUG", L"1");
      SetEnvironmentVariableW(L"STARLIGHT_NATIVE_LOG", (output / wide(name) / L"debug.log").c_str());
      child(name, binaries / L"Starlight.NativeRenderChecks.exe", {packet.wstring(),
        (output / wide(name) / L"render.json").wstring(), (output / wide(name) / L"frame.bmp").wstring(),
        L"3840",L"2160",std::to_wstring(tier),L"--disable-background-throttle",L"3",L"0"}, 90);
    }
    SetEnvironmentVariableW(L"STARLIGHT_D3D_DEBUG", nullptr);
    for (int repetition = 0; repetition < 2 && !cancel; ++repetition) {
      auto name = "measurement-" + std::to_string(repetition);
      status(70 + repetition * 10, L"Matched native 4K measurement " + std::to_wstring(repetition + 1));
      fs::create_directories(output / wide(name));
      SetEnvironmentVariableW(L"STARLIGHT_NATIVE_LOG", (output / wide(name) / L"native.log").c_str());
      child(name, binaries / L"Starlight.NativeRenderChecks.exe", {packet.wstring(),
        (output / wide(name) / L"render.json").wstring(), (output / wide(name) / L"frame.bmp").wstring(),
        L"3840",L"2160",L"1",L"--disable-background-throttle",L"3",L"0"}, 90);
    }
    SetEnvironmentVariableW(L"STARLIGHT_NATIVE_LOG", nullptr);
    if (!cancel) visibleEditorAudit();
    if (!cancel) visibleAudit(packet);
    finishGate0();
  } catch (const std::exception &e) {
    save(output / L"gate0-error.json", {{"error", e.what()}, {"runs", runs}});
    status(100, L"Gate 0 stopped — error evidence saved");
  }
  finished = true;
}
void audit() {
  if (gate0Only) { gate0Audit(); return; }
  json hw,
      comparisons = {{"commands", json::array()}, {"renderers", json::array()}};
  try {
    status(1, L"Detecting CPU and GPU");
    hw = hardware();
    save(output / L"hardware.json", hw);
    scan();
    if (cancel)
      throw std::runtime_error("Cancelled");
    stress();
    json hashes = json::array();
    for (const auto &p :
         {self, binaries / L"Starlight.NativeEditor.exe",
          binaries / L"Starlight.Viewport.dll",
          legacy / L"Starlight.Editor.exe", legacy / L"Starlight.Viewport.dll"})
      if (fs::exists(p))
        hashes.push_back(
            {{"path", utf8(p.wstring())}, {"sha256", hash_file(p)}});
    save(output / L"binary-identities.json", hashes);
    status(32, L"Core schemas, asset ingestion and crash diagnostics");
    child("native-core", binaries / L"Starlight.NativeCoreChecks.exe",
          {(output / L"native-core").wstring(),
           (root / L"migration/geometry-reference.json").wstring()});
    child("native-diagnostics",
          binaries / L"Starlight.NativeDiagnosticChecks.exe",
          {(output / L"native-diagnostics").wstring()});
    status(37, L"Texture formats and native scene lifecycle");
    child("native-textures", binaries / L"Starlight.NativeTextureChecks.exe",
          {(output / L"native-textures").wstring()});
    child("native-session", binaries / L"Starlight.NativeSessionChecks.exe",
          {(output / L"native-session").wstring(), root.wstring()}, 240);
    status(44, L"Classic editor UI and preview lifetime regression checks");
    child(
        "native-classic-ui", binaries / L"Starlight.NativeEditor.exe",
        {L"--ui-check", (output / L"native-classic-ui/result.json").wstring()},
        600);
    status(56, L"Matched legacy and native editor command measurements");
    SceneData scene;
    scene.Terrain.emplace();
    for (int i = 0; i < 64; ++i) {
      Character c;
      c.Shape = i % 2 ? "rock" : "cube";
      c.X = i % 8 - 4;
      c.Z = i / 8 - 4;
      scene.Characters.push_back(c);
    }
    atomic_write(output / L"comparison.starscene", scene.encode());
    auto packet = render_scene(scene);
    atomic_write(output / L"comparison.bin",
                 std::string_view(reinterpret_cast<const char *>(packet.data()),
                                  packet.size()));
    save(output / L"workload.json",
         json{{"sceneHash", hash_file(output / L"comparison.starscene")},
              {"packetHash", hash_file(output / L"comparison.bin")},
              {"objectCount", 64},
              {"width", 3840},
              {"height", 2160},
              {"requestedPresentation", "offscreen-no-present"}});
    for (int repetition = 0; repetition < 3 && !cancel; ++repetition)
      for (auto mode : {"describe", "inspect"}) {
        std::string name = std::string(mode) + "-" + std::to_string(repetition);
        json pair = {{"name", name}};
        for (int k = 0; k < 2; ++k) {
          bool old = (k + repetition) % 2 == 0;
          auto key = old ? "old" : "new";
          std::string runName = name + "-" + key;
          std::vector<std::wstring> args = {L"--" + wide(mode)};
          if (std::string(mode) == "inspect")
            args.push_back((output / L"comparison.starscene").wstring());
          args.push_back(L"--result");
          args.push_back((output / wide(runName) / L"result.json").wstring());
          pair[key] = child(runName,
                            old ? legacy / L"Starlight.Editor.exe"
                                : binaries / L"Starlight.NativeCommands.exe",
                            args, 60);
          auto result = readResult(output / wide(runName) / L"result.json");
          pair[key]["semanticSuccess"] = result.value("success", false);
          if (!result.value("success", false))
            finding("command result failure",
                    utf8((output / wide(runName) / L"result.json").wstring()),
                    name + " returned missing/unsuccessful JSON",
                    "Inspect CLI parity and archived runtime availability; "
                    "wall time is not comparable for failed commands.");
        }
        comparisons["commands"].push_back(pair);
      }
    fs::path oldHarness = output / L"legacy-renderer";
    fs::create_directories(oldHarness);
    // No overwrites of archived files. Loader search starts beside this
    // isolated harness.
    if (fs::exists(legacy / L"Starlight.Viewport.dll")) {
      for (const auto &e : fs::directory_iterator(legacy))
        if (e.is_regular_file() && e.path().extension() == L".dll")
          fs::copy_file(e.path(), oldHarness / e.path().filename(),
                        fs::copy_options::overwrite_existing);
      fs::copy_file(binaries / L"Starlight.NativeRenderChecks.exe",
                    oldHarness / L"Starlight.NativeRenderChecks.exe",
                    fs::copy_options::overwrite_existing);
    }
    for (int tier = 0; tier <= 4 && !cancel; ++tier)
      for (int rep = 0; rep < 2 && !cancel; ++rep) {
        auto name = "tier-" + std::to_string(tier) + "-" + std::to_string(rep);
        status(62 + tier * 6 + rep * 3,
               L"4K matched renderer comparison: " + wide(name));
        json pair = {{"name", name}};
        for (int k = 0; k < 2; ++k) {
          bool old = (k + rep) % 2 == 0;
          auto key = old ? "old" : "new";
          auto runName = name + "-" + key;
          auto dir = output / wide(runName);
          child(runName,
                (old ? oldHarness : binaries) /
                    L"Starlight.NativeRenderChecks.exe",
                {(output / L"comparison.bin").wstring(),
                 (dir / L"render.json").wstring(),
                 (dir / L"frame.bmp").wstring(), L"3840", L"2160",
                 std::to_wstring(tier), L"--disable-background-throttle", L"3",
                 L"0"},
                65);
          pair[key] = readResult(dir / L"render.json");
        }
        bool comparable = true;
        for (auto key : {"old", "new"}) {
          auto &r = pair[key];
          comparable &=
              r.value("success", false) && r.value("activeTier", -1) == tier &&
              r.value("renderWidth", 0) == 3840 &&
              r.value("denoiserRequested", -1) == 0 &&
              !r.value("rrActive", true) && !r.value("upscaling", true) &&
              !r.value("frameGeneration", true);
        }
        pair["comparable"] = comparable;
        pair["fpsComparable"] =
            comparable && pair["old"].value("presentationMode", "") ==
                              pair["new"].value("presentationMode", "");
        pair["comparisonScope"] =
            "Matching scene/native-wavelet GPU work; immutable legacy DLL uses "
            "focused presentation (500 FPS cap). New DLL bypasses DXGI "
            "presentation and all pacing. Do not compute an FPS speedup across "
            "these modes.";
        if (!pair["new"].value("backgroundThrottleBypassed", false))
          finding("benchmark configuration failure",
                  "tools/checks/render_checks.cpp", name,
                  "New renderer did not confirm offscreen benchmark mode; "
                  "reject its throughput result.");
        comparisons["renderers"].push_back(pair);
        save(output / L"comparisons.partial.json", comparisons);
      }
    if (!cancel) visibleEditorAudit();
    if (!cancel) visibleAudit(output / L"comparison.bin");
    save(output / L"visible-evidence.json", evidenceChecks);
    status(96, L"Writing findings, comparison tables and raw evidence");
  } catch (const std::exception &e) {
    finding("audit execution interrupted", "tools/audit/audit.cpp", e.what(),
            "Inspect partial evidence and rerun the failed stage after "
            "addressing the reported exception.");
  }
  try {
    report(hw, comparisons);
    status(100, cancel ? L"Cancelled — partial report saved"
                       : L"Completed — reports saved locally");
  } catch (const std::exception &e) {
    status(percent, L"Report write failed: " + wide(e.what()));
  }
  finished = true;
}

LRESULT CALLBACK proc(HWND h, UINT message, WPARAM w, LPARAM l) {
  LRESULT themed{}; if(audit_theme::message(message,w,l,themed)) return themed;
  switch (message) {
  case WM_APP+17:
    // Transfer permission on the UI thread that receives the user's activation.
    return AllowSetForegroundWindow(static_cast<DWORD>(w));
  case WM_CREATE:
    label = CreateWindowW(L"STATIC", L"Starting", WS_CHILD | WS_VISIBLE, 24, 24,
                          620, 36, h, nullptr, nullptr, nullptr);
    bar = CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, 24, 72,
                        620, 28, h, nullptr, nullptr, nullptr);
    amount = CreateWindowW(L"STATIC", L"0%", WS_CHILD | WS_VISIBLE, 24, 116,
                           620, 28, h, nullptr, nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Open output folder",
                  WS_CHILD | WS_VISIBLE | WS_TABSTOP, 24, 158, 200, 32, h,
                  reinterpret_cast<HMENU>(1), nullptr, nullptr);
    for (auto control : {label, amount})
      SendMessageW(control, WM_SETFONT,
                   reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),
                   TRUE);
    SendMessageW(bar, PBM_SETRANGE32, 0, 100);
    SetTimer(h, 1, 150, nullptr);
    audit_theme::apply_tree(h);
    return 0;
  case WM_TIMER: {
    if(batchMode && finished) { DestroyWindow(h); return 0; }
    std::lock_guard lock(stateMutex);
    SetWindowTextW(label, stage.c_str());
    auto text = std::to_wstring(percent.load()) + L"%   •   " +
                std::to_wstring(workers) + L" CPU workers";
    SetWindowTextW(amount, text.c_str());
    SendMessageW(bar, PBM_SETPOS, percent, 0);
    return 0;
  }
  case WM_COMMAND:
    if (LOWORD(w) == 1)
      ShellExecuteW(h, L"open", output.c_str(), nullptr, nullptr,
                    SW_SHOWNORMAL);
    return 0;
  case WM_CLOSE:
    if (!finished) {
      cancel = true;
      status(percent, L"Cancelling owned workers — saving partial results");
      return 0;
    }
    DestroyWindow(h);
    return 0;
  case WM_DESTROY:
    KillTimer(h, 1);
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProcW(h, message, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  wchar_t module[32768];
  GetModuleFileNameW(nullptr, module, 32768);
  self = module;
  binaries = self.parent_path();
  if (!fs::exists(binaries / L"Starlight.NativeSessionChecks.exe"))
    binaries = binaries.parent_path();
  for(int i=1;i<argc;++i) if(std::wstring_view(argv[i])==L"--batch") batchMode=true;
  root = argc > 1 ? fs::absolute(argv[1]) : fs::current_path();
  gate0Only = argc > 3 && std::wstring_view(argv[3]) == L"--gate0";
  gate0Components = argc > 3 && std::wstring_view(argv[3]) == L"--gate0-components";
  visibleOnly = argc > 3 && std::wstring_view(argv[3]) == L"--gate0-visible";
  gate0Only |= gate0Components || visibleOnly;
  SYSTEMTIME time{};
  GetLocalTime(&time);
  wchar_t stamp[100];
  swprintf_s(stamp, L"run-%04u%02u%02u-%02u%02u%02u-%lu", time.wYear,
             time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
             GetCurrentProcessId());
  output = argc > 2 ? fs::absolute(argv[2]) : root / L"audit-runs" / stamp;
  legacy = argc > 3 ? fs::path(argv[3])
                    : root.parent_path() /
                          L"_recovery/starlight-managed-retired-20260917/"
                          L"editor/bin/audit-release";
  LocalFree(argv);
  try {
    if (fs::exists(output) && !fs::is_empty(output))
      throw std::runtime_error("Use a fresh, empty audit output directory");
    fs::create_directories(output);
  } catch (const std::exception &e) {
    MessageBoxW(nullptr, wide(e.what()).c_str(),
                L"Starlight audit cannot start", MB_ICONERROR);
    return 2;
  }
  SetUnhandledExceptionFilter(crash);
  INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_PROGRESS_CLASS};
  InitCommonControlsEx(&controls);
  WNDCLASSW wc{};
  wc.hInstance = instance;
  wc.lpfnWndProc = proc;
  wc.lpszClassName = L"StarlightAuditProgress";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = audit_theme::brush();
  RegisterClassW(&wc);
  window = CreateWindowW(
      wc.lpszClassName, gate0Only ? L"Starlight Engine — Gate 0 Reliability Diagnostics" : L"Starlight Engine — Native Audit & Profiling",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
      CW_USEDEFAULT, 700, 260, nullptr, nullptr, instance, nullptr);
  if (!window)
    return 3;
  ShowWindow(window, SW_SHOW);
  UpdateWindow(window);
  coordinator = std::jthread(audit);
  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  cancel = true;
  if (coordinator.joinable())
    coordinator.join();
  if(batchMode) {
    if(runs.empty() || !findings.empty()) return 1;
    for(const auto& run:runs) if(run.value("status","")!="pass") return 1;
    if(gate0Only && (!fs::exists(output/L"audit-verdict.json") || readResult(output/L"audit-verdict.json").value("failedChecks",1)>0)) return 1;
  }
  return 0;
}
