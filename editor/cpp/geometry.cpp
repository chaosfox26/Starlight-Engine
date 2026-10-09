#include "geometry.hpp"
#include "terrain_volume.hpp"
#include "rock_geometry.hpp"
#include "mesh_reduce.hpp"
#include "fire_geometry.hpp"
#include "crystal_geometry.hpp"
#include "grass_geometry.hpp"
#include "lightning_geometry.hpp"
#include "../../deep/engine/chaos_lightning.h"
#include "color.hpp"
#include <map>
#include <numbers>
#include <tuple>
namespace starlight::editor {
namespace {
constexpr double pi = std::numbers::pi, tau = 2 * pi;
// Seeded System.Random's subtractive sequence preserves authored nature meshes.
class SeededRandom {
    std::array<int, 56> values{};
    int a = 0, b = 21;

  public:
    explicit SeededRandom(int seed) {
        int mj = 161803398 - std::abs(seed), mk = 1;
        values[55] = mj;
        for (int i = 1; i < 55; i++) {
            int ii = 21 * i % 55;
            values[ii] = mk;
            mk = mj - mk;
            if (mk < 0)
                mk += 2147483647;
            mj = values[ii];
        }
        for (int k = 0; k < 4; k++)
            for (int i = 1; i < 56; i++) {
                values[i] -= values[1 + (i + 30) % 55];
                if (values[i] < 0)
                    values[i] += 2147483647;
            }
    }
    double next() {
        if (++a >= 56)
            a = 1;
        if (++b >= 56)
            b = 1;
        int n = values[a] - values[b];
        if (n == 2147483647)
            n--;
        if (n < 0)
            n += 2147483647;
        values[a] = n;
        return n * (1.0 / 2147483647);
    }
};
void face(Mesh &m, const std::vector<Vec3> &points) {
    int start = int(m.vertices.size());
    for (auto p : points)
        m.vertices.push_back({p});
    for (int i = 1; i + 1 < int(points.size()); i++)
        m.indices.insert(m.indices.end(), {start, start + i, start + i + 1});
}
void uniform(Mesh &m, int kind) {
    for (auto &v : m.vertices)
        v.kind = float(kind);
    m.normals();
}
Mesh nature(std::string_view kind) {
    if(kind=="grass")return grass_surface();
    if(kind=="rock" || kind=="boulder" || kind=="pebbles" || kind=="chaos-rock"){
        static const std::array<Mesh,4> stones=[]{
            std::array<Mesh,4> result;
            const double detail=mesh_detail(); // 1 = full meshes; STARLIGHT_MESH_DETAIL lowers the share of triangles kept (see mesh_reduce.hpp)
            result[0]=reduce_mesh_smooth(rock_surface(.62,4,8241,RockForm::Outcrop),detail);result[1]=reduce_mesh_smooth(rock_surface(1.05,4,9173,RockForm::Boulder),detail);result[3]=reduce_mesh_smooth(rock_surface(.85,4,15317,RockForm::Crystal),detail);
            SeededRandom random(8241);
            for(int i=0;i<9;++i){double scale=.15+random.next()*.2,x=(random.next()-.5)*1.7,z=(random.next()-.5)*1.7;auto pebble=reduce_mesh_smooth(rock_surface(.62,3,8241+i*7919,RockForm::Pebble),detail);int base=int(result[2].vertices.size());for(auto v:pebble.vertices){v.position=v.position*scale+Vec3{x,0,z};result[2].vertices.push_back(v);}for(int index:pebble.indices)result[2].indices.push_back(base+index);}
            for(int i=0;i<4;++i)for(auto& vertex:result[i].vertices)vertex.kind=i==3?10.f:6.f;
            return result;
        }();
        return stones[kind=="rock"?0:kind=="boulder"?1:kind=="pebbles"?2:3];
    }
    Mesh m;
    SeededRandom random(8241);
    if (kind == "grass") {
        for (int blade = 0; blade < 100; blade++) {
            double a = random.next() * tau, r = std::sqrt(random.next()) * .48, x = std::cos(a) * r,
                   z = std::sin(a) * r, h = .18 + random.next() * .42, lean = (random.next() - .5) * .22,
                   turn = random.next() * tau;
            int k = int(m.vertices.size());
            for (int j = 0; j <= 4; j++) {
                double t = j / 4., w = .026 * (1 - t) + .001;
                m.vertices.push_back(
                    {{x + lean * t * t - std::cos(turn) * w, h * t, z + std::sin(turn) * w}});
                m.vertices.push_back(
                    {{x + lean * t * t + std::cos(turn) * w, h * t, z - std::sin(turn) * w}});
            }
            for (int j = 0; j < 4; j++) {
                int v = k + j * 2;
                m.indices.insert(m.indices.end(), {v, v + 2, v + 1, v + 1, v + 2, v + 3});
            }
        }
    } else {
        int count = kind == "pebbles" ? 9 : 1;
        for (int stone = 0; stone < count; stone++) {
            double scale = count == 1 ? 1 : .15 + random.next() * .2,
                   ox = count == 1 ? 0 : (random.next() - .5) * 1.7,
                   oz = count == 1 ? 0 : (random.next() - .5) * 1.7;
            int first = int(m.vertices.size()), rows = kind == "light-shard" ? 6 : 20,
                cols = kind == "light-shard" ? 7 : 32;
            for (int y = 0; y <= rows; y++)
                for (int x = 0; x <= cols; x++) {
                    double phi = pi * y / rows, theta = tau * x / cols,
                           warp = 1 + std::sin(phi) * (.12 * std::sin(theta * 3 + std::cos(phi) * 4) +
                                                       .065 * std::cos(theta * 5 - phi * 6)),
                           tall = kind == "light-shard" ? 2.4
                                  : kind == "boulder"   ? 1.05
                                                        : .62;
                    m.vertices.push_back({{ox + std::sin(phi) * std::cos(theta) * .72 * warp * scale,
                                           (std::cos(phi) + 1) * tall * .5 * warp * scale,
                                           oz + std::sin(phi) * std::sin(theta) * .59 * warp * scale}});
                }
            for (int y = 0; y < rows; y++)
                for (int x = 0; x < cols; x++) {
                    int k = first + y * (cols + 1) + x;
                    m.indices.insert(m.indices.end(),
                                     {k, k + 1, k + cols + 1, k + 1, k + cols + 2, k + cols + 1});
                }
        }
        Mesh welded;
        std::map<std::tuple<long long, long long, long long>, int> map;
        std::vector<int> remap;
        for (const auto &v : m.vertices) {
            auto p = v.position;
            auto [it, added] = map.emplace(std::tuple{(long long)std::nearbyint(p.x * 1e6),
                                                      (long long)std::nearbyint(p.y * 1e6),
                                                      (long long)std::nearbyint(p.z * 1e6)},
                                           int(welded.vertices.size()));
            if (added)
                welded.vertices.push_back(v);
            remap.push_back(it->second);
        }
        for (size_t i = 0; i < m.indices.size(); i += 3) {
            int a = remap[m.indices[i]], b = remap[m.indices[i + 1]], c = remap[m.indices[i + 2]];
            if (a != b && a != c && b != c)
                welded.indices.insert(welded.indices.end(), {a, b, c});
        }
        m = std::move(welded);
    }
    uniform(m, kind == "grass" ? 1 : kind == "chaos-rock" ? 10 : kind == "light-shard" ? 11 : 6);
    return m;
}
} // namespace
Vec3 rgb(std::string_view s) {
    auto c = color_rgb(std::string(s));
    return {c[0], c[1], c[2]};
}
inline float foliage_leaf_pack(double seed,const FoliageMaterial& f){
    auto q=[](double x){return uint32_t(std::round(std::clamp(x,0.,1.)*63));};
    return float(q(seed)|(q(f.LeafTransmission)<<6)|(q(f.LeafVeins)<<12)|(q((f.LeafRoughness-.4)/.45)<<18));
}
inline void foliage_surface_payload(Mesh& mesh,const Character& c){
    if(c.Surface!=12&&c.Surface!=13)return;
    const auto f=c.Foliage.value_or(FoliageMaterial{});f.validate();
    for(auto& v:mesh.vertices){
        if(c.Surface==12)v.color={f.BarkScale,f.BarkRelief,f.BarkRoughness};
        else{if(!v.materialCoordinate)throw std::runtime_error("Leaf mesh lost material coordinates");auto p=*v.materialCoordinate;v.color={p.x,p.y,foliage_leaf_pack(p.z,f)};}
    }
}
std::vector<MeshPart> shape_geometry(const Character &c) {
    c.validate();
    if (!c.Shape && !c.Sculpt)
        throw std::runtime_error("Object is an imported asset");
    auto kind = c.Shape.value_or("");
    auto tint = rgb(c.Color);
    if (kind == "chaos-lightning") {
        uint32_t seed=8241;auto random=[&](){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return double(seed&65535)/32767.5-1;};
        auto path=lightning_path({0,3,0},{c.TargetX,c.TargetY,c.TargetZ},c.BranchSpread*.2,6,random);
        std::vector<MeshPart> parts{{lightning_channel(path,.035,false),{1,1,1},true}};
        if(c.BranchSpread>0)for(int i=1;i<=3;++i){auto root=path[(path.size()-1)*i/4];auto tip=root+Vec3{random(),-std::abs(random()),random()}*c.BranchSpread;parts.push_back({lightning_channel(lightning_path(root,tip,c.BranchSpread*.12,4,random),.017,true),{1,1,1},true});}
        return parts;
    }
    if (c.Sculpt) {
        auto m = mesh_from(*c.Sculpt);
        uniform(m, c.Surface);
        foliage_surface_payload(m,c);
        return {{std::move(m), tint, c.Glow > 0}};
    }
    if(kind=="light-shard")return {{crystal_surface(),tint}};
    if (kind == "rock" || kind == "boulder" || kind == "pebbles" || kind == "grass" || kind == "chaos-rock" ||
        kind == "light-shard" || kind == "craxis-smoke")
        return {{nature(kind), tint}};
    std::vector<MeshPart> parts;
    auto cone = [&](double radius, double height, double bottom, Vec3 color) {
        Mesh m;
        for (int i = 0; i < 12; i++) {
            double a = i * pi / 6, b = (i + 1) * pi / 6;
            face(m, {{std::cos(a) * radius, bottom, std::sin(a) * radius},
                     {std::cos(b) * radius, bottom, std::sin(b) * radius},
                     {0, bottom + height, 0}});
        }
        m.normals();
        parts.push_back({std::move(m), color, true});
    };
    if (kind == "fire") {
        return {fire_volume(tint)};
    }
    if (kind == "light") {
        cone(.18, .3, .3, tint);
        cone(.18, -.3, .3, tint);
        return parts;
    }
    Mesh m;
    if (kind == "lava") {
        m.vertices.push_back({{0, .035, 0}});
        for (int i = 0; i < 33; i++)
            m.vertices.push_back({{std::cos(i * pi / 16), .035, std::sin(i * pi / 16)}});
        for (int i = 1; i <= 32; i++)
            m.indices.insert(m.indices.end(), {0, i + 1, i});
        uniform(m, 8);
        return {{std::move(m), tint, true}};
    }
    if (kind == "cylinder" || kind == "ring") {
        constexpr int segments = 48;
        const double inner = kind == "ring" ? c.RingInner : 0;
        std::vector<Vec3> outerBottom, outerTop, innerBottom, innerTop;
        for (int i = 0; i < segments; i++) {
            const double a = i * tau / segments, x = std::cos(a), z = std::sin(a);
            outerBottom.push_back({x, 0, z});
            outerTop.push_back({x, 1, z});
            innerBottom.push_back({x * inner, 0, z * inner});
            innerTop.push_back({x * inner, 1, z * inner});
        }
        Mesh solid;
        if (inner <= 0) {
            auto reverse = outerBottom;
            std::reverse(reverse.begin(), reverse.end());
            face(solid, reverse);
            face(solid, outerTop);
        }
        for (int i = 0; i < segments; i++) {
            const int n = (i + 1) % segments;
            face(solid, {outerBottom[i], outerBottom[n], outerTop[n], outerTop[i]});
            if (inner > 0) {
                face(solid, {outerTop[i], outerTop[n], innerTop[n], innerTop[i]});
                face(solid, {outerBottom[i], innerBottom[i], innerBottom[n], outerBottom[n]});
                face(solid, {innerBottom[i], innerTop[i], innerTop[n], innerBottom[n]});
            }
        }
        solid.normals();
        for (auto &vertex : solid.vertices) vertex.kind = float(c.Surface);
        foliage_surface_payload(solid, c);
        return {{std::move(solid), tint, c.Glow > 0}};
    }
    if (kind == "octagon") {
        std::vector<Vec3> bottom, top;
        for (int i = 0; i < 8; i++) {
            bottom.push_back({std::cos(i * pi / 4) * .65, 0, std::sin(i * pi / 4) * .65});
            top.push_back({bottom.back().x, 1, bottom.back().z});
        }
        auto reverse = bottom;
        std::reverse(reverse.begin(), reverse.end());
        face(m, reverse);
        face(m, top);
        for (int i = 0; i < 8; i++)
            face(m, {bottom[i], bottom[(i + 1) % 8], top[(i + 1) % 8], top[i]});
    } else {
        double x = kind == "rectangle" ? 1 : .5, t = kind == "trapezoid" ? .25 : x;
        Vec3 p[] = {{-x, 0, -.5}, {x, 0, -.5}, {x, 0, .5}, {-x, 0, .5},
                    {-t, 1, -.5}, {t, 1, -.5}, {t, 1, .5}, {-t, 1, .5}};
        for (auto indices : std::vector<std::array<int, 4>>{
                 {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}})
            face(m, {p[indices[0]], p[indices[1]], p[indices[2]], p[indices[3]]});
    }
    m.normals();
    for(auto& vertex:m.vertices)vertex.kind=float(c.Surface);
    foliage_surface_payload(m,c);
    return {{std::move(m), tint, c.Glow > 0}};
}
std::vector<MeshPart> terrain_geometry(const TerrainData &t) {
    t.validate();
    if(!t.Volumes.empty() && !t.WaterEnabled)return {{terrain_volume_geometry(t)}};
    Mesh ground, water;
    constexpr int n = 193;
    Vec3 palette[] = {rgb("#477345"), rgb("#80634A"), rgb("#C8B581"), rgb("#777D86"), rgb("#C4D2DC")};
    for (int z = 0; z < n; z++)
        for (int x = 0; x < n; x++) {
            double px = x * .125 - 12, pz = z * .125 - 12, u = x * .5, v = z * .5;
            int ix = std::min(int(u), 95), iz = std::min(int(v), 95);
            u -= ix;
            v -= iz;
            int ids[] = {iz * 97 + ix, iz * 97 + ix + 1, (iz + 1) * 97 + ix, (iz + 1) * 97 + ix + 1};
            double w[] = {(1 - u) * (1 - v), u * (1 - v), (1 - u) * v, u * v};
            Vec3 color;
            double kind = 0;
            for (int q = 0; q < 4; q++) {
                color = color + palette[t.Paint[ids[q]]] * w[q];
                kind += (t.Paint[ids[q]] + 1) * w[q];
            }
            color = {std::floor(color.x * 255) / 255, std::floor(color.y * 255) / 255,
                     std::floor(color.z * 255) / 255};
            ground.vertices.push_back({{px, t.height_at(px, pz), pz}, {}, color, float(kind)});
        }
    auto triangle = [&](int a, int b, int c) {
        ground.indices.insert(ground.indices.end(), {a, b, c});
        if (!t.WaterEnabled)
            return;
        Vec3 input[] = {ground.vertices[a].position, ground.vertices[b].position,
                        ground.vertices[c].position};
        std::vector<Vec3> clipped;
        for (int i = 0; i < 3; i++) {
            auto p = input[i], q = input[(i + 1) % 3];
            bool inside = p.y < t.WaterLevel, next = q.y < t.WaterLevel;
            if (inside)
                clipped.push_back({p.x, t.WaterLevel, p.z});
            if (inside != next) {
                double f = (t.WaterLevel - p.y) / (q.y - p.y);
                clipped.push_back({p.x + (q.x - p.x) * f, t.WaterLevel, p.z + (q.z - p.z) * f});
            }
        }
        face(water, clipped);
    };
    for (int z = 0; z < n - 1; z++)
        for (int x = 0; x < n - 1; x++) {
            int k = z * n + x;
            triangle(k, k + n, k + 1);
            triangle(k + 1, k + n, k + n + 1);
        }
    double base=*std::min_element(t.Heights.begin(),t.Heights.end())-1.;
    auto wall=[&](int a,int b){
        auto first=ground.vertices[a].position,last=ground.vertices[b].position;
        size_t begin=ground.vertices.size();
        face(ground,{first,last,{last.x,base,last.z},{first.x,base,first.z}});
        for(size_t k=begin;k<ground.vertices.size();++k){ground.vertices[k].color=palette[3];ground.vertices[k].kind=4.f;}
    };
    for(int k=0;k<n-1;++k){wall(k,k+1);wall(k*n+n-1,(k+1)*n+n-1);wall((n-1)*n+k+1,(n-1)*n+k);wall((k+1)*n,k*n);}
    size_t underside=ground.vertices.size();
    face(ground,{{-12,base,-12},{12,base,-12},{12,base,12},{-12,base,12}});
    for(size_t k=underside;k<ground.vertices.size();++k){ground.vertices[k].color=palette[3];ground.vertices[k].kind=4.f;}
    if(t.Volumes.empty())ground.normals();else ground=terrain_volume_geometry(t);
    std::vector<MeshPart> parts;
    parts.push_back({std::move(ground)});
    if (!water.indices.empty()) {
        for (auto &v : water.vertices) {
            v.color = {std::clamp((v.position.y-t.height_at(v.position.x,v.position.z))*.5,0.,1.),
                       1, 1};
            v.kind = t.Lava ? 8.f : 7.f;
        }
        water.normals();
        parts.push_back({std::move(water), {1, 1, 1}, t.Lava});
    }
    return parts;
}
void stamp_shape(TerrainData &terrain, const Character &c) {
    for (const auto &part : shape_geometry(c))
        for (size_t k = 0; k < part.mesh.indices.size(); k += 3) {
            auto a = world_point(c, part.mesh.vertices[part.mesh.indices[k]].position),
                 b = world_point(c, part.mesh.vertices[part.mesh.indices[k + 1]].position),
                 d = world_point(c, part.mesh.vertices[part.mesh.indices[k + 2]].position);
            double det = (b.z - d.z) * (a.x - d.x) + (d.x - b.x) * (a.z - d.z);
            if (std::abs(det) < 1e-9)
                continue;
            // Clamped in floating point before the int conversion, so a vast object cannot overflow it.
            auto cellOf = [](double v, int lo, int hi) { return v >= hi ? hi : v > lo ? int(v) : lo; };
            int x0 = cellOf(std::floor((std::min({a.x, b.x, d.x}) + 12) / .25), 0, 97),
                x1 = cellOf(std::ceil((std::max({a.x, b.x, d.x}) + 12) / .25), -1, 96),
                z0 = cellOf(std::floor((std::min({a.z, b.z, d.z}) + 12) / .25), 0, 97),
                z1 = cellOf(std::ceil((std::max({a.z, b.z, d.z}) + 12) / .25), -1, 96);
            for (int j = z0; j <= z1; j++)
                for (int i = x0; i <= x1; i++) {
                    double x = i * .25 - 12, z = j * .25 - 12,
                           u = ((b.z - d.z) * (x - d.x) + (d.x - b.x) * (z - d.z)) / det,
                           v = ((d.z - a.z) * (x - d.x) + (a.x - d.x) * (z - d.z)) / det, w = 1 - u - v;
                    if (u >= -1e-8 && v >= -1e-8 && w >= -1e-8)
                        terrain.Heights[j * 97 + i] = std::clamp(
                            std::max(terrain.Heights[j * 97 + i], u * a.y + v * b.y + w * d.y), -20., 20.);
                }
        }
}
} // namespace starlight::editor
