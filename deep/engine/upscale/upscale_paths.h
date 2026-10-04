#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace up {

inline std::wstring parentOf(const std::wstring& path) {
    size_t k = path.find_last_of(L"\\/");
    return k == std::wstring::npos ? std::wstring(L".") : path.substr(0, k);
}

inline std::wstring processDir() {
    wchar_t exe[MAX_PATH]; DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return parentOf(std::wstring(exe, n));
}

inline std::wstring hostModuleDir() {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&hostModuleDir), &m) || !m) return std::wstring();
    wchar_t path[MAX_PATH]; DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
    return n ? parentOf(std::wstring(path, n)) : std::wstring();
}

inline bool fileExists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

inline std::wstring vendorDir(const wchar_t* probeDll) {
    std::wstring host = hostModuleDir();
    if (!host.empty() && fileExists(host + L"\\" + probeDll)) {
        SetDllDirectoryW(host.c_str());
        return host;
    }
    return processDir();
}

inline HMODULE loadVendorModule(const wchar_t* dll) {
    std::wstring dir = vendorDir(dll);
    HMODULE m = LoadLibraryExW((dir + L"\\" + dll).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    return m ? m : LoadLibraryW(dll);
}

}
