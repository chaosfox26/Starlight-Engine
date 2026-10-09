#pragma once
#include <cstdint>

// Conservative visibility test for an axis-aligned box against the clip volume (-w <= x <= w, -w <= y <= w, 0 <= z <= w).
// The box is reported outside only when all eight corners lie beyond the same clip plane. Clip space is linear in the
// corners, so their hull is the transformed box and such a box cannot touch the volume. Nothing is reported outside
// that could be visible, whatever the sign of w. m holds 16 floats in XMFLOAT4X4 layout, applied as row vector times m.
struct ClipBox {
    static bool outside(const float* low, const float* high, const float* m) {
        unsigned outsideBits = 63;
        for (int i = 0; i < 8; ++i) {
            const float x = i & 1 ? high[0] : low[0], y = i & 2 ? high[1] : low[1], z = i & 4 ? high[2] : low[2];
            const float px = x * m[0] + y * m[4] + z * m[8] + m[12];
            const float py = x * m[1] + y * m[5] + z * m[9] + m[13];
            const float pz = x * m[2] + y * m[6] + z * m[10] + m[14];
            const float pw = x * m[3] + y * m[7] + z * m[11] + m[15];
            const unsigned bits = (px < -pw ? 1u : 0u) | (px > pw ? 2u : 0u) | (py < -pw ? 4u : 0u) | (py > pw ? 8u : 0u) | (pz < 0 ? 16u : 0u) | (pz > pw ? 32u : 0u);
            outsideBits &= bits;
        }
        return outsideBits != 0;
    }
};
