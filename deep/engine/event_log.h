#pragma once
// Diagnostic event log (STARLIGHT_EVENT_LOG=<path prefix>). Every module (the player/session exe, the viewport DLL) buffers
// timestamped events in memory with the system-wide QueryPerformanceCounter clock and writes "<prefix>.<module>.events" at
// shutdown, so no I/O ever happens inside a frame. Merge the files by timestamp (tools: _recovery\claude-fix-20261002\events.py).
// When the variable is not set every call is a single predictable branch.
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace starlight_diag {
inline void starlight_diag_anchor_dummy(){}
struct Event {double ms;uint32_t thread;char tag[20];double a,b,c;};
struct EventLog {
    static constexpr size_t capacity=1u<<21;
    Event* events=nullptr;std::atomic<uint32_t> count{0};bool enabled=false;char prefix[400]{};double frequency=0;
    EventLog(){
        const DWORD length=GetEnvironmentVariableA("STARLIGHT_EVENT_LOG",prefix,sizeof(prefix));
        if(!length||length>=sizeof(prefix)){prefix[0]=0;return;}
        LARGE_INTEGER f;QueryPerformanceFrequency(&f);frequency=double(f.QuadPart);
        events=static_cast<Event*>(VirtualAlloc(nullptr,sizeof(Event)*capacity,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
        enabled=events!=nullptr;
    }
    static double now(){static const double perMs=[]{LARGE_INTEGER f;QueryPerformanceFrequency(&f);return double(f.QuadPart)/1000.;}();LARGE_INTEGER q;QueryPerformanceCounter(&q);return double(q.QuadPart)/perMs;}
    void add(const char* tag,double a,double b,double c){
        const uint32_t i=count.fetch_add(1,std::memory_order_relaxed);if(i>=capacity)return;
        Event& e=events[i];e.ms=now();e.thread=GetCurrentThreadId();std::strncpy(e.tag,tag,sizeof(e.tag)-1);e.tag[sizeof(e.tag)-1]=0;e.a=a;e.b=b;e.c=c;
    }
    ~EventLog(){
        if(!enabled)return;
        HMODULE module=nullptr;char name[MAX_PATH]{};
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(&starlight_diag_anchor_dummy),&module);
        GetModuleFileNameA(module,name,MAX_PATH);
        const char* base=std::strrchr(name,'\\');base=base?base+1:name;
        char path[900];std::snprintf(path,sizeof(path),"%s.%s.events",prefix,base);
        FILE* f=nullptr;fopen_s(&f,path,"w");
        if(f){const uint32_t n=std::min<uint32_t>(count.load(),uint32_t(capacity));
            for(uint32_t i=0;i<n;++i)std::fprintf(f,"%.4f %u %s %.5f %.5f %.5f\n",events[i].ms,events[i].thread,events[i].tag,events[i].a,events[i].b,events[i].c);
            std::fclose(f);}
        VirtualFree(events,0,MEM_RELEASE);
    }
};
inline EventLog& event_log(){static EventLog log;return log;}
inline bool events_enabled(){return event_log().enabled;}
inline void evlog(const char* tag,double a=0,double b=0,double c=0){auto& log=event_log();if(log.enabled)log.add(tag,a,b,c);}
inline double evnow(){return EventLog::now();}
}
