#pragma once
#include "../../deep/src/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace starlight::editor {
using json = nlohmann::json;
inline int native_int(const json &value) {
    if (!value.is_number_integer() || value.get<double>() < -2147483648. || value.get<double>() > 2147483647.)
        throw std::runtime_error("Expected a 32-bit integer");
    return value.get<int>();
}
std::string new_id();
struct SculptMesh {
    std::vector<double> Positions;
    std::vector<int> Indices;
    void validate() const;
};
struct TerrainData {
    static constexpr int N = 97;
    static constexpr double Step = .25, Half = 12;
    std::vector<double> Heights = std::vector<double>(N * N);
    std::vector<int> Paint = std::vector<int>(N * N);
    bool Lava = false, WaterEnabled = false;
    double WaterLevel = -.5;
    void validate() const;
    double height_at(double x, double z) const;
    void brush(double x, double z, double radius, double strength, std::string_view tool, int paint,
               double target, double seconds);
};
struct LiquidSource {
    double X = 0, Z = 0;
    bool Lava = false;
    double Volume = 2;
};
void to_json(json &, const SculptMesh &);
void from_json(const json &, SculptMesh &);
void to_json(json &, const TerrainData &);
void from_json(const json &, TerrainData &);
void to_json(json &, const LiquidSource &);
void from_json(const json &, LiquidSource &);
struct WalkPoint {
    double X = {};
    double Z = {};
};
void to_json(json &, const WalkPoint &);
void from_json(const json &, WalkPoint &);
struct Character {
    std::string Id = new_id();
    std::string Name = "Character";
    std::string Asset = "";
    std::optional<std::string> Shape = {};
    std::optional<SculptMesh> Sculpt = {};
    std::optional<std::array<double, 4>> PhysicsRotation;
    double TargetX = 1;
    double TargetY = .05;
    double TargetZ = 0;
    double BranchSpread = 1;
    bool Dynamic = false;
    double Density = 2700;
    int Surface = {};
    double LightCone = 180;
    double LightIntensity = 12;
    double LightRange = 6;
    double Y = {};
    double RotationX = {};
    double RotationY = {};
    double RotationZ = {};
    double Width = 1;
    double Height = 1;
    double Depth = 1;
    bool KeepAboveFloor = true;
    std::string Color = "#B7C6E0";
    double X = {};
    double Z = {};
    double Size = 1;
    double Speed = 1.5;
    std::vector<WalkPoint> Route = {};
    WalkPoint at(double seconds) const;
    void validate() const;
};
void to_json(json &, const Character &);
void from_json(const json &, Character &);
struct SceneData {
    double Hour = 15;
    bool DayCycle = false;
    double DayLength = 180;
    double FogDensity = .018;
    bool RayReconstructionEnabled = true;
    bool RaytracingEnabled = false;
    int RaytracingQuality = 1;
    int PathBounces = 3;
    bool FreezeRaytracingAnimation = false;
    int SchemaVersion = 7;
    std::string Name = "My first scene";
    std::vector<Character> Characters = {};
    std::vector<LiquidSource> LiquidSources = {};
    std::optional<TerrainData> Terrain = {};
    std::string GroundColor = "#182033";
    void validate() const;
    std::string encode() const;
    static SceneData decode(std::string_view);
};
void to_json(json &, const SceneData &);
void from_json(const json &, SceneData &);
inline const std::vector<std::string> shapes = {"cube",       "rectangle",   "trapezoid",   "octagon",
                                                "fire",       "light",       "lava",        "rock",
                                                "boulder",    "pebbles",     "grass",       "chaos-lightning",
                                                "chaos-rock", "light-shard", "craxis-smoke"};
void configure(Character &, double, double, double, double);
} // namespace starlight::editor
