#pragma once
#include "mesh.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>
namespace starlight::editor {
// The axis-aligned box of a transformed mesh is the box of its transformed convex-hull vertices, so a posed box can be measured from these points instead of
// every vertex. Returns the hull's vertex positions, or an empty vector whenever the hull cannot be built and verified (too few points, flat or degenerate
// input, a point found outside the finished hull); the caller then keeps using every vertex, so a failure here can never change an answer.
inline std::vector<Vec3> convex_hull_vertices(const std::vector<Vec3>& in){
    struct Face{int a,b,c;Vec3 n;double d=0;std::vector<int> out;bool dead=false;};
    const size_t count=in.size();
    if(count<16)return {};
    Vec3 low=in[0],high=in[0];
    for(const Vec3& p:in){low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}
    const double scale=std::max({high.x-low.x,high.y-low.y,high.z-low.z});
    if(!(scale>0)||!std::isfinite(scale))return {};
    const double eps=1e-9*scale;
    auto plane=[&](Face& f){f.n=(in[f.b]-in[f.a]).cross(in[f.c]-in[f.a]).unit();f.d=f.n.dot(in[f.a]);};
    auto above=[&](const Face& f,Vec3 p){return f.n.dot(p)-f.d;};
    size_t i0=0,i1=0;
    for(size_t i=1;i<count;++i){if(in[i].x<in[i0].x)i0=i;if(in[i].x>in[i1].x)i1=i;}
    if(i0==i1)return {};
    const Vec3 axis=(in[i1]-in[i0]).unit();
    size_t i2=0;double best=-1;
    for(size_t i=0;i<count;++i){double dist=(in[i]-in[i0]).cross(axis).length();if(dist>best){best=dist;i2=i;}}
    if(!(best>eps*100))return {};
    const Vec3 base=(in[i1]-in[i0]).cross(in[i2]-in[i0]).unit();
    size_t i3=0;best=-1;
    for(size_t i=0;i<count;++i){double dist=std::abs(base.dot(in[i]-in[i0]));if(dist>best){best=dist;i3=i;}}
    if(!(best>eps*100))return {};
    std::vector<Face> faces;
    const Vec3 center=(in[i0]+in[i1]+in[i2]+in[i3])*.25;
    auto add=[&](int a,int b,int c){Face f;f.a=a;f.b=b;f.c=c;plane(f);if(above(f,center)>0){std::swap(f.b,f.c);plane(f);}faces.push_back(std::move(f));};
    add(int(i0),int(i1),int(i2));add(int(i0),int(i3),int(i1));add(int(i1),int(i3),int(i2));add(int(i2),int(i3),int(i0));
    std::vector<char> used(count,0);used[i0]=used[i1]=used[i2]=used[i3]=1;
    for(size_t i=0;i<count;++i){
        if(used[i])continue;
        for(Face& f:faces)if(above(f,in[i])>eps){f.out.push_back(int(i));break;}
    }
    for(size_t guard=0;guard<200000;++guard){
        int pick=-1;
        for(size_t i=0;i<faces.size();++i)if(!faces[i].dead&&!faces[i].out.empty()){pick=int(i);break;}
        if(pick<0)break;
        int apex=-1;double farthest=-1;
        for(int idx:faces[pick].out){double dist=above(faces[pick],in[idx]);if(dist>farthest){farthest=dist;apex=idx;}}
        std::vector<int> visible;
        for(size_t i=0;i<faces.size();++i)if(!faces[i].dead&&above(faces[i],in[apex])>eps)visible.push_back(int(i));
        if(visible.empty())return {};
        std::unordered_set<uint64_t> edges;
        auto key=[&](int u,int v){return (uint64_t(uint32_t(u))<<32)|uint32_t(v);};
        for(int vi:visible){const Face& f=faces[vi];edges.insert(key(f.a,f.b));edges.insert(key(f.b,f.c));edges.insert(key(f.c,f.a));}
        std::vector<std::pair<int,int>> horizon;
        for(int vi:visible){
            const Face& f=faces[vi];
            const int e[3][2]={{f.a,f.b},{f.b,f.c},{f.c,f.a}};
            for(auto& edge:e)if(!edges.count(key(edge[1],edge[0])))horizon.emplace_back(edge[0],edge[1]);
        }
        std::vector<int> orphans;
        for(int vi:visible){for(int idx:faces[vi].out)if(idx!=apex)orphans.push_back(idx);faces[vi].dead=true;faces[vi].out.clear();}
        used[apex]=1;
        const size_t first=faces.size();
        for(auto& edge:horizon){Face f;f.a=edge.first;f.b=edge.second;f.c=apex;plane(f);faces.push_back(std::move(f));}
        for(int idx:orphans)for(size_t i=first;i<faces.size();++i)if(above(faces[i],in[idx])>eps){faces[i].out.push_back(idx);break;}
        if(faces.size()>count*8+64)return {};
    }
    std::vector<char> onHull(count,0);
    for(const Face& f:faces)if(!f.dead){onHull[f.a]=onHull[f.b]=onHull[f.c]=1;}
    for(const Face& f:faces)if(!f.dead&&!f.out.empty())return {};
    // Verification: every input point must lie inside the finished hull, within a tolerance far smaller than any collision margin.
    const double tolerance=1e-7*scale;
    for(size_t i=0;i<count;++i)for(const Face& f:faces)if(!f.dead&&above(f,in[i])>tolerance)return {};
    std::vector<Vec3> result;
    for(size_t i=0;i<count;++i)if(onHull[i])result.push_back(in[i]);
    if(result.size()<4)return {};
    return result;
}
}
