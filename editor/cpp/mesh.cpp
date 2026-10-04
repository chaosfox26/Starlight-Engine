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
    if(!s.MaterialCoordinates.empty())for(size_t i=0;i<m.vertices.size();++i)
        m.vertices[i].materialCoordinate=Vec3{s.MaterialCoordinates[i*3],s.MaterialCoordinates[i*3+1],s.MaterialCoordinates[i*3+2]};
    m.normals();
    return m;
}
SculptMesh sculpt_from(const Mesh &m) {
    m.validate();
    SculptMesh s;
    s.Indices = m.indices;
    for (const auto &v : m.vertices)
        s.Positions.insert(s.Positions.end(), {v.position.x, v.position.y, v.position.z});
    bool coordinates=std::any_of(m.vertices.begin(),m.vertices.end(),[](const auto& v){return v.materialCoordinate.has_value();});
    if(coordinates)for(const auto& v:m.vertices){if(!v.materialCoordinate)throw std::runtime_error("Incomplete sculpt material coordinates");auto p=*v.materialCoordinate;s.MaterialCoordinates.insert(s.MaterialCoordinates.end(),{p.x,p.y,p.z});}
    s.validate();
    s.touch();
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
namespace {
std::array<double,4> route_orientation(const Character& c,double yaw){
    using Q=std::array<double,4>;auto multiply=[](Q a,Q b){Vec3 x{a[0],a[1],a[2]},y{b[0],b[1],b[2]};auto v=x*b[3]+y*a[3]+x.cross(y);return Q{v.x,v.y,v.z,a[3]*b[3]-x.dot(y)};};auto axis=[](int index,double angle){Q q{};q[index]=std::sin(angle*.5);q[3]=std::cos(angle*.5);return q;};double rad=std::numbers::pi/180;
    return multiply(axis(1,yaw),multiply(axis(2,c.RotationZ*rad),multiply(axis(1,c.RotationY*rad),axis(0,c.RotationX*rad))));
}
template<class T> Vec3 world_point_of(const T &c, Vec3 p) {
    p = {p.x * c.Width, p.y * c.Height, p.z * c.Depth};
    if (c.PhysicsRotation) {
        auto q = *c.PhysicsRotation;
        Vec3 v{q[0], q[1], q[2]};
        p = p + v.cross(v.cross(p) + p * q[3]) * 2;
    } else
        p = rotate(rotate(rotate(p, 0, c.RotationX), 1, c.RotationY), 2, c.RotationZ);
    return p * c.Size + Vec3{c.X, c.Y, c.Z};
}
template<class T> Vec3 local_point_of(const T &c, Vec3 p) {
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
}
Character route_pose(const Character& source,double seconds){
    auto c=source;if(c.Route.empty()||c.PhysicsRotation)return c;auto at=c.at(seconds);double yaw=c.route_yaw(seconds);c.X=at.X;c.Z=at.Z;
    c.PhysicsRotation=route_orientation(c,yaw);c.Route.clear();return c;
}
ObjectPose object_pose(const Character& c){
    ObjectPose p;p.X=c.X;p.Y=c.Y;p.Z=c.Z;p.RotationX=c.RotationX;p.RotationY=c.RotationY;p.RotationZ=c.RotationZ;p.Width=c.Width;p.Height=c.Height;p.Depth=c.Depth;p.Size=c.Size;p.PhysicsRotation=c.PhysicsRotation;return p;
}
ObjectPose route_pose_of(const Character& source,double seconds){
    auto p=object_pose(source);if(source.Route.empty()||source.PhysicsRotation)return p;
    auto at=source.at(seconds);double yaw=source.route_yaw(seconds);p.X=at.X;p.Z=at.Z;p.PhysicsRotation=route_orientation(source,yaw);return p;
}
Vec3 world_point(const Character &c, Vec3 p) {return world_point_of(c,p);}
Vec3 local_point(const Character &c, Vec3 p) {return local_point_of(c,p);}
Vec3 world_point(const ObjectPose &c, Vec3 p) {return world_point_of(c,p);}
Vec3 local_point(const ObjectPose &c, Vec3 p) {return local_point_of(c,p);}
std::optional<MeshHit> mesh_hit(const Mesh &m, const Character &c, Vec3 origin, Vec3 ray) {
    if (m.indices.size() > 1800) brute_force_ray_tests().fetch_add(1, std::memory_order_relaxed);
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
struct MeshAccel {
    struct Node { double lo[3], hi[3]; uint32_t a = 0, b = 0; }; // leaf: a = first slot, b = triangle count; inner: a = right child, b = 0, left child = this + 1
    std::vector<Node> nodes;
    std::vector<uint32_t> order; // triangle indices, grouped by leaf
};
std::shared_ptr<const MeshAccel> build_mesh_accel(const Mesh &m) {
    m.validate();
    auto accel = std::make_shared<MeshAccel>();
    const size_t count = m.indices.size() / 3;
    std::vector<std::array<double, 3>> low(count), high(count), center(count);
    accel->order.resize(count);
    for (size_t t = 0; t < count; ++t) {
        accel->order[t] = uint32_t(t);
        const Vec3 p[3] = {m.vertices[m.indices[3 * t]].position, m.vertices[m.indices[3 * t + 1]].position, m.vertices[m.indices[3 * t + 2]].position};
        for (int axis = 0; axis < 3; ++axis) {
            auto get = [&](const Vec3& v) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; };
            low[t][axis] = std::min({get(p[0]), get(p[1]), get(p[2])});
            high[t][axis] = std::max({get(p[0]), get(p[1]), get(p[2])});
            center[t][axis] = (low[t][axis] + high[t][axis]) * .5;
        }
    }
    struct Builder {
        MeshAccel& accel; const std::vector<std::array<double, 3>>&low, &high, &center;
        uint32_t build(size_t first, size_t last) {
            const uint32_t index = uint32_t(accel.nodes.size());
            accel.nodes.emplace_back();
            double lo[3] = {INFINITY, INFINITY, INFINITY}, hi[3] = {-INFINITY, -INFINITY, -INFINITY}, clo[3] = {INFINITY, INFINITY, INFINITY}, chi[3] = {-INFINITY, -INFINITY, -INFINITY};
            for (size_t k = first; k < last; ++k) {
                const uint32_t t = accel.order[k];
                for (int axis = 0; axis < 3; ++axis) {
                    lo[axis] = std::min(lo[axis], low[t][axis]); hi[axis] = std::max(hi[axis], high[t][axis]);
                    clo[axis] = std::min(clo[axis], center[t][axis]); chi[axis] = std::max(chi[axis], center[t][axis]);
                }
            }
            for (int axis = 0; axis < 3; ++axis) { accel.nodes[index].lo[axis] = lo[axis]; accel.nodes[index].hi[axis] = hi[axis]; }
            int axis = 0;
            for (int k = 1; k < 3; ++k) if (chi[k] - clo[k] > chi[axis] - clo[axis]) axis = k;
            if (last - first <= 4 || !(chi[axis] > clo[axis])) { accel.nodes[index].a = uint32_t(first); accel.nodes[index].b = uint32_t(last - first); return index; }
            const size_t middle = (first + last) / 2;
            std::nth_element(accel.order.begin() + first, accel.order.begin() + middle, accel.order.begin() + last, [&](uint32_t x, uint32_t y) { return center[x][axis] < center[y][axis]; });
            build(first, middle);
            const uint32_t right = build(middle, last);
            accel.nodes[index].a = right; accel.nodes[index].b = 0;
            return index;
        }
    } builder{*accel, low, high, center};
    if (count) builder.build(0, count);
    return accel;
}
size_t mesh_accel_bytes(const MeshAccel &accel) {
    return sizeof(MeshAccel) + accel.nodes.capacity() * sizeof(MeshAccel::Node) + accel.order.capacity() * sizeof(uint32_t);
}
std::optional<MeshHit> mesh_hit(const Mesh &m, const MeshAccel &accel, const Character &c, Vec3 origin, Vec3 ray) {
    return mesh_hit(m, accel, object_pose(c), origin, ray);
}
std::optional<MeshHit> mesh_hit(const Mesh &m, const MeshAccel &accel, const ObjectPose &c, Vec3 origin, Vec3 ray) {
    ray = ray.unit();
    if (accel.nodes.empty()) return {};
    // World = Size * Rotate(local * (Width, Height, Depth)) + position, so a ray in the world is a ray in the object's space with the
    // same parameter: no triangle needs transforming.
    const Vec3 localOrigin = local_point(c, origin), localDirection = local_point(c, origin + ray) - localOrigin;
    const double o[3] = {localOrigin.x, localOrigin.y, localOrigin.z}, d[3] = {localDirection.x, localDirection.y, localDirection.z};
    double inverse[3];
    for (int axis = 0; axis < 3; ++axis) inverse[axis] = d[axis] == 0 ? INFINITY : 1. / d[axis];
    double best = std::numeric_limits<double>::infinity();
    int64_t bestTriangle = -1;
    uint32_t stack[96];
    int top = 0;
    stack[top++] = 0;
    auto reaches = [&](const MeshAccel::Node& node) {
        double nearest = 0, farthest = best;
        for (int axis = 0; axis < 3; ++axis) {
            if (d[axis] == 0) { if (o[axis] < node.lo[axis] || o[axis] > node.hi[axis]) return false; continue; }
            double t1 = (node.lo[axis] - o[axis]) * inverse[axis], t2 = (node.hi[axis] - o[axis]) * inverse[axis];
            if (t1 > t2) std::swap(t1, t2);
            nearest = std::max(nearest, t1); farthest = std::min(farthest, t2);
            if (nearest > farthest) return false;
        }
        return true;
    };
    const Vec3 direction{d[0], d[1], d[2]};
    while (top) {
        const uint32_t index = stack[--top];
        const auto& node = accel.nodes[index];
        if (!reaches(node)) continue;
        if (node.b == 0) { if (top + 2 > 96) break; stack[top++] = node.a; stack[top++] = index + 1; continue; }
        for (uint32_t k = 0; k < node.b; ++k) {
            const uint32_t t = accel.order[node.a + k];
            const Vec3 a = m.vertices[m.indices[3 * t]].position, e1 = m.vertices[m.indices[3 * t + 1]].position - a, e2 = m.vertices[m.indices[3 * t + 2]].position - a;
            const Vec3 h = direction.cross(e2);
            const double det = e1.dot(h);
            if (std::abs(det) < 1e-14) continue;
            const Vec3 s = localOrigin - a;
            const double u = s.dot(h) / det;
            if (u < 0 || u > 1) continue;
            const Vec3 q = s.cross(e1);
            const double v = direction.dot(q) / det;
            if (v < 0 || u + v > 1) continue;
            const double distance = e2.dot(q) / det;
            if (distance <= 0 || distance > best || (distance == best && int64_t(t) > bestTriangle)) continue;
            best = distance; bestTriangle = t;
        }
    }
    if (bestTriangle < 0) return {};
    // The point and the surface normal come from the world-space corners of the one triangle that was hit.
    const uint32_t t = uint32_t(bestTriangle);
    const Vec3 a = world_point(c, m.vertices[m.indices[3 * t]].position), e1 = world_point(c, m.vertices[m.indices[3 * t + 1]].position) - a, e2 = world_point(c, m.vertices[m.indices[3 * t + 2]].position) - a;
    MeshHit hit{origin + ray * best, e1.cross(e2).unit(), best};
    if (hit.normal.dot(ray) > 0) hit.normal = hit.normal * -1;
    return hit;
}
SculptMesh prepare_sculpt(const Mesh &source) {
    source.validate();
    if (source.indices.size() > 240000)
        throw std::runtime_error("Sculpt source exceeds 80,000 triangles");
    // Preserve authored seams within the existing, unchanged preparation cap.
    if(std::any_of(source.vertices.begin(),source.vertices.end(),[](const auto& v){return v.materialCoordinate.has_value();}))return sculpt_from(source);
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
