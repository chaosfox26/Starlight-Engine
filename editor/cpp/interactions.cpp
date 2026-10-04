#include "interactions.hpp"
#include "object_cache.hpp"
#include <numbers>
#include <cstring>
#include <array>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace starlight::editor {
// Plain cubes (rigid fragments and other box proxies) are posed from the eight corners of one cached unit box; the generic path
// builds and walks the full mesh. Identical results: the cube's vertices are its corners and world_point is applied to each.
static bool cube_corners(std::array<Vec3,8>& corners){
    static const std::array<Vec3,8> cached=[]{
        std::array<Vec3,8> result{};Character proto;proto.Shape="cube";proto.Width=proto.Height=proto.Depth=proto.Size=1;
        Vec3 low{1e300,1e300,1e300},high{-1e300,-1e300,-1e300};std::vector<Vec3> seen;
        for(const auto& v:object_mesh(proto).vertices){low={std::min(low.x,v.position.x),std::min(low.y,v.position.y),std::min(low.z,v.position.z)};high={std::max(high.x,v.position.x),std::max(high.y,v.position.y),std::max(high.z,v.position.z)};}
        for(int k=0;k<8;++k)result[k]={k&1?high.x:low.x,k&2?high.y:low.y,k&4?high.z:low.z};
        // Only valid when the generic mesh really has a vertex at every corner.
        std::array<bool,8> present{};
        for(const auto& v:object_mesh(proto).vertices)for(int k=0;k<8;++k)if(v.position.x==result[k].x&&v.position.y==result[k].y&&v.position.z==result[k].z)present[k]=true;
        for(bool p:present)if(!p)return std::array<Vec3,8>{Vec3{1e300,0,0}};
        return result;
    }();
    if(cached[0].x==1e300)return false;
    corners=cached;return true;
}
Bounds object_bounds(const Character &c) {
    // Every caller in the engine reaches the shared cache here, so a new feature that asks for an object's bounds is cached without
    // knowing the cache exists. Plain cubes are cheaper to compute than to look up and objects that move on their own cannot be cached.
    if(!in_object_cache()&&(c.Sculpt||!(c.Shape&&*c.Shape=="cube"))){
        if(bounds_cacheable(c))return ObjectCache::shared().bounds(c);
        return ObjectCache::shared().bounds_at(c,object_pose(c),false); // a route object at its present spot: shared geometry, no mesh rebuilt
    }
    return object_bounds_raw(c);
}
Bounds object_bounds_raw(const Character &c) {
    if(!in_object_cache()&&counts_as_static_bypass(c)){bypass_counters().staticBounds.fetch_add(1,std::memory_order_relaxed);if(bypass_trace()){bypass_counters().record(_ReturnAddress(),0);}}
    static const bool noCubeFast=GetEnvironmentVariableA("STARLIGHT_DISABLE_CUBE_FAST",nullptr,0)>0;
    if(!noCubeFast&&c.Shape&&*c.Shape=="cube"&&!c.Sculpt){
        std::array<Vec3,8> corners;
        if(cube_corners(corners)){
            c.validate();
            Bounds fast;
            for(const auto& corner:corners){auto p=world_point(c,corner);
                fast.low={std::min(fast.low.x,p.x),std::min(fast.low.y,p.y),std::min(fast.low.z,p.z)};
                fast.high={std::max(fast.high.x,p.x),std::max(fast.high.y,p.y),std::max(fast.high.z,p.z)};}
            static const bool verify=[]{char value[8]{};return GetEnvironmentVariableA("STARLIGHT_BOUNDS_VERIFY",value,8)&&value[0]=='1';}();
            if(!verify)return fast;
            Bounds slow;
            for(const auto& v:object_mesh(c).vertices){auto p=world_point(c,v.position);
                slow.low={std::min(slow.low.x,p.x),std::min(slow.low.y,p.y),std::min(slow.low.z,p.z)};
                slow.high={std::max(slow.high.x,p.x),std::max(slow.high.y,p.y),std::max(slow.high.z,p.z)};}
            if(std::memcmp(&fast,&slow,sizeof(Bounds))!=0)throw std::runtime_error("Fast cube bounds differ from the mesh bounds");
            return fast;
        }
    }
    Bounds b;
    for (const auto &v : object_mesh(c).vertices) {
        auto p = world_point(c, v.position);
        b.low = {std::min(b.low.x, p.x), std::min(b.low.y, p.y), std::min(b.low.z, p.z)};
        b.high = {std::max(b.high.x, p.x), std::max(b.high.y, p.y), std::max(b.high.z, p.z)};
    }
    return b;
}
double snap_increment(double value,double step) {
    if(!std::isfinite(value)||!std::isfinite(step)||step<=0)
        throw std::runtime_error("Snap spacing must be positive and finite");
    const double result=std::nearbyint(value/step)*step;
    if(!std::isfinite(result))throw std::runtime_error("Snap result is out of range");
    return result;
}
void snap_object(SceneData &scene, Character &c, bool grid, bool objects, bool ground,double gridStep) {
    double oldX = c.X, oldZ = c.Z;
    if (grid) {
        c.X = snap_increment(c.X,gridStep);
        c.Y = snap_increment(c.Y,gridStep);
        c.Z = snap_increment(c.Z,gridStep);
    }
    if (objects) {
        auto b = object_bounds(c);
        double lo[] = {b.low.x, b.low.y, b.low.z}, hi[] = {b.high.x, b.high.y, b.high.z},
               best[] = {.251, .251, .251}, shift[] = {0, 0, 0};
        for (const auto &other : scene.Characters) {
            if (&other == &c)
                continue;
            auto o = object_bounds(other);
            double ol[] = {o.low.x, o.low.y, o.low.z}, oh[] = {o.high.x, o.high.y, o.high.z};
            for (int a = 0; a < 3; a++) {
                int u = (a + 1) % 3, v = (a + 2) % 3;
                if (hi[u] < ol[u] - .25 || lo[u] > oh[u] + .25 || hi[v] < ol[v] - .25 || lo[v] > oh[v] + .25)
                    continue;
                for (double delta : {ol[a] - hi[a], oh[a] - lo[a]})
                    if (std::abs(delta) < best[a]) {
                        best[a] = std::abs(delta);
                        shift[a] = delta;
                    }
            }
        }
        c.X += shift[0];
        c.Y += shift[1];
        c.Z += shift[2];
    }
    double floor = scene.Terrain ? scene.Terrain->height_at(c.X, c.Z) : 0;
    if (ground) {
        auto b = object_bounds(c);
        c.Y += floor - b.low.y;
    }
    if (c.KeepAboveFloor) {
        auto b = object_bounds(c);
        c.Y += std::max(0., floor - b.low.y);
    }
    for (auto &q : c.Route) {
        q.X += c.X - oldX;
        q.Z += c.Z - oldZ;
    }
}
std::optional<Vec3> terrain_hit(const TerrainData *terrain, Vec3 origin, Vec3 ray) {
    double previous = 0;
    bool had = false;
    auto gapAt = [&](Vec3 p) { return terrain ? (terrain->Volumes.empty()?p.y-terrain->height_at(p.x,p.z):terrain->density_at(p.x,p.y,p.z)) : p.y; };
    for (double t = 0; t < 180; t += .08) {
        auto p = origin + ray * t;
        if (std::abs(p.x) > 12 || std::abs(p.z) > 12) {
            had = false;
            continue;
        }
        double gap = gapAt(p);
        if (gap <= 0 && had && previous > 0) {
            double lo = t - .08, hi = t;
            for (int n = 0; n < 12; n++) {
                double mid = (lo + hi) / 2;
                auto q = origin + ray * mid;
                if (gapAt(q)>0)
                    lo = mid;
                else
                    hi = mid;
            }
            return origin + ray * ((lo + hi) / 2);
        }
        previous = gap;
        had = true;
    }
    return {};
}
std::vector<GizmoSegment> gizmo_segments(const Character &c, std::string_view mode) {
    std::vector<GizmoSegment> result;
    Vec3 origin{c.X, c.Y + .12, c.Z}, axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int axis = 0; axis < 3; axis++) {
        if (mode == "Rotate") {
            for (int i = 0; i < 48; i++) {
                double a = i * std::numbers::pi / 24, b = (i + 1) * std::numbers::pi / 24;
                auto u = axes[(axis + 1) % 3], v = axes[(axis + 2) % 3];
                result.push_back({origin + u * (std::cos(a) * 1.2) + v * (std::sin(a) * 1.2),
                                  origin + u * (std::cos(b) * 1.2) + v * (std::sin(b) * 1.2), axis});
            }
        } else {
            auto end = origin + axes[axis] * 1.8, cross = axes[(axis + 1) % 3];
            result.push_back({origin, end, axis});
            result.push_back({end, end - axes[axis] * .25 + cross * .14, axis});
            result.push_back({end, end - axes[axis] * .25 - cross * .14, axis});
        }
    }
    return result;
}
std::vector<MeshPart> gizmo_geometry(const Character &c, std::string_view mode) {
    std::vector<MeshPart> parts(3);
    Vec3 colors[] = {{1, 69. / 255, 0}, {50. / 255, 205. / 255, 50. / 255}, {0, 191. / 255, 1}};
    for (int i = 0; i < 3; i++) {
        parts[i].color = colors[i];
        parts[i].emissive = true;
    }
    for (auto line : gizmo_segments(c, mode)) {
        auto &mesh = parts[line.axis].mesh;
        auto dir = (line.b - line.a).unit();
        for (auto axis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
            auto side = dir.cross(axis);
            if (side.length() < 1e-8)
                continue;
            side = side.unit() * .025;
            int k = int(mesh.vertices.size());
            for (auto p : {line.a + side, line.b + side, line.b - side, line.a - side})
                mesh.vertices.push_back({p, {0, 1, 0}});
            mesh.indices.insert(mesh.indices.end(), {k, k + 1, k + 2, k, k + 2, k + 3});
        }
    }
    return parts;
}
} // namespace starlight::editor

