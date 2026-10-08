// Regression checks for the liquids, lava, terrain and flow audit findings (audit 07).
#include "flow.hpp"
template<class Require> void liquid_terrain_audit_checks(Require require){
    {   // 07#1: the liquid top used to decide which moving objects join the liquid mask is the drawn surface, not bed + depth.
        Character cube;cube.Shape="cube";cube.KeepAboveFloor=false;
        FlowObstacleMask mask;mask.add(shape_geometry(cube).front().mesh,cube);mask.finish();
        TerrainData terrain;terrain.WaterEnabled=true;terrain.WaterLevel=1.5; // well above the one metre cube
        auto lake=std::make_unique<TerrainFlow>(terrain,std::vector<LiquidSource>{});lake->set_obstacles(mask);
        const auto calm=lake->liquid_top_cells(0);
        bool exact=true,raised=false;size_t wet=0;
        for(int i=0;i<TerrainFlow::N*TerrainFlow::N;++i){
            const double depth=std::max(lake->water[i],lake->lava[i]);if(depth<=1e-5)continue;
            ++wet;exact&=std::abs(calm[i]-lake->surface(i,depth))<1e-12;
            raised|=lake->surface(i,depth)>depth+.1; // flat terrain: bed + depth is just the depth
        }
        require(raised,"07#1: The submerged cube raises the drawn surface above bed plus depth");
        require(wet>100&&exact,"07#1: In calm water the liquid top of every wet cell is its drawn surface, including the rise over a submerged solid");
        lake->impulse(5,5,1.5);for(int i=0;i<6;++i)lake->step(.02);
        const auto rippled=lake->liquid_top_cells(0);
        bool within=true,crest=false;
        for(int i=0;i<TerrainFlow::N*TerrainFlow::N;++i){
            const double depth=std::max(lake->water[i],lake->lava[i]);if(depth<=1e-5)continue;
            const double wave=rippled[i]-lake->surface(i,depth);
            within&=std::abs(wave)<=.3+1e-9;crest|=std::abs(wave)>1e-6;
        }
        require(within&&crest,"07#1: Waves raise and lower the liquid top by no more than the 0.3 m wave limit");
    }
}
