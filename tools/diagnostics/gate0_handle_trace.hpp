#pragma once
// Diagnostic-only, own-process handle origins. Native layouts are documented in
// https://github.com/winsiderss/phnt/blob/master/ntpsapi.h (ProcessHandleTracing).
// No object names, foreign processes, or global verifier settings are queried.
namespace gate0 {
using Query = LONG(WINAPI *)(HANDLE, ULONG, void *, ULONG, ULONG *);
using Set = LONG(WINAPI *)(HANDLE, ULONG, void *, ULONG);
inline Query query() {
  return reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
}
inline LONG enable() {
  if (!GetEnvironmentVariableW(L"STARLIGHT_HANDLE_TRACE", nullptr, 0)) return 1;
  auto set = reinterpret_cast<Set>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetInformationProcess"));
  struct { ULONG flags, slots; } options{0, 65536};
  return set ? set(GetCurrentProcess(), 32, &options, sizeof(options)) : -1;
}
inline json address(void *p) {
  MEMORY_BASIC_INFORMATION memory{};
  wchar_t module[32768]{};
  json result{{"address", reinterpret_cast<uintptr_t>(p)}};
  if (VirtualQuery(p, &memory, sizeof(memory)) && memory.Type == MEM_IMAGE &&
      GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase), module, 32768)) {
    result["module"] = fs::path(module).filename().string();
    result["offset"] = reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(memory.AllocationBase);
  }
  return result;
}
inline json describeHandle(HANDLE handle) {
  using ObjectQuery = LONG(WINAPI *)(HANDLE, ULONG, void *, ULONG, ULONG *);
  auto objectQuery = reinterpret_cast<ObjectQuery>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject"));
  struct Unicode { USHORT length, capacity; wchar_t *buffer; };
  json result = json::object();
  std::vector<unsigned char> storage(4096);
  ULONG needed = 0;
  if (objectQuery && objectQuery(handle, 2, storage.data(), static_cast<ULONG>(storage.size()), &needed) >= 0) {
    auto name = reinterpret_cast<Unicode *>(storage.data());
    result["typeName"] = utf8(std::wstring(name->buffer, name->length / sizeof(wchar_t)));
    if (result["typeName"] == "Thread") {
      auto threadQuery = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
      void *start = nullptr;
      if (threadQuery && threadQuery(handle, 9, &start, sizeof(start), nullptr) >= 0)
        result["threadStart"] = address(start);
      result["threadId"] = GetThreadId(handle);
    }
  }
  return result;
}
inline json origins(const json &baseline, const json &final) {
  struct Entry { HANDLE handle, process, thread; ULONG type; void *stack[16]; };
  struct Table { HANDLE handle; ULONG count; Entry entries[1]; };
  json result = json::array();
  auto q = query();
  if (!q || !baseline.value("available", false) || !final.value("available", false)) return result;
  // Query only newly observed handles. Reuse is possible; tracing records it.
  for (const auto &entry : final["entries"]) {
    bool seen = false;
    for (const auto &old : baseline["entries"]) if (old == entry) { seen = true; break; }
    if (seen) continue;
    std::vector<unsigned char> buffer(offsetof(Table, entries) + 4096 * sizeof(Entry));
    auto table = reinterpret_cast<Table *>(buffer.data());
    table->handle = reinterpret_cast<HANDLE>(entry["handle"].get<uintptr_t>());
    ULONG length = 0;
    auto status = q(GetCurrentProcess(), 32, table, static_cast<ULONG>(buffer.size()), &length);
    json row{{"handle", entry["handle"]}, {"ntstatus", status}, {"traces", json::array()}};
    if (status >= 0 && table->count <= 4096) {
      for (ULONG i = 0; i < table->count; ++i) {
        const auto &trace = table->entries[i];
        json stack = json::array();
        for (auto p : trace.stack) if (p) stack.push_back(address(p));
        auto creator = OpenThread(THREAD_QUERY_INFORMATION, FALSE, static_cast<DWORD>(reinterpret_cast<uintptr_t>(trace.thread)));
        json creatorInfo = creator ? describeHandle(creator) : json::object();
        if (creator) CloseHandle(creator);
        row["traces"].push_back({{"type", trace.type}, {"thread", reinterpret_cast<uintptr_t>(trace.thread)}, {"creator", creatorInfo}, {"stack", stack}});
      }
    }
    result.push_back(row);
  }
  return result;
}
}
