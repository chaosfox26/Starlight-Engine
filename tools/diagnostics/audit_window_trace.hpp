#pragma once
#include "platform.hpp"
#include <dbghelp.h>
#include <mutex>
#include <tlhelp32.h>

// Enabled only by --ui-check. CBT callbacks run synchronously on the creating
// thread, so these are creation stacks, not asynchronous WinEvent observer
// stacks.
struct AuditWindowTrace {
  inline static std::mutex mutex;
  inline static starlight::editor::json events =
      starlight::editor::json::array();
  std::vector<HHOOK> hooks;
  bool symbolsOwned = false;
  static LRESULT CALLBACK callback(int code, WPARAM w, LPARAM l) {
    if (code == HCBT_CREATEWND || code == HCBT_DESTROYWND) {
      wchar_t name[256]{};
      GetClassNameW(reinterpret_cast<HWND>(w), name, 256);
      if (std::wstring_view(name) == L"SystemUserAdapterWindowClass") {
        try {
          void *stack[48]{};
          auto count = CaptureStackBackTrace(0, 48, stack, nullptr);
          std::lock_guard lock(mutex);
          starlight::editor::json frames = starlight::editor::json::array();
          for (USHORT i = 0; i < count; ++i) {
            HMODULE module = nullptr;
            wchar_t path[32768]{};
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(stack[i]), &module);
            if (module)
              GetModuleFileNameW(module, path, 32768);
            frames.push_back(
                {{"module", starlight::editor::utf8(path)},
                 {"address", reinterpret_cast<uintptr_t>(stack[i])},
                 {"rva", reinterpret_cast<uintptr_t>(stack[i]) -
                             reinterpret_cast<uintptr_t>(module)}});
          }
          auto owner = reinterpret_cast<HMODULE>(
              GetClassLongPtrW(reinterpret_cast<HWND>(w), GCLP_HMODULE));
          wchar_t path[32768]{};
          if (owner)
            GetModuleFileNameW(owner, path, 32768);
          events.push_back(
              {{"event", code == HCBT_CREATEWND ? "create" : "destroy"},
               {"class", "SystemUserAdapterWindowClass"},
               {"hwnd", static_cast<uint64_t>(w)},
               {"thread", GetCurrentThreadId()},
               {"classModule", starlight::editor::utf8(path)},
               {"stack", frames}});
        } catch (...) {
        } // Never allow diagnostic allocation failure through a Win32 callback.
      }
    }
    return CallNextHookEx(nullptr, code, w, l);
  }
  AuditWindowTrace() {
    // No symbol server network traffic during measured UI churn.
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME |
                  SYMOPT_FAIL_CRITICAL_ERRORS);
    symbolsOwned = SymInitialize(GetCurrentProcess(), "", TRUE) != FALSE;
    starlight::editor::Handle snapshot(
        CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0));
    THREADENTRY32 entry{sizeof(entry)};
    if (snapshot && Thread32First(snapshot.value, &entry))
      do {
        if (entry.th32OwnerProcessID == GetCurrentProcessId())
          if (auto hook = SetWindowsHookExW(WH_CBT, callback, nullptr,
                                            entry.th32ThreadID))
            hooks.push_back(hook);
      } while (Thread32Next(snapshot.value, &entry));
  }
  starlight::editor::json report() {
    starlight::editor::json captured;
    {
      std::lock_guard lock(mutex);
      captured = events;
    }
    // Resolve local symbols after creation returns, outside both CBT and the
    // callback mutex; DbgHelp may load modules and must not run under USER
    // hooks.
    for (auto &event : captured)
      for (auto &frame : event["stack"]) {
        alignas(SYMBOL_INFO) unsigned char
            storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
        auto symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;
        if (SymFromAddr(GetCurrentProcess(), frame["address"].get<DWORD64>(),
                        &displacement, symbol)) {
          frame["symbol"] = std::string(symbol->Name);
          frame["displacement"] = displacement;
        }
      }
    return {{"hookedThreads", hooks.size()},
            {"events", captured},
            {"coverage", "Existing process threads at test entry; newly "
                         "spawned/system threads may be outside hook coverage. "
                         "Empty events are not proof of OS ownership."}};
  }
  ~AuditWindowTrace() {
    for (auto hook : hooks)
      UnhookWindowsHookEx(hook);
    if (symbolsOwned)
      SymCleanup(GetCurrentProcess());
  }
};
