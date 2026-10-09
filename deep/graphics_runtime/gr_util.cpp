#include "graphics_runtime.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>

namespace starlight::gfxrt {

std::string utf8(const fs::path& p) {
    auto u = p.u8string();
    return std::string(reinterpret_cast<const char*>(u.data()), u.size());
}
fs::path from_utf8(std::string_view s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

static fs::path long_path(const fs::path& p) {
    auto full = fs::absolute(p).lexically_normal().wstring();
    if (full.rfind(L"\\\\?\\", 0) == 0) return full;
    if (full.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + full.substr(2);
    return L"\\\\?\\" + full;
}

std::string sha256_bytes(std::string_view data) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw GrError("SHA-256 provider unavailable");
    BCRYPT_HASH_HANDLE h = nullptr;
    unsigned char out[32]{};
    NTSTATUS st = BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0);
    std::size_t at = 0;
    while (st >= 0 && at < data.size()) {
        auto n = static_cast<ULONG>(std::min<std::size_t>(data.size() - at, 1u << 24));
        st = BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data() + at)), n, 0);
        at += n;
    }
    if (st >= 0) st = BCryptFinishHash(h, out, 32, 0);
    if (h) BCryptDestroyHash(h);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st < 0) throw GrError("SHA-256 failed");
    static const char* hex = "0123456789abcdef";
    std::string s;
    for (auto b : out) { s += hex[b >> 4]; s += hex[b & 15]; }
    return s;
}

std::string read_text(const fs::path& p) {
    std::ifstream f(long_path(p), std::ios::binary);
    if (!f) throw GrError("Cannot open " + utf8(p.filename()));
    std::string s((std::istreambuf_iterator<char>(f)), {});
    if (f.bad()) throw GrError("Read failed for " + utf8(p.filename()));
    return s;
}

std::string sha256_file(const fs::path& p) {
    return sha256_bytes(read_text(p));
}

std::uint32_t crc32_bytes(std::string_view data) {
    static const auto table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

std::string fold_name(std::string_view s) {
    std::string folded(s);
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    int n = s.empty() ? 0 : MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return folded;
    std::wstring wide(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), wide.data(), n);
    CharLowerBuffW(wide.data(), static_cast<DWORD>(n));
    int m = WideCharToMultiByte(CP_UTF8, 0, wide.data(), n, nullptr, 0, nullptr, nullptr);
    if (m <= 0) return folded;
    std::string out(static_cast<std::size_t>(m), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), n, out.data(), m, nullptr, nullptr);
    return out;
}

