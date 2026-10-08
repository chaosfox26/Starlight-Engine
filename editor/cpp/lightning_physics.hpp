#pragma once
#include "particle_physics.hpp"
#include "lightning_geometry.hpp"
#include "object_cache.hpp"
#include <unordered_set>
namespace starlight::editor {
inline constexpr const char* lightning_player_id="@starlight-player";
struct LightningSegment {Vec3 source,target,normal;std::string hit;bool contact=false;std::vector<Vec3> path;bool branch=false;};
inline bool lightning_solid(const Character& c){auto k=c.Shape.value_or("");return k!="grass"&&k!="fire"&&k!="light"&&k!="lava"&&k!="chaos-lightning"&&k!="light-shard"&&k!="craxis-smoke";}
inline std::vector<LightningSegment> trace_lightning(const Character& emitter,const SceneData& scene,double time,std::optional<Bounds> player={}){
    std::vector<std::pair<std::string,Bounds>> bodies;
    // Scenery that does not move on its own is measured once by the shared cache, not copied and re-measured per strike.
    for(const auto& source:scene.Characters)if(lightning_solid(source)||(source.Breakable&&source.Id==emitter.LightningTargetId))bodies.push_back({source.Id,bounds_cacheable(source)?ObjectCache::shared().bounds(source):ObjectCache::shared().bounds_at(source,route_pose_of(source,time))});
    if(player)bodies.push_back({lightning_player_id,*player});
    const auto moved=route_pose_of(emitter,time);
    Vec3 source=world_point(moved,{0,3,0}),target=world_point(moved,{emitter.TargetX,emitter.TargetY,emitter.TargetZ});
    if(!emitter.LightningTargetId.empty()){auto found=std::find_if(scene.Characters.begin(),scene.Characters.end(),[&](const auto& c){return c.Id==emitter.LightningTargetId;});if(found==scene.Characters.end())return {};target=world_point(route_pose_of(*found,time),{emitter.TargetX,emitter.TargetY,emitter.TargetZ});}
    auto cast=[&](Vec3 a,Vec3 b,const std::unordered_set<std::string>& skip){LightningSegment s{a,b,{},{},false};auto travel=b-a;double first=1;
        for(const auto& [id,box]:bodies)if(!skip.contains(id)){double t=0;Vec3 normal;
            bool inside=a.x>=box.low.x&&a.x<=box.high.x&&a.y>=box.low.y&&a.y<=box.high.y&&a.z>=box.low.z&&a.z<=box.high.z;
            if(inside)normal=travel.length()>1e-10?travel.unit()*-1:Vec3{0,1,0};
            if((inside||particle_box_hit(a,travel,box,0,t,normal))&&t<=first){first=t;s.normal=normal;s.hit=id;s.contact=true;}}
        bool volume=scene.Terrain&&!scene.Terrain->Volumes.empty();
        auto gap=[&](double t){auto p=a+travel*t;return volume?scene.Terrain->density_at(p.x,p.y,p.z):p.y-(scene.Terrain?scene.Terrain->height_at(p.x,p.z):0.);};
        int count=std::max(1,int(std::ceil(travel.length()/(volume?.025:.1))));
        for(int i=1;i<=count;++i){double high=double(i)/count;if(high>first+1./count)break;if(gap(high)<=0){double low=double(i-1)/count;for(int j=0;j<14;++j){double m=(low+high)*.5;if(gap(m)>0)low=m;else high=m;}if(high<=first){first=low;s.normal={0,1,0};if(volume){auto p=a+travel*first;constexpr double e=.002;s.normal=Vec3{scene.Terrain->density_at(p.x+e,p.y,p.z)-scene.Terrain->density_at(p.x-e,p.y,p.z),scene.Terrain->density_at(p.x,p.y+e,p.z)-scene.Terrain->density_at(p.x,p.y-e,p.z),scene.Terrain->density_at(p.x,p.y,p.z+e)-scene.Terrain->density_at(p.x,p.y,p.z-e)}.unit();}s.hit.clear();s.contact=true;}break;}}
        s.target=a+travel*first;return s;};
    uint32_t seed=2166136261u;for(unsigned char c:emitter.Id){seed^=c;seed*=16777619u;}seed^=uint32_t(std::floor(std::max(0.,time)/.22))*2654435761u;
    auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return double(seed&65535)/32767.5-1;};
    auto pathCast=[&](Vec3 a,Vec3 b,const std::unordered_set<std::string>& skip,bool branch){
        auto direct=cast(a,b,skip);b=direct.target;LightningSegment arc{a,b,{},{},false};arc.branch=branch;arc.path.push_back(a);auto previous=a;
        auto proposed=lightning_path(a,b,emitter.BranchSpread*emitter.Size*(branch?.12:.2),branch?4:6,random);
        for(size_t i=1;i<proposed.size();++i){auto next=proposed[i];auto contact=cast(previous,next,skip);arc.path.push_back(contact.target);if(contact.contact){arc.target=contact.target;arc.normal=contact.normal;arc.hit=contact.hit;arc.contact=true;break;}previous=next;}
        if(!arc.contact&&direct.contact){arc.target=direct.target;arc.normal=direct.normal;arc.hit=direct.hit;arc.contact=true;arc.path.back()=arc.target;}return arc;
    };
    std::vector<LightningSegment> result;std::unordered_set<std::string> visited{emitter.Id};result.push_back(pathCast(source,target,visited,false));
    for(int hop=0;hop<emitter.LightningChains&&!result.back().hit.empty();++hop){visited.insert(result.back().hit);auto start=result.back().target;double nearest=emitter.LightningChainRadius;std::optional<Vec3> next;
        // Nearest first: the first candidate that can actually be reached is the one the full scan would have kept (ties keep body order), so the rest are never cast.
        std::vector<std::pair<double,size_t>> order;
        for(size_t i=0;i<bodies.size();++i)if(!visited.contains(bodies[i].first)){double distance=((bodies[i].second.low+bodies[i].second.high)*.5-start).length();if(distance<nearest)order.push_back({distance,i});}
        std::stable_sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        for(const auto& entry:order){const auto& body=bodies[entry.second];auto center=(body.second.low+body.second.high)*.5;if(cast(start,center,visited).hit!=body.first)continue;next=center;break;}
        if(!next)break;result.push_back(pathCast(start,*next,visited,false));
    }
    if(emitter.BranchSpread>0&&result.front().path.size()>4){auto main=result.front().path;for(int i=1;i<=3;++i){auto root=main[(main.size()-1)*i/4];auto end=root+Vec3{random(),-std::abs(random()),random()}*(emitter.BranchSpread*emitter.Size);result.push_back(pathCast(root,end,{emitter.Id},true));}}
    return result;
}
}
