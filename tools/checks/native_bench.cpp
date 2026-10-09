// CPU-only micro-benchmarks of the 2026-10-07/08 overnight performance changes. Not a check and not part of the default test run:
//   cmake --build <dir> --target Starlight.NativeBench      (the target is EXCLUDE_FROM_ALL)
//   Starlight.NativeBench [--reps N] [--detail X] [--only substring]
// Every case times an OLD path (A) against the NEW path (B) in alternating order, so load that changes during the run hits both alike,
// and prints median / p95 for each plus the speedup A/B with a 95% bootstrap interval of the ratio of medians. "SLOWER" means the interval
// lies entirely below 1, "faster" entirely above 1, "same" means the interval contains 1 (the difference cannot be told from noise).
// The file also compiles in a checkout from before the changes (features it cannot find are skipped), so the same cases run on the old
// code: build it in both trees and compare the "tree" lines. Nothing here uses the GPU.
#include "flow.hpp"
#include "geometry.hpp"
#include "rock_geometry.hpp"
#include "object_cache.hpp"
#if __has_include("mesh_reduce.hpp")
#include "mesh_reduce.hpp"
#define BENCH_HAVE_MESH_REDUCE 1
#endif
#if __has_include("../../deep/engine/contact_index.h")
#include "../../deep/engine/contact_index.h"
#define BENCH_HAVE_CONTACT_INDEX 1
#endif
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <numeric>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
using namespace starlight::editor;

namespace {
double now_us() {
    static const double perUs = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return double(f.QuadPart) / 1e6; }();
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    return double(q.QuadPart) / perUs;
}
// CPU time of this thread in microseconds, from the cycle counter the scheduler keeps per thread: it does not advance while the thread is
// pre-empted, so a busy machine inflates it only through shared-cache and hyper-thread contention, not through waiting for a turn. The
// cycles-per-microsecond factor is the best ratio seen over a few short spins (an uninterrupted spin shows the true clock).
double cpu_us() {
    static const double perUs = [] {
        double best = 0;
        for (int k = 0; k < 40; ++k) {
            ULONG64 c0, c1; QueryThreadCycleTime(GetCurrentThread(), &c0); const double t0 = now_us();
            while (now_us() - t0 < 1500) {}
            QueryThreadCycleTime(GetCurrentThread(), &c1); best = std::max(best, double(c1 - c0) / (now_us() - t0));
        }
        return best;
    }();
    ULONG64 c; QueryThreadCycleTime(GetCurrentThread(), &c);
    return double(c) / perUs;
}
double percentile(std::vector<double> v, double p) {
    std::sort(v.begin(), v.end());
    const size_t rank = size_t(std::ceil(p * double(v.size())));
    return v[std::clamp<size_t>(rank, 1, v.size()) - 1];
}
double median(const std::vector<double>& v) { return percentile(v, .5); }
struct Options { int reps = 30; std::string only; std::string csv; } options;
int sink_total = 0; // keeps results alive so the optimizer cannot drop the timed work
template <class T> void keep(const T& value) { sink_total += int(reinterpret_cast<const volatile unsigned char&>(value)); }

