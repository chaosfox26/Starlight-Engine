#pragma once
#include <atomic>
#include <cstdint>
#include <string>

namespace starlight::texture_limits {

inline constexpr uint32_t MaxSide = 16384;
inline constexpr uint64_t MaxPngPixelStreamBytes = 1ull << 30;
inline constexpr uint64_t DefaultDecodedBudgetBytes = 1ull << 30;
inline constexpr uint32_t DecodedChannels = 4;
inline constexpr uint32_t DecodedBytesPerChannel = 1;
inline constexpr const char* CacheRootFolder = "AssetCache";
inline constexpr const char* TextureCacheFolder = "textures";

inline std::atomic<uint64_t>& decodedBudgetCell() {
    static std::atomic<uint64_t> budget{DefaultDecodedBudgetBytes};
    return budget;
}
inline uint64_t decodedBudget() { return decodedBudgetCell().load(std::memory_order_relaxed); }
inline void setDecodedBudget(uint64_t bytes) { decodedBudgetCell().store(bytes ? bytes : DefaultDecodedBudgetBytes, std::memory_order_relaxed); }

inline bool multiplyChecked(uint64_t a, uint64_t b, uint64_t& out) {
    if (a && b > UINT64_MAX / a) return false;
    out = a * b;
    return true;
}
inline bool addChecked(uint64_t a, uint64_t b, uint64_t& out) {
    if (b > UINT64_MAX - a) return false;
    out = a + b;
    return true;
}

enum class Verdict { Ok, EmptySide, SideTooLarge, BadChannels, BytesOverflow, OverBudget };

struct Check {
    Verdict verdict = Verdict::Ok;
    uint64_t bytes = 0;
    bool ok() const { return verdict == Verdict::Ok; }
};

inline Check checkImage(uint64_t width, uint64_t height, uint64_t channels, uint64_t bytesPerChannel, uint64_t budget) {
    Check result;
    if (!width || !height) { result.verdict = Verdict::EmptySide; return result; }
    if (width > MaxSide || height > MaxSide) { result.verdict = Verdict::SideTooLarge; return result; }
    if (channels < 1 || channels > 4 || bytesPerChannel < 1 || bytesPerChannel > 2) { result.verdict = Verdict::BadChannels; return result; }
    uint64_t pixels = 0, bytes = 0;
    if (!multiplyChecked(width, height, pixels) || !multiplyChecked(pixels, channels, bytes) || !multiplyChecked(bytes, bytesPerChannel, bytes)) {
        result.verdict = Verdict::BytesOverflow;
        return result;
    }
    result.bytes = bytes;
    if (bytes > budget) result.verdict = Verdict::OverBudget;
    return result;
}
inline Check checkImage(uint64_t width, uint64_t height, uint64_t channels, uint64_t bytesPerChannel) {
    return checkImage(width, height, channels, bytesPerChannel, decodedBudget());
}
inline Check checkDecodedRgba8(uint64_t width, uint64_t height) {
    return checkImage(width, height, DecodedChannels, DecodedBytesPerChannel, decodedBudget());
}

inline uint64_t mipChainBytes(uint64_t width, uint64_t height, uint64_t bytesPerPixel) {
    uint64_t total = 0;
    for (;;) {
        uint64_t level = 0;
        if (!multiplyChecked(width * height, bytesPerPixel, level) || !addChecked(total, level, total)) return UINT64_MAX;
        if (width <= 1 && height <= 1) return total;
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
}

inline std::string explain(const Check& check, uint64_t width, uint64_t height, uint64_t channels, uint64_t bytesPerChannel, uint64_t budget) {
    const std::string size = std::to_string(width) + "x" + std::to_string(height);
    const std::string never = "Starlight never downsamples an image silently, so reduce it or split it into tiles before importing";
    switch (check.verdict) {
    case Verdict::Ok: return std::string();
    case Verdict::EmptySide: return "the image is " + size + " and both sides must be at least 1 pixel";
    case Verdict::SideTooLarge:
        return "the image is " + size + ", above the " + std::to_string(MaxSide) + " pixel limit per side. " + never +
               " (limits of the graphics device are checked later, when a texture is created)";
    case Verdict::BadChannels:
        return "the image declares " + std::to_string(channels) + " channels of " + std::to_string(bytesPerChannel) + " bytes, outside 1 to 4 channels of 1 or 2 bytes";
    case Verdict::BytesOverflow:
        return "the image is " + size + " with " + std::to_string(channels) + " channels of " + std::to_string(bytesPerChannel) + " bytes and its decoded size overflows a 64 bit count";
    case Verdict::OverBudget:
        return "decoding the " + size + " image needs " + std::to_string(check.bytes) + " bytes (" + std::to_string(channels) + " channels of " + std::to_string(bytesPerChannel) +
               " bytes), above the decoded-memory budget of " + std::to_string(budget) + " bytes. " + never;
    }
    return std::string();
}
inline std::string explain(const Check& check, uint64_t width, uint64_t height, uint64_t channels, uint64_t bytesPerChannel) {
    return explain(check, width, height, channels, bytesPerChannel, decodedBudget());
}

}
