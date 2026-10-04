#pragma once
#include "mesh.hpp"
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
namespace starlight::editor {
// Persistent worker pool for the liquid surface stages (replaces a std::async launch per stage per call).
// One submitting thread at a time; a nested or concurrent submission simply runs serially on the caller.
// Workers spin briefly after a job (consecutive stages of one geometry() call arrive within microseconds) and
// then sleep on an address wait, so an idle engine costs nothing. The pool lives for the whole process and is
// intentionally never destroyed: it must stay in the executable (not a DLL that unloads) like the code using it.
// Logical processors this process may run on (affinity-aware); defined in flow.cpp.
unsigned liquid_available_processors();
class LiquidWorkers {
    struct Job { void(*call)(void*,size_t,size_t,unsigned)=nullptr; void* context=nullptr; size_t count=0,grain=1; };
    std::vector<std::thread> threads_;
    Job job_;
    uint32_t counter_=0;
    std::atomic<size_t> next_{0};
    std::atomic<int> pending_{0};
    std::atomic<uint32_t> generation_{0};
    std::atomic<int> sleepers_{0};
    std::atomic<bool> busy_{false},stop_{false};
    std::mutex errorMutex_;
    std::exception_ptr error_;
    long long spinNs_=300000;
    static void relax(){
#if defined(_MSC_VER)
        _mm_pause();
#else
        std::this_thread::yield();
#endif
    }
    void drain(unsigned worker){
        try{
            for(;;){
                size_t begin=next_.fetch_add(job_.grain,std::memory_order_relaxed);
                if(begin>=job_.count)break;
                job_.call(job_.context,begin,std::min(job_.count,begin+job_.grain),worker);
            }
        }catch(...){
            std::lock_guard<std::mutex> lock(errorMutex_);
            if(!error_)error_=std::current_exception();
            next_.store(job_.count,std::memory_order_relaxed);
        }
    }
    void loop(unsigned worker){
        // generation word = (job counter << 8) | participating threads, so a late or lagging worker always sees a
        // consistent pair and never counts itself into a job that does not include it.
        uint32_t seen=0;
        for(;;){
            uint32_t now=generation_.load(std::memory_order_acquire);
            if(now==seen){
                auto start=std::chrono::steady_clock::now();
                unsigned spins=0;
                for(;;){
                    for(int i=0;i<32;++i)relax();
                    now=generation_.load(std::memory_order_acquire);
                    if(now==seen&&(spins&15)==15)std::this_thread::yield();
                    if(now!=seen||stop_.load(std::memory_order_relaxed))break;
                    if(++spins%8==0&&std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count()>spinNs_){
                        sleepers_.fetch_add(1);
                        if(generation_.load()==seen&&!stop_.load())generation_.wait(seen);
                        sleepers_.fetch_sub(1);
                        now=generation_.load(std::memory_order_acquire);
                        break;
                    }
                }
            }
            if(stop_.load(std::memory_order_relaxed))return;
            if(now==seen)continue;
            seen=now;
            if(worker<(now&255u)){
                drain(worker);
                pending_.fetch_sub(1,std::memory_order_acq_rel);
            }
        }
    }
    LiquidWorkers(){
        // Half of the processors the process may use, at most twelve (eight helped lava 3.06 ms per surface, twelve 2.65 ms): the pool never competes with the render thread, the
        // GPU driver and the audio/physics threads for every core, which is what turns a spin-waiting pool into a slowdown.
        unsigned hardware=std::max(1u,liquid_available_processors());
        unsigned total=std::min(12u,std::max(1u,hardware/2));
        if(const char* value=std::getenv("STARLIGHT_FLOW_THREADS")){int v=std::atoi(value);if(v>=1&&v<=64)total=unsigned(v);}
        if(const char* value=std::getenv("STARLIGHT_FLOW_SPIN_US")){int v=std::atoi(value);if(v>=0&&v<=100000)spinNs_=(long long)v*1000;}
        for(unsigned worker=1;worker<total;++worker)threads_.emplace_back([this,worker]{loop(worker);});
    }
  public:
    static LiquidWorkers& get(){static LiquidWorkers* pool=new LiquidWorkers;return *pool;}
    unsigned size()const{return unsigned(threads_.size())+1;}
    // Optional: stop and join the helper threads (call only while no stage is running, e.g. before unloading a DLL that
    // contains this code). Afterwards every stage simply runs on the calling thread.
    void shutdown(){
        if(threads_.empty())return;
        stop_.store(true);
        generation_.store(((++counter_)<<8),std::memory_order_seq_cst);
        generation_.notify_all();
        for(auto& thread:threads_)thread.join();
        threads_.clear();
    }
    // Runs function(begin,end,worker) over [0,count) in chunks of `grain` using at most maxThreads threads
    // (including the calling thread). Chunks are independent; results must be written by index.
    template<class Function> void run(size_t count,size_t grain,unsigned maxThreads,Function&& function){
        if(count==0)return;
        if(!begin(count,grain,maxThreads,function)){function(size_t(0),count,0u);return;}
        finish();
    }
    // Asynchronous form: the helper threads start at once and the calling thread is free for other (serial) work until it
    // calls wait(started), which lends it to the remaining chunks and returns when everything is done. `function` must
    // outlive wait(). Returns false when nothing could run in parallel (one thread, tiny count, pool busy): the work has
    // then already been done inline and wait() must not be called.
    template<class Function> bool submit(size_t count,size_t grain,unsigned maxThreads,Function& function){
        if(count==0)return false;
        if(begin(count,grain,maxThreads,function))return true;
        function(size_t(0),count,0u);
        return false;
    }
    void wait(){finish();}
  private:
    template<class Function> bool begin(size_t count,size_t grain,unsigned maxThreads,Function& function){
        if(grain==0)grain=1;
        unsigned want=unsigned(std::min<size_t>({size_t(std::max(1u,maxThreads)),size_t(size()),(count+grain-1)/grain}));
        bool expected=false;
        if(want<=1||!busy_.compare_exchange_strong(expected,true))return false;
        job_.call=[](void* context,size_t first,size_t last,unsigned worker){(*static_cast<std::remove_reference_t<Function>*>(context))(first,last,worker);};
        job_.context=const_cast<void*>(static_cast<const void*>(&function));
        job_.count=count;job_.grain=grain;error_=nullptr;
        next_.store(0,std::memory_order_relaxed);
        pending_.store(int(want)-1,std::memory_order_relaxed);
        generation_.store(((++counter_)<<8)|want,std::memory_order_seq_cst);
        if(sleepers_.load()>0)generation_.notify_all();
        return true;
    }
    void finish(){
        drain(0);
        unsigned spins=0;
        while(pending_.load(std::memory_order_acquire)>0){relax();if(++spins>256){std::this_thread::yield();spins=0;}}
        std::exception_ptr error=std::move(error_);error_=nullptr;
        busy_.store(false,std::memory_order_release);
        if(error)std::rethrow_exception(error);
    }
};
// Convenience wrapper: function(begin,end) or function(begin,end,worker).
template<class Function> inline void liquid_parallel_for(size_t count,size_t grain,unsigned maxThreads,Function&& function){
    LiquidWorkers::get().run(count,grain,maxThreads,[&](size_t begin,size_t end,unsigned worker){
        if constexpr(std::is_invocable_v<Function&,size_t,size_t,unsigned>)function(begin,end,worker);else function(begin,end);
    });
}
struct LiquidSurfacePoint {Vec3 position;double coverage=0;};
struct LiquidMeshTimes {double grid=0,refine=0,clip=0,clipTopology=0,clipCrossings=0,clipClearance=0;bool referenceRefinement=false;};
struct LiquidSurfaceNoSolid {double operator()(const Vec3&)const{return INFINITY;}};
// Reference (generic) algorithm. TerrainFlow uses a specialised exact implementation for its own fields and runs
// this one when STARLIGHT_FLOW_VERIFY=1 so every call can be compared bit for bit.
// Refine only boundary triangles and their shared neighbours. Splitting every
// incident edge in the same pass keeps the surface conforming during motion.
template<class Sample,class Clearance=LiquidSurfaceNoSolid> Mesh liquid_surface_mesh(int resolution,Sample sample,unsigned workers=1,unsigned boundaryRefinement=2,Clearance clearance={},LiquidMeshTimes* times=nullptr){
    constexpr double wet=.002;
    auto meshLap=std::chrono::steady_clock::now();
    auto lapMs=[&]{auto now=std::chrono::steady_clock::now();double ms=std::chrono::duration<double,std::milli>(now-meshLap).count();meshLap=now;return ms;};
    const bool reference=times&&times->referenceRefinement; // original hash-container refinement, kept for verification
    // Two passes are the exact legacy/default contract. Higher visual quality
    // resolves the actual sampled bank with at most three additional passes;
    // it never subdivides the full wet interior or changes the fluid field.
    boundaryRefinement=std::clamp(boundaryRefinement,2u,5u);
    const bool detailedBoundary=boundaryRefinement>2;
    const double spacing=24./resolution;
    std::vector<LiquidSurfacePoint> points;
    // Only callers with immutable, thread-safe fields opt into sampling on
    // bounded workers. Classification/output order stays deterministic.
    workers=std::min({std::max(1u,workers),8u,std::max(1u,liquid_available_processors())});
    auto ranges=[&](size_t count,auto task){
        unsigned jobs=std::min(workers,unsigned(std::max<size_t>(1,count/4096)));
        if(jobs<=1){for(size_t i=0;i<count;++i)task(i);return;}
        liquid_parallel_for(count,std::max<size_t>(256,count/(size_t(jobs)*4)),jobs,[&](size_t begin,size_t end){for(size_t i=begin;i<end;++i)task(i);});
    };
    points.resize((resolution+1)*(resolution+1));
    ranges(points.size(),[&](size_t i){int z=int(i)/(resolution+1),x=int(i)%(resolution+1);points[i]=sample(-12+x*spacing,-12+z*spacing);});
    if(times)times->grid+=lapMs();
    using Triangle=std::array<int,3>;
    std::vector<Triangle> triangles;triangles.reserve(resolution*resolution*2);
    for(int z=0;z<resolution;++z)for(int x=0;x<resolution;++x){int k=z*(resolution+1)+x;triangles.push_back({k,k+resolution+1,k+1});triangles.push_back({k+1,k+resolution+1,k+resolution+2});}
    auto edge=[](int a,int b){return (uint64_t(std::min(a,b))<<32)|uint32_t(std::max(a,b));};
    std::unordered_map<uint64_t,int> midpoints;
    struct TriangleHash {size_t operator()(const Triangle& t)const noexcept{
        size_t h=0;for(int id:t)h^=std::hash<int>{}(id)+size_t(0x9e3779b9)+(h<<6)+(h>>2);return h;
    }};
    // Unchanged triangles have identical immutable classification probes.
    std::unordered_map<Triangle,uint8_t,TriangleHash> dryBoundaryCache;
    if(detailedBoundary)dryBoundaryCache.reserve(triangles.size());
    std::vector<uint8_t> known(triangles.size(),0); // triangles already classified as dry boundary-free (carried instead of hashed)
    for(unsigned pass=0;pass<boundaryRefinement;++pass){
        std::unordered_set<uint64_t> split;
        std::vector<uint8_t> boundaryFlags(triangles.size());
        const auto& priorClassifications=dryBoundaryCache;
        ranges(triangles.size(),[&](size_t i){const auto& t=triangles[i];
            if(detailedBoundary&&(reference?priorClassifications.contains(t):known[i]!=0)){boundaryFlags[i]=0;return;}
            bool a=points[t[0]].coverage>wet,b=points[t[1]].coverage>wet,c=points[t[2]].coverage>wet;
            bool boundary=a!=b||b!=c;
            if(!boundary){auto center=(points[t[0]].position+points[t[1]].position+points[t[2]].position)/3.;boundary=(sample(center.x,center.z).coverage>wet)!=a;}
            // A small bay or dry bank can cross an edge while its endpoints
            // and triangle centre agree. Extra-quality probes discover that
            // real contour instead of smoothing positions into solid terrain.
            if(detailedBoundary&&!boundary)for(int j=0;j<3&&!boundary;++j){auto midpoint=(points[t[j]].position+points[t[(j+1)%3]].position)*.5;boundary=(sample(midpoint.x,midpoint.z).coverage>wet)!=a;}
            // A planar chord between clear surface samples can cut through a
            // boulder when displaced cell heights curve steeply around it.
            if(detailedBoundary&&!boundary&&a){
                auto p=points[t[0]].position,q=points[t[1]].position,r=points[t[2]].position;
                boundary=clearance((p+q+r)/3.)<=wet;
                for(int j=0;j<3&&!boundary;++j)boundary=clearance((points[t[j]].position+points[t[(j+1)%3]].position)*.5)<=wet;
            }
            boundaryFlags[i]=boundary;
        });
        if(detailedBoundary)for(size_t i=0;i<triangles.size();++i)if(!boundaryFlags[i]){if(reference)dryBoundaryCache.emplace(triangles[i],0);else known[i]=1;}
        for(size_t i=0;i<triangles.size();++i)if(boundaryFlags[i]){const auto& t=triangles[i];for(int j=0;j<3;++j)split.insert(edge(t[j],t[(j+1)%3]));}
        if(split.empty())break;
        std::vector<uint8_t> splitVertex(points.size(),0); // an edge can only be marked if both ends are on a marked edge
        for(auto key:split){splitVertex[size_t(key>>32)]=1;splitVertex[size_t(uint32_t(key))]=1;}
        // Finish all midpoint samples before reading triangle vertices again:
        // vector growth must not invalidate a borrowed sample reference.
        for(auto key:split)if(!midpoints.contains(key)){
            int a=int(key>>32),b=int(uint32_t(key));auto p=(points[a].position+points[b].position)*.5;
            midpoints.emplace(key,int(points.size()));points.push_back(sample(p.x,p.z));
        }
        std::vector<Triangle> next;next.reserve(triangles.size()+split.size()*2);std::vector<uint8_t> nextKnown;nextKnown.reserve(triangles.size()+split.size()*2);
                for(size_t triangleIndex=0;triangleIndex<triangles.size();++triangleIndex){auto t=triangles[triangleIndex];nextKnown.resize(next.size(),0);
            bool marked[3];int count=0;for(int j=0;j<3;++j){marked[j]=(reference||(splitVertex[t[j]]&&splitVertex[t[(j+1)%3]]))&&split.contains(edge(t[j],t[(j+1)%3]));count+=marked[j];}
            if(!count){next.push_back(t);nextKnown.push_back(known[triangleIndex]);continue;}
            if(count==1){int j=0;while(!marked[j])++j;int a=t[j],b=t[(j+1)%3],c=t[(j+2)%3],m=midpoints.at(edge(a,b));next.push_back({a,m,c});next.push_back({m,b,c});}
            else if(count==2){int j=0;while(!(marked[j]&&marked[(j+1)%3]))++j;int a=t[j],b=t[(j+1)%3],c=t[(j+2)%3],ab=midpoints.at(edge(a,b)),bc=midpoints.at(edge(b,c));next.push_back({b,bc,ab});next.push_back({a,ab,c});next.push_back({ab,bc,c});}
            else{int a=t[0],b=t[1],c=t[2],ab=midpoints.at(edge(a,b)),bc=midpoints.at(edge(b,c)),ca=midpoints.at(edge(c,a));next.push_back({a,ab,ca});next.push_back({ab,b,bc});next.push_back({ca,bc,c});next.push_back({ab,bc,ca});}
        }
                nextKnown.resize(next.size(),0);known=std::move(nextKnown);triangles=std::move(next);
    }
    if(times)times->refine+=lapMs();
    if(workers>1){
        // Preserve the serial first-seen IDs and first directed edge orientation.
        // Only immutable, thread-safe callback callers enable this path.
        Mesh mesh;mesh.vertices.reserve(points.size());mesh.indices.reserve(triangles.size()*3);
        std::vector<int> vertices(points.size(),-1);
        std::unordered_map<uint64_t,int> crossings;
        struct CrossingWork {int a,b,id;};
        std::vector<CrossingWork> crossingWork;
        std::vector<Triangle> clippedTriangles;clippedTriangles.reserve(triangles.size());
        auto vertex=[&](int point){auto& id=vertices[point];if(id<0){id=int(mesh.vertices.size());mesh.vertices.push_back({points[point].position});}return id;};
        auto crossing=[&](int a,int b){
            auto key=edge(a,b);auto [it,inserted]=crossings.emplace(key,int(mesh.vertices.size()));
            if(inserted){crossingWork.push_back({a,b,int(mesh.vertices.size())});mesh.vertices.push_back({});}
            return it->second;
        };
        for(const auto& t:triangles){
            int clipped[6],count=0;
            for(int j=0;j<3;++j){int a=t[j],b=t[(j+1)%3];bool inside=points[a].coverage>wet,next=points[b].coverage>wet;if(inside)clipped[count++]=vertex(a);if(inside!=next)clipped[count++]=crossing(a,b);}
            for(int j=1;j+1<count;++j)clippedTriangles.push_back({clipped[0],clipped[j],clipped[j+1]});
        }
        auto clipRanges=[&](size_t count,size_t grain,auto task){
            unsigned jobs=std::min(workers,unsigned(std::max<size_t>(1,count/grain)));
            if(jobs<=1){for(size_t i=0;i<count;++i)task(i);return;}
            liquid_parallel_for(count,std::max<size_t>(64,count/(size_t(jobs)*4)),jobs,[&](size_t begin,size_t end){for(size_t i=begin;i<end;++i)task(i);});
        };
        if(times)times->clipTopology+=lapMs();
        clipRanges(crossingWork.size(),256,[&](size_t work){
            const auto& crossing=crossingWork[work];int a=crossing.a,b=crossing.b;
            auto origin=points[a].position,delta=points[b].position-origin;
            double lo=0,hi=1;bool inside=points[a].coverage>wet;
            for(int i=0;i<14;++i){double t=(lo+hi)*.5;auto p=origin+delta*t;if((sample(p.x,p.z).coverage>wet)==inside)lo=t;else hi=t;}
            auto p=origin+delta*(inside?lo:hi);mesh.vertices[crossing.id]={sample(p.x,p.z).position};
        });
        if(times)times->clipCrossings+=lapMs();
        std::vector<uint8_t> clearFlags(clippedTriangles.size());
        clipRanges(clippedTriangles.size(),4096,[&](size_t triangle){
            const auto& clipped=clippedTriangles[triangle];
            auto p=mesh.vertices[clipped[0]].position,q=mesh.vertices[clipped[1]].position,r=mesh.vertices[clipped[2]].position;
            bool clear=clearance((p+q+r)/3.)>=-1e-6;
            for(int edge=0;edge<3&&clear;++edge){auto a=edge==0?p:edge==1?q:r,b=edge==0?q:edge==1?r:p;
                for(int probe=1;probe<4&&clear;++probe)clear=clearance(a+(b-a)*(probe*.25))>=-1e-6;}
            clearFlags[triangle]=clear;
        });
        if(times)times->clipClearance+=lapMs();
        for(size_t i=0;i<clippedTriangles.size();++i)if(clearFlags[i]){const auto& t=clippedTriangles[i];mesh.indices.insert(mesh.indices.end(),{t[0],t[1],t[2]});}
        if(times)times->clip+=lapMs();
        return mesh;
    }
    Mesh mesh;mesh.vertices.reserve(points.size());mesh.indices.reserve(triangles.size()*3);
    std::vector<int> vertices(points.size(),-1);
    std::unordered_map<uint64_t,int> crossings;
    auto vertex=[&](int point){auto& id=vertices[point];if(id<0){id=int(mesh.vertices.size());mesh.vertices.push_back({points[point].position});}return id;};
    auto crossing=[&](int a,int b){
        auto key=edge(a,b);auto [it,inserted]=crossings.emplace(key,int(mesh.vertices.size()));
        if(inserted){
            // The terrain and fluid surfaces are curved. Linear interpolation
            // of their endpoint gaps places the boundary inside solid banks.
            auto origin=points[a].position,delta=points[b].position-origin;
            double lo=0,hi=1;bool inside=points[a].coverage>wet;
            for(int i=0;i<14;++i){double t=(lo+hi)*.5;auto p=origin+delta*t;if((sample(p.x,p.z).coverage>wet)==inside)lo=t;else hi=t;}
            // A carved overhang can switch the highest exposed bed abruptly.
            // Keep the proven wet endpoint; the midpoint may be inside solid.
            auto p=origin+delta*(inside?lo:hi);mesh.vertices.push_back({sample(p.x,p.z).position});
        }
        return it->second;
    };
    for(const auto& t:triangles){
        int clipped[6],count=0;
        for(int j=0;j<3;++j){int a=t[j],b=t[(j+1)%3];bool inside=points[a].coverage>wet,next=points[b].coverage>wet;if(inside)clipped[count++]=vertex(a);if(inside!=next)clipped[count++]=crossing(a,b);}
        for(int j=1;j+1<count;++j){
            auto p=mesh.vertices[clipped[0]].position,q=mesh.vertices[clipped[j]].position,r=mesh.vertices[clipped[j+1]].position;
            bool clear=clearance((p+q+r)/3.)>=-1e-6;
            // At the bounded refinement limit discard intersecting chords;
            // do not draw a blue triangle over an opaque solid face.
            for(int edge=0;edge<3&&clear;++edge){auto a=edge==0?p:edge==1?q:r,b=edge==0?q:edge==1?r:p;
                for(int probe=1;probe<4&&clear;++probe)clear=clearance(a+(b-a)*(probe*.25))>=-1e-6;}
            if(clear)mesh.indices.insert(mesh.indices.end(),{clipped[0],clipped[j],clipped[j+1]});
        }
    }
    if(times)times->clip+=lapMs();
    return mesh;
}
}
