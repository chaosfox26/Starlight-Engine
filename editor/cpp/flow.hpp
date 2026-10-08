#pragma once
#include <memory>
#include "geometry.hpp"
#include <span>
#include <unordered_map>
namespace starlight::editor {
struct FlowObstacleMask {
    static constexpr int GeometryN=384;
    struct ColumnTriangle { Vec3 a,b,c; unsigned object=0; Vec3 boundsLow{},boundsHigh{}; bool dead=false; };
    // Exact immutable object surfaces for render clearance. The existing
    // subcolumns remain the physics displacement/transport contract.
    std::vector<ColumnTriangle> triangles;
    // Vertical extent of every triangle registered in a cell (valid when geometryRevision != 0): lets a query skip a
    // cell whose triangles all lie above or below it, which the per-triangle box test would reject one by one.
    std::vector<double> cellLowY,cellHighY;
    // Each triangle is listed in the cells its x/z projection (grown by the query tolerances) meets, not in its whole bounding box.
    std::vector<std::vector<unsigned>> triangleCells=std::vector<std::vector<unsigned>>(GeometryN*GeometryN);
    unsigned objectCount=0;
    double clearance(const Vec3&) const;
    // Exact clearance when it is below `cap`; otherwise any value >= cap. Every liquid-surface decision compares
    // clearance with a threshold below the cap, so this changes no decision while skipping most column searches.
    double clearance_capped(const Vec3&,double cap) const;
    // Performance hint only: starts loading what clearance(p) will read.
    void prefetch_clearance(const Vec3&) const;
    bool clear_triangle(Vec3,Vec3,Vec3) const;
    bool clear_triangle_indexed(Vec3,Vec3,Vec3) const; // cell-range and de-duplicated search used by clear_triangle
    bool clear_triangle_reference(Vec3,Vec3,Vec3) const; // original exhaustive search, used when STARLIGHT_FLOW_VERIFY=1
    // Keep dense masks off the 1 MiB Windows thread stack. Value copies still own their storage.
    std::vector<double> bottom = std::vector<double>(2304), top = std::vector<double>(2304),
                        coverage = std::vector<double>(2304);
    std::vector<double> low = std::vector<double>(9216, INFINITY),
                        high = std::vector<double>(9216, -INFINITY);
    uint64_t geometryRevision=0; // composition revision seen by consumers' caches (changes whenever the mobile layer changes)
    uint64_t layerRevision=0;    // revision of this layer's own geometry (keys the clearance memo)
    // Objects that move on their own live in a separately finished small layer, so replacing them each frame does not
    // re-register the immutable scenery. All queries below combine both layers.
    // Layers on top of this (heavy, immutable) one: light immutable scenery (replaced when a rock is removed or added) and the movers.
    std::shared_ptr<const FlowObstacleMask> lightStatic;
    std::shared_ptr<const FlowObstacleMask> mobile;
    // Installs the two upper layers (finished layers, or null) and merges the column extents with this layer's own (heavyLow/heavyHigh).
    void set_layers(std::shared_ptr<const FlowObstacleMask> light,std::shared_ptr<const FlowObstacleMask> movers,const std::vector<double>& heavyLow,const std::vector<double>& heavyHigh);
    void recompute_columns();
    double clearance_layer(const Vec3&) const;
    double clearance_capped_layer(const Vec3&,double cap) const;
    void prefetch_clearance_layer(const Vec3&) const;
    bool clear_triangle_layer(Vec3,Vec3,Vec3) const;
    bool clear_triangle_reference_layer(Vec3,Vec3,Vec3) const;
    bool region_empty_layer(double xLow,double xHigh,double zLow,double zHigh) const;
    bool region_clear_layer(double xLow,double xHigh,double zLow,double zHigh,double yLow,double yHigh,double cap) const;
    // Inclusive 2-D prefix sum of the cells that hold at least one obstacle triangle ((GeometryN+1)^2 entries, built by
    // finish(), empty while the mask is unfinished). A query whose padded cell rectangle sums to zero cannot touch any solid.
    std::vector<int> occupancy;
    // Compact, cache-friendly copy of every cell registration (same order as triangleCells), built by finish(): conservative
    // extents let a column or triangle query reject most registrations without touching the 128-byte triangle record.
    struct CellEntry { float yLow,yHigh; }; // outward-rounded vertical extent
    std::vector<CellEntry> cellEntries;
    // One 64-bit mask per registration (same order as cellEntries): bit (z*8+x) is set when the triangle's padded x/z box
    // can contain a point of that 1/8th-cell block. A column query tests one bit instead of four box comparisons.
    struct MaskEntry { uint64_t mask; uint32_t id,reserved; };
    std::vector<MaskEntry> cellMasks;
    std::vector<uint32_t> cellStart; // GeometryN*GeometryN+1 offsets into cellEntries
    // True only when no obstacle triangle is registered in or next to any cell overlapping the box (conservative: false
    // for an unfinished mask or a box that merely touches an occupied cell).
    bool region_empty(double xLow,double xHigh,double zLow,double zHigh) const;
    // True when every clearance_capped(p,cap) / clear_triangle probe of a triangle whose box is given (probes lie inside
    // the box) returns "no solid within cap": the padded cells are empty or lie wholly above/below the probes' heights.
    bool region_clear(double xLow,double xHigh,double zLow,double zHigh,double yLow,double yHigh,double cap) const;
    void add(const Mesh &, const Character &);
    void finish();
    // Takes whole objects (numbered in add() order) out of a finished layer without rebuilding it: their triangles leave every cell list and
    // block mask, the column extents are recomputed where they stood, and the revisions change so memoised answers are dropped. The
    // per-cell vertical extents and the occupancy table are left as they were, which can only make a query take its slower exact path,
    // until more than half of the triangle records are dead: then the survivors are renumbered and the layer is finished again.
    // That moves triangle indices, so it is for layers nobody holds a Mark of (not the heavy layer that truncate() works on).
    void remove_objects(const std::vector<unsigned>& objects);
    void rasterize_columns(const ColumnTriangle& triangle,int cellX0,int cellX1,int cellZ0,int cellZ1);
    // A point in the accumulated geometry. truncate() removes everything added after it (the mask must be re-finished), so
    // immutable scenery stays registered while objects that move are replaced.
    struct Mark { size_t triangleCount=0; unsigned objectCount=0; std::vector<double> low,high; };
    Mark mark() const { return {triangles.size(),objectCount,low,high}; }
    void truncate(const Mark&);
};
// A layer that may be edited in place: the one given when nothing else holds it, otherwise a private copy, so an edit can
// never reach another holder of the pointer (a copy of a TerrainFlow shares its layers).
inline std::shared_ptr<const FlowObstacleMask> editable_obstacle_layer(const std::shared_ptr<const FlowObstacleMask>& layer){
    if(layer.use_count()==1)return layer;
    return std::make_shared<FlowObstacleMask>(*layer); // created non-const, so the caller may cast the constness away to edit it
}
// Sample the same cell-centred surface continuously in both physics paths.
// Dry cells stay dry; neighbouring liquid types are not silently mixed.
template<class Sample> inline std::pair<double,double> sample_liquid(double x,double z,Sample sample){
    if(!std::isfinite(x)||!std::isfinite(z)||x< -12||x>=12||z< -12||z>=12)return {};
    auto center=sample(int((z+12)*2)*48+int((x+12)*2));if(center.second<=0)return center;
    double u=std::clamp((x+12)*2-.5,0.,47.),v=std::clamp((z+12)*2-.5,0.,47.);
    int ix=std::min(int(u),46),iz=std::min(int(v),46);u-=ix;v-=iz;double height=0,weight=0;
    for(int dz=0;dz<2;++dz)for(int dx=0;dx<2;++dx){auto value=sample((iz+dz)*48+ix+dx);double w=(dx?u:1-u)*(dz?v:1-v);if(value.second==center.second&&w>0){height+=value.first*w;weight+=w;}}
    return {weight>0?height/weight:center.first,center.second};
}
// Immutable cross-thread sample of the actual simulated liquid surface.
struct LiquidSnapshot {
    std::array<std::pair<double,double>,2304> cells{};
    std::vector<Vec3> currents=std::vector<Vec3>(2304);
    Vec3 velocity_at(double x,double z)const{
        if(at(x,z).second<=0)return {};
        auto component=[&](bool horizontal){return sample_liquid(x,z,[&](int i){return std::pair{horizontal?currents[i].x:currents[i].z,cells[i].second};}).first;};
        return {component(true),0,component(false)};
    }
    std::pair<double,double> at(double x,double z)const{
        return sample_liquid(x,z,[&](int i){return cells[i];});
    }
};
struct LiquidSplash {Vec3 position,velocity;double strength=0;bool lava=false;};
// Cumulative time inside TerrainFlow::geometry() by stage (milliseconds), for performance diagnosis.
struct FlowGeometryProfile { double setupMs=0,gridMs=0,refineMs=0,clipMs=0,vertexMs=0,obstacleMs=0,normalsMs=0; uint64_t calls=0,vertices=0,triangles=0; double clipTopologyMs=0,clipCrossingsMs=0,clipClearanceMs=0; };
FlowGeometryProfile flow_geometry_profile();
struct FlowGeometryScratch;
// Reusable per-flow buffers of TerrainFlow::geometry(); a copy of a flow starts with empty scratch.
struct FlowGeometryScratchHolder {
    FlowGeometryScratch* scratch=nullptr;
    FlowGeometryScratchHolder()=default;
    FlowGeometryScratchHolder(const FlowGeometryScratchHolder&){}
    FlowGeometryScratchHolder& operator=(const FlowGeometryScratchHolder&){return *this;}
    ~FlowGeometryScratchHolder();
};
struct FlowAsyncGeometry;
// Optional background computation of TerrainFlow::geometry() for the present state (see geometry_begin_async). A copy of
// a flow starts without one.
struct FlowAsyncGeometryHolder {
    FlowAsyncGeometry* state=nullptr;
    FlowAsyncGeometryHolder()=default;
    FlowAsyncGeometryHolder(const FlowAsyncGeometryHolder&){}
    FlowAsyncGeometryHolder& operator=(const FlowAsyncGeometryHolder&){return *this;}
    ~FlowAsyncGeometryHolder();
};
class TerrainFlow {
    TerrainData terrain_;
    std::array<double,2304> waves_{},waveVelocity_{};
    double windX_=0,windZ_=0,waveTime_=0,windWaveStrength_=.35;
    std::vector<double> lavaHeat_=std::vector<double>(2304,1);
    double lavaInitialHeat_=1,lavaCoolingRate_=.012,lavaCrustDepth_=.08,lavaLightStrength_=1;
    std::vector<Vec3> waterCurrent_=std::vector<Vec3>(2304),lavaCurrent_=std::vector<Vec3>(2304);
    struct MovingBody {Vec3 center;double bottom=0,surface=0;};
    std::unordered_map<std::string,MovingBody> movingBodies_;
    std::vector<LiquidSplash> splashes_;
    void advance_waves(double);
    std::vector<LiquidSource> sources_;
    std::vector<double> remaining_;
    std::array<double, 2304> bed_{}, outflow_{}, delta_{};
    struct Transfer {
        int a, b;
        double amount;
    };
    std::vector<Transfer> transfers_;
    std::optional<FlowObstacleMask> obstacles_;
    bool initialObstacleSettlement_=true;
    void settle_initial_liquid(std::array<double,2304>&);
    void transport(std::array<double, 2304> &, double, double);

