#pragma once
#include "geometry.hpp"
#include <filesystem>
namespace starlight::editor {
std::vector<MeshPart> load_glb(const std::filesystem::path &);
}
