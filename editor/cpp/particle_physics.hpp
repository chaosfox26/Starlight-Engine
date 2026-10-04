#pragma once
#include "interactions.hpp"
#include <span>
namespace starlight::editor {
struct PhysicalParticle {
    Vec3 position{},velocity{};double age=0;uint64_t motionId=0;
    double radius=.06,restitution=.3,drag=.2,lifetime=8;
};
// Swept sphere against conservative collision boxes. No endpoint-only tunneling.
inline bool particle_box_hit(Vec3 start,Vec3 travel,const Bounds& box,double radius,double& fraction,Vec3& normal){
    Vec3 low=box.low-Vec3{radius,radius,radius},high=box.high+Vec3{radius,radius,radius};
    double entry=0,exit=1;Vec3 hit{};
    double p[]={start.x,start.y,start.z},d[]={travel.x,travel.y,travel.z},lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};
    for(int axis=0;axis<3;++axis){
        if(std::abs(d[axis])<1e-12){if(p[axis]<lo[axis]||p[axis]>hi[axis])return false;continue;}
        double a=(lo[axis]-p[axis])/d[axis],b=(hi[axis]-p[axis])/d[axis];double sign=-1;
        if(a>b){std::swap(a,b);sign=1;}
        if(a>=entry){entry=a;hit={};if(axis==0)hit.x=sign;else if(axis==1)hit.y=sign;else hit.z=sign;}
        exit=std::min(exit,b);if(entry>exit)return false;
    }
    if(hit.length()==0 || entry<0 || entry>1)return false;
    fraction=entry;normal=hit;return true;
}
inline void step_particle(PhysicalParticle& p,double seconds,const TerrainData* terrain,std::span<const Bounds> obstacles,Vec3 wind={},Vec3 acceleration={0,-9.81,0}){
    if(!std::isfinite(seconds)||seconds<0||seconds>1)throw std::runtime_error("Invalid particle step");
    if(seconds==0)return;
    p.age+=seconds;
    const int steps=std::max(1,int(std::ceil(std::max(seconds*240,p.velocity.length()*seconds/.1))));
    const double dt=seconds/steps;
    auto height=[&](Vec3 at){return terrain?terrain->height_at(at.x,at.z):0.;};
    bool volume=terrain&&!terrain->Volumes.empty();
    auto gap=[&](Vec3 at){return volume?terrain->density_at(at.x,at.y,at.z)-p.radius:at.y-height(at)-p.radius;};
    auto groundNormal=[&](Vec3 at){
        const double e=volume?.002:.025;
        if(volume)return Vec3{terrain->density_at(at.x+e,at.y,at.z)-terrain->density_at(at.x-e,at.y,at.z),terrain->density_at(at.x,at.y+e,at.z)-terrain->density_at(at.x,at.y-e,at.z),terrain->density_at(at.x,at.y,at.z+e)-terrain->density_at(at.x,at.y,at.z-e)}.unit();
        return Vec3{(height(at+Vec3{-e,0,0})-height(at+Vec3{e,0,0}))/(2*e),1,(height(at+Vec3{0,0,-e})-height(at+Vec3{0,0,e}))/(2*e)}.unit();
    };
    auto resolveTerrain=[&]{
        if(!volume)return;
        for(int iteration=0;iteration<32;++iteration){double clearance=gap(p.position);if(clearance>=-1e-6)break;auto normal=groundNormal(p.position);if(normal.length()<.5){double best=-std::numeric_limits<double>::infinity();for(auto direction:{Vec3{1,0,0},Vec3{-1,0,0},Vec3{0,1,0},Vec3{0,-1,0},Vec3{0,0,1},Vec3{0,0,-1}}){double candidate=gap(p.position+direction*.02);if(candidate>best){best=candidate;normal=direction;}}}p.position=p.position+normal*std::clamp(-clearance+.0001,.0001,.5);double into=p.velocity.dot(normal);if(into<0)p.velocity=p.velocity-normal*into;}
    };
    for(int i=0;i<steps;++i){
        // Moving obstacles may engulf a particle between updates. Resolve the
        // existing overlap before the sweep rather than letting it escape through a wall.
        for(const auto& box:obstacles){
            auto lo=box.low-Vec3{p.radius,p.radius,p.radius},hi=box.high+Vec3{p.radius,p.radius,p.radius};
            if(p.position.x>lo.x && p.position.x<hi.x && p.position.y>lo.y && p.position.y<hi.y && p.position.z>lo.z && p.position.z<hi.z){
                double depths[]={p.position.x-lo.x,hi.x-p.position.x,p.position.y-lo.y,hi.y-p.position.y,p.position.z-lo.z,hi.z-p.position.z};
                Vec3 normals[]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
                int face=int(std::min_element(std::begin(depths),std::end(depths))-std::begin(depths));auto n=normals[face];
                p.position=p.position+n*(depths[face]+.0001);double inward=p.velocity.dot(n);if(inward<0)p.velocity=p.velocity-n*inward;
            }
        }
        resolveTerrain();
        p.velocity=wind+(p.velocity-wind)*std::exp(-p.drag*dt);p.velocity=p.velocity+acceleration*dt;
        double remaining=dt;
        for(int bounce=0;bounce<4 && remaining>1e-8;++bounce){
            auto travel=p.velocity*remaining;double earliest=1;Vec3 normal{};
            for(const auto& box:obstacles){double t;Vec3 n;if(particle_box_hit(p.position,travel,box,p.radius,t,n) && t<earliest){earliest=t;normal=n;}}
            double startGap=gap(p.position);
            if(volume){
                int samples=std::max(1,int(std::ceil(travel.length()/std::max(.005,std::min(.025,p.radius*.5)))));double previous=0;
                for(int sample=1;sample<=samples;++sample){double t=std::min(double(sample)/samples,earliest);if(gap(p.position+travel*t)<0){double lo=previous,hi=t;for(int refinement=0;refinement<14;++refinement){double mid=(lo+hi)*.5;if(gap(p.position+travel*mid)>=0)lo=mid;else hi=mid;}earliest=(lo+hi)*.5;normal=groundNormal(p.position+travel*earliest);break;}if(t>=earliest)break;previous=t;}
            }else{
                auto end=p.position+travel;double endGap=gap(end);
                if(endGap<0){double t=startGap<=0?0:std::clamp(startGap/(startGap-endGap),0.,1.);if(t<=earliest){earliest=t;normal=groundNormal(p.position+travel*t);}}
            }
            p.position=p.position+travel*earliest;
            if(normal.length()==0)break;
            p.position=p.position+normal*.0001;if(!volume)p.position.y=std::max(p.position.y,height(p.position)+p.radius);
            double into=p.velocity.dot(normal);if(into<0)p.velocity=p.velocity-normal*((1+p.restitution)*into);
            if(normal.y>.5){auto vertical=normal*p.velocity.dot(normal);p.velocity=vertical+(p.velocity-vertical)*std::exp(-5*remaining);if(std::abs(p.velocity.dot(normal))<.08)p.velocity=p.velocity-normal*p.velocity.dot(normal);}
            remaining*=1-earliest;
        }
    }
}
}
