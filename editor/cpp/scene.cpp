#include "scene.hpp"
#include "color.hpp"
#include <numeric>
#include <set>
namespace starlight::editor {
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool finite(std::initializer_list<double> a) {
    return std::all_of(a.begin(), a.end(), [](double x) { return std::isfinite(x); });
}
void color(const std::string &s) { color_rgb(s); }
} // namespace
void to_json(json &j, const SculptMesh &v) { j = {{"Positions", v.Positions}, {"Indices", v.Indices}}; }
void from_json(const json &j, SculptMesh &v) {
    j.at("Positions").get_to(v.Positions);
    require(j.at("Indices").is_array(), "Expected sculpt index array");
    v.Indices.clear();
    for (const auto &index : j.at("Indices"))
        v.Indices.push_back(native_int(index));
    v.validate();
}
void to_json(json &j, const LiquidSource &v) {
    j = {{"X", v.X}, {"Z", v.Z}, {"Lava", v.Lava}, {"Volume", v.Volume}};
}
void from_json(const json &j, LiquidSource &v) {
    v.X = j.value("X", 0.);
    v.Z = j.value("Z", 0.);
    v.Lava = j.value("Lava", false);
    v.Volume = j.value("Volume", 2.);
}
void to_json(json &j, const TerrainData &v) {
    j = {{"Heights", v.Heights},
         {"Paint", v.Paint},
         {"Lava", v.Lava},
         {"WaterEnabled", v.WaterEnabled},
         {"WaterLevel", v.WaterLevel}};
}
void from_json(const json &j, TerrainData &v) {
    if (j.contains("Heights"))
        j.at("Heights").get_to(v.Heights);
    if (j.contains("Paint")) {
        require(j.at("Paint").is_array(), "Expected terrain paint array");
        v.Paint.clear();
        for (const auto &paint : j.at("Paint"))
            v.Paint.push_back(native_int(paint));
    }
    v.Lava = j.value("Lava", false);
    v.WaterEnabled = j.value("WaterEnabled", false);
    v.WaterLevel = j.value("WaterLevel", -.5);
    v.validate();
}
void SculptMesh::validate() const {
    require(Positions.size() % 3 == 0 && Positions.size() <= 600000 && Indices.size() % 3 == 0 &&
                Indices.size() <= 1200000,
            "Invalid sculpt mesh size");
    for (double x : Positions)
        require(std::isfinite(x) && std::abs(x) <= 10000, "Invalid sculpt vertex");
    for (int i : Indices)
        require(i >= 0 && size_t(i) < Positions.size() / 3, "Invalid sculpt index");
}
void TerrainData::validate() const {
    require(Heights.size() == N * N && Paint.size() == N * N && std::isfinite(WaterLevel) &&
                std::abs(WaterLevel) <= 20,
            "Invalid terrain size or water level");
    for (double h : Heights)
        require(std::isfinite(h) && std::abs(h) <= 20, "Invalid terrain height");
    for (int p : Paint)
        require(p >= 0 && p < 5, "Invalid terrain paint");
}
double TerrainData::height_at(double x, double z) const {
    double u = std::clamp((x + Half) / Step, 0., double(N - 1)),
           v = std::clamp((z + Half) / Step, 0., double(N - 1));
    int a = std::min(int(u), N - 2), b = std::min(int(v), N - 2);
    u -= a;
    v -= b;
    return Heights[b * N + a] * (1 - u) * (1 - v) + Heights[b * N + a + 1] * u * (1 - v) +
           Heights[(b + 1) * N + a] * (1 - u) * v + Heights[(b + 1) * N + a + 1] * u * v;
}
void TerrainData::brush(double x, double z, double radius, double strength, std::string_view tool, int paint,
                        double target, double seconds) {
    require(finite({x, z, radius, strength, target, seconds}) && radius > 0 && seconds > 0 && paint >= 0 &&
                paint < 5,
            "Invalid terrain brush");
    const auto before = tool == "smooth" ? Heights : std::vector<double>{};
    for (int j = std::max(0, int((z - radius + Half) / Step));
         j <= std::min(N - 1, int(std::ceil((z + radius + Half) / Step))); ++j)
        for (int i = std::max(0, int((x - radius + Half) / Step));
             i <= std::min(N - 1, int(std::ceil((x + radius + Half) / Step))); ++i) {
            double d = std::hypot(i * Step - Half - x, j * Step - Half - z) / radius;
            if (d >= 1)
                continue;
            int k = j * N + i;
            double weight = std::pow(1 - d * d, 2), blend = 1 - std::exp(-strength * seconds * 5 * weight);
            if (tool == "paint") {
                Paint[k] = paint;
                continue;
            }
            if (tool == "raise" || tool == "lower")
                Heights[k] += (tool == "raise" ? 1 : -1) * strength * seconds * weight * 2;
            else if (tool == "smooth") {
                double sum = 0;
                int count = 0;
                for (int b = std::max(0, j - 1); b <= std::min(N - 1, j + 1); b++)
                    for (int a = std::max(0, i - 1); a <= std::min(N - 1, i + 1); a++) {
                        sum += before[b * N + a];
                        count++;
                    }
                Heights[k] += (sum / count - Heights[k]) * blend;
            } else if (tool == "flatten")
                Heights[k] += (target - Heights[k]) * blend;
            else if (tool == "hill" || tool == "crater")
                Heights[k] += (tool == "hill" ? 1 : -1) * strength * weight;
            else if (tool == "plateau")
                Heights[k] += (target + strength - Heights[k]) * std::min(1., (1 - d) * 5);
            else
                throw std::runtime_error("Unknown terrain brush");
            Heights[k] = std::clamp(Heights[k], -20., 20.);
        }
}
WalkPoint Character::at(double seconds) const {
    WalkPoint p{X, Z};
    if (PhysicsRotation)
        return p;
    double remaining = std::max(0., seconds) * Speed;
    for (const auto &q : Route) {
        double dx = q.X - p.X, dz = q.Z - p.Z, length = std::hypot(dx, dz);
        if (length > remaining && length > 0)
            return {p.X + dx * remaining / length, p.Z + dz * remaining / length};
        remaining -= length;
        p = q;
    }
    return p;
}
double Character::route_yaw(double seconds) const {
    if(seconds<=0||PhysicsRotation)return 0;WalkPoint p{X,Z};double remaining=seconds*Speed,yaw=0;
    for(const auto& q:Route){double dx=q.X-p.X,dz=q.Z-p.Z,length=std::hypot(dx,dz);if(length>1e-8)yaw=std::atan2(dx,dz);if(length>remaining&&length>0)break;remaining-=length;p=q;}return yaw;
}
void Character::validate() const {
    require(GrappleMode>=0 && GrappleMode<=2,"Invalid grapple attachment mode");
    require(std::isfinite(CombatSightRange) && CombatSightRange>=1 && CombatSightRange<=1000 && std::isfinite(CombatHealth) && CombatHealth>=0 && CombatHealth<=1000000000 && CombatTeam>=0 && CombatTeam<=255 && CombatControlEffect>=-1 && CombatControlEffect<14 && (!CombatBoss || CombatHealth>0),"Invalid combat actor settings");
    require(GameplayRole=="none" || GameplayRole=="key" || GameplayRole=="door" || GameplayRole=="exit" || GameplayRole=="breakable", "Invalid gameplay role");
    require(GameplayKey.size()<=128 && (GameplayRole!="key" || !GameplayKey.empty()), "Give a collectible key a key name");
    require(finite({X, Y, Z, Size, Speed, Width, Height, Depth, RotationX, RotationY, RotationZ, TargetX,
                    TargetY, TargetZ, BranchSpread, Density, LightCone, LightIntensity, LightRange}) &&
                Size > 0 && Speed > 0 && Width > 0 && Height > 0 && Depth > 0 &&
                std::max({Width, Height, Depth}) <= 1000,
            "Invalid character transform");
    require(!Shape || std::find(shapes.begin(), shapes.end(), *Shape) != shapes.end(), "Unknown shape");
    require(BranchSpread >= 0 && BranchSpread <= 5 &&
                std::max({std::abs(TargetX), std::abs(TargetY), std::abs(TargetZ)}) <= 30,
            "Invalid lightning target");
    require(finite({LightningDamage,LightningChainRadius})&&LightningDamage>=0&&LightningDamage<=100000&&LightningChainRadius>0&&LightningChainRadius<=100&&LightningChains>=0&&LightningChains<=16,"Invalid lightning behavior");
    require(finite({ParticleRate,ParticleLifetime,ParticleDrag,ParticleLift,ParticleWindX,ParticleWindY,ParticleWindZ})&&ParticleRate>=0&&ParticleRate<=1000&&ParticleLifetime>.01&&ParticleLifetime<=30&&ParticleDrag>=0&&ParticleDrag<=20&&std::abs(ParticleLift)<=100&&std::abs(ParticleWindX)<=100&&std::abs(ParticleWindY)<=100&&std::abs(ParticleWindZ)<=100,"Invalid particle settings");
    require(finite({VelocityX,VelocityY,VelocityZ,AngularVelocityX,AngularVelocityY,AngularVelocityZ})&&std::max({std::abs(VelocityX),std::abs(VelocityY),std::abs(VelocityZ)})<=500&&std::max({std::abs(AngularVelocityX),std::abs(AngularVelocityY),std::abs(AngularVelocityZ)})<=3600,"Invalid body starting velocity");
    require(finite({JointAngleMin,JointAngleMax})&&JointAngleMin>=-180&&JointAngleMax<=180&&JointAngleMin<=JointAngleMax,"Invalid hinge angle limits");
    require((JointSegments==0||(JointSegments>=2&&JointSegments<=64))&&finite({JointRadius})&&JointRadius>=.005&&JointRadius<=.5,"Invalid rope segment settings");
    require(finite({CombatProjectileSpeed,CombatProjectileGravity,CombatProjectileMass})&&CombatProjectileSpeed>=0&&CombatProjectileSpeed<=500&&CombatProjectileGravity>=0&&CombatProjectileGravity<=40&&CombatProjectileMass>0&&CombatProjectileMass<=100,"Invalid actor projectile settings");
    require(JointKind>=0 && JointKind<=6,"Invalid joint kind");
    require(finite({JointX,JointY,JointZ,JointTargetX,JointTargetY,JointTargetZ,JointAxisX,JointAxisY,JointAxisZ,JointLength,JointFrequency,JointDamping}),"Invalid joint values");
    require(JointLength>0 && JointLength<=1000 && JointFrequency>0 && JointFrequency<=120 && JointDamping>=0 && JointDamping<=2,"Invalid joint limits");
    require((JointKind!=2 && JointKind!=6) || JointAxisX*JointAxisX+JointAxisY*JointAxisY+JointAxisZ*JointAxisZ>1e-8,"Hinge axis must be nonzero");
    require(JointKind==0 || (Dynamic && JointTargetId!=Id),"A joint requires a dynamic body and a different target");
    require(finite({Friction,Restitution}) && Friction>=0 && Friction<=2 && Restitution>=0 && Restitution<=1,"Invalid physical material");
    require(Density >= 50 && Density <= 20000 && LightCone >= 5 && LightCone <= 180 && LightIntensity >= 0 &&
                LightIntensity <= 100 && LightRange >= .1 && LightRange <= 50 && Surface >= 0 &&
                Surface <= 11,
            "Invalid material or physics setting");
    if (Dynamic)
        require((Shape || Sculpt || !Asset.empty()) && (!Shape || (*Shape != "grass" && *Shape != "fire" && *Shape != "light" && *Shape != "lava" &&
                    *Shape != "chaos-lightning" && *Shape != "light-shard" && *Shape != "shadowenemy-smoke")),
                "Dynamic body needs a solid shape");
    for (const auto &p : Route)
        require(finite({p.X, p.Z}), "Invalid route point");
    color(Color);
    if (Sculpt)
        Sculpt->validate();
}
void GameplayRule::validate() const {
    require(!Id.empty() && Id.size()<=128 && Name.size()<=160 && Subject.size()<=128,"Invalid gameplay rule identity");
    require(Description.size()<=2048,"Objective description too long");
    require(Event=="item-collected" || Event=="enter-area" || Event=="interact" || Event=="object-opened" || Event=="object-broken" || Event=="stand-on-object","Unsupported gameplay event");
    require(finite({X,Y,Z,Radius}) && std::abs(X)<=100000 && std::abs(Y)<=100000 && std::abs(Z)<=100000 && Radius>=.1 && Radius<=1000,"Invalid rule area");
    require(Conditions.size()<=16 && !Actions.empty() && Actions.size()<=16,"Rules need 1-16 actions and at most 16 conditions");
    for(const auto& c:Conditions)require((c.Type=="has-key" || c.Type=="objective-complete") && !c.Value.empty() && c.Value.size()<=128,"Invalid rule condition");
    for(const auto& a:Actions)require((a.Type=="open-door" || a.Type=="complete-objective" || a.Type=="finish-game") && a.Target.size()<=128 && (a.Type=="finish-game" || !a.Target.empty()),"Invalid rule action");
}
void GameSettings::validate() const {
    require(finite({ProjectileSpeed,ProjectileGravity,ProjectileMass})&&ProjectileMass>0&&ProjectileMass<=100&&ProjectileSpeed>=0&&ProjectileSpeed<=500&&ProjectileGravity>=0&&ProjectileGravity<=40,"Invalid projectile speed or gravity");
    require(Preset=="adventure", "Unsupported game preset");
    require(finite({SpawnX,SpawnY,SpawnZ,MoveSpeed,LookSensitivity,PlayerRadius}) &&
        std::abs(SpawnX)<=100000 && std::abs(SpawnY)<=100000 && std::abs(SpawnZ)<=100000 &&
        MoveSpeed>=.1 && MoveSpeed<=100 && LookSensitivity>=.01 && LookSensitivity<=5 &&
        PlayerRadius>=.05 && PlayerRadius<=2 && std::isfinite(SightRange) && SightRange>=1 && SightRange<=1000, "Invalid player settings");
}
void SceneData::validate() const {
    if(Game)Game->validate();
    require(Rules.size()<=64,"A scene supports at most 64 gameplay rules");
    std::set<std::string> ruleIds;
    for(const auto& rule:Rules){rule.validate();require(ruleIds.insert(rule.Id).second,"Duplicate gameplay rule identity");}
    require(Quests.size()<=32,"Scene supports at most32 quests");
    std::set<std::string> questIds;
    for(const auto& q:Quests){
        require(!q.Id.empty() && q.Id.size()<=128 && questIds.insert(q.Id).second,"Invalid or duplicate quest ID");
        require(!q.Title.empty() && q.Title.size()<=160 && q.Description.size()<=4096,"Invalid quest text");
        require(!q.RuleIds.empty() && q.RuleIds.size()<=16,"Quest needs1-16 objectives");
        if(!q.StepSizes.empty()){
            require(q.StepSizes.size()<=16,"Quest supports at most 16 steps");
            size_t total=0;for(int count:q.StepSizes){require(count>=1 && count<=5,"Each quest step needs one to five objectives");total+=count;}
            require(total==q.RuleIds.size(),"Step sizes must cover every selected objective exactly once");
            size_t start=0;
            for(int count:q.StepSizes){
                for(size_t i=start;i<start+count;++i){
                    auto r=std::find_if(Rules.begin(),Rules.end(),[&](const auto& rule){return rule.Id==q.RuleIds[i];});
                    if(r==Rules.end())continue;
                    for(const auto& condition:r->Conditions)if(condition.Type=="objective-complete")
                        for(size_t j=start;j<q.RuleIds.size();++j){
                            auto other=std::find_if(Rules.begin(),Rules.end(),[&](const auto& rule){return rule.Id==q.RuleIds[j];});
                            if(other!=Rules.end())for(const auto& action:other->Actions)
                                require(action.Type!="complete-objective" || action.Target!=condition.Value,"Objectives cannot depend on their own step or a later step");
                        }
                }
                start+=count;
            }
        }
        std::set<std::string> links;
        for(const auto& id:q.RuleIds)require(ruleIds.contains(id) && links.insert(id).second,"Quest objective rule missing or duplicated");
    }
    require(SchemaVersion >= 1 && SchemaVersion <= 12, "Unsupported scene schema");
    require(RaytracingQuality >= 0 && RaytracingQuality <= 4 && PathBounces >= 2 && PathBounces <= 4,
            "Invalid raytracing settings");
    require(finite({Hour, DayLength, FogDensity}) && Hour >= 0 && Hour <= 24 && DayLength >= 10 &&
                DayLength <= 36000 && FogDensity >= 0 && FogDensity <= .3,
            "Invalid atmosphere");
    require(LiquidSources.size() <= 32, "Too many pours");
    for (const auto &q : LiquidSources)
        require(finite({q.X, q.Z, q.Volume}) && std::abs(q.X) <= 12 && std::abs(q.Z) <= 12 && q.Volume > 0 &&
                    q.Volume <= 20,
                "Invalid liquid source");
    int lights = 0;
    for (const auto &c : Characters) {
        c.validate();
        if (c.Shape && (*c.Shape == "fire" || *c.Shape == "light" || *c.Shape == "chaos-lightning" ||
                        *c.Shape == "chaos-rock" || *c.Shape == "light-shard"))
            lights++;
    }
    require(lights <= 32, "Too many local lights");
    if (Terrain)
        Terrain->validate();
    color(GroundColor);
}
SceneData SceneData::decode(std::string_view text) {
    auto s = json::parse(text).get<SceneData>();
    s.validate();
    s.SchemaVersion = std::any_of(s.Characters.begin(),s.Characters.end(),[](const auto& c){return c.CombatHealth>0;}) ? 12 : std::any_of(s.Quests.begin(),s.Quests.end(),[](const auto& q){return !q.StepSizes.empty();}) ? 11 : !s.Quests.empty() ? 10 : !s.Rules.empty() ? 9 : s.Game || std::any_of(s.Characters.begin(),s.Characters.end(),[](const auto& c){return c.GameplayRole!="none";}) ? 8 : 7;
    return s;
}
std::string SceneData::encode() const {
    validate();
    return json(*this).dump(2);
}
void configure(Character &c, double x, double z, double speed, double size) {
    require(finite({x, z, speed, size}) && std::abs(x) <= 12 && std::abs(z) <= 12 && speed >= .2 &&
                speed <= 5 && size >= .3 && size <= 3,
            "Invalid character settings");
    for (auto &p : c.Route) {
        p.X += x - c.X;
        p.Z += z - c.Z;
    }
    c.X = x;
    c.Z = z;
    c.Speed = speed;
    c.Size = size;
}
} // namespace starlight::editor
