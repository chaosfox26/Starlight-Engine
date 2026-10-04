#pragma once
#include "geometry.hpp"
#include <unordered_map>
namespace starlight::editor {
template<class SurfaceVertex>
void refine_terrain_surface(Mesh& mesh,const TerrainData& terrain,SurfaceVertex surfaceVertex){
    auto key=[](int a,int b){return (uint64_t(std::min(a,b))<<32)|uint32_t(std::max(a,b));};
    for(int pass=0;pass<2;++pass){
        std::unordered_map<uint64_t,int> splits;
        auto originalVertices=mesh.vertices.size();
        for(size_t i=0;i<mesh.indices.size();i+=3)for(int edge=0;edge<3;++edge){
            int a=mesh.indices[i+edge],b=mesh.indices[i+(edge+1)%3];auto id=key(a,b);
            if(splits.contains(id))continue;
            auto va=mesh.vertices[a],vb=mesh.vertices[b];auto midpoint=(va.position+vb.position)*.5;
            double length=(vb.position-va.position).length(),error=std::abs(terrain.density_at(midpoint.x,midpoint.y,midpoint.z));
            if(length<.035 || (error<.001 && va.normal.dot(vb.normal)>.9975)){splits.emplace(id,-1);continue;}
            auto p=midpoint;
            for(int iteration=0;iteration<5;++iteration){
                double density=terrain.density_at(p.x,p.y,p.z);if(std::abs(density)<1e-6)break;
                constexpr double e=.002;
                Vec3 gradient{terrain.density_at(p.x+e,p.y,p.z)-terrain.density_at(p.x-e,p.y,p.z),terrain.density_at(p.x,p.y+e,p.z)-terrain.density_at(p.x,p.y-e,p.z),terrain.density_at(p.x,p.y,p.z+e)-terrain.density_at(p.x,p.y,p.z-e)};
                gradient=gradient/(2*e);double squared=gradient.dot(gradient);if(squared<1e-12)break;
                auto offset=gradient*(density/squared);double size=offset.length();if(size>length*.25)offset=offset*(length*.25/size);
                p=p-offset;
            }
            int vertex=int(mesh.vertices.size());mesh.vertices.push_back(surfaceVertex(p));splits.emplace(id,vertex);
        }
        if(mesh.vertices.size()==originalVertices)break;
        std::vector<int> indices;indices.reserve(mesh.indices.size()*2);
        auto triangle=[&](int a,int b,int c){indices.insert(indices.end(),{a,b,c});};
        for(size_t i=0;i<mesh.indices.size();i+=3){
            int v[]={mesh.indices[i],mesh.indices[i+1],mesh.indices[i+2]};
            int m[]={splits.at(key(v[0],v[1])),splits.at(key(v[1],v[2])),splits.at(key(v[2],v[0]))};
            int count=(m[0]>=0)+(m[1]>=0)+(m[2]>=0);
            if(count==0){triangle(v[0],v[1],v[2]);continue;}
            if(count==3){triangle(v[0],m[0],m[2]);triangle(m[0],v[1],m[1]);triangle(m[2],m[1],v[2]);triangle(m[0],m[1],m[2]);continue;}
            int origin=0;
            if(count==1){while(m[origin]<0)++origin;int a=v[origin],b=v[(origin+1)%3],c=v[(origin+2)%3],ab=m[origin];triangle(a,ab,c);triangle(ab,b,c);}
            else{while(m[origin]>=0)++origin;int a=v[origin],b=v[(origin+1)%3],c=v[(origin+2)%3],bc=m[(origin+1)%3],ca=m[(origin+2)%3];triangle(c,ca,bc);triangle(a,b,ca);triangle(b,bc,ca);}
        }
        mesh.indices=std::move(indices);
    }
}
}
