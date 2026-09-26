#include "asset_library.hpp"
#include <cwctype>
#include <chrono>
namespace starlight::editor {
namespace {
std::wstring lower(std::wstring value) {
    for (auto &c : value) c = wchar_t(std::towlower(c));
    return value;
}
}
AssetIndex scan_asset_library(const fs::path& folder) {
    auto root = fs::canonical(folder);
    if (!fs::is_directory(root)) throw std::runtime_error("Choose a folder for the asset browser");
    AssetIndex result;
    std::error_code error;
    fs::recursive_directory_iterator current(root, fs::directory_options::none, error), end;
    if (error) throw std::runtime_error("Cannot read asset folder");
    size_t visited = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (current != end) {
        if (++visited > 50000 || result.entries.size() >= 10000 || std::chrono::steady_clock::now() >= deadline) {
            result.partial = true; break;
        }
        const auto entry = *current;
        const auto attributes = GetFileAttributesW(entry.path().c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) { ++result.errors; result.partial = true; current.disable_recursion_pending(); }
        else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) { current.disable_recursion_pending(); }
        else if (entry.is_directory(error)) {
            if (current.depth() >= 8) { current.disable_recursion_pending(); result.partial = true; }
        } else if (entry.is_regular_file(error)) {
            auto extension = lower(entry.path().extension().wstring());
            std::wstring kind = extension == L".glb" ? L"Model" : extension == L".starobject" ? L"Object" :
                extension == L".stargroup" ? L"Group" : extension == L".starscene" ? L"Scene objects" : L"";
            if (!kind.empty()) result.entries.push_back({entry.path(), kind + L" · " + entry.path().lexically_relative(root).generic_wstring()});
        }
        if (error) { ++result.errors; result.partial = true; error.clear(); }
        current.increment(error);
        if (error) { ++result.errors; result.partial = true; error.clear(); }
    }
    std::sort(result.entries.begin(), result.entries.end(), [](const auto &a, const auto &b) { return lower(a.label) < lower(b.label); });
    return result;
}
std::vector<size_t> filter_asset_library(const AssetIndex& index, std::wstring_view query, std::wstring_view extension) {
    auto needle = lower(std::wstring(query));
    auto type = lower(std::wstring(extension));
    std::vector<size_t> result;
    for (size_t i = 0; i < index.entries.size(); ++i)
        if ((type.empty() || lower(index.entries[i].path.extension().wstring()) == type) &&
            lower(index.entries[i].label).find(needle) != std::wstring::npos) result.push_back(i);
    return result;
}
}
