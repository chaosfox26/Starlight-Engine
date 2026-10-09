#include "object_template.hpp"
#include "interactions.hpp"
#include "engine_assets.hpp"
#include <unordered_map>
namespace starlight::editor {
namespace {
constexpr size_t maximumBytes = 32 * 1024 * 1024;
void self_contained(const Character& object) {
    object.validate();
    if (!object.Shape && !object.Sculpt && object.Asset.empty())
        throw std::runtime_error("Reusable objects need a built-in shape, a sculpt or an imported model");
}
}
std::string encode_object_template(const Character& object) {
    self_contained(object);
    if(!object.JointTargetId.empty())throw std::runtime_error("Save connected bodies together as a reusable group");
    if(!object.LightningTargetId.empty())throw std::runtime_error("Save the lightning emitter and its target together as a reusable group");
    auto saved = object;
    if (saved.Shape || saved.Sculpt) saved.Asset.clear();
    saved.Id.clear();
    auto result = json{{"Format","starlight-object"},{"Version",2},{"Object",saved}}.dump(2);
    if (result.size() > maximumBytes) throw std::runtime_error("Reusable object exceeds 32 MiB");
    return result;
}
Character decode_object_template(std::string_view bytes) {
    if (bytes.size() > maximumBytes) throw std::runtime_error("Reusable object exceeds 32 MiB");
    auto data = json::parse(bytes);
    if (data.at("Format") != "starlight-object") throw std::runtime_error("Unsupported reusable object format");
    if (native_int(data.at("Version")) < 2) throw ConversionRequired("This reusable object uses the pre-key-card format. Convert it with Starlight.SceneMigration before using it.");
    if (native_int(data.at("Version")) != 2) throw std::runtime_error("Unsupported reusable object format");
    auto object = data.at("Object").get<Character>();
    self_contained(object);
    if (!object.Shape && !object.Sculpt) (void)EngineAssets::get(AssetKind::Model, object.Asset);
    object.Id = new_id();
    return object;
}
void save_object_template(const Character& object, const fs::path& path) {
    if (_wcsicmp(path.extension().c_str(), L".starobject")) throw std::runtime_error("Save reusable objects as .starobject files");
    auto bytes = encode_object_template(object);
    EngineAssets::bundle_into(EngineAssets::referenced_ids(json::parse(bytes)), fs::absolute(path).parent_path() / L"assets");
    atomic_write(path, bytes, true);
}
Character load_object_template(const fs::path& path) {
    if (fs::file_size(path) > maximumBytes) throw std::runtime_error("Reusable object exceeds 32 MiB");
    EngineAssets::discover_near(fs::absolute(path).parent_path());
    return decode_object_template(read_file(path));
}
void place_object_template(Character& object, double x, double z) {
    auto placed = object;
    double dx = x - object.X, dz = z - object.Z;
    placed.X = x; placed.Z = z;
    for (auto& point : placed.Route) { point.X += dx; point.Z += dz; }
    if (placed.Shape == "chaos-lightning" && placed.LightningTargetId.empty()) { placed.TargetX += dx; placed.TargetZ += dz; }
    placed.validate();
    object = std::move(placed);
}
std::string encode_object_group(const std::vector<Character>& objects) {
    if (objects.empty() || objects.size() > 4096) throw std::runtime_error("Select between 1 and 4096 native objects");
    json saved = json::array();
    std::unordered_map<std::string,std::string> ids;
    for(size_t i=0;i<objects.size();++i)ids[objects[i].Id]="member-"+std::to_string(i);
    for(const auto& object:objects){auto copy=object;copy.LightningTargetId.clear();copy.JointTargetId.clear();auto item=json::parse(encode_object_template(copy)).at("Object");item["Id"]=ids.at(object.Id);
        if(!object.LightningTargetId.empty()){auto target=ids.find(object.LightningTargetId);if(target==ids.end())throw std::runtime_error("Include the lightning target in its reusable group");item["LightningTargetId"]=target->second;}if(!object.JointTargetId.empty()){auto target=ids.find(object.JointTargetId);if(target==ids.end())throw std::runtime_error("Include the joint target in its reusable group");item["JointTargetId"]=target->second;}saved.push_back(std::move(item));}

    auto bytes = json{{"Format","starlight-object-group"},{"Version",2},{"Objects",saved}}.dump(2);
    if (bytes.size() > maximumBytes) throw std::runtime_error("Reusable group exceeds 32 MiB");
    return bytes;
}
std::vector<Character> decode_object_group(std::string_view bytes) {
    if (bytes.size() > maximumBytes) throw std::runtime_error("Reusable group exceeds 32 MiB");
    auto data = json::parse(bytes);
    if (data.at("Format") != "starlight-object-group") throw std::runtime_error("Unsupported reusable group format");
    if (native_int(data.at("Version")) < 2) throw ConversionRequired("This reusable group uses the pre-key-card format. Convert it with Starlight.SceneMigration before using it.");
    if (native_int(data.at("Version")) != 2) throw std::runtime_error("Unsupported reusable group format");
    const auto &objects = data.at("Objects");
    if (!objects.is_array() || objects.empty() || objects.size() > 4096)
        throw std::runtime_error("Reusable groups require between 1 and 4096 objects");
    std::vector<Character> result;
    std::unordered_map<std::string,std::string> ids;
    for(const auto& object:objects){auto decoded=decode_object_template(json{{"Format","starlight-object"},{"Version",2},{"Object",object}}.dump());auto old=object.value("Id",std::string{});if(!old.empty()&&!ids.emplace(old,decoded.Id).second)throw std::runtime_error("Reusable group repeats an object identity");result.push_back(std::move(decoded));}
    for(auto& object:result)if(!object.LightningTargetId.empty()){auto target=ids.find(object.LightningTargetId);if(target==ids.end())throw std::runtime_error("Reusable group lightning target is missing");object.LightningTargetId=target->second;}

    for(auto& object:result)if(!object.JointTargetId.empty()){auto target=ids.find(object.JointTargetId);if(target==ids.end())throw std::runtime_error("Reusable group joint target is missing");object.JointTargetId=target->second;}
    return result;
}
void save_object_group(const std::vector<Character>& objects, const fs::path& path) {
    if (_wcsicmp(path.extension().c_str(), L".stargroup")) throw std::runtime_error("Save reusable groups as .stargroup files");
    auto bytes = encode_object_group(objects);
    EngineAssets::bundle_into(EngineAssets::referenced_ids(json::parse(bytes)), fs::absolute(path).parent_path() / L"assets");
    atomic_write(path, bytes, true);
}
std::vector<Character> load_object_group(const fs::path& path) {
    if (fs::file_size(path) > maximumBytes) throw std::runtime_error("Reusable group exceeds 32 MiB");
    EngineAssets::discover_near(fs::absolute(path).parent_path());
    return decode_object_group(read_file(path));
}
void renew_object_ids(std::vector<Character>& objects) {
    std::unordered_map<std::string,std::string> renamed;
    for (auto& object : objects) {
        auto old = object.Id; object.Id = new_id();
        if (!renamed.emplace(old, object.Id).second) throw std::runtime_error("Two copied objects share one identity");
    }
    for (auto& object : objects) {
        if (auto target = renamed.find(object.JointTargetId); !object.JointTargetId.empty() && target != renamed.end()) object.JointTargetId = target->second;
        if (auto target = renamed.find(object.LightningTargetId); !object.LightningTargetId.empty() && target != renamed.end()) object.LightningTargetId = target->second;
    }
}
void drop_unresolved_links(std::vector<Character>& objects, const std::unordered_set<std::string>& present) {
    for (auto& object : objects) {
        if (!object.JointTargetId.empty() && !present.contains(object.JointTargetId)) { object.JointKind = 0; object.JointTargetId.clear(); }
        if (!object.LightningTargetId.empty() && !present.contains(object.LightningTargetId)) object.LightningTargetId.clear();
    }
}
std::vector<std::string> rules_naming_objects(const SceneData& scene, const std::vector<Character>& removed) {
    std::unordered_set<std::string> ids, keys;
    for (const auto& object : removed) {
        ids.insert(object.Id);
        if (object.GameplayRole == "key" && !object.GameplayKey.empty()) keys.insert(object.GameplayKey);
    }
    for (const auto& object : scene.Characters) if (object.GameplayRole == "key") keys.erase(object.GameplayKey);
    std::vector<std::string> names;
    for (const auto& rule : scene.Rules) {
        bool names_one = !rule.Subject.empty() && (ids.contains(rule.Subject) || (rule.Event == "item-collected" && keys.contains(rule.Subject)));
        for (const auto& action : rule.Actions) names_one = names_one || (action.Type == "open-door" && ids.contains(action.Target));
        if (names_one) names.push_back(rule.Name);
    }
    return names;
}
void place_object_group(std::vector<Character>& objects, SceneData& scene, double x, double z,
                        bool grid, bool objectSnap, bool ground,double gridStep) {
    if (objects.empty()) throw std::runtime_error("Cannot place an empty group");
    auto placed = objects;
    auto anchor = placed.front();
    place_object_template(anchor, x, z);
    snap_object(scene, anchor, grid, objectSnap, false,gridStep);
    const double dx = anchor.X - placed.front().X, dz = anchor.Z - placed.front().Z;
    const double dy = anchor.Y - placed.front().Y;
    for (auto &object : placed) {
        place_object_template(object, object.X + dx, object.Z + dz);
        object.Y += dy;
        if (object.Shape == "chaos-lightning" && object.LightningTargetId.empty()) object.TargetY += dy;
    }
    // Lift the arrangement together instead of independently snapping each member.
    double lift = 0;
    for (const auto &object : placed) {
        auto snapped = object;
        snap_object(scene, snapped, false, false, ground);
        lift = std::max(lift, snapped.Y - object.Y);
    }
    for (auto &object : placed) {
        object.Y += lift;
        if (object.Shape == "chaos-lightning" && object.LightningTargetId.empty()) object.TargetY += lift;
        object.validate();
    }
    objects = std::move(placed);
}
}
