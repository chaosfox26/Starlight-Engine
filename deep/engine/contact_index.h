#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Counts the boxes that touch a query box, without testing every box: boxes are bucketed by 1 m cells of the x/z plane (24 x 24 cells over -12..12, indices clamped
// so anything outside shares the border cells). The answer is exactly what a loop over all boxes gives; only the work changes.
struct ContactIndex {
    struct Box { float lowX, lowY, lowZ, highX, highY, highZ; };
    static constexpr int grid = 24;
    float padXZ = .2f, padY = .1f; // a box touches the query when the query reaches within these margins of it
    std::vector<Box> boxes;
    std::vector<std::vector<uint32_t>> buckets = std::vector<std::vector<uint32_t>>(size_t(grid) * grid);
    std::vector<uint32_t> stamp;
    uint32_t epoch = 0;
    static int cell(float v) {
        if (!(v == v)) return 0;
        return int(std::clamp(std::floor(v + 12.f), 0.f, float(grid - 1))); // clamped first so a huge or infinite value never reaches the int conversion
    }
    void build(std::vector<Box> next) {
        boxes = std::move(next);
        for (auto& bucket : buckets) bucket.clear();
        stamp.assign(boxes.size(), 0);
        epoch = 0;
        for (uint32_t i = 0; i < boxes.size(); ++i) {
            const Box& b = boxes[i];
            for (int z = cell(b.lowZ - padXZ); z <= cell(b.highZ + padXZ); ++z)
                for (int x = cell(b.lowX - padXZ); x <= cell(b.highX + padXZ); ++x) buckets[size_t(z) * grid + x].push_back(i);
        }
    }
    static bool touches(const Box& b, float lx, float ly, float lz, float hx, float hy, float hz, float padXZ, float padY) {
        return lx <= b.highX + padXZ && hx >= b.lowX - padXZ && lz <= b.highZ + padXZ && hz >= b.lowZ - padXZ && ly <= b.highY + padY && hy >= b.lowY - padY;
    }
    unsigned count(float lx, float ly, float lz, float hx, float hy, float hz) {
        if (boxes.empty()) return 0;
        if (++epoch == 0) {
            std::fill(stamp.begin(), stamp.end(), 0u);
            epoch = 1;
        }
        unsigned total = 0;
        for (int z = cell(lz); z <= cell(hz); ++z)
            for (int x = cell(lx); x <= cell(hx); ++x)
                for (uint32_t i : buckets[size_t(z) * grid + x]) {
                    if (stamp[i] == epoch) continue;
                    stamp[i] = epoch;
                    if (touches(boxes[i], lx, ly, lz, hx, hy, hz, padXZ, padY)) ++total;
                }
        return total;
    }
    // The plain loop over every box, kept for the checks.
    unsigned count_reference(float lx, float ly, float lz, float hx, float hy, float hz) const {
        unsigned total = 0;
        for (const Box& b : boxes)
            if (touches(b, lx, ly, lz, hx, hy, hz, padXZ, padY)) ++total;
        return total;
    }
};
