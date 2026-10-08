#pragma once
#include "geometry.hpp"
#include <cstdio>
#include <map>
namespace starlight::editor {
// Local damage is transient; authored heights/paint remain untouched. The
// resulting subtractive solid operation is shared by render and collision.
class TerrainDestruction {
    std::map<std::array<int,3>,double> damage_;
    bool capped_=false;
public:
    void reset(){damage_.clear();capped_=false;}
    // True once a hit that would have carved was refused because the terrain already holds the most holes it can take.
    bool capped()const{return capped_;}
    bool damage(TerrainData& terrain,Vec3 point,Vec3 normal,double amount){
        if(!terrain.Breakable||!std::isfinite(amount)||amount<=0||!std::isfinite(point.x)||!std::isfinite(point.y)||!std::isfinite(point.z)||!std::isfinite(normal.x)||!std::isfinite(normal.y)||!std::isfinite(normal.z)||normal.length()<.5||std::abs(point.x)>=12||std::abs(point.z)>=12||std::abs(point.y)>20)return false;
        terrain.validate();
        normal=normal.unit();double radius=terrain.DestructionRadius;
        auto center=point-normal*(radius*.3);
        if(std::abs(center.y)>20||terrain.density_at(center.x,center.y,center.z)>radius*.5)return false;
        if(terrain.Volumes.size()>=128){if(!capped_){capped_=true;std::fprintf(stderr,"terrain destruction: the terrain already holds 128 holes, so further hits no longer carve it\n");}return false;}
        auto cell=std::array{int(std::floor(point.x*2)),int(std::floor(point.y*2)),int(std::floor(point.z*2))};
        if(!damage_.contains(cell)&&damage_.size()>=4096)return false;
        auto& total=damage_[cell];total=std::min(terrain.DestructionHealth,total+std::min(amount,terrain.DestructionHealth));
        if(total<terrain.DestructionHealth)return false;
        terrain.Volumes.push_back({center.x,center.y,center.z,radius,true});damage_.erase(cell);return true;
    }
};
}
