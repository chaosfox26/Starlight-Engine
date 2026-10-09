#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "host_readiness.h"

namespace keycard {

struct Rgb { float r = 0, g = 0, b = 0; };

struct Part {
    std::string id, shape, group;
    float origin[3]{}, axisX[3]{}, axisY[3]{}, axisZ[3]{};
    Rgb color;
    float glow = 0;
    float ringInner = 0.82f;
};

struct Follow {
    float offsetX = 0, offsetZ = 0, lag = 3, wobble = 0, poolRadius = 1.5f;
    bool housing = false;
    float housingSize[3]{ 0.6f, 0.4f, 0.6f };
    Rgb housingColor;
    bool beam = false;
    float haze = 0.07f, pool = 0.3f, innerPool = 0.45f;
    float topY = 0, restRangeRatio = 1.6f;
};

struct Light {
    std::string id;
    float position[3]{}, direction[3]{ 0, -1, 0 };
    Rgb color;
    float intensity = 12, range = 6, coneDegrees = 180, innerRatio = 0.8f;
    bool follow = false;
    Follow rig;
};

struct Lighting {
    bool present = false;
    bool authored = true;
    float keyDirection[3]{ 0, -1, 0 };
    float keyIntensity = 1;
    float keyIntensityFlat = -1;
    Rgb ambient{ 0.5f, 0.5f, 0.5f };
    float ambientGround = 0.5f;
    Rgb clear{ 0.05f, 0.05f, 0.08f };
    float fogMix = 0.35f;
    std::map<std::string, std::map<std::string, float>> modulators;
    float modulator(const std::string& name, const std::string& target) const {
        const auto set = modulators.find(name);
        if (set == modulators.end()) return 0;
        const auto value = set->second.find(target);
        return value == set->second.end() ? 0.0f : value->second;
    }
};

struct Mood {
    std::string id;
    Rgb clear;
    bool hasAmbient = false, hasAmbientGround = false, hasKeyIntensity = false, hasFogMix = false;
    Rgb ambient;
    float ambientGround = 0, keyIntensity = 0, fogMix = 0;
};

struct View {
    bool present = false;
    float fovDegrees = 50, nearPlane = 0.1f, farPlane = 400, orbitRate = 1.6f;
    bool farRule = false;
    float farDistance = 30, farPerDistance = 4, farBase = 800;
    bool follow = false;
    float targetHeight = 1.1f, targetHeightPerY = 0.5f, defaultDistance = 9, defaultHeight = 5;
    bool idleOrbit = false;
    float idleDelaySeconds = 4, idlePeriodSeconds = 32;
};

struct Zone {
    std::string id, kind, shape, note;
    float x = 0, z = 0, radius = 0, width = 0, depth = 0, height = 0;
    bool contains(float px, float pz) const {
        if (shape == "box") return px >= x - width * 0.5f && px <= x + width * 0.5f && pz >= z - depth * 0.5f && pz <= z + depth * 0.5f;
        const float dx = px - x, dz = pz - z;
        return dx * dx + dz * dz < radius * radius;
    }
};

struct Crowd {
    bool present = false;
    std::vector<int> grids{ 18, 9 }, enterPx{ 65, 15 }, exitPx{ 78, 19 };
    int variants = 4;
    float cullCentreHeight = 0.9f, cullRadius = 1.35f;
};

struct FurProfile { float length = 0.012f, softness = 0.5f, stiffness = 0.5f, sheen = 0.5f, sway = 0.5f, density = 1; };

struct Effect {
    std::vector<std::string> maps;
    std::map<std::string, FurProfile> profiles;
};

struct CastModel {
    std::string id, modelAsset, role;
    int variants = 1;
    bool hasRig = false;
    std::map<std::string, std::string> clips, joints;
    bool hasHeightRule = false;
    float acceptMin = 1.4f, acceptMax = 2.4f, targetHeight = 1.8f;
    std::string uvLayout = "glb", uvAsset;
    std::map<std::string, std::map<std::string, std::string>> maps;
    std::map<std::string, Effect> effects;
    std::vector<std::string> requiredItems, wantedItems;
    bool declaresRequired(const std::string& item) const { return listed(requiredItems, item); }
    bool declaresWanted(const std::string& item) const { return listed(wantedItems, item); }
    static bool listed(const std::vector<std::string>& list, const std::string& item) {
        for (const std::string& entry : list) if (entry == item) return true;
        return false;
    }
};

struct KeyCard {
    std::string name, path;
    std::vector<Part> parts;
    std::vector<Light> lights;
    Lighting lighting;
    std::vector<Mood> moods;
    std::string activeMood;
    View view;
    std::vector<Zone> zones;
    Crowd crowd;
    std::vector<CastModel> cast;
    bool hasSpawn = false;
    float spawn[3]{};
    std::vector<std::string> skipped;
    const Mood* mood(const std::string& id) const {
        for (const Mood& entry : moods) if (entry.id == id) return &entry;
        return nullptr;
    }
    const Zone* zone(const std::string& kind) const {
        for (const Zone& entry : zones) if (entry.kind == kind) return &entry;
        return nullptr;
    }
};

enum class AssetClass { Model, Texture, Blob };

struct AssetBytes {
    std::shared_ptr<const void> keep;
    const uint8_t* data = nullptr;
    size_t size = 0;
    std::string blobType;
    uint32_t blobVersion = 0;
};

struct LibraryStatus {
    std::string description;
    std::string problems;
};

std::string loadKeyCard(const std::string& cardPath, KeyCard& out);
bool assetPresent(AssetClass kind, const std::string& id, std::string& why);
std::string fetchAsset(AssetClass kind, const std::string& id, AssetBytes& out);
LibraryStatus libraryStatus();
void releaseLibraryCache();

// Objects the card holds but this host cannot draw (models, sculpts, other shapes) become one Degraded readiness item with the count and the first few names,
// so a card that is partly missing never reads as fully loaded (audit 04 finding 7). The item is Optional: a card that loads today still loads.
inline void reportSkippedContent(const KeyCard& card, host_readiness::Readiness& ready, const std::string& cardFile) {
    if (card.skipped.empty()) return;
    std::string detail = std::to_string(card.skipped.size()) + (card.skipped.size() == 1 ? " object is missing from the scene: " : " objects are missing from the scene: ");
    for (size_t i = 0; i < card.skipped.size() && i < 5; ++i) detail += (i ? "; " : "") + card.skipped[i];
    if (card.skipped.size() > 5) detail += "; and " + std::to_string(card.skipped.size() - 5) + " more";
    ready.markDegraded(ready.require("key card content", "objects this host cannot draw", cardFile, host_readiness::Need::Optional), detail);
}

// Every path string in the key card interface is UTF-8, whatever the console code page.
inline std::filesystem::path pathFromUtf8(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
inline std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
inline std::string resolveCardPath(const std::string& requested, const std::string& moduleFolder, const std::string& fileName, std::string& error) {
    namespace fs = std::filesystem;
    std::error_code ignored;
    if (!requested.empty()) {
        if (fs::is_regular_file(pathFromUtf8(requested), ignored)) return requested;
        error = "The key card named on the command line does not exist: " + requested;
        return std::string();
    }
    const fs::path beside = pathFromUtf8(moduleFolder) / pathFromUtf8(fileName);
    if (fs::is_regular_file(beside, ignored)) return pathToUtf8(beside);
    error = "No key card was found. Pass --card <file.starscene>, or place " + fileName + " beside the host module (" + moduleFolder + "). The starter card ships with the engine under starters.";
    return std::string();
}

}
