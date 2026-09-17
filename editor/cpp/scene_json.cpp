#include "platform.hpp"
#include "scene.hpp"
namespace starlight::editor {
void to_json(json &j, const WalkPoint &v) {
    j = json::object();
    j["X"] = v.X;
    j["Z"] = v.Z;
}
void from_json(const json &j, WalkPoint &v) {
    if (!j.is_object())
        throw std::runtime_error("Expected WalkPoint object");
    if (j.contains("X"))
        j.at("X").get_to(v.X);
    if (j.contains("Z"))
        j.at("Z").get_to(v.Z);
}
void to_json(json &j, const Character &v) {
    j = json::object();
    j["Id"] = v.Id;
    j["Name"] = v.Name;
    j["Asset"] = v.Asset;
    j["Shape"] = v.Shape ? json(*v.Shape) : json(nullptr);
    j["Sculpt"] = v.Sculpt ? json(*v.Sculpt) : json(nullptr);
    j["TargetX"] = v.TargetX;
    j["TargetY"] = v.TargetY;
    j["TargetZ"] = v.TargetZ;
    j["BranchSpread"] = v.BranchSpread;
    j["Dynamic"] = v.Dynamic;
    j["Density"] = v.Density;
    j["Surface"] = v.Surface;
    j["LightCone"] = v.LightCone;
    j["LightIntensity"] = v.LightIntensity;
    j["LightRange"] = v.LightRange;
    j["Y"] = v.Y;
    j["RotationX"] = v.RotationX;
    j["RotationY"] = v.RotationY;
    j["RotationZ"] = v.RotationZ;
    j["Width"] = v.Width;
    j["Height"] = v.Height;
    j["Depth"] = v.Depth;
    j["KeepAboveFloor"] = v.KeepAboveFloor;
    j["Color"] = v.Color;
    j["X"] = v.X;
    j["Z"] = v.Z;
    j["Size"] = v.Size;
    j["Speed"] = v.Speed;
    j["Route"] = v.Route;
}
void from_json(const json &j, Character &v) {
    if (!j.is_object())
        throw std::runtime_error("Expected Character object");
    if (j.contains("Id"))
        j.at("Id").get_to(v.Id);
    if (j.contains("Name"))
        j.at("Name").get_to(v.Name);
    if (j.contains("Asset"))
        j.at("Asset").get_to(v.Asset);
    if (j.contains("Shape")) {
        if (j.at("Shape").is_null())
            v.Shape.reset();
        else
            v.Shape = j.at("Shape").get<std::string>();
    }
    if (j.contains("Sculpt")) {
        if (j.at("Sculpt").is_null())
            v.Sculpt.reset();
        else
            v.Sculpt = j.at("Sculpt").get<SculptMesh>();
    }
    if (j.contains("TargetX"))
        j.at("TargetX").get_to(v.TargetX);
    if (j.contains("TargetY"))
        j.at("TargetY").get_to(v.TargetY);
    if (j.contains("TargetZ"))
        j.at("TargetZ").get_to(v.TargetZ);
    if (j.contains("BranchSpread"))
        j.at("BranchSpread").get_to(v.BranchSpread);
    if (j.contains("Dynamic"))
        j.at("Dynamic").get_to(v.Dynamic);
    if (j.contains("Density"))
        j.at("Density").get_to(v.Density);
    if (j.contains("Surface"))
        v.Surface = native_int(j.at("Surface"));
    if (j.contains("LightCone"))
        j.at("LightCone").get_to(v.LightCone);
    if (j.contains("LightIntensity"))
        j.at("LightIntensity").get_to(v.LightIntensity);
    if (j.contains("LightRange"))
        j.at("LightRange").get_to(v.LightRange);
    if (j.contains("Y"))
        j.at("Y").get_to(v.Y);
    if (j.contains("RotationX"))
        j.at("RotationX").get_to(v.RotationX);
    if (j.contains("RotationY"))
        j.at("RotationY").get_to(v.RotationY);
    if (j.contains("RotationZ"))
        j.at("RotationZ").get_to(v.RotationZ);
    if (j.contains("Width"))
        j.at("Width").get_to(v.Width);
    if (j.contains("Height"))
        j.at("Height").get_to(v.Height);
    if (j.contains("Depth"))
        j.at("Depth").get_to(v.Depth);
    if (j.contains("KeepAboveFloor"))
        j.at("KeepAboveFloor").get_to(v.KeepAboveFloor);
    if (j.contains("Color"))
        j.at("Color").get_to(v.Color);
    if (j.contains("X"))
        j.at("X").get_to(v.X);
    if (j.contains("Z"))
        j.at("Z").get_to(v.Z);
    if (j.contains("Size"))
        j.at("Size").get_to(v.Size);
    if (j.contains("Speed"))
        j.at("Speed").get_to(v.Speed);
    if (j.contains("Route"))
        j.at("Route").get_to(v.Route);
}
void to_json(json &j, const SceneData &v) {
    j = json::object();
    j["Hour"] = v.Hour;
    j["DayCycle"] = v.DayCycle;
    j["DayLength"] = v.DayLength;
    j["FogDensity"] = v.FogDensity;
    j["RayReconstructionEnabled"] = v.RayReconstructionEnabled;
    j["RaytracingEnabled"] = v.RaytracingEnabled;
    j["RaytracingQuality"] = v.RaytracingQuality;
    j["PathBounces"] = v.PathBounces;
    j["FreezeRaytracingAnimation"] = v.FreezeRaytracingAnimation;
    j["SchemaVersion"] = v.SchemaVersion;
    j["Name"] = v.Name;
    j["Characters"] = v.Characters;
    j["LiquidSources"] = v.LiquidSources;
    j["Terrain"] = v.Terrain ? json(*v.Terrain) : json(nullptr);
    j["GroundColor"] = v.GroundColor;
}
void from_json(const json &j, SceneData &v) {
    if (!j.is_object())
        throw std::runtime_error("Expected SceneData object");
    if (j.contains("Hour"))
        j.at("Hour").get_to(v.Hour);
    if (j.contains("DayCycle"))
        j.at("DayCycle").get_to(v.DayCycle);
    if (j.contains("DayLength"))
        j.at("DayLength").get_to(v.DayLength);
    if (j.contains("FogDensity"))
        j.at("FogDensity").get_to(v.FogDensity);
    if (j.contains("RayReconstructionEnabled"))
        j.at("RayReconstructionEnabled").get_to(v.RayReconstructionEnabled);
    if (j.contains("RaytracingEnabled"))
        j.at("RaytracingEnabled").get_to(v.RaytracingEnabled);
    if (j.contains("RaytracingQuality"))
        v.RaytracingQuality = native_int(j.at("RaytracingQuality"));
    if (j.contains("PathBounces"))
        v.PathBounces = native_int(j.at("PathBounces"));
    if (j.contains("FreezeRaytracingAnimation"))
        j.at("FreezeRaytracingAnimation").get_to(v.FreezeRaytracingAnimation);
    if (j.contains("SchemaVersion"))
        v.SchemaVersion = native_int(j.at("SchemaVersion"));
    if (j.contains("Name"))
        j.at("Name").get_to(v.Name);
    if (j.contains("Characters"))
        j.at("Characters").get_to(v.Characters);
    if (j.contains("LiquidSources"))
        j.at("LiquidSources").get_to(v.LiquidSources);
    if (j.contains("Terrain")) {
        if (j.at("Terrain").is_null())
            v.Terrain.reset();
        else
            v.Terrain = j.at("Terrain").get<TerrainData>();
    }
    if (j.contains("GroundColor"))
        j.at("GroundColor").get_to(v.GroundColor);
}
} // namespace starlight::editor