// Alternates A and B (A first on even reps), each timed as `inner` back-to-back calls, and reports microseconds per call.
// prepare() runs untimed before each timed call and may build per-call state (a copy that will be edited).
struct Case {
    std::string name;
    std::function<void()> prepareA, prepareB, runA, runB;
    int inner = 1;
};
void run_case(const Case& c, const char* aLabel = "old", const char* bLabel = "new") {
    if (!options.only.empty() && c.name.find(options.only) == std::string::npos) return;
    std::vector<double> wallA, wallB;
    double lastWall = 0;
    auto timeOne = [&](const std::function<void()>& prepare, const std::function<void()>& run) {
        if (prepare) prepare();
        const double w0 = now_us(), t0 = cpu_us();
        for (int i = 0; i < c.inner; ++i) run();
        const double t1 = cpu_us();
        lastWall = (now_us() - w0) / c.inner;
        return (t1 - t0) / c.inner;
    };
    for (int w = 0; w < 2; ++w) { timeOne(c.prepareA, c.runA); if (c.runB) timeOne(c.prepareB, c.runB); }
    std::vector<double> a, b;
    for (int r = 0; r < options.reps; ++r) {
        auto doA = [&] { a.push_back(timeOne(c.prepareA, c.runA)); wallA.push_back(lastWall); };
        auto doB = [&] { b.push_back(timeOne(c.prepareB, c.runB)); wallB.push_back(lastWall); };
        if (r % 2 == 0) { doA(); if (c.runB) doB(); } else { if (c.runB) doB(); doA(); }
    }
    if (!options.csv.empty()) {
        if (FILE* f = std::fopen(options.csv.c_str(), "a")) {
            auto dump = [&](const char* label, const std::vector<double>& v) { std::fprintf(f, "%s;%s", c.name.c_str(), label); for (double x : v) std::fprintf(f, ";%.4f", x); std::fprintf(f, "\n"); };
            dump("A", a); if (c.runB) dump("B", b);
            std::fclose(f);
        }
    }
    auto unit = [](double us) { char t[32]; if (us >= 1000) std::snprintf(t, sizeof t, "%8.3f ms", us / 1000); else std::snprintf(t, sizeof t, "%8.2f us", us); return std::string(t); };
    std::printf("%-58s %s: med %s p95 %s [wall med %s]", c.name.c_str(), aLabel, unit(median(a)).c_str(), unit(percentile(a, .95)).c_str(), unit(median(wallA)).c_str());
    if (c.runB) {
        std::mt19937 rng(12345);
        std::vector<double> ratios;
        for (int k = 0; k < 4000; ++k) {
            std::vector<double> ra(a.size()), rb(b.size());
            for (auto& x : ra) x = a[rng() % a.size()];
            for (auto& x : rb) x = b[rng() % b.size()];
            ratios.push_back(median(ra) / median(rb));
        }
        const double lo = percentile(ratios, .025), hi = percentile(ratios, .975), point = median(a) / median(b);
        std::printf(" | %s: med %s p95 %s | speedup x%.3f [%.3f, %.3f] %s\n", bLabel, unit(median(b)).c_str(), unit(percentile(b, .95)).c_str(), point, lo, hi, hi < 1 ? "SLOWER" : lo > 1 ? "faster" : "same");
    } else std::printf(" (single path)\n");
    std::fflush(stdout);
}

struct Rng { uint32_t s; double next() { s = s * 1664525u + 1013904223u; return double(s >> 8) / double(1 << 24); } };
std::vector<Character> make_rocks(int count, uint32_t seed = 4242) {
    Rng r{seed};
    std::vector<Character> rocks;
    for (int i = 0; i < count; ++i) {
        Character c; c.Id = "rock-" + std::to_string(i); c.Shape = "chaos-rock";
        const double size = .5 + r.next() * 1.1;
        c.X = -11 + 22 * r.next(); c.Z = -11 + 22 * r.next(); c.Y = .2 + .5 * r.next();
        c.Width = c.Height = c.Depth = c.Size = size;
        rocks.push_back(c);
    }
    return rocks;
}
size_t triangle_total(const std::vector<Mesh>& meshes) { size_t n = 0; for (const auto& m : meshes) n += m.indices.size() / 3; return n; }

template <class L> concept CanRemove = requires(L l, std::vector<unsigned> v) { l.remove_objects(v); };
template <class L> concept HasDead = requires(L l) { l.triangles[0].dead; };

FlowObstacleMask build_layer(const std::vector<Mesh>& meshes, const std::vector<Character>& rocks, const std::vector<unsigned>& keep, bool reserve) {
    FlowObstacleMask layer;
    if (reserve) { size_t n = 0; for (unsigned i : keep) n += meshes[i].indices.size() / 3; layer.triangles.reserve(n); }
    for (unsigned i : keep) layer.add(meshes[i], rocks[i]);
    layer.finish();
    return layer;
}

