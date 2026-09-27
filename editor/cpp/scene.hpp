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
    double CombatSightRange=60;
    double CombatHealth=0; // Zero keeps ordinary scenery out of actor combat.
    int CombatTeam=2; // Viewer/player team is 1; 0 is neutral.
    bool CombatBoss=false;
    bool CombatAutoAttack=false;
    double CombatProjectileSpeed=0,CombatProjectileGravity=0,CombatProjectileMass=.02;
    bool CombatPursuit=false;
    int GrappleMode=0; // Auto, wall attachment, or swing.
    int CombatControlEffect=-1; // None, or shared ControlEffect index.
    std::string GameplayRole="none", GameplayKey="";
    std::string Id = new_id();
    std::string Name = "Character";
    std::string Asset = "";
    std::optional<std::string> Shape = {};
    std::optional<SculptMesh> Sculpt = {};
    std::optional<std::array<double, 4>> PhysicsRotation;
    std::string LightningTargetId;
    double LightningDamage=0,LightningChainRadius=3;
    int LightningChains=0;
    double TargetX = 1;
    double TargetY = .05;
    double TargetZ = 0;
    double BranchSpread = 1;
    double ParticleRate=40;
    double ParticleLifetime=1;
    double ParticleDrag=0.6;
    double ParticleLift=14;
    double ParticleWindX=0.3;
    double ParticleWindY=0;
    double ParticleWindZ=0.15;

    bool Dynamic = false;
    double Density = 2700;
    double Friction = .72, Restitution = .03;
    double VelocityX=0,VelocityY=0,VelocityZ=0;
    double AngularVelocityX=0,AngularVelocityY=0,AngularVelocityZ=0;
    int JointKind = 0; // None, fixed, hinge, rope, spring, ball, slider.
    std::string JointTargetId; // Empty attaches to the world.
    double JointX=0, JointY=0, JointZ=0;
    double JointTargetX=0, JointTargetY=0, JointTargetZ=0;
    double JointAxisX=0, JointAxisY=1, JointAxisZ=0;
    double JointLength=1, JointFrequency=3, JointDamping=.5;
    double JointAngleMin=-180,JointAngleMax=180;
    int JointSegments=0;
    double JointRadius=.025;
    std::vector<std::array<double,3>> JointRuntimePoints;
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
    double route_yaw(double seconds) const;
    void validate() const;
};
void to_json(json &, const Character &);
void from_json(const json &, Character &);
struct GameplayCondition { std::string Type="has-key", Value; };
struct GameplayAction { std::string Type="complete-objective", Target; };
struct GameplayRule {
    std::string Id=new_id(), Name="New rule", Event="item-collected", Subject, Description;
    double X=0, Y=1.7, Z=0, Radius=2;
    bool Once=true, Enabled=true;
    std::vector<GameplayCondition> Conditions;
    std::vector<GameplayAction> Actions;
    void validate() const;
};
void to_json(json&,const GameplayCondition&);
void from_json(const json&,GameplayCondition&);
void to_json(json&,const GameplayAction&);
void from_json(const json&,GameplayAction&);
void to_json(json&,const GameplayRule&);
void from_json(const json&,GameplayRule&);
struct QuestDefinition {
    std::string Id=new_id(), Title="New quest", Description;
    std::vector<std::string> RuleIds;
    std::vector<int> StepSizes;
    bool Tracked=true;
};
void to_json(json&,const QuestDefinition&);
void from_json(const json&,QuestDefinition&);
struct GameSettings {
    double ProjectileSpeed=0,ProjectileGravity=0,ProjectileMass=.02; // Zero speed preserves the existing instant sector attack.
    std::string Preset = "adventure";
    double SpawnX=0, SpawnY=1.7, SpawnZ=6;
    double MoveSpeed=4, LookSensitivity=.15, PlayerRadius=.3, SightRange=60;
    bool Collision=true, ReducedCameraMotion=false;
    void validate() const;
};
void to_json(json &, const GameSettings &);
void from_json(const json &, GameSettings &);
struct SceneData {
    std::vector<GameplayRule> Rules;
    std::vector<QuestDefinition> Quests;
    std::optional<GameSettings> Game;

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
                                                "chaos-rock", "light-shard", "shadowenemy-smoke"};
void configure(Character &, double, double, double, double);
} // namespace starlight::editor
