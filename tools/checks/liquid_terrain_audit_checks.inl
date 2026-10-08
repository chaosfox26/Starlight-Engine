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
        const auto drawn=lake->snapshot();bool within=true,crest=false,notBelowStill=true,trough=false;
        for(int i=0;i<TerrainFlow::N*TerrainFlow::N;++i){
            const double depth=std::max(lake->water[i],lake->lava[i]);if(depth<=1e-5)continue;
            const double wave=rippled[i]-lake->surface(i,depth);
            within&=std::abs(wave)<=.3+1e-9;crest|=wave>1e-6;
            notBelowStill&=rippled[i]>=depth-1e-12; // flat terrain: bed + depth is just the depth
            trough|=lake->surface(i,depth)<=depth+1e-12&&std::abs(rippled[i]-depth)<1e-12&&drawn.cells[i].first<depth-1e-6&&drawn.cells[i].second>0; // the snapshot holds each cell's drawn height
        }
        require(within&&crest,"07#1: Waves raise the liquid top by no more than the 0.3 m wave limit");
        require(notBelowStill,"07#1: A wave trough never takes the liquid top below the still level, so objects stay in the wet-object filter");
        require(trough,"07#1: The rippled lake has troughs where the drawn surface is below the still level and the filter still uses the still level");
    }
    {   // 07#5: editing a layer that another holder shares must not change what that holder sees.
        auto make=[](double x){Character c;c.Id="shared-rock";c.Shape="chaos-rock";c.X=x;c.Y=.5;c.Width=c.Height=c.Depth=c.Size=1.2;auto layer=std::make_shared<FlowObstacleMask>();layer->add(object_mesh(c),c);layer->finish();return std::shared_ptr<const FlowObstacleMask>(layer);};
        auto alone=make(1);const FlowObstacleMask* addressBefore=alone.get();
        require(editable_obstacle_layer(alone).get()==addressBefore,"07#5: A layer nobody else holds is edited in place");
        auto original=make(-2);auto holder=original;const auto columnsBefore=original->coverage;const auto trianglesBefore=original->triangles.size();
        auto edited=editable_obstacle_layer(original);
        const_cast<FlowObstacleMask&>(*edited).remove_objects({0});
        require(edited.get()!=original.get()&&original->coverage==columnsBefore&&original->triangles.size()==trianglesBefore,"07#5: A shared layer is copied before an edit, so the other holder keeps its triangles and columns");
        require(edited->coverage!=columnsBefore,"07#5: The copy carries the edit");
    }
    {   // 07#7: the 128 hole limit is not silent.
        TerrainData ground;TerrainDestruction destruction;
        for(int i=0;i<127;++i)ground.Volumes.push_back({-11.+(i%12)*2,-10,-11.+(i/12)*2,.3,true});
        require(destruction.damage(ground,{5.3,.04,5.3},{0,1,0},1000)&&ground.Volumes.size()==128&&!destruction.capped(),"07#7: The 128th hole is still carved");
        require(!destruction.damage(ground,{-5.3,.04,-5.3},{0,1,0},1000)&&ground.Volumes.size()==128&&destruction.capped(),"07#7: A hit past the limit carves nothing and the refusal is reported");
        destruction.reset();require(!destruction.capped(),"07#7: A reset clears the report");
    }
    {   // 07#9: a layer that lost most of its objects is compacted, and repeated removals still answer like a fresh layer.
        auto rock=[](int i,double x,double z,double size){
            Character c;c.Id="rock-"+std::to_string(i);c.Shape="chaos-rock";c.X=x;c.Y=.4+.2*(i%3);c.Z=z;c.Width=c.Height=c.Depth=size;c.Size=size;
            return c;
        };
        std::vector<Character> rocks{rock(0,-3,-2,1.4),rock(1,-2.6,-1.8,1.1),rock(2,0,0,1.6),rock(3,.5,.4,1.2),rock(4,4,3,1.0),rock(5,4.4,3.3,1.3),rock(6,-7,5,1.5),rock(7,8,-6,.9)};
        std::vector<Mesh> meshes;for(const auto& r:rocks)meshes.push_back(object_mesh(r));
        auto build=[&](const std::vector<unsigned>& keep){FlowObstacleMask layer;for(unsigned i:keep)layer.add(meshes[i],rocks[i]);layer.finish();return layer;};
        FlowObstacleMask edited=build({0,1,2,3,4,5,6,7});
        uint32_t seed=24681357u;auto random=[&]{seed=seed*1664525u+1013904223u;return double(seed>>8)/double(1<<24);};
        auto same=[&](const FlowObstacleMask& a,const FlowObstacleMask& b){
            if(!(a.coverage==b.coverage&&a.bottom==b.bottom&&a.top==b.top&&a.low==b.low&&a.high==b.high))return false;
            for(int k=0;k<3000;++k){
                const Vec3 p{-12+24*random(),-1+3*random(),-12+24*random()};
                const double x=a.clearance_layer(p),y=b.clearance_layer(p);
                if(std::isfinite(x)!=std::isfinite(y)||(std::isfinite(x)&&std::abs(x-y)>1e-9))return false;
                const Vec3 q=p+Vec3{random()*.3,random()*.3,random()*.3},r=p+Vec3{random()*.3,-random()*.2,random()*.3};
                if(a.clear_triangle_layer(p,q,r)!=b.clear_triangle_layer(p,q,r)||a.clear_triangle_reference_layer(p,q,r)!=b.clear_triangle_reference_layer(p,q,r))return false;
                if(a.region_empty_layer(p.x-.2,p.x+.2,p.z-.2,p.z+.2)&&!b.region_empty_layer(p.x-.2,p.x+.2,p.z-.2,p.z+.2))return false;
            }
            return true;
        };
        edited.remove_objects({0,1,2});
        require(same(edited,build({3,4,5,6,7})),"07#9: A second removal step leaves the layer answering like one built without those objects");
        const size_t before=edited.triangles.size();
        edited.remove_objects({3,4,5});
        const auto reference=build({6,7});
        require(edited.triangles.size()==reference.triangles.size()&&edited.triangles.size()<before,"07#9: Once most records are dead the layer drops them instead of keeping them for good");
        require(same(edited,reference),"07#9: A compacted layer answers like a freshly built one");
        edited.remove_objects({6});
        require(same(edited,build({7})),"07#9: Objects keep their numbers across a compaction, so a later removal takes out the right one");
        edited.remove_objects({7});
        require(edited.triangles.empty()&&same(edited,build({})),"07#9: Removing the last object leaves an empty layer");
    }
}
