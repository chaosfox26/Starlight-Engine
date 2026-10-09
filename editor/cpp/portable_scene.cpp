#include "platform.hpp"
#include "assets.hpp"
#include "engine_assets.hpp"
#include "object_template.hpp"
#include <cstring>
namespace starlight::editor {
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
        for (auto &object : scene.Characters)
            if (object.Shape || object.Sculpt) object.Asset.clear();
        // The key card goes out with exactly the engine assets it references (its dependency closure), in the bundle's own asset folder.
        for (const auto &id : scene_asset_ids(scene)) {
            auto bytes = EngineAssets::verified_file_bytes(id);
            auto relative = fs::path(L"assets") / wide(id + ".slasset");
            atomic_write(stage / relative, bytes);
            assets.push_back({{"path", utf8(relative.generic_wstring())}, {"sha256", hash_bytes(bytes)}, {"bytes", bytes.size()}});
        }
        atomic_write(stage / L"scene.starscene", scene.encode());
        json manifest{{"format","starlight-portable-scene"},{"version",1},{"scene","scene.starscene"},
            {"sceneSha256",hash_file(stage / L"scene.starscene")},{"assets",assets},
            {"scope","Editor scene and its engine assets (sculpts, terrain, models); not an executable game"}};
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
        if (relative.is_absolute() || relative.has_root_name() || relative.empty() || name.find(':') != std::string::npos)
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
            throw std::runtime_error("Portable asset changed since export");
        verified.push_back(path);
    }
    EngineAssets::ScopedRoot bundleLibrary(root / L"assets");
    auto sceneText = read_file(scenePath);
    for (const auto &id : EngineAssets::referenced_ids(json::parse(sceneText))) {
        // canonical() throws for a file that is not in the folder, so an absent asset is refused with the manifest message before it is resolved.
        const auto name = "assets/" + id + ".slasset";
        std::error_code ignored;
        if (!fs::is_regular_file(root / wide(name), ignored) || std::find(verified.begin(), verified.end(), contained(name)) == verified.end())
            throw std::runtime_error("Scene references an engine asset missing from its manifest");
        EngineAssets::verify_on_disk(id);
    }
    (void)SceneData::decode(sceneText);
    return {{"success",true},{"assetCount",assets.size()},{"note","Exported scene and engine asset hashes match; this is integrity verification, not runtime certification."}};
}
void verify_exported_content(const fs::path &folder) {
    // The manifest is what makes an export verifiable, so a folder that looks like an export but has lost its manifest is refused instead of being run unchecked.
    if (fs::exists(folder / L"manifest.json")) { (void)verify_portable_scene(folder); return; }
    std::error_code ec;
    bool bundled = false;
    for (fs::directory_iterator it(folder / L"assets", ec), end; !ec && it != end && !bundled; it.increment(ec)) bundled = it->path().extension() == L".slasset";
    const auto absolute = fs::absolute(folder);
    if (bundled || (absolute.filename() == L"content" && fs::exists(absolute.parent_path() / L"game-manifest.json")))
        throw std::runtime_error("manifest.json is missing from the exported content, so it cannot be verified; export the game again");
}
void verify_game_files(const fs::path &folder) {
    // A plain scene run has no game-manifest.json and is not checked; an exported game must still hold exactly the runtime files it was exported with.
    const auto manifestPath = folder / L"game-manifest.json";
    if (!fs::exists(manifestPath)) return;
    const auto manifest = json::parse(read_file(manifestPath));
    if (!manifest.is_object() || !manifest.contains("files") || !manifest.at("files").is_array())
        throw std::runtime_error("game-manifest.json is damaged, so the game files cannot be checked; export the game again");
    for (const auto &entry : manifest.at("files")) {
        const auto name = entry.at("path").get<std::string>();
        const auto relative = fs::u8path(name);
        if (relative.empty() || relative.is_absolute() || relative.has_root_name() || std::any_of(relative.begin(), relative.end(), [](const fs::path &part) { return part == L".."; }))
            throw std::runtime_error("game-manifest.json lists an invalid file path: " + name);
        const auto path = folder / relative;
        if (!fs::is_regular_file(path)) throw std::runtime_error("Game file is missing: " + name + ". Reinstall or export the game again.");
        if (_stricmp(hash_file(path).c_str(), entry.at("sha256").get<std::string>().c_str()))
            throw std::runtime_error("Game file was changed since export: " + name + ". Reinstall or export the game again.");
    }
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
    drop_unresolved_links(selected.Characters, std::unordered_set<std::string>(ids.begin(), ids.end()));
    return export_portable_scene(selected, destination);
}
std::optional<size_t> repair_scene_models(SceneData &scene,
    const std::function<std::optional<fs::path>(const std::string&, const std::string&)>& choose) {
    auto repaired = scene;
    std::vector<std::pair<std::string,std::string>> replacements;
    std::set<std::string> intact;
    for (auto &object : repaired.Characters) {
        if (object.Shape || object.Sculpt || object.Asset.empty()) continue;
        if (intact.contains(object.Asset)) continue;
        auto existing = std::find_if(replacements.begin(), replacements.end(), [&](const auto &entry) { return entry.first == object.Asset; });
        if (existing != replacements.end()) { object.Asset = existing->second; continue; }
        if (model_available(object.Asset)) { intact.insert(object.Asset); continue; }
        auto replacement = choose(object.Asset, object.Name);
        if (!replacement) return std::nullopt;
        auto id = import_model(fs::absolute(*replacement).lexically_normal());
        replacements.emplace_back(object.Asset, id);
        object.Asset = std::move(id);
    }
    repaired.validate();
    scene = std::move(repaired);
    return replacements.size();
}
}