  public:
    static constexpr int N = 48;
    static constexpr double Cell = .5, Area = .25;
    std::array<double, 2304> water{}, lava{};
    double emitted = 0;
    TerrainFlow(const TerrainData &, const std::vector<LiquidSource> &, std::span<const Character> = {});
    static int index(double x, double z) {
        return std::clamp(int((z + 12) / Cell), 0, N - 1) * N + std::clamp(int((x + 12) / Cell), 0, N - 1);
    }
    void set_obstacles(FlowObstacleMask mask);
    bool has_obstacles()const{return obstacles_.has_value();}
    template<class Edit> void amend_obstacles(Edit&& edit){settle_async();if(obstacles_)edit(*obstacles_);}
    // Highest liquid surface (as drawn: displaced by submerged solids, plus the wave height; never below bed plus depth) in each cell (48x48), widened by `margin` cells; -1e9 where no liquid is near. An object
    // that is horizontally outside these cells, or entirely above their surface, cannot change what the liquid does.
    std::vector<double> liquid_top_cells(int margin)const{
        std::vector<double> top(N*N,-1e9);
        for(int z=0;z<N;++z)for(int x=0;x<N;++x){
            const double depth=std::max(water[z*N+x],lava[z*N+x]);if(depth<=1e-5)continue;
            const double surfaceHeight=std::max(bed_[z*N+x]+depth,surface(z*N+x,depth)+waves_[z*N+x]); // the drawn surface (raised over submerged solids, moved by the waves), never below the still level, so a wave trough does not take a cell out of the filter
            for(int dz=-margin;dz<=margin;++dz)for(int dx=-margin;dx<=margin;++dx){int xx=x+dx,zz=z+dz;if(xx>=0&&xx<N&&zz>=0&&zz<N)top[zz*N+xx]=std::max(top[zz*N+xx],surfaceHeight);}
        }
        return top;
    }
    // Cells (48x48) holding liquid, widened by `margin` cells: only objects touching these can change what the liquid does.
    std::vector<uint8_t> wet_cells(int margin)const{
        std::vector<uint8_t> wet(N*N,0);
        for(int z=0;z<N;++z)for(int x=0;x<N;++x)if(water[z*N+x]>1e-5||lava[z*N+x]>1e-5)
            for(int dz=-margin;dz<=margin;++dz)for(int dx=-margin;dx<=margin;++dx){int xx=x+dx,zz=z+dz;if(xx>=0&&xx<N&&zz>=0&&zz<N)wet[zz*N+xx]=1;}
        return wet;
    }
    bool terrain(const TerrainData&); // Preserve water/lava quantities, heat, currents and source progress.
    double surface(int, double) const;
    std::pair<double, double> liquid_at(double, double) const;
    LiquidSnapshot snapshot()const;
    Vec3 velocity_at(double,double)const;
    double volume() const;
    void wind(double x,double z,double strength=.35){settle_async();if(!std::isfinite(x)||!std::isfinite(z)||std::hypot(x,z)>100||!std::isfinite(strength)||strength<0||strength>1)throw std::runtime_error("Invalid liquid wind");windX_=x;windZ_=z;windWaveStrength_=strength;}
    void lava_settings(double heat,double cooling,double crust,double light,bool reset=false);
    double lava_heat_at(double x,double z)const;
    std::vector<std::array<float,12>> lava_lights()const;
    void impulse(double x,double z,double strength);
    void interact(std::span<const Character>,double dt,double time);
    const std::vector<LiquidSplash>& splashes()const{return splashes_;}
    double wave_energy()const;
    void step(double);
    std::vector<MeshPart> geometry() const;
    // Same result as geometry(), computed by the pipeline stages below.
    std::vector<MeshPart> geometry_checked() const;
    // One-frame-ahead pipelining: call after the last change of a step. A background thread computes geometry() of this
    // state while the caller keeps working. Every method that changes the flow first waits for it (settle_async), so the
    // state can never change underneath it. geometry_for_render() returns the finished result of the previous begin, or
    // (nothing prepared) the geometry of the present state.
    void geometry_begin_async() const;
    std::vector<MeshPart> geometry_for_render() const;
    std::vector<MeshPart> geometry_unverified() const; // fast exact path (or the reference when exactClearance_ is set)
    std::vector<MeshPart> geometry_reference() const;  // original generic algorithm with exact clearance and hash-container refinement
    mutable bool exactClearance_=false; // STARLIGHT_FLOW_VERIFY=1: rebuild with exact obstacle clearance and compare
  private:
    uint64_t terrainVersion_=1; // bumped whenever terrain_'s solid shape (heights/volumes) changes
    mutable FlowGeometryScratchHolder geometryScratch_;
    mutable FlowAsyncGeometryHolder asyncGeometry_;
    void settle_async_impl() const;
    void settle_async() const { if(asyncGeometry_.state)settle_async_impl(); }
};
// Atmospheric density occupies air above both terrain and the current liquid surface.
inline double atmosphere_surface(const TerrainData* terrain,const TerrainFlow* flow,double x,double z){
    double height=terrain?terrain->surface_at(x,z):0.;
    if(flow){auto liquid=flow->liquid_at(x,z);if(liquid.second>0)height=std::max(height,liquid.first);}
    return height;
}
} // namespace starlight::editor
