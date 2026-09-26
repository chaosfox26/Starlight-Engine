#pragma once
// Opt-in own-process import instrumentation for diagnostic executables only.
// No system tracing privileges, foreign processes, or persistent settings.
#include <cstdio>
#ifdef STARLIGHT_INLINE_ALLOCATION_TRACE
#include <MinHook.h>
#endif
namespace memory_trace {
using Allocate = LONG(NTAPI*)(HANDLE, void**, ULONG_PTR, SIZE_T*, ULONG, ULONG);
using Free = LONG(NTAPI*)(HANDLE, void**, SIZE_T*, ULONG);
using AllocateEx = LONG(NTAPI*)(HANDLE,void**,SIZE_T*,ULONG,ULONG,void*,ULONG);
using VirtualAllocate = void*(WINAPI*)(void*,SIZE_T,DWORD,DWORD);
using VirtualRelease = BOOL(WINAPI*)(void*,SIZE_T,DWORD);
inline Allocate allocate = nullptr;
inline Free release = nullptr;
inline AllocateEx allocateEx = nullptr;
inline VirtualAllocate virtualAllocate = nullptr;
inline VirtualRelease virtualRelease = nullptr;
inline HANDLE log = INVALID_HANDLE_VALUE;
inline thread_local bool recording = false;
struct Event {
  uint64_t tick, operation, address, bytes, flags, thread, count, process, stack[24];
};
inline void record(uint64_t operation, void* address, SIZE_T bytes, ULONG flags, HANDLE process=GetCurrentProcess()) {
  if (recording || log==INVALID_HANDLE_VALUE) return;
  const auto savedError=GetLastError();
  recording=true;
  Event event{};
  event.tick=GetTickCount64(); event.operation=operation;
  event.address=reinterpret_cast<uintptr_t>(address); event.bytes=bytes;
  event.flags=flags; event.thread=GetCurrentThreadId();
  event.process=reinterpret_cast<uintptr_t>(process);
  event.count=CaptureStackBackTrace(2,24,reinterpret_cast<void**>(event.stack),nullptr);
  DWORD written=0; WriteFile(log,&event,sizeof(event),&written,nullptr);
  recording=false;
  SetLastError(savedError);
}
inline LONG NTAPI on_allocate(HANDLE process, void** base, ULONG_PTR zero,
                              SIZE_T* bytes, ULONG type, ULONG protection) {
  const auto status=allocate(process,base,zero,bytes,type,protection);
  if (status>=0) record(1,*base,*bytes,type,process);
  return status;
}
inline LONG NTAPI on_free(HANDLE process, void** base, SIZE_T* bytes, ULONG type) {
  void* address=nullptr; SIZE_T size=0;
  __try { address=*base; size=*bytes; }
  __except(EXCEPTION_EXECUTE_HANDLER) { return release(process,base,bytes,type); }
  const auto status=release(process,base,bytes,type);
  if (status>=0) record(2,address,size,type,process);
  return status;
}
inline LONG NTAPI on_allocate_ex(HANDLE process,void** base,SIZE_T* bytes,ULONG type,
                                 ULONG protection,void* parameters,ULONG count) {
  auto status=allocateEx(process,base,bytes,type,protection,parameters,count);
  if(status>=0) record(5,*base,*bytes,type,process);
  return status;
}
inline void* WINAPI on_virtual_allocate(void* base,SIZE_T bytes,DWORD type,DWORD protection) {
  auto address=virtualAllocate(base,bytes,type,protection);
  if(address)record(3,address,bytes,type);
  return address;
}
inline BOOL WINAPI on_virtual_free(void* base,SIZE_T bytes,DWORD type) {
  auto result=virtualRelease(base,bytes,type);
  if(result)record(4,base,bytes,type);
  return result;
}
inline void start(const fs::path& output) {
  if (!GetEnvironmentVariableW(L"STARLIGHT_ALLOCATION_TRACE",nullptr,0)) return;
  if (!allocate) {
    auto ntdll=GetModuleHandleW(L"ntdll.dll");
    allocate=reinterpret_cast<Allocate>(GetProcAddress(ntdll,"NtAllocateVirtualMemory"));
    release=reinterpret_cast<Free>(GetProcAddress(ntdll,"NtFreeVirtualMemory"));
    allocateEx=reinterpret_cast<AllocateEx>(GetProcAddress(ntdll,"NtAllocateVirtualMemoryEx"));
    virtualAllocate=reinterpret_cast<VirtualAllocate>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"VirtualAlloc"));
    virtualRelease=reinterpret_cast<VirtualRelease>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"VirtualFree"));
    if (!allocate || !release) throw std::runtime_error("Allocation trace APIs unavailable");
    log=CreateFileW((output/L"allocation-events.bin").c_str(),GENERIC_WRITE,FILE_SHARE_READ,
                    nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(log==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create allocation trace");
#ifdef STARLIGHT_INLINE_ALLOCATION_TRACE
    auto check=[](MH_STATUS status) { if(status!=MH_OK) throw std::runtime_error(MH_StatusToString(status)); };
    check(MH_Initialize());
    check(MH_CreateHook(reinterpret_cast<void*>(allocate),reinterpret_cast<void*>(&on_allocate),reinterpret_cast<void**>(&allocate)));
    check(MH_CreateHook(reinterpret_cast<void*>(release),reinterpret_cast<void*>(&on_free),reinterpret_cast<void**>(&release)));
    if(allocateEx) check(MH_CreateHook(reinterpret_cast<void*>(allocateEx),reinterpret_cast<void*>(&on_allocate_ex),reinterpret_cast<void**>(&allocateEx)));
    check(MH_EnableHook(MH_ALL_HOOKS));
#endif
  }
  HMODULE modules[1024]{}; DWORD needed=0;
  if (!EnumProcessModules(GetCurrentProcess(),modules,sizeof(modules),&needed) || needed>sizeof(modules))
    throw std::runtime_error("Cannot enumerate own modules");
  std::ofstream manifest(output/L"allocation-modules.csv");
  manifest << "base,size,module,hooks\n";
  for (DWORD i=0;i<needed/sizeof(HMODULE);++i) {
    auto image=reinterpret_cast<unsigned char*>(modules[i]);
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic!=IMAGE_DOS_SIGNATURE) continue;
    auto pe=reinterpret_cast<IMAGE_NT_HEADERS*>(image+dos->e_lfanew);
    auto directory=pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    unsigned hooks=0;
    if (directory.VirtualAddress) {
#ifndef STARLIGHT_INLINE_ALLOCATION_TRACE
      auto imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+directory.VirtualAddress);
      for (;imports->Name;++imports) {
        auto slot=reinterpret_cast<IMAGE_THUNK_DATA*>(image+imports->FirstThunk);
        for (;slot->u1.Function;++slot) {
          uintptr_t replacement=0;
          if(slot->u1.Function==reinterpret_cast<uintptr_t>(allocate)) replacement=reinterpret_cast<uintptr_t>(&on_allocate);
          if(slot->u1.Function==reinterpret_cast<uintptr_t>(release)) replacement=reinterpret_cast<uintptr_t>(&on_free);
          if(allocateEx && slot->u1.Function==reinterpret_cast<uintptr_t>(allocateEx)) replacement=reinterpret_cast<uintptr_t>(&on_allocate_ex);
          if(slot->u1.Function==reinterpret_cast<uintptr_t>(virtualAllocate)) replacement=reinterpret_cast<uintptr_t>(&on_virtual_allocate);
          if(slot->u1.Function==reinterpret_cast<uintptr_t>(virtualRelease)) replacement=reinterpret_cast<uintptr_t>(&on_virtual_free);
          if (!replacement) continue;
          DWORD old=0,restored=0;
          if(!VirtualProtect(&slot->u1.Function,sizeof(uintptr_t),PAGE_READWRITE,&old))
            throw std::runtime_error("Cannot instrument own import slot");
          InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&slot->u1.Function),reinterpret_cast<void*>(replacement));
          if(!VirtualProtect(&slot->u1.Function,sizeof(uintptr_t),old,&restored))
            throw std::runtime_error("Cannot restore own import protection");
          ++hooks;
        }
      }
