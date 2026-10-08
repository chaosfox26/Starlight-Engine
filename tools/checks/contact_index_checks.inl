// The grass contact index must count exactly the boxes a loop over all of them would (grass reach, which used to cost grass parts x bodies every frame).
#include "../../deep/engine/contact_index.h"
static void contact_index_checks(const auto& require){
    uint32_t seed=24681357u;auto random=[&]{seed=seed*1664525u+1013904223u;return float(double(seed>>8)/double(1<<24));};
    auto between=[&](float a,float b){return a+(b-a)*random();};
    ContactIndex index;
    bool same=true;unsigned checked=0;
    for(int round=0;round<6;++round){
        std::vector<ContactIndex::Box> boxes;
        const int count=round==0?0:round==1?1:40*round*round;
        for(int i=0;i<count;++i){
            // Mostly boxes inside the arena, some wide ones, some far outside it, some flat or tiny.
            float x=between(-14,14),z=between(-14,14),y=between(-1,3),w=between(0,round%2?4.f:.6f),d=between(0,round%2?4.f:.6f),h=between(0,2);
            if(i%11==0){x=between(-80,80);z=between(-80,80);}
            boxes.push_back({x,y,z,x+w,y+h,z+d});
        }
        index.build(boxes);
        for(int q=0;q<600;++q){
            float lx=between(-16,16),lz=between(-16,16),ly=between(-1.5f,3),hx=lx+between(0,3),hz=lz+between(0,3),hy=ly+between(0,2);
            if(q%17==0){lx=between(-200,200);hx=lx+between(0,50);}
            ++checked;
            if(index.count(lx,ly,lz,hx,hy,hz)!=index.count_reference(lx,ly,lz,hx,hy,hz))same=false;
        }
        // Edges: a query that touches only through the padding, exactly on a cell border, and across the whole arena.
        for(const auto& b:boxes){
            ++checked;if(index.count(b.highX+.2f,b.lowY,b.highZ+.2f,b.highX+.2f,b.highY,b.highZ+.2f)!=index.count_reference(b.highX+.2f,b.lowY,b.highZ+.2f,b.highX+.2f,b.highY,b.highZ+.2f))same=false;
            ++checked;if(index.count(std::floor(b.lowX),b.lowY,std::floor(b.lowZ),std::floor(b.lowX),b.highY,std::floor(b.lowZ))!=index.count_reference(std::floor(b.lowX),b.lowY,std::floor(b.lowZ),std::floor(b.lowX),b.highY,std::floor(b.lowZ)))same=false;
        }
        ++checked;if(index.count(-1e6f,-1e6f,-1e6f,1e6f,1e6f,1e6f)!=index.count_reference(-1e6f,-1e6f,-1e6f,1e6f,1e6f,1e6f))same=false;
    }
    require(same&&checked>3000,"Contact index: counts match the plain loop over every box for thousands of random, edge and far-away queries");
    const unsigned nan=index.count(std::nanf(""),0,0,1,1,1);
    require(nan<=index.count_reference(-1e6f,-1e6f,-1e6f,1e6f,1e6f,1e6f),"Contact index: a non-finite query neither crashes nor counts more than exist");
    ContactIndex empty;empty.build({});
    require(empty.count(-1,-1,-1,1,1,1)==0,"Contact index: no boxes means a count of zero");
    // The same box listed in several cells is counted once.
    ContactIndex wide;wide.build({{-10,0,-10,10,1,10}});
    require(wide.count(-12,0,-12,12,1,12)==1,"Contact index: one wide box that fills many cells counts once");
    for(unsigned k=0;k<5;++k)wide.count(0,0,0,1,1,1);
    require(wide.count(0,0,0,1,1,1)==1,"Contact index: repeated queries keep giving the same count (stamps reset between queries)");
}
