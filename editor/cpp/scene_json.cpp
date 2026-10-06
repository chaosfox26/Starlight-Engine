#include "platform.hpp"
#include "scene.hpp"
#include "engine_assets.hpp"
namespace starlight::editor {
namespace {
[[noreturn]] void invalid(const std::string &where, const std::string &what) { throw std::runtime_error(where + ": " + what); }
void only(const json &j, std::initializer_list<const char *> allowed, const std::string &where) {
    if (!j.is_object()) invalid(where, "expected an object");
    for (auto it = j.begin(); it != j.end(); ++it)
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char *key) { return it.key() == key; }))
            invalid(where, "unknown field '" + it.key() + "'");
}
double number(const json &j, const char *key, double fallback, const std::string &where) {
    if (!j.contains(key)) return fallback;
    const auto &value = j.at(key);
    if (!value.is_number()) invalid(where + "." + key, "must be a number");
    return value.get<double>();
}
int whole(const json &j, const char *key, int fallback, const std::string &where) {
    if (!j.contains(key)) return fallback;
    const auto &value = j.at(key);
    if (!value.is_number_integer()) invalid(where + "." + key, "must be a whole number");
    return native_int(value);
}
std::string text(const json &j, const char *key, const std::string &fallback, const std::string &where) {
    if (!j.contains(key)) return fallback;
    const auto &value = j.at(key);
    if (!value.is_string()) invalid(where + "." + key, "must be text");
    return value.get<std::string>();
}
std::string required_text(const json &j, const char *key, const std::string &where) {
    if (!j.contains(key)) invalid(where, std::string("missing required field '") + key + "'");
    return text(j, key, {}, where);
}
std::array<double, 3> triple(const json &j, const char *key, const std::array<double, 3> &fallback, const std::string &where) {
    if (!j.contains(key)) return fallback;
    const auto &value = j.at(key);
    if (!value.is_array() || value.size() != 3 || !std::all_of(value.begin(), value.end(), [](const json &x) { return x.is_number(); }))
        invalid(where + "." + key, "must be three numbers");
    return {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
}
const json &sub(const json &j, const char *key, const std::string &where) {
    const auto &value = j.at(key);
    if (!value.is_object()) invalid(where + "." + key, "must be an object");
    return value;
}
std::vector<std::string> text_list(const json &j, const char *key, const std::string &where, size_t limit) {
    std::vector<std::string> result;
    if (!j.contains(key)) return result;
    const auto &value = j.at(key);
    if (!value.is_array() || value.size() > limit) invalid(where + "." + key, "must be a list of at most " + std::to_string(limit) + " texts");
    for (const auto &item : value) {
        if (!item.is_string()) invalid(where + "." + key, "must contain only text");
        result.push_back(item.get<std::string>());
    }
    return result;
}
std::vector<int> whole_list(const json &j, const char *key, const std::string &where, size_t limit) {
    std::vector<int> result;
    if (!j.contains(key)) return result;
    const auto &value = j.at(key);
    if (!value.is_array() || value.size() > limit) invalid(where + "." + key, "must be a list of at most " + std::to_string(limit) + " whole numbers");
    for (const auto &item : value) {
        if (!item.is_number_integer()) invalid(where + "." + key, "must contain only whole numbers");
        result.push_back(native_int(item));
    }
    return result;
}
std::map<std::string, std::string> text_map(const json &j, const char *key, const std::string &where, size_t limit) {
    std::map<std::string, std::string> result;
    if (!j.contains(key)) return result;
    const auto &value = sub(j, key, where);
    if (value.size() > limit) invalid(where + "." + key, "has too many entries (at most " + std::to_string(limit) + ")");
    for (auto it = value.begin(); it != value.end(); ++it) {
        if (!it.value().is_string()) invalid(where + "." + key + "." + it.key(), "must be text");
        result[it.key()] = it.value().get<std::string>();
    }
    return result;
}
} // namespace
void to_json(json &j, const LightFollow &v) {
    j = {{"Target", v.Target}, {"OffsetX", v.OffsetX}, {"OffsetZ", v.OffsetZ}, {"Lag", v.Lag}, {"Wobble", v.Wobble}, {"PoolRadius", v.PoolRadius}};
    if (v.Housing) j["Housing"] = {{"Width", v.Housing->Width}, {"Height", v.Housing->Height}, {"Depth", v.Housing->Depth}, {"Color", v.Housing->Color}};
    if (v.Beam) j["Beam"] = {{"Haze", v.Beam->Haze}, {"Pool", v.Beam->Pool}, {"InnerPool", v.Beam->InnerPool}};
}
void from_json(const json &j, LightFollow &v) {
    const std::string where = "Follow";
    only(j, {"Target", "OffsetX", "OffsetZ", "Lag", "Wobble", "PoolRadius", "Housing", "Beam"}, where);
    const LightFollow d;
    v.Target = text(j, "Target", d.Target, where);
    v.OffsetX = number(j, "OffsetX", d.OffsetX, where);
    v.OffsetZ = number(j, "OffsetZ", d.OffsetZ, where);
    v.Lag = number(j, "Lag", d.Lag, where);
    v.Wobble = number(j, "Wobble", d.Wobble, where);
    v.PoolRadius = number(j, "PoolRadius", d.PoolRadius, where);
    v.Housing.reset();
    v.Beam.reset();
    if (j.contains("Housing")) {
        const auto &h = sub(j, "Housing", where);
        only(h, {"Width", "Height", "Depth", "Color"}, where + ".Housing");
        const LightFollowHousing hd;
        v.Housing = LightFollowHousing{number(h, "Width", hd.Width, where + ".Housing"), number(h, "Height", hd.Height, where + ".Housing"),
                                       number(h, "Depth", hd.Depth, where + ".Housing"), text(h, "Color", hd.Color, where + ".Housing")};
    }
    if (j.contains("Beam")) {
        const auto &b = sub(j, "Beam", where);
        only(b, {"Haze", "Pool", "InnerPool"}, where + ".Beam");
        const LightFollowBeam bd;
        v.Beam = LightFollowBeam{number(b, "Haze", bd.Haze, where + ".Beam"), number(b, "Pool", bd.Pool, where + ".Beam"),
                                 number(b, "InnerPool", bd.InnerPool, where + ".Beam")};
    }
}
void to_json(json &j, const SceneLighting &v) {
    j = {{"Mode", v.Mode}, {"KeyDirection", v.KeyDirection}, {"KeyIntensity", v.KeyIntensity}, {"Ambient", v.Ambient},
         {"AmbientGround", v.AmbientGround}, {"Clear", v.Clear}, {"FogMix", v.FogMix}};
    if (v.KeyIntensityFlat) j["KeyIntensityFlat"] = *v.KeyIntensityFlat;
    if (!v.Modulators.empty()) j["Modulators"] = v.Modulators;
}
void from_json(const json &j, SceneLighting &v) {
    const std::string where = "Lighting";
    only(j, {"Mode", "KeyDirection", "KeyIntensity", "KeyIntensityFlat", "Ambient", "AmbientGround", "Clear", "FogMix", "Modulators"}, where);
    const SceneLighting d;
    v.Mode = text(j, "Mode", d.Mode, where);
    v.KeyDirection = triple(j, "KeyDirection", d.KeyDirection, where);
    v.KeyIntensity = number(j, "KeyIntensity", d.KeyIntensity, where);
    v.KeyIntensityFlat.reset();
    if (j.contains("KeyIntensityFlat")) v.KeyIntensityFlat = number(j, "KeyIntensityFlat", 0, where);
    v.Ambient = triple(j, "Ambient", d.Ambient, where);
    v.AmbientGround = number(j, "AmbientGround", d.AmbientGround, where);
    v.Clear = triple(j, "Clear", d.Clear, where);
    v.FogMix = number(j, "FogMix", d.FogMix, where);
    v.Modulators.clear();
    if (j.contains("Modulators")) {
        const auto &all = sub(j, "Modulators", where);
        if (all.size() > 16) invalid(where + ".Modulators", "has too many modulators (at most 16)");
        for (auto it = all.begin(); it != all.end(); ++it) {
            const std::string at = where + ".Modulators." + it.key();
            if (!it.value().is_object()) invalid(at, "must be an object of target coefficients");
            if (it.value().size() > 32) invalid(at, "has too many targets (at most 32)");
            for (auto t = it.value().begin(); t != it.value().end(); ++t) {
                if (!t.value().is_number()) invalid(at + "." + t.key(), "must be a number");
                v.Modulators[it.key()][t.key()] = t.value().get<double>();
            }
        }
    }
}
void to_json(json &j, const SceneMood &v) {
    j = {{"Id", v.Id}, {"Clear", v.Clear}};
    if (v.Ambient) j["Ambient"] = *v.Ambient;
    if (v.AmbientGround) j["AmbientGround"] = *v.AmbientGround;
    if (v.KeyIntensity) j["KeyIntensity"] = *v.KeyIntensity;
    if (v.FogMix) j["FogMix"] = *v.FogMix;
}
void from_json(const json &j, SceneMood &v) {
    only(j, {"Id", "Clear", "Ambient", "AmbientGround", "KeyIntensity", "FogMix"}, "Mood");
    v.Id = required_text(j, "Id", "Mood");
    const std::string where = "Mood '" + v.Id + "'";
    if (!j.contains("Clear")) invalid(where, "missing required field 'Clear'");
    v.Clear = triple(j, "Clear", {}, where);
    v.Ambient.reset();
    v.AmbientGround.reset();
    v.KeyIntensity.reset();
    v.FogMix.reset();
    if (j.contains("Ambient")) v.Ambient = triple(j, "Ambient", {}, where);
    if (j.contains("AmbientGround")) v.AmbientGround = number(j, "AmbientGround", 0, where);
    if (j.contains("KeyIntensity")) v.KeyIntensity = number(j, "KeyIntensity", 0, where);
    if (j.contains("FogMix")) v.FogMix = number(j, "FogMix", 0, where);
}
void to_json(json &j, const SceneView &v) {
    j = {{"FovDegrees", v.FovDegrees}, {"Near", v.Near}, {"Far", v.Far}, {"OrbitRateRadiansPerSecond", v.OrbitRateRadiansPerSecond}};
    if (v.FarWhenDistanceAbove) j["FarWhenDistanceAbove"] = {{"Distance", v.FarWhenDistanceAbove->Distance}, {"FarPerDistance", v.FarWhenDistanceAbove->FarPerDistance}, {"FarBase", v.FarWhenDistanceAbove->FarBase}};
    if (v.Follow) j["Follow"] = {{"TargetHeight", v.Follow->TargetHeight}, {"TargetHeightPerY", v.Follow->TargetHeightPerY}, {"DefaultDistance", v.Follow->DefaultDistance}, {"DefaultHeight", v.Follow->DefaultHeight}};
    if (v.IdleOrbit) j["IdleOrbit"] = {{"DelaySeconds", v.IdleOrbit->DelaySeconds}, {"PeriodSeconds", v.IdleOrbit->PeriodSeconds}};
}
void from_json(const json &j, SceneView &v) {
    const std::string where = "View";
    only(j, {"FovDegrees", "Near", "Far", "OrbitRateRadiansPerSecond", "FarWhenDistanceAbove", "Follow", "IdleOrbit"}, where);
    const SceneView d;
    v.FovDegrees = number(j, "FovDegrees", d.FovDegrees, where);
    v.Near = number(j, "Near", d.Near, where);
    v.Far = number(j, "Far", d.Far, where);
    v.OrbitRateRadiansPerSecond = number(j, "OrbitRateRadiansPerSecond", d.OrbitRateRadiansPerSecond, where);
    v.FarWhenDistanceAbove.reset();
    v.Follow.reset();
    v.IdleOrbit.reset();
    if (j.contains("FarWhenDistanceAbove")) {
        const auto &o = sub(j, "FarWhenDistanceAbove", where);
        const std::string at = where + ".FarWhenDistanceAbove";
        only(o, {"Distance", "FarPerDistance", "FarBase"}, at);
        const ViewFarRule rd;
        v.FarWhenDistanceAbove = ViewFarRule{number(o, "Distance", rd.Distance, at), number(o, "FarPerDistance", rd.FarPerDistance, at), number(o, "FarBase", rd.FarBase, at)};
    }
    if (j.contains("Follow")) {
        const auto &o = sub(j, "Follow", where);
        const std::string at = where + ".Follow";
        only(o, {"TargetHeight", "TargetHeightPerY", "DefaultDistance", "DefaultHeight"}, at);
        const ViewFollow fd;
        v.Follow = ViewFollow{number(o, "TargetHeight", fd.TargetHeight, at), number(o, "TargetHeightPerY", fd.TargetHeightPerY, at),
                              number(o, "DefaultDistance", fd.DefaultDistance, at), number(o, "DefaultHeight", fd.DefaultHeight, at)};
    }
    if (j.contains("IdleOrbit")) {
        const auto &o = sub(j, "IdleOrbit", where);
        const std::string at = where + ".IdleOrbit";
        only(o, {"DelaySeconds", "PeriodSeconds"}, at);
        const ViewIdleOrbit od;
        v.IdleOrbit = ViewIdleOrbit{number(o, "DelaySeconds", od.DelaySeconds, at), number(o, "PeriodSeconds", od.PeriodSeconds, at)};
    }
}
void to_json(json &j, const SceneZone &v) {
    j = {{"Id", v.Id}, {"Kind", v.Kind}, {"Shape", v.Shape}, {"X", v.X}, {"Z", v.Z}};
    if (v.Shape == "circle") j["Radius"] = v.Radius;
    else {
        j["Width"] = v.Width;
        j["Depth"] = v.Depth;
    }
    if (v.Height != 0) j["Height"] = v.Height;
    if (!v.Note.empty()) j["Note"] = v.Note;
}
void from_json(const json &j, SceneZone &v) {
    only(j, {"Id", "Kind", "Shape", "X", "Z", "Radius", "Width", "Depth", "Height", "Note"}, "Zone");
    v.Id = required_text(j, "Id", "Zone");
    const std::string where = "Zone '" + v.Id + "'";
    v.Kind = required_text(j, "Kind", where);
    v.Shape = text(j, "Shape", "circle", where);
    v.X = number(j, "X", 0, where);
    v.Z = number(j, "Z", 0, where);
    v.Radius = number(j, "Radius", 0, where);
    v.Width = number(j, "Width", 0, where);
    v.Depth = number(j, "Depth", 0, where);
    v.Height = number(j, "Height", 0, where);
    v.Note = text(j, "Note", {}, where);
}
void to_json(json &j, const SceneCrowd &v) {
    j = {{"Lod", {{"Grids", v.Lod.Grids}, {"EnterPx", v.Lod.EnterPx}, {"ExitPx", v.Lod.ExitPx}}},
         {"Variants", v.Variants},
         {"CullSphere", {{"CentreHeight", v.CullSphere.CentreHeight}, {"Radius", v.CullSphere.Radius}}}};
}
void from_json(const json &j, SceneCrowd &v) {
    const std::string where = "Crowd";
    only(j, {"Lod", "Variants", "CullSphere"}, where);
    const SceneCrowd d;
    v.Lod = d.Lod;
    v.CullSphere = d.CullSphere;
    v.Variants = whole(j, "Variants", d.Variants, where);
    if (j.contains("Lod")) {
        const auto &o = sub(j, "Lod", where);
        only(o, {"Grids", "EnterPx", "ExitPx"}, where + ".Lod");
        v.Lod.Grids = o.contains("Grids") ? whole_list(o, "Grids", where + ".Lod", 4) : d.Lod.Grids;
        v.Lod.EnterPx = o.contains("EnterPx") ? whole_list(o, "EnterPx", where + ".Lod", 4) : d.Lod.EnterPx;
        v.Lod.ExitPx = o.contains("ExitPx") ? whole_list(o, "ExitPx", where + ".Lod", 4) : d.Lod.ExitPx;
    }
    if (j.contains("CullSphere")) {
        const auto &o = sub(j, "CullSphere", where);
        only(o, {"CentreHeight", "Radius"}, where + ".CullSphere");
        v.CullSphere = CrowdCullSphere{number(o, "CentreHeight", d.CullSphere.CentreHeight, where + ".CullSphere"), number(o, "Radius", d.CullSphere.Radius, where + ".CullSphere")};
    }
}
void to_json(json &j, const CastModel &v) {
    j = {{"Id", v.Id}, {"Model", v.Model}, {"Role", v.Role}, {"Variants", v.Variants}, {"Uv", {{"Layout", v.Uv.Layout}}}};
    if (!v.Uv.Asset.empty()) j["Uv"]["Asset"] = v.Uv.Asset;
    if (v.Rig) {
        json rig = json::object();
        rig["Clips"] = v.Rig->Clips;
        rig["Joints"] = v.Rig->Joints;
        if (v.Rig->HeightRule) rig["HeightRule"] = {{"AcceptRange", v.Rig->HeightRule->AcceptRange}, {"TargetHeight", v.Rig->HeightRule->TargetHeight}};
        j["Rig"] = rig;
    }
    if (!v.Maps.empty()) j["Maps"] = v.Maps;
    if (!v.Effects.empty()) {
        json effects = json::object();
        for (const auto &[name, effect] : v.Effects) {
            json e = {{"Maps", effect.Maps}};
            if (!effect.Profiles.empty()) {
                json profiles = json::object();
                for (const auto &[profile, p] : effect.Profiles)
                    profiles[profile] = {{"length", p.length}, {"softness", p.softness}, {"stiffness", p.stiffness}, {"sheen", p.sheen}, {"sway", p.sway}, {"density", p.density}};
                e["Profiles"] = profiles;
            }
            effects[name] = e;
        }
        j["Effects"] = effects;
    }
    if (!v.Requires.empty()) j["Requires"] = v.Requires;
    if (!v.Wants.empty()) j["Wants"] = v.Wants;
}
void from_json(const json &j, CastModel &v) {
    only(j, {"Id", "Model", "Role", "Variants", "Rig", "Uv", "Maps", "Effects", "Requires", "Wants"}, "Cast model");
    v.Id = required_text(j, "Id", "Cast model");
    const std::string where = "Cast model '" + v.Id + "'";
    v.Model = required_text(j, "Model", where);
    v.Role = text(j, "Role", "crowd", where);
    v.Variants = whole(j, "Variants", 1, where);
    v.Rig.reset();
    v.Uv = {};
    v.Maps.clear();
    v.Effects.clear();
    if (j.contains("Rig")) {
        const auto &o = sub(j, "Rig", where);
        const std::string at = where + ".Rig";
        only(o, {"Clips", "Joints", "HeightRule"}, at);
        CastRig rig;
        rig.Clips = text_map(o, "Clips", at, 32);
        rig.Joints = text_map(o, "Joints", at, 32);
        if (o.contains("HeightRule")) {
            const auto &h = sub(o, "HeightRule", at);
            only(h, {"AcceptRange", "TargetHeight"}, at + ".HeightRule");
            CastHeightRule rule;
            if (h.contains("AcceptRange")) {
                const auto &r = h.at("AcceptRange");
                if (!r.is_array() || r.size() != 2 || !r[0].is_number() || !r[1].is_number()) invalid(at + ".HeightRule.AcceptRange", "must be two numbers");
                rule.AcceptRange = {r[0].get<double>(), r[1].get<double>()};
            }
            rule.TargetHeight = number(h, "TargetHeight", rule.TargetHeight, at + ".HeightRule");
            rig.HeightRule = rule;
        }
        v.Rig = std::move(rig);
    }
    if (j.contains("Uv")) {
        const auto &o = sub(j, "Uv", where);
        only(o, {"Layout", "Asset"}, where + ".Uv");
        v.Uv.Layout = text(o, "Layout", "glb", where + ".Uv");
        v.Uv.Asset = text(o, "Asset", {}, where + ".Uv");
    }
    if (j.contains("Maps")) {
        const auto &variants = sub(j, "Maps", where);
        if (variants.size() > 8) invalid(where + ".Maps", "has too many variants (at most 8)");
        for (auto it = variants.begin(); it != variants.end(); ++it) {
            if (!it.value().is_object()) invalid(where + ".Maps." + it.key(), "must be an object of slot -> texture id");
            if (it.value().size() > 6) invalid(where + ".Maps." + it.key(), "has too many slots (at most 6)");
            for (auto s = it.value().begin(); s != it.value().end(); ++s) {
                if (!s.value().is_string()) invalid(where + ".Maps." + it.key() + "." + s.key(), "must be a texture asset id");
                v.Maps[it.key()][s.key()] = s.value().get<std::string>();
            }
        }
    }
    if (j.contains("Effects")) {
        const auto &effects = sub(j, "Effects", where);
        if (effects.size() > 4) invalid(where + ".Effects", "has too many effects (at most 4)");
        for (auto it = effects.begin(); it != effects.end(); ++it) {
            const std::string at = where + ".Effects." + it.key();
            const auto &e = it.value();
            only(e, {"Maps", "Profiles"}, at);
            CastEffect effect;
            effect.Maps = text_list(e, "Maps", at, 6);
            if (e.contains("Profiles")) {
                const auto &profiles = sub(e, "Profiles", at);
                if (profiles.size() > 8) invalid(at + ".Profiles", "has too many profiles (at most 8)");
                for (auto p = profiles.begin(); p != profiles.end(); ++p) {
                    const std::string pat = at + ".Profiles." + p.key();
                    only(p.value(), {"length", "softness", "stiffness", "sheen", "sway", "density"}, pat);
                    const FurProfile d;
                    effect.Profiles[p.key()] = FurProfile{number(p.value(), "length", d.length, pat), number(p.value(), "softness", d.softness, pat),
                                                          number(p.value(), "stiffness", d.stiffness, pat), number(p.value(), "sheen", d.sheen, pat),
                                                          number(p.value(), "sway", d.sway, pat), number(p.value(), "density", d.density, pat)};
                }
            }
            v.Effects[it.key()] = std::move(effect);
        }
    }
    v.Requires = text_list(j, "Requires", where, 64);
    v.Wants = text_list(j, "Wants", where, 64);
}
void to_json(json &j, const SceneCast &v) { j = {{"Models", v.Models}}; }
void from_json(const json &j, SceneCast &v) {
    only(j, {"Models"}, "Cast");
    v.Models.clear();
    if (!j.contains("Models")) return;
    const auto &models = j.at("Models");
    if (!models.is_array() || models.size() > 32) invalid("Cast.Models", "must be a list of at most 32 models");
    models.get_to(v.Models);
}
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
    j["Breakable"] = v.Breakable;
    j["DestructionHealth"] = v.DestructionHealth;
    if(v.GrappleMode)j["GrappleMode"]=v.GrappleMode;
    if(v.CombatHealth>0){j["CombatHealth"]=v.CombatHealth;j["CombatSightRange"]=v.CombatSightRange;j["CombatTeam"]=v.CombatTeam;j["CombatBoss"]=v.CombatBoss;j["CombatAutoAttack"]=v.CombatAutoAttack;j["CombatPursuit"]=v.CombatPursuit;j["CombatControlEffect"]=v.CombatControlEffect;}
    if(v.GameplayRole!="none"){j["GameplayRole"]=v.GameplayRole;j["GameplayKey"]=v.GameplayKey;}
    j["Name"] = v.Name;
    j["Asset"] = v.Asset;
    j["Shape"] = v.Shape ? json(*v.Shape) : json(nullptr);
    j["Sculpt"] = v.Sculpt ? json(*v.Sculpt) : json(nullptr);
    if(!v.LightningTargetId.empty())j["LightningTargetId"]=v.LightningTargetId;
    j["TargetX"] = v.TargetX;
    j["TargetY"] = v.TargetY;
    j["TargetZ"] = v.TargetZ;
    j["BranchSpread"] = v.BranchSpread;
    j["Dynamic"] = v.Dynamic;
    j["Density"] = v.Density;
    j["VelocityX"]=v.VelocityX;
    j["VelocityY"]=v.VelocityY;
    j["VelocityZ"]=v.VelocityZ;
    j["AngularVelocityX"]=v.AngularVelocityX;
    j["AngularVelocityY"]=v.AngularVelocityY;
    j["AngularVelocityZ"]=v.AngularVelocityZ;
    j["JointAngleMin"]=v.JointAngleMin;
    j["JointAngleMax"]=v.JointAngleMax;
    j["JointSegments"]=v.JointSegments;
    j["JointRadius"]=v.JointRadius;
    j["CombatProjectileSpeed"]=v.CombatProjectileSpeed;
    j["CombatProjectileGravity"]=v.CombatProjectileGravity;
    j["CombatProjectileMass"]=v.CombatProjectileMass;
    j["JointKind"]=v.JointKind;
    j["JointTargetId"]=v.JointTargetId;
    j["JointX"]=v.JointX;
    j["JointY"]=v.JointY;
    j["JointZ"]=v.JointZ;
    j["JointTargetX"]=v.JointTargetX;
    j["JointTargetY"]=v.JointTargetY;
    j["JointTargetZ"]=v.JointTargetZ;
    j["JointAxisX"]=v.JointAxisX;
    j["JointAxisY"]=v.JointAxisY;
    j["JointAxisZ"]=v.JointAxisZ;
    j["JointLength"]=v.JointLength;
    j["JointFrequency"]=v.JointFrequency;
    j["JointDamping"]=v.JointDamping;
    j["LightningDamage"]=v.LightningDamage;
    j["LightningChainRadius"]=v.LightningChainRadius;
    j["LightningChains"]=v.LightningChains;
    j["ParticleRate"]=v.ParticleRate;
    j["ParticleLifetime"]=v.ParticleLifetime;
    j["ParticleDrag"]=v.ParticleDrag;
    j["ParticleLift"]=v.ParticleLift;
    j["ParticleWindX"]=v.ParticleWindX;
    j["ParticleWindY"]=v.ParticleWindY;
    j["ParticleWindZ"]=v.ParticleWindZ;
    j["Friction"] = v.Friction; j["Restitution"] = v.Restitution;
    j["Surface"] = v.Surface;
    if(v.Foliage)j["Foliage"]=*v.Foliage;
    j["LightCone"] = v.LightCone;
    j["LightIntensity"] = v.LightIntensity;
    j["LightRange"] = v.LightRange;
    if(v.LightInner!=.8)j["LightInner"]=v.LightInner;
    if(v.Glow!=0)j["Glow"]=v.Glow;
    if(!v.Group.empty())j["Group"]=v.Group;
    if(v.RingInner!=.82)j["RingInner"]=v.RingInner;
    if(v.Follow)j["Follow"]=*v.Follow;
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
    v.JointRuntimePoints.clear();
    v.GrappleMode=j.value("GrappleMode",0);
    v.CombatSightRange=j.value("CombatSightRange",60.);v.CombatHealth=j.value("CombatHealth",0.);v.CombatTeam=j.value("CombatTeam",2);v.CombatBoss=j.value("CombatBoss",false);v.CombatAutoAttack=j.value("CombatAutoAttack",false);v.CombatPursuit=j.value("CombatPursuit",false);v.CombatControlEffect=j.value("CombatControlEffect",-1);
    v.GameplayRole=j.value("GameplayRole",std::string("none"));v.GameplayKey=j.value("GameplayKey",std::string{});
    v.Breakable=j.value("Breakable",true);v.DestructionHealth=j.value("DestructionHealth",10.);
    if (j.contains("Id"))
        j.at("Id").get_to(v.Id);
    if (j.contains("Name"))
        j.at("Name").get_to(v.Name);
    if (j.contains("Asset")) {
        j.at("Asset").get_to(v.Asset);
        if (!v.Asset.empty() && !EngineAssets::valid_id(AssetKind::Model, v.Asset))
            throw ConversionRequired("This scene refers to an imported model by file path (the pre-key-card format). Convert it with Starlight.SceneMigration, which imports the model into the asset library, before opening it.");
    }
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
    if(j.contains("LightningTargetId"))j.at("LightningTargetId").get_to(v.LightningTargetId);
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
    if(j.contains("VelocityX"))j.at("VelocityX").get_to(v.VelocityX);
    if(j.contains("VelocityY"))j.at("VelocityY").get_to(v.VelocityY);
    if(j.contains("VelocityZ"))j.at("VelocityZ").get_to(v.VelocityZ);
    if(j.contains("AngularVelocityX"))j.at("AngularVelocityX").get_to(v.AngularVelocityX);
    if(j.contains("AngularVelocityY"))j.at("AngularVelocityY").get_to(v.AngularVelocityY);
    if(j.contains("AngularVelocityZ"))j.at("AngularVelocityZ").get_to(v.AngularVelocityZ);
    if(j.contains("JointAngleMin"))j.at("JointAngleMin").get_to(v.JointAngleMin);
    if(j.contains("JointAngleMax"))j.at("JointAngleMax").get_to(v.JointAngleMax);
    if(j.contains("JointSegments"))v.JointSegments=native_int(j.at("JointSegments"));
    if(j.contains("JointRadius"))j.at("JointRadius").get_to(v.JointRadius);
    if(j.contains("CombatProjectileSpeed"))j.at("CombatProjectileSpeed").get_to(v.CombatProjectileSpeed);
    if(j.contains("CombatProjectileGravity"))j.at("CombatProjectileGravity").get_to(v.CombatProjectileGravity);
    if(j.contains("CombatProjectileMass"))j.at("CombatProjectileMass").get_to(v.CombatProjectileMass);
    if(j.contains("JointKind"))v.JointKind=native_int(j.at("JointKind"));
    if(j.contains("JointTargetId"))j.at("JointTargetId").get_to(v.JointTargetId);
    if(j.contains("JointX"))j.at("JointX").get_to(v.JointX);
    if(j.contains("JointY"))j.at("JointY").get_to(v.JointY);
    if(j.contains("JointZ"))j.at("JointZ").get_to(v.JointZ);
    if(j.contains("JointTargetX"))j.at("JointTargetX").get_to(v.JointTargetX);
    if(j.contains("JointTargetY"))j.at("JointTargetY").get_to(v.JointTargetY);
    if(j.contains("JointTargetZ"))j.at("JointTargetZ").get_to(v.JointTargetZ);
    if(j.contains("JointAxisX"))j.at("JointAxisX").get_to(v.JointAxisX);
    if(j.contains("JointAxisY"))j.at("JointAxisY").get_to(v.JointAxisY);
    if(j.contains("JointAxisZ"))j.at("JointAxisZ").get_to(v.JointAxisZ);
    if(j.contains("JointLength"))j.at("JointLength").get_to(v.JointLength);
    if(j.contains("JointFrequency"))j.at("JointFrequency").get_to(v.JointFrequency);
    if(j.contains("JointDamping"))j.at("JointDamping").get_to(v.JointDamping);
    if(j.contains("LightningDamage"))j.at("LightningDamage").get_to(v.LightningDamage);
    if(j.contains("LightningChainRadius"))j.at("LightningChainRadius").get_to(v.LightningChainRadius);
    if(j.contains("LightningChains"))v.LightningChains=native_int(j.at("LightningChains"));
    if(j.contains("ParticleRate"))j.at("ParticleRate").get_to(v.ParticleRate);
    if(j.contains("ParticleLifetime"))j.at("ParticleLifetime").get_to(v.ParticleLifetime);
    if(j.contains("ParticleDrag"))j.at("ParticleDrag").get_to(v.ParticleDrag);
    if(j.contains("ParticleLift"))j.at("ParticleLift").get_to(v.ParticleLift);
    if(j.contains("ParticleWindX"))j.at("ParticleWindX").get_to(v.ParticleWindX);
    if(j.contains("ParticleWindY"))j.at("ParticleWindY").get_to(v.ParticleWindY);
    if(j.contains("ParticleWindZ"))j.at("ParticleWindZ").get_to(v.ParticleWindZ);
    if(j.contains("Friction"))j.at("Friction").get_to(v.Friction);
    if(j.contains("Restitution"))j.at("Restitution").get_to(v.Restitution);
    if (j.contains("Surface"))
        v.Surface = native_int(j.at("Surface"));
    v.Foliage=j.contains("Foliage")?std::optional<FoliageMaterial>(j.at("Foliage").get<FoliageMaterial>()):std::nullopt;
    if (j.contains("LightCone"))
        j.at("LightCone").get_to(v.LightCone);
    if (j.contains("LightIntensity"))
        j.at("LightIntensity").get_to(v.LightIntensity);
    if (j.contains("LightRange"))
        j.at("LightRange").get_to(v.LightRange);
    {
        const std::string where = "Object '" + v.Id + "'";
        v.LightInner = number(j, "LightInner", .8, where);
        v.Glow = number(j, "Glow", 0., where);
        v.Group = text(j, "Group", std::string{}, where);
        v.RingInner = number(j, "RingInner", .82, where);
        v.Follow = j.contains("Follow") ? std::optional<LightFollow>(j.at("Follow").get<LightFollow>()) : std::nullopt;
    }
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
       {"ProjectileMass",v.ProjectileMass},{"ProjectileSpeed",v.ProjectileSpeed},{"ProjectileGravity",v.ProjectileGravity},{"MoveSpeed",v.MoveSpeed},{"LookSensitivity",v.LookSensitivity},{"PlayerRadius",v.PlayerRadius},{"Collision",v.Collision},{"ReducedCameraMotion",v.ReducedCameraMotion},{"SightRange",v.SightRange}};
}
void from_json(const json &j, GameSettings &v) {
    if(!j.is_object())throw std::runtime_error("Expected game settings object");
    v.Preset=j.value("Preset",v.Preset);v.SpawnX=j.value("SpawnX",v.SpawnX);
    v.SpawnY=j.value("SpawnY",v.SpawnY);v.SpawnZ=j.value("SpawnZ",v.SpawnZ);
    v.ProjectileMass=j.value("ProjectileMass",.02);v.ProjectileSpeed=j.value("ProjectileSpeed",0.);v.ProjectileGravity=j.value("ProjectileGravity",0.);
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
    j["RainIntensity"] = v.RainIntensity;
    j["WaterWindX"]=v.WaterWindX;j["WaterWindZ"]=v.WaterWindZ;j["WaterWaveStrength"]=v.WaterWaveStrength;
    j["LavaInitialHeat"]=v.LavaInitialHeat;j["LavaCoolingRate"]=v.LavaCoolingRate;j["LavaCrustDepth"]=v.LavaCrustDepth;j["LavaLightStrength"]=v.LavaLightStrength;
    j["BloomStrength"] = v.BloomStrength;
    j["BloomQuality"] = v.BloomQuality;
    j["ShadowQuality"] = v.ShadowQuality;
    j["RainWindX"] = v.RainWindX;
    j["RainWindZ"] = v.RainWindZ;
    j["RayReconstructionEnabled"] = v.RayReconstructionEnabled;
    j["RaytracingEnabled"] = v.RaytracingEnabled;
    j["RaytracingQuality"] = v.RaytracingQuality;
    j["PathBounces"] = v.PathBounces;
    j["FreezeRaytracingAnimation"] = v.FreezeRaytracingAnimation;
    j["SchemaVersion"] = EngineAssets::KeyCardSchema;
    if(v.Game)j["Game"]=*v.Game;
    if(!v.Rules.empty())j["Rules"]=v.Rules;
    if(!v.Quests.empty())j["Quests"]=v.Quests;
    j["Name"] = v.Name;
    j["Characters"] = v.Characters;
    j["LiquidSources"] = v.LiquidSources;
    j["Terrain"] = v.Terrain ? json(*v.Terrain) : json(nullptr);
    j["GroundColor"] = v.GroundColor;
    if(v.Lighting)j["Lighting"]=*v.Lighting;
    if(!v.Moods.empty())j["Moods"]=v.Moods;
    if(!v.ActiveMood.empty())j["ActiveMood"]=v.ActiveMood;
    if(v.View)j["View"]=*v.View;
    if(!v.Zones.empty())j["Zones"]=v.Zones;
    if(v.Crowd)j["Crowd"]=*v.Crowd;
    if(!v.Cast.Models.empty())j["Cast"]=v.Cast;
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
    v.RainIntensity = j.value("RainIntensity", 0.);
    v.BloomStrength = j.value("BloomStrength", .12);
    v.BloomQuality = j.contains("BloomQuality")?native_int(j.at("BloomQuality")):2;
    v.WaterWindX=j.value("WaterWindX",1.5);v.WaterWindZ=j.value("WaterWindZ",.5);v.WaterWaveStrength=j.value("WaterWaveStrength",.35);
    v.LavaInitialHeat=j.value("LavaInitialHeat",1.);v.LavaCoolingRate=j.value("LavaCoolingRate",.012);v.LavaCrustDepth=j.value("LavaCrustDepth",.08);v.LavaLightStrength=j.value("LavaLightStrength",1.);
    v.ShadowQuality = j.contains("ShadowQuality")?native_int(j.at("ShadowQuality")):2;
    v.RainWindX = j.value("RainWindX", 0.);
    v.RainWindZ = j.value("RainWindZ", 0.);
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
    if (!j.contains("SchemaVersion") || !j.at("SchemaVersion").is_number_integer() || j.at("SchemaVersion").get<int64_t>() < EngineAssets::KeyCardSchema)
        throw ConversionRequired("This scene uses the pre-key-card format (schema older than 16). Convert it with Starlight.SceneMigration before opening it.");
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
    auto present = [&](const char *key) { return j.contains(key) && !j.at(key).is_null(); };
    v.Lighting.reset();
    v.Moods.clear();
    v.ActiveMood.clear();
    v.View.reset();
    v.Zones.clear();
    v.Crowd.reset();
    v.Cast = {};
    if (present("Lighting")) v.Lighting = j.at("Lighting").get<SceneLighting>();
    if (present("Moods")) {
        const auto &moods = j.at("Moods");
        if (!moods.is_array() || moods.size() > 32) throw std::runtime_error("Moods: must be a list of at most 32 moods");
        moods.get_to(v.Moods);
    }
    v.ActiveMood = text(j, "ActiveMood", std::string{}, "Scene");
    if (present("View")) v.View = j.at("View").get<SceneView>();
    if (present("Zones")) {
        const auto &zones = j.at("Zones");
        if (!zones.is_array() || zones.size() > 64) throw std::runtime_error("Zones: must be a list of at most 64 zones");
        zones.get_to(v.Zones);
    }
    if (present("Crowd")) v.Crowd = j.at("Crowd").get<SceneCrowd>();
    if (present("Cast")) v.Cast = j.at("Cast").get<SceneCast>();
}
} // namespace starlight::editor
