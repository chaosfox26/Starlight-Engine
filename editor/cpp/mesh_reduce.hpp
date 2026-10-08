#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "mesh.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <queue>
#include <set>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace starlight::editor {

// Detail setting for the engine's built-in (procedural) meshes: the share of triangles kept. The default is 0.33 (67% fewer triangles, the owner 2026-10-08). STARLIGHT_MESH_DETAIL=0.67
// keeps two thirds of the triangles of every large built-in mesh; STARLIGHT_MESH_DETAIL=1 restores the full meshes. Authored files are never touched: the reduction is applied
// to the generated geometry only, once, when a shape is first built.
inline double mesh_detail() {
    static const double value = [] {
        char text[32]{};
        if (!GetEnvironmentVariableA("STARLIGHT_MESH_DETAIL", text, sizeof(text))) return 0.33;
        const double v = std::atof(text);
        return (v > 0.05 && v < 1.0) ? v : 1.0;
    }();
    return value;
}

// Quadric edge-collapse reduction (Garland and Heckbert). Keeps the surface shape by always collapsing the edge that moves it least, never lets a closed
// surface open or fold over itself, and holds open borders in place. Vertices with the same position are treated as one, so a mesh that repeats vertices
// along a hard edge is reduced as the single surface it is. The result keeps the attributes (colour, kind, ...) of the surviving vertices; normals are NOT
// recomputed here, call Mesh::normals() when the mesh is meant to be smooth-shaded.
// `keep` is the share of triangles to keep (0.67 = about two thirds). Meshes with fewer than `minimumTriangles` triangles are returned unchanged.
inline Mesh reduce_mesh(const Mesh& source, double keep, size_t minimumTriangles = 400) {
    const size_t faceCount = source.indices.size() / 3;
    if (!(keep > 0 && keep < 1) || faceCount < minimumTriangles) return source;
    // Weld vertices by exact position.
    std::map<std::tuple<double, double, double>, int> welded;
    std::vector<int> weldOf(source.vertices.size());
    std::vector<int> representative; // welded id -> source vertex
    for (size_t i = 0; i < source.vertices.size(); ++i) {
        const auto& p = source.vertices[i].position;
        auto key = std::make_tuple(p.x, p.y, p.z);
        auto it = welded.find(key);
        if (it == welded.end()) {
            it = welded.emplace(key, int(representative.size())).first;
            representative.push_back(int(i));
        }
        weldOf[i] = it->second;
    }
    const int vertexCount = int(representative.size());
    std::vector<Vec3> position(vertexCount);
    for (int v = 0; v < vertexCount; ++v) position[v] = source.vertices[representative[v]].position;
    std::vector<std::array<int, 3>> face;
    face.reserve(faceCount);
    for (size_t f = 0; f < faceCount; ++f) {
        std::array<int, 3> t{weldOf[source.indices[f * 3]], weldOf[source.indices[f * 3 + 1]], weldOf[source.indices[f * 3 + 2]]};
        if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue; // already degenerate
        face.push_back(t);
    }
    std::vector<char> faceAlive(face.size(), 1), vertexAlive(vertexCount, 1);
    std::vector<std::vector<int>> around(vertexCount); // faces touching each vertex
    for (size_t f = 0; f < face.size(); ++f)
        for (int v : face[f]) around[v].push_back(int(f));
    auto faceNormalArea = [&](const std::array<int, 3>& t, const Vec3* moved = nullptr, int movedVertex = -1, Vec3 movedTo = {}) {
        Vec3 p[3];
        for (int k = 0; k < 3; ++k) p[k] = t[k] == movedVertex ? movedTo : position[t[k]];
        (void)moved;
        return (p[1] - p[0]).cross(p[2] - p[0]);
    };
    // Quadrics: symmetric 4x4 stored as 10 doubles.
    using Quadric = std::array<double, 10>;
    auto plane = [](Vec3 n, double d, double weight) {
        return Quadric{weight * n.x * n.x, weight * n.x * n.y, weight * n.x * n.z, weight * n.x * d, weight * n.y * n.y,
                       weight * n.y * n.z, weight * n.y * d,   weight * n.z * n.z, weight * n.z * d,   weight * d * d};
    };
    auto add = [](Quadric a, const Quadric& b) {
        for (int i = 0; i < 10; ++i) a[i] += b[i];
        return a;
    };
    auto evaluate = [](const Quadric& q, Vec3 p) {
        return q[0] * p.x * p.x + 2 * q[1] * p.x * p.y + 2 * q[2] * p.x * p.z + 2 * q[3] * p.x + q[4] * p.y * p.y + 2 * q[5] * p.y * p.z + 2 * q[6] * p.y +
               q[7] * p.z * p.z + 2 * q[8] * p.z + q[9];
    };
    std::vector<Quadric> quadric(vertexCount, Quadric{});
    for (size_t f = 0; f < face.size(); ++f) {
        Vec3 n = faceNormalArea(face[f]);
        double twiceArea = n.length();
        if (twiceArea < 1e-18) continue;
        n = n * (1. / twiceArea);
        double d = -n.dot(position[face[f][0]]);
        Quadric q = plane(n, d, twiceArea * .5);
        for (int v : face[f]) quadric[v] = add(quadric[v], q);
    }
    // Open borders: an edge used by a single face gets a plane standing on it, so the border keeps its place.
    std::map<std::pair<int, int>, int> edgeUse;
    for (size_t f = 0; f < face.size(); ++f)
        for (int k = 0; k < 3; ++k) {
            int a = face[f][k], b = face[f][(k + 1) % 3];
            ++edgeUse[{std::min(a, b), std::max(a, b)}];
        }
    std::vector<char> onBorder(vertexCount, 0);
    for (size_t f = 0; f < face.size(); ++f) {
        Vec3 fn = faceNormalArea(face[f]);
        if (fn.length() < 1e-18) continue;
        fn = fn.unit();
        for (int k = 0; k < 3; ++k) {
            int a = face[f][k], b = face[f][(k + 1) % 3];
            if (edgeUse[{std::min(a, b), std::max(a, b)}] != 1) continue;
            Vec3 edge = position[b] - position[a];
            double length = edge.length();
            if (length < 1e-12) continue;
            Vec3 n = edge.cross(fn).unit();
            Quadric q = plane(n, -n.dot(position[a]), length * length * 100.);
            quadric[a] = add(quadric[a], q);
            quadric[b] = add(quadric[b], q);
            onBorder[a] = onBorder[b] = 1;
        }
    }
    std::vector<int> version(vertexCount, 0);
    struct Candidate {
        double cost;
        int a, b, versionA, versionB;
        Vec3 target;
        bool operator<(const Candidate& o) const { return cost > o.cost; }
    };
    std::priority_queue<Candidate> queue;
    auto neighbours = [&](int v) {
        std::set<int> result;
        for (int f : around[v])
            if (faceAlive[f])
                for (int w : face[f])
                    if (w != v) result.insert(w);
        return result;
    };
    auto candidate = [&](int a, int b) {
        Quadric q = add(quadric[a], quadric[b]);
        Vec3 options[3]{position[a], position[b], (position[a] + position[b]) * .5};
        int best = 0;
        double bestCost = 1e300;
        for (int i = 0; i < 3; ++i) {
            double c = evaluate(q, options[i]);
            if (c < bestCost) {
                bestCost = c;
                best = i;
            }
        }
        const double length = (position[a] - position[b]).length();
        return Candidate{std::max(0., bestCost) + 1e-12 * length * length, a, b, version[a], version[b], options[best]};
    };
    for (int v = 0; v < vertexCount; ++v)
        for (int w : neighbours(v))
            if (v < w) queue.push(candidate(v, w));
    size_t liveFaces = face.size();
    const size_t target = std::max<size_t>(4, size_t(std::llround(double(liveFaces) * keep)));
    auto collapsible = [&](const Candidate& c) {
        const int a = c.a, b = c.b;
        std::vector<int> shared; // faces holding both ends
        for (int f : around[b])
            if (faceAlive[f] && (face[f][0] == a || face[f][1] == a || face[f][2] == a)) shared.push_back(f);
        if (shared.empty() || shared.size() > 2) return false;
        // Link condition: the only common neighbours of a and b are the far corners of the faces they share.
        auto na = neighbours(a), nb = neighbours(b);
        std::set<int> corners;
        for (int f : shared)
            for (int w : face[f])
                if (w != a && w != b) corners.insert(w);
        for (int w : na)
            if (w != b && nb.count(w) && !corners.count(w)) return false;
        if (onBorder[a] && onBorder[b] && shared.size() == 2) return false; // would pinch two border parts together
        // No face may fold over or collapse to a sliver.
        for (int survivor : {a, b}) {
            for (int f : around[survivor]) {
                if (!faceAlive[f]) continue;
                bool holdsBoth = (face[f][0] == a || face[f][1] == a || face[f][2] == a) && (face[f][0] == b || face[f][1] == b || face[f][2] == b);
                if (holdsBoth) continue;
                Vec3 before = faceNormalArea(face[f]);
                std::array<int, 3> t = face[f];
                for (int& w : t)
                    if (w == b) w = a;
                Vec3 after = faceNormalArea(t, nullptr, a, c.target);
                double lengthBefore = before.length(), lengthAfter = after.length();
                if (lengthAfter < 1e-14 || lengthBefore < 1e-14) return false;
                if (before.dot(after) / (lengthBefore * lengthAfter) < .25) return false;
            }
        }
        return true;
    };
    while (liveFaces > target && !queue.empty()) {
        Candidate c = queue.top();
        queue.pop();
        if (!vertexAlive[c.a] || !vertexAlive[c.b] || c.versionA != version[c.a] || c.versionB != version[c.b]) continue;
        if (!collapsible(c)) continue;
        const int a = c.a, b = c.b;
        // Faces holding both ends vanish; the rest that held b now hold a.
        for (int f : around[b]) {
            if (!faceAlive[f]) continue;
            bool holdsA = face[f][0] == a || face[f][1] == a || face[f][2] == a;
            if (holdsA) {
                faceAlive[f] = 0;
                --liveFaces;
                continue;
            }
            for (int& w : face[f])
                if (w == b) w = a;
            around[a].push_back(f);
        }
        position[a] = c.target;
        quadric[a] = add(quadric[a], quadric[b]);
        onBorder[a] = onBorder[a] || onBorder[b];
        vertexAlive[b] = 0;
        ++version[a];
        for (int w : neighbours(a)) queue.push(candidate(std::min(a, w), std::max(a, w)));
    }
    // Compact.
    Mesh out;
    std::vector<int> newIndex(vertexCount, -1);
    for (size_t f = 0; f < face.size(); ++f) {
        if (!faceAlive[f]) continue;
        for (int w : face[f]) {
            if (newIndex[w] < 0) {
                newIndex[w] = int(out.vertices.size());
                Vertex v = source.vertices[representative[w]];
                v.position = position[w];
                out.vertices.push_back(v);
            }
            out.indices.push_back(newIndex[w]);
        }
    }
    return out;
}

} // namespace starlight::editor
