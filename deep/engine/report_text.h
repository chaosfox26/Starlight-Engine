#pragma once
#include <cstdarg>
#include <cstdio>
#include <string>

// Text helpers for the benchmark report (RND-6): printf into a string of any length, and a JSON string escaper that covers control characters.
namespace report_text {
inline std::string format(const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    va_list again; va_copy(again, args);
    const int length = vsnprintf(nullptr, 0, fmt, args);
    va_end(args);
    std::string out(length > 0 ? (size_t)length : 0, '\0');
    if (length > 0) vsnprintf(&out[0], out.size() + 1, fmt, again);
    va_end(again);
    return out;
}
inline std::string escape(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out.push_back('\\'); out.push_back((char)c); }
        else if (c < 0x20) { char u[8]; snprintf(u, sizeof(u), "\\u%04x", (unsigned)c); out += u; }
        else out.push_back((char)c);
    }
    return out;
}
}
