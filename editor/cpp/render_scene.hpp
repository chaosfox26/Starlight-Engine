#pragma once
#include "assets.hpp"
#include "flow.hpp"
#include <cstdint>
namespace starlight::editor {
std::vector<uint8_t> render_scene(const SceneData &, const TerrainFlow *flow = nullptr,
                                  const std::vector<MeshPart> &overlays = {});
Mesh object_mesh(const Character &);
} // namespace starlight::editor