void bench_contact_index() {
#ifdef BENCH_HAVE_CONTACT_INDEX
    for (int bodies : {16, 48, 128, 976}) {
        Rng r{777u + uint32_t(bodies)};
        std::vector<ContactIndex::Box> boxes;
        for (int i = 0; i < bodies; ++i) { const float x = float(-12 + 24 * r.next()), z = float(-12 + 24 * r.next()), y = float(r.next()), w = float(.2 + r.next()), h = float(.3 + r.next() * 1.2); boxes.push_back({x - w, y, z - w, x + w, y + h, z + w}); }
        std::vector<ContactIndex::Box> queries; // one 1 m grass patch each
        for (int i = 0; i < 600; ++i) { const float x = float(-12 + 24 * r.next()), z = float(-12 + 24 * r.next()); queries.push_back({x - .5f, 0.f, z - .5f, x + .5f, .6f, z + .5f}); }
        ContactIndex index; index.build(boxes);
        unsigned refTotal = 0, idxTotal = 0;
        for (const auto& q : queries) { refTotal += index.count_reference(q.lowX, q.lowY, q.lowZ, q.highX, q.highY, q.highZ); idxTotal += index.count(q.lowX, q.lowY, q.lowZ, q.highX, q.highY, q.highZ); }
        if (refTotal != idxTotal) { std::printf("MISMATCH contact index %u vs %u\n", refTotal, idxTotal); std::exit(2); }
        char name[96];
        std::snprintf(name, sizeof name, "grass reach: 600 queries, %d bodies (index built)", bodies);
        unsigned total = 0;
        run_case({name, nullptr, nullptr,
                  [&] { for (const auto& q : queries) total += index.count_reference(q.lowX, q.lowY, q.lowZ, q.highX, q.highY, q.highZ); keep(total); },
                  [&] { for (const auto& q : queries) total += index.count(q.lowX, q.lowY, q.lowZ, q.highX, q.highY, q.highZ); keep(total); }, 20}, "brute", "index");
        std::snprintf(name, sizeof name, "grass reach: per frame incl. index build, %d bodies", bodies);
        ContactIndex frameIndex;
        run_case({name, nullptr, nullptr,
                  [&] { for (const auto& q : queries) total += index.count_reference(q.lowX, q.lowY, q.lowZ, q.highX, q.highY, q.highZ); keep(total); },
                  [&] { auto copy = boxes; frameIndex.build(std::move(copy)); for (const auto& q : queries) total += frameIndex.count(q.lowX, q.lowY, q.lowZ, q.highX, q.highY, q.highZ); keep(total); }, 20}, "brute", "index");
    }
#else
    std::printf("contact index: not in this tree\n");
#endif
}

