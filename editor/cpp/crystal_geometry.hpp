#pragma once
#include "mesh.hpp"
#include <numbers>
namespace starlight::editor {
inline const Mesh& crystal_surface(){
    static const Mesh surface=[] {
        Mesh mesh;std::vector<Vec3> outline;
        for(int corner=0;corner<6;++corner){double angle=corner*std::numbers::pi/3;Vec3 at{std::cos(angle),0,std::sin(angle)},before{std::cos(angle-std::numbers::pi/3),0,std::sin(angle-std::numbers::pi/3)},after{std::cos(angle+std::numbers::pi/3),0,std::sin(angle+std::numbers::pi/3)};outline.push_back(at*.9+before*.1);outline.push_back(at*.9+after*.1);}
        auto face=[&](std::vector<Vec3> points){int first=int(mesh.vertices.size());Vec3 normal;for(size_t i=1;i+1<points.size();++i)normal=normal+(points[i]-points[0]).cross(points[i+1]-points[0]);normal=normal.unit();for(auto p:points)mesh.vertices.push_back({p,normal,{1,1,1},11});for(int i=1;i+1<int(points.size());++i)mesh.indices.insert(mesh.indices.end(),{first,first+i,first+i+1});};
        std::vector<std::vector<Vec3>> rings;
        struct Ring {double height,radius;Vec3 shift;};
        for(auto ring:std::vector<Ring>{{0,.48,{}},{.06,.55,{}},{1.6,.5,{.025,0,.035}},{1.66,.495,{.025,0,.035}},{2.32,.04,{.11,0,.02}}}){std::vector<Vec3> points;for(auto p:outline)points.push_back(p*ring.radius+Vec3{0,ring.height,0}+ring.shift);rings.push_back(std::move(points));}
        face(rings.front());
        for(size_t row=0;row+1<rings.size();++row)for(size_t side=0;side<outline.size();++side){size_t next=(side+1)%outline.size();face({rings[row][side],rings[row+1][side],rings[row+1][next],rings[row][next]});}
        Vec3 tip{.12,2.4,.02};for(size_t side=0;side<outline.size();++side)face({rings.back()[side],tip,rings.back()[(side+1)%outline.size()]});
        return mesh;
    }();return surface;
}
}
