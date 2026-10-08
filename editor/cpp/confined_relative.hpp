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
}
