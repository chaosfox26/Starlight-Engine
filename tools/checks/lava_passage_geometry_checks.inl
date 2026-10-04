#include "../../editor/cpp/lava_surface.hpp"
template<class Require> void lava_passage_geometry_checks(Require require){
    using starlight::editor::lava_crust;using starlight::editor::lava_passage;
    bool bounded=true,frozen=true,moves=false,closes=true;int openRegions=0,raisedRafts=0;double maximumHot=0,maximumCold=0;
    for(int iz=0;iz<49;++iz)for(int ix=0;ix<49;++ix){
        double x=-12+ix*.5,z=-12+iz*.5;
        auto hot=lava_crust(x,z,0,1,1,.12),cold=lava_crust(x,z,0,0,1,.12),laterCold=lava_crust(x,z,120,0,1,.12);
        auto exposure=lava_passage(x,z,0,1);auto later=lava_crust(x,z,3.5,1,1,.12);
        frozen&=cold.cover==laterCold.cover&&cold.relief==laterCold.relief;
        moves|=std::abs(hot.relief-later.relief)>.002;
        closes&=lava_passage(x,z,0,0).exposed==0&&cold.cover>=hot.cover;
        maximumHot=std::max(maximumHot,hot.relief);maximumCold=std::max(maximumCold,cold.relief);
        if(exposure.exposed>.98&&hot.cover<.02&&hot.relief<.001)++openRegions;
        if(exposure.exposed<.03&&hot.cover>.95&&hot.relief>.02)++raisedRafts;
        for(double heat:{0.,.2,.5,.8,1.})for(double depth:{0.,.01,1.})for(double thickness:{0.,.08,.3}){
            auto v=lava_crust(x,z,3.5,heat,depth,thickness);
            double bound=std::min(thickness,depth*.3);
            bounded&=std::isfinite(v.cover)&&std::isfinite(v.relief)&&v.cover>=0&&v.cover<=1&&v.relief>=0&&v.relief<=bound+1e-12;
        }
    }
    require(bounded,"Passage-shaped lava keeps finite coverage and actual relief within authored thickness and local depth bounds");
    require(openRegions>20&&raisedRafts>20&&maximumHot>.02,"Hot lava mesh has genuinely lowered broad molten regions beside raised intact basalt rafts");
    require(closes&&maximumCold>maximumHot,"Cooling closes actual molten passages and thickens surviving crust without editing simulated mass");
    require(frozen,"Cold lava passage and raft geometry is exactly frozen regardless of carried clock");
    require(moves,"Advected hot passages change real relief rather than only material color or normals");
}