template <class L> void bench_layers(int rockCount) {
    auto rocks = make_rocks(rockCount);
    std::vector<Mesh> meshes; for (const auto& r : rocks) meshes.push_back(object_mesh(r));
    std::vector<unsigned> all(rocks.size()); std::iota(all.begin(), all.end(), 0u);
    std::printf("-- layer with %d rocks, %zu triangles (%.0f per rock)\n", rockCount, triangle_total(meshes), double(triangle_total(meshes)) / rockCount);
    char name[128];
    std::snprintf(name, sizeof name, "layer build %d rocks: add+finish; old=no reserve new=reserve", rockCount);
    run_case({name, nullptr, nullptr, [&] { auto l = build_layer(meshes, rocks, all, false); keep(l.layerRevision); }, [&] { auto l = build_layer(meshes, rocks, all, true); keep(l.layerRevision); }, 1});
    // add() alone and finish() alone
    {
        std::snprintf(name, sizeof name, "layer %d rocks: add() only / finish() only (tree timing)", rockCount);
        std::vector<double> adds, finishes;
        const bool wanted = options.only.empty() || std::string(name).find(options.only) != std::string::npos;
        for (int r = 0; wanted && r < options.reps; ++r) {
            FlowObstacleMask l; size_t n = 0; for (unsigned i : all) n += meshes[i].indices.size() / 3; l.triangles.reserve(n);
            const double t0 = cpu_us(); for (unsigned i : all) l.add(meshes[i], rocks[i]); const double t1 = cpu_us(); l.finish(); const double t2 = cpu_us();
            adds.push_back(t1 - t0); finishes.push_back(t2 - t1); keep(l.layerRevision);
        }
        if (wanted && !options.csv.empty()) if (FILE* f = std::fopen(options.csv.c_str(), "a")) {
            std::fprintf(f, "layer %d rocks: add()-only;A", rockCount); for (double x : adds) std::fprintf(f, ";%.4f", x); std::fprintf(f, "\n");
            std::fprintf(f, "layer %d rocks: finish()-only;A", rockCount); for (double x : finishes) std::fprintf(f, ";%.4f", x); std::fprintf(f, "\n");
            std::fclose(f);
        }
        if (wanted) std::printf("%-58s add() med %.3f ms p95 %.3f ms | finish() med %.3f ms p95 %.3f ms   [tree timing, reserved]\n", name, median(adds) / 1000, percentile(adds, .95) / 1000, median(finishes) / 1000, percentile(finishes, .95) / 1000);
    }
    if constexpr (CanRemove<L>) {
        L base = build_layer(meshes, rocks, all, true);
        std::snprintf(name, sizeof name, "layer copy %d rocks (the copy-on-share cost)", rockCount);
        run_case({name, nullptr, nullptr, [&] { L c(base); keep(c.layerRevision); }, nullptr, 1});
        std::vector<int> goneCounts{1, 8, rockCount * 5 / 100, rockCount * 10 / 100, rockCount * 15 / 100, rockCount * 25 / 100, rockCount * 40 / 100, rockCount * 60 / 100};
        goneCounts.erase(std::remove_if(goneCounts.begin(), goneCounts.end(), [](int n) { return n < 1; }), goneCounts.end());
        std::sort(goneCounts.begin(), goneCounts.end()); goneCounts.erase(std::unique(goneCounts.begin(), goneCounts.end()), goneCounts.end());
        for (int goneCount : goneCounts) {
            std::vector<unsigned> gone; for (int k = 0; k < goneCount; ++k) gone.push_back(unsigned((size_t(k) * 7919 + 13) % rockCount));
            std::sort(gone.begin(), gone.end()); gone.erase(std::unique(gone.begin(), gone.end()), gone.end());
            std::vector<unsigned> keepIds; for (unsigned i : all) if (!std::binary_search(gone.begin(), gone.end(), i)) keepIds.push_back(i);
            std::snprintf(name, sizeof name, "break %zu of %d rocks: rebuild vs remove_objects", gone.size(), rockCount);
            std::unique_ptr<L> work;
            run_case({name, nullptr, [&] { work = std::make_unique<L>(base); },
                      [&] { auto l = build_layer(meshes, rocks, keepIds, true); keep(l.layerRevision); },
                      [&] { work->remove_objects(gone); keep(work->layerRevision); }, 1}, "rebuild", "remove");
        }
        // Query cost afterwards: a layer edited in place keeps old per-cell extents, so queries may take the slower exact path.
        std::vector<unsigned> gone8; for (int k = 0; k < 8; ++k) gone8.push_back(unsigned((size_t(k) * 7919 + 13) % rockCount));
        std::sort(gone8.begin(), gone8.end()); gone8.erase(std::unique(gone8.begin(), gone8.end()), gone8.end());
        std::vector<unsigned> keep8; for (unsigned i : all) if (!std::binary_search(gone8.begin(), gone8.end(), i)) keep8.push_back(i);
        L edited(base); edited.remove_objects(gone8);
        L fresh = build_layer(meshes, rocks, keep8, true);
        Rng r{99};
        std::vector<Vec3> points; for (int i = 0; i < 20000; ++i) points.push_back({-12 + 24 * r.next(), -.5 + 2.5 * r.next(), -12 + 24 * r.next()});
        double acc = 0;
        std::snprintf(name, sizeof name, "queries after 8 removed (%d rocks): 20000 clearance_capped", rockCount);
        run_case({name, nullptr, nullptr, [&] { for (const auto& p : points) acc += fresh.clearance_capped_layer(p, .05); keep(acc); }, [&] { for (const auto& p : points) acc += edited.clearance_capped_layer(p, .05); keep(acc); }, 1}, "fresh", "edited");
        std::snprintf(name, sizeof name, "queries after 8 removed (%d rocks): 20000 clearance", rockCount);
        run_case({name, nullptr, nullptr, [&] { for (const auto& p : points) acc += fresh.clearance_layer(p); keep(acc); }, [&] { for (const auto& p : points) acc += edited.clearance_layer(p); keep(acc); }, 1}, "fresh", "edited");
        int hits = 0;
        std::snprintf(name, sizeof name, "queries after 8 removed (%d rocks): 20000 region_clear", rockCount);
        run_case({name, nullptr, nullptr, [&] { for (const auto& p : points) hits += fresh.region_clear_layer(p.x - .2, p.x + .2, p.z - .2, p.z + .2, p.y - .1, p.y + .1, .05); keep(hits); }, [&] { for (const auto& p : points) hits += edited.region_clear_layer(p.x - .2, p.x + .2, p.z - .2, p.z + .2, p.y - .1, p.y + .1, .05); keep(hits); }, 1}, "fresh", "edited");
        std::snprintf(name, sizeof name, "queries after 8 removed (%d rocks): 5000 clear_triangle", rockCount);
        run_case({name, nullptr, nullptr, [&] { for (int i = 0; i < 5000; ++i) { const Vec3& p = points[i]; hits += fresh.clear_triangle_layer(p, p + Vec3{.2, .0, .0}, p + Vec3{.0, .0, .2}); } keep(hits); }, [&] { for (int i = 0; i < 5000; ++i) { const Vec3& p = points[i]; hits += edited.clear_triangle_layer(p, p + Vec3{.2, .0, .0}, p + Vec3{.0, .0, .2}); } keep(hits); }, 1}, "fresh", "edited");
    } else std::printf("remove_objects: not in this tree\n");
}

