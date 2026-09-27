#pragma once
#include "assets.hpp"
#include "flow.hpp"
#include <cstdint>
namespace starlight::editor {
// Owned by one render session; invalidate whenever its scene geometry changes.
struct RenderSceneCache { std::vector<uint8_t> packet; void clear(){packet.clear();} };
std::vector<uint8_t> render_scene(const SceneData &, const TerrainFlow *flow = nullptr,
                                  const std::vector<MeshPart> &overlays = {}, RenderSceneCache *cache = nullptr);
Mesh object_mesh(const Character &);
} // namespace starlight::editor
