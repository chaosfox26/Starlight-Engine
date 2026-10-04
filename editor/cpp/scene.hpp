#pragma once
#include "../../deep/src/json.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
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
struct FoliageMaterial {
    double BarkScale=1,BarkRelief=1,BarkRoughness=.88;
    double LeafTransmission=.55,LeafVeins=.7,LeafRoughness=.6;
    bool operator==(const FoliageMaterial&) const=default;
    void validate()const{
        auto in=[](double x,double a,double b){return std::isfinite(x)&&x>=a&&x<=b;};
        if(!in(BarkScale,.25,4)||!in(BarkRelief,0,2)||!in(BarkRoughness,.45,.98)||
           !in(LeafTransmission,0,1)||!in(LeafVeins,0,1)||!in(LeafRoughness,.4,.85))
            throw std::runtime_error("Invalid foliage material controls");
    }
};
void to_json(json&,const FoliageMaterial&);
void from_json(const json&,FoliageMaterial&);
enum class AuthoredSurface {Plain=0,Grass=1,Earth=2,Sand=3,Stone=4,Snow=5,Rock=6,Water=7,Lava=8,Lightning=9,ChaosRock=10,LightBound=11,Bark=12,Leaf=13};
struct SculptMesh {
    std::vector<double> Positions;
    std::vector<int> Indices;
    std::vector<double> MaterialCoordinates; // Optional triples: leaf u,v,variation in [0,1].
    // Identity of this geometry for the engine's shared caches (not saved). Every place that creates or edits the arrays above calls touch();
    // copies keep the value because they hold the same data. Zero means "unknown": content_revision() then hashes the whole contents.
    uint64_t Revision = 0;
    void touch();
    uint64_t content_revision() const;
    bool operator==(const SculptMesh& other) const { return Positions == other.Positions && Indices == other.Indices && MaterialCoordinates == other.MaterialCoordinates; }
    void validate() const;
};
struct TerrainVolume {
    double X=0,Y=0,Z=0,Radius=1;
    bool Subtract=true;
    bool operator==(const TerrainVolume&) const=default;
};
void to_json(json&,const TerrainVolume&);
void from_json(const json&,TerrainVolume&);
struct TerrainData {
    static constexpr int N = 97;
    static constexpr double Step = .25, Half = 12;
    std::vector<double> Heights = std::vector<double>(N * N);
    std::vector<int> Paint = std::vector<int>(N * N);
    std::vector<TerrainVolume> Volumes;
    bool Lava = false, WaterEnabled = false;
    double WaterLevel = -.5;
    bool Breakable=true;
    double DestructionHealth=10,DestructionRadius=.65;
    void validate() const;
    double height_at(double x, double z) const;
    double density_at(double x,double y,double z) const;
    double surface_at(double x,double z) const; // Highest actual solid surface, including open craters.
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
    bool operator==(const WalkPoint&) const=default;
};
void to_json(json &, const WalkPoint &);
void from_json(const json &, WalkPoint &);
struct LightFollowHousing {
    double Width=.6,Height=.4,Depth=.6;
    std::string Color="#120F1A";
    bool operator==(const LightFollowHousing&) const=default;
};
struct LightFollowBeam {
    double Haze=.07,Pool=.3,InnerPool=.45;
    bool operator==(const LightFollowBeam&) const=default;
};
struct LightFollow {
    std::string Target="focus";
    double OffsetX=0,OffsetZ=0,Lag=3,Wobble=0,PoolRadius=1.5;
    std::optional<LightFollowHousing> Housing;
    std::optional<LightFollowBeam> Beam;
    bool operator==(const LightFollow&) const=default;
    void validate() const;
};
void to_json(json&,const LightFollow&);
void from_json(const json&,LightFollow&);
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
    bool Breakable=true;
    double DestructionHealth=10; // Scenery durability; actors retain combat health.
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
    std::optional<FoliageMaterial> Foliage;
    double LightCone = 180;
    double LightIntensity = 12;
    double LightRange = 6;
    double LightInner = .8; // Inner cone as a fraction of the outer half-angle; lights only.
    double Glow = 0; // 0..1; above zero the solid shape or sculpt is drawn emissive.
    std::string Group; // Optional group name that scene lighting modulators can target.
    double RingInner = .82; // Inner radius as a fraction of the outer radius; rings only.
    std::optional<LightFollow> Follow; // Follow-spot behaviour; lights only.
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
    // Member-wise equality (exact, and new fields are included automatically): lets a scene update prove that surviving objects are unchanged without serializing them.
    bool operator==(const Character&) const=default;
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
struct SceneLighting {
    std::string Mode="authored"; // authored: these values; sun: the Hour-driven sun as before.
    std::array<double,3> KeyDirection{0,-1,0};
    double KeyIntensity=1;
    std::optional<double> KeyIntensityFlat;
    std::array<double,3> Ambient{.5,.5,.5};
    double AmbientGround=.5;
    std::array<double,3> Clear{.05,.05,.08};
    double FogMix=.35;
    std::map<std::string,std::map<std::string,double>> Modulators; // Modulator name -> target (Clear, Key, Ambient, FogMix, Spots, group:<name>) -> coefficient.
    bool operator==(const SceneLighting&) const=default;
    void validate() const;
};
void to_json(json&,const SceneLighting&);
void from_json(const json&,SceneLighting&);
struct SceneMood {
    std::string Id;
    std::array<double,3> Clear{};
    std::optional<std::array<double,3>> Ambient;
    std::optional<double> AmbientGround,KeyIntensity,FogMix;
    bool operator==(const SceneMood&) const=default;
    void validate() const;
};
void to_json(json&,const SceneMood&);
void from_json(const json&,SceneMood&);
struct ViewFarRule { double Distance=30,FarPerDistance=4,FarBase=800; bool operator==(const ViewFarRule&) const=default; };
struct ViewFollow { double TargetHeight=1.1,TargetHeightPerY=.5,DefaultDistance=9,DefaultHeight=5; bool operator==(const ViewFollow&) const=default; };
struct ViewIdleOrbit { double DelaySeconds=4,PeriodSeconds=32; bool operator==(const ViewIdleOrbit&) const=default; };
struct SceneView {
    double FovDegrees=50,Near=.1,Far=400,OrbitRateRadiansPerSecond=1.6;
    std::optional<ViewFarRule> FarWhenDistanceAbove;
    std::optional<ViewFollow> Follow;
    std::optional<ViewIdleOrbit> IdleOrbit;
    bool operator==(const SceneView&) const=default;
    void validate() const;
};
void to_json(json&,const SceneView&);
void from_json(const json&,SceneView&);
struct SceneZone {
    std::string Id,Kind="anchor",Shape="circle",Note; // Kind: raised-floor, anchor, bounds, spawn, crowd. Shape: circle (Radius) or box (Width, Depth).
    double X=0,Z=0,Radius=0,Width=0,Depth=0,Height=0; // Height only for raised-floor.
    bool operator==(const SceneZone&) const=default;
    void validate() const;
};
void to_json(json&,const SceneZone&);
void from_json(const json&,SceneZone&);
struct CrowdLod {
    std::vector<int> Grids{18,9},EnterPx{65,15},ExitPx{78,19};
    bool operator==(const CrowdLod&) const=default;
};
struct CrowdCullSphere { double CentreHeight=.9,Radius=1.35; bool operator==(const CrowdCullSphere&) const=default; };
struct SceneCrowd {
    CrowdLod Lod;
    int Variants=4;
    CrowdCullSphere CullSphere;
    bool operator==(const SceneCrowd&) const=default;
    void validate() const;
};
void to_json(json&,const SceneCrowd&);
void from_json(const json&,SceneCrowd&);
struct CastHeightRule { std::array<double,2> AcceptRange{1.4,2.4}; double TargetHeight=1.8; bool operator==(const CastHeightRule&) const=default; };
struct CastRig {
    std::map<std::string,std::string> Clips,Joints;
    std::optional<CastHeightRule> HeightRule;
    bool operator==(const CastRig&) const=default;
};
struct CastUv {
    std::string Layout="glb"; // glb: the model's own UVs; atlas: a separate UV blob (Asset) is required.
    std::string Asset;
    bool operator==(const CastUv&) const=default;
};
struct FurProfile {
    double length=.012,softness=.5,stiffness=.5,sheen=.5,sway=.5,density=1;
    bool operator==(const FurProfile&) const=default;
};
struct CastEffect {
    std::vector<std::string> Maps; // Map slots the effect reads.
    std::map<std::string,FurProfile> Profiles;
    bool operator==(const CastEffect&) const=default;
};
struct CastModel {
    std::string Id,Model,Role="crowd"; // Role: lead or crowd.
    int Variants=1;
    std::optional<CastRig> Rig;
    CastUv Uv;
    std::map<std::string,std::map<std::string,std::string>> Maps; // Variant ("v0"...) -> slot -> texture asset id.
    std::map<std::string,CastEffect> Effects;
    std::vector<std::string> Requires,Wants; // Items: model, skeleton, uv:atlas, map:<slot>, effect:<name>.
    bool operator==(const CastModel&) const=default;
    void validate() const;
};
void to_json(json&,const CastModel&);
void from_json(const json&,CastModel&);
struct SceneCast {
    std::vector<CastModel> Models;
    bool operator==(const SceneCast&) const=default;
    void validate() const;
};
void to_json(json&,const SceneCast&);
void from_json(const json&,SceneCast&);
bool valid_blob_id(const std::string&);
std::set<std::string> cast_asset_ids(const json& card);
struct SceneData {
    std::vector<GameplayRule> Rules;
    std::vector<QuestDefinition> Quests;
    std::optional<GameSettings> Game;