namespace starlight::editor {
std::vector<MeshPart> editor_overlays(const SceneData &scene, std::string_view selected,
                                      std::string_view tool, bool playing) {
    std::vector<MeshPart> output;
    MeshPart lines;
    auto line = [&](double x, double z, double xx, double zz, Vec3 color, double width) {
        double dx = xx - x, dz = zz - z, len = std::hypot(dx, dz);
        if (len < 1e-6)
            return;
        double nx = -dz / len * width, nz = dx / len * width;
        int k = int(lines.mesh.vertices.size());
        for (auto p : {Vec3{x + nx, .014, z + nz}, Vec3{xx + nx, .014, zz + nz}, Vec3{xx - nx, .014, zz - nz},
                       Vec3{x - nx, .014, z - nz}})
            lines.mesh.vertices.push_back({p, {0, 1, 0}, color});
        lines.mesh.indices.insert(lines.mesh.indices.end(), {k, k + 1, k + 2, k, k + 2, k + 3});
    };
    if (!scene.Terrain)
        for (int n = -12; n <= 12; n++) {
            line(n, -12, n, 12, {41. / 255, 54. / 255, 78. / 255}, .008);
            line(-12, n, 12, n, {41. / 255, 54. / 255, 78. / 255}, .008);
        }
    for (const auto &c : scene.Characters) {
        double x = c.X, z = c.Z;
        for (auto p : c.Route) {
            line(x, z, p.X, p.Z,
                 c.Id == selected ? Vec3{230. / 255, 111. / 255, 231. / 255}
                                  : Vec3{71. / 255, 161. / 255, 233. / 255},
                 .04);
            x = p.X;
            z = p.Z;
        }
        if (c.Id == selected) {
            for (int n = 0; n < 40; n++) {
                double a = n * std::numbers::pi / 20, b = (n + 1) * std::numbers::pi / 20;
                line(c.X + std::cos(a) * .55 * c.Size, c.Z + std::sin(a) * .55 * c.Size,
                     c.X + std::cos(b) * .55 * c.Size, c.Z + std::sin(b) * .55 * c.Size,
                     {113. / 255, 208. / 255, 1}, .03);
            }
            if (!playing && (tool == "Move" || tool == "Rotate" || tool == "Scale"))
                output = gizmo_geometry(c, tool);
        }
    }
    if (!lines.mesh.indices.empty())
        output.push_back(std::move(lines));
    return output;
}
} // namespace starlight::editor

