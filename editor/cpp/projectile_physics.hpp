#pragma once
#include "particle_physics.hpp"
namespace starlight::editor {
inline constexpr const char* projectile_player_id="@starlight-player";
struct ProjectileTarget {std::string id;Bounds before,after;};
struct ProjectileImpact {std::string target;Vec3 position,normal,velocity;std::string source;int team=1,control=-1;double damage=10,mass=.02,accuracy=1;};
struct PhysicalProjectile {uint64_t serial=0;Vec3 position,previous,velocity;double remaining=20,age=0,gravity=0;std::string source;int team=1,control=-1;double damage=10,mass=.02,accuracy=1;};
class ProjectilePhysics {
    uint64_t serial_=0;
    std::vector<PhysicalProjectile> shots_;
public:
    const auto& shots()const{return shots_;}
    void clear(){shots_.clear();}
    void fire(Vec3 position,Vec3 direction,double speed,double gravity,std::string source={},int team=1,double mass=.02,int control=-1,double accuracy=1){
        if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)||!std::isfinite(direction.x)||!std::isfinite(direction.y)||!std::isfinite(direction.z)||!std::isfinite(speed)||speed<=0||speed>500||!std::isfinite(gravity)||gravity<0||gravity>40||direction.length()<.5||team<0||team>255||!std::isfinite(mass)||mass<=0||mass>100||!std::isfinite(accuracy)||accuracy<0||accuracy>1||control< -1||control>=14)throw std::runtime_error("Invalid projectile parameters");
        if(shots_.size()>=128)shots_.erase(shots_.begin());
        shots_.push_back({++serial_,position,position,direction.unit()*speed,20,0,gravity,std::move(source),team,control,10,mass,accuracy});
    }
    std::vector<ProjectileImpact> step(double dt,const TerrainData* terrain,std::span<const ProjectileTarget> targets){
        if(!std::isfinite(dt)||dt<0||dt>.05)throw std::runtime_error("Invalid projectile elapsed time");
        std::vector<ProjectileImpact> impacts;if(dt==0)return impacts;int count=std::max(1,int(std::ceil(dt*240)));double slice=dt/count;
        for(auto& shot:shots_){shot.previous=shot.position;
            for(int sub=0;sub<count&&shot.remaining>0&&shot.age<10;++sub){
                Vec3 travel=shot.velocity*slice+Vec3{0,-.5*shot.gravity*slice*slice,0};shot.velocity.y-=shot.gravity*slice;
                if(travel.length()>shot.remaining)travel=travel.unit()*shot.remaining;
                double first=1;Vec3 normal{};std::string target;bool collided=false;
                for(const auto& object:targets){if(object.id==shot.source)continue;
                    auto shift=(object.after.low+object.after.high-object.before.low-object.before.high)*.5;
                    Bounds box=object.before;box.low=box.low+shift*(double(sub)/count);box.high=box.high+shift*(double(sub)/count);
                    auto oldSize=object.before.high-object.before.low,newSize=object.after.high-object.after.low;
                    Vec3 grow{std::max(0.,newSize.x-oldSize.x)*.5,std::max(0.,newSize.y-oldSize.y)*.5,std::max(0.,newSize.z-oldSize.z)*.5};box.low=box.low-grow;box.high=box.high+grow;
                    double hit=0;Vec3 n{};auto p=shot.position;
                    bool inside=p.x>=box.low.x-.04&&p.x<=box.high.x+.04&&p.y>=box.low.y-.04&&p.y<=box.high.y+.04&&p.z>=box.low.z-.04&&p.z<=box.high.z+.04;
                    if((inside||particle_box_hit(p,travel-shift*(1./count),box,.04,hit,n))&&hit<=first){first=hit;normal=inside?travel.unit()*-1:n;target=object.id;collided=true;}
                }
                auto gap=[&](double t){auto p=shot.position+travel*t;return p.y-.04-(terrain?terrain->height_at(p.x,p.z):0);};
                int samples=std::max(1,int(std::ceil(travel.length()/.05)));
                for(int j=0;j<=samples;++j){double b=double(j)/samples;if(b>first+1./samples)break;if(gap(b)<=0){double a=std::max(0.,double(j-1)/samples);for(int k=0;k<12;++k){double m=(a+b)*.5;if(gap(m)>0)a=m;else b=m;}if(b<=first){first=b;auto at=shot.position+travel*b;constexpr double e=.025;auto height=[&](double x,double z){return terrain?terrain->height_at(x,z):0.;};normal=Vec3{(height(at.x-e,at.z)-height(at.x+e,at.z))/(2*e),1,(height(at.x,at.z-e)-height(at.x,at.z+e))/(2*e)}.unit();target.clear();collided=true;}break;}}
                shot.position=shot.position+travel*first;shot.remaining-=travel.length()*first;shot.age+=slice;
                if(collided){impacts.push_back({target,shot.position,normal,shot.velocity,shot.source,shot.team,shot.control,shot.damage,shot.mass,shot.accuracy});shot.remaining=0;}
            }
        }
        std::erase_if(shots_,[](const auto& shot){return shot.remaining<=1e-8||shot.age>=10;});return impacts;
    }
};
}
