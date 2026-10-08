#pragma once
#include "mesh.hpp"
#include "mesh_reduce.hpp"

namespace starlight::editor {
inline const Mesh& grass_surface(){
    static const Mesh source=[] {
        Mesh mesh;uint32_t state=8241;auto next=[&]{state^=state<<13;state^=state>>17;state^=state<<5;return double(state&0xffffff)/16777216.;};constexpr double pi=3.14159265358979323846;
        for(int blade=0;blade<100;++blade){
            double angle=next()*pi*2,radius=std::sqrt(next())*.48,x=std::cos(angle)*radius,z=std::sin(angle)*radius;
            double height=.18+next()*.42,lean=(next()-.5)*.22,turn=next()*pi*2,variation=next();
            Vec3 side{std::cos(turn),0,-std::sin(turn)},forward{std::sin(turn),0,std::cos(turn)};int base=int(mesh.vertices.size());
            for(int ring=0;ring<7;++ring){double t=ring/7.,width=.004*(1-t)+.018*std::sin(pi*std::pow(t,.65));
                auto center=Vec3{x+lean*t*t,height*t,z}+forward*(height*.15*t*t*t);
                for(int column=0;column<3;++column){double u=column*.5;auto p=center+side*((u*2-1)*width)+forward*(column==1?width*.28:0);mesh.vertices.push_back({p,{}, {u,t,variation},1.35f});}
            }
            auto tip=Vec3{x+lean,height,z}+forward*(height*.15);mesh.vertices.push_back({tip,{}, {.5,1,variation},1.35f});
            for(int ring=0;ring<6;++ring)for(int column=0;column<2;++column){int a=base+ring*3+column,b=a+1,c=a+3,d=c+1;mesh.indices.insert(mesh.indices.end(),{a,c,b,b,c,d});}
            mesh.indices.insert(mesh.indices.end(),{base+18,base+21,base+19,base+19,base+21,base+20});
        }
        mesh=reduce_mesh(mesh,mesh_detail());mesh.normals();return mesh; // full detail unless STARLIGHT_MESH_DETAIL asks for fewer triangles
    }();return source;
}
}
