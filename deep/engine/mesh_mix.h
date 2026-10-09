#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Folds bytes into a 64-bit identity hash eight bytes per step; a partial tail is folded byte by byte. For a fixed
// running hash each step is a bijection of its input, so changing any single byte always changes the result.
inline uint64_t mesh_mix_bytes(uint64_t hash, const void* bytes, size_t count) {
    const auto ptr = static_cast<const uint8_t*>(bytes);
    size_t j = 0;
    for (; j + 8 <= count; j += 8) {
        uint64_t word;
        std::memcpy(&word, ptr + j, 8);
        hash = (hash ^ word) * 0x9e3779b97f4a7c15ull;
        hash ^= hash >> 29;
    }
    for (; j < count; ++j) {
        hash ^= ptr[j];
        hash *= 1099511628211ull;
    }
    return hash;
}
