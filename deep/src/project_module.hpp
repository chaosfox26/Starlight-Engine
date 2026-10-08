#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// A project's "module" must name one file inside the project folder: no drive or root, no separators, no "..", no stream suffix.
inline std::filesystem::path project_module_path(const std::filesystem::path &projectFolder, const std::wstring &name) {
    const std::filesystem::path leaf(name);
    if (name.empty() || name == L"." || name == L".." || name.find_first_of(L"\\/:") != std::wstring::npos || leaf.has_root_path() ||
        leaf.filename() != leaf)
        throw std::runtime_error("Project module must be a file name inside the project folder");
    return (projectFolder / leaf).lexically_normal();
}

// The saved default test settings sit in settings/ (old deep/bin layout) or deep/settings/ (source tree and staged package) of the host's folder or one of its
// ancestors; the host is at a different depth in the build tree, the package and the old layout. Nearest folder first.
inline std::vector<std::filesystem::path> default_preset_candidates(const std::filesystem::path &hostFolder) {
    std::vector<std::filesystem::path> found;
    std::filesystem::path folder = hostFolder;
    for (int level = 0; level < 5; ++level) {
        found.push_back(folder / L"settings" / L"default.startest");
        found.push_back(folder / L"deep" / L"settings" / L"default.startest");
        if (!folder.has_parent_path() || folder.parent_path() == folder) break;
        folder = folder.parent_path();
    }
    return found;
}
inline std::filesystem::path find_default_preset(const std::filesystem::path &hostFolder) {
    std::error_code ignored;
    for (const auto &candidate : default_preset_candidates(hostFolder))
        if (std::filesystem::is_regular_file(candidate, ignored)) return candidate;
    return {};
}
