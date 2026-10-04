#pragma once
#include "interactions.hpp"

namespace starlight::editor {
inline std::optional<double> terrain_support_height(const TerrainData& terrain,double x,double z,double feet,double rise=.35,double fall=.65){
    if(terrain.Volumes.empty())return terrain.height_at(x,z);
    double previous=feet+rise,previousGap=terrain.density_at(x,previous,z);
    int samples=int(std::ceil((rise+fall)/.025));
    for(int i=1;i<=samples;++i){
        double y=feet+rise-(rise+fall)*i/samples,gap=terrain.density_at(x,y,z);
        if(previousGap>=0&&gap<0){
            double low=y,high=previous;
            for(int step=0;step<14;++step){double mid=(low+high)*.5;if(terrain.density_at(x,mid,z)<0)low=mid;else high=mid;}
            double floor=(low+high)*.5,e=.002;
            Vec3 normal{terrain.density_at(x+e,floor,z)-terrain.density_at(x-e,floor,z),terrain.density_at(x,floor+e,z)-terrain.density_at(x,floor-e,z),terrain.density_at(x,floor,z+e)-terrain.density_at(x,floor,z-e)};
            if(normal.unit().y<.55)return {};
            return floor;
        }
        previous=y;previousGap=gap;
    }
    return {};
}
inline bool terrain_body_clear(const TerrainData& terrain,const Bounds& bounds,Vec3 offset={}){
    if(terrain.Volumes.empty())return true;
    auto low=bounds.low+offset,high=bounds.high+offset;
    double bottom=std::min(high.y,low.y+.035),top=std::max(bottom,high.y-.015);
    int vertical=std::max(1,int(std::ceil((top-bottom)/.2)));
    for(int y=0;y<=vertical;++y)for(int z=0;z<3;++z)for(int x=0;x<3;++x){
        Vec3 p{low.x+(high.x-low.x)*x*.5,bottom+(top-bottom)*y/vertical,low.z+(high.z-low.z)*z*.5};
        if(terrain.density_at(p.x,p.y,p.z)<-.003)return false;
    }
    return true;
}
inline std::optional<double> terrain_body_support(const TerrainData& terrain,const Bounds& bounds,Vec3 offset,double nearFeet){
    double floor=-std::numeric_limits<double>::infinity();
    for(int z=0;z<3;++z)for(int x=0;x<3;++x){
        auto height=terrain_support_height(terrain,bounds.low.x+offset.x+(bounds.high.x-bounds.low.x)*x*.5,bounds.low.z+offset.z+(bounds.high.z-bounds.low.z)*z*.5,nearFeet);
        if(!height)return {};floor=std::max(floor,*height);
    }
    offset.y=floor-bounds.low.y;
    if(!terrain_body_clear(terrain,bounds,offset))return {};
    return floor;
}
}