namespace starlight::editor {
void move_group(SceneData &scene, const Character &selected,
                const std::unordered_map<std::string, Character> &originals) {
    auto it = originals.find(selected.Id);
    if (it == originals.end())
        return;
    const auto &original = it->second;
    Character transform;
    transform.RotationX = selected.RotationX - original.RotationX;
    transform.RotationY = selected.RotationY - original.RotationY;
    transform.RotationZ = selected.RotationZ - original.RotationZ;
    double sx = selected.Width / original.Width, sy = selected.Height / original.Height,
           sz = selected.Depth / original.Depth;
    for (auto &c : scene.Characters) {
        auto found = originals.find(c.Id);
        if (c.Id == selected.Id || found == originals.end())
            continue;
        const auto &before = found->second;
        auto offset = world_point(transform, {(before.X - original.X) * sx, (before.Y - original.Y) * sy,
                                              (before.Z - original.Z) * sz});
        c.X = selected.X + offset.x;
        c.Y = selected.Y + offset.y;
        c.Z = selected.Z + offset.z;
        c.Width = before.Width * sx;
        c.Height = before.Height * sy;
        c.Depth = before.Depth * sz;
        c.RotationX = before.RotationX + transform.RotationX;
        c.RotationY = before.RotationY + transform.RotationY;
        c.RotationZ = before.RotationZ + transform.RotationZ;
        snap_object(scene, c, false, false, false);
        c.Route = before.Route;
        for (auto &p : c.Route) {
            p.X += c.X - before.X;
            p.Z += c.Z - before.Z;
        }
    }
}
} // namespace starlight::editor