    double Hour = 15;
    bool DayCycle = false;
    double DayLength = 180;
    double FogDensity = .018;
    double RainIntensity = 0, RainWindX = 0, RainWindZ = 0;
    double WaterWindX=1.5,WaterWindZ=.5,WaterWaveStrength=.35;
    double LavaInitialHeat=1,LavaCoolingRate=.012,LavaCrustDepth=.08,LavaLightStrength=1;
    double BloomStrength = .12;
    int BloomQuality = 2;
    int ShadowQuality = 2;
    bool RayReconstructionEnabled = true;
    bool RaytracingEnabled = false;
    int RaytracingQuality = 1;
    int PathBounces = 3;
    bool FreezeRaytracingAnimation = false;
    int SchemaVersion = 16; // Key-card schema: configuration and engine-asset references only (see engine_assets.hpp).
    std::string Name = "My first scene";
    std::vector<Character> Characters = {};
    std::vector<LiquidSource> LiquidSources = {};
    std::optional<TerrainData> Terrain = {};
    std::string GroundColor = "#182033";
    std::optional<SceneLighting> Lighting;
    std::vector<SceneMood> Moods;
    std::string ActiveMood;
    std::optional<SceneView> View;
    std::vector<SceneZone> Zones;
    std::optional<SceneCrowd> Crowd;
    SceneCast Cast;
    void validate() const;
    std::string encode() const;
    static SceneData decode(std::string_view);
};
void to_json(json &, const SceneData &);
void from_json(const json &, SceneData &);
inline const std::vector<std::string> shapes = {"cube",       "rectangle",   "trapezoid",   "octagon",
                                                "fire",       "light",       "lava",        "rock",
                                                "boulder",    "pebbles",     "grass",       "chaos-lightning",
                                                "chaos-rock", "light-shard", "craxis-smoke",
                                                "cylinder",   "ring"};
std::set<std::string> cast_asset_ids(const SceneData&);
std::vector<std::string> cast_missing_assets(const SceneData&);
void configure(Character &, double, double, double, double);
} // namespace starlight::editor
