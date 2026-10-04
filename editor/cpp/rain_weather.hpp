#pragma once
#include "particle_physics.hpp"
#include "particle_geometry.hpp"
#include "flow.hpp"

namespace starlight::editor {
class RainWeather {
  public:
    struct Impact {Vec3 position{},velocity{};bool water=false,lava=false;};
    static constexpr size_t Capacity=2048;
  private:
    std::vector<PhysicalParticle> drops_;
    std::vector<Impact> impacts_;
    uint32_t random_=0x6a09e667;
    uint64_t serial_=0,impactCount_=0;
    double remainder_=0;
    double random(){random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;return double(random_&0xffffff)/16777216.;}
    static double clearance(const TerrainData* terrain,Vec3 p,double radius){
        if(!terrain)return p.y-radius;
        return terrain->Volumes.empty()?p.y-terrain->height_at(p.x,p.z)-radius:terrain->density_at(p.x,p.y,p.z)-radius;
    }
  public:
    void reset(){drops_.clear();impacts_.clear();random_=0x6a09e667;serial_=0;impactCount_=0;remainder_=0;}
    const auto& particles()const{return drops_;}
    const auto& impacts()const{return impacts_;}
    uint64_t impact_count()const{return impactCount_;}
    void step(const SceneData& scene,double dt,std::span<const Bounds> obstacles,TerrainFlow* liquids){
        if(!std::isfinite(dt)||dt<0||dt>.05)throw std::runtime_error("Invalid rain step");
        impacts_.clear();if(dt==0)return;
        const auto* terrain=scene.Terrain?&*scene.Terrain:nullptr;
        Vec3 wind{scene.RainWindX,0,scene.RainWindZ};
        for(auto& p:drops_){
            p.age+=dt;int steps=std::max(1,int(std::ceil(p.velocity.length()*dt/.04)));double seconds=dt/steps;
            for(int sub=0;sub<steps&&p.age<p.lifetime;++sub){
                p.velocity.x+=(wind.x-p.velocity.x)*(1-std::exp(-2*seconds));
                p.velocity.z+=(wind.z-p.velocity.z)*(1-std::exp(-2*seconds));
                p.velocity.y=std::max(-9.,p.velocity.y-9.81*seconds);
                Vec3 travel=p.velocity*seconds;double earliest=1;bool hit=false,water=false,lava=false;
                for(const auto& box:obstacles){double fraction;Vec3 normal;
                    if(particle_box_hit(p.position,travel,box,p.radius,fraction,normal)&&fraction<=earliest){earliest=fraction;hit=true;}
                }
                double start=clearance(terrain,p.position,p.radius),end=clearance(terrain,p.position+travel*earliest,p.radius);
                if(start<=0||end<=0){double lo=0,hi=earliest;
                    if(start>0)for(int refine=0;refine<14;++refine){double mid=(lo+hi)*.5;if(clearance(terrain,p.position+travel*mid,p.radius)>0)lo=mid;else hi=mid;}
                    else hi=0;
                    earliest=(lo+hi)*.5;hit=true;
                }
                if(liquids){auto at=p.position+travel*earliest;auto surface=liquids->liquid_at(at.x,at.z);
                    if(surface.second>0&&at.y-p.radius<=surface.first){double t=travel.y<0?std::clamp((surface.first+p.radius-p.position.y)/travel.y,0.,earliest):0.;
                        earliest=t;hit=true;water=surface.second==1000;lava=!water;
                    }
                }
                p.position=p.position+travel*earliest;
                if(hit){impacts_.push_back({p.position,p.velocity,water,lava});++impactCount_;p.age=p.lifetime;
                    if(water)liquids->impulse(p.position.x,p.position.z,.004);
                }
            }
        }
        std::erase_if(drops_,[](const auto& p){return p.age>=p.lifetime;});
        if(scene.RainIntensity==0){remainder_=0;return;}
        remainder_+=dt*1200*scene.RainIntensity;int count=int(remainder_);remainder_-=count;
        double top=12;
        if(terrain){for(double h:terrain->Heights)top=std::max(top,h+10);
            for(const auto& volume:terrain->Volumes)if(!volume.Subtract)top=std::max(top,volume.Y+volume.Radius+5);
        }
        for(const auto& box:obstacles)top=std::max(top,box.high.y+5);
        for(int i=0;i<count&&drops_.size()<Capacity;++i){PhysicalParticle p;
            p.motionId=render_identity("rain-weather",++serial_);p.position={(random()-.5)*24,top+random()*2,(random()-.5)*24};
            p.velocity={wind.x,-5-random()*3,wind.z};p.radius=.008+random()*.006;p.lifetime=(top+20)/5;
            drops_.push_back(p);
        }
    }
    std::vector<MeshPart> geometry()const{
        if(drops_.empty())return {};
        MeshPart part;part.color={1,1,1};part.mesh.vertices.reserve(drops_.size()*particle_surface().vertices.size());part.mesh.indices.reserve(drops_.size()*particle_surface().indices.size());
        for(const auto& p:drops_)append_particle_surface(part.mesh,p.position,p.velocity,p.radius,4,p.motionId,{.5,p.radius*2,1},7.2f);
        return {std::move(part)};
    }
};
}