void bench_mesh_generation() {
    struct Shape { const char* name; double height; int subdivisions; uint32_t seed; RockForm form; };
    const Shape shapes[] = {{"outcrop", .62, 4, 8241, RockForm::Outcrop}, {"boulder", 1.05, 4, 9173, RockForm::Boulder}, {"crystal", .85, 4, 15317, RockForm::Crystal}, {"pebble", .62, 3, 8241, RockForm::Pebble}};
#ifdef BENCH_HAVE_MESH_REDUCE
    const double detail = mesh_detail();
    std::printf("-- mesh detail in effect: %.2f\n", detail);
    for (const auto& s : shapes) {
        Mesh full = rock_surface(s.height, s.subdivisions, s.seed, s.form);
        Mesh reduced = reduce_mesh(full, detail);
        char name[128]; std::snprintf(name, sizeof name, "mesh %s: %zu -> %zu triangles; old=generate new=generate+reduce", s.name, full.indices.size() / 3, reduced.indices.size() / 3);
        run_case({name, nullptr, nullptr, [&] { auto m = rock_surface(s.height, s.subdivisions, s.seed, s.form); keep(m.vertices.size()); }, [&] { auto m = reduce_mesh(rock_surface(s.height, s.subdivisions, s.seed, s.form), detail); keep(m.vertices.size()); }, 1});
    }
    {
        char name[128]; std::snprintf(name, sizeof name, "mesh rock set (3 rocks + 9 pebbles): old=generate new=+reduce");
        auto generate = [&](bool reduce) {
            size_t tris = 0;
            for (int k = 0; k < 3; ++k) { auto m = rock_surface(shapes[k].height, shapes[k].subdivisions, shapes[k].seed, shapes[k].form); if (reduce) m = reduce_mesh(m, detail); tris += m.indices.size(); }
            for (int i = 0; i < 9; ++i) { auto m = rock_surface(.62, 3, 8241 + i * 7919, RockForm::Pebble); if (reduce) m = reduce_mesh(m, detail); tris += m.indices.size(); }
            return tris;
        };
        const size_t before = generate(false), after = generate(true);
        std::printf("-- built-in rock set: %zu -> %zu triangles (x%.2f)\n", before / 3, after / 3, double(after) / double(before));
        run_case({name, nullptr, nullptr, [&] { keep(generate(false)); }, [&] { keep(generate(true)); }, 1});
    }
    // Grass blade: generation copied from before (7 rings) and after (rings follow the detail), grass_geometry.hpp.
    auto blade = [](int rings) {
        Mesh mesh; uint32_t state = 8241; auto next = [&] { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return double(state & 0xffffff) / 16777216.; }; constexpr double pi = 3.14159265358979323846;
        for (int b = 0; b < 100; ++b) {
            double angle = next() * pi * 2, radius = std::sqrt(next()) * .48, x = std::cos(angle) * radius, z = std::sin(angle) * radius;
            double height = .18 + next() * .42, lean = (next() - .5) * .22, turn = next() * pi * 2, variation = next();
            Vec3 side{std::cos(turn), 0, -std::sin(turn)}, forward{std::sin(turn), 0, std::cos(turn)}; int base = int(mesh.vertices.size());
            for (int ring = 0; ring < rings; ++ring) {
                double t = ring / double(rings), width = .004 * (1 - t) + .018 * std::sin(pi * std::pow(t, .65));
                auto center = Vec3{x + lean * t * t, height * t, z} + forward * (height * .15 * t * t * t);
                for (int column = 0; column < 3; ++column) { double u = column * .5; auto p = center + side * ((u * 2 - 1) * width) + forward * (column == 1 ? width * .28 : 0); mesh.vertices.push_back({p, {}, {u, t, variation}, 1.35f}); }
            }
            auto tip = Vec3{x + lean, height, z} + forward * (height * .15); mesh.vertices.push_back({tip, {}, {.5, 1, variation}, 1.35f});
            for (int ring = 0; ring < rings - 1; ++ring) for (int column = 0; column < 2; ++column) { int a = base + ring * 3 + column, bb = a + 1, c = a + 3, d = c + 1; mesh.indices.insert(mesh.indices.end(), {a, c, bb, bb, c, d}); }
            { const int last = base + (rings - 1) * 3, tipIndex = base + rings * 3; mesh.indices.insert(mesh.indices.end(), {last, tipIndex, last + 1, last + 1, tipIndex, last + 2}); }
        }
        mesh.normals(); return mesh;
    };
    const int rings = detail >= 1 ? 7 : std::clamp(int(std::lround(1 + 6 * detail)), 3, 7);
    char name[128]; std::snprintf(name, sizeof name, "grass blade set (100 blades): %zu -> %zu triangles; 7 rings vs %d", blade(7).indices.size() / 3, blade(rings).indices.size() / 3, rings);
    run_case({name, nullptr, nullptr, [&] { auto m = blade(7); keep(m.vertices.size()); }, [&] { auto m = blade(rings); keep(m.vertices.size()); }, 1}, "7 rings", "reduced");
#else
    std::printf("mesh reduction: not in this tree\n");
    for (const auto& s : shapes) { char name[96]; std::snprintf(name, sizeof name, "mesh %s generate (tree timing)", s.name); run_case({name, nullptr, nullptr, [&] { auto m = rock_surface(s.height, s.subdivisions, s.seed, s.form); keep(m.vertices.size()); }, nullptr, 1}); }
#endif
}

