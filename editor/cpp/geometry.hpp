#pragma once
#include "mesh.hpp"
namespace starlight::editor {
// Compact GPU-resident attack-fill instance. The CPU keeps gameplay/hit testing.
struct GpuTelegraph {float x=0,y=0,z=0,aimX=0,aimZ=-1,range=3,halfAngle=.785398163f,progress=0,r=1,g=1,b=1;uint64_t identity=0;};
static_assert(offsetof(GpuTelegraph,identity)>=11*sizeof(float));
struct MeshPart {
    Mesh mesh;
    Vec3 color{1, 1, 1};
    bool emissive = false;
    std::optional<GpuTelegraph> gpuTelegraph;
};
std::vector<MeshPart> shape_geometry(const Character &);
std::vector<MeshPart> terrain_geometry(const TerrainData &);
void stamp_shape(TerrainData &, const Character &);
Vec3 rgb(std::string_view);
} // namespace starlight::editor
