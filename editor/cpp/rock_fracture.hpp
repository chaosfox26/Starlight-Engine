#pragma once
#include "fracture.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <future>
#include <stop_token>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace starlight::editor {
inline bool closed_rock_body_mesh(const Mesh& mesh){
    Vec3 low{INFINITY,INFINITY,INFINITY},high{-INFINITY,-INFINITY,-INFINITY};
    for(const auto& v:mesh.vertices){auto p=v.position;low={std::min(low.x,p.x),std::min(low.y,p.y),std::min(low.z,p.z)};high={std::max(high.x,p.x),std::max(high.y,p.y),std::max(high.z,p.z)};}
    auto centered=mesh;auto center=(low+high)*.5;for(auto& v:centered.vertices)v.position=v.position-center;
    return orient_fracture_mesh(centered);
}
// Build each new cleavage surface once, then install the identical surface
// with opposite winding in its two neighboring solids. Boundary vertices stay
// fixed; displaced interior vertices produce real mineral facets without gaps
// or a shader-only illusion. Limit displacement against the existing solid.
inline void roughen_rock_cut(const Mesh& source,Mesh& a,Mesh& b,double plane,float kind,uint32_t seed){
    auto cap=[&](const Mesh& mesh,size_t t){for(int j=0;j<3;++j){const auto& v=mesh.vertices[mesh.indices[t+j]];if(v.kind!=kind||std::abs(v.position.x-plane)>1e-7)return false;}return true;};
    // yLow..zHigh bound the triangle in the ray's (y,z) plane, widened far beyond the 1e-8 barycentric tolerance (and any rounding
    // in it); nearly edge-on triangles are never skipped, so every triangle the exact test could accept still reaches it.
    struct ClearanceTriangle{Vec3 p,q,r;double det;double yLow,yHigh,zLow,zHigh;bool bounded;};std::vector<ClearanceTriangle> clearanceTriangles;clearanceTriangles.reserve(source.indices.size()/3);
    for(size_t i=0;i<source.indices.size();i+=3){auto p=source.vertices[source.indices[i]].position,q=source.vertices[source.indices[i+1]].position,r=source.vertices[source.indices[i+2]].position;double det=(q.y-p.y)*(r.z-p.z)-(q.z-p.z)*(r.y-p.y);
        double yLow=std::min({p.y,q.y,r.y}),yHigh=std::max({p.y,q.y,r.y}),zLow=std::min({p.z,q.z,r.z}),zHigh=std::max({p.z,q.z,r.z});
        const double scale=std::max(yHigh-yLow,zHigh-zLow),margin=1e-6*scale+1e-12;
        clearanceTriangles.push_back({p,q,r,det,yLow-margin,yHigh+margin,zLow-margin,zHigh+margin,std::isfinite(det)&&std::isfinite(scale)&&std::abs(det)>=1e-9*scale*scale});}
    std::vector<std::array<Vertex,3>> facets;
    for(size_t t=0;t<a.indices.size();t+=3)if(cap(a,t)){
        auto x=a.vertices[a.indices[t]],y=a.vertices[a.indices[t+1]],z=a.vertices[a.indices[t+2]];
        Vertex center{(x.position+y.position+z.position)/3.,{},(x.color+y.color+z.color)/3.,kind};
        double edge=std::min({(x.position-y.position).length(),(y.position-z.position).length(),(z.position-x.position).length()});
        uint32_t hash=seed;for(double q:{center.position.y,center.position.z})hash=(hash^uint32_t(int64_t(std::llround(q*1e7))))*16777619u;
        double offset=(double(hash&65535)/65535.*2-1)*edge*.16;
        double clearance=INFINITY;
        // Intersect the displacement ray with actual source triangles; using
        // only the bounding box would allow facets to protrude out of a wedge.
        for(const auto& triangle:clearanceTriangles){
            if(triangle.bounded&&(center.position.y<triangle.yLow||center.position.y>triangle.yHigh||center.position.z<triangle.zLow||center.position.z>triangle.zHigh))continue;
            auto p=triangle.p,q=triangle.q,r=triangle.r;double det=triangle.det;
            if(std::abs(det)<1e-14)continue;
            double u=((center.position.y-p.y)*(r.z-p.z)-(center.position.z-p.z)*(r.y-p.y))/det,v=((q.y-p.y)*(center.position.z-p.z)-(q.z-p.z)*(center.position.y-p.y))/det;
            if(u< -1e-8||v< -1e-8||u+v>1+1e-8)continue;double distance=p.x+(q.x-p.x)*u+(r.x-p.x)*v-plane;
            if(distance*offset>0)clearance=std::min(clearance,std::abs(distance));
        }
        offset=std::copysign(std::min(std::abs(offset),clearance*.3),offset);center.position.x+=offset;
        for(auto triangle:{std::array<Vertex,3>{x,y,center},std::array<Vertex,3>{y,z,center},std::array<Vertex,3>{z,x,center}}){auto n=(triangle[1].position-triangle[0].position).cross(triangle[2].position-triangle[0].position);n=n.length()>0?n/n.length():x.normal;for(auto& vertex:triangle)vertex.normal=n;facets.push_back(triangle);}
    }
    auto install=[&](Mesh& mesh,bool reverse){std::vector<int> indices;for(size_t t=0;t<mesh.indices.size();t+=3)if(!cap(mesh,t))indices.insert(indices.end(),mesh.indices.begin()+t,mesh.indices.begin()+t+3);mesh.indices=std::move(indices);
        for(auto triangle:facets){if(reverse){std::swap(triangle[1],triangle[2]);for(auto& vertex:triangle)vertex.normal=vertex.normal*-1;}int base=int(mesh.vertices.size());mesh.vertices.insert(mesh.vertices.end(),triangle.begin(),triangle.end());mesh.indices.insert(mesh.indices.end(),{base,base+1,base+2});}
        std::vector<Vertex> used;used.reserve(mesh.indices.size());for(auto& index:mesh.indices){used.push_back(mesh.vertices[index]);index=int(used.size()-1);}mesh.vertices=std::move(used);
    };install(a,false);install(b,true);
}
// Rock cuts draw their planes from one sequential random stream, and a cut that is rejected (open cap, non-closed half)
// retries on the next four draws. Every attempt is therefore a pure function of (solid, stream slot): attempt number i of the
// round always starts at the stream advanced by 4*i draws. Each round resolves the solids strictly in order exactly like the
// serial algorithm, but computes the attempts that are most likely to be needed next in parallel (speculating by how often
// attempts are accepted) and only ever consumes the attempt the serial order would have used. The fragments are bit-identical
// to the serial result; discarded speculation costs only CPU on helper threads. Helpers come from a shared budget (the
// prefracture workers run in parallel too) and run below normal priority; without free slots the work stays on the caller.
inline std::atomic<int>& rock_fracture_helper_slots(){
    static std::atomic<int> slots{[]{
        unsigned n=std::thread::hardware_concurrency();int helpers=int(n>=12?std::min(6u,n-8):2u);
        if(const char* text=std::getenv("STARLIGHT_FRACTURE_HELPERS")){char* end=nullptr;long value=std::strtol(text,&end,10);if(end!=text&&value>=0&&value<=64)helpers=int(value);}
        return helpers;}()};
    return slots;
}
template<class Fn> inline void rock_fracture_parallel_for(size_t count,size_t lanes,Fn&& fn){
    if(count==0)return;
    auto& slots=rock_fracture_helper_slots();
    std::atomic<size_t> next{0};
    auto drain=[&]{for(size_t i;(i=next.fetch_add(1))<count;)fn(i);};
    std::vector<std::future<void>> helpers;
    for(size_t h=1;h<std::min(count,lanes);++h){
        if(slots.fetch_sub(1)<=0){slots.fetch_add(1);break;}
        try{helpers.push_back(std::async(std::launch::async,[&]{
#ifdef _WIN32
            // The task runs on a shared pool thread that outlives it, so the priority goes back when the task ends.
            struct PriorityRestore{int previous=GetThreadPriority(GetCurrentThread());~PriorityRestore(){if(previous!=THREAD_PRIORITY_ERROR_RETURN)SetThreadPriority(GetCurrentThread(),previous);}} restore;
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
            drain();}));}catch(...){slots.fetch_add(1);break;}
    }
    std::exception_ptr error;
    try{drain();}catch(...){error=std::current_exception();}
    for(auto& helper:helpers){try{helper.get();}catch(...){if(!error)error=std::current_exception();}slots.fetch_add(1);}
    if(error)std::rethrow_exception(error);
}
// Closed geological chunks retain the source exterior instead of reducing a
// detailed rock to independent thin triangle shells. No changes to authored
// primitive, imported or unsupported-shell fracture behavior.
inline std::vector<FracturePiece> fracture_rock(Mesh source,Vec3 tint,bool emissive=false,uint32_t seed=8241,std::string* failure=nullptr,std::stop_token stop={}){
    auto fail=[&](const std::string& reason){if(failure)*failure=reason;return std::vector<FracturePiece>{};};
    if(source.indices.empty()||source.indices.size()>80000||!orient_fracture_mesh(source))return fail("Unsupported source topology or size");
    // Pebble clusters contain independent closed rocks. Partition those before
    // cutting so a rigid body's convex hull never spans disconnected pebbles.
    using Key=std::tuple<int64_t,int64_t,int64_t>;
    std::map<Key,int> ids;std::vector<int> vertexIds;std::vector<int> parent(source.indices.size()/3);
    for(const auto& v:source.vertices){auto p=v.position;auto [i,added]=ids.emplace(Key{std::llround(p.x*1e7),std::llround(p.y*1e7),std::llround(p.z*1e7)},int(ids.size()));vertexIds.push_back(i->second);}
    for(int i=0;i<int(parent.size());++i)parent[i]=i;
    auto root=[&](int i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
    std::map<std::pair<int,int>,int> edgeFaces;
    for(int t=0;t<int(parent.size());++t)for(int e=0;e<3;++e){int a=vertexIds[source.indices[t*3+e]],b=vertexIds[source.indices[t*3+(e+1)%3]];auto [i,added]=edgeFaces.emplace(std::pair{std::min(a,b),std::max(a,b)},t);if(!added)parent[root(t)]=root(i->second);}
    std::map<int,Mesh> components;
    for(int t=0;t<int(parent.size());++t){auto& m=components[root(t)];for(int e=0;e<3;++e){m.indices.push_back(int(m.vertices.size()));m.vertices.push_back(source.vertices[source.indices[t*3+e]]);}}
    if(components.size()>16)return fail("Component count exceeds sixteen");
    auto advance=[](uint32_t& state){state=state*1664525u+1013904223u;return double(state>>8)/16777216.;};
    struct Attempt{bool ok=false;Mesh a,b;};
    // One cut attempt of one solid starting at the given stream position (the serial algorithm's loop body, unchanged).
    auto attempt=[&](const Mesh& m,uint32_t stream){
        Attempt out;
        Vec3 origin{};for(const auto& v:m.vertices)origin=origin+v.position;origin=origin/double(m.vertices.size());
        auto n=Vec3{advance(stream)-.5,advance(stream)-.5,advance(stream)-.5}.unit();if(n.length()<.5)n={1,0,0};
        auto u=n.cross(std::abs(n.y)<.8?Vec3{0,1,0}:Vec3{0,0,1}).unit(),v=n.cross(u);
        auto local=[&](Vec3 p){return Vec3{p.dot(n),p.dot(u),p.dot(v)};};
        auto world=[&](Vec3 p){return n*p.x+u*p.y+v*p.z;};
        Mesh aligned=m;double lo=INFINITY,hi=-INFINITY;
        for(auto& vert:aligned.vertices){vert.position=local(vert.position-origin);vert.normal=local(vert.normal);lo=std::min(lo,vert.position.x);hi=std::max(hi,vert.position.x);}
        double plane=lo+(hi-lo)*(.2+.6*advance(stream));
        float interior=m.vertices.front().kind>9.5?10.25f:6.45f;std::string capFailure;
        auto a=fracture_concave_half(aligned,0,plane,true,interior,true,&capFailure),b=fracture_concave_half(aligned,0,plane,false,interior,true,&capFailure);
        
        if(!a||!b||a->indices.empty()||b->indices.empty())return out;
        if(!orient_fracture_mesh(*a)||!orient_fracture_mesh(*b)){return out;}
        
        roughen_rock_cut(aligned,*a,*b,plane,interior,stream);
        
        if(!orient_fracture_mesh(*a)||!orient_fracture_mesh(*b)){return out;}
        
        for(auto* half:{&*a,&*b})for(auto& vert:half->vertices){vert.position=world(vert.position)+origin;vert.normal=world(vert.normal).unit();}
        // Validate in the actual coordinate space used by physics,
        // too: a vanishingly narrow sliver can weld differently
        // after rotation. Retry without sacrificing its volume.
        
        if(!orient_fracture_mesh(*a)||!orient_fracture_mesh(*b)||!closed_rock_body_mesh(*a)||!closed_rock_body_mesh(*b)){return out;}
        
        out.ok=true;out.a=std::move(*a);out.b=std::move(*b);return out;
    };
    std::vector<Mesh> all;
    for(auto& [id,component]:components){
        std::vector<Mesh> pieces;pieces.push_back(std::move(component));
        const int rounds=components.size()==1?6:4;
        for(int round=0;round<rounds;++round){
            const size_t count=pieces.size();std::vector<Mesh> next;next.reserve(count*2);
            // Stream position at the start of attempt slot i (every attempt consumes four draws).
            std::vector<uint32_t> streams{seed};
            auto streamAt=[&](size_t slotIndex){while(streams.size()<=slotIndex){uint32_t s=streams.back();for(int d=0;d<4;++d)advance(s);streams.push_back(s);}return streams[slotIndex];};
            std::vector<std::map<int,Attempt>> cache(count);
            size_t piece=0;int slot=0;double acceptance=.55;int tried=0,accepted=0;
            const size_t lanes=size_t(std::max(0,rock_fracture_helper_slots().load()))+1;
            for(;;){
                if(stop.stop_requested())return fail("Stopped");
                // Resolve solids strictly in order from the cached attempts; stop at the first attempt that is not computed yet.
                int attemptSlot=-1;
                while(piece<count){
                    bool resolved=false;attemptSlot=-1;
                    for(int a=0;a<24;++a){
                        auto found=cache[piece].find(slot+a);
                        if(found==cache[piece].end()){attemptSlot=slot+a;break;}
                        if(found->second.ok){next.push_back(std::move(found->second.a));next.push_back(std::move(found->second.b));slot+=a+1;resolved=true;break;}
                    }
                    if(!resolved&&attemptSlot<0){
                        // Stop subdivision of this particular closed chunk when no
                        // safe split exists. Keep the actual rock instead of dropping
                        // it or replacing the entire breakup with synthetic shells.
                        next.push_back(std::move(pieces[piece]));slot+=24;resolved=true;
                    }
                    if(!resolved)break;
                    cache[piece].clear();++piece;
                }
                if(piece>=count)break;
                // Choose the attempts most likely to be consumed next: the probability that solid piece+j will try slot
                // attemptSlot+x, given the observed acceptance rate (a rejected attempt is followed by the next slot).
                struct Job{size_t piece;int slot;double probability;Attempt result;};
                std::vector<Job> jobs;jobs.push_back({piece,attemptSlot,1.,{}});
                if(lanes>1){
                    const int horizon=std::min<int>(48,int(lanes)*3);
                    const double reject=1.-acceptance;
                    std::vector<double> start(horizon+1,0.),nextStart(horizon+1,0.);start[0]=1.;
                    for(size_t j=0;j<std::min<size_t>(count-piece,size_t(horizon));++j){
                        for(int x=0;x<=horizon&&x<24;++x){
                            if(j==0&&x==0)continue;
                            double need=0;for(int y=0;y<=x;++y)need+=start[y]*std::pow(reject,x-y);
                            if(need<.04)continue;
                            const int s=attemptSlot+x;
                            if(cache[piece+j].count(s))continue;
                            jobs.push_back({piece+j,s,need,{}});
                        }
                        std::fill(nextStart.begin(),nextStart.end(),0.);
                        for(int x=0;x<=horizon;++x){double mass=0;for(int y=0;y<x;++y)mass+=start[y]*std::pow(reject,x-1-y);nextStart[x]=mass*acceptance;}
                        start.swap(nextStart);
                    }
                    std::stable_sort(jobs.begin()+1,jobs.end(),[](const Job& a,const Job& b){return a.probability>b.probability;});
                    if(jobs.size()>lanes)jobs.resize(lanes);
                }
                for(auto& job:jobs)streamAt(size_t(job.slot));
                rock_fracture_parallel_for(jobs.size(),lanes,[&](size_t i){
                    if(stop.stop_requested())return;
                    jobs[i].result=attempt(pieces[jobs[i].piece],streams[size_t(jobs[i].slot)]);
                });
                for(auto& job:jobs){++tried;if(job.result.ok)++accepted;cache[job.piece][job.slot]=std::move(job.result);}
                acceptance=std::clamp(double(accepted+1)/double(tried+2),.12,.85);
            }
            seed=streamAt(size_t(slot));
            pieces=std::move(next);
        }
        for(auto& m:pieces)all.push_back(std::move(m));
    }
    std::vector<FracturePiece> result;
    for(auto& m:all){
        // Also validates cap winding and closed topology before physics sees it.
        if(!orient_fracture_mesh(m))return fail("Final chunk is not closed at index "+std::to_string(result.size()));
        Vec3 lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
        for(const auto& v:m.vertices){auto p=v.position;lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
        auto center=(lo+hi)*.5;double radius=0;for(auto& v:m.vertices){v.position=v.position-center;radius=std::max(radius,v.position.length());}
        auto part=std::make_shared<MeshPart>();part->mesh=std::move(m);part->color=tint;part->emissive=emissive;result.push_back({part,center,radius});
    }return result;
}
}
