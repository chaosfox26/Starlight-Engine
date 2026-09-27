#include "render_scene.hpp"
#include "platform.hpp"
#include <cstring>
#include <numbers>
namespace starlight::editor {
namespace {
struct Writer {
    std::vector<uint8_t> bytes;
    template <class T> void put(T value) {
        auto p = reinterpret_cast<const uint8_t *>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(T));
    }
    void f(double x) {
        if (!std::isfinite(x) || std::abs(x) > FLT_MAX)
            throw std::runtime_error("Render value outside float range");
        put(float(x));
    }
    void vec(Vec3 p) {
        f(p.x);
        f(p.y);
        f(p.z);
    }
};
std::vector<MeshPart> parts(const Character &c) {
    return c.Shape || c.Sculpt ? shape_geometry(c) : load_glb(wide(c.Asset));
}
} // namespace
std::vector<MeshPart> object_parts(const Character& c){return parts(c);}
Mesh object_mesh(const Character &c) {
    Mesh m;
    for (const auto &part : parts(c)) {
        int offset = int(m.vertices.size());
        m.vertices.insert(m.vertices.end(), part.mesh.vertices.begin(), part.mesh.vertices.end());
        for (int i : part.mesh.indices)
            m.indices.push_back(i + offset);
    }
    return m;
}
std::vector<uint8_t> render_scene(const SceneData &scene, const TerrainFlow *flow,
                                  const std::vector<MeshPart> &overlays, RenderSceneCache *cache, bool runtimeEffects) {
    // Keep terrain and authored static meshes resident while liquids and rigid
    // bodies change. Their motion must not rebuild every blade of grass.
    if(cache && std::any_of(scene.Characters.begin(),scene.Characters.end(),[](const auto& c){return c.Dynamic && (c.Shape=="chaos-rock" || c.Shape=="light-shard" || c.Shape=="light" || c.Shape=="fire" || c.Shape=="chaos-lightning" || c.Shape=="craxis-smoke");}))cache=nullptr;
    if(cache && (cache->runtimeEffects!=runtimeEffects || cache->simulatedLiquids!=(flow!=nullptr))){cache->clear();cache->runtimeEffects=runtimeEffects;cache->simulatedLiquids=flow!=nullptr;}
    if(cache && cache->packet.empty()){
        auto base=scene;
        if(flow && base.Terrain)base.Terrain->WaterEnabled=false;
        std::erase_if(base.Characters,[&](const auto& c){return c.Dynamic || (flow && c.Shape=="lava");});
        cache->packet=render_scene(base,nullptr,{},nullptr,runtimeEffects);
    }
    if(!cache)scene.validate();
    struct Draw {
        MeshPart part;
        const Character *actor;
        bool transient=false;
        uint64_t identity=0;
    };
    std::vector<Draw> draws;
    if (!cache) {
    if (scene.Terrain) {
        auto terrain=*scene.Terrain;if(flow)terrain.WaterEnabled=false;
        for (auto &p : terrain_geometry(terrain))
            draws.push_back({std::move(p), nullptr});
    } else {
        MeshPart ground;
        ground.color = rgb(scene.GroundColor);
        ground.mesh.vertices = {{{-12, 0, -12}, {0, 1, 0}},
                                {{-12, 0, 12}, {0, 1, 0}},
                                {{12, 0, 12}, {0, 1, 0}},
                                {{12, 0, -12}, {0, 1, 0}}};
        ground.mesh.indices = {0, 1, 2, 0, 2, 3};
        draws.push_back({std::move(ground), nullptr});
    }
    }
    for (const auto &c : scene.Characters)
        if((!cache || c.Dynamic) && !(runtimeEffects && (c.Shape=="chaos-lightning" || c.Shape=="fire" || c.Shape=="grass")) && !(flow && c.Shape=="lava"))
        {uint32_t partIndex=0;for (auto &p : parts(c)){
            auto identity=render_identity(c.Id,partIndex++);
            if (!p.mesh.indices.empty())draws.push_back({std::move(p), &c,false,identity});
        }}
    if (flow)
        for (auto &p : flow->geometry())
            draws.push_back({std::move(p), nullptr, true});
    for (const auto &part : overlays)
        draws.push_back({part, nullptr, true});
    uint32_t baseDraws=0;
    if(cache)std::memcpy(&baseDraws,cache->packet.data()+4,sizeof(baseDraws));
    if (baseDraws + draws.size() >= 8192)
        throw std::runtime_error("Native draw capacity exceeded");
    Writer w;
    // Reserve once: changing attack meshes otherwise grow this byte buffer
    // repeatedly on the render thread. This is capacity only, not a format change.
    size_t packetCapacity = 256 + scene.Characters.size() * 64;
    for (const auto& draw : draws)
        packetCapacity += 160 + (draw.actor ? draw.actor->Route.size() * 8 : 0)
                        + draw.part.mesh.vertices.size() * 48
                        + draw.part.mesh.indices.size() * 4;
    w.bytes.reserve(packetCapacity + (cache ? cache->packet.size() : 0));
    if(cache){
        w.bytes.insert(w.bytes.end(),cache->packet.begin(),cache->packet.end());
        uint32_t total=baseDraws+uint32_t(draws.size());
        std::memcpy(w.bytes.data()+4,&total,sizeof(total));
    } else {
    w.put(uint32_t(0x41564c53));
    w.put(uint32_t(draws.size()));
    std::vector<const Character *> lights, smoke;
    for (const auto &c : scene.Characters) {
        auto kind = c.Shape.value_or("");
        if (kind == "fire" || kind == "light" || kind == "chaos-lightning" || kind == "chaos-rock" ||
            kind == "light-shard")
            lights.push_back(&c);
        if (kind == "craxis-smoke" && smoke.size() < 16)
            smoke.push_back(&c);
    }
    w.put(uint32_t(lights.size()));
    for (auto light : lights) {
        auto &c = *light;
        auto kind = *c.Shape;
        auto color = rgb(kind == "chaos-lightning" || kind == "chaos-rock" ? "#C42DFF" : c.Color);
        auto target = world_point(c, kind == "chaos-lightning" ? Vec3{c.TargetX, c.TargetY, c.TargetZ}
                                                               : Vec3{0, .7, 0});
        w.vec(target);
        w.f(c.LightRange);
        auto direction = (world_point(c, {0, -1, 0}) - world_point(c, {0, 0, 0})).unit();
        w.vec(direction);
        w.f(kind == "chaos-lightning" ? -3
            : c.LightCone >= 180      ? -2
                                      : std::cos(c.LightCone * std::numbers::pi / 360));
        w.vec(color * c.LightIntensity);
        w.f(c.LightCone >= 180 ? -1 : std::cos(c.LightCone * std::numbers::pi / 450));
    }
    w.f(scene.Hour);
    w.f(scene.DayCycle ? 1 : 0);
    w.f(scene.DayLength);
    w.f(scene.FogDensity);
    w.put(uint32_t(smoke.size()));
    for (auto c : smoke) {
        w.vec({c->X, c->Y + c->Height * c->Size * .5, c->Z});
        w.f(std::max({c->Width, c->Height, c->Depth}) * c->Size * 1.5);
    }
    w.put(int32_t(scene.RaytracingEnabled ? scene.RaytracingQuality : 0));
    w.put(int32_t(scene.PathBounces));
    w.put(int32_t(scene.FreezeRaytracingAnimation));
    w.put(int32_t(scene.RayReconstructionEnabled));
    }
    for (const auto &draw : draws) {
        const auto *c = draw.actor;
        w.put(draw.identity);
        w.put(uint32_t(draw.transient));
        auto kind = c ? c->Shape.value_or("") : "";
        w.f(draw.part.emissive ? 1 : 0);
        w.put(int32_t(kind == "craxis-smoke" ? 4 : kind == "fire" ? 1 : kind == "lava" ? 2 : 0));
        w.f(c ? c->Y : 0);
        w.vec(draw.part.color);
        w.f(c ? c->X : 0);
        w.f(c ? c->Z : 0);
        w.f(c ? c->Size : 1);
        w.f(c ? c->Speed : 0);
        w.put(uint32_t(c && !c->Dynamic ? c->Route.size() : 0));
        if (c && !c->Dynamic)
            for (auto p : c->Route) {
                w.f(p.X);
                w.f(p.Z);
            }
        Character transform;
        if (c) {
            transform = *c;
            transform.X = transform.Z = 0;
            transform.Y /= transform.Size;
            transform.Size = 1;
        }
        auto origin = world_point(transform, {});
        for (auto basis : {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}}) {
            w.vec(world_point(transform, basis) - origin);
            w.f(0);
        }
        w.vec(origin);
        w.f(1);
        const auto &mesh = draw.part.mesh;
        mesh.validate();
        w.put(uint32_t(mesh.vertices.size()));
        for (const auto &v : mesh.vertices) {
            w.vec(v.position);
            w.vec(v.normal.dot(v.normal) > 1e-12 ? v.normal : Vec3{0, 1, 0});
            w.vec(v.color);
            w.f(v.kind);
            w.put(v.motionIdentity);
        }
        w.put(uint32_t(mesh.indices.size()));
        for (int i : mesh.indices)
            w.put(uint32_t(i));
    }
    return std::move(w.bytes);
}
} // namespace starlight::editor
