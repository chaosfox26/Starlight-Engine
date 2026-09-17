#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "scene.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <windows.h>
namespace starlight::editor {
namespace fs = std::filesystem;
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : value(other.value) { other.value = nullptr; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
std::wstring wide(std::string_view);
std::string utf8(std::wstring_view);
std::string read_file(const fs::path &);
std::string hash_file(const fs::path &);
void atomic_write(const fs::path &, std::string_view, bool backup = false,
                  const std::optional<std::string> &expected = {});
fs::path application_data();
void diagnostic(std::string_view, std::string_view) noexcept;
std::vector<fs::path> file_dialog(HWND owner, std::string_view purpose,
                                  const std::vector<std::pair<std::wstring, std::wstring>> &filters,
                                  bool save = false, bool multiple = false, const fs::path &fallback = {});
void save_scene(const SceneData &, const fs::path &, const std::optional<std::string> &expected = {});
} // namespace starlight::editor
