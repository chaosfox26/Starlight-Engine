#include "flow.hpp"
#include <map>
#include <numeric>
#include <tuple>
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
TerrainFlow::TerrainFlow(const TerrainData &t, const std::vector<LiquidSource> &sources)
    : terrain_(t), sources_(sources) {
    terrain_.validate();
    for (const auto &s : sources_)
        remaining_.push_back(s.Volume);
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++)
            bed_[z * N + x] = t.height_at(-12 + (x + .5) * Cell, -12 + (z + .5) * Cell);
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
std::pair<double, double> TerrainFlow::liquid_at(double x, double z) const {
    int i = index(x, z);
    if (water[i] > .002)
        return {surface(i, water[i]), 1000};
    if (lava[i] > .002)
        return {surface(i, lava[i]), 2800};
    return {-INFINITY, 0};
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
}
void TerrainFlow::transport(std::array<double, 2304> &depth, double dt, double speed) {
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
        auto sample = [&](double u, double v) {
            u = std::clamp(u, 0., double(N - 1));
            v = std::clamp(v, 0., double(N - 1));
            int x = std::min(int(u), N - 2), z = std::min(int(v), N - 2);
            u -= x;
            v -= z;
            return depths[z * N + x] * (1 - u) * (1 - v) + depths[z * N + x + 1] * u * (1 - v) +
                   depths[(z + 1) * N + x] * (1 - u) * v + depths[(z + 1) * N + x + 1] * u * v;
        };
        for (int z = 0; z <= R; z++)
            for (int x = 0; x <= R; x++) {
                int k = z * (R + 1) + x;
                double px = -12 + x * .25, pz = -12 + z * .25;
                values[k] = sample(x * .5 - .5, z * .5 - .5);
                points[k] = {px, surface(index(px, pz), values[k]) + .009, pz};
            }
        std::map<std::tuple<long long, long long, long long>, int> vertices;
        auto vertex = [&](Vec3 p) {
            auto [it, inserted] = vertices.emplace(std::tuple{(long long)std::nearbyint(p.x * 1e6),
                                                              (long long)std::nearbyint(p.y * 1e6),
                                                              (long long)std::nearbyint(p.z * 1e6)},
                                                   int(mesh.vertices.size()));
            if (inserted)
                mesh.vertices.push_back({p});
            return it->second;
        };
        auto triangle = [&](int a, int b, int c) {
            int ids[] = {a, b, c};
            std::vector<Vec3> clipped;
            for (int j = 0; j < 3; j++) {
                int u = ids[j], v = ids[(j + 1) % 3];
                bool inside = values[u] > .002, next = values[v] > .002;
                if (inside)
                    clipped.push_back(points[u]);
                if (inside != next) {
                    double t = (.002 - values[u]) / (values[v] - values[u]);
                    clipped.push_back(points[u] + (points[v] - points[u]) * t);
                }
            }
            if (clipped.size() < 3)
                return;
            int first = vertex(clipped[0]);
            for (size_t j = 1; j + 1 < clipped.size(); j++)
                mesh.indices.insert(mesh.indices.end(), {first, vertex(clipped[j]), vertex(clipped[j + 1])});
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
