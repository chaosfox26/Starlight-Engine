#pragma once
#include "render_scene.hpp"
#include <unordered_map>
namespace starlight::editor {
struct Bounds {
    Vec3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
};
Bounds object_bounds(const Character &);
void move_group(SceneData &, const Character &, const std::unordered_map<std::string, Character> &);
void snap_object(SceneData &, Character &, bool grid, bool objects, bool ground);
std::optional<Vec3> terrain_hit(const TerrainData *, Vec3 origin, Vec3 ray);
struct GizmoSegment {
    Vec3 a, b;
    int axis;
};
std::vector<GizmoSegment> gizmo_segments(const Character &, std::string_view mode);
std::vector<MeshPart> gizmo_geometry(const Character &, std::string_view mode);
std::vector<MeshPart> editor_overlays(const SceneData &, std::string_view selected, std::string_view tool,
                                      bool playing);
} // namespace starlight::editor
