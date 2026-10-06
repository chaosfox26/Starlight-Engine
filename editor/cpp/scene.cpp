#include <atomic>
#include "scene.hpp"
#include "engine_assets.hpp"
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
void require(bool ok, const std::string &message) {
    if (!ok)
        throw std::runtime_error(message);
}
bool in_range(double x, double low, double high) { return std::isfinite(x) && x >= low && x <= high; }
bool token(const std::string &s, size_t limit = 64) {
    return !s.empty() && s.size() <= limit &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_' || c == '.'; });
}
bool triple_in(const std::array<double, 3> &a, double low, double high) { return in_range(a[0], low, high) && in_range(a[1], low, high) && in_range(a[2], low, high); }
bool contains(std::initializer_list<const char *> list, const std::string &s) {
    return std::any_of(list.begin(), list.end(), [&](const char *item) { return s == item; });
}
bool plain_solid(const Character &c) {
    if (c.Sculpt) return true;
    return c.Shape && contains({"cube", "rectangle", "trapezoid", "octagon", "cylinder", "ring"}, *c.Shape);
}
} // namespace
void to_json(json& j,const FoliageMaterial& v){v.validate();j={{"BarkScale",v.BarkScale},{"BarkRelief",v.BarkRelief},{"BarkRoughness",v.BarkRoughness},{"LeafTransmission",v.LeafTransmission},{"LeafVeins",v.LeafVeins},{"LeafRoughness",v.LeafRoughness}};}
void from_json(const json& j,FoliageMaterial& v){v.BarkScale=j.value("BarkScale",1.);v.BarkRelief=j.value("BarkRelief",1.);v.BarkRoughness=j.value("BarkRoughness",.88);v.LeafTransmission=j.value("LeafTransmission",.55);v.LeafVeins=j.value("LeafVeins",.7);v.LeafRoughness=j.value("LeafRoughness",.6);v.validate();}
// Key cards hold a reference to the sculpt, never its vertices: the geometry lives in the shared engine asset library.
void to_json(json &j, const SculptMesh &v) { v.validate(); j = {{"Asset", store_sculpt(v)}}; }
void from_json(const json &j, SculptMesh &v) {
    if (!j.is_object() || j.contains("Positions") || j.contains("Indices")) throw ConversionRequired("This scene uses the pre-key-card format (it embeds geometry or predates schema 16). Convert it with Starlight.SceneMigration before opening it.");
    v = load_sculpt(j.at("Asset").get<std::string>());
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
void to_json(json& j,const TerrainVolume& v){j={{"X",v.X},{"Y",v.Y},{"Z",v.Z},{"Radius",v.Radius},{"Subtract",v.Subtract}};}
void from_json(const json& j,TerrainVolume& v){v.X=j.at("X").get<double>();v.Y=j.at("Y").get<double>();v.Z=j.at("Z").get<double>();v.Radius=j.at("Radius").get<double>();v.Subtract=j.value("Subtract",true);}
void to_json(json &j, const TerrainData &v) {
    v.validate();
    j = {{"Asset", store_terrain_geometry(v)},
         {"Lava", v.Lava},
         {"WaterEnabled", v.WaterEnabled},
         {"WaterLevel", v.WaterLevel}, {"Breakable",v.Breakable},
         {"DestructionHealth",v.DestructionHealth}, {"DestructionRadius",v.DestructionRadius}};
}
void from_json(const json &j, TerrainData &v) {
    if (!j.is_object() || j.contains("Heights") || j.contains("Paint") || j.contains("Volumes")) throw ConversionRequired("This scene uses the pre-key-card format (it embeds geometry or predates schema 16). Convert it with Starlight.SceneMigration before opening it.");
    load_terrain_geometry(j.at("Asset").get<std::string>(), v);
    v.Lava = j.value("Lava", false);
    v.WaterEnabled = j.value("WaterEnabled", false);
    v.WaterLevel = j.value("WaterLevel", -.5);
    v.Breakable=j.value("Breakable",true);v.DestructionHealth=j.value("DestructionHealth",10.);v.DestructionRadius=j.value("DestructionRadius",.65);
    v.validate();
}
void SculptMesh::touch() {
    static std::atomic<uint64_t> next{1};
    Revision = next.fetch_add(1, std::memory_order_relaxed);
}
uint64_t SculptMesh::content_revision() const {
    if (Revision) return Revision;
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* data, size_t bytes) { auto p = static_cast<const unsigned char*>(data); for (size_t i = 0; i < bytes; ++i) { h ^= p[i]; h *= 1099511628211ull; } };
    const uint64_t sizes[3] = {Positions.size(), Indices.size(), MaterialCoordinates.size()};
    mix(sizes, sizeof(sizes));
    mix(Positions.data(), Positions.size() * sizeof(double));
    mix(Indices.data(), Indices.size() * sizeof(int));
    mix(MaterialCoordinates.data(), MaterialCoordinates.size() * sizeof(double));
    return h | (1ull << 63); // content hashes never collide with counter revisions
}
void SculptMesh::validate() const {
    require(MaterialCoordinates.empty()||MaterialCoordinates.size()==Positions.size(),"Invalid sculpt material coordinate count");
    for(double value:MaterialCoordinates)require(std::isfinite(value)&&value>=0&&value<=1,"Invalid sculpt material coordinate");
    require(Positions.size() % 3 == 0 && Positions.size() <= 600000 && Indices.size() % 3 == 0 &&
                Indices.size() <= 1200000,
            "Invalid sculpt mesh size");
    for (double x : Positions)
        require(std::isfinite(x) && std::abs(x) <= 10000, "Invalid sculpt vertex");
    for (int i : Indices)
        require(i >= 0 && size_t(i) < Positions.size() / 3, "Invalid sculpt index");
    if(!MaterialCoordinates.empty())for(size_t i=0;i<Indices.size();i+=3){double seed=MaterialCoordinates[Indices[i]*3+2];require(seed==MaterialCoordinates[Indices[i+1]*3+2]&&seed==MaterialCoordinates[Indices[i+2]*3+2],"Material seed must be constant on each triangle");}
}
void TerrainData::validate() const {
    require(Heights.size() == N * N && Paint.size() == N * N && std::isfinite(WaterLevel) &&
                std::abs(WaterLevel) <= 20,
            "Invalid terrain size or water level");
    require(Volumes.size()<=128,"Too many terrain volume operations");
    require(finite({DestructionHealth,DestructionRadius})&&DestructionHealth>0&&DestructionHealth<=1e9&&DestructionRadius>=.25&&DestructionRadius<=4,"Invalid terrain destruction settings");
    for(const auto& v:Volumes)require(finite({v.X,v.Y,v.Z,v.Radius}) && std::abs(v.X)<=12 && std::abs(v.Z)<=12 && std::abs(v.Y)<=20 && v.Radius>=.25 && v.Radius<=8,"Invalid terrain volume operation");
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
double TerrainData::density_at(double x,double y,double z) const {
    double value=std::max(y-height_at(x,z),-21.-y);
    auto blendMin=[](double a,double b,double width){double h=std::clamp(.5+.5*(b-a)/width,0.,1.);return b+(a-b)*h-width*h*(1-h);};
    for(const auto& v:Volumes){double dx=x-v.X,dy=y-v.Y,dz=z-v.Z;double sphere=std::sqrt(dx*dx+dy*dy+dz*dz)-v.Radius;double width=std::min(.3,v.Radius*.25);value=v.Subtract?-blendMin(-value,sphere,width):blendMin(value,sphere,width);}
    return std::max({value,std::abs(x)-Half,std::abs(z)-Half});
}
double TerrainData::surface_at(double x,double z) const {
    if(Volumes.empty())return height_at(x,z);
    const double height=height_at(x,z);
    double top=height+.5;
    for(const auto& v:Volumes)if(!v.Subtract)top=std::max(top,v.Y+v.Radius+.5);
    // Search only affected columns; untouched heightfield samples stay exact.
    bool affected=false;for(const auto& v:Volumes)if(std::hypot(x-v.X,z-v.Z)<v.Radius+.3){affected=true;break;}
    if(!affected)return height;
    // A vertical search keeps x/z fixed. Cache the column terms once rather
    // than re-interpolating the bed and repeating horizontal sphere work at
    // every search/bisection height. Preserve operation order and exact SDF
    // arithmetic; this does not replace carved terrain with a coarse proxy.
    struct ColumnOperation {double dxSquared,dzSquared,y,radius,width;bool subtract;};
    std::array<ColumnOperation,128> column;
    const bool prepared=Volumes.size()<=column.size();
    if(prepared)for(size_t i=0;i<Volumes.size();++i){const auto& v=Volumes[i];double dx=x-v.X,dz=z-v.Z;column[i]={dx*dx,dz*dz,v.Y,v.Radius,std::min(.3,v.Radius*.25),v.Subtract};}
    auto density=[&](double y){
        if(!prepared)return density_at(x,y,z);
        double value=std::max(y-height,-21.-y);
        auto blendMin=[](double a,double b,double width){double h=std::clamp(.5+.5*(b-a)/width,0.,1.);return b+(a-b)*h-width*h*(1-h);};
        for(size_t i=0;i<Volumes.size();++i){const auto& v=column[i];double dy=y-v.y;double sphere=std::sqrt(v.dxSquared+dy*dy+v.dzSquared)-v.radius;value=v.subtract?-blendMin(-value,sphere,v.width):blendMin(value,sphere,v.width);}
        return std::max({value,std::abs(x)-Half,std::abs(z)-Half});
    };
    for(double high=top;high> -21.;high-=.125){double low=std::max(-21.,high-.125);
        if(density(low)<=0){for(int i=0;i<16;++i){double mid=(low+high)*.5;if(density(mid)<=0)low=mid;else high=mid;}return (low+high)*.5;}
    }
    return -21.;
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
    require(Asset.empty()||EngineAssets::valid_id(AssetKind::Model,Asset),"An imported model must be referenced by its asset library id (import the model first)");
    if(Foliage)Foliage->validate();
    require(!Foliage||(Surface==12||Surface==13),"Foliage controls require Bark or Leaf surface");
    require(Surface<12||Sculpt||(Shape&&(*Shape=="cube"||*Shape=="rectangle"||*Shape=="trapezoid"||*Shape=="octagon")),"Bark/Leaf material requires supported authored geometry");
    require(Surface!=13||(Sculpt&&!Sculpt->MaterialCoordinates.empty()),"Leaf surface requires authored Sculpt coordinates");

    require(finite({LightInner,Glow,RingInner})&&LightInner>=.1&&LightInner<=1&&Glow>=0&&Glow<=1&&RingInner>=0&&RingInner<=.99,"Invalid light inner cone, glow or ring inner ratio");
    require(Glow==0||plain_solid(*this),"Glow applies only to solid shapes and sculpts");
    require(Group.empty()||token(Group),"An object group name uses 1-64 letters, digits, '-', '_' or '.'");
    require(LightInner==.8||(Shape&&*Shape=="light"),"The inner cone ratio applies only to a light");
    require(RingInner==.82||(Shape&&*Shape=="ring"),"The ring inner ratio applies only to a ring");
    require(!Follow||(Shape&&*Shape=="light"),"Follow-spot behaviour applies only to a light");
    if(Follow)Follow->validate();
    require(GrappleMode>=0 && GrappleMode<=2,"Invalid grapple attachment mode");
    require(std::isfinite(CombatSightRange) && CombatSightRange>=1 && CombatSightRange<=1000 && std::isfinite(CombatHealth) && CombatHealth>=0 && CombatHealth<=1000000000 && CombatTeam>=0 && CombatTeam<=255 && CombatControlEffect>=-1 && CombatControlEffect<14 && (!CombatBoss || CombatHealth>0),"Invalid combat actor settings");
    require(GameplayRole=="none" || GameplayRole=="key" || GameplayRole=="door" || GameplayRole=="exit" || GameplayRole=="breakable", "Invalid gameplay role");
    require(std::isfinite(DestructionHealth)&&DestructionHealth>0&&DestructionHealth<=1000000000,"Durability must be positive and finite");
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
                Surface <= 13,
            "Invalid material or physics setting");
    if (Dynamic)
        require((Shape || Sculpt || !Asset.empty()) && (!Shape || (*Shape != "grass" && *Shape != "fire" && *Shape != "light" && *Shape != "lava" && *Shape != "ring" &&
                    *Shape != "chaos-lightning" && *Shape != "light-shard" && *Shape != "craxis-smoke")),
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
void LightFollow::validate() const {
    require(token(Target),"Follow-spot target uses 1-64 letters, digits, '-', '_' or '.'");
    require(in_range(OffsetX,-1000,1000)&&in_range(OffsetZ,-1000,1000),"Follow-spot offset must be within 1000 units");
    require(in_range(Lag,0,60),"Follow-spot lag must be 0-60");
    require(in_range(Wobble,0,20),"Follow-spot wobble must be 0-20");
    require(in_range(PoolRadius,.05,50),"Follow-spot pool radius must be 0.05-50");
    if(Housing){
        require(in_range(Housing->Width,.01,1000)&&in_range(Housing->Height,.01,1000)&&in_range(Housing->Depth,.01,1000),"Follow-spot housing size must be 0.01-1000");
        color(Housing->Color);
    }
    if(Beam)require(in_range(Beam->Haze,0,2)&&in_range(Beam->Pool,0,2)&&in_range(Beam->InnerPool,0,2),"Follow-spot beam strengths must be 0-2");
}
void SceneLighting::validate() const {
    require(Mode=="authored"||Mode=="sun","Lighting mode must be authored or sun");
    require(triple_in(KeyDirection,-1000,1000)&&KeyDirection[0]*KeyDirection[0]+KeyDirection[1]*KeyDirection[1]+KeyDirection[2]*KeyDirection[2]>1e-8,"Lighting key direction must be finite and not zero");
    require(in_range(KeyIntensity,0,100)&&(!KeyIntensityFlat||in_range(*KeyIntensityFlat,0,100)),"Lighting key intensity must be 0-100");
    require(triple_in(Ambient,0,4)&&in_range(AmbientGround,0,4),"Lighting ambient must be 0-4");
    require(triple_in(Clear,0,1),"Lighting clear colour must be 0-1");
    require(in_range(FogMix,0,1),"Lighting fog mix must be 0-1");
    require(Modulators.size()<=16,"Lighting supports at most 16 modulators");
    for(const auto& [name,targets]:Modulators){
        require(token(name),"Invalid lighting modulator name: "+name);
        require(!targets.empty()&&targets.size()<=32,"Lighting modulator '"+name+"' needs 1-32 targets");
        for(const auto& [target,coefficient]:targets){
            require(contains({"Clear","Key","Ambient","FogMix","Spots"},target)||(target.starts_with("group:")&&token(target.substr(6))),"Lighting modulator '"+name+"' has an unknown target: "+target);
            require(in_range(coefficient,-4,4),"Lighting modulator '"+name+"' coefficients must be -4 to 4");
        }
    }
}
void SceneMood::validate() const {
    require(token(Id),"A mood name uses 1-64 letters, digits, '-', '_' or '.'");
    require(triple_in(Clear,0,1),"Mood '"+Id+"' clear colour must be 0-1");
    require(!Ambient||triple_in(*Ambient,0,4),"Mood '"+Id+"' ambient must be 0-4");
    require((!AmbientGround||in_range(*AmbientGround,0,4))&&(!KeyIntensity||in_range(*KeyIntensity,0,100))&&(!FogMix||in_range(*FogMix,0,1)),"Mood '"+Id+"' override out of range");
}
void SceneView::validate() const {
    require(in_range(FovDegrees,10,140),"View field of view must be 10-140 degrees");
    require(in_range(Near,.001,100)&&in_range(Far,Near,1000000)&&Far>Near,"View near and far planes must satisfy 0.001 <= near < far <= 1000000");
    require(in_range(OrbitRateRadiansPerSecond,0,50),"View orbit rate must be 0-50 radians per second");
    if(FarWhenDistanceAbove)require(in_range(FarWhenDistanceAbove->Distance,0,100000)&&in_range(FarWhenDistanceAbove->FarPerDistance,0,1000)&&in_range(FarWhenDistanceAbove->FarBase,0,1000000),"View far-plane distance rule out of range");
    if(Follow)require(in_range(Follow->TargetHeight,-100,100)&&in_range(Follow->TargetHeightPerY,-10,10)&&in_range(Follow->DefaultDistance,.1,1000)&&in_range(Follow->DefaultHeight,-1000,1000),"View follow settings out of range");
    if(IdleOrbit)require(in_range(IdleOrbit->DelaySeconds,0,3600)&&in_range(IdleOrbit->PeriodSeconds,1,3600),"View idle orbit settings out of range");
}
void SceneZone::validate() const {
    require(token(Id),"A zone name uses 1-64 letters, digits, '-', '_' or '.'");
    require(contains({"raised-floor","anchor","bounds","spawn","crowd"},Kind),"Zone '"+Id+"' has an unknown kind: "+Kind);
    require(Shape=="circle"||Shape=="box","Zone '"+Id+"' shape must be circle or box");
    require(in_range(X,-100000,100000)&&in_range(Z,-100000,100000),"Zone '"+Id+"' position out of range");
    if(Shape=="circle")require(in_range(Radius,.01,100000)&&Width==0&&Depth==0,"Zone '"+Id+"' is circular: give a radius and no width or depth");
    else require(Radius==0&&in_range(Width,.01,100000)&&in_range(Depth,.01,100000),"Zone '"+Id+"' is a box: give width and depth and no radius");
    require(in_range(Height,0,1000)&&(Height==0||Kind=="raised-floor"),"Zone '"+Id+"': only a raised floor has a height (0-1000)");
    require(Note.size()<=256,"Zone '"+Id+"' note is too long");
}
void SceneCrowd::validate() const {
    const auto& levels=Lod.Grids;
    require(!levels.empty()&&levels.size()<=4&&Lod.EnterPx.size()==levels.size()&&Lod.ExitPx.size()==levels.size(),"Crowd LOD needs 1-4 levels with matching Grids, EnterPx and ExitPx lists");
    for(size_t i=0;i<levels.size();++i)require(levels[i]>=2&&levels[i]<=64&&Lod.EnterPx[i]>=0&&Lod.EnterPx[i]<=4096&&Lod.ExitPx[i]>=Lod.EnterPx[i]&&Lod.ExitPx[i]<=4096,"Crowd LOD level out of range (grid 2-64, pixels 0-4096, exit not below enter)");
    require(Variants>=1&&Variants<=8,"Crowd variants must be 1-8");
    require(in_range(CullSphere.CentreHeight,-1000,1000)&&in_range(CullSphere.Radius,.01,1000),"Crowd cull sphere out of range");
}
namespace {
bool cast_slot(const std::string& s){return contains({"basecolor","normal","roughness","ao","furflow","sparkle"},s);}
bool variant_key(const std::string& s,int& index){
    if(s.size()<2||s.size()>3||s[0]!='v'||(s[1]=='0'&&s.size()>2))return false;
    for(size_t i=1;i<s.size();++i)if(!std::isdigit(static_cast<unsigned char>(s[i])))return false;
    index=std::stoi(s.substr(1));return true;
}
}
bool valid_blob_id(const std::string& id){
    return id.size()==5+64&&id.compare(0,5,"blob-")==0&&std::all_of(id.begin()+5,id.end(),[](unsigned char c){return std::isdigit(c)||(c>='a'&&c<='f');});
}
void CastModel::validate() const {
    const std::string who="Cast model '"+Id+"': ";
    require(token(Id),"A cast model name uses 1-64 letters, digits, '-', '_' or '.'");
    require(EngineAssets::valid_id(AssetKind::Model,Model),who+"Model must be a model asset id (model- plus 64 lowercase hex digits)");
    require(Role=="lead"||Role=="crowd",who+"Role must be lead or crowd");
    require(Variants>=1&&Variants<=8,who+"Variants must be 1-8");
    if(Rig){
        require(Rig->Clips.size()<=32&&Rig->Joints.size()<=32,who+"the rig has too many clips or joints (at most 32 each)");
        for(const auto* table:{&Rig->Clips,&Rig->Joints})for(const auto& [key,value]:*table)require(token(key)&&token(value),who+"rig names use 1-64 letters, digits, '-', '_' or '.'");
        if(Rig->HeightRule){const auto& h=*Rig->HeightRule;require(in_range(h.AcceptRange[0],.01,100)&&in_range(h.AcceptRange[1],.01,100)&&h.AcceptRange[0]<h.AcceptRange[1]&&in_range(h.TargetHeight,.01,100),who+"the rig height rule is invalid");}
    }
    if(Uv.Layout=="glb")require(Uv.Asset.empty(),who+"UV layout glb takes no UV blob");
    else if(Uv.Layout=="atlas")require(valid_blob_id(Uv.Asset),who+"UV layout atlas needs a UV blob id (blob- plus 64 lowercase hex digits)");
    else throw std::runtime_error(who+"UV layout must be glb or atlas");
    require(Maps.size()<=8,who+"too many map variants");
    for(const auto& [variant,slots]:Maps){
        int index=0;
        require(variant_key(variant,index)&&index<Variants,who+"map variant '"+variant+"' must be v0 to v"+std::to_string(Variants-1));
        require(slots.size()<=6,who+"too many map slots in "+variant);
        for(const auto& [slot,id]:slots)require(cast_slot(slot)&&EngineAssets::valid_id(AssetKind::Texture,id),who+"map "+variant+"/"+slot+" needs a known slot (basecolor, normal, roughness, ao, furflow, sparkle) and a texture asset id");
    }
    for(const auto& [name,effect]:Effects){
        require(name=="fur",who+"unknown effect '"+name+"' (supported: fur)");
        require(effect.Maps.size()<=6,who+"effect '"+name+"' lists too many maps");
        std::set<std::string> seen;
        for(const auto& slot:effect.Maps)require(cast_slot(slot)&&seen.insert(slot).second,who+"effect '"+name+"' lists an unknown or repeated map slot: "+slot);
        require(effect.Profiles.size()<=8,who+"effect '"+name+"' has too many profiles");
        for(const auto& [profile,p]:effect.Profiles){
            require(token(profile,16),who+"effect '"+name+"' profile names use 1-16 letters, digits, '-', '_' or '.'");
            require(in_range(p.length,0,.5)&&in_range(p.softness,0,1)&&in_range(p.stiffness,0,1)&&in_range(p.sheen,0,1)&&in_range(p.sway,0,1)&&in_range(p.density,0,4),who+"effect '"+name+"' profile '"+profile+"' is out of range");
        }
    }
    require(Requires.size()+Wants.size()<=64,who+"too many Requires and Wants items");
    auto declared_everywhere=[&](const std::string& slot){
        for(int i=0;i<Variants;++i){
            auto variant=Maps.find("v"+std::to_string(i));
            if(variant==Maps.end()||!variant->second.contains(slot))return false;
        }
        return true;
    };
    std::set<std::string> named;
    auto check=[&](const std::vector<std::string>& list,const char* which){
        for(const auto& item:list){
            const std::string prefix=who+which+" item '"+item+"' ";
            require(named.insert(item).second,prefix+"is named more than once in Requires and Wants");
            if(item=="model")continue;
            if(item=="skeleton"){require(Rig&&!Rig->Joints.empty(),prefix+"needs a declared rig with joints");continue;}
            if(item=="uv:atlas"){require(Uv.Layout=="atlas",prefix+"needs UV layout atlas");continue;}
            if(item.starts_with("map:")){
                const auto slot=item.substr(4);
                require(cast_slot(slot),prefix+"names an unknown map slot");
                require(declared_everywhere(slot),prefix+"needs that map declared for every variant");
                continue;
            }
            if(item.starts_with("effect:")){
                const auto effect=Effects.find(item.substr(7));
                require(effect!=Effects.end()&&!effect->second.Maps.empty(),prefix+"needs that effect declared with its maps");
                for(const auto& slot:effect->second.Maps)require(declared_everywhere(slot),prefix+"needs the "+slot+" map declared for every variant");
                continue;
            }
            throw std::runtime_error(prefix+"is not a known item (model, skeleton, uv:atlas, map:<slot>, effect:<name>)");
        }
    };
    check(Requires,"Requires");
    check(Wants,"Wants");
}
void SceneCast::validate() const {
    require(Models.size()<=32,"A scene supports at most 32 cast models");
    std::set<std::string> ids;
    for(const auto& model:Models){model.validate();require(ids.insert(model.Id).second,"Duplicate cast model name: "+model.Id);}
}
std::set<std::string> cast_asset_ids(const SceneData& scene){
    std::set<std::string> ids;
    for(const auto& model:scene.Cast.Models){
        ids.insert(model.Model);
        if(!model.Uv.Asset.empty())ids.insert(model.Uv.Asset);
        for(const auto& [variant,slots]:model.Maps)for(const auto& [slot,id]:slots)ids.insert(id);
    }
    return ids;
}
std::set<std::string> cast_asset_ids(const json& card){
    std::set<std::string> ids;
    if(!card.is_object()||!card.contains("Cast")||!card.at("Cast").is_object()||!card.at("Cast").contains("Models")||!card.at("Cast").at("Models").is_array())return ids;
    auto take=[&](const json& object,const char* key){if(object.is_object()&&object.contains(key)&&object.at(key).is_string()&&!object.at(key).get<std::string>().empty())ids.insert(object.at(key).get<std::string>());};
    for(const auto& model:card.at("Cast").at("Models")){
        if(!model.is_object())continue;
        take(model,"Model");
        if(model.contains("Uv"))take(model.at("Uv"),"Asset");
        if(model.contains("Maps")&&model.at("Maps").is_object())for(const auto& variant:model.at("Maps"))if(variant.is_object())for(auto it=variant.begin();it!=variant.end();++it)if(it.value().is_string())ids.insert(it.value().get<std::string>());
    }
    return ids;
}
std::vector<std::string> cast_missing_assets(const SceneData& scene){
    std::vector<std::string> missing;
    for(const auto& id:cast_asset_ids(scene))if(EngineAssets::locate(id).empty())missing.push_back(id);
    return missing;
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
    require(SchemaVersion == EngineAssets::KeyCardSchema, "Unsupported scene schema");
    require(RaytracingQuality >= 0 && RaytracingQuality <= 4 && PathBounces >= 2 && PathBounces <= 4,
            "Invalid raytracing settings");
    require(finite({BloomStrength}) && BloomStrength>=0 && BloomStrength<=.5 && BloomQuality>=0 && BloomQuality<=3,"Invalid glow settings");
    require(ShadowQuality>=0 && ShadowQuality<=3,"Invalid shadow quality");
    require(finite({WaterWindX,WaterWindZ,WaterWaveStrength})&&std::abs(WaterWindX)<=20&&std::abs(WaterWindZ)<=20&&WaterWaveStrength>=0&&WaterWaveStrength<=1,"Invalid water wind or wave strength");
    require(finite({LavaInitialHeat,LavaCoolingRate,LavaCrustDepth,LavaLightStrength})&&LavaInitialHeat>=0&&LavaInitialHeat<=1&&LavaCoolingRate>=0&&LavaCoolingRate<=1&&LavaCrustDepth>=0&&LavaCrustDepth<=.3&&LavaLightStrength>=0&&LavaLightStrength<=8,"Invalid lava heat, cooling, crust depth or light strength");
    require(finite({Hour, DayLength, FogDensity}) && Hour >= 0 && Hour <= 24 && DayLength >= 10 &&
                DayLength <= 36000 && FogDensity >= 0 && FogDensity <= .3,
            "Invalid atmosphere");
    require(finite({RainIntensity, RainWindX, RainWindZ}) && RainIntensity >= 0 && RainIntensity <= 1 &&
                std::abs(RainWindX) <= 20 && std::abs(RainWindZ) <= 20, "Invalid rain settings");
    require(LiquidSources.size() <= 32, "Too many pours");
    for (const auto &q : LiquidSources)
        require(finite({q.X, q.Z, q.Volume}) && std::abs(q.X) <= 12 && std::abs(q.Z) <= 12 && q.Volume > 0 &&
                    q.Volume <= 20,
                "Invalid liquid source");
    int lights = 0;
    std::set<std::string> objectIds;
    for (const auto &c : Characters) {
        c.validate();
        require(objectIds.insert(c.Id).second, "Duplicate object id: " + c.Id);
        if (c.Shape && (*c.Shape == "fire" || *c.Shape == "light" || *c.Shape == "chaos-lightning" ||
                        *c.Shape == "chaos-rock" || *c.Shape == "light-shard"))
            lights++;
    }
    require(lights <= 32, "Too many local lights");
    std::set<std::string> groups;
    for (const auto &c : Characters)
        if (!c.Group.empty()) groups.insert(c.Group);
    if (Lighting) {
        Lighting->validate();
        for (const auto &[name, targets] : Lighting->Modulators)
            for (const auto &[target, coefficient] : targets)
                if (target.starts_with("group:"))
                    require(groups.contains(target.substr(6)), "Lighting modulator '" + name + "' targets a group no object belongs to: " + target.substr(6));
    }
    require(Moods.size() <= 32, "A scene supports at most 32 moods");
    std::set<std::string> moodIds;
    for (const auto &mood : Moods) {
        mood.validate();
        require(moodIds.insert(mood.Id).second, "Duplicate mood name: " + mood.Id);
    }
    require(ActiveMood.empty() || moodIds.contains(ActiveMood), "The active mood is not one of the scene's moods: " + ActiveMood);
    if (View) View->validate();
    require(Zones.size() <= 64, "A scene supports at most 64 zones");
    std::set<std::string> zoneIds;
    for (const auto &zone : Zones) {
        zone.validate();
        require(zoneIds.insert(zone.Id).second, "Duplicate zone name: " + zone.Id);
    }
    if (Crowd) Crowd->validate();
    Cast.validate();
    if (Terrain)
        Terrain->validate();
    color(GroundColor);
}
SceneData SceneData::decode(std::string_view text) {
    auto s = json::parse(text).get<SceneData>();
    s.validate();
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
