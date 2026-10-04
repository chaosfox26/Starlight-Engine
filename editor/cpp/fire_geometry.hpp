#pragma once
#include "geometry.hpp"
namespace starlight::editor {
inline MeshPart fire_volume(Vec3 tint) {
    MeshPart part;part.color=tint;part.emissive=true;
    for(int i=0;i<8;++i)part.mesh.vertices.push_back({{i&1?.6:-.6,i&2?2.2:0.,i&4?.6:-.6},{},{1,1,1},12});
    part.mesh.indices={0,4,6,0,6,2,1,3,7,1,7,5,0,1,5,0,5,4,2,6,7,2,7,3,0,2,3,0,3,1,4,5,7,4,7,6};
    part.mesh.normals();return part;
}
}
