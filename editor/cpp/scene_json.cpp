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
    if(v.GrappleMode)j["GrappleMode"]=v.GrappleMode;
    if(v.CombatHealth>0){j["CombatHealth"]=v.CombatHealth;j["CombatSightRange"]=v.CombatSightRange;j["CombatTeam"]=v.CombatTeam;j["CombatBoss"]=v.CombatBoss;j["CombatAutoAttack"]=v.CombatAutoAttack;j["CombatPursuit"]=v.CombatPursuit;j["CombatControlEffect"]=v.CombatControlEffect;}
    if(v.GameplayRole!="none"){j["GameplayRole"]=v.GameplayRole;j["GameplayKey"]=v.GameplayKey;}
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
    v.GrappleMode=j.value("GrappleMode",0);
    v.CombatSightRange=j.value("CombatSightRange",60.);v.CombatHealth=j.value("CombatHealth",0.);v.CombatTeam=j.value("CombatTeam",2);v.CombatBoss=j.value("CombatBoss",false);v.CombatAutoAttack=j.value("CombatAutoAttack",false);v.CombatPursuit=j.value("CombatPursuit",false);v.CombatControlEffect=j.value("CombatControlEffect",-1);
    v.GameplayRole=j.value("GameplayRole",std::string("none"));v.GameplayKey=j.value("GameplayKey",std::string{});
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
void to_json(json& j,const GameplayCondition& v){j={{"Type",v.Type},{"Value",v.Value}};}
void from_json(const json& j,GameplayCondition& v){j.at("Type").get_to(v.Type);j.at("Value").get_to(v.Value);}
void to_json(json& j,const GameplayAction& v){j={{"Type",v.Type},{"Target",v.Target}};}
void from_json(const json& j,GameplayAction& v){j.at("Type").get_to(v.Type);v.Target=j.value("Target",std::string{});}
void to_json(json& j,const GameplayRule& v){
    j={{"Id",v.Id},{"Name",v.Name},{"Event",v.Event},{"Subject",v.Subject},{"X",v.X},{"Y",v.Y},{"Z",v.Z},{"Radius",v.Radius},{"Once",v.Once},{"Enabled",v.Enabled},{"Conditions",v.Conditions},{"Actions",v.Actions}};
    if(!v.Description.empty())j["Description"]=v.Description;
}
void from_json(const json& j,GameplayRule& v){
    j.at("Id").get_to(v.Id);v.Name=j.value("Name",v.Name);v.Description=j.value("Description",std::string{});j.at("Event").get_to(v.Event);
    v.Subject=j.value("Subject",v.Subject);v.X=j.value("X",v.X);v.Y=j.value("Y",v.Y);v.Z=j.value("Z",v.Z);v.Radius=j.value("Radius",v.Radius);
    v.Once=j.value("Once",v.Once);v.Enabled=j.value("Enabled",v.Enabled);
    if(j.contains("Conditions")){if(!j.at("Conditions").is_array() || j.at("Conditions").size()>16)throw std::runtime_error("Too many rule conditions");j.at("Conditions").get_to(v.Conditions);}
    if(!j.at("Actions").is_array() || j.at("Actions").size()>16)throw std::runtime_error("Too many rule actions");
    j.at("Actions").get_to(v.Actions);v.validate();
}
void to_json(json& j,const QuestDefinition& q){j={{"Id",q.Id},{"Title",q.Title},{"Description",q.Description},{"RuleIds",q.RuleIds},{"Tracked",q.Tracked}};if(!q.StepSizes.empty())j["StepSizes"]=q.StepSizes;}
void from_json(const json& j,QuestDefinition& q){
    j.at("Id").get_to(q.Id);j.at("Title").get_to(q.Title);q.Description=j.value("Description",std::string{});q.Tracked=j.value("Tracked",true);
    if(!j.at("RuleIds").is_array() || j.at("RuleIds").size()>16)throw std::runtime_error("Quest supports at most16 objectives");
    j.at("RuleIds").get_to(q.RuleIds);
    q.StepSizes.clear();if(j.contains("StepSizes")){
        if(!j.at("StepSizes").is_array() || j.at("StepSizes").size()>16)throw std::runtime_error("Too many quest steps");
        for(const auto& size:j.at("StepSizes")){if(!size.is_number_integer())throw std::runtime_error("Quest step sizes must be whole numbers");q.StepSizes.push_back(native_int(size));}
    }
}
void to_json(json &j, const GameSettings &v) {
    j={{"Preset",v.Preset},{"SpawnX",v.SpawnX},{"SpawnY",v.SpawnY},{"SpawnZ",v.SpawnZ},
       {"MoveSpeed",v.MoveSpeed},{"LookSensitivity",v.LookSensitivity},{"PlayerRadius",v.PlayerRadius},{"Collision",v.Collision},{"ReducedCameraMotion",v.ReducedCameraMotion},{"SightRange",v.SightRange}};
}
void from_json(const json &j, GameSettings &v) {
    if(!j.is_object())throw std::runtime_error("Expected game settings object");
    v.Preset=j.value("Preset",v.Preset);v.SpawnX=j.value("SpawnX",v.SpawnX);
    v.SpawnY=j.value("SpawnY",v.SpawnY);v.SpawnZ=j.value("SpawnZ",v.SpawnZ);
    v.MoveSpeed=j.value("MoveSpeed",v.MoveSpeed);v.LookSensitivity=j.value("LookSensitivity",v.LookSensitivity);
    v.PlayerRadius=j.value("PlayerRadius",v.PlayerRadius);v.Collision=j.value("Collision",v.Collision);
    v.ReducedCameraMotion=j.value("ReducedCameraMotion",false);v.SightRange=j.value("SightRange",60.);
    v.validate();
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
    j["SchemaVersion"] = std::any_of(v.Characters.begin(),v.Characters.end(),[](const auto& c){return c.CombatHealth>0;}) ? 12 : std::any_of(v.Quests.begin(),v.Quests.end(),[](const auto& q){return !q.StepSizes.empty();}) ? 11 : !v.Quests.empty() ? 10 : !v.Rules.empty() ? 9 : v.Game || std::any_of(v.Characters.begin(),v.Characters.end(),[](const auto& c){return c.GameplayRole!="none";}) ? 8 : v.SchemaVersion;
    if(v.Game)j["Game"]=*v.Game;
    if(!v.Rules.empty())j["Rules"]=v.Rules;
    if(!v.Quests.empty())j["Quests"]=v.Quests;
    j["Name"] = v.Name;
    j["Characters"] = v.Characters;
    j["LiquidSources"] = v.LiquidSources;
    j["Terrain"] = v.Terrain ? json(*v.Terrain) : json(nullptr);
    j["GroundColor"] = v.GroundColor;
}
void from_json(const json &j, SceneData &v) {
    if (!j.is_object())
        throw std::runtime_error("Expected SceneData object");
    if(j.contains("Quests")){if(!j.at("Quests").is_array() || j.at("Quests").size()>32)throw std::runtime_error("Too many quests");j.at("Quests").get_to(v.Quests);}
    if(j.contains("Rules")){if(!j.at("Rules").is_array() || j.at("Rules").size()>64)throw std::runtime_error("Too many gameplay rules");j.at("Rules").get_to(v.Rules);}
    if(j.contains("Game") && !j.at("Game").is_null())v.Game=j.at("Game").get<GameSettings>();
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
