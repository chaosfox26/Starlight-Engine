#include "liquid_surface_mesh.hpp"
template<class Require> void liquid_surface_checks(Require require,const fs::path& folder){
    auto circle=[](double x,double z){return LiquidSurfacePoint{{x,.25,z},1.002-std::hypot(x-.073,z+.041)};};
    Mesh mesh=liquid_surface_mesh(96,circle);
    auto parallelMesh=liquid_surface_mesh(96,circle,4);
    bool exactParallel=mesh.indices==parallelMesh.indices&&mesh.vertices.size()==parallelMesh.vertices.size();
    if(exactParallel)for(size_t i=0;i<mesh.vertices.size();++i){const auto& a=mesh.vertices[i].position;const auto& b=parallelMesh.vertices[i].position;exactParallel&=a.x==b.x&&a.y==b.y&&a.z==b.z;}
    require(exactParallel,"Bounded parallel liquid sampling preserves exact curved shoreline vertices and topology");
    std::map<std::pair<int,int>,int> edges;
    bool finite=true,outward=true;double area=0;
    for(size_t i=0;i<mesh.indices.size();i+=3){
        auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
        auto face=(b-a).cross(c-a);outward&=face.y>1e-12;area+=face.y*.5;
        for(int j=0;j<3;++j){int u=mesh.indices[i+j],v=mesh.indices[i+(j+1)%3];edges[{std::min(u,v),std::max(u,v)}]++;}
    }
    double worstBoundaryError=0,longestBoundary=0;bool conforming=true;
    for(const auto& [e,count]:edges){
        conforming&=count==1||count==2;
        if(count==1){
            auto a=mesh.vertices[e.first].position,b=mesh.vertices[e.second].position;
            worstBoundaryError=std::max({worstBoundaryError,std::abs(circle(a.x,a.z).coverage-.002),std::abs(circle(b.x,b.z).coverage-.002)});
            longestBoundary=std::max(longestBoundary,(b-a).length());
        }
    }
    for(const auto& v:mesh.vertices)finite&=std::isfinite(v.position.x+v.position.y+v.position.z)&&circle(v.position.x,v.position.z).coverage>=.002-1e-5;
    require(!mesh.indices.empty()&&finite&&outward,"Curved liquid banks retain finite upward triangles outside solid terrain");
    require(conforming&&worstBoundaryError<1e-5,"Refined liquid bank neighbours share edges without interior cracks or duplicate seams");
    require(longestBoundary<.09&&std::abs(area-std::numbers::pi)<.006,"Curved liquid shorelines resolve sub-decimetre edges and retain the analytic pool area");
    auto dry=liquid_surface_mesh(24,[](double x,double z){return LiquidSurfacePoint{{x,0,z},0};});
    require(dry.vertices.empty()&&dry.indices.empty(),"A dry fluid domain produces no visible surface");
    auto full=liquid_surface_mesh(24,[](double x,double z){return LiquidSurfacePoint{{x,0,z},1};});
    require(full.vertices.size()==625&&full.indices.size()==24*24*6,"Interior liquid retains its original grid density without global subdivision");
    // Measure the actual bank chord against the sampled curve, not just its
    // vertex count. Boundary midpoints expose the silhouette error between
    // exact bisection endpoints on a curved wet/dry interface.
    auto contourMetrics=[&](const Mesh& surface,auto field)->json{
        std::map<std::pair<int,int>,int> incidence;double surfaceArea=0;
        bool valid=true;for(size_t i=0;i<surface.indices.size();i+=3){
            auto a=surface.vertices[surface.indices[i]].position,b=surface.vertices[surface.indices[i+1]].position,c=surface.vertices[surface.indices[i+2]].position;
            auto face=(b-a).cross(c-a);valid&=face.y>1e-12;surfaceArea+=face.y*.5;
            for(int j=0;j<3;++j){int u=surface.indices[i+j],v=surface.indices[i+(j+1)%3];incidence[{std::min(u,v),std::max(u,v)}]++;}
        }
        double chordError=0,longest=0,minimumCoverage=INFINITY;size_t contourEdges=0;
        for(const auto& [e,count]:incidence){valid&=count==1||count==2;if(count!=1)continue;
            auto a=surface.vertices[e.first].position,b=surface.vertices[e.second].position;
            double ca=field(a.x,a.z).coverage,cb=field(b.x,b.z).coverage;
            // Exclude the unchanged outer domain boundary of an island pool.
            if(std::abs(ca-.002)>1e-5||std::abs(cb-.002)>1e-5){bool domain=(std::abs(a.x+12)<1e-9&&std::abs(b.x+12)<1e-9)||(std::abs(a.x-12)<1e-9&&std::abs(b.x-12)<1e-9)||(std::abs(a.z+12)<1e-9&&std::abs(b.z+12)<1e-9)||(std::abs(a.z-12)<1e-9&&std::abs(b.z-12)<1e-9);valid&=domain;continue;}
            auto middle=(a+b)*.5;double cm=field(middle.x,middle.z).coverage;
            chordError=std::max(chordError,std::abs(cm-.002));longest=std::max(longest,(b-a).length());minimumCoverage=std::min(minimumCoverage,cm);++contourEdges;
        }
        return json{{"chordError",chordError},{"longestEdge",longest},{"area",surfaceArea},{"minimumContourCoverage",minimumCoverage},{"contourEdges",contourEdges},{"conformingUpward",valid},{"vertices",surface.vertices.size()},{"triangles",surface.indices.size()/3}};
    };
    auto explicitLegacy=liquid_surface_mesh(96,circle,1,2);
    bool exactLegacy=mesh.indices==explicitLegacy.indices&&mesh.vertices.size()==explicitLegacy.vertices.size();
    if(exactLegacy)for(size_t i=0;i<mesh.vertices.size();++i){auto a=mesh.vertices[i].position,b=explicitLegacy.vertices[i].position;exactLegacy&=a.x==b.x&&a.y==b.y&&a.z==b.z;}
    require(exactLegacy,"Explicit legacy shoreline quality retains exact default vertices and topology");
    auto medium=liquid_surface_mesh(96,circle,4,3),fine=liquid_surface_mesh(96,circle,1,4),fineParallel=liquid_surface_mesh(96,circle,4,4);
    bool exactFine=fine.indices==fineParallel.indices&&fine.vertices.size()==fineParallel.vertices.size();
    if(exactFine)for(size_t i=0;i<fine.vertices.size();++i){auto a=fine.vertices[i].position,b=fineParallel.vertices[i].position;exactFine&=a.x==b.x&&a.y==b.y&&a.z==b.z;}
    auto coarseMetrics=contourMetrics(mesh,circle),mediumMetrics=contourMetrics(medium,circle),fineMetrics=contourMetrics(fine,circle);
    require(mediumMetrics["conformingUpward"].get<bool>()&&mediumMetrics["chordError"].get<double>()<coarseMetrics["chordError"].get<double>()*.4&&mediumMetrics["longestEdge"].get<double>()<coarseMetrics["longestEdge"].get<double>()*.65,"Three-pass shoreline refinement measurably reduces contour error and edge length without interior cracks");
    require(exactFine&&fineMetrics["conformingUpward"].get<bool>(),"High-quality curved liquid banks retain exact parallel sampling and conforming upward triangles");
    require(fineMetrics["chordError"].get<double>()<coarseMetrics["chordError"].get<double>()*.25&&fineMetrics["longestEdge"].get<double>()<coarseMetrics["longestEdge"].get<double>()*.35,"High-quality shoreline geometry reduces measured curved contour error and segment length");
    require(std::abs(fineMetrics["area"].get<double>()-std::numbers::pi)<std::abs(area-std::numbers::pi)*.25,"Additional shoreline samples improve analytic pool area without changing the liquid field");
    auto island=[](double x,double z){return LiquidSurfacePoint{{x,.25,z},.002+std::hypot(x-.073,z+.041)-1};};
    auto coarseIsland=liquid_surface_mesh(96,island,1,2),fineIsland=liquid_surface_mesh(96,island,4,4);
    auto coarseIslandMetrics=contourMetrics(coarseIsland,island),fineIslandMetrics=contourMetrics(fineIsland,island);
    require(fineIslandMetrics["conformingUpward"].get<bool>()&&fineIslandMetrics["chordError"].get<double>()<coarseIslandMetrics["chordError"].get<double>()*.25&&fineIslandMetrics["minimumContourCoverage"].get<double>()>=0,"Refined dry-island silhouette reduces chord intrusion while maintaining actual solid clearance");
    auto highFull=liquid_surface_mesh(24,[](double x,double z){return LiquidSurfacePoint{{x,0,z},1};},4,5);
    require(highFull.vertices.size()==full.vertices.size()&&highFull.indices==full.indices,"Highest shoreline quality leaves a fully wet interior at its original exact grid density");
    auto bounded=liquid_surface_mesh(24,circle,1,999),highest=liquid_surface_mesh(24,circle,1,5);
    require(bounded.indices==highest.indices&&bounded.vertices.size()==highest.vertices.size(),"Requested shoreline quality is bounded to five local refinement passes");
    auto tinyIsland=[](double x,double z){return LiquidSurfacePoint{{x,.25,z},.002+std::hypot(x-.5,z)-.12};};
    auto missedIsland=liquid_surface_mesh(24,tinyIsland,1,2),resolvedIsland=liquid_surface_mesh(24,tinyIsland,4,5);
    auto missedMetrics=contourMetrics(missedIsland,tinyIsland),resolvedMetrics=contourMetrics(resolvedIsland,tinyIsland);
    require(missedMetrics["contourEdges"].get<size_t>()==0&&resolvedMetrics["contourEdges"].get<size_t>()>8&&resolvedMetrics["area"].get<double>()<575.975&&resolvedMetrics["conformingUpward"].get<bool>(),"Detailed edge probes recover a sub-grid dry island that legacy endpoint and centroid probes miss");
    atomic_write(folder/"liquid-bank-refinement.json",json{{"legacyPool",coarseMetrics},{"threePassPool",mediumMetrics},{"refinedPool",fineMetrics},{"legacyIsland",coarseIslandMetrics},{"refinedIsland",fineIslandMetrics},{"legacyTinyIsland",missedMetrics},{"refinedTinyIsland",resolvedMetrics},{"exactLegacy",exactLegacy},{"exactParallel",exactFine},{"maximumBoundaryPasses",5}}.dump(2));
    // Compare the fast column search with the public three-dimensional SDF,
    // including ordered unions/subtractions, domain edges and missed columns.
    auto columnTerrain=std::make_unique<TerrainData>();
    for(int z=0;z<97;++z)for(int x=0;x<97;++x)columnTerrain->Heights[z*97+x]=.3*std::sin(x*.13)*std::cos(z*.17);
    columnTerrain->Volumes={{.2,-.5,.1,1.7,true},{.7,.3,-.4,1.1,false},{-.3,.2,.6,.8,true},{3,-2,2,2.4,true}};
    auto referenceColumn=[&](double x,double z){double top=columnTerrain->height_at(x,z)+.5;for(const auto& v:columnTerrain->Volumes)if(!v.Subtract)top=std::max(top,v.Y+v.Radius+.5);bool affected=false;for(const auto& v:columnTerrain->Volumes)if(std::hypot(x-v.X,z-v.Z)<v.Radius+.3){affected=true;break;}if(!affected)return columnTerrain->height_at(x,z);for(double high=top;high> -21.;high-=.125){double low=std::max(-21.,high-.125);if(columnTerrain->density_at(x,low,z)<=0){for(int i=0;i<16;++i){double mid=(low+high)*.5;if(columnTerrain->density_at(x,mid,z)<=0)low=mid;else high=mid;}return (low+high)*.5;}}return -21.;};
    bool exactColumns=true;for(int z=0;z<31;++z)for(int x=0;x<31;++x){double px=-4.73+x*.313,pz=-4.17+z*.297;exactColumns&=columnTerrain->surface_at(px,pz)==referenceColumn(px,pz);}
    for(double x:{-12.,12.,-12.01,12.01})for(double z:{-12.,12.,-12.01,12.01})exactColumns&=columnTerrain->surface_at(x,z)==referenceColumn(x,z);
    require(exactColumns,"Optimized carved column queries retain exact public SDF intersections across ordered operations and domain edges");
    // A real carved bank is nonlinear in both the liquid and terrain fields.
    // Every exposed shoreline vertex must lie on the actual bed rather than
    // a linearly interpolated endpoint estimate of that bed.
    auto terrain=std::make_unique<TerrainData>();terrain->WaterEnabled=true;terrain->WaterLevel=-.4;
    terrain->Volumes.push_back({.073,-.35,-.041,1.3,true});
    auto flow=std::make_unique<TerrainFlow>(*terrain,std::vector<LiquidSource>{});
    auto before=flow->water;double volume=flow->volume();auto parts=flow->geometry();
    bool clear=true,sharedSurface=true,clearInteriors=true;size_t vertices=0;double minimumGap=INFINITY,maximumSurfaceError=0,minimumTriangleDensity=INFINITY;
    for(const auto& part:parts)for(const auto& v:part.mesh.vertices){
        ++vertices;double gap=v.position.y-terrain->surface_at(v.position.x,v.position.z);minimumGap=std::min(minimumGap,gap);clear&=gap>=.002-1e-4;
        auto liquid=flow->liquid_at(v.position.x,v.position.z);if(liquid.second>0){double error=std::abs(v.position.y-liquid.first-.009);maximumSurfaceError=std::max(maximumSurfaceError,error);sharedSurface&=error<1e-8;}
    }
    for(const auto& part:parts)for(size_t i=0;i<part.mesh.indices.size();i+=3){
        auto a=part.mesh.vertices[part.mesh.indices[i]].position,b=part.mesh.vertices[part.mesh.indices[i+1]].position,c=part.mesh.vertices[part.mesh.indices[i+2]].position;
        for(auto p:{(a+b+c)/3.,(a+b)*.5,(b+c)*.5,(c+a)*.5}){double density=terrain->density_at(p.x,p.y,p.z);minimumTriangleDensity=std::min(minimumTriangleDensity,density);clearInteriors&=density>=-1e-6;}
    }
    atomic_write(folder/"liquid-bank-clearance.json",json{{"vertices",vertices},{"clear",clear},{"sharedSurface",sharedSurface},{"minimumGap",minimumGap},{"maximumSurfaceError",maximumSurfaceError},{"clearTriangleInteriors",clearInteriors},{"minimumTriangleDensity",minimumTriangleDensity}}.dump(2));
    require(vertices>0&&clear&&sharedSurface,"Carved liquid banks clip to the actual solid contour while matching the physics surface");
    require(clearInteriors,"Refined real carved-bank triangle centres and edge midpoints remain outside the terrain solid");
    require(flow->water==before&&flow->volume()==volume,"Boundary refinement changes visible geometry without altering fluid cells or volume");
    atomic_write(folder/"liquid-bank-diagnostics.json",json{{"vertices",mesh.vertices.size()},{"triangles",mesh.indices.size()/3},{"boundaryError",worstBoundaryError},{"longestBoundaryEdge",longestBoundary},{"area",area},{"realCarvedVertices",vertices},{"actualSolidClearance",clear}}.dump(2));
    auto scene=std::make_unique<SceneData>();scene->Name="Curved carved liquid banks";scene->Terrain=*terrain;scene->Hour=14;scene->FogDensity=0;scene->Game.emplace();scene->Game->SpawnZ=4;
    scene->WaterWindX=scene->WaterWindZ=scene->WaterWaveStrength=0;
    auto packet=render_scene(*scene,flow.get());atomic_write(folder/"liquid-bank-water.bin",std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));
    atomic_write(folder/"liquid-bank-water.starscene",scene->encode());
    scene->Terrain->Lava=true;auto molten=std::make_unique<TerrainFlow>(*scene->Terrain,std::vector<LiquidSource>{});molten->lava_settings(.7,0,.04,1,true);
    packet=render_scene(*scene,molten.get());atomic_write(folder/"liquid-bank-lava.bin",std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));
}
