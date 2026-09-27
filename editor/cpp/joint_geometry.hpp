#pragma once
#include "interactions.hpp"
namespace starlight::editor {
inline std::vector<MeshPart> joint_geometry(const SceneData& current,const SceneData& authored){
    uint64_t jointId=0,vertexIndex=0;double radius=.018;
    MeshPart part;part.color={.55,.45,.25};
    auto line=[&](Vec3 a,Vec3 b){auto direction=(b-a).unit();for(auto axis:{Vec3{1,0,0},Vec3{0,1,0}}){auto side=direction.cross(axis);if(side.length()<1e-8)continue;side=side.unit()*radius;int n=int(part.mesh.vertices.size());for(auto p:{a-side,a+side,b+side,b-side})part.mesh.vertices.push_back({p,{}, {1,1,1},0,vertex_motion_identity(7,jointId,vertexIndex++)});part.mesh.indices.insert(part.mesh.indices.end(),{n,n+1,n+2,n,n+2,n+3,n+2,n+1,n,n+3,n+2,n});}};
    for(const auto& c:current.Characters)if(c.JointKind==3||c.JointKind==4){
        jointId=render_identity(c.Id,0);vertexIndex=0;radius=c.JointSegments?c.JointRadius:.018;
        auto original=std::find_if(authored.Characters.begin(),authored.Characters.end(),[&](const auto& o){return o.Id==c.Id;});if(original==authored.Characters.end())continue;
        auto a=world_point(c,{c.JointX,c.JointY,c.JointZ});Vec3 b{original->X+c.JointTargetX,original->Y+c.JointTargetY,original->Z+c.JointTargetZ};
        if(!c.JointTargetId.empty()){auto target=std::find_if(current.Characters.begin(),current.Characters.end(),[&](const auto& o){return o.Id==c.JointTargetId;});if(target==current.Characters.end())continue;b=world_point(*target,{c.JointTargetX,c.JointTargetY,c.JointTargetZ});}
        if(c.JointRuntimePoints.size()>1){for(size_t i=1;i<c.JointRuntimePoints.size();++i){auto p=c.JointRuntimePoints[i-1],q=c.JointRuntimePoints[i];line({p[0],p[1],p[2]},{q[0],q[1],q[2]});}continue;}
        auto direction=(b-a).unit();auto side=direction.cross({0,1,0});if(side.length()<1e-6)side=direction.cross({1,0,0});side=side.unit();auto other=direction.cross(side);auto previous=a;double slack=std::sqrt(std::max(0.,c.JointLength*c.JointLength-(b-a).dot(b-a)))*.35;
        for(int i=1;i<=48;++i){double t=i/48.;Vec3 p=a+(b-a)*t;if(c.JointKind==3)p.y-=4*t*(1-t)*slack;else p=p+(side*std::cos(t*50.265482457)+other*std::sin(t*50.265482457))*(.08*std::sin(t*3.141592654));line(previous,p);previous=p;}
    }if(part.mesh.indices.empty())return {};part.mesh.normals();return {std::move(part)};
}
}
