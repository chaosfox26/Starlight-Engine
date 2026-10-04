#pragma once
#include "particle_physics.hpp"
#include "geometry.hpp"
namespace starlight::editor {
// Transient destruction feedback stays independent of rigid fragments, authored
// objects, fire and chaos emitters. Its small collision carriers use the same
// swept terrain/obstacle contacts as other particles.
struct DustPuff : PhysicalParticle {
    Vec3 tint{.4,.36,.3};
    double initialRadius=.2,spread=.3,strength=1;
    uint64_t serial=0;
    double extent() const {return initialRadius+spread*age;}
    double opacity() const {
        double t=std::clamp(age/lifetime,0.,1.);
        return strength*std::pow(1-t,2)*std::pow(initialRadius/extent(),1.4);
    }
};
struct DustGrain : PhysicalParticle {Vec3 tint;uint64_t serial=0;};
class DestructionDust {
    std::vector<DustPuff> puffs_;
    std::vector<DustGrain> grains_;
    uint64_t serial_=0;
public:
    const auto& puffs() const {return puffs_;}
    const auto& grains() const {return grains_;}
    void clear(){puffs_.clear();grains_.clear();}
    void burst(Vec3 position,Vec3 normal,Vec3 tint,double scale,uint32_t seed,bool shatter){
        if(!std::isfinite(scale)||scale<=0||!std::isfinite(position.x+position.y+position.z))return;
        scale=std::clamp(scale,.05,3.);
        normal=normal.unit();if(normal.length()<.5)normal={0,1,0};
        auto random=[&](){seed=seed*1664525u+1013904223u;return double(seed>>8)/16777216.;};
        const int clouds=shatter?6:1,grains=shatter?36:6;
        tint={std::clamp(tint.x*.7+.12,.12,.65),std::clamp(tint.y*.7+.1,.1,.6),std::clamp(tint.z*.7+.08,.08,.55)};
        for(int i=0;i<clouds;++i){
            double angle=random()*6.283185307179586;
            Vec3 radial{std::cos(angle),random()*.5,std::sin(angle)};
            DustPuff p;p.serial=++serial_;p.tint=tint;p.position=position+normal*.055+radial*(shatter?scale*.22:0);
            p.velocity=normal*(shatter?.5:.7)+radial*(shatter?.65:.25);p.radius=.035;p.drag=1.5;p.restitution=0;
            p.initialRadius=scale*(shatter?.25:.65);p.spread=scale*.22;p.strength=shatter?1.1:.7;p.lifetime=2.4+random()*1.4;
            puffs_.push_back(p);
        }
        for(int i=0;i<grains;++i){
            double angle=random()*6.283185307179586;DustGrain g;g.serial=++serial_;g.tint=tint;
            g.position=position+normal*.055;g.velocity=normal*(.6+random()*1.1)+Vec3{std::cos(angle),random()*.6,std::sin(angle)}*(.7+random()*1.6)*std::sqrt(scale);
            g.radius=(.007+random()*.012)*std::sqrt(scale);g.drag=.35;g.restitution=.15;g.lifetime=1.3+random()*1.2;grains_.push_back(g);
        }
        if(puffs_.size()>24)puffs_.erase(puffs_.begin(),puffs_.end()-24);
        if(grains_.size()>256)grains_.erase(grains_.begin(),grains_.end()-256);
    }
    void advance(double dt,const TerrainData* terrain,std::span<const Bounds> obstacles,Vec3 wind){
        for(auto& p:puffs_)step_particle(p,dt,terrain,obstacles,wind,{0,-.15,0});
        for(auto& g:grains_)step_particle(g,dt,terrain,obstacles,wind);
        std::erase_if(puffs_,[](const auto& p){return p.age>=p.lifetime;});
        std::erase_if(grains_,[](const auto& g){return g.age>=g.lifetime;});
    }
};
inline MeshPart dust_volume(const DustPuff& puff){
    MeshPart part;part.color=puff.tint;const double r=puff.extent();
    // Kind fraction carries optical strength through existing scene packets;
    // these bounds are consumed by the volume pass, never drawn as solid cubes.
    const float kind=13.f+float(std::clamp(puff.opacity(),0.,1.8))*.4f;
    for(int i=0;i<8;++i)part.mesh.vertices.push_back({puff.position+Vec3{i&1?r:-r,i&2?r:-r,i&4?r:-r},{},{1,1,1},kind,vertex_motion_identity(14,puff.serial,i)});
    part.mesh.indices={0,4,6,0,6,2,1,3,7,1,7,5,0,1,5,0,5,4,2,6,7,2,7,3,0,2,3,0,3,1,4,5,7,4,7,6};
    part.mesh.normals();return part;
}
inline MeshPart dust_grain(const DustGrain& grain){
    MeshPart part;part.color=grain.tint;
    const Vec3 offsets[]={{1,1,1},{-1,-1,1},{-1,1,-1},{1,-1,-1}};
    for(int i=0;i<4;++i)part.mesh.vertices.push_back({grain.position+offsets[i]*grain.radius,offsets[i].unit(),{1,1,1},6.f,vertex_motion_identity(15,grain.serial,i)});
    part.mesh.indices={0,2,1,0,1,3,0,3,2,1,2,3};part.mesh.normals();return part;
}
}
