#include "interactions.hpp"
#include <numbers>
namespace starlight::editor {
Bounds object_bounds(const Character &c) {
    Bounds b;
    for (const auto &v : object_mesh(c).vertices) {
        auto p = world_point(c, v.position);
        b.low = {std::min(b.low.x, p.x), std::min(b.low.y, p.y), std::min(b.low.z, p.z)};
        b.high = {std::max(b.high.x, p.x), std::max(b.high.y, p.y), std::max(b.high.z, p.z)};
    }
    return b;
}
void snap_object(SceneData &scene, Character &c, bool grid, bool objects, bool ground) {
    double oldX = c.X, oldZ = c.Z;
    if (grid) {
        c.X = std::nearbyint(c.X * 2) / 2;
        c.Y = std::nearbyint(c.Y * 2) / 2;
        c.Z = std::nearbyint(c.Z * 2) / 2;
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
    auto height = [&](Vec3 p) { return terrain ? terrain->height_at(p.x, p.z) : 0.; };
    for (double t = 0; t < 180; t += .08) {
        auto p = origin + ray * t;
        if (std::abs(p.x) > 12 || std::abs(p.z) > 12) {
            had = false;
            continue;
        }
        double gap = p.y - height(p);
        if (gap <= 0 && had && previous > 0) {
            double lo = t - .08, hi = t;
            for (int n = 0; n < 12; n++) {
                double mid = (lo + hi) / 2;
                auto q = origin + ray * mid;
                if (q.y > height(q))
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
