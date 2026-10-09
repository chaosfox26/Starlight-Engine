#pragma once
#include <cmath>

// per-entity smoothed transform (interpolation between snapshots)
struct SmoothXf {
    float x = 0, z = 0, y = 0, yaw = 0; bool init = false;
    // Exponential smoothing toward a snapshot target. A non-finite target leaves the state untouched (RND-1), so one bad
    // snapshot value can neither poison the entity for good nor hang the yaw wrap.
    void follow(float tx, float tz, float ty, float tyaw, float a) {
        if (!std::isfinite(tx) || !std::isfinite(tz) || !std::isfinite(ty) || !std::isfinite(tyaw)) return;
        if (!init) { x = tx; z = tz; y = ty; yaw = tyaw; init = true; return; }
        x += (tx - x) * a; z += (tz - z) * a; y += (ty - y) * a;
        const float kTwoPi = 6.28318530717958647692f;
        yaw += std::remainder(tyaw - yaw, kTwoPi) * a;
    }
};
