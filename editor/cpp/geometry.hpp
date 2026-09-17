#pragma once
#include "mesh.hpp"
namespace starlight::editor {
struct MeshPart {
    Mesh mesh;
    Vec3 color{1, 1, 1};
    bool emissive = false;
};
std::vector<MeshPart> shape_geometry(const Character &);
std::vector<MeshPart> terrain_geometry(const TerrainData &);
void stamp_shape(TerrainData &, const Character &);
Vec3 rgb(std::string_view);
} // namespace starlight::editor
