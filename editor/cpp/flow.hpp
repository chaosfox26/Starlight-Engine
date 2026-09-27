#pragma once
#include "geometry.hpp"
#include <span>
#include <unordered_map>
namespace starlight::editor {
struct FlowObstacleMask {
    // Keep dense masks off the 1 MiB Windows thread stack. Value copies still own their storage.
    std::vector<double> bottom = std::vector<double>(2304), top = std::vector<double>(2304),
                        coverage = std::vector<double>(2304);
    std::vector<double> low = std::vector<double>(9216, INFINITY),
                        high = std::vector<double>(9216, -INFINITY);
    void add(const Mesh &, const Character &);
    void finish();
};
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
class TerrainFlow {
    TerrainData terrain_;
    std::array<double,2304> waves_{},waveVelocity_{};
    double windX_=0,windZ_=0,waveTime_=0;
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
    void set_obstacles(FlowObstacleMask mask) { obstacles_ = std::move(mask); }
    double surface(int, double) const;
    std::pair<double, double> liquid_at(double, double) const;
    LiquidSnapshot snapshot()const;
    Vec3 velocity_at(double,double)const;
    double volume() const;
    void wind(double x,double z){if(!std::isfinite(x)||!std::isfinite(z)||std::hypot(x,z)>100)throw std::runtime_error("Invalid liquid wind");windX_=x;windZ_=z;}
    void impulse(double x,double z,double strength);
    void interact(std::span<const Character>,double dt,double time);
    const std::vector<LiquidSplash>& splashes()const{return splashes_;}
    double wave_energy()const;
    void step(double);
    std::vector<MeshPart> geometry() const;
};
// Atmospheric density occupies air above both terrain and the current liquid surface.
inline double atmosphere_surface(const TerrainData* terrain,const TerrainFlow* flow,double x,double z){
    double height=terrain?terrain->height_at(x,z):0.;
    if(flow){auto liquid=flow->liquid_at(x,z);if(liquid.second>0)height=std::max(height,liquid.first);}
    return height;
}
} // namespace starlight::editor
