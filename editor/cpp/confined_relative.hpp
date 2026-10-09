#pragma once
#include "platform.hpp"
#include <stdexcept>
#include <string>
namespace starlight::editor {
// An inventory names its output paths; they must stay beneath the stage and archive roots (audit DEP-08).
inline const std::string& confined_relative(const std::string& text) {
    const fs::path path(wide(text));
    bool climbs = false;
    for (const auto& part : path) climbs = climbs || part == L"..";
    if (text.empty() || climbs || path.is_absolute() || path.has_root_name() || path.has_root_directory() || text.find(':') != std::string::npos)
        throw std::runtime_error("Inventory path is not a plain relative path under the output folder: " + text);
    return text;
}
// A batch output must not reach outside the stage or the archive through a junction or symbolic link (audit DEP-08). Every part of the path below the
// root that exists must be a plain file or folder. The root itself is chosen by the person and is not checked.
inline void refuse_links_under(const fs::path& root, const fs::path& target) {
    fs::path part = root;
    for (const auto& piece : target.lexically_relative(root)) {
        part /= piece;
        const auto attributes = GetFileAttributesW(part.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return;
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) throw std::runtime_error("Output path passes through a link or junction: " + utf8(part.wstring()));
    }
}
}
