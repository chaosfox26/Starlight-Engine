// Taking objects out of a finished liquid obstacle layer in place (remove_objects) must answer every query exactly as a layer built without them.
#include "flow.hpp"
#include <deque>
static void obstacle_removal_checks(const auto& require){
    auto rock=[](int i,double x,double z,double size){
        Character c;c.Id="rock-"+std::to_string(i);c.Shape="chaos-rock";c.X=x;c.Y=.4+.2*(i%3);c.Z=z;c.Width=c.Height=c.Depth=size;c.Size=size;
        return c;
    };
    // Rocks that overlap each other in plan view (so the column extents combine several objects) and some that stand alone.
    std::vector<Character> rocks{rock(0,-3,-2,1.4),rock(1,-2.6,-1.8,1.1),rock(2,0,0,1.6),rock(3,.5,.4,1.2),rock(4,4,3,1.0),rock(5,4.4,3.3,1.3),rock(6,-7,5,1.5),rock(7,8,-6,.9)};
    std::vector<Mesh> meshes;for(const auto& r:rocks)meshes.push_back(object_mesh(r));
    auto build=[&](const std::vector<unsigned>& keep){
        FlowObstacleMask layer;
        for(unsigned i:keep)layer.add(meshes[i],rocks[i]);
        layer.finish();
        return layer;
    };
    std::vector<unsigned> all;for(unsigned i=0;i<rocks.size();++i)all.push_back(i);
    uint32_t seed=987654321u;auto random=[&]{seed=seed*1664525u+1013904223u;return double(seed>>8)/double(1<<24);};
    std::deque<std::string> names; // require() keeps the text pointer, so the strings must outlive the call
    struct Case{const char* label;std::vector<unsigned> gone;};
    for(const auto& c:std::vector<Case>{{"one standalone rock",{6}},{"one of two overlapping rocks",{0}},{"both overlapping rocks",{2,3}},{"several scattered rocks",{1,4,7}},{"nothing but one left",{0,1,2,3,4,5,6}}}){
        FlowObstacleMask edited=build(all);
        std::vector<unsigned> keep;
        for(unsigned i:all)if(std::find(c.gone.begin(),c.gone.end(),i)==c.gone.end())keep.push_back(i);
        edited.remove_objects(c.gone);
        FlowObstacleMask reference=build(keep);
        const bool columns=edited.coverage==reference.coverage&&edited.bottom==reference.bottom&&edited.top==reference.top&&edited.low==reference.low&&edited.high==reference.high;
        require(columns,[&]{names.push_back(std::string("Obstacle removal: ")+c.label+" leaves the same column extents as a layer built without them");return names.back().c_str();}());
        bool clearance=true,triangles=true,regionsSafe=true,capped=true;
        for(int k=0;k<4000;++k){
            const Vec3 p{-12+24*random(),-1+3*random(),-12+24*random()};
            const double a=edited.clearance_layer(p),b=reference.clearance_layer(p);
            if(std::isfinite(a)!=std::isfinite(b)||(std::isfinite(a)&&std::abs(a-b)>1e-9))clearance=false;
            const double ac=edited.clearance_capped_layer(p,.05),bc=reference.clearance_capped_layer(p,.05);
            // clearance_capped_layer is exact below the cap and may return any value at or above it.
            if(ac<.05||bc<.05){if(!(ac<.05&&bc<.05)||std::abs(ac-bc)>1e-9)capped=false;}
            const Vec3 q=p+Vec3{random()*.3,random()*.3,random()*.3},r=p+Vec3{random()*.3,-random()*.2,random()*.3};
            if(edited.clear_triangle_layer(p,q,r)!=reference.clear_triangle_layer(p,q,r)||edited.clear_triangle_reference_layer(p,q,r)!=reference.clear_triangle_reference_layer(p,q,r))triangles=false;
            // A conservative answer is allowed: the edited layer may say "not empty" where the fresh one says "empty", never the reverse.
            if(edited.region_empty_layer(p.x-.2,p.x+.2,p.z-.2,p.z+.2)&&!reference.region_empty_layer(p.x-.2,p.x+.2,p.z-.2,p.z+.2))regionsSafe=false;
            if(edited.region_clear_layer(p.x-.2,p.x+.2,p.z-.2,p.z+.2,p.y-.1,p.y+.1,.05)&&!reference.region_clear_layer(p.x-.2,p.x+.2,p.z-.2,p.z+.2,p.y-.1,p.y+.1,.05))regionsSafe=false;
        }
        require(clearance&&capped,[&]{names.push_back(std::string("Obstacle removal: ")+c.label+" gives the same clearance at 4000 random points");return names.back().c_str();}());
        require(triangles,[&]{names.push_back(std::string("Obstacle removal: ")+c.label+" gives the same liquid-face answers, indexed and exhaustive");return names.back().c_str();}());
        require(regionsSafe,[&]{names.push_back(std::string("Obstacle removal: ")+c.label+" never reports an empty region where a fresh layer finds a solid");return names.back().c_str();}());
        require(edited.layerRevision!=0&&edited.geometryRevision==edited.layerRevision,[&]{names.push_back(std::string("Obstacle removal: ")+c.label+" changes the layer revision so memoised answers are dropped");return names.back().c_str();}());
    }
    FlowObstacleMask untouched=build(all);const auto before=untouched.layerRevision;untouched.remove_objects({});
    require(untouched.layerRevision==before,"Obstacle removal: removing nothing changes nothing");
}
