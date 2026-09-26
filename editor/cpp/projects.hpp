#pragma once
#include "platform.hpp"
namespace starlight::editor {
struct ProjectData {
    fs::path path;
    std::vector<std::string> scenes;
    std::string selected;
    static ProjectData load(const fs::path &);
};
struct ResolvedPreset {
    fs::path path, plan;
    json data;
};
ResolvedPreset resolve_preset(const fs::path &engine, const ProjectData &, const fs::path &source);
void launch_project(const fs::path &engine, const ProjectData &, const fs::path &preset);
} // namespace starlight::editor
