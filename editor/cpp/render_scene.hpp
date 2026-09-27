#pragma once
#include "assets.hpp"
#include "flow.hpp"
#include <cstdint>
namespace starlight::editor {
inline uint64_t render_identity(std::string_view id,uint32_t part){uint64_t hash=1469598103934665603ull;for(unsigned char c:id){hash^=c;hash*=1099511628211ull;}for(int i=0;i<4;++i){hash^=(part>>(i*8))&255;hash*=1099511628211ull;}return hash?hash:1;}
// Owned by one render session; invalidate whenever its scene geometry changes.
struct RenderSceneCache { std::vector<uint8_t> packet; bool runtimeEffects=false,simulatedLiquids=false; void clear(){packet.clear();} };
std::vector<uint8_t> render_scene(const SceneData &, const TerrainFlow *flow = nullptr,
                                  const std::vector<MeshPart> &overlays = {}, RenderSceneCache *cache = nullptr,
                                  bool runtimeEffects = false);
std::vector<MeshPart> object_parts(const Character &);
Mesh object_mesh(const Character &);
} // namespace starlight::editor
