#pragma once
#include <windows.h>
#include <dbghelp.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#pragma comment(lib,"dbghelp.lib")

namespace starlight_diagnostics {
inline wchar_t dumpPath[32768]{};
inline LONG WINAPI unhandled(EXCEPTION_POINTERS* exception) {
 HANDLE file=CreateFileW(dumpPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file!=INVALID_HANDLE_VALUE){MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),exception,FALSE};MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),file,MiniDumpNormal,&info,nullptr,nullptr);CloseHandle(file);}
 return EXCEPTION_CONTINUE_SEARCH;
}
inline std::filesystem::path initialize(std::wstring_view component=L"Engine"){
 wchar_t local[32768]{};DWORD n=GetEnvironmentVariableW(L"STARLIGHT_DATA_ROOT",local,32768);
 std::filesystem::path root;
 if(n){
  if(n>=32768)throw std::runtime_error("Starlight data directory is too long");
  root=local;
  if(!root.is_absolute())throw std::runtime_error("Starlight data directory must be absolute");
 }else{
  n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
  if(!n||n>=32768)throw std::runtime_error("LOCALAPPDATA is unavailable for diagnostics");
  root=std::filesystem::path(local)/L"Starlight";
 }
 auto dir=root/L"Diagnostics"/component;std::filesystem::create_directories(dir);
 auto dump=dir/(L"Starlight."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L".dmp");
 wcsncpy_s(dumpPath,dump.c_str(),_TRUNCATE);SetUnhandledExceptionFilter(unhandled);
 return dir/(L"launch-"+std::to_wstring(GetCurrentProcessId())+L".json");
}
}
