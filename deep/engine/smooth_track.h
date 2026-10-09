#pragma once
#include <cstdint>
#include <unordered_map>

// ---- per-entity smoothed transform (interpolation between snapshots) ---------------------------
// 'seen' is the frame the entry was last tracked; entries nobody has tracked for a long while belong to ids that left the world.
struct SmoothXf { float x = 0, z = 0, y = 0, yaw = 0; bool init = false; uint32_t seen = 0; };

constexpr uint32_t kSmoothSweepFrames = 600;     // how often the map is swept
constexpr uint32_t kSmoothMaxAgeFrames = 3600;   // untouched for this many frames = gone (seconds of wall time at any playable frame rate)

// Removes entries not tracked within kSmoothMaxAgeFrames of 'frame'. Entries tracked this frame are never removed, so references held by callers stay valid.
inline void pruneSmooth(std::unordered_map<uint64_t, SmoothXf>& map, uint32_t frame) {
    for (auto it = map.begin(); it != map.end();) {
        if (uint32_t(frame - it->second.seen) > kSmoothMaxAgeFrames) it = map.erase(it); else ++it;
    }
}
