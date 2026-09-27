#include "flow.hpp"
#include "interactions.hpp"
#include <map>
#include <numeric>
#include <tuple>
#include <unordered_map>
namespace starlight::editor {
void FlowObstacleMask::add(const Mesh &mesh, const Character &transform) {
    mesh.validate();
    std::vector<Vec3> points;
    for (const auto &v : mesh.vertices)
        points.push_back(world_point(transform, v.position));
    for (size_t t = 0; t < mesh.indices.size(); t += 3) {
        auto a = points[mesh.indices[t]], b = points[mesh.indices[t + 1]], c = points[mesh.indices[t + 2]];
        double det = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::abs(det) < 1e-12)
            continue;
        auto cell = [](double v) { return std::clamp(int(std::floor((v + 12) / .5)), 0, 47); };
        int x0 = cell(std::min({a.x, b.x, c.x})), x1 = cell(std::max({a.x, b.x, c.x})),
            z0 = cell(std::min({a.z, b.z, c.z})), z1 = cell(std::max({a.z, b.z, c.z}));
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++)
                for (int sub = 0; sub < 4; sub++) {
                    double px = -12 + (x + .25 + (sub % 2) * .5) * .5,
                           pz = -12 + (z + .25 + (sub / 2) * .5) * .5,
                           u = ((b.z - c.z) * (px - c.x) + (c.x - b.x) * (pz - c.z)) / det,
                           v = ((c.z - a.z) * (px - c.x) + (a.x - c.x) * (pz - c.z)) / det, w = 1 - u - v;
                    if (std::min({u, v, w}) < -.00001)
                        continue;
                    double y = u * a.y + v * b.y + w * c.y;
                    int k = (z * 48 + x) * 4 + sub;
                    low[k] = std::min(low[k], y);
                    high[k] = std::max(high[k], y);
                }
    }
}
void FlowObstacleMask::finish() {
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
TerrainFlow::TerrainFlow(const TerrainData &t, const std::vector<LiquidSource> &sources, std::span<const Character> objects)
    : terrain_(t), sources_(sources) {
    terrain_.validate();
    for (const auto &s : sources_)
        remaining_.push_back(s.Volume);
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++)
            bed_[z * N + x] = t.height_at(-12 + (x + .5) * Cell, -12 + (z + .5) * Cell);
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
            auto cell=[](double p){return std::clamp(int(std::floor((p+12)/Cell)),0,N-1);};
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
double TerrainFlow::surface(int i, double amount) const {
    if (!obstacles_ || obstacles_->coverage[i] <= 0)
        return bed_[i] + amount;
    double bottom = std::max(bed_[i], obstacles_->bottom[i]), top = std::max(bottom, obstacles_->top[i]),
           coverage = obstacles_->coverage[i], below = bottom - bed_[i],
           capacity = (top - bottom) * (1 - coverage);
    if (amount < below)
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
void TerrainFlow::step(double dt) {
    if (!std::isfinite(dt) || dt <= 0 || dt > .05)
        throw std::runtime_error("Fluid step must be within (0, .05]");
    for (size_t i = 0; i < sources_.size(); i++) {
        const auto &s = sources_[i];
        double amount = std::min(remaining_[i], s.Volume / 3 * dt);
        remaining_[i] -= amount;
        (s.Lava ? lava : water)[index(s.X, s.Z)] += amount / Area;
        emitted += amount;
    }
    transport(water, dt, 3);
    transport(lava, dt, .25);
    advance_waves(dt);
}
double TerrainFlow::wave_energy()const {double sum=0;for(size_t i=0;i<waves_.size();++i)sum+=waves_[i]*waves_[i]+waveVelocity_[i]*waveVelocity_[i];return sum;}
void TerrainFlow::impulse(double x,double z,double strength){
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
        if(windSpeed>0)for(int z=0;z<N;++z)for(int x=0;x<N;++x){int i=z*N+x;if(water[i]>.002&&water[i]>=lava[i]){forcing[i]=.0025*windSpeed*std::sin(.7*((x*Cell-12)*windX_+(z*Cell-12)*windZ_)/windSpeed-waveTime_*1.7);forceMean+=forcing[i];++windCells;}}
        if(windCells)forceMean/=windCells;
        std::array<double,2304> velocity=waveVelocity_;
        for(int z=0;z<N;++z)for(int x=0;x<N;++x){int i=z*N+x;double depth=water[i]+lava[i];if(depth<.002){waves_[i]=waveVelocity_[i]=0;continue;}double laplace=0;
            for(auto [dx,dz]:{std::pair{-1,0},{1,0},{0,-1},{0,1}}){int xx=x+dx,zz=z+dz;if(xx<0||xx>=N||zz<0||zz>=N)continue;int j=zz*N+xx;if(water[j]+lava[j]>.002)laplace+=waves_[j]-waves_[i];}
            bool molten=lava[i]>water[i];double speed=molten?.5:2.;velocity[i]=(waveVelocity_[i]+(laplace*speed*speed/(Cell*Cell)+(!molten&&windCells?forcing[i]-forceMean:0))*h)*std::exp(-(molten?5.:1.2)*h);
        }
        for(size_t i=0;i<waves_.size();++i)if(water[i]+lava[i]>.002){waveVelocity_[i]=velocity[i];waves_[i]=std::clamp(waves_[i]+velocity[i]*h,-std::min(.3,(water[i]+lava[i])*.5),.3);}
    }
}
void TerrainFlow::interact(std::span<const Character> objects,double dt,double time){
    splashes_.clear();if(!std::isfinite(dt)||dt<=0||dt>.05)return;
    std::unordered_map<std::string,MovingBody> next;
    for(auto object:objects)if(object.Dynamic||!object.Route.empty()){
        object=route_pose(object,time);auto box=object_bounds(object);auto center=(box.low+box.high)*.5;
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
        delta_[t.a] -= move;
        delta_[t.b] += move;
        // Cell-face flux converted to horizontal flow speed. Half weighting
        // averages the incident faces; the same transfer moves liquid volume.
        Vec3 direction{double(t.b%N-t.a%N),0,double(t.b/N-t.a/N)};
        double speed=std::min(10.,move*Cell/(dt*std::max(.002,(depth[t.a]+depth[t.b])*.5)));
        currents[t.a]=currents[t.a]+direction*(speed*.5);currents[t.b]=currents[t.b]+direction*(speed*.5);
    }
    for (int i = 0; i < 2304; i++)
        depth[i] = std::max(0., depth[i] + delta_[i]);
}
std::vector<MeshPart> TerrainFlow::geometry() const {
    std::vector<MeshPart> result;
    auto add = [&](const auto &depths, bool isLava) {
        Mesh mesh;
        constexpr int R = N * 2;
        std::vector<Vec3> points((R + 1) * (R + 1));
        std::vector<double> values(points.size());
        std::array<double,2304> heights;for(int i=0;i<2304;++i)heights[i]=surface(i,depths[i])+waves_[i];
        auto sample = [&](const auto& field,double u, double v) {
            u = std::clamp(u, 0., double(N - 1));
            v = std::clamp(v, 0., double(N - 1));
            int x = std::min(int(u), N - 2), z = std::min(int(v), N - 2);
            u -= x;
            v -= z;
            return field[z * N + x] * (1 - u) * (1 - v) + field[z * N + x + 1] * u * (1 - v) +
                   field[(z + 1) * N + x] * (1 - u) * v + field[(z + 1) * N + x + 1] * u * v;
        };
        for (int z = 0; z <= R; z++)
            for (int x = 0; x <= R; x++) {
                int k = z * (R + 1) + x;
                double px = -12 + x * .25, pz = -12 + z * .25;
                values[k] = sample(depths,x * .5 - .5, z * .5 - .5);
                points[k] = {px, sample(heights,x * .5 - .5,z * .5 - .5) + .009, pz};
            }
        // The water surface is a structured grid. Share by grid vertex/edge,
        // avoiding per-triangle allocations and a coordinate-tree lookup.
        std::vector<int> gridVertices(points.size(),-1);
        std::unordered_map<uint64_t,int> shoreVertices;
        mesh.vertices.reserve(points.size());mesh.indices.reserve(R*R*6);
        auto vertex=[&](int point){auto& id=gridVertices[point];if(id<0){id=int(mesh.vertices.size());mesh.vertices.push_back({points[point]});}return id;};
        auto shoreline=[&](int u,int v){
            uint64_t key=(uint64_t(std::min(u,v))<<32)|uint32_t(std::max(u,v));
            auto [it,inserted]=shoreVertices.emplace(key,int(mesh.vertices.size()));
            if(inserted){double t=(.002-values[u])/(values[v]-values[u]);mesh.vertices.push_back({points[u]+(points[v]-points[u])*t});}
            return it->second;
        };
        auto triangle=[&](int a,int b,int c){
            int source[]={a,b,c},clipped[6],count=0;
            for(int j=0;j<3;++j){int u=source[j],v=source[(j+1)%3];bool inside=values[u]>.002,next=values[v]>.002;
                if(inside)clipped[count++]=vertex(u);
                if(inside!=next)clipped[count++]=shoreline(u,v);
            }
            for(int j=1;j+1<count;++j)mesh.indices.insert(mesh.indices.end(),{clipped[0],clipped[j],clipped[j+1]});
        };
        for (int z = 0; z < R; z++)
            for (int x = 0; x < R; x++) {
                int k = z * (R + 1) + x;
                triangle(k, k + R + 1, k + 1);
                triangle(k + 1, k + R + 1, k + R + 2);
            }
        if (mesh.indices.empty())
            return;
        for (auto &v : mesh.vertices) {
            v.motionIdentity=vertex_motion_identity(isLava?6:5,uint64_t(int64_t(std::llround(v.position.x*1e6))),uint64_t(int64_t(std::llround(v.position.z*1e6))));
            v.kind = isLava ? 8.f : 7.f;
            v.color = {std::floor(std::clamp(
                           (v.position.y - terrain_.height_at(v.position.x, v.position.z)) * 127, 0., 255.)) /
                           255,
                       1, 1};
        }
        mesh.normals();
        result.push_back({std::move(mesh),
                          isLava ? Vec3{1, 85. / 255, 15. / 255} : Vec3{38. / 255, 151. / 255, 218. / 255},
                          isLava});
    };
    add(water, false);
    add(lava, true);
    return result;
}
} // namespace starlight::editor
