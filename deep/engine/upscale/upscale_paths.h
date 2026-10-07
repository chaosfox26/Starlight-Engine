#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>

namespace up {

inline std::wstring parentOf(const std::wstring& path) {
    size_t k = path.find_last_of(L"\\/");
    return k == std::wstring::npos ? std::wstring(L".") : path.substr(0, k);
}

// Full module path, grown until it fits; empty on failure or when the path was cut.
inline std::wstring modulePath(HMODULE module) {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(module, buffer.data(), DWORD(buffer.size()));
        if (n == 0) return std::wstring();
        if (n < buffer.size()) return std::wstring(buffer.data(), n);
        if (buffer.size() >= 32768) return std::wstring();
        buffer.resize(buffer.size() * 2);
    }
}

inline std::wstring processDir() {
    return parentOf(modulePath(nullptr));
}

inline std::wstring hostModuleDir() {
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&hostModuleDir), &m) || !m) return std::wstring();
    std::wstring path = modulePath(m);
    return path.empty() ? std::wstring() : parentOf(path);
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
    // Full path only, with the vendor folder and the system folders as the search places. A bare-name fallback would also search the current folder and PATH.
    return LoadLibraryExW((dir + L"\\" + dll).c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}

}
