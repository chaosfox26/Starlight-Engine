#include "commands.hpp"
#include "assets.hpp"
#include "features.hpp"
#include <set>
namespace starlight::editor {
namespace {
void keys(const json &object, std::initializer_list<std::string_view> allowed) {
    if (!object.is_object())
        throw std::runtime_error("Expected command object");
    for (auto it = object.begin(); it != object.end(); ++it)
        if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            throw std::runtime_error("Unknown command setting: " + it.key());
}
Character &find(SceneData &scene, const json &op) {
    auto id = op.at("id").get<std::string>();
    Character *found = nullptr;
    for (auto &c : scene.Characters)
        if (c.Id == id) {
            if (found)
                throw std::runtime_error("Ambiguous object ID");
            found = &c;
        }
    if (!found)
        throw std::runtime_error("Object ID not found");
    return *found;
}
void snap(SceneData &scene, Character &c) {
    if (!c.KeepAboveFloor)
        return;
    auto parts = c.Shape || c.Sculpt ? shape_geometry(c) : load_glb(wide(c.Asset));
    auto transform = c;
    transform.Y = 0;
    double minimum = INFINITY;
    for (const auto &part : parts)
        for (const auto &v : part.mesh.vertices)
            minimum = std::min(minimum, world_point(transform, v.position).y);
    if (std::isfinite(minimum))
        c.Y = std::max(c.Y, (scene.Terrain ? scene.Terrain->height_at(c.X, c.Z) : 0) - minimum);
}
} // namespace
json describe_commands() {
    return {{"success", true},
            {"protocol", "starlight-editor-local"},
            {"schemaVersion", 1},
            {"providerIndependent", true},
            {"implementation", "native-cpp"},
            {"features", json::parse(feature_catalog)},
            {"featureCommand", "--feature <id> --result <report.json>"},
            {"operations",
             {"add", "move", "route", "rename", "remove", "configure", "shape", "transform", "terrain-brush",
              "terrain-water", "terrain-stamp"}},
            {"readOperations", {"describe", "inspect"}},
            {"limitations",
             {"Native Win32 editor; fixed panels, no docking layout system",
              "Static GLB geometry preview; skeletal animation not imported"}}};
}
json inspect_scene(const fs::path &input) {
    auto p = fs::absolute(input).lexically_normal();
    auto bytes = read_file(p);
    auto scene = SceneData::decode(bytes);
    return {{"success", true}, {"scenePath", utf8(p.wstring())}, {"sha256", hash_file(p)}, {"scene", scene}};
}
json apply_commands(const fs::path &file) {
    auto request = json::parse(read_file(file));
    keys(request, {"schemaVersion", "scenePath", "expectedSha256", "operations"});
    if (native_int(request.at("schemaVersion")) != 1)
        throw std::runtime_error("Unknown command schema");
    auto path = fs::absolute(wide(request.at("scenePath").get<std::string>())).lexically_normal();
    if (_wcsicmp(path.extension().c_str(), L".starscene"))
        throw std::runtime_error("Commands require a .starscene file");
    auto expected = request.at("expectedSha256").get<std::string>();
    if (_stricmp(hash_file(path).c_str(), expected.c_str()))
        throw std::runtime_error("Scene changed; inspect again before editing");
    auto scene = SceneData::decode(read_file(path));
    const auto &operations = request.at("operations");
    if (!operations.is_array())
        throw std::runtime_error("Expected operations array");
    for (const auto &op : operations) {
        auto kind = op.at("op").get<std::string>();
        if (kind == "terrain-brush") {
            keys(op, {"op", "tool", "x", "z", "radius", "strength", "seconds", "target", "paint"});
            auto tool = op.at("tool").get<std::string>();
            double x = op.value("x", 0.), z = op.value("z", 0.), radius = op.value("radius", 2.),
                   strength = op.value("strength", 1.), seconds = op.value("seconds", .05),
                   target = op.value("target", 0.);
            int paint = op.contains("paint") ? native_int(op.at("paint")) : 0;
            static const std::set<std::string> tools{"raise", "lower", "smooth", "flatten",
                                                     "paint", "hill",  "crater", "plateau"};
            if (!tools.contains(tool) || !std::isfinite(x + z + radius + strength + seconds + target) ||
                std::abs(x) > 12 || std::abs(z) > 12 || radius < .4 || radius > 5 || strength < .2 ||
                strength > 4 || seconds <= 0 || seconds > 10 || paint < 0 || paint >= 5 ||
                std::abs(target) > 20)
                throw std::runtime_error("Invalid terrain brush settings");
            if (!scene.Terrain)
                scene.Terrain.emplace();
            scene.Terrain->brush(x, z, radius, strength, tool, paint, target, seconds);
            continue;
        }
        if (kind == "terrain-water") {
            keys(op, {"op", "enabled", "level", "lava"});
            if (!scene.Terrain)
                scene.Terrain.emplace();
            scene.Terrain->WaterEnabled = op.at("enabled").get<bool>();
            scene.Terrain->WaterLevel = op.at("level").get<double>();
            scene.Terrain->Lava = op.value("lava", false);
            scene.Terrain->validate();
            continue;
        }
        if (kind == "terrain-stamp") {
            keys(op, {"op", "id"});
            auto &c = find(scene, op);
            if (!scene.Terrain)
                scene.Terrain.emplace();
            stamp_shape(*scene.Terrain, c);
            continue;
        }
        if (kind == "shape") {
            keys(op, {"op", "shape", "name", "x", "y", "z"});
            Character c;
            c.Shape = op.at("shape").get<std::string>();
            c.Name = op.value("name", std::string("Shape"));
            c.X = op.value("x", 0.);
            c.Y = op.value("y", 0.);
            c.Z = op.value("z", 0.);
            c.validate();
            c.Y = std::max(0., c.Y);
            scene.Characters.push_back(std::move(c));
            continue;
        }
        if (kind == "add") {
            keys(op, {"op", "name", "asset", "x", "z"});
            auto asset = fs::absolute(wide(op.at("asset").get<std::string>())).lexically_normal();
            load_glb(asset);
            Character c;
            c.Asset = utf8(asset.wstring());
            c.Name = op.at("name").get<std::string>();
            c.X = op.at("x").get<double>();
            c.Z = op.at("z").get<double>();
            scene.Characters.push_back(std::move(c));
            continue;
        }
        auto &c = find(scene, op);
        if (kind == "transform") {
            keys(op, {"op", "id", "y", "width", "height", "depth", "rotationX", "rotationY", "rotationZ",
                      "keepAboveFloor", "color", "lightIntensity", "lightRange"});
            c.Y = op.value("y", c.Y);
            c.Width = op.value("width", c.Width);
            c.Height = op.value("height", c.Height);
            c.Depth = op.value("depth", c.Depth);
            c.RotationX = op.value("rotationX", c.RotationX);
            c.RotationY = op.value("rotationY", c.RotationY);
            c.RotationZ = op.value("rotationZ", c.RotationZ);
            c.KeepAboveFloor = op.value("keepAboveFloor", c.KeepAboveFloor);
            c.Color = op.value("color", c.Color);
            c.LightIntensity = op.value("lightIntensity", c.LightIntensity);
            c.LightRange = op.value("lightRange", c.LightRange);
            c.validate();
            snap(scene, c);
        } else if (kind == "configure") {
            keys(op, {"op", "id", "x", "z", "speed", "size"});
            configure(c, op.at("x").get<double>(), op.at("z").get<double>(), op.at("speed").get<double>(),
                      op.at("size").get<double>());
        } else if (kind == "move") {
            keys(op, {"op", "id", "x", "z"});
            double x = op.at("x").get<double>(), z = op.at("z").get<double>();
            for (auto &p : c.Route) {
                p.X += x - c.X;
                p.Z += z - c.Z;
            }
            c.X = x;
            c.Z = z;
        } else if (kind == "route") {
            keys(op, {"op", "id", "points"});
            const auto &points = op.at("points");
            if (!points.is_array())
                throw std::runtime_error("Expected route array");
            c.Route.clear();
            for (const auto &p : points)
                c.Route.push_back({p.at("x").get<double>(), p.at("z").get<double>()});
        } else if (kind == "rename") {
            keys(op, {"op", "id", "name"});
            c.Name = op.at("name").get<std::string>();
        } else if (kind == "remove") {
            keys(op, {"op", "id"});
            auto index = &c - scene.Characters.data();
            scene.Characters.erase(scene.Characters.begin() + index);
        } else
            throw std::runtime_error("Unsupported editor operation: " + kind);
    }
    scene.validate();
    save_scene(scene, path, expected);
    json characters = json::array();
    for (const auto &c : scene.Characters)
        characters.push_back({{"Id", c.Id}, {"Name", c.Name}});
    return {{"success", true},
            {"scenePath", utf8(path.wstring())},
            {"sha256", hash_file(path)},
            {"characters", characters},
            {"note", "Scene saved with backup. No engine code or preset changed."}};
}
} // namespace starlight::editor

namespace starlight::editor {
json invoke_feature(std::string_view id) {
    for (const auto &slot : json::parse(feature_catalog))
        if (slot.at("Id").get<std::string>() == id) {
            bool experimental = slot.at("Status") == "experimental";
            return {
                {"Success", false},
                {"Code", experimental ? "editor_only" : "not_implemented"},
                {"FeatureId", id},
                {"Message", slot.at("Name").get<std::string>() +
                                (experimental ? ": experimental controls are available in the editor. This "
                                                "discovery command does not run them. No changes made."
                                              : ": coming soon — not implemented. No changes made.")}};
        }
    return {
        {"Success", false}, {"Code", "unknown_feature"}, {"FeatureId", id}, {"Message", "Unknown feature."}};
}
} // namespace starlight::editor
