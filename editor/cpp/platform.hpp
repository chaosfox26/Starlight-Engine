#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "scene.hpp"
#include <filesystem>
#include <functional>
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
std::string hash_bytes(std::string_view);
void atomic_write(const fs::path &, std::string_view, bool backup = false,
                  const std::optional<std::string> &expected = {});
fs::path application_data();
void diagnostic(std::string_view, std::string_view) noexcept;
std::vector<fs::path> file_dialog(HWND owner, std::string_view purpose,
                                  const std::vector<std::pair<std::wstring, std::wstring>> &filters,
                                  bool save = false, bool multiple = false, const fs::path &fallback = {},
                                  std::wstring_view title = {}, bool folders = false);
// Returns the SHA-256 of the exact bytes written, so callers need not read the file back (a lock or a concurrent write between save and re-read left their bookkeeping wrong).
// Thrown by save_scene when the scene names assets that are not in the asset library; `ids` lists them. Nothing has been written.
struct MissingAssetsError : std::runtime_error {
    std::vector<std::string> ids;
    MissingAssetsError(const std::string &message, std::vector<std::string> missing) : std::runtime_error(message), ids(std::move(missing)) {}
};
std::string save_scene(const SceneData &, const fs::path &, const std::optional<std::string> &expected = {}, bool allowMissingAssets = false);
// `hash`, when given, receives the hash of the exact bytes that were decoded (not of a second read of the file).
SceneData load_scene(const fs::path &, std::string *hash = nullptr);
json export_playable_game(const SceneData &, const fs::path &destination, const fs::path &runtime);
json export_portable_scene(const SceneData &, const fs::path &destination);
json verify_portable_scene(const fs::path &folder);
void verify_exported_content(const fs::path &folder);
json export_scene_selection(const SceneData &, const std::vector<std::string>& ids, const fs::path &destination);
std::optional<size_t> repair_scene_models(SceneData &, const std::function<std::optional<fs::path>(const std::string& missingId, const std::string& objectName)>& choose);
} // namespace starlight::editor
