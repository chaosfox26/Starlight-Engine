#pragma once
#include "mesh.hpp"
#include <queue>
#include <unordered_map>
#include <vector>
namespace starlight::editor {
// Bounded local grid search; every edge is collision checked, including diagonals.
template<class Clear> std::vector<Vec3> ground_path(Vec3 origin,Vec3 goal,Clear clear){
    struct Node{int x,z,parent;double cost;};
    struct Open{double score;int index;bool operator<(const Open& b)const{return score>b.score;}};
    constexpr double cell=.5;constexpr int bound=48;
    auto key=[](int x,int z){return (x+bound)*(2*bound+1)+z+bound;};
    auto point=[&](int x,int z){return origin+Vec3{x*cell,0,z*cell};};
    auto distance=[&](Vec3 p){return std::hypot(p.x-goal.x,p.z-goal.z);};
    std::vector<Node> nodes{{0,0,-1,0}};std::priority_queue<Open> open;open.push({distance(origin),0});
    std::unordered_map<int,double> best{{key(0,0),0}};
    while(!open.empty() && nodes.size()<6000){
        auto entry=open.top();open.pop();auto n=nodes[entry.index];
        if(n.cost>best[key(n.x,n.z)]+1e-8)continue;
        auto here=point(n.x,n.z);
        if(distance(here)<=.4){std::vector<Vec3> path;for(int i=entry.index;nodes[i].parent>=0;i=nodes[i].parent)path.push_back(point(nodes[i].x,nodes[i].z));std::reverse(path.begin(),path.end());return path;}
        for(int x=-1;x<=1;x++)for(int z=-1;z<=1;z++){
            if(!x && !z)continue;int nx=n.x+x,nz=n.z+z;if(std::abs(nx)>bound || std::abs(nz)>bound)continue;
            auto next=point(nx,nz);if(!clear(here,next))continue;
            if(x && z && (!clear(here,point(nx,n.z)) || !clear(here,point(n.x,nz))))continue;
            double cost=n.cost+std::hypot(double(x),double(z))*cell;int k=key(nx,nz);auto found=best.find(k);if(found!=best.end() && found->second<=cost)continue;
            best[k]=cost;nodes.push_back({nx,nz,entry.index,cost});open.push({cost+distance(next),int(nodes.size()-1)});
        }
    }
    return {};
}
}
