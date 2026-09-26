#include "mesh.hpp"
#include <map>
#include <numbers>
#include <tuple>
namespace starlight::editor {
void Mesh::validate() const {
    if (indices.size() % 3)
        throw std::runtime_error("Incomplete mesh triangle");
    for (int i : indices)
        if (i < 0 || size_t(i) >= vertices.size())
            throw std::runtime_error("Mesh index outside vertex array");
    for (const auto &v : vertices)
        if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z))
            throw std::runtime_error("Nonfinite mesh vertex");
}
void Mesh::normals() {
    validate();
    for (auto &v : vertices)
        v.normal = {};
    for (size_t i = 0; i < indices.size(); i += 3) {
        auto &a = vertices[indices[i]];
        auto &b = vertices[indices[i + 1]];
        auto &c = vertices[indices[i + 2]];
        auto n = (b.position - a.position).cross(c.position - a.position);
        a.normal = a.normal + n;
        b.normal = b.normal + n;
        c.normal = c.normal + n;
    }
    for (auto &v : vertices)
        v.normal = v.normal.unit();
}
Mesh mesh_from(const SculptMesh &s) {
    s.validate();
    Mesh m;
    m.indices = s.Indices;
    for (size_t i = 0; i < s.Positions.size(); i += 3)
        m.vertices.push_back({{s.Positions[i], s.Positions[i + 1], s.Positions[i + 2]}});
    m.normals();
    return m;
}
SculptMesh sculpt_from(const Mesh &m) {
    m.validate();
    SculptMesh s;
    s.Indices = m.indices;
    for (const auto &v : m.vertices)
        s.Positions.insert(s.Positions.end(), {v.position.x, v.position.y, v.position.z});
    s.validate();
    return s;
}
namespace {
Vec3 rotate(Vec3 p, int axis, double degrees) {
    double a = degrees * std::numbers::pi / 180, c = std::cos(a), s = std::sin(a);
    if (axis == 0)
        return {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
    if (axis == 1)
        return {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
    return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
}
} // namespace
Vec3 world_point(const Character &c, Vec3 p) {
    p = {p.x * c.Width, p.y * c.Height, p.z * c.Depth};
    if (c.PhysicsRotation) {
        auto q = *c.PhysicsRotation;
        Vec3 v{q[0], q[1], q[2]};
        p = p + v.cross(v.cross(p) + p * q[3]) * 2;
    } else
        p = rotate(rotate(rotate(p, 0, c.RotationX), 1, c.RotationY), 2, c.RotationZ);
    return p * c.Size + Vec3{c.X, c.Y, c.Z};
}
Vec3 local_point(const Character &c, Vec3 p) {
    if (c.Size <= 0 || c.Width <= 0 || c.Height <= 0 || c.Depth <= 0)
        throw std::runtime_error("Singular object transform");
    p = (p - Vec3{c.X, c.Y, c.Z}) / c.Size;
    if (c.PhysicsRotation) {
        auto q = *c.PhysicsRotation;
        Vec3 v{-q[0], -q[1], -q[2]};
        p = p + v.cross(v.cross(p) + p * q[3]) * 2;
    } else
        p = rotate(rotate(rotate(p, 2, -c.RotationZ), 1, -c.RotationY), 0, -c.RotationX);
    return {p.x / c.Width, p.y / c.Height, p.z / c.Depth};
}
std::optional<MeshHit> mesh_hit(const Mesh &m, const Character &c, Vec3 origin, Vec3 ray) {
    m.validate();
    ray = ray.unit();
    MeshHit hit;
    for (size_t t = 0; t < m.indices.size(); t += 3) {
        auto a = world_point(c, m.vertices[m.indices[t]].position),
             e1 = world_point(c, m.vertices[m.indices[t + 1]].position) - a,
             e2 = world_point(c, m.vertices[m.indices[t + 2]].position) - a;
        auto h = ray.cross(e2);
        double det = e1.dot(h);
        if (std::abs(det) < 1e-10)
            continue;
        auto s = origin - a;
        double u = s.dot(h) / det;
        if (u < 0 || u > 1)
            continue;
        auto q = s.cross(e1);
        double v = ray.dot(q) / det;
        if (v < 0 || u + v > 1)
            continue;
        double d = e2.dot(q) / det;
        if (d <= 0 || d >= hit.distance)
            continue;
        hit = {origin + ray * d, e1.cross(e2).unit(), d};
        if (hit.normal.dot(ray) > 0)
            hit.normal = hit.normal * -1;
    }
    if (std::isfinite(hit.distance))
        return hit;
    return {};
}
SculptMesh prepare_sculpt(const Mesh &source) {
    source.validate();
    if (source.indices.size() > 240000)
        throw std::runtime_error("Sculpt source exceeds 80,000 triangles");
    Mesh m;
    std::map<std::tuple<long long, long long, long long>, int> lookup;
    std::vector<int> remap;
    for (const auto &v : source.vertices) {
        auto p = v.position;
        auto key = std::tuple{(long long)std::nearbyint(p.x * 1e6), (long long)std::nearbyint(p.y * 1e6),
                              (long long)std::nearbyint(p.z * 1e6)};
        auto [it, inserted] = lookup.emplace(key, int(m.vertices.size()));
        if (inserted)
            m.vertices.push_back(v);
        remap.push_back(it->second);
    }
    for (int i : source.indices)
        m.indices.push_back(remap[i]);
    for (int pass = 0; pass < 6 && m.indices.size() < 24000; pass++) {
        Mesh next;
        next.vertices = m.vertices;
        std::map<std::pair<int, int>, int> edges;
        auto mid = [&](int a, int b) {
            auto key = std::pair{std::min(a, b), std::max(a, b)};
            auto [it, inserted] = edges.emplace(key, int(next.vertices.size()));
            if (inserted)
                next.vertices.push_back({(m.vertices[a].position + m.vertices[b].position) * .5});
            return it->second;
        };
        for (size_t t = 0; t < m.indices.size(); t += 3) {
            int a = m.indices[t], b = m.indices[t + 1], d = m.indices[t + 2], ab = mid(a, b), bd = mid(b, d),
                da = mid(d, a);
            next.indices.insert(next.indices.end(), {a, ab, da, ab, b, bd, da, bd, d, ab, bd, da});
        }
        m = std::move(next);
    }
    return sculpt_from(m);
}
void sculpt_brush(Character &c, Vec3 hit, Vec3 normal, double radius, double strength, std::string_view tool,
                  bool reverse, double dt) {
    if (!c.Sculpt)
        throw std::runtime_error("Object is not prepared for sculpting");
    if (!(radius > 0 && strength >= 0 && dt >= 0) || !std::isfinite(radius + strength + dt) ||
        !std::isfinite(hit.x + hit.y + hit.z + normal.x + normal.y + normal.z))
        throw std::runtime_error("Invalid sculpt brush");
    if (tool != "pull" && tool != "push" && tool != "smooth" && tool != "flatten")
        throw std::runtime_error("Unknown sculpt brush");
    auto mesh = mesh_from(*c.Sculpt);
    std::vector<Vec3> points, sums(mesh.vertices.size());
    std::vector<int> count(mesh.vertices.size());
    for (const auto &v : mesh.vertices)
        points.push_back(world_point(c, v.position));
    if (tool == "smooth")
        for (size_t t = 0; t < mesh.indices.size(); t += 3)
            for (int j = 0; j < 3; j++) {
                int a = mesh.indices[t + j], b = mesh.indices[t + (j + 1) % 3];
                sums[a] = sums[a] + points[b];
                sums[b] = sums[b] + points[a];
                count[a]++;
                count[b]++;
            }
    for (size_t i = 0; i < points.size(); i++) {
        auto p = points[i];
        double d = (p - hit).length() / radius;
        if (d >= 1)
            continue;
        double w = std::pow(1 - d * d, 2), blend = 1 - std::exp(-strength * dt * 10 * w);
        Vec3 next;
        if (tool == "smooth" && count[i])
            next = p + (sums[i] / count[i] - p) * blend;
        else if (tool == "flatten")
            next = p - normal * ((p - hit).dot(normal) * blend);
        else
            next = p + normal * ((tool == "push" ? -1 : 1) * (reverse ? -1 : 1) * strength * dt * w * .8);
        mesh.vertices[i].position = local_point(c, next);
    }
    c.Sculpt = sculpt_from(mesh);
}
} // namespace starlight::editor
