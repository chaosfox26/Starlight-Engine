#include "keycard_view.h"
#include "assets.hpp"
#include "color.hpp"
#include "engine_assets.hpp"
#include "mesh.hpp"
#include "platform.hpp"
#include "scene.hpp"
#include <cmath>
#include <stdexcept>

namespace ed = starlight::editor;

namespace keycard {
namespace {

Rgb toRgb(const std::string& text) {
    const auto v = ed::color_rgb(text);
    return Rgb{ float(v[0]), float(v[1]), float(v[2]) };
}

Rgb toRgb(const std::array<double, 3>& v) { return Rgb{ float(v[0]), float(v[1]), float(v[2]) }; }

void store(float (&out)[3], const ed::Vec3& v) {
    out[0] = float(v.x);
    out[1] = float(v.y);
    out[2] = float(-v.z);
    if (!std::isfinite(out[0]) || !std::isfinite(out[1]) || !std::isfinite(out[2])) throw std::runtime_error("an object lies beyond the range the host can draw");
}

float finite(double value, float fallback) { return std::isfinite(value) ? float(value) : fallback; }

Light makeLight(const ed::Character& c, const std::vector<Zone>& zones) {
    Light light;
    light.id = c.Id;
    store(light.position, ed::world_point(c, { 0, 0.7, 0 }));
    const ed::Vec3 direction = (ed::world_point(c, { 0, -1, 0 }) - ed::world_point(c, {})).unit();
    store(light.direction, direction);
    light.color = toRgb(c.Color);
    light.intensity = float(c.LightIntensity);
    light.range = float(c.LightRange);
    light.coneDegrees = float(c.LightCone);
    light.innerRatio = float(c.LightInner);
    if (!c.Follow) return light;
    const ed::LightFollow& f = *c.Follow;
    light.follow = true;
    Follow& rig = light.rig;
    rig.offsetX = float(f.OffsetX);
    rig.offsetZ = float(-f.OffsetZ);
    rig.topY = float(c.Y + 0.7 * c.Height * c.Size);
    rig.lag = float(f.Lag);
    rig.wobble = float(f.Wobble);
    rig.poolRadius = float(f.PoolRadius);
    if (f.Housing) {
        rig.housing = true;
        rig.housingSize[0] = float(f.Housing->Width);
        rig.housingSize[1] = float(f.Housing->Height);
        rig.housingSize[2] = float(f.Housing->Depth);
        rig.housingColor = toRgb(f.Housing->Color);
    }
    if (f.Beam) {
        rig.beam = true;
        rig.haze = float(f.Beam->Haze);
        rig.pool = float(f.Beam->Pool);
        rig.innerPool = float(f.Beam->InnerPool);
    }
    float hitHeight = 0.02f;
    for (const Zone& zone : zones)
        if (zone.kind == "raised-floor" && zone.contains(0, 0)) hitHeight += zone.height;
    const float vertical = rig.topY - hitHeight;
    const float slant = std::sqrt(rig.offsetX * rig.offsetX + rig.offsetZ * rig.offsetZ + vertical * vertical);
    if (slant > 1e-3f && light.range > 0) rig.restRangeRatio = light.range / slant;
    return light;
}

bool drawable(const std::string& shape) { return shape == "cube" || shape == "cylinder" || shape == "ring"; }

Part makePart(const ed::Character& c) {
    Part part;
    part.id = c.Id;
    part.shape = *c.Shape;
    part.group = c.Group;
    part.color = toRgb(c.Color);
    part.glow = float(c.Glow);
    part.ringInner = float(c.RingInner);
    const ed::Vec3 origin = ed::world_point(c, {});
    const ed::Vec3 ex = ed::world_point(c, { 1, 0, 0 }) - origin;
    const ed::Vec3 ey = ed::world_point(c, { 0, 1, 0 }) - origin;
    const ed::Vec3 ez = ed::world_point(c, { 0, 0, 1 }) - origin;
    store(part.origin, origin);
    store(part.axisX, ex);
    store(part.axisY, ey);
    store(part.axisZ, ed::Vec3{ -ez.x, -ez.y, -ez.z });
    return part;
}

}

std::string loadKeyCard(const std::string& cardPath, KeyCard& out) {
    try {
        out = KeyCard{};
        const std::filesystem::path path = std::filesystem::absolute(pathFromUtf8(cardPath));
        ed::EngineAssets::discover_near(path.parent_path());
        const std::string bytes = ed::read_file(path);
        const ed::SceneData scene = ed::SceneData::decode(bytes);
        scene.validate();
        out.name = scene.Name;
        out.path = pathToUtf8(path);
        out.activeMood = scene.ActiveMood;

        for (const ed::SceneZone& z : scene.Zones) {
            Zone zone;
            zone.id = z.Id;
            zone.kind = z.Kind;
            zone.shape = z.Shape;
            zone.note = z.Note;
            zone.x = float(z.X);
            zone.z = float(-z.Z);
            zone.radius = float(z.Radius);
            zone.width = float(z.Width);
            zone.depth = float(z.Depth);
            zone.height = float(z.Height);
            out.zones.push_back(std::move(zone));
        }

        for (const ed::Character& c : scene.Characters) {
            if (c.Shape && *c.Shape == "light") {
                out.lights.push_back(makeLight(c, out.zones));
            } else if (c.Shape && drawable(*c.Shape)) {
                out.parts.push_back(makePart(c));
            } else {
                out.skipped.push_back("object " + c.Id + (c.Shape ? " (shape " + *c.Shape + ")" : std::string(" (model or sculpt)")) + " is not drawn by this host");
            }
        }

        if (scene.Lighting) {
            const ed::SceneLighting& l = *scene.Lighting;
            Lighting& out_l = out.lighting;
            out_l.present = true;
            out_l.authored = l.Mode == "authored";
            out_l.keyDirection[0] = float(l.KeyDirection[0]);
            out_l.keyDirection[1] = float(l.KeyDirection[1]);
            out_l.keyDirection[2] = float(-l.KeyDirection[2]);
            out_l.keyIntensity = float(l.KeyIntensity);
            out_l.keyIntensityFlat = l.KeyIntensityFlat ? float(*l.KeyIntensityFlat) : -1.0f;
            out_l.ambient = toRgb(l.Ambient);
            out_l.ambientGround = float(l.AmbientGround);
            out_l.clear = toRgb(l.Clear);
            out_l.fogMix = float(l.FogMix);
            for (const auto& [name, targets] : l.Modulators)
                for (const auto& [target, coefficient] : targets) out_l.modulators[name][target] = float(coefficient);
        }

        for (const ed::SceneMood& m : scene.Moods) {
            Mood mood;
            mood.id = m.Id;
            mood.clear = toRgb(m.Clear);
            if (m.Ambient) { mood.hasAmbient = true; mood.ambient = toRgb(*m.Ambient); }
            if (m.AmbientGround) { mood.hasAmbientGround = true; mood.ambientGround = float(*m.AmbientGround); }
            if (m.KeyIntensity) { mood.hasKeyIntensity = true; mood.keyIntensity = float(*m.KeyIntensity); }
            if (m.FogMix) { mood.hasFogMix = true; mood.fogMix = float(*m.FogMix); }
            out.moods.push_back(std::move(mood));
        }

        if (scene.View) {
            const ed::SceneView& v = *scene.View;
            View& view = out.view;
            view.present = true;
            view.fovDegrees = float(v.FovDegrees);
            view.nearPlane = float(v.Near);
            view.farPlane = float(v.Far);
            view.orbitRate = float(v.OrbitRateRadiansPerSecond);
            if (v.FarWhenDistanceAbove) {
                view.farRule = true;
                view.farDistance = float(v.FarWhenDistanceAbove->Distance);
                view.farPerDistance = float(v.FarWhenDistanceAbove->FarPerDistance);
                view.farBase = float(v.FarWhenDistanceAbove->FarBase);
            }
            if (v.Follow) {
                view.follow = true;
                view.targetHeight = float(v.Follow->TargetHeight);
                view.targetHeightPerY = float(v.Follow->TargetHeightPerY);
                view.defaultDistance = float(v.Follow->DefaultDistance);
                view.defaultHeight = float(v.Follow->DefaultHeight);
            }
            if (v.IdleOrbit) {
                view.idleOrbit = true;
                view.idleDelaySeconds = float(v.IdleOrbit->DelaySeconds);
                view.idlePeriodSeconds = float(v.IdleOrbit->PeriodSeconds);
            }
        }

        if (scene.Crowd) {
            Crowd& crowd = out.crowd;
            crowd.present = true;
            crowd.grids = scene.Crowd->Lod.Grids;
            crowd.enterPx = scene.Crowd->Lod.EnterPx;
            crowd.exitPx = scene.Crowd->Lod.ExitPx;
            crowd.variants = scene.Crowd->Variants;
            crowd.cullCentreHeight = float(scene.Crowd->CullSphere.CentreHeight);
            crowd.cullRadius = float(scene.Crowd->CullSphere.Radius);
        }

        if (scene.Game) {
            out.hasSpawn = true;
            out.spawn[0] = float(scene.Game->SpawnX);
            out.spawn[1] = float(scene.Game->SpawnY);
            out.spawn[2] = float(-scene.Game->SpawnZ);
        }

        for (const ed::CastModel& m : scene.Cast.Models) {
            CastModel model;
            model.id = m.Id;
            model.modelAsset = m.Model;
            model.role = m.Role;
            model.variants = m.Variants;
            if (m.Rig) {
                model.hasRig = true;
                model.clips = m.Rig->Clips;
                model.joints = m.Rig->Joints;
                if (m.Rig->HeightRule) {
                    model.hasHeightRule = true;
                    model.acceptMin = float(m.Rig->HeightRule->AcceptRange[0]);
                    model.acceptMax = float(m.Rig->HeightRule->AcceptRange[1]);
                    model.targetHeight = float(m.Rig->HeightRule->TargetHeight);
                }
            }
            model.uvLayout = m.Uv.Layout;
            model.uvAsset = m.Uv.Asset;
            model.maps = m.Maps;
            for (const auto& [name, effect] : m.Effects) {
                Effect converted;
                converted.maps = effect.Maps;
                for (const auto& [sex, profile] : effect.Profiles)
                    converted.profiles[sex] = FurProfile{ float(profile.length), float(profile.softness), float(profile.stiffness), float(profile.sheen), float(profile.sway), float(profile.density) };
                model.effects[name] = std::move(converted);
            }
            model.requiredItems = m.Requires;
            model.wantedItems = m.Wants;
            out.cast.push_back(std::move(model));
        }
        return std::string();
    } catch (const std::exception& error) {
        out = KeyCard{};
        return std::string("The key card ") + cardPath + " cannot be loaded: " + error.what();
    }
}

bool assetPresent(AssetClass kind, const std::string& id, std::string& why) {
    try {
        const ed::AssetKind asset = kind == AssetClass::Model ? ed::AssetKind::Model : kind == AssetClass::Texture ? ed::AssetKind::Texture : ed::AssetKind::Blob;
        if (!ed::EngineAssets::valid_id(asset, id)) { why = "the key card names an invalid asset id"; return false; }
        if (!ed::EngineAssets::locate(id).empty()) return true;
        why = "the asset is not in any registered asset root";
        return false;
    } catch (const std::exception& error) {
        why = error.what();
        return false;
    }
}

std::string fetchAsset(AssetClass kind, const std::string& id, AssetBytes& out) {
    try {
        out = AssetBytes{};
        if (kind == AssetClass::Blob) {
            ed::BlobData blob = ed::load_blob(id);
            out.keep = blob.envelope;
            out.data = reinterpret_cast<const uint8_t*>(blob.data.data());
            out.size = blob.data.size();
            out.blobType = blob.type;
            out.blobVersion = blob.version;
            return std::string();
        }
        std::shared_ptr<const std::string> bytes = kind == AssetClass::Model ? ed::load_model_bytes(id) : ed::load_texture(id);
        out.keep = bytes;
        out.data = reinterpret_cast<const uint8_t*>(bytes->data());
        out.size = bytes->size();
        return std::string();
    } catch (const std::exception& error) {
        out = AssetBytes{};
        return error.what();
    }
}

void releaseLibraryCache() {
    ed::EngineAssets::drop_memory_cache();
    ed::model_service_trim();
}

LibraryStatus libraryStatus() {
    LibraryStatus status;
    try {
        status.description = ed::EngineAssets::describe_roots();
        status.problems = ed::EngineAssets::root_problems();
    } catch (const std::exception& error) {
        status.problems = error.what();
    }
    return status;
}

}
