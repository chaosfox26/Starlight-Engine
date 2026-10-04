#pragma once
#include "mesh.hpp"
#include <unordered_map>

namespace starlight::editor {
inline const Mesh& particle_surface(){
    static const Mesh surface=[] {
        Mesh mesh;double t=(1+std::sqrt(5.))*.5;
        for(auto p:std::vector<Vec3>{{-1,t,0},{1,t,0},{-1,-t,0},{1,-t,0},{0,-1,t},{0,1,t},{0,-1,-t},{0,1,-t},{t,0,-1},{t,0,1},{-t,0,-1},{-t,0,1}})mesh.vertices.push_back({p.unit(),p.unit()});
        std::vector<int> faces={0,11,5,0,5,1,0,1,7,0,7,10,0,10,11,1,5,9,5,11,4,11,10,2,10,7,6,7,1,8,3,9,4,3,4,2,3,2,6,3,6,8,3,8,9,4,9,5,2,4,11,6,2,10,8,6,7,9,8,1};
        for(int level=0;level<2;++level){
            std::unordered_map<uint64_t,int> edges;
            auto midpoint=[&](int a,int b){uint64_t key=(uint64_t(std::min(a,b))<<32)|uint32_t(std::max(a,b));auto found=edges.find(key);if(found!=edges.end())return found->second;auto p=(mesh.vertices[a].position+mesh.vertices[b].position).unit();int index=int(mesh.vertices.size());mesh.vertices.push_back({p,p});edges.emplace(key,index);return index;};
            std::vector<int> refined;refined.reserve(faces.size()*4);
            for(size_t i=0;i<faces.size();i+=3){int a=faces[i],b=faces[i+1],c=faces[i+2],ab=midpoint(a,b),bc=midpoint(b,c),ca=midpoint(c,a);refined.insert(refined.end(),{a,ab,ca,b,bc,ab,c,ca,bc,ab,bc,ca});}
            faces=std::move(refined);
        }
        mesh.indices=std::move(faces);
        return mesh;
    }();return surface;
}
inline void append_particle_surface(Mesh& mesh,Vec3 center,Vec3 velocity,double radius,double stretch,uint64_t identity,Vec3 color,float kind){
    if(!std::isfinite(radius)||radius<=0)return;
    stretch=std::clamp(stretch,1.,4.);double transverse=1/std::sqrt(stretch);
    Vec3 along=velocity.length()>1e-8?velocity.unit():Vec3{0,1,0};
    Vec3 side=along.cross(std::abs(along.y)<.9?Vec3{0,1,0}:Vec3{1,0,0}).unit(),across=side.cross(along);
    const auto& source=particle_surface();int base=int(mesh.vertices.size());uint64_t vertex=0,prefix=vertex_motion_prefix(1,identity);
    for(const auto& sample:source.vertices){auto p=sample.position;auto offset=(side*(p.x*transverse)+along*(p.y*stretch)+across*(p.z*transverse))*radius;
        auto normal=(side*(p.x/transverse)+along*(p.y/stretch)+across*(p.z/transverse)).unit();
        mesh.vertices.push_back({center+offset,normal,color,kind,vertex_motion_suffix(prefix,vertex++)});
    }
    for(auto index:source.indices)mesh.indices.push_back(base+index);
}
}