#endif
    }
    wchar_t path[32768]{}; GetModuleFileNameW(modules[i],path,32768);
    manifest << reinterpret_cast<uintptr_t>(image) << ',' << pe->OptionalHeader.SizeOfImage
             << ',' << fs::path(path).filename().string() << ',' << hooks << '\n';
  }
  record(0,nullptr,0,0);
  void* probe=nullptr; SIZE_T probeBytes=65536;
  auto entry=reinterpret_cast<Allocate>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtAllocateVirtualMemory"));
  auto status=entry(GetCurrentProcess(),&probe,0,&probeBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
  record(99,probe,probeBytes,static_cast<ULONG>(status));
  if(status<0)throw std::runtime_error("Allocation trace self-test failed");
  auto freeEntry=reinterpret_cast<Free>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtFreeVirtualMemory"));
  probeBytes=0; status=freeEntry(GetCurrentProcess(),&probe,&probeBytes,MEM_RELEASE);
  record(98,probe,probeBytes,static_cast<ULONG>(status));
  if(status<0)throw std::runtime_error("Allocation trace self-test release failed");
}
inline void stop() {
#ifdef STARLIGHT_INLINE_ALLOCATION_TRACE
  if(log!=INVALID_HANDLE_VALUE) {
    auto status=MH_DisableHook(MH_ALL_HOOKS);
    if(status!=MH_OK) throw std::runtime_error(MH_StatusToString(status));
    FlushFileBuffers(log);
  }
#endif
}
}
