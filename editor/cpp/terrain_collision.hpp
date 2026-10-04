#pragma once
#include "terrain_volume.hpp"
extern "C" __declspec(dllimport) int slp_terrain_mesh(void*,const float*,int,const uint32_t*,int);
namespace starlight::editor {
inline bool install_terrain_volume_collision(void* world,const TerrainData& terrain){
    auto mesh=terrain_volume_geometry(terrain);std::vector<float> points;points.reserve(mesh.vertices.size()*3);
    for(const auto& v:mesh.vertices)points.insert(points.end(),{float(v.position.x),float(v.position.y),float(v.position.z)});
    std::vector<uint32_t> indices(mesh.indices.begin(),mesh.indices.end());
    return slp_terrain_mesh(world,points.data(),int(mesh.vertices.size()),indices.data(),int(indices.size()))!=0;
}
}
