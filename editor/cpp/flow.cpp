#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "flow.hpp"
#include "event_log.h"
#include "interactions.hpp"
#include "lava_surface.hpp"
#include "liquid_surface_mesh.hpp"
#include "object_cache.hpp"
#include <map>
#include <numeric>
#include <tuple>
#include <unordered_map>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <exception>
#include <bit>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <immintrin.h>
#include <memory>
#include <new>
namespace starlight::editor {
unsigned liquid_available_processors(){
    DWORD_PTR process=0,system=0;
    unsigned count=std::max(1u,std::thread::hardware_concurrency());
    if(GetProcessAffinityMask(GetCurrentProcess(),&process,&system)&&process){unsigned allowed=unsigned(std::popcount(uint64_t(process)));if(allowed)count=std::min(count,allowed);}
    return count;
}
// Same results as std::min({a,b,c}) / std::max({a,b,c}) (first extreme element wins) without the out-of-line vectorised
// min_element/max_element calls the initializer-list overloads compile to.
template<class T> inline T min3(const T& a,const T& b,const T& c){const T& m=(b<a)?b:a;return (c<m)?c:m;}
template<class T> inline T max3(const T& a,const T& b,const T& c){const T& m=(a<b)?b:a;return (m<c)?c:m;}
namespace {
std::atomic<double> geometryClipTopologyMs{0},geometryClipCrossingsMs{0},geometryClipClearanceMs{0};
std::atomic<double> geometrySetupMs{0},geometryGridMs{0},geometryRefineMs{0},geometryClipMs{0},geometryVertexMs{0},geometryObstacleMs{0},geometryNormalsMs{0};
std::atomic<uint64_t> geometryCalls{0},geometryVertices{0},geometryTriangles{0};
using GeometryClock=std::chrono::steady_clock;
double geometryMs(GeometryClock::time_point a,GeometryClock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
}
static std::atomic<uint64_t>& nonfinite_parts_dropped(){static std::atomic<uint64_t> count{0};return count;}
uint64_t flow_nonfinite_parts_dropped(){return nonfinite_parts_dropped().load(std::memory_order_relaxed);}
FlowGeometryProfile flow_geometry_profile(){return {geometrySetupMs.load(),geometryGridMs.load(),geometryRefineMs.load(),geometryClipMs.load(),geometryVertexMs.load(),geometryObstacleMs.load(),geometryNormalsMs.load(),geometryCalls.load(),geometryVertices.load(),geometryTriangles.load(),geometryClipTopologyMs.load(),geometryClipCrossingsMs.load(),geometryClipClearanceMs.load()};}
namespace {
// Obstacle columns depend only on (x,z) and the finished mask, so their intervals are cached across frames in one
// process-wide table keyed by the exact bits of x and z plus the mask revision (stale or foreign entries just miss).
// Entries are protected by a per-slot sequence counter: a reader that races a writer simply treats the lookup as a
// miss and recomputes, so a cached column is always exactly what the column search would have produced.
struct SiteSlot {
    std::atomic<uint64_t> sequence;
    std::atomic<uint64_t> x,z,revision;
    std::atomic<uint64_t> count;
    std::atomic<uint64_t> bounds[10]; // lo0,hi0,lo1,hi1,... (bit patterns of doubles)
};
constexpr size_t kSiteSlots=size_t(1)<<17;
constexpr uint64_t kSiteIntervals=5;
SiteSlot* site_table(){
    static SiteSlot* table=[]{void* memory=std::calloc(kSiteSlots,sizeof(SiteSlot));if(!memory)throw std::bad_alloc();return static_cast<SiteSlot*>(memory);}();
    return table;
}
inline size_t site_index(uint64_t x,uint64_t z,uint64_t revision){
    uint64_t h=x*0x9e3779b97f4a7c15ull^std::rotl(z*0xc2b2ae3d27d4eb4full,29)^revision*0x165667b19e3779f9ull;
    h^=h>>32;h*=0xd6e8feb86659fd93ull;h^=h>>32;
    return size_t(h)&(kSiteSlots-1);
}
inline const SiteSlot* site_slot(uint64_t x,uint64_t z,uint64_t revision){return &site_table()[site_index(x,z,revision)];}
bool site_lookup(uint64_t x,uint64_t z,uint64_t revision,double* bounds,uint64_t& count){
    SiteSlot& slot=site_table()[site_index(x,z,revision)];
    uint64_t first=slot.sequence.load(std::memory_order_acquire);
    if(first&1)return false;
    if(slot.x.load(std::memory_order_relaxed)!=x||slot.z.load(std::memory_order_relaxed)!=z||slot.revision.load(std::memory_order_relaxed)!=revision)return false;
    count=slot.count.load(std::memory_order_relaxed);
    if(count>kSiteIntervals)return false;
    for(uint64_t i=0;i<count*2;++i)bounds[i]=std::bit_cast<double>(slot.bounds[i].load(std::memory_order_relaxed));
    std::atomic_thread_fence(std::memory_order_acquire);
    return slot.sequence.load(std::memory_order_relaxed)==first;
}
void site_store(uint64_t x,uint64_t z,uint64_t revision,const double* bounds,uint64_t count){
    SiteSlot& slot=site_table()[site_index(x,z,revision)];
    uint64_t sequence=slot.sequence.load(std::memory_order_relaxed);
    if(sequence&1||!slot.sequence.compare_exchange_strong(sequence,sequence+1,std::memory_order_acq_rel))return;
    slot.x.store(x,std::memory_order_relaxed);slot.z.store(z,std::memory_order_relaxed);slot.revision.store(revision,std::memory_order_relaxed);
    slot.count.store(count,std::memory_order_relaxed);
    for(uint64_t i=0;i<count*2;++i)slot.bounds[i].store(std::bit_cast<uint64_t>(bounds[i]),std::memory_order_relaxed);
    slot.sequence.store(sequence+2,std::memory_order_release);
}
}
// Calls visit(cell index) for every geometry cell whose rectangle meets the triangle's x/z projection grown by the padding the block
// masks use (a point the barycentric test accepts, or a contact the separating-axis test reports, always lies inside that), instead of
// every cell of its bounding box: a long diagonal triangle touches a thin band of cells, not the whole square. Rows are clipped
// against the triangle, so the cells are a superset of those it overlaps. The outermost rows and columns take everything beyond the
// domain, as the clamped cell numbers of the queries do.
template<class Visit> static void visit_triangle_cells(const FlowObstacleMask::ColumnTriangle& t,Visit&& visit){
    constexpr int G=FlowObstacleMask::GeometryN;
    const double width=24./G,pad=1e-6+4e-9*std::max(t.boundsHigh.x-t.boundsLow.x,t.boundsHigh.z-t.boundsLow.z);
    auto cell=[](double v){return int(std::clamp(std::floor((v+12)*G/24),0.,double(G-1)));};
    const Vec3 corners[3]{t.a,t.b,t.c};
    for(int z=cell(t.boundsLow.z-pad),z1=cell(t.boundsHigh.z+pad);z<=z1;++z){
        const double lo=z==0?-INFINITY:-12+z*width-pad,hi=z==G-1?INFINITY:-12+(z+1)*width+pad;
        double xLow=INFINITY,xHigh=-INFINITY;
        auto take=[&](double x){xLow=std::min(xLow,x);xHigh=std::max(xHigh,x);};
        for(int i=0;i<3;++i){
            const Vec3 p=corners[i],q=corners[(i+1)%3];
            if(p.z>=lo&&p.z<=hi)take(p.x);
            if(p.z!=q.z)for(double edge:{lo,hi})if(std::isfinite(edge)&&(edge-p.z)*(edge-q.z)<=0)take(p.x+(edge-p.z)/(q.z-p.z)*(q.x-p.x));
        }
        if(xLow>xHigh)continue;
        for(int x=cell(xLow-pad),x1=cell(xHigh+pad);x<=x1;++x)visit(size_t(z)*G+x);
    }
}
void FlowObstacleMask::add(const Mesh &mesh, const Character &transform) {
    geometryRevision=0;layerRevision=0;lightStatic.reset();mobile.reset();occupancy.clear();cellEntries.clear();cellMasks.clear();cellStart.clear();
    mesh.validate();
    unsigned object=objectCount++;
    std::vector<Vec3> points;
    for (const auto &v : mesh.vertices)
        points.push_back(world_point(transform, v.position));
    for (size_t t = 0; t < mesh.indices.size(); t += 3) {
        auto a = points[mesh.indices[t]], b = points[mesh.indices[t + 1]], c = points[mesh.indices[t + 2]];
        double det = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        auto cell = [](double v) { double c = std::floor((v + 12) / .5); return c >= 47 ? 47 : c > 0 ? int(c) : 0; }; // clamped before the int conversion, so a vast object cannot overflow it
        int x0 = cell(min3(a.x, b.x, c.x)), x1 = cell(max3(a.x, b.x, c.x)),
            z0 = cell(min3(a.z, b.z, c.z)), z1 = cell(max3(a.z, b.z, c.z));
        unsigned triangle=unsigned(triangles.size());
        triangles.push_back({a,b,c,object});triangles.back().boundsLow={min3(a.x,b.x,c.x),min3(a.y,b.y,c.y),min3(a.z,b.z,c.z)};triangles.back().boundsHigh={max3(a.x,b.x,c.x),max3(a.y,b.y,c.y),max3(a.z,b.z,c.z)};
        visit_triangle_cells(triangles.back(),[&](size_t index){triangleCells[index].push_back(triangle);});
        if(std::abs(det)<1e-12)continue;
        rasterize_columns(triangles.back(),x0,x1,z0,z1);
    }
}
static uint64_t next_mask_revision();
// Samples one triangle into the column extents of the given cell range (the 48 x 48 grid, four sub-columns per cell).
void FlowObstacleMask::rasterize_columns(const ColumnTriangle& triangle,int x0,int x1,int z0,int z1) {
    const Vec3 a=triangle.a,b=triangle.b,c=triangle.c;
    double det = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
    if(std::abs(det)<1e-12)return;
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++)
            for (int sub = 0; sub < 4; sub++) {
                double px = -12 + (x + .25 + (sub % 2) * .5) * .5,
                       pz = -12 + (z + .25 + (sub / 2) * .5) * .5,
                       u = ((b.z - c.z) * (px - c.x) + (c.x - b.x) * (pz - c.z)) / det,
                       v = ((c.z - a.z) * (px - c.x) + (a.x - c.x) * (pz - c.z)) / det, w = 1 - u - v;
                if (min3(u, v, w) < -.00001)
                    continue;
                double y = u * a.y + v * b.y + w * c.y;
                int k = (z * 48 + x) * 4 + sub;
                low[k] = std::min(low[k], y);
                high[k] = std::max(high[k], y);
            }
}
void FlowObstacleMask::remove_objects(const std::vector<unsigned>& objects) {
    if(objects.empty()||layerRevision==0)return;
    std::vector<char> gone(objectCount,0);
    for(unsigned o:objects)if(o<objectCount)gone[o]=1;
    auto columnCell=[](double v){double c=std::floor((v+12)/.5);return c>=47?47:c>0?int(c):0;}; // clamped before the int conversion, as in add()
    int cx0=47,cx1=0,cz0=47,cz1=0;
    const bool compact=cellStart.size()==size_t(GeometryN)*GeometryN+1&&cellMasks.size()==cellEntries.size();
    for(size_t id=0;id<triangles.size();++id){
        auto& t=triangles[id];
        if(t.dead||t.object>=gone.size()||!gone[t.object])continue;
        visit_triangle_cells(t,[&](size_t cell){
            auto& list=triangleCells[cell];
            list.erase(std::remove(list.begin(),list.end(),unsigned(id)),list.end());
            if(compact)for(uint32_t k=cellStart[cell];k<cellStart[cell+1];++k)if(cellMasks[k].id==id)cellMasks[k].mask=0;
        });
        cx0=std::min(cx0,columnCell(t.boundsLow.x));cx1=std::max(cx1,columnCell(t.boundsHigh.x));cz0=std::min(cz0,columnCell(t.boundsLow.z));cz1=std::max(cz1,columnCell(t.boundsHigh.z));
        t.dead=true;
    }
    if(cx0<=cx1&&cz0<=cz1){
        for(int z=cz0;z<=cz1;++z)for(int x=cx0;x<=cx1;++x)for(int sub=0;sub<4;++sub){const int k=(z*48+x)*4+sub;low[k]=INFINITY;high[k]=-INFINITY;}
        for(const auto& t:triangles){
            if(t.dead)continue;
            const int x0=std::max(cx0,columnCell(t.boundsLow.x)),x1=std::min(cx1,columnCell(t.boundsHigh.x)),z0=std::max(cz0,columnCell(t.boundsLow.z)),z1=std::min(cz1,columnCell(t.boundsHigh.z));
            if(x0<=x1&&z0<=z1)rasterize_columns(t,x0,x1,z0,z1);
        }
    }
    // Once more than half of the records are dead, drop them and register the survivors again (finish() rebuilds the tables and
    // the revision), so a layer that lost many objects does not keep scanning dead records and treating their cells as occupied.
    // Object numbers stay as they were; only the record order closes up.
    size_t alive=0;for(const auto& t:triangles)if(!t.dead)++alive;
    if(alive*2<triangles.size()){
        std::vector<ColumnTriangle> kept;kept.reserve(alive);
        for(const auto& t:triangles)if(!t.dead)kept.push_back(t);
        triangles=std::move(kept);
        for(auto& list:triangleCells)std::vector<unsigned>().swap(list);
        for(unsigned id=0;id<triangles.size();++id){
            visit_triangle_cells(triangles[id],[&](size_t cell){triangleCells[cell].push_back(id);});
        }
        finish();
        return;
    }
    recompute_columns();
    layerRevision=next_mask_revision();geometryRevision=layerRevision;
}
// ---- layered queries: the immutable layer (this) plus an optional layer of objects that move (`mobile`) ----
double FlowObstacleMask::clearance(const Vec3& p) const {double r=clearance_layer(p);if(lightStatic)r=std::min(r,lightStatic->clearance_layer(p));if(mobile)r=std::min(r,mobile->clearance_layer(p));return r;}
double FlowObstacleMask::clearance_capped(const Vec3& p,double cap) const {double r=clearance_capped_layer(p,cap);if(lightStatic)r=std::min(r,lightStatic->clearance_capped_layer(p,cap));if(mobile)r=std::min(r,mobile->clearance_capped_layer(p,cap));return r;}
void FlowObstacleMask::prefetch_clearance(const Vec3& p) const {prefetch_clearance_layer(p);if(lightStatic)lightStatic->prefetch_clearance_layer(p);if(mobile)mobile->prefetch_clearance_layer(p);}
bool FlowObstacleMask::clear_triangle(Vec3 a,Vec3 b,Vec3 c) const {return clear_triangle_layer(a,b,c)&&(!lightStatic||lightStatic->clear_triangle_layer(a,b,c))&&(!mobile||mobile->clear_triangle_layer(a,b,c));}
bool FlowObstacleMask::clear_triangle_reference(Vec3 a,Vec3 b,Vec3 c) const {return clear_triangle_reference_layer(a,b,c)&&(!lightStatic||lightStatic->clear_triangle_reference_layer(a,b,c))&&(!mobile||mobile->clear_triangle_reference_layer(a,b,c));}
bool FlowObstacleMask::region_empty(double xLow,double xHigh,double zLow,double zHigh) const {return region_empty_layer(xLow,xHigh,zLow,zHigh)&&(!lightStatic||lightStatic->region_empty_layer(xLow,xHigh,zLow,zHigh))&&(!mobile||mobile->region_empty_layer(xLow,xHigh,zLow,zHigh));}
bool FlowObstacleMask::region_clear(double xLow,double xHigh,double zLow,double zHigh,double yLow,double yHigh,double cap) const {return region_clear_layer(xLow,xHigh,zLow,zHigh,yLow,yHigh,cap)&&(!lightStatic||lightStatic->region_clear_layer(xLow,xHigh,zLow,zHigh,yLow,yHigh,cap))&&(!mobile||mobile->region_clear_layer(xLow,xHigh,zLow,zHigh,yLow,yHigh,cap));}
static uint64_t next_mask_revision(){static std::atomic<uint64_t> nextRevision{1};return nextRevision.fetch_add(1,std::memory_order_relaxed);}
void FlowObstacleMask::recompute_columns() {
    for (int i = 0; i < 2304; i++) {
        double lo = INFINITY, hi = -INFINITY;
        int count = 0;
        for (int sub = 0; sub < 4; sub++) {
            int k = i * 4 + sub;
            if (high[k] - low[k] > .001) {
                count++;
                lo = std::min(lo, low[k]);
                hi = std::max(hi, high[k]);
            }
        }
        coverage[i] = count * .25;
        bottom[i] = count ? lo : 0;
        top[i] = count ? hi : 0;
    }
}
void FlowObstacleMask::set_layers(std::shared_ptr<const FlowObstacleMask> light,std::shared_ptr<const FlowObstacleMask> movers,const std::vector<double>& heavyLow,const std::vector<double>& heavyHigh) {
    low=heavyLow;high=heavyHigh;
    auto merge=[&](const std::shared_ptr<const FlowObstacleMask>& layer)->std::shared_ptr<const FlowObstacleMask> {
        if(!layer||layer->triangles.empty())return nullptr;
        for(size_t k=0;k<low.size();++k){low[k]=std::min(low[k],layer->low[k]);high[k]=std::max(high[k],layer->high[k]);}
        return layer;
    };
    lightStatic=merge(light);
    mobile=merge(movers);
    recompute_columns();
    geometryRevision=next_mask_revision(); // consumers' caches see a new composition; every layer keeps its own revision and memo
}
void FlowObstacleMask::truncate(const Mark& m) {
    geometryRevision=0;layerRevision=0;lightStatic.reset();mobile.reset();occupancy.clear();cellEntries.clear();cellMasks.clear();cellStart.clear();
    // Triangles were registered in increasing id order, so each cell's last entry is the newest triangle covering it.
    for(size_t id=triangles.size();id>m.triangleCount;){
        --id;
        visit_triangle_cells(triangles[id],[&](size_t index){auto& cell=triangleCells[index];if(!cell.empty()&&cell.back()==unsigned(id))cell.pop_back();});
    }
    triangles.resize(m.triangleCount);objectCount=m.objectCount;low=m.low;high=m.high;
}
bool FlowObstacleMask::region_empty_layer(double xLow,double xHigh,double zLow,double zHigh) const {
    if(layerRevision==0||occupancy.size()!=size_t(GeometryN+1)*(GeometryN+1))return false;
    auto cell=[](double value){return std::clamp(int((value+12)*GeometryN/24),0,GeometryN-1);};
    // One spare cell on each side covers the rounding of centroids and edge probes that lie on the box boundary.
    const int x0=std::max(0,cell(xLow)-1),x1=std::min(GeometryN-1,cell(xHigh)+1),z0=std::max(0,cell(zLow)-1),z1=std::min(GeometryN-1,cell(zHigh)+1);
    const size_t stride=GeometryN+1;
    return occupancy[size_t(z1+1)*stride+x1+1]-occupancy[size_t(z0)*stride+x1+1]-occupancy[size_t(z1+1)*stride+x0]+occupancy[size_t(z0)*stride+x0]==0;
}
bool FlowObstacleMask::region_clear_layer(double xLow,double xHigh,double zLow,double zHigh,double yLow,double yHigh,double cap) const {
    if(region_empty_layer(xLow,xHigh,zLow,zHigh))return true;
    if(layerRevision==0||cellLowY.size()!=triangleCells.size()||cellHighY.size()!=triangleCells.size())return false;
    auto cell=[](double value){return std::clamp(int((value+12)*GeometryN/24),0,GeometryN-1);};
    const int x0=std::max(0,cell(xLow)-1),x1=std::min(GeometryN-1,cell(xHigh)+1),z0=std::max(0,cell(zLow)-1),z1=std::min(GeometryN-1,cell(zHigh)+1);
    yLow-=1e-9;yHigh+=1e-9;
    for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x){
        const size_t ci=size_t(z)*GeometryN+x;
        // The same test clearance_capped applies to a probe, evaluated at both extremes of the probes' heights.
        if(!(yLow>cellHighY[ci]+cap+1e-6||yHigh<cellLowY[ci]-cap-1e-6))return false;
    }
    return true;
}
double FlowObstacleMask::clearance_layer(const Vec3& p) const {
    
    if(p.x< -12||p.x>12||p.z< -12||p.z>12)return INFINITY;
    int x=std::clamp(int((p.x+12)*GeometryN/24),0,GeometryN-1),z=std::clamp(int((p.z+12)*GeometryN/24),0,GeometryN-1);
    const auto& columnTriangles=triangleCells[z*GeometryN+x];
    if(columnTriangles.empty())return INFINITY;
    // Object columns depend only on x/z. The gap is the minimum over the intervals between paired surface crossings of
    // each object, so it is accumulated directly instead of being stored (and no per-query heap storage or shared
    // cache is needed; the old per-thread hash cache was cold on every new worker thread and thrashed otherwise).
    const uint64_t xBits=std::bit_cast<uint64_t>(p.x),zBits=std::bit_cast<uint64_t>(p.z);
    const bool cacheable=layerRevision!=0;
    double cachedBounds[2*kSiteIntervals];
    if(cacheable){
        uint64_t cachedCount=0;
        if(site_lookup(xBits,zBits,layerRevision,cachedBounds,cachedCount)){
            
            double hit=INFINITY;
            for(uint64_t i=0;i<cachedCount;++i)hit=std::min(hit,std::max(cachedBounds[2*i]-p.y,p.y-cachedBounds[2*i+1]));
            return hit;
        }
    }
    
    double gap=INFINITY;
    uint64_t intervalCount=0;
    thread_local std::vector<double> crossings;
    crossings.clear();if(crossings.capacity()<16)crossings.reserve(16);
    unsigned object=~0u;
    auto finishObject=[&]{
        if(crossings.size()>1){
            std::sort(crossings.begin(),crossings.end());
            crossings.erase(std::unique(crossings.begin(),crossings.end(),[](double a,double b){return std::abs(a-b)<1e-8;}),crossings.end());
            for(size_t i=1;i<crossings.size();i+=2)
                if(crossings[i]-crossings[i-1]>.001){
                    gap=std::min(gap,std::max(crossings[i-1]-p.y,p.y-crossings[i]));
                    if(intervalCount<kSiteIntervals){cachedBounds[2*intervalCount]=crossings[i-1];cachedBounds[2*intervalCount+1]=crossings[i];}
                    ++intervalCount;
                }
        }
        crossings.clear();
    };
    // Entries retain add() order, so each object's ray interval is contiguous.
    // Keep separate objects separate: an air gap between stacked rocks is air.
    auto accept=[&](unsigned id){
        const auto& t=triangles[id];
        if(t.object!=object){finishObject();object=t.object;}
        // Outside the triangle's x/z box by more than the barycentric tolerance means min(u,v,w) < -1e-9 below, so it would be skipped.
        {const double margin=1e-6+4e-9*std::max(t.boundsHigh.x-t.boundsLow.x,t.boundsHigh.z-t.boundsLow.z);
         if(p.x<t.boundsLow.x-margin||p.x>t.boundsHigh.x+margin||p.z<t.boundsLow.z-margin||p.z>t.boundsHigh.z+margin)return;}
        double det=(t.b.z-t.c.z)*(t.a.x-t.c.x)+(t.c.x-t.b.x)*(t.a.z-t.c.z);
        if(std::abs(det)<1e-12)return;
        double u=((t.b.z-t.c.z)*(p.x-t.c.x)+(t.c.x-t.b.x)*(p.z-t.c.z))/det;
        double v=((t.c.z-t.a.z)*(p.x-t.c.x)+(t.a.x-t.c.x)*(p.z-t.c.z))/det,w=1-u-v;
        if(min3(u,v,w)< -1e-9)return;
        crossings.push_back(u*t.a.y+v*t.b.y+w*t.c.y);
    };
    if(layerRevision&&cellStart.size()==size_t(GeometryN)*GeometryN+1&&cellMasks.size()==cellEntries.size()){
        // The block masks contain the exact padded boxes, so a registration rejected here would be rejected below too;
        // flushing the previous object lazily (at the next surviving triangle) yields the same per-object crossings.
        const size_t cellIndex=size_t(z)*GeometryN+x;
        const double cellX=-12+x*(24./GeometryN),cellZ=-12+z*(24./GeometryN);
        const uint64_t bit=uint64_t(1)<<(std::clamp(int((p.z-cellZ)*128.),0,7)*8+std::clamp(int((p.x-cellX)*128.),0,7));
        for(uint32_t k=cellStart[cellIndex],last=cellStart[cellIndex+1];k<last;++k){
            const MaskEntry& e=cellMasks[k];
            if(e.mask&bit)accept(e.id);
        }
    }else for(unsigned id:columnTriangles)accept(id);
    finishObject();
    if(cacheable&&intervalCount<=kSiteIntervals)site_store(xBits,zBits,layerRevision,cachedBounds,intervalCount);
    return gap;
}
bool FlowObstacleMask::clear_triangle_indexed(Vec3 a,Vec3 b,Vec3 c) const {
    // Surface intersection alone misses a small liquid face wholly enclosed
    // by a solid. Reject contained faces as well as boundary crossings.
    if(clearance_capped_layer(a,.05)<-1e-8||clearance_capped_layer(b,.05)<-1e-8||clearance_capped_layer(c,.05)<-1e-8||
       clearance_capped_layer((a+b+c)/3,.05)<-1e-8)return false;
    auto cell=[](double value){return std::clamp(int(std::floor((value+12)*GeometryN/24)),0,GeometryN-1);};
    Vec3 lo{min3(a.x,b.x,c.x),min3(a.y,b.y,c.y),min3(a.z,b.z,c.z)};
    Vec3 hi{max3(a.x,b.x,c.x),max3(a.y,b.y,c.y),max3(a.z,b.z,c.z)};
    Vec3 edges[3]{b-a,c-b,a-c},normal=edges[0].cross(edges[1]);
    const double normalLength=normal.length();
    // True when the registered triangle really meets this one (separating-axis test with the original tolerances).
    auto meets=[&](const ColumnTriangle& t){
        if(t.boundsHigh.x<lo.x||t.boundsLow.x>hi.x||
           t.boundsHigh.y<lo.y||t.boundsLow.y>hi.y||
           t.boundsHigh.z<lo.z||t.boundsLow.z>hi.z)return false;
        Vec3 other[3]{t.b-t.a,t.c-t.b,t.a-t.c},otherNormal=other[0].cross(other[1]);
        auto separated=[&](Vec3 axis){
            double length=axis.length();if(length<1e-12)return false;axis=axis/length;
            double lowA=min3(a.dot(axis),b.dot(axis),c.dot(axis)),highA=max3(a.dot(axis),b.dot(axis),c.dot(axis));
            double lowB=min3(t.a.dot(axis),t.b.dot(axis),t.c.dot(axis)),highB=max3(t.a.dot(axis),t.b.dot(axis),t.c.dot(axis));
            return highA<lowB-1e-8||highB<lowA-1e-8;
        };
        bool apart=separated(normal)||separated(otherNormal);
        for(auto e:edges)for(auto f:other)if(!apart)apart=separated(e.cross(f));
        // Coplanar triangles need additional in-plane axes; crossing planes
        // already have the complete separating-axis set above.
        double otherLength=otherNormal.length();
        bool coplanar=normalLength>1e-12&&otherLength>1e-12&&
            normal.cross(otherNormal).length()<=1e-9*normalLength*otherLength&&
            std::abs((t.a-a).dot(normal))<=1e-8*normalLength;
        if(coplanar){
            for(auto e:edges)if(!apart)apart=separated(normal.cross(e));
            for(auto e:other)if(!apart)apart=separated(otherNormal.cross(e));
        }
        return !apart;
    };
    const int qx0=cell(lo.x),qx1=cell(hi.x),qz0=cell(lo.z),qz1=cell(hi.z);
    const bool cellRanges=layerRevision&&cellLowY.size()==triangleCells.size()&&cellHighY.size()==triangleCells.size();
    // A triangle registered in several scanned cells would be tested once per cell; its answer cannot change, so test it once.
    thread_local std::vector<uint32_t> visited;thread_local uint32_t visitEpoch=0;
    if(visited.size()<triangles.size())visited.resize(triangles.size(),0);
    if(++visitEpoch==0){std::fill(visited.begin(),visited.end(),0u);visitEpoch=1;}
    if(layerRevision&&cellRanges&&cellStart.size()==size_t(GeometryN)*GeometryN+1){
        for(int z=qz0;z<=qz1;++z)for(int x=qx0;x<=qx1;++x){
            const size_t cellIndex=size_t(z)*GeometryN+x;
            if(cellHighY[cellIndex]<lo.y||cellLowY[cellIndex]>hi.y)continue;
            // 8x8 sub-blocks of this cell the query box touches (the registrations' masks cover their padded boxes).
            const double cellX=-12+x*(24./GeometryN),cellZ=-12+z*(24./GeometryN);
            const int bx0=std::clamp(int((lo.x-cellX)*128.),0,7),bx1=std::clamp(int((hi.x-cellX)*128.),0,7),bz0=std::clamp(int((lo.z-cellZ)*128.),0,7),bz1=std::clamp(int((hi.z-cellZ)*128.),0,7);
            const uint64_t row=(uint64_t(2)<<bx1)-(uint64_t(1)<<bx0);
            uint64_t queryMask=0;for(int bz=bz0;bz<=bz1;++bz)queryMask|=row<<(bz*8);
            for(uint32_t k=cellStart[cellIndex];k<cellStart[cellIndex+1];++k){
                if(!(cellMasks[k].mask&queryMask))continue;
                const CellEntry& e=cellEntries[k];
                if(double(e.yHigh)<lo.y||double(e.yLow)>hi.y)continue;
                const unsigned id=cellMasks[k].id;
                if(visited[id]==visitEpoch)continue;visited[id]=visitEpoch;
                if(meets(triangles[id]))return false;
            }
        }
        return true;
    }
    for(int z=qz0;z<=qz1;++z)for(int x=qx0;x<=qx1;++x){
    const size_t cellIndex=size_t(z)*GeometryN+x;const auto& cellTriangles=triangleCells[cellIndex];
    if(cellTriangles.empty())continue;
    if(cellRanges&&(cellHighY[cellIndex]<lo.y||cellLowY[cellIndex]>hi.y))continue;
    for(unsigned id:cellTriangles){
        if(visited[id]==visitEpoch)continue;visited[id]=visitEpoch;
        if(meets(triangles[id]))return false;
    }}
    return true;
}
double FlowObstacleMask::clearance_capped_layer(const Vec3& p,double cap) const {
    if(p.x< -12||p.x>12||p.z< -12||p.z>12)return INFINITY;
    const size_t cell=size_t(std::clamp(int((p.z+12)*GeometryN/24),0,GeometryN-1))*GeometryN+size_t(std::clamp(int((p.x+12)*GeometryN/24),0,GeometryN-1));
    if(triangleCells[cell].empty())return INFINITY;
    // Every crossing in this column lies inside the vertical extent of the triangles registered in the cell, so a point
    // farther than `cap` above or below that extent is at least `cap` clear of every interval.
    if(layerRevision&&cellLowY.size()==triangleCells.size()&&(p.y>cellHighY[cell]+cap+1e-6||p.y<cellLowY[cell]-cap-1e-6))return INFINITY;
    return clearance_layer(p);
}
void FlowObstacleMask::prefetch_clearance_layer(const Vec3& p) const {
    if(!layerRevision||p.x< -12||p.x>12||p.z< -12||p.z>12)return;
    const size_t cell=size_t(std::clamp(int((p.z+12)*GeometryN/24),0,GeometryN-1))*GeometryN+size_t(std::clamp(int((p.x+12)*GeometryN/24),0,GeometryN-1));
    if(triangleCells[cell].empty())return;
    _mm_prefetch(reinterpret_cast<const char*>(site_slot(std::bit_cast<uint64_t>(p.x),std::bit_cast<uint64_t>(p.z),layerRevision)),_MM_HINT_T0);
}
bool FlowObstacleMask::clear_triangle_layer(Vec3 a,Vec3 b,Vec3 c) const {
    const bool result=clear_triangle_indexed(a,b,c);
    static const bool verify=[]{const char* value=std::getenv("STARLIGHT_FLOW_VERIFY");return value&&value[0]=='1';}();
    if(verify&&result!=clear_triangle_reference_layer(a,b,c))throw std::runtime_error("Indexed obstacle search disagrees with the exhaustive reference");
    return result;
}
bool FlowObstacleMask::clear_triangle_reference_layer(Vec3 a,Vec3 b,Vec3 c) const {
    // Surface intersection alone misses a small liquid face wholly enclosed
    // by a solid. Reject contained faces as well as boundary crossings.
    if(clearance_layer(a)<-1e-8||clearance_layer(b)<-1e-8||clearance_layer(c)<-1e-8||
       clearance_layer((a+b+c)/3)<-1e-8)return false;
    auto cell=[](double value){return std::clamp(int(std::floor((value+12)*GeometryN/24)),0,GeometryN-1);};
    Vec3 lo{std::min({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::min({a.z,b.z,c.z})};
    Vec3 hi{std::max({a.x,b.x,c.x}),std::max({a.y,b.y,c.y}),std::max({a.z,b.z,c.z})};
    Vec3 edges[3]{b-a,c-b,a-c},normal=edges[0].cross(edges[1]);
    for(int z=cell(lo.z);z<=cell(hi.z);++z)for(int x=cell(lo.x);x<=cell(hi.x);++x)for(unsigned id:triangleCells[z*GeometryN+x]){
        const auto& t=triangles[id];
        if(std::max({t.a.x,t.b.x,t.c.x})<lo.x||std::min({t.a.x,t.b.x,t.c.x})>hi.x||
           std::max({t.a.y,t.b.y,t.c.y})<lo.y||std::min({t.a.y,t.b.y,t.c.y})>hi.y||
           std::max({t.a.z,t.b.z,t.c.z})<lo.z||std::min({t.a.z,t.b.z,t.c.z})>hi.z)continue;
        Vec3 other[3]{t.b-t.a,t.c-t.b,t.a-t.c},otherNormal=other[0].cross(other[1]);
        auto separated=[&](Vec3 axis){
            double length=axis.length();if(length<1e-12)return false;axis=axis/length;
            double lowA=std::min({a.dot(axis),b.dot(axis),c.dot(axis)}),highA=std::max({a.dot(axis),b.dot(axis),c.dot(axis)});
            double lowB=std::min({t.a.dot(axis),t.b.dot(axis),t.c.dot(axis)}),highB=std::max({t.a.dot(axis),t.b.dot(axis),t.c.dot(axis)});
            return highA<lowB-1e-8||highB<lowA-1e-8;
        };
        bool apart=separated(normal)||separated(otherNormal);
        for(auto e:edges)for(auto f:other)if(!apart)apart=separated(e.cross(f));
        // Coplanar triangles need additional in-plane axes; crossing planes
        // already have the complete separating-axis set above.
        double normalLength=normal.length(),otherLength=otherNormal.length();
        bool coplanar=normalLength>1e-12&&otherLength>1e-12&&
            normal.cross(otherNormal).length()<=1e-9*normalLength*otherLength&&
            std::abs((t.a-a).dot(normal))<=1e-8*normalLength;
        if(coplanar){
            for(auto e:edges)if(!apart)apart=separated(normal.cross(e));
            for(auto e:other)if(!apart)apart=separated(otherNormal.cross(e));
        }
        if(!apart)return false;
    }
    return true;
}
void FlowObstacleMask::finish() {
    recompute_columns();
    cellLowY.assign(triangleCells.size(),INFINITY);cellHighY.assign(triangleCells.size(),-INFINITY);
    {
        // Compact registrations: offsets by cell, then one entry per registration with outward-rounded float boxes.
        auto down=[](double v){float f=float(v);if(double(f)>v)f=std::nextafterf(f,-INFINITY);return f;};
        auto up=[](double v){float f=float(v);if(double(f)<v)f=std::nextafterf(f,INFINITY);return f;};
        cellStart.assign(size_t(GeometryN)*GeometryN+1,0);
        for(size_t cell=0;cell<triangleCells.size();++cell)cellStart[cell+1]=cellStart[cell]+uint32_t(triangleCells[cell].size());
        cellEntries.resize(cellStart.back());cellMasks.resize(cellStart.back());
        liquid_parallel_for(size_t(GeometryN),16,8,[&](size_t zBegin,size_t zEnd){
            for(size_t z=zBegin;z<zEnd;++z)for(size_t x=0;x<size_t(GeometryN);++x){
                const size_t cell=z*GeometryN+x;uint32_t out=cellStart[cell];
                for(unsigned id:triangleCells[cell]){
                    const auto& t=triangles[id];
                    const double margin=1e-6+4e-9*std::max(t.boundsHigh.x-t.boundsLow.x,t.boundsHigh.z-t.boundsLow.z);
                    cellLowY[cell]=std::min(cellLowY[cell],t.boundsLow.y);cellHighY[cell]=std::max(cellHighY[cell],t.boundsHigh.y);
                    {
                        // Sub-blocks (8x8 per cell) the padded box can touch, with the same monotone mapping the query uses.
                        const double cellX=-12+double(x)*(24./GeometryN),cellZ=-12+double(z)*(24./GeometryN);
                        auto block=[](double v,double origin){return std::clamp(int((v-origin)*128.),0,7);};
                        const int bx0=block(t.boundsLow.x-margin,cellX),bx1=block(t.boundsHigh.x+margin,cellX),bz0=block(t.boundsLow.z-margin,cellZ),bz1=block(t.boundsHigh.z+margin,cellZ);
                        uint64_t row=0;for(int bx=bx0;bx<=bx1;++bx)row|=uint64_t(1)<<bx;
                        uint64_t mask=0;for(int bz=bz0;bz<=bz1;++bz)mask|=row<<(bz*8);
                        cellMasks[out]={mask,id,0};
                    }
                    cellEntries[out++]={down(t.boundsLow.y),up(t.boundsHigh.y)};
                }
            }
        });
    }
    {
        const size_t stride=GeometryN+1;occupancy.assign(stride*stride,0);
        for(int z=0;z<GeometryN;++z)for(int x=0;x<GeometryN;++x)
            occupancy[size_t(z+1)*stride+x+1]=occupancy[size_t(z)*stride+x+1]+occupancy[size_t(z+1)*stride+x]-occupancy[size_t(z)*stride+x]+(triangleCells[size_t(z)*GeometryN+x].empty()?0:1);
    }
    layerRevision=next_mask_revision();geometryRevision=layerRevision;
}
bool TerrainFlow::terrain(const TerrainData& t){settle_async();
    if(terrain_.Heights==t.Heights&&terrain_.Volumes==t.Volumes){terrain_=t;return false;}
    t.validate();std::array<double,2304> next{};
    for(int z=0;z<N;++z)for(int x=0;x<N;++x)next[z*N+x]=t.surface_at(-12+(x+.5)*Cell,-12+(z+.5)*Cell);
    terrain_=t;bed_=next;++terrainVersion_;return true;
}
TerrainFlow::TerrainFlow(const TerrainData &t, const std::vector<LiquidSource> &sources, std::span<const Character> objects)
    : terrain_(t), sources_(sources) {
    terrain_.validate();
    for (const auto &s : sources_)
        remaining_.push_back(s.Volume);
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++)
            bed_[z * N + x] = t.surface_at(-12 + (x + .5) * Cell, -12 + (z + .5) * Cell);
    // Authored water is an initial volume, not an infinite stationary render plane.
    if(t.WaterEnabled)for(int i=0;i<N*N;++i)(t.Lava?lava:water)[i]=std::max(0.,t.WaterLevel-bed_[i]);
    // Lava patches initialize a finite pool over their transformed footprint.
    // Sampling their actual triangles respects rotation and nonuniform scale.
    for(const auto& c:objects)if(c.Shape=="lava"){
        for(const auto& part:shape_geometry(c))for(size_t k=0;k<part.mesh.indices.size();k+=3){
            auto a=world_point(c,part.mesh.vertices[part.mesh.indices[k]].position);
            auto b=world_point(c,part.mesh.vertices[part.mesh.indices[k+1]].position);
            auto d=world_point(c,part.mesh.vertices[part.mesh.indices[k+2]].position);
            double determinant=(b.z-d.z)*(a.x-d.x)+(d.x-b.x)*(a.z-d.z);
            if(std::abs(determinant)<1e-12)continue;
            auto cell=[](double p){double c=std::floor((p+12)/Cell);return c>=N-1?N-1:c>0?int(c):0;}; // clamped before the int conversion, so a vast patch cannot overflow it
            for(int z=cell(std::min({a.z,b.z,d.z}));z<=cell(std::max({a.z,b.z,d.z}));++z)
                for(int x=cell(std::min({a.x,b.x,d.x}));x<=cell(std::max({a.x,b.x,d.x}));++x){
                    double px=-12+(x+.5)*Cell,pz=-12+(z+.5)*Cell;
                    double u=((b.z-d.z)*(px-d.x)+(d.x-b.x)*(pz-d.z))/determinant;
                    double v=((d.z-a.z)*(px-d.x)+(a.x-d.x)*(pz-d.z))/determinant,w=1-u-v;
                    if(std::min({u,v,w})>=-1e-8){int i=z*N+x;lava[i]=std::max(lava[i],std::max(0.,u*a.y+v*b.y+w*d.y-bed_[i]));}
                }
        }
    }
}
void TerrainFlow::settle_initial_liquid(std::array<double,2304>& field){
    // Installing an authored solid displaces the initial finite volume. Spread
    // that volume across each connected pool before displaying it, rather than
    // briefly perching an entire water column above the rock's top.
    std::array<bool,2304> visited{};
    std::array<int,2304> owner;owner.fill(-1);
    std::vector<std::vector<int>> settledPools(N*N);
    auto capacity=[&](int i,double level){double bottom=std::max(bed_[i],obstacles_->bottom[i]),top=std::max(bottom,obstacles_->top[i]);return std::max(0.,level-bed_[i]-obstacles_->coverage[i]*std::max(0.,std::min(level,top)-bottom));};
    for(int seed=0;seed<N*N;++seed){
        if(visited[seed]||field[seed]<=0)continue;
        std::vector<int> pool{seed};visited[seed]=true;
        for(size_t cursor=0;cursor<pool.size();++cursor){int i=pool[cursor],x=i%N,z=i/N;
            for(auto [dx,dz]:{std::pair{-1,0},{1,0},{0,-1},{0,1}}){int xx=x+dx,zz=z+dz;if(xx<0||xx>=N||zz<0||zz>=N)continue;int j=zz*N+xx;if(!visited[j]&&field[j]>0){visited[j]=true;pool.push_back(j);}}
        }
        bool displaced=false;double total=0,low=INFINITY,high=-INFINITY;
        for(int i:pool){total+=field[i];low=std::min(low,bed_[i]);high=std::max(high,surface(i,field[i]));displaced|=obstacles_->coverage[i]>0&&obstacles_->top[i]>bed_[i];}
        if(!displaced){for(int i:pool)owner[i]=seed;settledPools[seed]=std::move(pool);continue;}
        std::array<bool,2304> included{};for(int i:pool)included[i]=true;
        double level=0;
        for(;;){
            for(int iteration=0;iteration<64;++iteration){double middle=(low+high)*.5,sum=0;for(int i:pool)sum+=capacity(i,middle);if(sum<total)low=middle;else high=middle;}
            level=(low+high)*.5;bool expanded=false;
            // Displaced water may spill onto initially dry banks or join an
            // existing pool. Treating the old wet-cell outline as a wall would
            // leave small, fully occupied pools perched on boulders forever.
            for(size_t cursor=0;cursor<pool.size();++cursor){int i=pool[cursor],x=i%N,z=i/N;
                for(auto [dx,dz]:{std::pair{-1,0},{1,0},{0,-1},{0,1}}){int xx=x+dx,zz=z+dz;if(xx<0||xx>=N||zz<0||zz>=N)continue;int j=zz*N+xx;
                    if(included[j]||capacity(j,level)<=1e-12)continue;
                    auto append=[&](int cell){if(included[cell])return;included[cell]=visited[cell]=true;pool.push_back(cell);total+=field[cell];expanded=true;};
                    if(owner[j]>=0){int previous=owner[j];for(int cell:settledPools[previous])append(cell);settledPools[previous].clear();}
                    else append(j);
                }
            }
            if(!expanded)break;
            low=INFINITY;high=level;for(int i:pool){low=std::min(low,bed_[i]);high=std::max(high,surface(i,field[i]));}
        }
        double sum=0;int largest=pool.front();
        for(int i:pool){field[i]=capacity(i,level);sum+=field[i];if(field[i]>field[largest])largest=i;}
        field[largest]+=total-sum;
        for(int i:pool)owner[i]=seed;settledPools[seed]=std::move(pool);
    }
}
void TerrainFlow::set_obstacles(FlowObstacleMask mask){settle_async();
    obstacles_=std::move(mask);
    if(initialObstacleSettlement_){settle_initial_liquid(water);settle_initial_liquid(lava);initialObstacleSettlement_=false;}
}
double TerrainFlow::surface(int i, double amount) const {
    if (!obstacles_ || obstacles_->coverage[i] <= 0)
        return bed_[i] + amount;
    double bottom = std::max(bed_[i], obstacles_->bottom[i]), top = std::max(bottom, obstacles_->top[i]),
           coverage = obstacles_->coverage[i], below = bottom - bed_[i],
           capacity = (top - bottom) * (1 - coverage);
    // At a fully occupied column the capacity has a plateau between the
    // underside and top of the object. Equality belongs to the underside;
    // otherwise conserved water jumps to the rock top without added volume.
    if (amount <= below + 1e-12)
        return bed_[i] + amount;
    if (amount < below + capacity && coverage < .999)
        return bottom + (amount - below) / (1 - coverage);
    return bed_[i] + amount + (top - bottom) * coverage;
}
LiquidSnapshot TerrainFlow::snapshot()const{
    LiquidSnapshot result;for(int z=0;z<N;++z)for(int x=0;x<N;++x){int i=z*N+x;result.cells[i]=liquid_at(-12+(x+.5)*Cell,-12+(z+.5)*Cell);result.currents[i]=water[i]>.002?waterCurrent_[i]:lava[i]>.002?lavaCurrent_[i]:Vec3{};}return result;
}
Vec3 TerrainFlow::velocity_at(double x,double z)const{
    if(liquid_at(x,z).second<=0)return {};
    auto component=[&](bool horizontal){return sample_liquid(x,z,[&](int i){double density=water[i]>.002?1000:lava[i]>.002?2800:0;auto v=density==1000?waterCurrent_[i]:lavaCurrent_[i];return std::pair{horizontal?v.x:v.z,density};}).first;};
    return {component(true),0,component(false)};
}
std::pair<double, double> TerrainFlow::liquid_at(double x, double z) const {
    return sample_liquid(x,z,[&](int i)->std::pair<double,double>{
        if(water[i]>.002)return {surface(i,water[i])+waves_[i],1000};
        if(lava[i]>.002)return {surface(i,lava[i])+waves_[i],2800};
        return {-INFINITY,0};
    });
}
double TerrainFlow::volume() const {
    return (std::accumulate(water.begin(), water.end(), 0.) + std::accumulate(lava.begin(), lava.end(), 0.)) *
           Area;
}
void TerrainFlow::lava_settings(double heat,double cooling,double crust,double light,bool reset){settle_async();
    if(!std::isfinite(heat+cooling+crust+light)||heat<0||heat>1||cooling<0||cooling>1||crust<0||crust>.3||light<0||light>8)throw std::runtime_error("Invalid lava settings");
    lavaInitialHeat_=heat;lavaCoolingRate_=cooling;lavaCrustDepth_=crust;lavaLightStrength_=light;if(reset)std::fill(lavaHeat_.begin(),lavaHeat_.end(),heat);
}
double TerrainFlow::lava_heat_at(double x,double z)const{
    auto value=sample_liquid(x,z,[&](int i){return std::pair{lavaHeat_[i],lava[i]>.002?2800.:0.};});return value.second>0?value.first:0.;
}
std::vector<std::array<float,12>> TerrainFlow::lava_lights()const{
    struct Bin {Vec3 center{};double power=0;};std::array<Bin,4> bins{};double total=0,area=0;
    for(int z=0;z<N;++z)for(int x=0;x<N;++x){int i=z*N+x;if(lava[i]<=.002)continue;area+=Area;double power=Area*std::pow(lavaHeat_[i],4);int bin=(x>=N/2?1:0)+(z>=N/2?2:0);bins[bin].power+=power;bins[bin].center=bins[bin].center+Vec3{-12+(x+.5)*Cell,surface(i,lava[i])+waves_[i]+.3,-12+(z+.5)*Cell}*power;total+=power;}
    std::vector<std::array<float,12>> lights;if(total<=1e-8||lavaLightStrength_==0)return lights;
    double scale=std::min(12.,area*.06)*lavaLightStrength_/area;
    for(const auto& bin:bins)if(bin.power>1e-8){auto p=bin.center/bin.power;double power=bin.power*scale;lights.push_back({float(p.x),float(p.y),float(p.z),8,0,-1,0,-2,float(power*1.4),float(power*.18),float(power*.005),-1});}return lights;
}
void TerrainFlow::step(double dt) {settle_async();
    if (!std::isfinite(dt) || dt <= 0 || dt > .05)
        throw std::runtime_error("Fluid step must be within (0, .05]");
    for (size_t i = 0; i < sources_.size(); i++) {
        const auto &s = sources_[i];
        double amount = std::min(remaining_[i], s.Volume / 3 * dt);
        remaining_[i] -= amount;
        int cell=index(s.X,s.Z);if(s.Lava){double added=amount/Area;lavaHeat_[cell]=(lava[cell]*lavaHeat_[cell]+added*lavaInitialHeat_)/std::max(1e-30,lava[cell]+added);lava[cell]+=added;}else water[cell]+=amount/Area;
        emitted += amount;
    }
    transport(water, dt, 3);
    transport(lava, dt, .25);
    // A normalized surface-heat proxy, not a full thermodynamic solver.
    // Shallow films lose heat faster; inflow and transport carry heat by mass.
    for(int i=0;i<N*N;++i)if(lava[i]>.002)lavaHeat_[i]*=std::exp(-lavaCoolingRate_*dt*(1+.06/std::max(.01,lava[i])));
    advance_waves(dt);
}
double TerrainFlow::wave_energy()const {double sum=0;for(size_t i=0;i<waves_.size();++i)sum+=waves_[i]*waves_[i]+waveVelocity_[i]*waveVelocity_[i];return sum;}
void TerrainFlow::impulse(double x,double z,double strength){settle_async();
    if(!std::isfinite(x)||!std::isfinite(z)||!std::isfinite(strength)||x< -12||x>=12||z< -12||z>=12)return;
    int center=index(x,z);if(water[center]+lava[center]<.002)return;
    int cx=center%N,cz=center/N;double amount=std::clamp(strength,-2.,2.);
    waveVelocity_[center]+=amount;
    std::vector<int> ring;for(auto [dx,dz]:{std::pair{-1,0},{1,0},{0,-1},{0,1}}){int xx=cx+dx,zz=cz+dz;if(xx>=0&&xx<N&&zz>=0&&zz<N){int j=zz*N+xx;if(water[j]+lava[j]>.002)ring.push_back(j);}}
    if(!ring.empty())for(int j:ring)waveVelocity_[j]-=amount/ring.size();
}
void TerrainFlow::advance_waves(double dt){
    int steps=std::max(1,int(std::ceil(dt*120)));double h=dt/steps;
    for(int step=0;step<steps;++step){
        waveTime_+=h;std::array<double,2304> forcing{};double windSpeed=std::hypot(windX_,windZ_),forceMean=0;int windCells=0;
        // Resolved metre-scale wind waves complement the shader's fine ripples.
        // Coherent crossing frequencies travel with gravity-wave dispersion;
        // subtract their wet-cell mean so wind does not pump the whole basin.
        if(windSpeed>0)for(int z=0;z<N;++z)for(int x=0;x<N;++x){int i=z*N+x;if(water[i]>.002&&water[i]>=lava[i]){double along=((x+.5)*Cell-12)*windX_/windSpeed+((z+.5)*Cell-12)*windZ_/windSpeed,cross=((x+.5)*Cell-12)*(-windZ_/windSpeed)+((z+.5)*Cell-12)*windX_/windSpeed;
            forcing[i]=.32*windWaveStrength_*windSpeed/(1+.15*windSpeed)*(.65*std::sin(.7*along-waveTime_*std::sqrt(9.81*.7))+.35*std::sin(1.25*(along*.8+cross*.6)-waveTime_*std::sqrt(9.81*1.25)));forceMean+=forcing[i];++windCells;}}
        if(windCells)forceMean/=windCells;
        std::array<double,2304> velocity=waveVelocity_;
        for(int z=0;z<N;++z)for(int x=0;x<N;++x){int i=z*N+x;double depth=water[i]+lava[i];if(depth<.002){waves_[i]=waveVelocity_[i]=0;continue;}double laplace=0;
            bool molten=lava[i]>water[i];
            for(auto [dx,dz]:{std::pair{-1,0},{1,0},{0,-1},{0,1}}){int xx=x+dx,zz=z+dz;if(xx<0||xx>=N||zz<0||zz>=N)continue;int j=zz*N+xx;double neighbor=water[j]+lava[j];if(neighbor<=.002||(lava[j]>water[j])!=molten)continue;
                // Symmetric face flux slows water in shallows and reflects at
                // dry banks. Lava retains its slower, strongly damped response.
                double c2=molten?.25:std::min(16.,9.81*(2*depth*neighbor/(depth+neighbor)));laplace+=(waves_[j]-waves_[i])*c2;}
            velocity[i]=(waveVelocity_[i]+(laplace/(Cell*Cell)+(!molten&&windCells?forcing[i]-forceMean:0))*h)*std::exp(-(molten?5.:1.2)*h);
        }
        for(size_t i=0;i<waves_.size();++i)if(water[i]+lava[i]>.002){waveVelocity_[i]=velocity[i];double limit=std::min(.3,(water[i]+lava[i])*.5);waves_[i]=std::clamp(waves_[i]+velocity[i]*h,-limit,limit);}
    }
}
void TerrainFlow::interact(std::span<const Character> objects,double dt,double time){settle_async();
    splashes_.clear();if(!std::isfinite(dt)||dt<=0||dt>.05)return;
    std::unordered_map<std::string,MovingBody> next;
    for(const auto& object:objects)if(object.Dynamic||!object.Route.empty()){
        auto box=ObjectCache::shared().bounds_at(object,route_pose_of(object,time));auto center=(box.low+box.high)*.5;
        auto [height,density]=liquid_at(center.x,center.z);auto old=movingBodies_.find(object.Id);
        if(old!=movingBodies_.end()&&density>0){auto velocity=(center-old->second.center)/dt;
            if(velocity.length()<100){bool crossing=old->second.bottom>old->second.surface&&box.low.y<=height;
                bool immersed=box.low.y<height&&box.high.y>height-.3;
                if(crossing||immersed){double strength=std::min(2.,(std::abs(velocity.y)+std::hypot(velocity.x,velocity.z)*.3)*(crossing?.2:dt));impulse(center.x,center.z,strength);
                    if(crossing&&velocity.y<-.2)splashes_.push_back({{center.x,height,center.z},velocity,std::min(2.,-velocity.y*.2),density>2000});}
            }
        }
        next[object.Id]={center,box.low.y,height};
    }movingBodies_=std::move(next);
}
void TerrainFlow::transport(std::array<double, 2304> &depth, double dt, double speed) {
    const bool molten=&depth==&lava;std::array<double,2304> heatDelta{};
    auto& currents=(&depth==&water)?waterCurrent_:lavaCurrent_;std::fill(currents.begin(),currents.end(),Vec3{});
    outflow_.fill(0);
    delta_.fill(0);
    transfers_.clear();
    auto edge = [&](int a, int b) {
        double difference = surface(a, depth[a]) - surface(b, depth[b]);
        if (std::abs(difference) < 1e-12)
            return;
        if (difference < 0) {
            std::swap(a, b);
            difference = -difference;
        }
        if (depth[a] <= 0)
            return;
        double amount = difference * speed * dt;
        transfers_.push_back({a, b, amount});
        outflow_[a] += amount;
    };
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            int i = z * N + x;
            if (x + 1 < N)
                edge(i, i + 1);
            if (z + 1 < N)
                edge(i, i + N);
        }
    for (auto t : transfers_) {
        double move = t.amount * std::min(1., .45 * depth[t.a] / std::max(1e-30, outflow_[t.a]));
        if(obstacles_&&obstacles_->coverage[t.b]>0){
            double level=surface(t.a,depth[t.a]);
            double bottom=std::max(bed_[t.b],obstacles_->bottom[t.b]);
            double top=std::max(bottom,obstacles_->top[t.b]);
            double capacity=std::max(0.,level-bed_[t.b]-obstacles_->coverage[t.b]*
                std::max(0.,std::min(level,top)-bottom));
            // A low donor cannot push water through a solid capacity plateau
            // and teleport it onto the object's roof. Keep rejected inflow in
            // its donor; account for all earlier transfers into this cell.
            move=std::min(move,std::max(0.,capacity-depth[t.b]-delta_[t.b]));
        }
        delta_[t.a] -= move;
        delta_[t.b] += move;
        if(molten){double heat=move*lavaHeat_[t.a];heatDelta[t.a]-=heat;heatDelta[t.b]+=heat;}
        // Cell-face flux converted to horizontal flow speed. Half weighting
        // averages the incident faces; the same transfer moves liquid volume.
        Vec3 direction{double(t.b%N-t.a%N),0,double(t.b/N-t.a/N)};
        double speed=std::min(10.,move*Cell/(dt*std::max(.002,(depth[t.a]+depth[t.b])*.5)));
        currents[t.a]=currents[t.a]+direction*(speed*.5);currents[t.b]=currents[t.b]+direction*(speed*.5);
    }
    for (int i = 0; i < 2304; i++){
        double next=std::max(0.,depth[i]+delta_[i]);if(molten&&next>1e-20)lavaHeat_[i]=std::clamp((depth[i]*lavaHeat_[i]+heatDelta[i])/next,0.,1.);depth[i]=next;
    }
}
// ---------------------------------------------------------------------------------------------------------------------
// Liquid surface mesh: fast exact path.
//
// geometry_unverified() reproduces the generic liquid_surface_mesh() + per-vertex pass + obstacle pass + normals of
// geometry_reference() bit for bit (positions, kind, motion identity, colour, normals, index values and order). It is
// faster because
//   * every stage runs on a persistent worker pool (at most eight threads) with index-addressed output; independent
//     work (per-point surface fields, crossing bisection) overlaps the serial parts,
//   * sub-cell certificates (a quarter-metre cell whose interpolation block is comfortably wet and above the bank, with no
//     obstacle triangle nearby, or comfortably dry) prove "coverage > wet" / "<= wet" for every point inside, so the
//     probes of the refinement passes, bisection and clip stages are skipped instead of evaluated,
//   * a 2-D prefix sum of occupied obstacle cells plus per-cell vertical extents prove "no solid within reach" for the
//     clearance probes of a triangle, and obstacle columns are cached across frames,
//   * refinement bookkeeping uses flat arrays instead of unordered containers (ids are only identities, so the
//     numbering of refinement midpoints cannot change the output), and the first-seen vertex numbering is produced by
//     merging per-chunk lists.
// Everything that is not provably identical falls back to the original per-point arithmetic. STARLIGHT_FLOW_VERIFY=1
// compares every call against geometry_reference().
// ---------------------------------------------------------------------------------------------------------------------
namespace {
using Triangle=std::array<int,3>;
constexpr double kWet=.002;
constexpr int kField=TerrainFlow::N;           // 48 cells per axis
constexpr int kBlocks=kField-1;                // 47 interpolation blocks per axis
constexpr int kSubs=96;                        // certificate granularity: a quarter-metre sub-cell (one terrain cell) of a block
// Liquid surface stages are the longest serial chain of a lava frame; more helpers shorten it (8 -> 12 threads: 3.06 -> 2.65 ms). Half of the machine, at most 12, never fewer than 8 where the CPU has them.
const unsigned kThreads=[]{const unsigned hardware=std::max(1u,std::thread::hardware_concurrency());unsigned threads=std::min(hardware,std::max(8u,std::min(12u,hardware/2)));const char* text=std::getenv("STARLIGHT_FLOW_THREADS");if(text){long value=std::strtol(text,nullptr,10);if(value>=1&&value<=32)threads=unsigned(value);}return threads;}();
constexpr uint64_t kEmptyKey=~0ull;
inline uint64_t edgeKey(int a,int b){return (uint64_t(std::min(a,b))<<32)|uint32_t(std::max(a,b));}
inline size_t hashKey(uint64_t k){k^=k>>33;k*=0xff51afd7ed558ccdull;k^=k>>33;k*=0xc4ceb9fe1a85ec53ull;k^=k>>33;return size_t(k);}
// Open-addressing 64-bit key -> int table with a monotone insertion API.
struct EdgeTable {
    std::vector<uint64_t> keys;std::vector<int> values;size_t mask=0,count=0;
    void reset(size_t hint){size_t capacity=16;while(capacity<hint*2)capacity<<=1;keys.assign(capacity,kEmptyKey);values.resize(capacity);mask=capacity-1;count=0;}
    void grow(){
        std::vector<uint64_t> oldKeys=std::move(keys);std::vector<int> oldValues=std::move(values);
        size_t capacity=(mask+1)*2;keys.assign(capacity,kEmptyKey);values.resize(capacity);mask=capacity-1;count=0;
        for(size_t i=0;i<oldKeys.size();++i)if(oldKeys[i]!=kEmptyKey)put(oldKeys[i],oldValues[i]);
    }
    void put(uint64_t key,int value){size_t i=hashKey(key)&mask;while(keys[i]!=kEmptyKey)i=(i+1)&mask;keys[i]=key;values[i]=value;++count;}
    // Returns the stored value, or inserts `value` and returns it with inserted=true.
    int find_or_insert(uint64_t key,int value,bool& inserted){
        if((count+1)*2>mask+1)grow();
        size_t i=hashKey(key)&mask;
        while(keys[i]!=kEmptyKey){if(keys[i]==key){inserted=false;return values[i];}i=(i+1)&mask;}
        keys[i]=key;values[i]=value;++count;inserted=true;return value;
    }
    int find(uint64_t key)const{size_t i=hashKey(key)&mask;while(keys[i]!=kEmptyKey){if(keys[i]==key)return values[i];i=(i+1)&mask;}return -1;}
    bool contains(uint64_t key)const{size_t i=hashKey(key)&mask;while(keys[i]!=kEmptyKey){if(keys[i]==key)return true;i=(i+1)&mask;}return false;}
};
struct CrossingWork {int a,b,id;};
// Waits for an asynchronous pool job if the scope is left early (an exception) so that no helper thread keeps running
// against locals that are about to disappear.
struct AsyncJob {
    LiquidWorkers& pool;bool active;
    AsyncJob(LiquidWorkers& workers,bool started):pool(workers),active(started){}
    void join(){if(active){active=false;pool.wait();}}
    ~AsyncJob(){if(active){try{pool.wait();}catch(...){}}}
};
struct PointPost {Vec3 color;double y=0;float kind=0;uint64_t motion=0;}; // everything a vertex gets from its final position
}
struct FlowGeometryScratch {
    std::atomic<bool> busy{false};
    // Static per terrain / obstacle mask.
    uint64_t terrainVersion=0,maskRevision=0;bool maskPresent=false;
    std::vector<double> bankMax;std::vector<uint8_t> volumeSafe,obstacleFree;
    // Per call.
    std::array<double,2304> heights{};std::vector<double> extended=std::vector<double>(2304);
    std::vector<uint8_t> certificate=std::vector<uint8_t>(kSubs*kSubs);
    std::vector<Vec3> points;std::vector<uint8_t> inside;std::vector<uint16_t> blockOf;
    std::vector<Triangle> triangleA,triangleB;std::vector<uint8_t> knownA,knownB,flags,marks,childCount;
    std::vector<int> boundary,vertexId,vertexSource,clipped;
    std::vector<uint8_t> splitVertex;
    std::vector<size_t> chunkSum;
    std::vector<CrossingWork> crossingWork;std::vector<Vec3> crossingPosition,vertexPosition;
    EdgeTable splitTable,crossingTable;
    std::vector<Vec3> faceNormal;
    struct TopologyItem {int a,b;};
    std::vector<std::vector<TopologyItem>> chunkItems;std::vector<size_t> chunkTriangles;std::vector<std::vector<uint32_t>> workerSeen;uint32_t seenEpoch=0;
    std::vector<PointPost> pointPost,crossingPost;std::vector<uint8_t> keep;std::vector<size_t> blockKept;
};
FlowGeometryScratchHolder::~FlowGeometryScratchHolder(){delete scratch;}
struct FlowAsyncGeometry {
    std::mutex mutex;std::condition_variable wake,idle;
    const TerrainFlow* owner=nullptr;
    bool requested=false,running=false,stop=false,hasPrepared=false;
    std::vector<MeshPart> prepared;std::exception_ptr error;
    std::thread thread;
    void run(){
        std::unique_lock lock(mutex);
        for(;;){
            wake.wait(lock,[&]{return requested||stop;});
            if(stop)return;
            requested=false;running=true;lock.unlock();
            std::vector<MeshPart> result;std::exception_ptr failure;
            const double g0=starlight_diag::evnow();const auto before=flow_geometry_profile();
            try{result=owner->geometry_checked();}catch(...){failure=std::current_exception();}
            if(starlight_diag::events_enabled()){const auto after=flow_geometry_profile();const double g1=starlight_diag::evnow();
                starlight_diag::evlog("geom",g1-g0,after.setupMs-before.setupMs,after.gridMs-before.gridMs);
                starlight_diag::evlog("geom2",after.refineMs-before.refineMs,after.clipMs-before.clipMs,after.vertexMs-before.vertexMs);
                starlight_diag::evlog("geom3",after.obstacleMs-before.obstacleMs,after.normalsMs-before.normalsMs,double(after.triangles-before.triangles));
                starlight_diag::evlog("geom4",after.clipTopologyMs-before.clipTopologyMs,after.clipCrossingsMs-before.clipCrossingsMs,after.clipClearanceMs-before.clipClearanceMs);}
            lock.lock();
            if(failure)error=failure;else{prepared=std::move(result);hasPrepared=true;}
            running=false;idle.notify_all();
        }
    }
    ~FlowAsyncGeometry(){{std::lock_guard lock(mutex);stop=true;}wake.notify_all();if(thread.joinable())thread.join();}
};
FlowAsyncGeometryHolder::~FlowAsyncGeometryHolder(){delete state;}
void TerrainFlow::geometry_begin_async() const {
    if(!asyncGeometry_.state){auto* created=new FlowAsyncGeometry;created->owner=this;created->thread=std::thread([created]{created->run();});asyncGeometry_.state=created;}
    auto& s=*asyncGeometry_.state;std::lock_guard lock(s.mutex);
    if(s.requested||s.running)return;
    s.requested=true;s.wake.notify_one();
}
void TerrainFlow::settle_async_impl() const {
    auto& s=*asyncGeometry_.state;std::unique_lock lock(s.mutex);
    s.idle.wait(lock,[&]{return !s.requested&&!s.running;});
    if(s.error){auto failure=s.error;s.error=nullptr;std::rethrow_exception(failure);}
}
std::vector<MeshPart> TerrainFlow::geometry_for_render() const {
    if(asyncGeometry_.state){
        auto& s=*asyncGeometry_.state;
        {std::lock_guard lock(s.mutex);if(s.hasPrepared){s.hasPrepared=false;return std::move(s.prepared);}}
        settle_async_impl();
        // Nothing was ready (first frame, or right after a reset): wait for the run just started and hand out a copy while
        // keeping the original, so the next frame finds a finished result (one frame old) without waiting again.
        {std::lock_guard lock(s.mutex);if(s.hasPrepared)return s.prepared;}
    }
    return geometry_checked();
}
namespace {
inline int blockIndex(double u){return std::min(int(std::clamp(u,0.,double(kField-1))),kField-2);}
// Sub-cell (0..95) of a point from its interpolation coordinate u = (p+12)/Cell-.5, derived from the same value that
// selects the interpolation block so the certificate and the sampled cells can never disagree.
inline int subOf(double u){
    if(!(u>=0))return 0;
    if(u>=double(kField-1))return kSubs-1;
    const int b=std::min(int(u),kField-2);const bool upper=u-b>=.5;
    if(b==0)return upper?2:1;
    if(b==kField-2)return upper?kSubs-2:kSubs-3;
    return 2*b+1+(upper?1:0);
}
inline int blockOfSub(int sub){return sub==0?0:std::min((sub-1)/2,kField-2);}
struct ScratchLease {
    FlowGeometryScratch* scratch;std::unique_ptr<FlowGeometryScratch> owned;bool leased=false;
    explicit ScratchLease(FlowGeometryScratchHolder& holder){
        if(!holder.scratch)holder.scratch=new FlowGeometryScratch;
        bool expected=false;
        if(holder.scratch->busy.compare_exchange_strong(expected,true)){scratch=holder.scratch;leased=true;}
        else{owned=std::make_unique<FlowGeometryScratch>();scratch=owned.get();}
    }
    ~ScratchLease(){if(leased)scratch->busy.store(false);}
};
// Same arithmetic as the lambdas of the reference path (operation order included); all fields are immutable here.
struct FieldSampler {
    const double* depths;const double* heights;const double* extended;const TerrainData* terrain;const FlowObstacleMask* obstacles;
    const uint8_t* certificate;
    static double bilinear(const double* field,double u,double v){
        u=std::clamp(u,0.,double(kField-1));
        v=std::clamp(v,0.,double(kField-1));
        int x=std::min(int(u),kField-2),z=std::min(int(v),kField-2);
        u-=x;
        v-=z;
        return field[z*kField+x]*(1-u)*(1-v)+field[z*kField+x+1]*u*(1-v)+field[(z+1)*kField+x]*(1-u)*v+field[(z+1)*kField+x+1]*u*v;
    }
    double wetSurface(double u,double v)const{
        u=std::clamp(u,0.,double(kField-1));v=std::clamp(v,0.,double(kField-1));int x=std::min(int(u),kField-2),z=std::min(int(v),kField-2);u-=x;v-=z;double height=0,weight=0;
        for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx){int i=(z+dz)*kField+x+dx;double w=(dx?u:1-u)*(dz?v:1-v);if(depths[i]>kWet){height+=heights[i]*w;weight+=w;}}
        return weight>1e-12?height/weight:bilinear(extended,u+x,v+z);
    }
    // All four cells wet: the same sums without the wet tests.
    double wetSurfaceAllWet(double u,double v)const{
        u=std::clamp(u,0.,double(kField-1));v=std::clamp(v,0.,double(kField-1));int x=std::min(int(u),kField-2),z=std::min(int(v),kField-2);u-=x;v-=z;double height=0,weight=0;
        for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx){int i=(z+dz)*kField+x+dx;double w=(dx?u:1-u)*(dz?v:1-v);height+=heights[i]*w;weight+=w;}
        return height/weight;
    }
    // coverage > wet for the reference point above (px,pz): coverage is min(depth, height above bed, obstacle clearance),
    // so the comparison is the conjunction of the three, evaluated cheapest first and abandoned as soon as one fails.
    // The surface height of a point that turns out not to be wet is never used (such points are neither vertices nor
    // shape the bisection, which only reads x and z), so it is left at zero in that case.
    bool evaluate(double px,double pz,double u,double v,Vec3* position)const{
        if(!(bilinear(depths,u,v)>kWet)){if(position)*position={px,0,pz};return false;}
        Vec3 point{px,wetSurface(u,v)+.009,pz};
        if(position)*position=point;
        if(!(point.y-terrain->surface_at(px,pz)>kWet))return false;
        return !obstacles||obstacles->clearance_capped(point,.05)>kWet;
    }
    // Position and "coverage > wet" of the surface point above (px,pz).
    void point(double px,double pz,Vec3& position,uint8_t& isInside,uint16_t& block)const{
        double u=(px+12)/TerrainFlow::Cell-.5,v=(pz+12)/TerrainFlow::Cell-.5;
        int sx=subOf(u),sz=subOf(v);block=uint16_t(sx|(sz<<8));
        switch(certificate[sz*kSubs+sx]){
        case 1:position={px,wetSurfaceAllWet(u,v)+.009,pz};isInside=1;return;
        case 2:position={px,0,pz};isInside=0;return;
        default:isInside=evaluate(px,pz,u,v,&position);
        }
    }
    // coverage(px,pz) > wet, without the position.
    bool wetAt(double px,double pz)const{
        double u=(px+12)/TerrainFlow::Cell-.5,v=(pz+12)/TerrainFlow::Cell-.5;
        switch(certificate[subOf(v)*kSubs+subOf(u)]){
        case 1:return true;
        case 2:return false;
        default:return evaluate(px,pz,u,v,nullptr);
        }
    }
    double surfaceY(double px,double pz)const{
        double u=(px+12)/TerrainFlow::Cell-.5,v=(pz+12)/TerrainFlow::Cell-.5;
        return (certificate[subOf(v)*kSubs+subOf(u)]==1?wetSurfaceAllWet(u,v):wetSurface(u,v))+.009;
    }
};
double subLow(int s){return -12.+.25*s;}
double subHigh(int s){return -12.+.25*(s+1);}
// Parallel exclusive prefix helper: chunk sums are computed by the caller; returns the total.
size_t prefixSums(std::vector<size_t>& sums){size_t total=0;for(auto& value:sums){size_t v=value;value=total;total+=v;}return total;}
}
std::vector<MeshPart> TerrainFlow::geometry() const {settle_async();return geometry_checked();}
std::vector<MeshPart> TerrainFlow::geometry_checked() const {
    static const bool verify=[]{const char* value=std::getenv("STARLIGHT_FLOW_VERIFY");return value&&value[0]=='1';}();
    if(!verify||exactClearance_)return geometry_unverified();
    auto fast=geometry_unverified();
    exactClearance_=true;
    std::vector<MeshPart> exact;
    try{exact=geometry_unverified();}catch(...){exactClearance_=false;throw;}
    exactClearance_=false;
    bool same=fast.size()==exact.size();
    for(size_t part=0;same&&part<fast.size();++part){
        const auto& a=fast[part];const auto& b=exact[part];
        same=a.mesh.indices==b.mesh.indices&&a.mesh.vertices.size()==b.mesh.vertices.size()&&a.emissive==b.emissive&&std::memcmp(&a.color,&b.color,sizeof(Vec3))==0;
        for(size_t i=0;same&&i<a.mesh.vertices.size();++i){
            const auto& x=a.mesh.vertices[i];const auto& y=b.mesh.vertices[i];
            same=std::memcmp(&x.position,&y.position,sizeof(Vec3))==0&&std::memcmp(&x.normal,&y.normal,sizeof(Vec3))==0&&std::memcmp(&x.color,&y.color,sizeof(Vec3))==0&&x.kind==y.kind&&x.motionIdentity==y.motionIdentity&&!x.materialCoordinate&&!y.materialCoordinate;
        }
    }
    if(!same)throw std::runtime_error("Fast liquid surface differs from the reference algorithm");
    return fast;
}
std::vector<MeshPart> TerrainFlow::geometry_reference() const {
    std::vector<MeshPart> result;
    auto add = [&](const auto &depths, bool isLava) {
        if(std::none_of(depths.begin(),depths.end(),[](double depth){return depth>.002;}))return;
        Mesh mesh;LiquidMeshTimes meshTimes;meshTimes.referenceRefinement=true;
        const int R=N*(isLava?4:2);
        std::array<double,2304> heights;for(int i=0;i<2304;++i)heights[i]=surface(i,depths[i])+waves_[i];
        std::vector<double> extended(heights.begin(),heights.end());
        for(int z=0;z<N;++z)for(int x=0;x<N;++x){int i=z*N+x;if(depths[i]>.002)continue;int best=100;
            for(int dz=-2;dz<=2;++dz)for(int dx=-2;dx<=2;++dx){int xx=x+dx,zz=z+dz,distance=dx*dx+dz*dz;if(xx<0||xx>=N||zz<0||zz>=N||distance>=best)continue;int j=zz*N+xx;if(depths[j]>.002){extended[i]=heights[j];best=distance;}}}
        auto sample = [&](const auto& field,double u, double v) {
            u = std::clamp(u, 0., double(N - 1));
            v = std::clamp(v, 0., double(N - 1));
            int x = std::min(int(u), N - 2), z = std::min(int(v), N - 2);
            u -= x;
            v -= z;
            return field[z * N + x] * (1 - u) * (1 - v) + field[z * N + x + 1] * u * (1 - v) +
                   field[(z + 1) * N + x] * (1 - u) * v + field[(z + 1) * N + x + 1] * u * v;
        };
        auto wetSurface=[&](double u,double v){
            u=std::clamp(u,0.,double(N-1));v=std::clamp(v,0.,double(N-1));int x=std::min(int(u),N-2),z=std::min(int(v),N-2);u-=x;v-=z;double height=0,weight=0;
            for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx){int i=(z+dz)*N+x+dx;double w=(dx?u:1-u)*(dz?v:1-v);if(depths[i]>.002){height+=heights[i]*w;weight+=w;}}
            // Dry terrain is not a liquid height. Mixing it into the surface
            // raises water onto the bank and creates a floating stepped skirt.
            return weight>1e-12?height/weight:sample(extended,u+x,v+z);
        };
        auto wetHeat=[&](double px,double pz){
            double u=std::clamp((px+12)/Cell-.5,0.,double(N-1)),v=std::clamp((pz+12)/Cell-.5,0.,double(N-1));int x=std::min(int(u),N-2),z=std::min(int(v),N-2);u-=x;v-=z;double heat=0,weight=0;
            for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx){int i=(z+dz)*N+x+dx;double w=(dx?u:1-u)*(dz?v:1-v);if(depths[i]>.002){heat+=lavaHeat_[i]*w;weight+=w;}}
            // Boundary vertices are on the visible surface, even though the
            // physics query deliberately rejects the domain's maximum edge.
            return weight>1e-12?heat/weight:sample(lavaHeat_,u+x,v+z);
        };
        mesh=liquid_surface_mesh(R,[&](double px,double pz){
            double u=(px+12)/Cell-.5,v=(pz+12)/Cell-.5;
            Vec3 point{px,wetSurface(u,v)+.009,pz};
            double gap=std::min(sample(depths,u,v),point.y-terrain_.surface_at(px,pz));
            if(obstacles_)gap=std::min(gap,obstacles_->clearance(point));
            return LiquidSurfacePoint{point,gap};
        },4,3,[&](const Vec3& point){return obstacles_?obstacles_->clearance(point):double(INFINITY);},&meshTimes);
        if (mesh.indices.empty())
            return;
        for (auto &v : mesh.vertices) {
            v.motionIdentity=vertex_motion_identity(isLava?6:5,uint64_t(int64_t(std::llround(v.position.x*1e6))),uint64_t(int64_t(std::llround(v.position.z*1e6))));
            v.kind = isLava ? 8.f : 7.f;
            const double bedDepth=v.position.y-terrain_.surface_at(v.position.x,v.position.z);
            v.color = {std::clamp(bedDepth*.5,0.,1.),
                       1, 1};
            if(isLava){double depth=std::max(0.,bedDepth),heat=wetHeat(v.position.x,v.position.z);auto crust=lava_crust(v.position.x,v.position.z,waveTime_,heat,depth,lavaCrustDepth_);v.position.y+=crust.relief;v.kind=8.1f;v.color={waveTime_,heat,crust.cover};}
        }
        if(obstacles_){
            std::vector<int> clearIndices;clearIndices.reserve(mesh.indices.size());
            const size_t triangles=mesh.indices.size()/3;
            std::vector<uint8_t> clear(triangles);
            liquid_parallel_for(triangles,256,kThreads,[&](size_t begin,size_t end){
                for(size_t t=begin;t<end;++t){size_t i=t*3;
                    auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;
                    clear[t]=obstacles_->clear_triangle_reference(a,b,c);
                }
            });
            for(size_t t=0;t<triangles;++t)if(clear[t]){size_t i=t*3;
                clearIndices.insert(clearIndices.end(),{mesh.indices[i],mesh.indices[i+1],mesh.indices[i+2]});
            }
            mesh.indices=std::move(clearIndices);
        }
        if(mesh.indices.empty())return;
        mesh.normals();
        result.push_back({std::move(mesh),
                          isLava ? Vec3{1, 85. / 255, 15. / 255} : Vec3{38. / 255, 151. / 255, 218. / 255},
                          isLava});
    };
    add(water, false);
    add(lava, true);
    return result;
}
std::vector<MeshPart> TerrainFlow::geometry_unverified() const {
    if(exactClearance_)return geometry_reference();
    std::vector<MeshPart> result;
    ScratchLease lease(geometryScratch_);
    FlowGeometryScratch& S=*lease.scratch;
    auto add = [&](const auto &depthArray, bool isLava) {
        const double* depths=depthArray.data();
        if(std::none_of(depthArray.begin(),depthArray.end(),[](double depth){return depth>kWet;}))return;
        auto lapStart=GeometryClock::now();
        auto lap=[&]{auto now=GeometryClock::now();double ms=geometryMs(lapStart,now);lapStart=now;return ms;};
        const int R=N*(isLava?4:2);
        const int W=R+1;
        const double spacing=24./R;
        auto& heights=S.heights;
        for(int i=0;i<2304;++i)heights[i]=surface(i,depthArray[i])+waves_[i];
        auto& extended=S.extended;
        extended.assign(heights.begin(),heights.end());
        for(int z=0;z<N;++z)for(int x=0;x<N;++x){int i=z*N+x;if(depths[i]>kWet)continue;int best=100;
            for(int dz=-2;dz<=2;++dz)for(int dx=-2;dx<=2;++dx){int xx=x+dx,zz=z+dz,distance=dx*dx+dz*dz;if(xx<0||xx>=N||zz<0||zz>=N||distance>=best)continue;int j=zz*N+xx;if(depths[j]>kWet){extended[i]=heights[j];best=distance;}}}
        // Static facts per terrain / obstacle mask: highest bank under each interpolation block, whether a carved volume
        // can touch it, and whether any obstacle triangle can lie in or next to its footprint.
        if(S.terrainVersion!=terrainVersion_||S.bankMax.size()!=size_t(kSubs*kSubs)){
            S.bankMax.assign(kSubs*kSubs,0.);S.volumeSafe.assign(kSubs*kSubs,0);
            for(int sz=0;sz<kSubs;++sz)for(int sx=0;sx<kSubs;++sx){
                const double x0=subLow(sx)-.06,x1=subHigh(sx)+.06,z0=subLow(sz)-.06,z1=subHigh(sz)+.06;
                const int a0=std::max(0,int(std::floor((x0+TerrainData::Half)/TerrainData::Step))-1),a1=std::min(TerrainData::N-1,int(std::ceil((x1+TerrainData::Half)/TerrainData::Step))+1);
                const int b0=std::max(0,int(std::floor((z0+TerrainData::Half)/TerrainData::Step))-1),b1=std::min(TerrainData::N-1,int(std::ceil((z1+TerrainData::Half)/TerrainData::Step))+1);
                double top=-INFINITY;
                for(int b=b0;b<=b1;++b)for(int a=a0;a<=a1;++a)top=std::max(top,terrain_.Heights[size_t(b)*TerrainData::N+a]);
                S.bankMax[sz*kSubs+sx]=top;
                bool safe=terrain_.Heights.size()==size_t(TerrainData::N)*TerrainData::N;
                for(const auto& v:terrain_.Volumes){
                    double dx=max3(0.,x0-v.X,v.X-x1),dz=max3(0.,z0-v.Z,v.Z-z1);
                    if(!(std::hypot(dx,dz)>v.Radius+.4))safe=false;
                }
                S.volumeSafe[sz*kSubs+sx]=safe;
            }
            S.terrainVersion=terrainVersion_;
        }
        const bool maskNow=obstacles_.has_value();
        if(!maskNow){
            if(S.maskPresent||S.obstacleFree.size()!=size_t(kSubs*kSubs)||S.maskRevision!=0){S.obstacleFree.assign(kSubs*kSubs,1);S.maskRevision=0;S.maskPresent=false;}
        }else if(obstacles_->geometryRevision==0||S.maskRevision!=obstacles_->geometryRevision||!S.maskPresent||S.obstacleFree.size()!=size_t(kSubs*kSubs)){
            S.obstacleFree.assign(kSubs*kSubs,0);
            for(int sz=0;sz<kSubs;++sz)for(int sx=0;sx<kSubs;++sx)
                S.obstacleFree[sz*kSubs+sx]=obstacles_->region_empty(subLow(sx)-.06,subHigh(sx)+.06,subLow(sz)-.06,subHigh(sz)+.06);
            S.maskRevision=obstacles_->geometryRevision;S.maskPresent=true;
        }
        {
            // 1: every point of the sub-cell is provably wet; 2: every point is provably dry (depth cannot exceed wet).
            // The depth and surface-height terms come from the four cells of the interpolation block the sub-cell lies in.
            struct BlockFacts {double depthLow,depthHigh,heightLow;};
            static thread_local std::vector<BlockFacts> facts;facts.resize(size_t(kBlocks)*kBlocks);
            for(int bz=0;bz<kBlocks;++bz)for(int bx=0;bx<kBlocks;++bx){
                const int i00=bz*kField+bx;
                facts[size_t(bz)*kBlocks+bx]={min3(std::min(depths[i00],depths[i00+1]),depths[i00+kField],depths[i00+kField+1]),
                    std::max(std::max(depths[i00],depths[i00+1]),std::max(depths[i00+kField],depths[i00+kField+1])),
                    min3(std::min(heights[i00],heights[i00+1]),heights[i00+kField],heights[i00+kField+1])};
            }
            for(int sz=0;sz<kSubs;++sz){const int bz=blockOfSub(sz);for(int sx=0;sx<kSubs;++sx){
                const auto& f=facts[size_t(bz)*kBlocks+blockOfSub(sx)];const int c=sz*kSubs+sx;
                S.certificate[c]=(S.obstacleFree[c]&&S.volumeSafe[c]&&f.depthLow>kWet+1e-6&&f.heightLow+.009-S.bankMax[c]>kWet+1e-6)?1:(f.depthHigh<kWet-1e-6?2:0);
            }}
        }
        FieldSampler sampler{depths,heights.data(),extended.data(),&terrain_,obstacles_?&*obstacles_:nullptr,S.certificate.data()};
        geometrySetupMs=geometrySetupMs+lap();
        const FlowObstacleMask* obstacles=sampler.obstacles;
        // ---- grid ---------------------------------------------------------------------------------------------------
        auto& points=S.points;auto& inside=S.inside;auto& blockOf=S.blockOf;
        points.resize(size_t(W)*W);inside.resize(size_t(W)*W);blockOf.resize(size_t(W)*W);
        liquid_parallel_for(points.size(),256,kThreads,[&](size_t begin,size_t end){
            for(size_t i=begin;i<end;++i){int z=int(i)/W,x=int(i)%W;sampler.point(-12+x*spacing,-12+z*spacing,points[i],inside[i],blockOf[i]);}
        });
                auto* triangles=&S.triangleA;auto* spare=&S.triangleB;auto* known=&S.knownA;auto* spareKnown=&S.knownB;
        triangles->resize(size_t(R)*R*2);
        liquid_parallel_for(size_t(R),32,kThreads,[&](size_t begin,size_t end){
            for(size_t z=begin;z<end;++z)for(int x=0;x<R;++x){int k=int(z)*W+x;size_t t=(z*R+x)*2;(*triangles)[t]={k,k+R+1,k+1};(*triangles)[t+1]={k+1,k+R+1,k+R+2};}
        });
        known->assign(triangles->size(),0);
        geometryGridMs=geometryGridMs+lap();
        // ---- boundary refinement (three passes; every pass classifies the triangles not yet proven interior) ------------
        auto triangleCertified=[&](const Triangle& t){
            const Vec3& p0=points[t[0]];const Vec3& p1=points[t[1]];const Vec3& p2=points[t[2]];
            if((p0.x==p1.x&&p1.x==p2.x)||(p0.z==p1.z&&p1.z==p2.z))return 0;
            int bx0=99,bx1=-1,bz0=99,bz1=-1;
            for(int j=0;j<3;++j){int block=blockOf[t[j]];int bx=block&255,bz=block>>8;bx0=std::min(bx0,bx);bx1=std::max(bx1,bx);bz0=std::min(bz0,bz);bz1=std::max(bz1,bz);}
            const int kind=S.certificate[bz0*kSubs+bx0];
            if(kind==0)return 0;
            for(int bz=bz0;bz<=bz1;++bz)for(int bx=bx0;bx<=bx1;++bx)if(S.certificate[bz*kSubs+bx]!=kind)return 0;
            return kind;
        };
        auto clearanceProbe=[&](const Vec3& point){return obstacles?obstacles->clearance_capped(point,.05):double(INFINITY);};
        for(unsigned pass=0;pass<3;++pass){
            const size_t count=triangles->size();
            S.flags.resize(count);
            liquid_parallel_for(count,64,kThreads,[&](size_t begin,size_t end){
                for(size_t i=begin;i<end;++i){
                    if((*known)[i]){S.flags[i]=0;continue;}
                    const Triangle& t=(*triangles)[i];
                    if(triangleCertified(t)!=0){S.flags[i]=0;(*known)[i]=1;continue;}
                    
                    bool a=inside[t[0]],b=inside[t[1]],c=inside[t[2]];
                    bool boundaryTriangle=a!=b||b!=c;
                    if(!boundaryTriangle){auto center=(points[t[0]]+points[t[1]]+points[t[2]])/3.;boundaryTriangle=sampler.wetAt(center.x,center.z)!=a;}
                    if(!boundaryTriangle)for(int j=0;j<3&&!boundaryTriangle;++j){auto midpoint=(points[t[j]]+points[t[(j+1)%3]])*.5;boundaryTriangle=sampler.wetAt(midpoint.x,midpoint.z)!=a;}
                    if(!boundaryTriangle&&a&&obstacles){
                        auto p=points[t[0]],q=points[t[1]],r=points[t[2]];
                        if(!obstacles->region_clear(min3(p.x,q.x,r.x),max3(p.x,q.x,r.x),min3(p.z,q.z,r.z),max3(p.z,q.z,r.z),min3(p.y,q.y,r.y),max3(p.y,q.y,r.y),.05)){
                        boundaryTriangle=clearanceProbe((p+q+r)/3.)<=kWet;
                        for(int j=0;j<3&&!boundaryTriangle;++j)boundaryTriangle=clearanceProbe((points[t[j]]+points[t[(j+1)%3]])*.5)<=kWet;
                        }
                    }
                    S.flags[i]=boundaryTriangle;
                    if(!boundaryTriangle)(*known)[i]=1;
                }
            });
                        S.boundary.clear();
            for(size_t i=0;i<count;++i)if(S.flags[i])S.boundary.push_back(int(i));
            if(S.boundary.empty())break;
            const int base=int(points.size());
            S.splitTable.reset(S.boundary.size()*3);
            std::vector<uint64_t> keys;keys.reserve(S.boundary.size()*3);
            for(int index:S.boundary){const Triangle& t=(*triangles)[index];for(int j=0;j<3;++j){
                bool inserted=false;uint64_t key=edgeKey(t[j],t[(j+1)%3]);
                S.splitTable.find_or_insert(key,base+int(keys.size()),inserted);
                if(inserted)keys.push_back(key);
            }}
            S.splitVertex.assign(points.size(),0);
            for(uint64_t key:keys){S.splitVertex[size_t(key>>32)]=1;S.splitVertex[size_t(uint32_t(key))]=1;}
                        points.resize(base+keys.size());inside.resize(base+keys.size());blockOf.resize(base+keys.size());
            liquid_parallel_for(keys.size(),64,kThreads,[&](size_t begin,size_t end){
                for(size_t k=begin;k<end;++k){
                    int a=int(keys[k]>>32),b=int(uint32_t(keys[k]));auto p=(points[a]+points[b])*.5;
                    sampler.point(p.x,p.z,points[base+k],inside[base+k],blockOf[base+k]);
                }
            });
                        // Rebuild the triangle list (two-phase: child counts, then positioned writes). Child order is the original's.
            constexpr size_t chunk=2048;
            const size_t chunks=(count+chunk-1)/chunk;
            S.marks.resize(count);S.childCount.resize(count);S.chunkSum.assign(chunks,0);
            liquid_parallel_for(chunks,1,kThreads,[&](size_t begin,size_t end){
                for(size_t c=begin;c<end;++c){
                    size_t sum=0;
                    for(size_t i=c*chunk;i<std::min(count,(c+1)*chunk);++i){
                        const Triangle& t=(*triangles)[i];unsigned mask=0;
                        for(int j=0;j<3;++j)if(S.splitVertex[t[j]]&&S.splitVertex[t[(j+1)%3]]&&S.splitTable.contains(edgeKey(t[j],t[(j+1)%3])))mask|=1u<<j;
                        S.marks[i]=uint8_t(mask);
                        static constexpr uint8_t children[8]{1,2,2,3,2,3,3,4};
                        S.childCount[i]=children[mask];sum+=children[mask];
                    }
                    S.chunkSum[c]=sum;
                }
            });
                        const size_t total=prefixSums(S.chunkSum);
            spare->resize(total);spareKnown->assign(total,0);
            liquid_parallel_for(chunks,1,kThreads,[&](size_t begin,size_t end){
                for(size_t c=begin;c<end;++c){
                    size_t out=S.chunkSum[c];
                    for(size_t i=c*chunk;i<std::min(count,(c+1)*chunk);++i){
                        const Triangle& t=(*triangles)[i];unsigned mask=S.marks[i];
                        auto midpoint=[&](int a,int b){return S.splitTable.find(edgeKey(a,b));};
                        switch(S.childCount[i]){
                        case 1:(*spare)[out]=t;(*spareKnown)[out]=(*known)[i];++out;break;
                        case 2:{int j=0;while(!(mask>>j&1))++j;int a=t[j],b=t[(j+1)%3],c2=t[(j+2)%3],m=midpoint(a,b);(*spare)[out++]={a,m,c2};(*spare)[out++]={m,b,c2};break;}
                        case 3:{int j=0;while(!((mask>>j&1)&&(mask>>((j+1)%3)&1)))++j;int a=t[j],b=t[(j+1)%3],c2=t[(j+2)%3],ab=midpoint(a,b),bc=midpoint(b,c2);(*spare)[out++]={b,bc,ab};(*spare)[out++]={a,ab,c2};(*spare)[out++]={ab,bc,c2};break;}
                        default:{int a=t[0],b=t[1],c2=t[2],ab=midpoint(a,b),bc=midpoint(b,c2),ca=midpoint(c2,a);(*spare)[out++]={a,ab,ca};(*spare)[out++]={ab,b,bc};(*spare)[out++]={ca,bc,c2};(*spare)[out++]={ab,bc,ca};break;}
                        }
                    }
                }
            });
                        std::swap(triangles,spare);std::swap(known,spareKnown);
        }
        geometryRefineMs=geometryRefineMs+lap();
        // ---- per-point surface fields (workers) overlapped with the clip topology (calling thread) ---------------------------
        // Every field written for a vertex (motion identity, colour, kind and the lava relief) depends only on the
        // vertex position, and every inside point becomes exactly one vertex, so they are computed per point while the
        // serial first-seen numbering below runs.
        auto wetHeat=[&](double px,double pz){
            double u=std::clamp((px+12)/Cell-.5,0.,double(N-1)),v=std::clamp((pz+12)/Cell-.5,0.,double(N-1));int x=std::min(int(u),N-2),z=std::min(int(v),N-2);u-=x;v-=z;double heat=0,weight=0;
            for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx){int i=(z+dz)*N+x+dx;double w=(dx?u:1-u)*(dz?v:1-v);if(depths[i]>.002){heat+=lavaHeat_[i]*w;weight+=w;}}
            // Boundary vertices are on the visible surface, even though the
            // physics query deliberately rejects the domain's maximum edge.
            return weight>1e-12?heat/weight:FieldSampler::bilinear(lavaHeat_.data(),u+x,v+z);
        };
        auto surfaceFields=[&](const Vec3& position)->PointPost{
            PointPost r;
            r.motion=vertex_motion_identity(isLava?6:5,uint64_t(int64_t(std::llround(position.x*1e6))),uint64_t(int64_t(std::llround(position.z*1e6))));
            r.kind=isLava?8.f:7.f;
            const double bedDepth=position.y-terrain_.surface_at(position.x,position.z);
            r.color={std::clamp(bedDepth*.5,0.,1.),1,1};
            r.y=position.y;
            if(isLava){double depth=std::max(0.,bedDepth),heat=wetHeat(position.x,position.z);auto crust=lava_crust(position.x,position.z,waveTime_,heat,depth,lavaCrustDepth_);r.y=position.y+crust.relief;r.kind=8.1f;r.color={waveTime_,heat,crust.cover};}
            return r;
        };
        LiquidWorkers& pool=LiquidWorkers::get();
        S.pointPost.resize(points.size());
        auto pointJob=[&](size_t begin,size_t end,unsigned){for(size_t i=begin;i<end;++i)if(inside[i])S.pointPost[i]=surfaceFields(points[i]);};
        // ---- clip topology -----------------------------------------------------------------------------------------------
        // The reference numbers vertices (and crossings) in first-seen order while walking the triangles. That order is
        // reproduced in three steps: (A, parallel) every chunk of triangles lists the points / crossing edges it touches in
        // its own first-seen order; (B, serial, overlapped with the per-point fields) the lists are merged in chunk order,
        // which assigns the global ids; (C, parallel) the clipped triangles are written with those ids.
        Mesh mesh;
        const size_t pointCount=points.size();
        const size_t triangleTotal=triangles->size();
        constexpr size_t topologyChunk=1024;
        const size_t topologyChunks=(triangleTotal+topologyChunk-1)/topologyChunk;
        static constexpr uint8_t clippedPerMask[8]{0,1,1,2,1,2,2,1};
        if(S.chunkItems.size()<topologyChunks)S.chunkItems.resize(topologyChunks);
        S.chunkTriangles.assign(topologyChunks+1,0);
        const unsigned seenArrays=std::min(kThreads,pool.size());
        if(S.workerSeen.size()<seenArrays)S.workerSeen.resize(seenArrays);
        for(size_t w=0;w<S.workerSeen.size();++w)if(S.workerSeen[w].size()<pointCount)S.workerSeen[w].resize(pointCount,0);
        if(S.seenEpoch>0xfffff000u-uint32_t(topologyChunks)){for(auto& seen:S.workerSeen)std::fill(seen.begin(),seen.end(),0u);S.seenEpoch=0;}
        const uint32_t epochBase=S.seenEpoch;S.seenEpoch+=uint32_t(topologyChunks)+1;
        auto listJob=[&](size_t begin,size_t end,unsigned worker){
            uint32_t* seen=S.workerSeen[worker].data();
            const uint8_t* in=inside.data();
            for(size_t c=begin;c<end;++c){
                auto& items=S.chunkItems[c];items.clear();
                const uint32_t stamp=epochBase+uint32_t(c)+1;
                size_t clippedTriangles=0;
                for(size_t i=c*topologyChunk;i<std::min(triangleTotal,(c+1)*topologyChunk);++i){
                    const Triangle& t=(*triangles)[i];
                    const unsigned mask=unsigned(in[t[0]])|unsigned(in[t[1]])<<1|unsigned(in[t[2]])<<2;
                    if(mask==0)continue;
                    clippedTriangles+=clippedPerMask[mask];
                    for(int j=0;j<3;++j){
                        const int a=t[j],b=t[(j+1)%3];
                        if(in[a]&&seen[a]!=stamp){seen[a]=stamp;items.push_back({a,-1});}
                        if(in[a]!=in[b])items.push_back({a,b});
                    }
                }
                S.chunkTriangles[c+1]=clippedTriangles;
            }
        };
        pool.run(topologyChunks,1,kThreads,listJob);
        AsyncJob pointAsync(pool,pool.submit(points.size(),1024,kThreads,pointJob));
        S.vertexId.assign(pointCount,-1);
        S.vertexSource.clear();S.vertexSource.reserve(pointCount);
        S.crossingWork.clear();
        S.crossingTable.reset(256);
        size_t itemTotal=0;for(size_t c=0;c<topologyChunks;++c)itemTotal+=S.chunkItems[c].size();
        if(S.vertexPosition.size()<itemTotal+8)S.vertexPosition.resize(itemTotal+8);
        {
            int nextId=0;
            int* vid=S.vertexId.data();
            for(size_t c=0;c<topologyChunks;++c)for(const auto& item:S.chunkItems[c]){
                if(item.b<0){
                    int id=vid[item.a];
                    if(id<0){id=vid[item.a]=nextId++;S.vertexSource.push_back(item.a);S.vertexPosition[id]=points[item.a];}
                }else{
                    bool inserted=false;S.crossingTable.find_or_insert(edgeKey(item.a,item.b),nextId,inserted);
                    if(inserted){S.crossingWork.push_back({item.a,item.b,nextId});S.vertexSource.push_back(-int(S.crossingWork.size()));++nextId;}
                }
            }
        }
        for(size_t c=0;c<topologyChunks;++c)S.chunkTriangles[c+1]+=S.chunkTriangles[c];
        const size_t clippedCount=S.chunkTriangles[topologyChunks];
        if(S.clipped.size()<clippedCount*3+6)S.clipped.resize(clippedCount*3+6);
        pointAsync.join();
        auto emitJob=[&](size_t begin,size_t end,unsigned){
            const uint8_t* in=inside.data();const int* vid=S.vertexId.data();
            for(size_t c=begin;c<end;++c){
                int* out=S.clipped.data()+S.chunkTriangles[c]*3;
                for(size_t i=c*topologyChunk;i<std::min(triangleTotal,(c+1)*topologyChunk);++i){
                    const Triangle& t=(*triangles)[i];
                    const unsigned mask=unsigned(in[t[0]])|unsigned(in[t[1]])<<1|unsigned(in[t[2]])<<2;
                    if(mask==0)continue;
                    if(mask==7){out[0]=vid[t[0]];out[1]=vid[t[1]];out[2]=vid[t[2]];out+=3;continue;}
                    int polygon[6],count=0;
                    for(int j=0;j<3;++j){
                        const int a=t[j],b=t[(j+1)%3];
                        if(in[a])polygon[count++]=vid[a];
                        if(in[a]!=in[b])polygon[count++]=S.crossingTable.find(edgeKey(a,b));
                    }
                    for(int j=1;j+1<count;++j){out[0]=polygon[0];out[1]=polygon[j];out[2]=polygon[j+1];out+=3;}
                }
            }
        };
        pool.run(topologyChunks,1,kThreads,emitJob);
        const size_t vertexCount=S.vertexSource.size();
        geometryClipTopologyMs=geometryClipTopologyMs+lap();
        // ---- crossing vertices: 14 bisection steps per crossing (workers) while the calling thread allocates the mesh ----------
        S.crossingPosition.resize(S.crossingWork.size());S.crossingPost.resize(S.crossingWork.size());
        auto crossingJob=[&](size_t begin,size_t end,unsigned){
            for(size_t k=begin;k<end;++k){
                const auto& crossing=S.crossingWork[k];int a=crossing.a,b=crossing.b;
                auto origin=points[a],delta=points[b]-origin;
                double lo=0,hi=1;bool wetA=inside[a];
                for(int i=0;i<14;++i){
                    double t=(lo+hi)*.5;auto p=origin+delta*t;
                    // lo = t when the probe agrees with the inside endpoint, else hi = t (selected without a branch)
                    const uint64_t same=uint64_t(0)-uint64_t(sampler.wetAt(p.x,p.z)==wetA);
                    const uint64_t tb=std::bit_cast<uint64_t>(t),lb=std::bit_cast<uint64_t>(lo),hb=std::bit_cast<uint64_t>(hi);
                    lo=std::bit_cast<double>((tb&same)|(lb&~same));hi=std::bit_cast<double>((hb&same)|(tb&~same));
                }
                auto p=origin+delta*(wetA?lo:hi);Vec3 position{p.x,sampler.surfaceY(p.x,p.z),p.z};
                S.crossingPosition[k]=position;S.crossingPost[k]=surfaceFields(position);S.vertexPosition[crossing.id]=position;
            }
        };
        AsyncJob crossingAsync(pool,pool.submit(S.crossingWork.size(),32,kThreads,crossingJob));
        mesh.vertices.resize(vertexCount);
        S.keep.resize(clippedCount);
        constexpr size_t block=128,vertexBlock=512;
        const size_t triangleBlocks=(clippedCount+block-1)/block,vertexBlocks=(vertexCount+vertexBlock-1)/vertexBlock;
        S.blockKept.assign(triangleBlocks+1,0);
        crossingAsync.join();
        geometryClipCrossingsMs=geometryClipCrossingsMs+lap();
        // ---- vertices (fully formed) and, per clipped triangle, the clip and obstacle classification ----------------------------
        // Position of vertex `id` before / after the lava relief, from the point or crossing it came from.
        auto sourceHeight=[&](int id)->double{int source=S.vertexSource[id];return source>=0?S.pointPost[source].y:S.crossingPost[size_t(-source-1)].y;};
        std::atomic<bool> finite{true};
        const double reliefReach=isLava?std::max(0.,lavaCrustDepth_)+1e-9:0.;
        auto fillJob=[&](size_t begin,size_t end,unsigned){
            for(size_t job=begin;job<end;++job){
                if(job<triangleBlocks){
                    size_t kept=0;
                    for(size_t k=job*block;k<std::min(clippedCount,(job+1)*block);++k){
                        const int* ids=S.clipped.data()+k*3;
                        const Vec3 p=S.vertexPosition[ids[0]],q=S.vertexPosition[ids[1]],r=S.vertexPosition[ids[2]];
                        bool clear=true;
                        // The lava relief only raises a vertex, by at most the crust depth, so one region test over the raised
                        // height range covers the clip probes and the final-triangle obstacle pass alike.
                        if(obstacles&&!obstacles->region_clear(min3(p.x,q.x,r.x),max3(p.x,q.x,r.x),min3(p.z,q.z,r.z),max3(p.z,q.z,r.z),min3(p.y,q.y,r.y),max3(p.y,q.y,r.y)+reliefReach,.05)){
                            
                            {   // start loading the cached columns of the ten probes before evaluating them
                                obstacles->prefetch_clearance((p+q+r)/3.);
                                for(int edge=0;edge<3;++edge){auto a=edge==0?p:edge==1?q:r,b=edge==0?q:edge==1?r:p;for(int probe=1;probe<4;++probe)obstacles->prefetch_clearance(a+(b-a)*(probe*.25));}
                            }
                            clear=clearanceProbe((p+q+r)/3.)>=-1e-6;
                            for(int edge=0;edge<3&&clear;++edge){auto a=edge==0?p:edge==1?q:r,b=edge==0?q:edge==1?r:p;
                                for(int probe=1;probe<4&&clear;++probe)clear=clearanceProbe(a+(b-a)*(probe*.25))>=-1e-6;}
                        }
                        // Obstacle pass on the final (relief-displaced) triangle.
                        if(clear&&obstacles_){
                            Vec3 a{p.x,sourceHeight(ids[0]),p.z},b{q.x,sourceHeight(ids[1]),q.z},c{r.x,sourceHeight(ids[2]),r.z};
                            if(!obstacles_->region_clear(min3(a.x,b.x,c.x),max3(a.x,b.x,c.x),min3(a.z,b.z,c.z),max3(a.z,b.z,c.z),min3(a.y,b.y,c.y),max3(a.y,b.y,c.y),.05)){
                                
                                obstacles_->prefetch_clearance(a);obstacles_->prefetch_clearance(b);obstacles_->prefetch_clearance(c);obstacles_->prefetch_clearance((a+b+c)/3);
                                clear=obstacles_->clear_triangle(a,b,c);
                            }
                        }
                        S.keep[k]=clear;kept+=clear;
                    }
                    S.blockKept[job]=kept;
                }else{
                    const size_t vb=job-triangleBlocks;
                    bool ok=true;
                    for(size_t id=vb*vertexBlock;id<std::min(vertexCount,(vb+1)*vertexBlock);++id){
                        int source=S.vertexSource[id];
                        const Vec3 pre=S.vertexPosition[id];
                        const PointPost& post=source>=0?S.pointPost[source]:S.crossingPost[size_t(-source-1)];
                        Vertex& v=mesh.vertices[id];
                        v.position={pre.x,post.y,pre.z};v.motionIdentity=post.motion;v.kind=post.kind;v.color=post.color;
                        ok&=std::isfinite(pre.x)&&std::isfinite(post.y)&&std::isfinite(pre.z);
                    }
                    if(!ok)finite.store(false,std::memory_order_relaxed);
                }
            }
        };
        pool.run(triangleBlocks+vertexBlocks,1,kThreads,fillJob);
        geometryClipClearanceMs=geometryClipClearanceMs+lap();
        size_t keptTotal=0;
        for(size_t c=0;c<triangleBlocks;++c){size_t v=S.blockKept[c];S.blockKept[c]=keptTotal;keptTotal+=v;}
        if(keptTotal==0){geometryClipMs=geometryClipMs+lap();return;}
        mesh.indices.resize(keptTotal*3);
        S.faceNormal.resize(keptTotal);
        auto writeJob=[&](size_t begin,size_t end,unsigned){
            for(size_t c=begin;c<end;++c){
                size_t out=S.blockKept[c];
                for(size_t k=c*block;k<std::min(clippedCount,(c+1)*block);++k)if(S.keep[k]){
                    const int* ids=S.clipped.data()+k*3;
                    mesh.indices[out*3]=ids[0];mesh.indices[out*3+1]=ids[1];mesh.indices[out*3+2]=ids[2];
                    const Vec3& a=mesh.vertices[ids[0]].position;const Vec3& b=mesh.vertices[ids[1]].position;const Vec3& c2=mesh.vertices[ids[2]].position;
                    S.faceNormal[out]=(b-a).cross(c2-a);
                    ++out;
                }
            }
        };
        pool.run(triangleBlocks,1,kThreads,writeJob);
        geometryClipMs=geometryClipMs+lap();
        geometryVertexMs=geometryVertexMs+lap();
        geometryObstacleMs=geometryObstacleMs+lap();
        // ---- normals (Mesh::normals() with the per-vertex sums split across workers; same summation order) ------------------
        // One bad vertex costs this liquid for this frame: it is counted and logged, and the next frame builds again. Throwing here would be
        // rethrown into the render loop by the next change to the flow and end the render worker.
        if(!finite.load()){nonfinite_parts_dropped().fetch_add(1,std::memory_order_relaxed);starlight_diag::evlog("liquidNonFin",isLava?1.:0.);geometryClipMs=geometryClipMs+lap();return;}
        {
            const size_t triangleCount=keptTotal;
            const unsigned parts=unsigned(std::min<size_t>(kThreads,std::max<size_t>(1,vertexCount/2048)));
            liquid_parallel_for(parts,1,parts,[&](size_t begin,size_t end){
                for(size_t part=begin;part<end;++part){
                    const size_t low=vertexCount*part/parts,high=vertexCount*(part+1)/parts;
                    for(size_t id=low;id<high;++id)mesh.vertices[id].normal={};
                    for(size_t t=0;t<triangleCount;++t){
                        for(int j=0;j<3;++j){size_t id=size_t(mesh.indices[t*3+j]);if(id>=low&&id<high)mesh.vertices[id].normal=mesh.vertices[id].normal+S.faceNormal[t];}
                    }
                    for(size_t id=low;id<high;++id)mesh.vertices[id].normal=mesh.vertices[id].normal.unit();
                }
            });
        }
        geometryNormalsMs=geometryNormalsMs+lap();
        ++geometryCalls;geometryVertices+=mesh.vertices.size();geometryTriangles+=mesh.indices.size()/3;
        result.push_back({std::move(mesh),
                          isLava ? Vec3{1, 85. / 255, 15. / 255} : Vec3{38. / 255, 151. / 255, 218. / 255},
                          isLava});
    };
    add(water, false);
    add(lava, true);
    return result;
}
} // namespace starlight::editor