void bench_object_cache(int count) {
    auto rocks = make_rocks(count, 31337);
    for (auto& r : rocks) { ObjectCache::shared().mesh(r); ObjectCache::shared().bounds(r); }
    char name[128];
    std::snprintf(name, sizeof name, "ObjectCache: %d cached mesh() lookups (hit path)", count);
    size_t tris = 0;
    run_case({name, nullptr, nullptr, [&] { for (const auto& r : rocks) tris += ObjectCache::shared().mesh(r)->indices.size(); keep(tris); }, nullptr, 10});
    std::snprintf(name, sizeof name, "ObjectCache: %d cached bounds() lookups (hit path)", count);
    double acc = 0;
    run_case({name, nullptr, nullptr, [&] { for (const auto& r : rocks) acc += ObjectCache::shared().bounds(r).low.x; keep(acc); }, nullptr, 10});
    // buildLayer in native_session.cpp now walks the held objects once more (mesh() per object) to size the reserve: that walk is the mesh() line above.
}

// The per-update signature of the held scene objects (native_session.cpp consider()): before and after R6 (pose quaternion, asset and shape in the signature).
void bench_signature(int count) {
    auto rocks = make_rocks(count, 555);
    for (size_t i = 0; i < rocks.size(); i += 3) rocks[i].PhysicsRotation = std::array<double, 4>{0, .1, 0, .99};
    struct HeldOld { const Character* object; int layer; };
    struct HeldNew { const Character* object; int layer; uint64_t idHash, objectSig; };
    std::vector<HeldOld> heldOld; std::vector<HeldNew> heldNew;
    uint64_t sinkSig = 0;
    auto oldPass = [&] {
        heldOld.clear(); uint64_t heavy = 1469598103934665603ull, light = 1469598103934665603ull;
        for (const auto& source : rocks) {
            const Character* use = &source; const int layer = 1; uint64_t& sig = layer == 0 ? heavy : light;
            auto mix = [&](uint64_t v) { sig ^= v; sig *= 1099511628211ull; };
            mix(std::hash<std::string>{}(source.Id));
            for (double v : {use->X, use->Y, use->Z, use->RotationX, use->RotationY, use->RotationZ, use->Size, use->Width, use->Height, use->Depth}) { uint64_t bits; std::memcpy(&bits, &v, 8); mix(bits); }
            heldOld.push_back({use, layer});
        }
        sinkSig ^= heavy ^ light; keep(sinkSig);
    };
    auto newPass = [&] {
        heldNew.clear(); uint64_t heavy = 1469598103934665603ull, light = 1469598103934665603ull;
        for (const auto& source : rocks) {
            const Character* use = &source; const int layer = 1; uint64_t& sig = layer == 0 ? heavy : light;
            uint64_t own = 1469598103934665603ull;
            auto mix = [&](uint64_t v) { sig ^= v; sig *= 1099511628211ull; own ^= v; own *= 1099511628211ull; };
            const uint64_t idHash = std::hash<std::string>{}(source.Id);
            mix(idHash);
            for (double v : {use->X, use->Y, use->Z, use->RotationX, use->RotationY, use->RotationZ, use->Size, use->Width, use->Height, use->Depth}) { uint64_t bits; std::memcpy(&bits, &v, 8); mix(bits); }
            if (use->PhysicsRotation) for (double v : *use->PhysicsRotation) { uint64_t bits; std::memcpy(&bits, &v, 8); mix(bits); } else mix(0x9e3779b97f4a7c15ull);
            mix(std::hash<std::string>{}(use->Asset)); mix(std::hash<std::string>{}(use->Shape.value_or("")));
            heldNew.push_back({use, layer, idHash, own});
        }
        sinkSig ^= heavy ^ light; keep(sinkSig);
    };
    char name[128]; std::snprintf(name, sizeof name, "obstacle signature pass over %d objects (every update)", count);
    run_case({name, nullptr, nullptr, oldPass, newPass, 20});
}

