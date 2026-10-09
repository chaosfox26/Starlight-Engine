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
    {   // 07#6: two scenes with different ground alternate without rebuilding each other's terrain mesh.
        auto ground=[](double offset){TerrainData t;for(int j=0;j<TerrainData::N;++j)for(int i=0;i<TerrainData::N;++i)t.Heights[size_t(j)*TerrainData::N+i]=offset+.3*std::sin(i*.2)*std::cos(j*.17);t.Volumes.push_back({1.5,offset,1.5,.8,true});return t;};
        auto sameMesh=[](const Mesh& a,const Mesh& b,double tolerance=0){if(a.indices!=b.indices||a.vertices.size()!=b.vertices.size())return false;for(size_t i=0;i<a.vertices.size();++i)if((a.vertices[i].position-b.vertices[i].position).length()>tolerance)return false;return true;};
        const TerrainData a=ground(.2),b=ground(-.4),c=ground(.9);
        TerrainVolumeCachePool pool;
        const Mesh firstA=pool.get(a);const size_t coldA=pool.rebuiltChunks;
        const Mesh firstB=pool.get(b);const size_t coldB=pool.rebuiltChunks;
        require(coldA==169&&coldB==169&&!firstA.indices.empty()&&!sameMesh(firstA,firstB),"07#6: Two different grounds each build their mesh once");
        bool reused=true,unchanged=true;
        for(int round=0;round<3;++round){
            const Mesh againA=pool.get(a);reused&=pool.rebuiltChunks==0;unchanged&=sameMesh(againA,firstA);
            const Mesh againB=pool.get(b);reused&=pool.rebuiltChunks==0;unchanged&=sameMesh(againB,firstB);
        }
        require(reused&&unchanged,"07#6: Alternating between the two grounds rebuilds nothing and returns the same meshes");
        auto edited=a;edited.Volumes.push_back({-6,.1,-6,.6,true});
        const Mesh incremental=pool.get(edited);const size_t touched=pool.rebuiltChunks;
        TerrainVolumeCache clean;
        require(touched>0&&touched<169&&sameMesh(incremental,clean.get(edited),1e-8),"07#6: A new hole on a known ground rebuilds only the chunks it touches, and matches a mesh built from nothing");
        pool.get(c);
        const size_t afterThird=pool.rebuiltChunks;pool.get(edited);const size_t editedAgain=pool.rebuiltChunks;
        require(afterThird==169&&editedAgain==0,"07#6: A third ground replaces the cache used longest ago and keeps the other");
    }
    {   // 07#8: one non-finite vertex costs that liquid its frame; it does not throw into the render loop.
        TerrainData terrain;terrain.WaterEnabled=true;terrain.WaterLevel=1.5;
        auto lake=std::make_unique<TerrainFlow>(terrain,std::vector<LiquidSource>{});
        const auto healthy=lake->geometry();
        require(!healthy.empty(),"07#8: The test lake produces a water surface");
        const uint64_t before=flow_nonfinite_parts_dropped();
        const double good=lake->water[1000];lake->water[1000]=INFINITY;
        bool threw=false;std::vector<MeshPart> broken;
        try{broken=lake->geometry();}catch(...){threw=true;}
        require(!threw&&broken.empty()&&flow_nonfinite_parts_dropped()==before+1,"07#8: A non-finite water height drops the water surface for that frame, is counted, and does not throw");
        lake->geometry_begin_async();
        threw=false;try{(void)lake->geometry_for_render();lake->wind(1,0);}catch(...){threw=true;}
        require(!threw,"07#8: The one-frame-ahead path does not store an error that the next change would throw into the render loop");
        lake->water[1000]=good;
        require(lake->geometry().size()==healthy.size(),"07#8: The next frame with a finite height builds the liquid again");
    }
    {   // 07#2: the surface search skips volumes that cannot change a decision, so it must return exactly what the full evaluation returns.
        auto reference=[](const TerrainData& t,double x,double z){ // the search as it was: every volume at every probe
            if(t.Volumes.empty())return t.height_at(x,z);
            const double height=t.height_at(x,z);double top=height+.5;
            for(const auto& v:t.Volumes)if(!v.Subtract)top=std::max(top,v.Y+v.Radius+.5);
            bool affected=false;for(const auto& v:t.Volumes)if(std::hypot(x-v.X,z-v.Z)<v.Radius+.3){affected=true;break;}
            if(!affected)return height;
            for(double high=top;high> -21.;high-=.125){double low=std::max(-21.,high-.125);
                if(t.density_at(x,low,z)<=0){for(int i=0;i<16;++i){double mid=(low+high)*.5;if(t.density_at(x,mid,z)<=0)low=mid;else high=mid;}return (low+high)*.5;}
            }
            return -21.;
        };
        uint32_t seed=424242u;auto random=[&]{seed=seed*1664525u+1013904223u;return double(seed>>8)/double(1<<24);};
        size_t compared=0,carved=0;bool identical=true;
        for(int scene=0;scene<12;++scene){
            TerrainData ground;for(auto& h:ground.Heights)h=-1+2*random();
            const int holes=scene==0?1:scene<4?20:scene<8?60:128; // a crowded field overlaps its blends; a sparse one leaves most volumes far away
            for(int i=0;i<holes;++i)ground.Volumes.push_back({-11.5+23*random(),-3+5*random(),-11.5+23*random(),.25+(scene<8?1.:3.)*random(),random()<(scene%3==2?.5:.85)});
            for(int k=0;k<1500;++k){
                double x=-12+24*random(),z=-12+24*random();
                if(k%3==0&&!ground.Volumes.empty()){const auto& v=ground.Volumes[size_t(random()*ground.Volumes.size())%ground.Volumes.size()];x=v.X+(random()*2-1)*(v.Radius+.4);z=v.Z+(random()*2-1)*(v.Radius+.4);}
                const double a=ground.surface_at(x,z),b=reference(ground,x,z);
                identical&=a==b;++compared;carved+=a<ground.height_at(x,z)-.05;
            }
        }
        require(compared==18000&&carved>1500,"07#2: The comparison covers carved columns");
        require(identical,"07#2: The surface search returns bit-for-bit what the full evaluation of every volume returns");
    }
    {   // 07#3: a triangle is registered only in the cells its x/z projection meets, and every query answers as if its whole bounding box were registered.
        constexpr int G=FlowObstacleMask::GeometryN;
        auto cellOf=[](double v){return std::clamp(int(std::floor((v+12)*G/24)),0,G-1);};
        auto registrations=[](const FlowObstacleMask& m){size_t n=0;for(const auto& c:m.triangleCells)n+=c.size();return n;};
        auto boxed=[&](const FlowObstacleMask& source){ // the previous scheme: every cell of the bounding box
            FlowObstacleMask wide=source;for(auto& c:wide.triangleCells)c.clear();
            for(unsigned id=0;id<wide.triangles.size();++id){const auto& t=wide.triangles[id];for(int z=cellOf(t.boundsLow.z);z<=cellOf(t.boundsHigh.z);++z)for(int x=cellOf(t.boundsLow.x);x<=cellOf(t.boundsHigh.x);++x)wide.triangleCells[size_t(z)*G+x].push_back(id);}
            wide.finish();return wide;
        };
        uint32_t seed=97531u;auto random=[&]{seed=seed*1664525u+1013904223u;return double(seed>>8)/double(1<<24);};
        {   // one long diagonal triangle no longer fills its 24 m x 24 m bounding box
            Mesh sliver;sliver.vertices.resize(3);sliver.vertices[0].position={-12,0,-12};sliver.vertices[1].position={12,0,12};sliver.vertices[2].position={12,1,11.9};sliver.indices={0,1,2};
            FlowObstacleMask diagonal;diagonal.add(sliver,Character{});diagonal.finish();
            require(diagonal.triangles.size()==1&&diagonal.triangles[0].c.z==11.9&&registrations(boxed(diagonal))==size_t(G)*G,"07#3: The diagonal test triangle spans the whole domain");
            require(registrations(diagonal)<2500&&registrations(diagonal)>=size_t(G),"07#3: A long diagonal triangle is registered in a thin band of cells, not in its whole bounding box");
            bool covered=true;const auto& t=diagonal.triangles[0];
            for(int k=0;k<4000;++k){double u=random(),v=random();if(u+v>1){u=1-u;v=1-v;}const Vec3 p=t.a*(1-u-v)+t.b*u+t.c*v;const auto& list=diagonal.triangleCells[size_t(cellOf(p.z))*G+cellOf(p.x)];covered&=std::find(list.begin(),list.end(),0u)!=list.end();}
            require(covered,"07#3: Every point of the triangle's projection lies in a cell that lists it");
        }
        std::vector<Character> solids;
        for(int i=0;i<16;++i){
            Character c;c.Id="solid-"+std::to_string(i);c.Shape=i%3==0?"chaos-rock":"cube";c.KeepAboveFloor=false;
            c.X=-13+26*random();c.Y=2*random();c.Z=-13+26*random();c.Size=1;
            c.Width=i%4==1?.2+9*random():.3+2*random();c.Height=.1+2*random();c.Depth=i%4==2?.2+9*random():.3+2*random(); // some long and thin
            c.RotationY=360*random();if(i%4==3){c.RotationX=40*random();c.RotationZ=40*random();}
            solids.push_back(c);
        }
        FlowObstacleMask fresh;for(const auto& c:solids)fresh.add(object_mesh(c),c);fresh.finish();
        const FlowObstacleMask wide=boxed(fresh);
        require(registrations(fresh)<registrations(wide),"07#3: The scene's solids need fewer registrations than their bounding boxes");
        bool depth=true,capped=true,triangles=true,reference=true,emptyRegions=true,clearRegions=true;size_t blocked=0,close=0;
        for(int k=0;k<6000;++k){
            const Character& around=solids[size_t(random()*solids.size())%solids.size()];
            const double spread=.5+std::max({around.Width,around.Depth})*.7;
            const Vec3 p{around.X+(random()*2-1)*spread,around.Y+(random()*2-1)*(1+around.Height),around.Z+(random()*2-1)*spread};
            const double a=fresh.clearance_layer(p),b=wide.clearance_layer(p);
            depth&=std::isfinite(a)==std::isfinite(b)&&(!std::isfinite(a)||std::abs(a-b)<1e-9);
            const double ca=fresh.clearance_capped_layer(p,.05),cb=wide.clearance_capped_layer(p,.05);
            capped&=(ca<.05||cb<.05)?std::abs(ca-cb)<1e-9:true;
            if(std::isfinite(a)&&a<0)++close;
            const double edge=.05+.5*random();
            const Vec3 q=p+Vec3{(random()*2-1)*edge,(random()*2-1)*edge*.3,(random()*2-1)*edge},r=p+Vec3{(random()*2-1)*edge,(random()*2-1)*edge*.3,(random()*2-1)*edge};
            const bool freshClear=fresh.clear_triangle_layer(p,q,r);if(!freshClear)++blocked;
            triangles&=freshClear==wide.clear_triangle_layer(p,q,r);
            reference&=fresh.clear_triangle_reference_layer(p,q,r)==wide.clear_triangle_reference_layer(p,q,r)&&freshClear==fresh.clear_triangle_reference_layer(p,q,r);
            const double xLow=std::min({p.x,q.x,r.x}),xHigh=std::max({p.x,q.x,r.x}),zLow=std::min({p.z,q.z,r.z}),zHigh=std::max({p.z,q.z,r.z}),yLow=std::min({p.y,q.y,r.y}),yHigh=std::max({p.y,q.y,r.y});
            emptyRegions&=!wide.region_empty_layer(xLow,xHigh,zLow,zHigh)||fresh.region_empty_layer(xLow,xHigh,zLow,zHigh);
            if(fresh.region_clear_layer(xLow,xHigh,zLow,zHigh,yLow,yHigh,.05))clearRegions&=wide.clear_triangle_layer(p,q,r)&&wide.clearance_capped_layer((p+q+r)/3,.05)>=.05&&wide.clearance_capped_layer(p,.05)>=.05;
        }
        require(close>50&&blocked>50,"07#3: The probes include points inside solids and triangles that touch them");
        require(depth,"07#3: Column clearance is unchanged by registering only the cells a triangle meets");
        require(capped,"07#3: Capped clearance is unchanged wherever it is below the cap");
        require(triangles&&reference,"07#3: Triangle clearance, indexed and exhaustive, is unchanged");
        require(emptyRegions&&clearRegions,"07#3: The empty-region and clear-region shortcuts only skip probes that find nothing in the box-registered layer either");
        // Taking objects back out (truncate) walks the same cells it was registered in.
        FlowObstacleMask grown;for(size_t i=0;i<6;++i)grown.add(object_mesh(solids[i]),solids[i]);grown.finish();
        const auto mark=grown.mark();const auto expected=grown.triangleCells;
        for(size_t i=6;i<12;++i)grown.add(object_mesh(solids[i]),solids[i]);
        grown.truncate(mark);
        require(grown.triangleCells==expected&&grown.triangles.size()==mark.triangleCount,"07#3: Truncating a layer removes exactly the registrations of the triangles it drops");
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
    {   // 07-T1: a very large obstacle covers the same liquid columns as a moderate one. Cell numbers used to be converted to int before clamping, so a far edge past about 1e9 m wrapped to cell 0.
        auto covered=[](double size){Character c;c.Shape="cube";c.KeepAboveFloor=false;c.Size=size;FlowObstacleMask mask;mask.add(shape_geometry(c).front().mesh,c);mask.finish();size_t n=0;for(double v:mask.coverage)if(v>0)++n;return n;};
        const size_t moderate=covered(1e6),vast=covered(1e10);
        require(moderate>0&&vast==moderate,"07-T1: A vast obstacle occupies the same liquid columns as a moderate one");
    }
    {   // 07-T2: a lava patch far larger than the terrain paints every cell of the terrain, as a moderate one does.
        auto painted=[](double size){Character patch;patch.Shape="lava";patch.Size=size;std::vector<Character> objects{patch};TerrainData flat;TerrainFlow flow(flat,std::vector<LiquidSource>{},objects);size_t n=0;for(double v:flow.lava)if(v>0)++n;return n;};
        const size_t moderate=painted(1e6),vast=painted(1e10);
        require(moderate==TerrainFlow::N*TerrainFlow::N&&vast==moderate,"07-T2: A vast lava patch fills the same cells as a moderate one");
    }
}
