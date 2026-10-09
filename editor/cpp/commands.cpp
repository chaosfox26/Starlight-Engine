#include "commands.hpp"
#include "assets.hpp"
#include "engine_assets.hpp"
#include "features.hpp"
#include "object_template.hpp"
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
std::string model_for_command(const std::string &named, const fs::path &baseFolder) {
    if (EngineAssets::valid_id(AssetKind::Model, named)) {
        (void)EngineAssets::get(AssetKind::Model, named);
        return named;
    }
    auto source = fs::path(wide(named));
    if (source.is_relative()) source = baseFolder / source;
    return import_model(fs::absolute(source).lexically_normal());
}
void snap(SceneData &scene, Character &c) {
    if (!c.KeepAboveFloor)
        return;
    std::shared_ptr<const std::vector<MeshPart>> model;
    std::vector<MeshPart> built;
    if (c.Shape || c.Sculpt) built = shape_geometry(c); else model = load_model(c.Asset);
    const auto &parts = model ? *model : built;
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
             {"add", "move", "route", "rename", "remove", "configure", "shape", "transform", "follow", "set-block", "terrain-brush",
              "terrain-water", "terrain-stamp", "terrain-volume", "insert-object", "insert-group", "duplicate", "replace-model", "merge-scene"}},
            {"requestKeys",
             {{"allowMissingAssets", "Optional boolean, default false. true saves the scene even when it names models that are not in the asset library (the editor's 'Save anyway'); by default such a save is refused and nothing is written."},
              {"correctedAdd", "Optional boolean, default false. true makes 'add' resolve a relative model path against the request file's folder (as replace-model does) and rest the new object on the terrain; by default 'add' resolves against the working directory and leaves Y at 0."}}},
            {"sceneBlocks", {"Lighting", "Moods", "ActiveMood", "View", "Zones", "Crowd", "Cast"}},
            {"readOperations", {"describe", "inspect"}},
            {"portableExport", "--export-portable <scene.starscene> <new-folder>"},
            {"portableVerification", "--verify-portable <folder>"},
            {"modelImport", "--import-model <file.glb>"},
            {"textureImport", "--import-texture <file.png|file.jpg>"},
            {"blobImport", "--import-blob <type> <file>"},
            {"assetReadiness", "--asset-ready <asset-id>"},
            {"atlasValidation", "--validate-atlas <blob-id> <model-id>"},
            {"assetRoots", "--asset-root-list | --asset-root-add <folder> [--writable] [--label <text>] | --asset-root-init <folder> [--writable] [--label <text>] | --asset-root-remove <folder>"},
            {"starterAdventure", "--new-adventure <new-scene.starscene>"},
            {"playableExport", "--export-game <scene.starscene> <new-folder> <runtime-folder>"},
            {"limitations",
             {"Native Win32 editor; fixed panels, no docking layout system",
              "Static GLB geometry preview; skeletal animation not imported"}}};
}
json inspect_scene(const fs::path &input) {
    auto p = fs::absolute(input).lexically_normal();
    auto scene = load_scene(p);
    json assets = json::array();
    size_t missing = 0;
    for (const auto &object : scene.Characters) {
        if (object.Shape || object.Sculpt || object.Asset.empty()) continue;
        auto found = std::find_if(assets.begin(), assets.end(), [&](const json &entry) { return entry.at("asset") == object.Asset; });
        if (found != assets.end()) { (*found)["objectIds"].push_back(object.Id); continue; }
        json entry{{"asset",object.Asset},{"objectIds",json::array({object.Id})},{"available",false},
            {"geometryValidated",false}};
        try {
            EngineAssets::verify_on_disk(object.Asset);
            entry["available"] = true;
            std::error_code error;
            auto size = fs::file_size(EngineAssets::locate(object.Asset), error);
            if (!error) entry["bytes"] = size;
        } catch (const AssetMissing &problem) {
            ++missing;
            entry["problem"] = problem.what();
        }
        assets.push_back(std::move(entry));
    }
    json result{{"success", true}, {"scenePath", utf8(p.wstring())}, {"sha256", hash_file(p)}, {"scene", scene},
        {"assets",assets},{"missingAssetCount",missing},{"assetCheckScope","Library presence and file integrity only; geometry and runtime correctness are not certified"}};
    if (!scene.Cast.Models.empty()) {
        json castAssets = json::array();
        size_t castMissing = 0;
        for (const auto &id : cast_asset_ids(scene)) {
            const bool available = !EngineAssets::locate(id).empty();
            if (!available) ++castMissing;
            castAssets.push_back({{"asset", id}, {"available", available}});
        }
        result["castAssets"] = castAssets;
        result["missingCastAssetCount"] = castMissing;
    }
    return result;
}
json apply_commands(const fs::path &file) {
    auto request = json::parse(read_file(file));
    keys(request, {"schemaVersion", "scenePath", "expectedSha256", "operations", "allowMissingAssets", "correctedAdd"});
    if (native_int(request.at("schemaVersion")) != 1)
        throw std::runtime_error("Unknown command schema");
    auto path = fs::absolute(wide(request.at("scenePath").get<std::string>())).lexically_normal();
    if (_wcsicmp(path.extension().c_str(), L".starscene"))
        throw std::runtime_error("Commands require a .starscene file");
    auto expected = request.at("expectedSha256").get<std::string>();
    if (_stricmp(hash_file(path).c_str(), expected.c_str()))
        throw std::runtime_error("Scene changed; inspect again before editing");
    auto scene = load_scene(path);
    const auto &operations = request.at("operations");
    if (!operations.is_array())
        throw std::runtime_error("Expected operations array");
    bool allowMissingAssets = request.value("allowMissingAssets", false);
    bool castCheckWaived = false;
    const bool correctedAdd = request.value("correctedAdd", false);
    std::vector<std::string> staleRules;
    for (const auto &op : operations) {
        auto kind = op.at("op").get<std::string>();
        if (kind == "merge-scene") {
            keys(op, {"op", "file", "expectedSha256", "x", "z"});
            auto source = fs::path(wide(op.at("file").get<std::string>()));
            if (source.is_relative()) source = fs::absolute(file).parent_path() / source;
            source = fs::absolute(source).lexically_normal();
            auto bytes = read_file(source);
            if (_stricmp(hash_bytes(bytes).c_str(), op.at("expectedSha256").get<std::string>().c_str()))
                throw std::runtime_error("Source scene changed; inspect it again before merging");
            EngineAssets::discover_near(source.parent_path());
            auto objects = SceneData::decode(bytes).Characters;
            if (objects.empty()) throw std::runtime_error("Source scene contains no objects");
            std::unordered_map<std::string,std::string> mergedIds;
            for (auto &object : objects) {
                auto oldId=object.Id;object.Id = new_id();mergedIds[oldId]=object.Id;
                if (!object.Shape && !object.Sculpt && !object.Asset.empty()) (void)EngineAssets::get(AssetKind::Model, object.Asset);
            }
            for(auto& object:objects)if(auto target=mergedIds.find(object.JointTargetId);target!=mergedIds.end())object.JointTargetId=target->second;
            for(auto& object:objects)if(auto target=mergedIds.find(object.LightningTargetId);target!=mergedIds.end())object.LightningTargetId=target->second;
            place_object_group(objects, scene, op.value("x", objects.front().X), op.value("z", objects.front().Z));
            for (auto &object : objects) scene.Characters.push_back(std::move(object));
            continue;
        }
        if (kind == "insert-object" || kind == "insert-group") {
            if (kind == "insert-group") keys(op, {"op", "file", "expectedSha256", "x", "z"});
            else keys(op, {"op", "file", "expectedSha256", "name", "x", "z"});
            auto source = fs::path(wide(op.at("file").get<std::string>()));
            if (source.is_relative()) source = fs::absolute(file).parent_path() / source;
            if (fs::file_size(source) > 32 * 1024 * 1024) throw std::runtime_error("Reusable object exceeds 32 MiB");
            auto bytes = read_file(source);
            // Hash the same bytes that are decoded, avoiding a check/read race.
            if (_stricmp(hash_bytes(bytes).c_str(), op.at("expectedSha256").get<std::string>().c_str()))
                throw std::runtime_error("Saved object changed; inspect it again before inserting");
            EngineAssets::discover_near(fs::absolute(source).parent_path());
            if (kind == "insert-group") {
                auto objects = decode_object_group(bytes);
                place_object_group(objects, scene, op.value("x", objects.front().X), op.value("z", objects.front().Z));
                for (auto &object : objects) scene.Characters.push_back(std::move(object));
                continue;
            }
            auto object = decode_object_template(bytes);
            object.Name = op.value("name", object.Name);
            place_object_template(object, op.value("x", object.X), op.value("z", object.Z));
            snap(scene, object);
            scene.Characters.push_back(std::move(object));
            continue;
        }
        if (kind == "duplicate") {
            keys(op, {"op", "id", "name", "x", "z"});
            auto object = find(scene, op);
            object.Id = new_id();
            object.Name = op.value("name", object.Name + " copy");
            place_object_template(object, op.value("x", object.X), op.value("z", object.Z));
            snap(scene, object);
            scene.Characters.push_back(std::move(object));
            continue;
        }
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
        if(kind=="terrain-volume"){
            keys(op,{"op","x","y","z","radius","subtract"});
            auto terrain=scene.Terrain.value_or(TerrainData{});
            terrain.Volumes.push_back({op.at("x").get<double>(),op.at("y").get<double>(),op.at("z").get<double>(),op.value("radius",1.),op.value("subtract",true)});
            terrain.validate();scene.Terrain=std::move(terrain);continue;
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
        if (kind == "set-block") {
            keys(op, {"op", "block", "value", "verifyAssets"});
            const auto block = op.at("block").get<std::string>();
            const auto &value = op.at("value");
            const bool clear = value.is_null();
            if (block == "Lighting") {
                if (clear) scene.Lighting.reset(); else scene.Lighting = value.get<SceneLighting>();
            } else if (block == "Moods") {
                scene.Moods.clear();
                if (!clear) {
                    if (!value.is_array()) throw std::runtime_error("Moods: must be a list");
                    value.get_to(scene.Moods);
                }
            } else if (block == "ActiveMood") {
                if (!clear && !value.is_string()) throw std::runtime_error("ActiveMood: must be text or null");
                scene.ActiveMood = clear ? std::string{} : value.get<std::string>();
            } else if (block == "View") {
                if (clear) scene.View.reset(); else scene.View = value.get<SceneView>();
            } else if (block == "Zones") {
                scene.Zones.clear();
                if (!clear) {
                    if (!value.is_array()) throw std::runtime_error("Zones: must be a list");
                    value.get_to(scene.Zones);
                }
            } else if (block == "Crowd") {
                if (clear) scene.Crowd.reset(); else scene.Crowd = value.get<SceneCrowd>();
            } else if (block == "Cast") {
                scene.Cast = clear ? SceneCast{} : value.get<SceneCast>();
                scene.Cast.validate();
                if (!op.value("verifyAssets", true)) castCheckWaived = true;
                if (op.value("verifyAssets", true)) {
                    EngineAssets::discover_near(path.parent_path());
                    const auto missing = cast_missing_assets(scene);
                    if (!missing.empty()) {
                        std::string list;
                        for (const auto &id : missing) list += (list.empty() ? "" : ", ") + id;
                        throw std::runtime_error("The cast names assets the library does not hold: " + list);
                    }
                }
            } else
                throw std::runtime_error("Unknown scene block: " + block + " (Lighting, Moods, ActiveMood, View, Zones, Crowd, Cast)");
            continue;
        }
        if (kind == "shape") {
            keys(op, {"op", "shape", "name", "x", "y", "z", "width", "height", "depth", "color", "glow", "group", "ringInner", "lightInner"});
            Character c;
            c.Shape = op.at("shape").get<std::string>();
            c.Name = op.value("name", std::string("Shape"));
            c.X = op.value("x", 0.);
            c.Y = op.value("y", 0.);
            c.Z = op.value("z", 0.);
            c.Width = op.value("width", c.Width);
            c.Height = op.value("height", c.Height);
            c.Depth = op.value("depth", c.Depth);
            c.Color = op.value("color", c.Color);
            c.Glow = op.value("glow", c.Glow);
            c.Group = op.value("group", c.Group);
            c.RingInner = op.value("ringInner", c.RingInner);
            c.LightInner = op.value("lightInner", c.LightInner);
            c.validate();
            c.Y = std::max(0., c.Y);
            scene.Characters.push_back(std::move(c));
            continue;
        }
        if (kind == "add") {
            keys(op, {"op", "name", "asset", "x", "z"});
            Character c;
            c.Asset = model_for_command(op.at("asset").get<std::string>(), correctedAdd ? fs::absolute(file).parent_path() : fs::current_path());
            c.Name = op.at("name").get<std::string>();
            c.X = op.at("x").get<double>();
            c.Z = op.at("z").get<double>();
            if (correctedAdd) snap(scene, c);
            scene.Characters.push_back(std::move(c));
            continue;
        }
        static const std::set<std::string> identified{"replace-model", "transform", "follow", "configure", "move", "route", "rename", "remove"};
        if (!identified.contains(kind))
            throw std::runtime_error("Unsupported editor operation: " + kind);
        auto &c = find(scene, op);
        if (kind == "replace-model") {
            keys(op, {"op", "id", "asset"});
            if (c.Shape || c.Sculpt) throw std::runtime_error("Model replacement requires an imported object, not a shape or sculpt");
            c.Asset = model_for_command(op.at("asset").get<std::string>(), fs::absolute(file).parent_path());
        } else if (kind == "transform") {
            keys(op, {"op", "id", "y", "width", "height", "depth", "rotationX", "rotationY", "rotationZ",
                      "keepAboveFloor", "color", "lightIntensity", "lightRange", "glow", "group", "ringInner", "lightInner"});
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
            c.Glow = op.value("glow", c.Glow);
            c.Group = op.value("group", c.Group);
            c.RingInner = op.value("ringInner", c.RingInner);
            c.LightInner = op.value("lightInner", c.LightInner);
            c.validate();
            snap(scene, c);
        } else if (kind == "follow") {
            keys(op, {"op", "id", "clear", "target", "offsetX", "offsetZ", "lag", "wobble", "poolRadius", "housing", "beam"});
            if (op.value("clear", false)) {
                c.Follow.reset();
            } else {
                json current = c.Follow ? json(*c.Follow) : json(LightFollow{});
                json patch = json::object();
                for (const auto &[from, to] : std::initializer_list<std::pair<const char *, const char *>>{
                         {"target", "Target"}, {"offsetX", "OffsetX"}, {"offsetZ", "OffsetZ"}, {"lag", "Lag"}, {"wobble", "Wobble"},
                         {"poolRadius", "PoolRadius"}, {"housing", "Housing"}, {"beam", "Beam"}})
                    if (op.contains(from)) {
                        // Only housing and beam treat null as removal; for a scalar, merge-patch would drop the key and silently restore its default.
                        if (op.at(from).is_null() && std::string_view(to) != "Housing" && std::string_view(to) != "Beam")
                            throw std::runtime_error(std::string("Follow setting ") + from + " must not be null");
                        patch[to] = op.at(from);
                    }
                current.merge_patch(patch);
                c.Follow = current.get<LightFollow>();
            }
            c.validate();
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
            auto removed=std::vector<Character>{c};auto removedId=c.Id;scene.Characters.erase(scene.Characters.begin() + index);
            for (auto &name : rules_naming_objects(scene, removed)) if (std::find(staleRules.begin(), staleRules.end(), name) == staleRules.end()) staleRules.push_back(std::move(name));
            for(auto& object:scene.Characters)if(object.JointTargetId==removedId){object.JointKind=0;object.JointTargetId.clear();}for(auto& object:scene.Characters)if(object.LightningTargetId==removedId)object.LightningTargetId.clear();
        }
    }
    scene.validate();
    try {
        save_scene(scene, path, expected, allowMissingAssets);
    } catch (const MissingAssetsError &problem) {
        // A waived cast check may save over missing cast assets only; a model that an object names still refuses the save.
        const bool objectMissing = std::any_of(scene.Characters.begin(), scene.Characters.end(), [&](const Character &c) {
            return !c.Shape && !c.Sculpt && std::find(problem.ids.begin(), problem.ids.end(), c.Asset) != problem.ids.end();
        });
        if (!castCheckWaived || objectMissing) throw;
        save_scene(scene, path, expected, true);
    }
    json characters = json::array();
    for (const auto &c : scene.Characters)
        characters.push_back({{"Id", c.Id}, {"Name", c.Name}});
    json report{{"success", true},
                {"scenePath", utf8(path.wstring())},
                {"sha256", hash_file(path)},
                {"characters", characters},
                {"note", "Scene saved with backup. No engine code or preset changed."}};
    if (!staleRules.empty()) report["rulesNamingRemovedObjects"] = staleRules;
    return report;
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
