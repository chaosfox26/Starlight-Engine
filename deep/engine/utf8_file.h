#pragma once
// Paths that arrive as UTF-16 (command line) are held as UTF-8 text and opened through the wide API, so a folder outside the ANSI code page keeps its name (audit DEEP-1).
#include <cstdio>
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

inline std::string utf8FromWide(const std::wstring& w) {
    int n = w.empty() ? 0 : WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? (size_t)n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
inline errno_t fopenUtf8(FILE** f, const std::string& path, const wchar_t* mode) {
    int n = path.empty() ? 0 : MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), w.data(), n);
    return _wfopen_s(f, w.c_str(), mode);
}