// event_log.h now(): QueryPerformanceFrequency on every call (old) or once (new).
void bench_event_clock() {
    double acc = 0;
    auto oldNow = [] { LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f); return double(q.QuadPart) * 1000. / double(f.QuadPart); };
    auto newNow = [] { static const double perMs = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return double(f.QuadPart) / 1000.; }(); LARGE_INTEGER q; QueryPerformanceCounter(&q); return double(q.QuadPart) / perMs; };
    run_case({"event log now(): 1000 calls", nullptr, nullptr, [&] { for (int i = 0; i < 1000; ++i) acc += oldNow(); keep(acc); }, [&] { for (int i = 0; i < 1000; ++i) acc += newNow(); keep(acc); }, 5});
}

// Projectile sweep bounds (adventure.hpp): per update, one lookup+store per object (old two lookups and a store) plus a sweep erase of ids not seen (new).
void bench_projectile_bounds(int count) {
    auto rocks = make_rocks(count, 808);
    struct B { Vec3 low, high; };
    std::unordered_map<std::string, B> oldMap; std::unordered_map<std::string, std::pair<B, uint64_t>> newMap; uint64_t pass = 0;
    std::vector<std::pair<std::string, B>> targets; targets.reserve(count * 2);
    for (const auto& r : rocks) { oldMap[r.Id] = {}; newMap[r.Id] = {{}, 0}; }
    auto oldUpdate = [&] {
        targets.clear();
        for (const auto& r : rocks) { B after{{r.X, r.Y, r.Z}, {r.X + 1, r.Y + 1, r.Z + 1}}; auto before = oldMap.contains(r.Id) ? oldMap.at(r.Id) : after; targets.push_back({r.Id, before}); oldMap[r.Id] = after; }
        keep(targets.size());
    };
    auto newUpdate = [&] {
        targets.clear(); const uint64_t p = ++pass;
        for (const auto& r : rocks) { B after{{r.X, r.Y, r.Z}, {r.X + 1, r.Y + 1, r.Z + 1}}; auto [it, fresh] = newMap.try_emplace(r.Id, after, p); targets.push_back({r.Id, fresh ? after : it->second.first}); it->second = {after, p}; }
        std::erase_if(newMap, [&](const auto& entry) { return entry.second.second != p; });
        keep(targets.size());
    };
    char name[128]; std::snprintf(name, sizeof name, "projectile sweep bounds bookkeeping, %d objects per update", count);
    run_case({name, nullptr, nullptr, oldUpdate, newUpdate, 10});
}

