#include "object_template.hpp"
#include "interactions.hpp"
namespace starlight::editor {
namespace {
constexpr size_t maximumBytes = 32 * 1024 * 1024;
void self_contained(const Character& object) {
    object.validate();
    if (!object.Shape && !object.Sculpt)
        throw std::runtime_error("Reusable objects currently support built-in shapes and sculpts. Imported model packaging is not available yet.");
}
}
std::string encode_object_template(const Character& object) {
    self_contained(object);
    auto saved = object;
    saved.Asset.clear(); // Sculpt geometry can be independent of its original model.
    saved.Id.clear();
    auto result = json{{"Format","starlight-object"},{"Version",1},{"Object",saved}}.dump(2);
    if (result.size() > maximumBytes) throw std::runtime_error("Reusable object exceeds 32 MiB");
    return result;
}
Character decode_object_template(std::string_view bytes) {
    if (bytes.size() > maximumBytes) throw std::runtime_error("Reusable object exceeds 32 MiB");
    auto data = json::parse(bytes);
    if (data.at("Format") != "starlight-object" || native_int(data.at("Version")) != 1)
        throw std::runtime_error("Unsupported reusable object format");
    auto object = data.at("Object").get<Character>();
    self_contained(object);
    if (!object.Asset.empty()) throw std::runtime_error("Reusable objects must contain their own geometry");
    object.Id = new_id();
    return object;
}
void save_object_template(const Character& object, const fs::path& path) {
    if (_wcsicmp(path.extension().c_str(), L".starobject")) throw std::runtime_error("Save reusable objects as .starobject files");
    atomic_write(path, encode_object_template(object), true);
}
Character load_object_template(const fs::path& path) {
    if (fs::file_size(path) > maximumBytes) throw std::runtime_error("Reusable object exceeds 32 MiB");
    return decode_object_template(read_file(path));
}
void place_object_template(Character& object, double x, double z) {
    auto placed = object;
    double dx = x - object.X, dz = z - object.Z;
    placed.X = x; placed.Z = z;
    for (auto& point : placed.Route) { point.X += dx; point.Z += dz; }
    if (placed.Shape == "chaos-lightning") { placed.TargetX += dx; placed.TargetZ += dz; }
    placed.validate();
    object = std::move(placed);
}
std::string encode_object_group(const std::vector<Character>& objects) {
    if (objects.empty() || objects.size() > 4096) throw std::runtime_error("Select between 1 and 4096 native objects");
    json saved = json::array();
    for (const auto &object : objects) saved.push_back(json::parse(encode_object_template(object)).at("Object"));
    auto bytes = json{{"Format","starlight-object-group"},{"Version",1},{"Objects",saved}}.dump(2);
    if (bytes.size() > maximumBytes) throw std::runtime_error("Reusable group exceeds 32 MiB");
    return bytes;
}
std::vector<Character> decode_object_group(std::string_view bytes) {
    if (bytes.size() > maximumBytes) throw std::runtime_error("Reusable group exceeds 32 MiB");
    auto data = json::parse(bytes);
    if (data.at("Format") != "starlight-object-group" || native_int(data.at("Version")) != 1)
        throw std::runtime_error("Unsupported reusable group format");
    const auto &objects = data.at("Objects");
    if (!objects.is_array() || objects.empty() || objects.size() > 4096)
        throw std::runtime_error("Reusable groups require between 1 and 4096 objects");
    std::vector<Character> result;
    for (const auto &object : objects)
        result.push_back(decode_object_template(json{{"Format","starlight-object"},{"Version",1},{"Object",object}}.dump()));
    return result;
}
void save_object_group(const std::vector<Character>& objects, const fs::path& path) {
    if (_wcsicmp(path.extension().c_str(), L".stargroup")) throw std::runtime_error("Save reusable groups as .stargroup files");
    atomic_write(path, encode_object_group(objects), true);
}
std::vector<Character> load_object_group(const fs::path& path) {
    if (fs::file_size(path) > maximumBytes) throw std::runtime_error("Reusable group exceeds 32 MiB");
    return decode_object_group(read_file(path));
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
        if (object.Shape == "chaos-lightning") object.TargetY += dy;
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
        if (object.Shape == "chaos-lightning") object.TargetY += lift;
        object.validate();
    }
    objects = std::move(placed);
}
}
