#pragma once
#include "particle_physics.hpp"
#include <unordered_set>
namespace starlight::editor {
inline constexpr const char* lightning_player_id="@starlight-player";
struct LightningSegment {Vec3 source,target,normal;std::string hit;bool contact=false;std::vector<Vec3> path;bool branch=false;};
inline bool lightning_solid(const Character& c){auto k=c.Shape.value_or("");return k!="grass"&&k!="fire"&&k!="light"&&k!="lava"&&k!="chaos-lightning"&&k!="light-shard"&&k!="craxis-smoke";}
inline std::vector<LightningSegment> trace_lightning(const Character& emitter,const SceneData& scene,double time,std::optional<Bounds> player={}){
    std::vector<std::pair<std::string,Bounds>> bodies;
    for(auto c:scene.Characters)if(lightning_solid(c)){c=route_pose(c,time);bodies.push_back({c.Id,object_bounds(c)});}
    if(player)bodies.push_back({lightning_player_id,*player});
    auto moved=route_pose(emitter,time);
    Vec3 source=world_point(moved,{0,3,0}),target=world_point(moved,{moved.TargetX,moved.TargetY,moved.TargetZ});
    if(!moved.LightningTargetId.empty()){auto found=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==moved.LightningTargetId;});if(found==scene.Characters.end())return {};auto c=*found;c=route_pose(c,time);target=world_point(c,{moved.TargetX,moved.TargetY,moved.TargetZ});}
    auto cast=[&](Vec3 a,Vec3 b,const std::unordered_set<std::string>& skip){LightningSegment s{a,b,{},{},false};auto travel=b-a;double first=1;
        for(const auto& [id,box]:bodies)if(!skip.contains(id)){double t=0;Vec3 normal;
            bool inside=a.x>=box.low.x&&a.x<=box.high.x&&a.y>=box.low.y&&a.y<=box.high.y&&a.z>=box.low.z&&a.z<=box.high.z;
            if(inside)normal=travel.length()>1e-10?travel.unit()*-1:Vec3{0,1,0};
            if((inside||particle_box_hit(a,travel,box,0,t,normal))&&t<=first){first=t;s.normal=normal;s.hit=id;s.contact=true;}}
        auto gap=[&](double t){auto p=a+travel*t;return p.y-(scene.Terrain?scene.Terrain->height_at(p.x,p.z):0.);};int count=std::max(1,int(std::ceil(travel.length()/.1)));
        for(int i=1;i<=count;++i){double b=double(i)/count;if(b>first+1./count)break;if(gap(b)<=0){double a=double(i-1)/count;for(int j=0;j<12;++j){double m=(a+b)*.5;if(gap(m)>0)a=m;else b=m;}if(b<=first){first=b;s.normal={0,1,0};s.hit.clear();s.contact=true;}break;}}
        s.target=a+travel*first;return s;};
    uint32_t seed=2166136261u;for(unsigned char c:emitter.Id){seed^=c;seed*=16777619u;}seed^=uint32_t(std::floor(std::max(0.,time)/.22))*2654435761u;
    auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return double(seed&65535)/32767.5-1;};
    auto pathCast=[&](Vec3 a,Vec3 b,const std::unordered_set<std::string>& skip,bool branch){
        auto direct=cast(a,b,skip);b=direct.target;auto delta=b-a;auto direction=delta.unit();auto side=direction.cross({0,1,0});if(side.length()<1e-6)side=direction.cross({1,0,0});side=side.unit();auto other=direction.cross(side);LightningSegment arc{a,b,{},{},false};arc.branch=branch;arc.path.push_back(a);auto previous=a;const int steps=branch?16:64;
        for(int i=1;i<=steps;++i){double t=double(i)/steps;auto next=a+delta*t;if(i<steps)next=next+(side*random()+other*random())*(emitter.BranchSpread*emitter.Size*.12*std::sin(t*3.141592653589793));auto contact=cast(previous,next,skip);arc.path.push_back(contact.target);if(contact.contact){arc.target=contact.target;arc.normal=contact.normal;arc.hit=contact.hit;arc.contact=true;break;}previous=next;}
        if(!arc.contact&&direct.contact){arc.target=direct.target;arc.normal=direct.normal;arc.hit=direct.hit;arc.contact=true;arc.path.back()=arc.target;}return arc;
    };
    std::vector<LightningSegment> result;std::unordered_set<std::string> visited{emitter.Id};result.push_back(pathCast(source,target,visited,false));
    for(int hop=0;hop<emitter.LightningChains&&!result.back().hit.empty();++hop){visited.insert(result.back().hit);auto start=result.back().target;double nearest=emitter.LightningChainRadius;std::optional<Vec3> next;
        for(const auto& [id,box]:bodies)if(!visited.contains(id)){auto center=(box.low+box.high)*.5;double distance=(center-start).length();if(distance>=nearest)continue;auto candidate=cast(start,center,visited);if(candidate.hit!=id)continue;nearest=distance;next=center;}
        if(!next)break;result.push_back(pathCast(start,*next,visited,false));
    }
    if(emitter.BranchSpread>0&&result.front().path.size()>4){auto main=result.front().path;for(int i=1;i<=3;++i){auto root=main[(main.size()-1)*i/4];auto end=root+Vec3{random(),-std::abs(random()),random()}*(emitter.BranchSpread*emitter.Size);result.push_back(pathCast(root,end,{emitter.Id},true));}}
    return result;
}
}