// liquid_top_cells (flow.hpp): bed+depth before, drawn surface after. Same code in both trees; compare the tree lines.
void bench_liquid_top() {
    auto flow = std::make_unique<TerrainFlow>(TerrainData{}, std::vector<LiquidSource>{});
    for (int z = 0; z < 48; ++z) for (int x = 0; x < 48; ++x) if ((x / 6 + z / 6) % 2 == 0) flow->water[z * 48 + x] = .3;
    size_t acc = 0;
    run_case({"liquid_top_cells(margin 2), half the cells wet (tree timing)", nullptr, nullptr, [&] { auto v = flow->liquid_top_cells(2); acc += v.size(); keep(acc); }, nullptr, 20});
}
} // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--reps") && i + 1 < argc) {
            char *end = nullptr;
            const long reps = std::strtol(argv[++i], &end, 10);
            if (end == argv[i] || *end || reps < 1 || reps > 1000000) {
                std::fprintf(stderr, "Invalid --reps value\n");
                return 2;
            }
            options.reps = std::max(5, int(reps));
        }
        else if (!std::strcmp(argv[i], "--only") && i + 1 < argc) options.only = argv[++i];
        else if (!std::strcmp(argv[i], "--csv") && i + 1 < argc) options.csv = argv[++i];
        else if (!std::strcmp(argv[i], "--detail") && i + 1 < argc) _putenv_s("STARLIGHT_MESH_DETAIL", argv[++i]);
        else {
            std::fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return 2;
        }
    }
    std::printf("Starlight.NativeBench: %d repetitions per case, A/B alternated, thread CPU microseconds per call, wall median in brackets (machine load is whatever it is: this is NOT a quiet-machine run)\n", options.reps);
    bench_contact_index();
    bench_layers<FlowObstacleMask>(100);
    bench_layers<FlowObstacleMask>(976);
    bench_mesh_generation();
    bench_object_cache(976);
    bench_signature(976);
    bench_event_clock();
    bench_projectile_bounds(1000);
    bench_liquid_top();
    std::printf("done (%d)\n", sink_total & 1);
    return 0;
}
