#include "platform.hpp"
#include "assets.hpp"
#include <cstring>
namespace starlight::editor {
namespace {
void embedded_uris(const json &value) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (it.key() == "uri" && (!it.value().is_string() ||
                !it.value().get<std::string>().starts_with("data:")))
                throw std::runtime_error("Portable export requires embedded GLB resources; external URI found");
            embedded_uris(it.value());
        }
    } else if (value.is_array()) for (const auto &item : value) embedded_uris(item);
}
}
json export_portable_scene(const SceneData &source, const fs::path &destination) {
    source.validate();
    auto target = fs::absolute(destination).lexically_normal();
    if (fs::exists(target)) throw std::runtime_error("Choose a new folder for portable export; existing folders are never overwritten");
    fs::create_directories(target.parent_path());
    auto stage = target.parent_path() / (L".starlight-export-" + wide(new_id()));
    if (!fs::create_directory(stage)) throw std::runtime_error("Cannot create export staging folder");
    try {
        auto scene = source;
        json assets = json::array();
        for (auto &object : scene.Characters) {
            if (object.Shape || object.Sculpt) { object.Asset.clear(); continue; }
            if (object.Asset.empty()) continue;
            fs::path asset(wide(object.Asset));
            if (!asset.is_absolute()) throw std::runtime_error("Open the scene before exporting relative asset paths");
            if (fs::file_size(asset) > 256ull * 1024 * 1024) throw std::runtime_error("Export GLB exceeds 256 MiB");
            auto bytes = read_file(asset);
            auto hash = hash_bytes(bytes);
            auto relative = fs::path(L"assets") / (wide(hash) + L".glb");
            auto copied = stage / relative;
            if (!fs::exists(copied)) {
                atomic_write(copied, bytes);
                load_glb(copied); // Validate the exact exported bytes, not the mutable source file.
                for (size_t offset = 12; offset + 8 <= bytes.size();) {
                    uint32_t count, kind;
                    std::memcpy(&count, bytes.data() + offset, 4);
                    std::memcpy(&kind, bytes.data() + offset + 4, 4);
                    offset += 8;
                    if (count > bytes.size() - offset) throw std::runtime_error("Truncated GLB chunk");
                    if (kind == 0x4e4f534a) embedded_uris(json::parse(bytes.substr(offset, count)));
                    offset += count;
                }
                assets.push_back({{"path", utf8(relative.generic_wstring())}, {"sha256", hash}, {"bytes", bytes.size()}});
            }
            object.Asset = utf8(relative.generic_wstring());
        }
        atomic_write(stage / L"scene.starscene", scene.encode());
        json manifest{{"format","starlight-portable-scene"},{"version",1},{"scene","scene.starscene"},
            {"sceneSha256",hash_file(stage / L"scene.starscene")},{"assets",assets},
            {"scope","Editor scene and embedded GLB assets; not an executable game"}};
        atomic_write(stage / L"manifest.json", manifest.dump(2));
        fs::rename(stage, target);
        return {{"success",true},{"folder",utf8(target.wstring())},{"assetCount",assets.size()}};
    } catch (...) {
        std::error_code ignored;
        fs::remove_all(stage, ignored); // Only this invocation's uniquely created staging directory.
        throw;
    }
}
json verify_portable_scene(const fs::path &folder) {
    auto root = fs::canonical(folder);
    auto manifest = json::parse(read_file(root / L"manifest.json"));
    if (manifest.at("format") != "starlight-portable-scene" || native_int(manifest.at("version")) != 1 ||
        manifest.at("scene") != "scene.starscene") throw std::runtime_error("Unsupported portable scene manifest");
    auto contained = [&](const std::string &name) {
        auto relative = fs::path(wide(name));
        if (relative.is_absolute() || relative.has_root_name() || relative.empty())
            throw std::runtime_error("Invalid portable scene path");
        auto path = fs::canonical(root / relative);
        auto within = path.lexically_relative(root);
        if (within.empty() || *within.begin() == L"..") throw std::runtime_error("Asset escapes portable scene folder");
        return path;
    };
    auto scenePath = contained("scene.starscene");
    if (hash_file(scenePath) != manifest.at("sceneSha256").get<std::string>())
        throw std::runtime_error("Portable scene changed since export");
    const auto &assets = manifest.at("assets");
    if (!assets.is_array()) throw std::runtime_error("Expected portable asset list");
    std::vector<fs::path> verified;
    for (const auto &asset : assets) {
        auto path = contained(asset.at("path").get<std::string>());
        if (hash_file(path) != asset.at("sha256").get<std::string>() || fs::file_size(path) != asset.at("bytes").get<uint64_t>())
            throw std::runtime_error("Portable model changed since export");
        verified.push_back(path);
    }
    auto scene = SceneData::decode(read_file(scenePath));
    for (const auto &object : scene.Characters) {
        if (object.Asset.empty()) continue;
        auto path = contained(object.Asset);
        if (std::find(verified.begin(), verified.end(), path) == verified.end())
            throw std::runtime_error("Scene references a model missing from its manifest");
    }
    return {{"success",true},{"assetCount",assets.size()},{"note","Exported scene and model hashes match; this is integrity verification, not runtime certification."}};
}
json export_scene_selection(const SceneData &scene, const std::vector<std::string>& ids, const fs::path &destination) {
    if (ids.empty()) throw std::runtime_error("Select objects to export first");
    auto unique = ids;
    std::sort(unique.begin(), unique.end());
    if (std::adjacent_find(unique.begin(), unique.end()) != unique.end())
        throw std::runtime_error("Selection contains duplicate object identities");
    SceneData selected;
    selected.Name = scene.Name + " - selected objects";
    for (const auto &object : scene.Characters)
        if (std::binary_search(unique.begin(), unique.end(), object.Id)) selected.Characters.push_back(object);
    if (selected.Characters.size() != ids.size()) throw std::runtime_error("Selected object no longer exists");
    return export_portable_scene(selected, destination);
}
std::optional<size_t> repair_scene_models(SceneData &scene,
    const std::function<std::optional<fs::path>(const fs::path&)>& choose) {
    auto repaired = scene;
    std::vector<std::pair<std::string,std::string>> replacements;
    for (auto &object : repaired.Characters) {
        if (object.Shape || object.Sculpt || object.Asset.empty()) continue;
        fs::path original(wide(object.Asset));
        std::error_code error;
        if (fs::is_regular_file(original, error)) continue;
        auto existing = std::find_if(replacements.begin(), replacements.end(), [&](const auto &entry) { return entry.first == object.Asset; });
        if (existing != replacements.end()) { object.Asset = existing->second; continue; }
        auto replacement = choose(original);
        if (!replacement) return std::nullopt;
        auto path = fs::absolute(*replacement).lexically_normal();
        load_glb(path);
        auto name = utf8(path.wstring());
        replacements.emplace_back(object.Asset, name);
        object.Asset = std::move(name);
    }
    repaired.validate();
    scene = std::move(repaired);
    return replacements.size();
}
}
