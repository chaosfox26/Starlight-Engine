#include "flow.hpp"
template<class Require> void liquid_obstacle_surface_checks(Require require,const fs::path& folder){
    Character cube;cube.Shape="cube";cube.KeepAboveFloor=false;
    auto cubeMesh=shape_geometry(cube).front().mesh;
    FlowObstacleMask cubeMask;cubeMask.add(cubeMesh,cube);cubeMask.finish();
    require(cubeMask.clearance({0,.5,0})<-.49&&cubeMask.clearance({0,1.2,0})>.19&&
            std::isinf(cubeMask.clearance({2,.5,0})),"Exact obstacle columns distinguish cube interior, submerged top and open air");
    require(!cubeMask.clear_triangle({-.8,.5,-.1},{.8,.5,-.1},{0,.5,.1}),
            "Exact triangle clearance rejects water chord through a vertical cube face");
    require(cubeMask.clear_triangle({-.8,1.2,-.1},{.8,1.2,-.1},{0,1.2,.1}),
            "Exact triangle clearance preserves water above a submerged cube");
    // This face intersection is narrower than midpoint/quarter-edge probes.
    require(!cubeMask.clear_triangle({.4999,.4,-.2},{.5001,.4,.2},{.9,.4,-.2}),
            "Exact obstacle intersection rejects a thin crossing without relying on finite probes");
    Character upper=cube;upper.Y=2;FlowObstacleMask stacked;
    stacked.add(cubeMesh,cube);stacked.add(cubeMesh,upper);stacked.finish();
    require(stacked.clearance({0,1.5,0})>.49,"Separate obstacle objects preserve their intervening air gap");
    auto layeredMesh=cubeMesh;
    int base=int(layeredMesh.vertices.size());
    for(auto vertex:cubeMesh.vertices){vertex.position.y+=2;layeredMesh.vertices.push_back(vertex);}
    for(int index:cubeMesh.indices)layeredMesh.indices.push_back(base+index);
    FlowObstacleMask layered;layered.add(layeredMesh,cube);layered.finish();
    require(layered.clearance({0,.5,0})<-.49&&layered.clearance({0,2.5,0})<-.49&&
            layered.clearance({0,1.5,0})>.49,
            "Multiple closed layers in one obstacle preserve intervening air instead of filling its entire column");
    TerrainData terrain;terrain.WaterEnabled=true;terrain.WaterLevel=.18;
    {
        Character hovering=cube;hovering.Y=.18;
        FlowObstacleMask hoveringMask;hoveringMask.add(cubeMesh,hovering);hoveringMask.finish();
        auto lake=std::make_unique<TerrainFlow>(terrain,std::vector<LiquidSource>{});
        double volume=lake->volume();lake->set_obstacles(hoveringMask);
        double maximum=-INFINITY;
        for(int i=0;i<2304;++i)if(lake->water[i]>.002)maximum=std::max(maximum,lake->surface(i,lake->water[i]));
        require(maximum<.181&&std::abs(lake->volume()-volume)<1e-8,
                "Water at a solid underside capacity plateau does not jump to the object top");
        lake->water[23*48+22]=.3;
        volume=lake->volume();lake->step(.01);maximum=-INFINITY;
        for(int i=0;i<2304;++i)if(lake->water[i]>.002)maximum=std::max(maximum,lake->surface(i,lake->water[i]));
        require(maximum<=.301&&std::abs(lake->volume()-volume)<1e-8,
                "Low-level inflow retains mass without teleporting across a solid's capacity plateau");
    }
    {
        auto settled=std::make_unique<TerrainFlow>(terrain,std::vector<LiquidSource>{});
        double originalVolume=settled->volume();settled->set_obstacles(cubeMask);
        double highest=-INFINITY;for(int i=0;i<TerrainFlow::N*TerrainFlow::N;++i)if(settled->water[i]>.002)highest=std::max(highest,settled->surface(i,settled->water[i]));
        require(highest<.181&&std::abs(settled->volume()-originalVolume)<1e-8,
                "Initial solid displacement spreads conserved water across the pool instead of perching it above a rock");
        auto before=settled->water;settled->set_obstacles(cubeMask);
        require(settled->water==before,"Later obstacle updates do not repeatedly reset the moving fluid surface");
        TerrainData basins;
        for(int z=0;z<TerrainData::N;++z)for(int x=0;x<TerrainData::N;++x){double px=x*.25-12,pz=z*.25-12;basins.Heights[z*TerrainData::N+x]=(std::abs(pz)<2&&((px>-2&&px<2)||(px>4.5&&px<8.5)))?0:1;}
        auto separated=std::make_unique<TerrainFlow>(basins,std::vector<LiquidSource>{});
        for(int z=21;z<27;++z)for(int x=21;x<27;++x)separated->water[z*48+x]=.2;
        for(int z=21;z<27;++z)for(int x=34;x<40;++x)separated->water[z*48+x]=.4;
        auto untouched=separated->water;double volume=separated->volume();separated->set_obstacles(cubeMask);
        bool unchanged=true;for(int z=21;z<27;++z)for(int x=34;x<40;++x)unchanged&=separated->water[z*48+x]==untouched[z*48+x];
        require(unchanged&&std::abs(separated->volume()-volume)<1e-8,
                "Initial displacement retains separate pool levels and total mass");
    }
    auto checkFixture=[&](std::string shape)->json{
        Character obstacle;obstacle.Shape=shape;obstacle.KeepAboveFloor=false;obstacle.X=.073;obstacle.Z=-.041;
        FlowObstacleMask mask;mask.add(shape_geometry(obstacle).front().mesh,obstacle);mask.finish();
        auto flow=std::make_unique<TerrainFlow>(terrain,std::vector<LiquidSource>{});flow->set_obstacles(mask);
        double initialVolume=flow->volume();json frames=json::array();
        for(int frame=0;frame<3;++frame){
            if(frame==1)flow->step(.001);if(frame==2)for(int i=0;i<14;++i)flow->step(.0125);
            auto before=flow->water;double volume=flow->volume();size_t triangles=0;double minimum=INFINITY;bool clear=true;
            for(const auto& part:flow->geometry()){
                part.mesh.validate();
                for(size_t i=0;i<part.mesh.indices.size();i+=3){
                    auto a=part.mesh.vertices[part.mesh.indices[i]].position,b=part.mesh.vertices[part.mesh.indices[i+1]].position,c=part.mesh.vertices[part.mesh.indices[i+2]].position;
                    clear&=mask.clear_triangle(a,b,c);++triangles;
                    for(int u=0;u<=8;++u)for(int v=0;v<=8-u;++v){auto p=a+(b-a)*(u/8.)+(c-a)*(v/8.);double gap=mask.clearance(p);minimum=std::min(minimum,gap);clear&=gap>=-1e-6;}
                }
            }
            require(clear&&triangles>0,("Initial and evolving liquid triangles stay outside actual "+shape+" solid").c_str());
            require(flow->water==before&&flow->volume()==volume&&std::abs(volume-initialVolume)<1e-8,
                    "Obstacle surface clipping preserves fluid quantities and conserved volume");
            frames.push_back({{"frame",frame},{"triangles",triangles},{"minimumObjectClearance",minimum},{"volume",volume},{"clear",clear}});
        }
        return frames;
    };
    atomic_write(folder/"liquid-obstacle-surface-diagnostics.json",json{{"cube",checkFixture("cube")},{"boulder",checkFixture("boulder")}}.dump(2));
}
