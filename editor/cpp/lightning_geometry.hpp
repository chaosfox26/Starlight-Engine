#pragma once
#include "mesh.hpp"
#include <numbers>

namespace starlight::editor {
// Midpoint displacement produces connected kinks at several scales, rather
// than independent jitter at every sample. Endpoints are never displaced.
template<class Random>
std::vector<Vec3> lightning_path(Vec3 a,Vec3 b,double spread,int depth,Random& random){
    auto direction=(b-a).unit(),side=direction.cross({0,1,0});
    if(side.length()<1e-6)side=direction.cross({1,0,0});
    side=side.unit();auto other=direction.cross(side);
    std::vector<Vec3> path{a};
    auto divide=[&](auto&& self,Vec3 start,Vec3 end,int level,double amplitude)->void{
        if(!level){path.push_back(end);return;}
        auto middle=(start+end)*.5+(side*random()+other*random())*amplitude;
        self(self,start,middle,level-1,amplitude*.52);
        self(self,middle,end,level-1,amplitude*.52);
    };
    divide(divide,a,b,depth,spread);return path;
}

inline Mesh lightning_channel(const std::vector<Vec3>& input,double radius,bool branch,uint64_t identity=0){
    std::vector<Vec3> path;
    for(auto p:input)if(path.empty()||(p-path.back()).length()>1e-7)path.push_back(p);
    Mesh mesh;if(path.size()<2)return mesh;
    constexpr int sides=12;Vec3 previousSide{};double distance=0,total=0;
    for(size_t i=1;i<path.size();++i)total+=(path[i]-path[i-1]).length();
    mesh.vertices.reserve(path.size()*sides+2);
    auto add=[&](Vec3 p,Vec3 n,double progress){auto index=mesh.vertices.size();mesh.vertices.push_back({p,n,{progress,branch?1.:0.,1},9.35f,identity?vertex_motion_identity(2,identity,index):0});};
    for(size_t i=0;i<path.size();++i){
        if(i)distance+=(path[i]-path[i-1]).length();double progress=distance/total;
        auto tangent=(path[std::min(i+1,path.size()-1)]-path[i?i-1:0]).unit();
        // Transport each ring's frame to avoid twists at sharp kinks.
        auto side=previousSide-tangent*previousSide.dot(tangent);
        if(side.length()<1e-6){side=tangent.cross({0,1,0});if(side.length()<1e-6)side=tangent.cross({1,0,0});}
        side=side.unit();previousSide=side;auto other=tangent.cross(side);
        double width=radius*(1-(branch?.65:.3)*progress);
        for(int j=0;j<sides;++j){double angle=j*2*std::numbers::pi/sides;auto normal=side*std::cos(angle)+other*std::sin(angle);add(path[i]+normal*width,normal,progress);}
        if(i)for(int j=0;j<sides;++j){int a=int((i-1)*sides+j),b=int(i*sides+j),c=int((i-1)*sides+(j+1)%sides),d=int(i*sides+(j+1)%sides);mesh.indices.insert(mesh.indices.end(),{a,c,b,c,d,b});}
    }
    int first=int(mesh.vertices.size());add(path.front(),(path.front()-path[1]).unit(),0);
    int last=int(mesh.vertices.size());add(path.back(),(path.back()-path[path.size()-2]).unit(),1);
    int end=int((path.size()-1)*sides);
    for(int j=0;j<sides;++j)mesh.indices.insert(mesh.indices.end(),{first,(j+1)%sides,j,last,end+j,end+(j+1)%sides});
    return mesh;
}
}
