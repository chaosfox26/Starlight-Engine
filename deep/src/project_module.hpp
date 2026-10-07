#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>

// A project's "module" must name one file inside the project folder: no drive or root, no separators, no "..", no stream suffix.
inline std::filesystem::path project_module_path(const std::filesystem::path &projectFolder, const std::wstring &name) {
    const std::filesystem::path leaf(name);
    if (name.empty() || name == L"." || name == L".." || name.find_first_of(L"\\/:") != std::wstring::npos || leaf.has_root_path() ||
        leaf.filename() != leaf)
        throw std::runtime_error("Project module must be a file name inside the project folder");
    return (projectFolder / leaf).lexically_normal();
}