bool safe_relative_path(std::string_view path, std::string& why) {
    if (path.empty()) { why = "empty path"; return false; }
    if (path.size() > 240) { why = "path too long"; return false; }
    if (path.front() == '/' ) { why = "absolute path"; return false; }
    std::size_t start = 0;
    while (start <= path.size()) {
        auto end = path.find('/', start);
        auto seg = path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (seg.empty()) { why = "empty path segment"; return false; }
        if (seg == "." || seg == "..") { why = "dot segment"; return false; }
        if (seg.back() == '.' || seg.back() == ' ') { why = "trailing dot or space"; return false; }
        for (unsigned char ch : seg) {
            if (ch < 32 || ch == '\\' || ch == ':' || ch == '<' || ch == '>' || ch == '"' || ch == '|' || ch == '?' || ch == '*') {
                why = "forbidden character"; return false;
            }
        }
        std::string stem(seg.substr(0, seg.find('.')));
        while (!stem.empty() && stem.back() == ' ') stem.pop_back();
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        static const char* reserved[] = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
        for (auto r : reserved) if (stem == r) { why = "reserved device name"; return false; }
        if (stem.size() == 4 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] >= '1' && stem[3] <= '9') {
            why = "reserved device name"; return false;
        }
        if (stem.size() == 5 && (stem.rfind("COM", 0) == 0 || stem.rfind("LPT", 0) == 0) && stem[3] == '\xC2' && (stem[4] == '\xB9' || stem[4] == '\xB2' || stem[4] == '\xB3')) {
            why = "reserved device name"; return false;
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

bool is_reparse_or_link(const fs::path& p) {
    auto a = GetFileAttributesW(long_path(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

static void write_synced(const fs::path& path, std::string_view data) {
    HANDLE f = CreateFileW(long_path(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) throw GrError("Cannot create " + utf8(path.filename()));
    bool ok = true;
    std::size_t at = 0;
    while (ok && at < data.size()) {
        DWORD n = 0;
        ok = WriteFile(f, data.data() + at, static_cast<DWORD>(std::min<std::size_t>(data.size() - at, 1u << 24)), &n, nullptr) && n;
        at += n;
    }
    ok = ok && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok) throw GrError("Write failed for " + utf8(path.filename()));
}

void atomic_replace_file(const fs::path& target, std::string_view data, const FaultHook& fault, std::string_view prefix, bool keepBackup) {
    auto call = [&](const char* step) { if (fault) fault(std::string(prefix) + "." + step); };
    fs::create_directories(target.parent_path());
    fs::path tmp = target;
    tmp += L".tmp";
    write_synced(tmp, data);
    call("tmp-written");
    if (keepBackup && fs::exists(target)) {
        fs::path bak = target, bakTmp = target;
        bak += L".bak";
        bakTmp += L".bak.tmp";
        write_synced(bakTmp, read_text(target));
        if (!MoveFileExW(long_path(bakTmp).c_str(), long_path(bak).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw GrError("Backup rename failed");
        call("bak-written");
    }
    if (!MoveFileExW(long_path(tmp).c_str(), long_path(target).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw GrError("Replace failed for " + utf8(target.filename()));
    call("replaced");
}

std::optional<Version> parse_version(std::string_view s) {
    Version v;
    std::size_t at = 0;
    while (true) {
        if (at >= s.size() || !std::isdigit(static_cast<unsigned char>(s[at]))) return std::nullopt;
        unsigned long n = 0;
        int digits = 0;
        while (at < s.size() && std::isdigit(static_cast<unsigned char>(s[at]))) {
            n = n * 10 + (s[at] - '0');
            if (++digits > 9) return std::nullopt;
            ++at;
        }
        v.parts.push_back(static_cast<unsigned>(n));
        if (at == s.size()) break;
        if (s[at] != '.') return std::nullopt;
        ++at;
    }
    if (v.parts.size() < 2 || v.parts.size() > 4) return std::nullopt;
    return v;
}
int Version::compare(const Version& o) const {
    for (std::size_t i = 0; i < std::max(parts.size(), o.parts.size()); ++i) {
        unsigned a = i < parts.size() ? parts[i] : 0, b = i < o.parts.size() ? o.parts[i] : 0;
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;
}
std::string Version::str() const {
    std::string s;
    for (auto p : parts) { if (!s.empty()) s += '.'; s += std::to_string(p); }
    return s;
}

const char* channel_name(Channel c) { return c == Channel::Stable ? "stable" : "experimental"; }
std::optional<Channel> parse_channel(std::string_view s) {
    if (s == "stable") return Channel::Stable;
    if (s == "experimental") return Channel::Experimental;
    return std::nullopt;
}
const char* category_name(Category c) {
    switch (c) {
    case Category::Upscaling: return "upscaling";
    case Category::FrameGeneration: return "frame-generation";
    default: return "latency-reduction";
    }
}
std::optional<Category> parse_category(std::string_view s) {
    if (s == "upscaling") return Category::Upscaling;
    if (s == "frame-generation") return Category::FrameGeneration;
    if (s == "latency-reduction") return Category::LatencyReduction;
    return std::nullopt;
}

EngineApi EngineApi::parse(std::string_view spec) {
    EngineApi api;
    api.major.clear();
    std::size_t at = 0;
    while (at < spec.size()) {
        auto end = spec.find(',', at);
        auto item = spec.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        auto eq = item.find('=');
        if (eq == std::string_view::npos || eq == 0 || eq + 1 >= item.size()) throw GrError("Engine API must look like sl=2,ffx=2");
        api.major[std::string(item.substr(0, eq))] = std::stoi(std::string(item.substr(eq + 1)));
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    return api;
}

fs::path default_installed_root(std::string_view appName) {
    wchar_t buf[MAX_PATH * 2]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, static_cast<DWORD>(std::size(buf)));
    if (!n || n >= std::size(buf)) throw GrError("Per-user application data folder is unavailable");
    return fs::path(buf) / from_utf8(appName) / L"graphics-runtime";
}
fs::path portable_root(const fs::path& adjacent) {
    if (adjacent.empty()) throw GrError("Portable mode needs an explicit adjacent folder");
    return adjacent / L"graphics-runtime-managed";
}

}
