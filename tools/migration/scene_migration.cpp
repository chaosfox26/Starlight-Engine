#include "assets.hpp"
#include "engine_assets.hpp"
#include "object_template.hpp"
#include "platform.hpp"
#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <shlobj.h>
#include <sstream>
using namespace starlight::editor;

namespace {

struct Refused : std::runtime_error {
    using std::runtime_error::runtime_error;
};

constexpr int ObjectSchema = 2;

enum class Kind { Scene, Object, Group };

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
std::string generic(const fs::path& p) { return utf8(p.generic_wstring()); }
fs::path from_utf8(const std::string& s) { return fs::path(wide(s)); }
std::string sha_of(std::string_view data) { return lower(hash_bytes(data)); }
bool ends_with(const std::string& s, const std::string& tail) { return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0; }
bool contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
std::string clip(std::string s, size_t n = 120) {
    if (s.size() > n) s = s.substr(0, n) + "...";
    return s;
}
fs::path canonical_guess(const fs::path& p) {
    std::error_code error;
    auto x = fs::weakly_canonical(fs::absolute(p), error);
    return error ? fs::path{} : x;
}
bool same_path(const fs::path& a, const fs::path& b) {
    const auto x = canonical_guess(a), y = canonical_guess(b);
    if (x.empty() || y.empty()) return false;
    return lower(generic(x)) == lower(generic(y));
}
std::string path_key(const fs::path& p) {
    const auto c = canonical_guess(p);
    return lower(generic(c.empty() ? fs::absolute(p) : c));
}
bool inside(const fs::path& file, const fs::path& folder) {
    const auto f = canonical_guess(file), r = canonical_guess(folder);
    if (f.empty() || r.empty()) return false;
    const auto relative = f.lexically_relative(r);
    return !relative.empty() && *relative.begin() != L"..";
}

void drop_lock(const fs::path& file) {
    std::error_code error;
    const fs::path lock(file.wstring() + L".lock");
    if (fs::is_regular_file(lock, error) && fs::file_size(lock, error) == 0) fs::remove(lock, error);
}
void sweep_locks(const fs::path& folder, bool recursive) {
    std::error_code error;
    if (!fs::is_directory(folder, error)) return;
    std::vector<fs::path> locks;
    auto consider = [&](const fs::directory_entry& entry) {
        std::error_code local;
        if (!entry.is_regular_file(local)) return;
        const auto name = lower(utf8(entry.path().filename().wstring()));
        if (!ends_with(name, ".lock") || entry.file_size(local) != 0) return;
        const fs::path guarded(entry.path().wstring().substr(0, entry.path().wstring().size() - 5));
        if (fs::is_regular_file(guarded, local)) locks.push_back(entry.path());
    };
    if (recursive) {
        for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end; it != end && !error; it.increment(error)) consider(*it);
    } else {
        for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, error), end; it != end && !error; it.increment(error)) consider(*it);
    }
    for (const auto& lock : locks) fs::remove(lock, error);
}
void write_text(const fs::path& path, const std::string& text) {
    atomic_write(path, text);
    drop_lock(path);
}

Kind kind_of_path(const fs::path& p) {
    const auto name = lower(utf8(p.filename().wstring()));
    if (ends_with(name, ".starobject")) return Kind::Object;
    if (ends_with(name, ".stargroup")) return Kind::Group;
    return Kind::Scene;
}
const char* kind_name(Kind kind) { return kind == Kind::Scene ? "scene" : kind == Kind::Object ? "object" : "group"; }
bool is_document_name(const std::string& name) { return ends_with(name, ".starscene") || ends_with(name, ".starobject") || ends_with(name, ".stargroup"); }

bool legacy_sculpt(const json& j) { return j.is_object() && (j.contains("Positions") || j.contains("Indices") || j.contains("MaterialCoordinates")); }
bool legacy_terrain(const json& j) { return j.is_object() && (j.contains("Heights") || j.contains("Paint") || j.contains("Volumes")); }

std::vector<json*> members(json& card, Kind kind) {
    std::vector<json*> out;
    if (kind == Kind::Scene) {
        if (card.contains("Characters") && card.at("Characters").is_array())
            for (auto& c : card.at("Characters")) out.push_back(&c);
    } else if (kind == Kind::Object) {
        if (card.contains("Object")) out.push_back(&card.at("Object"));
    } else if (card.contains("Objects") && card.at("Objects").is_array()) {
        for (auto& c : card.at("Objects")) out.push_back(&c);
    }
    return out;
}
std::vector<const json*> members(const json& card, Kind kind) {
    std::vector<const json*> out;
    for (auto* c : members(const_cast<json&>(card), kind)) out.push_back(c);
    return out;
}

bool model_active(const json& c) {
    if (!c.is_object()) return false;
    const bool shape = c.contains("Shape") && !c.at("Shape").is_null();
    const bool sculpt = c.contains("Sculpt") && !c.at("Sculpt").is_null();
    return !shape && !sculpt;
}
bool has_asset_path(const json& c) { return c.is_object() && c.contains("Asset") && c.at("Asset").is_string() && !c.at("Asset").get<std::string>().empty(); }
bool asset_is_path(const json& c) { return has_asset_path(c) && !EngineAssets::valid_id(AssetKind::Model, c.at("Asset").get<std::string>()); }

struct Scan {
    int64_t schema = -1;
    size_t objects = 0, sculpts = 0, sculptBytes = 0, terrainBytes = 0, models = 0, pathLinks = 0;
    bool legacy = false;
};
Scan scan(const json& j, Kind kind) {
    Scan s;
    if (!j.is_object()) throw std::runtime_error("Document is not a JSON object");
    if (kind == Kind::Scene) {
        if (j.contains("SchemaVersion") && j.at("SchemaVersion").is_number_integer()) s.schema = j.at("SchemaVersion").get<int64_t>();
    } else {
        const char* format = kind == Kind::Object ? "starlight-object" : "starlight-object-group";
        if (!j.contains("Format") || !j.at("Format").is_string() || j.at("Format").get<std::string>() != format) throw std::runtime_error(std::string("Not a ") + format + " document");
        if (!j.contains("Version") || !j.at("Version").is_number_integer()) throw std::runtime_error("Document has no integer Version");
        s.schema = j.at("Version").get<int64_t>();
        if (kind == Kind::Object && !(j.contains("Object") && j.at("Object").is_object())) throw std::runtime_error("Reusable object has no Object");
        if (kind == Kind::Group && !(j.contains("Objects") && j.at("Objects").is_array())) throw std::runtime_error("Reusable group has no Objects");
    }
    for (const auto* c : members(j, kind)) {
        ++s.objects;
        if (c->is_object() && c->contains("Sculpt") && legacy_sculpt(c->at("Sculpt"))) {
            ++s.sculpts;
            s.sculptBytes += c->at("Sculpt").dump().size();
        }
        if (asset_is_path(*c)) {
            ++s.pathLinks;
            if (model_active(*c)) ++s.models;
        }
    }
    if (kind == Kind::Scene && j.contains("Terrain") && legacy_terrain(j.at("Terrain"))) {
        const auto& t = j.at("Terrain");
        for (const char* key : {"Heights", "Paint", "Volumes"})
            if (t.contains(key)) s.terrainBytes += t.at(key).dump().size();
    }
    s.legacy = kind == Kind::Scene ? (s.schema < EngineAssets::KeyCardSchema || s.sculpts || s.terrainBytes || s.pathLinks) : (s.schema < ObjectSchema || s.sculpts || s.pathLinks);
    return s;
}

std::string classify(const fs::path& p) {
    const auto g = "/" + lower(generic(p));
    if (contains(g, "/editor/checks/roundtrip/")) return "generated-check";
    if (contains(g, "/audit-runs/")) return "audit-evidence";
    return "authored";
}
constexpr const char* OutsideClass = "user-scene-outside-repo";

std::vector<double> read_doubles(const json& j, const char* name) {
    if (!j.is_array()) throw std::runtime_error(std::string(name) + " is not an array");
    std::vector<double> out;
    out.reserve(j.size());
    for (const auto& v : j) {
        if (!v.is_number()) throw std::runtime_error(std::string(name) + " holds a non-number");
        out.push_back(v.get<double>());
    }
    return out;
}
std::vector<int> read_ints(const json& j, const char* name) {
    if (!j.is_array()) throw std::runtime_error(std::string(name) + " is not an array");
    std::vector<int> out;
    out.reserve(j.size());
    for (const auto& v : j) out.push_back(native_int(v));
    return out;
}

SculptMesh build_sculpt(const json& j) {
    SculptMesh mesh;
    if (!j.contains("Positions") || !j.contains("Indices")) throw std::runtime_error("Sculpt lacks Positions or Indices");
    mesh.Positions = read_doubles(j.at("Positions"), "Positions");
    mesh.Indices = read_ints(j.at("Indices"), "Indices");
    if (j.contains("MaterialCoordinates")) mesh.MaterialCoordinates = read_doubles(j.at("MaterialCoordinates"), "MaterialCoordinates");
    mesh.validate();
    return mesh;
}
TerrainData build_terrain(const json& j) {
    TerrainData t;
    if (!j.contains("Heights") || !j.contains("Paint")) throw std::runtime_error("Terrain lacks Heights or Paint");
    t.Heights = read_doubles(j.at("Heights"), "Heights");
    t.Paint = read_ints(j.at("Paint"), "Paint");
    if (j.contains("Volumes")) {
        if (!j.at("Volumes").is_array()) throw std::runtime_error("Volumes is not an array");
        for (const auto& v : j.at("Volumes")) {
            TerrainVolume volume;
            volume.X = v.at("X").get<double>();
            volume.Y = v.at("Y").get<double>();
            volume.Z = v.at("Z").get<double>();
            volume.Radius = v.at("Radius").get<double>();
            volume.Subtract = v.value("Subtract", true);
            t.Volumes.push_back(volume);
        }
    }
    t.Lava = j.value("Lava", false);
    t.WaterEnabled = j.value("WaterEnabled", false);
    t.WaterLevel = j.value("WaterLevel", -.5);
    t.Breakable = j.value("Breakable", true);
    t.DestructionHealth = j.value("DestructionHealth", 10.);
    t.DestructionRadius = j.value("DestructionRadius", .65);
    t.validate();
    return t;
}

bool same_bits(const std::vector<double>& a, const std::vector<double>& b) {
    return a.size() == b.size() && (a.empty() || !std::memcmp(a.data(), b.data(), a.size() * sizeof(double)));
}
bool same_bits(double a, double b) { return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b); }
bool same_sculpt(const SculptMesh& a, const SculptMesh& b) {
    return a == b && same_bits(a.Positions, b.Positions) && same_bits(a.MaterialCoordinates, b.MaterialCoordinates);
}
bool same_terrain(const TerrainData& a, const TerrainData& b) {
    if (!same_bits(a.Heights, b.Heights) || a.Paint != b.Paint || a.Volumes.size() != b.Volumes.size()) return false;
    for (size_t i = 0; i < a.Volumes.size(); ++i) {
        const auto &x = a.Volumes[i], &y = b.Volumes[i];
        if (!same_bits(x.X, y.X) || !same_bits(x.Y, y.Y) || !same_bits(x.Z, y.Z) || !same_bits(x.Radius, y.Radius) || x.Subtract != y.Subtract) return false;
    }
    return a.Lava == b.Lava && a.WaterEnabled == b.WaterEnabled && same_bits(a.WaterLevel, b.WaterLevel) && a.Breakable == b.Breakable &&
           same_bits(a.DestructionHealth, b.DestructionHealth) && same_bits(a.DestructionRadius, b.DestructionRadius);
}

struct Diff {
    std::vector<std::string> real;
    size_t realTotal = 0;
    std::map<std::string, size_t> added;
};
std::string normalize(const std::string& path) {
    std::string out;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '[') {
            out += "[]";
            while (i < path.size() && path[i] != ']') ++i;
        } else out += path[i];
    }
    return out;
}
void note_real(Diff& d, std::string text) {
    ++d.realTotal;
    if (d.real.size() < 200) d.real.push_back(std::move(text));
}
void diff_json(const json& expected, const json& actual, const std::string& path, Diff& d) {
    if (expected.is_object() && actual.is_object()) {
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            auto found = actual.find(it.key());
            if (found == actual.end()) {
                if (it.key() == "GameplayRole" && it.value().is_string() && it.value().get<std::string>() == "none")
                    ++d.added[normalize(path + "/" + it.key()) + " (explicit default omitted by the engine)"];
                else note_real(d, "dropped by the engine: " + path + "/" + it.key());
            }
            else diff_json(it.value(), *found, path + "/" + it.key(), d);
        }
        for (auto it = actual.begin(); it != actual.end(); ++it)
            if (!expected.contains(it.key())) ++d.added[normalize(path + "/" + it.key())];
    } else if (expected.is_array() && actual.is_array()) {
        if (expected.size() != actual.size()) {
            note_real(d, "array length differs at " + path + ": " + std::to_string(expected.size()) + " became " + std::to_string(actual.size()));
            return;
        }
        for (size_t i = 0; i < expected.size(); ++i) diff_json(expected[i], actual[i], path + "[" + std::to_string(i) + "]", d);
    } else if (!(expected == actual)) {
        note_real(d, "changed at " + path + ": " + clip(expected.dump(), 60) + " became " + clip(actual.dump(), 60));
    }
}

std::string disk_check(const std::string& id) {
    AssetKind kind;
    std::string head;
    if (id.rfind("sculpt-", 0) == 0) { kind = AssetKind::Sculpt; head = "sculpt-"; }
    else if (id.rfind("terrain-", 0) == 0) { kind = AssetKind::Terrain; head = "terrain-"; }
    else if (id.rfind("model-", 0) == 0) { kind = AssetKind::Model; head = "model-"; }
    else if (id.rfind("texture-", 0) == 0) { kind = AssetKind::Texture; head = "texture-"; }
    else if (id.rfind("blob-", 0) == 0) { kind = AssetKind::Blob; head = "blob-"; }
    else return "unknown asset kind";
    if (!EngineAssets::valid_id(kind, id)) return "invalid asset id";
    const auto path = EngineAssets::locate(id);
    if (path.empty()) return "asset file is missing from the library";
    const auto file = read_file(path);
    if (file.size() < 20 || std::memcmp(file.data(), "SLAS", 4)) return "not a Starlight asset file";
    uint32_t version, storedKind;
    uint64_t bytes;
    std::memcpy(&version, file.data() + 4, 4);
    std::memcpy(&storedKind, file.data() + 8, 4);
    std::memcpy(&bytes, file.data() + 12, 8);
    if (version != 1) return "unsupported asset file version";
    if (storedKind != uint32_t(kind)) return "asset file holds a different kind of asset";
    if (bytes != file.size() - 20) return "asset file is truncated or has trailing data";
    std::string keyed;
    keyed.append(reinterpret_cast<const char*>(&storedKind), 4);
    keyed.append(file, 20, std::string::npos);
    if (head + sha_of(keyed) != id) return "asset contents do not match their identity";
    return {};
}
uint64_t asset_file_bytes(const std::string& id) {
    const auto path = EngineAssets::locate(id);
    std::error_code error;
    return path.empty() ? 0 : fs::file_size(path, error);
}

struct Options {
    fs::path library, archive;
    std::vector<fs::path> sourceLibraries;
    bool inPlace = false;
    bool sweep = true;
    std::string relative;
};

std::vector<fs::path> default_source_libraries() {
    try {
        return EngineAssets::default_roots();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("source library: ") + e.what());
    }
}
void import_assets(const std::set<std::string>& ids, const fs::path& sourceFile, const fs::path& destination, const Options& o) {
    if (ids.empty()) return;
    const auto defaults = default_source_libraries();
    EngineAssets::ScopedRoot scope(defaults.empty() ? destination : defaults.front());
    for (size_t i = 1; i < defaults.size(); ++i) EngineAssets::add_search_root(defaults[i]);
    for (const auto& folder : o.sourceLibraries) EngineAssets::add_search_root(folder);
    EngineAssets::discover_near(fs::absolute(sourceFile).parent_path());
    try {
        EngineAssets::bundle_into(ids, destination);
    } catch (const AssetMissing& e) {
        throw std::runtime_error(std::string("source asset: ") + e.what());
    }
}

json load_manifest(const fs::path& path) {
    std::error_code error;
    if (!fs::exists(path, error)) return json{{"entries", json::array()}};
    auto manifest = json::parse(read_file(path));
    if (!manifest.is_object() || !manifest.contains("entries") || !manifest.at("entries").is_array()) throw std::runtime_error("Archive manifest is damaged");
    return manifest;
}
json archive_original(const fs::path& in, const std::string& original, const std::string& sha, const Options& o) {
    const std::string rel = o.relative.empty() ? sha.substr(0, 12) + "-" + utf8(in.filename().wstring()) : o.relative;
    const auto target = o.archive / L"files" / from_utf8(rel);
    const auto manifestPath = o.archive / L"manifest.json";
    auto manifest = load_manifest(manifestPath);
    for (const auto& e : manifest.at("entries"))
        if (e.value("archive", "") == rel && lower(e.value("sha256", "")) != sha)
            throw std::runtime_error("Archive entry " + rel + " already exists with a different hash; refusing to overwrite it");
    std::error_code error;
    if (fs::exists(target, error)) {
        if (sha_of(read_file(target)) != sha) throw std::runtime_error("Archive file " + generic(target) + " already exists with different contents; refusing to overwrite it");
    } else write_text(target, original);
    if (sha_of(read_file(target)) != sha) throw std::runtime_error("Archive copy does not hash like the original");
    bool listed = false;
    for (const auto& e : manifest.at("entries"))
        if (e.value("archive", "") == rel) listed = true;
    json entry = {{"path", generic(fs::absolute(in))}, {"archive", rel}, {"sha256", sha}, {"bytes", original.size()}};
    if (!listed) {
        manifest["entries"].push_back(entry);
        write_text(manifestPath, manifest.dump(2));
    }
    return entry;
}

struct ModelLink {
    size_t index = 0;
    std::string name, original, id, action;
    fs::path resolved;
    bool active = false, exists = false;
    uint64_t bytes = 0;
};
fs::path resolve_like_load_scene(const std::string& asset, const fs::path& sceneFile) {
    const fs::path path(wide(asset));
    if (path.is_relative()) return (fs::absolute(sceneFile).parent_path() / path).lexically_normal();
    return path;
}
std::vector<ModelLink> import_models(json& card, Kind kind, const fs::path& source, json& warnings) {
    std::vector<ModelLink> links;
    auto list = members(card, kind);
    std::vector<std::string> missing;
    for (size_t i = 0; i < list.size(); ++i) {
        auto& c = *list[i];
        if (!asset_is_path(c)) continue;
        ModelLink link;
        link.index = i;
        link.name = c.value("Name", std::string{});
        link.original = c.at("Asset").get<std::string>();
        link.active = model_active(c);
        link.resolved = resolve_like_load_scene(link.original, source);
        std::error_code error;
        link.exists = fs::is_regular_file(link.resolved, error);
        if (link.exists) link.bytes = fs::file_size(link.resolved, error);
        if (link.active && !link.exists) missing.push_back("object " + std::to_string(i) + " (" + link.name + ") -> " + utf8(link.resolved.wstring()));
        links.push_back(std::move(link));
    }
    if (!missing.empty()) {
        std::string text;
        for (const auto& m : missing) text += (text.empty() ? "" : "; ") + m;
        throw std::runtime_error("the model file(s) this document points to cannot be found, so nothing was converted: " + text);
    }
    std::map<std::string, std::string> done;
    for (auto& link : links) {
        auto& c = *list[link.index];
        auto clear = [&](const std::string& why) {
            c["Asset"] = "";
            link.action = "cleared";
            warnings.push_back("object " + std::to_string(link.index) + " (" + link.name + ") is a shape or sculpt, so its model link (" + link.original + ") is not used by the engine and was cleared" + why + "; the original file is archived");
        };
        const bool keepIgnored = !link.active && link.exists && kind == Kind::Scene;
        if (!link.active && !keepIgnored) { clear(""); continue; }
        const auto key = path_key(link.resolved);
        auto found = done.find(key);
        try {
            if (found == done.end()) {
                const auto id = import_model(link.resolved);
                if (read_file(link.resolved) != *EngineAssets::get(AssetKind::Model, id)) throw std::runtime_error("the stored copy differs from the source file");
                found = done.emplace(key, id).first;
            }
        } catch (const std::exception& e) {
            if (link.active) throw;
            clear(std::string(" (it also could not be imported: ") + e.what() + ")");
            continue;
        }
        link.id = found->second;
        link.action = "imported";
        c["Asset"] = link.id;
    }
    return links;
}
json models_json(const std::vector<ModelLink>& links) {
    json out = json::array();
    for (const auto& l : links)
        out.push_back({{"object", l.index}, {"name", l.name}, {"active", l.active}, {"original", l.original}, {"asset", l.id}, {"action", l.action}, {"resolved", utf8(l.resolved.wstring())},
                       {"exists", l.exists}, {"bytes", l.bytes}});
    return out;
}

json verify_document(const fs::path& file, const std::optional<fs::path>& library);

json convert_file(const fs::path& in, const fs::path& out, const Options& o) {
    if (same_path(in, out) && !o.inPlace) throw Refused("Refusing to write over the input; use a different --out (or --in-place, which archives the original first)");
    const Kind kind = kind_of_path(in);
    json report = {{"input", generic(fs::absolute(in))}, {"output", generic(fs::absolute(out))}, {"kind", kind_name(kind)}, {"result", "FAIL"}};
    std::string stage = "read";
    auto fail = [&](const std::string& why) { throw std::runtime_error(why); };
    json warnings = json::array();
    try {
        const auto original = read_file(in);
        const auto sha = sha_of(original);
        report["original_sha256"] = sha;
        report["original_bytes"] = original.size();
        stage = "parse";
        const json src = json::parse(original);
        const auto sc = scan(src, kind);
        report["schema_before"] = sc.schema;
        report["objects"] = sc.objects;
        const int64_t newest = kind == Kind::Scene ? EngineAssets::KeyCardSchema : ObjectSchema;
        if (sc.schema > newest) fail(std::string("Schema ") + std::to_string(sc.schema) + " is newer than this tool understands");
        const bool legacy = sc.legacy;
        report["legacy"] = legacy;
        bool archived = false;
        if (legacy) {
            stage = "archive";
            report["archive"] = archive_original(in, original, sha, o);
            archived = true;
        }
        EngineAssets::set_root(o.library);
        stage = "extract";
        json card = src;
        auto list = members(card, kind);
        std::vector<std::optional<SculptMesh>> sculpts(list.size());
        std::optional<TerrainData> terrain;
        std::vector<std::string> sculptIds;
        std::set<std::string> distinct;
        std::string terrainId;
        for (size_t i = 0; i < list.size(); ++i) {
            auto& c = *list[i];
            if (!c.is_object() || !c.contains("Sculpt") || !legacy_sculpt(c.at("Sculpt"))) continue;
            try {
                sculpts[i] = build_sculpt(c.at("Sculpt"));
                const auto id = store_sculpt(*sculpts[i]);
                c["Sculpt"] = json{{"Asset", id}};
                sculptIds.push_back(id);
                distinct.insert(id);
            } catch (const std::exception& e) {
                fail("object " + std::to_string(i) + " (" + c.value("Name", std::string{}) + ") sculpt: " + e.what());
            }
        }
        if (kind == Kind::Scene && card.contains("Terrain") && legacy_terrain(card.at("Terrain"))) {
            try {
                terrain = build_terrain(card.at("Terrain"));
                terrainId = store_terrain_geometry(*terrain);
                auto& t = card["Terrain"];
                t.erase("Heights");
                t.erase("Paint");
                t.erase("Volumes");
                t["Asset"] = terrainId;
                distinct.insert(terrainId);
            } catch (const std::exception& e) {
                fail(std::string("terrain: ") + e.what());
            }
        }
        stage = "models";
        std::vector<ModelLink> links = import_models(card, kind, in, warnings);
        for (const auto& l : links)
            if (!l.id.empty()) distinct.insert(l.id);
        report["models"] = models_json(links);
        stage = "import-assets";
        {
            auto referenced = EngineAssets::referenced_ids(card);
            std::set<std::string> missing;
            for (const auto& id : referenced)
                if (!distinct.contains(id)) missing.insert(id);
            if (!missing.empty()) import_assets(missing, in, o.library, o);
            distinct.insert(referenced.begin(), referenced.end());
        }
        if (!legacy) {
            for (const auto* c : list)
                if (c->is_object() && c->contains("Sculpt") && c->at("Sculpt").is_object() && c->at("Sculpt").contains("Asset") && c->at("Sculpt").at("Asset").is_string())
                    sculptIds.push_back(c->at("Sculpt").at("Asset").get<std::string>());
            if (kind == Kind::Scene && card.contains("Terrain") && card.at("Terrain").is_object() && card.at("Terrain").contains("Asset") && card.at("Terrain").at("Asset").is_string())
                terrainId = card.at("Terrain").at("Asset").get<std::string>();
        }
        if (kind == Kind::Scene) card["SchemaVersion"] = EngineAssets::KeyCardSchema;
        else card["Version"] = ObjectSchema;
        report["sculpt_assets"] = sculptIds;
        report["terrain_asset"] = terrainId.empty() ? json(nullptr) : json(terrainId);
        report["asset_ids"] = distinct;
        stage = "verify";
        for (const auto& id : distinct)
            if (auto problem = disk_check(id); !problem.empty()) fail("asset " + id + ": " + problem);
        for (const auto& id : distinct)
            if (id.rfind("model-", 0) == 0) {
                try {
                    (void)load_model(id);
                } catch (const std::exception& e) {
                    fail("model " + id + " does not load from the library: " + e.what());
                }
            }
        const auto cardText = card.dump(2);
        std::string encoded;
        json expected = card;
        size_t renamed = 0;
        if (kind == Kind::Scene) {
            const auto decoded = SceneData::decode(cardText);
            if (decoded.Characters.size() != list.size()) fail("object count changed");
            for (size_t i = 0; i < sculpts.size(); ++i) {
                if (!sculpts[i]) continue;
                if (!decoded.Characters[i].Sculpt || !same_sculpt(*sculpts[i], *decoded.Characters[i].Sculpt)) fail("object " + std::to_string(i) + " sculpt does not decode back to the legacy arrays bit-for-bit");
            }
            if (terrain) {
                if (!decoded.Terrain || !same_terrain(*terrain, *decoded.Terrain)) fail("terrain does not decode back to the legacy arrays bit-for-bit");
            }
            encoded = decoded.encode();
            const auto reencoded = SceneData::decode(encoded).encode();
            if (reencoded != encoded) fail("encoded key card is not stable under decode and re-encode");
        } else if (kind == Kind::Object) {
            const auto decoded = decode_object_template(cardText);
            if (sculpts.size() != 1) fail("reusable object has no Object");
            if (sculpts[0] && (!decoded.Sculpt || !same_sculpt(*sculpts[0], *decoded.Sculpt))) fail("object sculpt does not decode back to the legacy arrays bit-for-bit");
            encoded = encode_object_template(decoded);
            if (encode_object_template(decode_object_template(encoded)) != encoded) fail("encoded reusable object is not stable under decode and re-encode");
            auto& o2 = expected["Object"];
            if (o2.contains("Id")) {
                if (o2.at("Id").is_string() && !o2.at("Id").get<std::string>().empty()) ++renamed;
                o2["Id"] = "";
            }
        } else {
            const auto decoded = decode_object_group(cardText);
            if (decoded.size() != list.size()) fail("object count changed");
            for (size_t i = 0; i < sculpts.size(); ++i) {
                if (!sculpts[i]) continue;
                if (!decoded[i].Sculpt || !same_sculpt(*sculpts[i], *decoded[i].Sculpt)) fail("object " + std::to_string(i) + " sculpt does not decode back to the legacy arrays bit-for-bit");
            }
            encoded = encode_object_group(decoded);
            if (encode_object_group(decode_object_group(encoded)) != encoded) fail("encoded reusable group is not stable under decode and re-encode");
            auto& arr = expected["Objects"];
            std::map<std::string, std::string> rename;
            for (size_t i = 0; i < arr.size(); ++i) {
                const auto id = arr[i].value("Id", std::string{});
                if (!id.empty()) rename[id] = "member-" + std::to_string(i);
            }
            for (size_t i = 0; i < arr.size(); ++i)
                for (const char* key : {"Id", "JointTargetId", "LightningTargetId"}) {
                    if (!arr[i].contains(key) || !arr[i].at(key).is_string()) continue;
                    const auto value = arr[i].at(key).get<std::string>();
                    const auto found = rename.find(value);
                    if (value.empty() || found == rename.end()) continue;
                    if (value != found->second) ++renamed;
                    arr[i][key] = found->second;
                }
        }
        Diff diff;
        diff_json(expected, json::parse(encoded), "", diff);
        report["defaults_added"] = diff.added;
        report["real_differences"] = diff.real;
        report["real_difference_count"] = diff.realTotal;
        report["ids_normalized"] = renamed;
        if (diff.realTotal) fail(std::to_string(diff.realTotal) + " field(s) differ between the original and what the engine reads back; first: " + (diff.real.empty() ? "" : diff.real.front()));
        const bool verbatim = encoded == original;
        report["copied_verbatim"] = verbatim;
        stage = "write";
        const bool skipWrite = verbatim && same_path(in, out);
        if (!skipWrite) {
            if (!verbatim && !archived) {
                report["archive"] = archive_original(in, original, sha, o);
                archived = true;
            }
            if (kind != Kind::Scene) {
                const auto bundle = fs::absolute(out).parent_path() / L"assets";
                EngineAssets::bundle_into(EngineAssets::referenced_ids(json::parse(encoded)), bundle);
                report["bundle_folder"] = generic(bundle);
                if (o.sweep) sweep_locks(bundle, false);
            }
            write_text(out, encoded);
        }
        const auto written = read_file(out);
        if (written != encoded) fail("written file differs from the verified text");
        report["new_sha256"] = sha_of(written);
        report["new_bytes"] = written.size();
        stage = "verify-destination";
        const auto finalCheck = verify_document(out, kind == Kind::Scene ? std::optional<fs::path>(o.library) : std::nullopt);
        report["final_verify"] = finalCheck.at("result");
        if (finalCheck.at("result") != "PASS") fail("final verification of the written file failed: " + finalCheck.value("error", std::string{}));
        if (kind == Kind::Scene) {
            const auto& after = finalCheck.at("models");
            for (auto& l : links) {
                const json* found = nullptr;
                for (const auto& m : after)
                    if (m.at("object").get<size_t>() == l.index) found = &m;
                if (l.id.empty()) {
                    if (found) fail("the cleared model link for object " + std::to_string(l.index) + " is still present after the write");
                    continue;
                }
                if (!found) fail("model link for object " + std::to_string(l.index) + " is missing after the write");
                if (found->at("asset").get<std::string>() != l.id) fail("model link for object " + std::to_string(l.index) + " no longer names " + l.id);
            }
        }
        uint64_t libraryBytes = 0;
        for (const auto& id : distinct) libraryBytes += asset_file_bytes(id);
        report["asset_file_bytes"] = libraryBytes;
        report["distinct_assets"] = distinct.size();
        report["warnings"] = warnings;
        report["result"] = legacy ? "PASS" : "already key card";
    } catch (const std::exception& e) {
        report["result"] = "FAIL";
        report["error"] = stage + ": " + e.what();
        report["warnings"] = warnings;
    }
    try {
        write_text(fs::path(out.wstring() + L".migration-report.json"), report.dump(2));
    } catch (const std::exception& e) {
        report["report_error"] = e.what();
    }
    if (o.sweep) {
        sweep_locks(o.library, false);
        sweep_locks(o.archive, true);
        sweep_locks(fs::absolute(out).parent_path(), false);
    }
    return report;
}

json verify_scene(const fs::path& file, const fs::path& library) {
    json report = {{"file", generic(fs::absolute(file))}, {"kind", "scene"}, {"result", "FAIL"}};
    try {
        EngineAssets::set_root(library);
        const auto text = read_file(file);
        report["bytes"] = text.size();
        report["sha256"] = sha_of(text);
        const json raw = json::parse(text);
        const auto sc = scan(raw, Kind::Scene);
        if (sc.legacy) throw std::runtime_error("File still embeds geometry or predates schema 16");
        const auto ids = EngineAssets::referenced_ids(raw);
        json assets = json::array();
        bool assetsOk = true;
        for (const auto& id : ids) {
            const auto problem = disk_check(id);
            assets.push_back({{"id", id}, {"ok", problem.empty()}, {"problem", problem}});
            assetsOk = assetsOk && problem.empty();
        }
        report["assets"] = assets;
        if (!assetsOk) throw std::runtime_error("one or more referenced assets are missing or damaged");
        const auto scene = SceneData::decode(text);
        const auto encoded = scene.encode();
        report["stable"] = encoded == text;
        if (encoded != text) throw std::runtime_error("decode then encode does not reproduce the file text");
        json models = json::array();
        json warnings = json::array();
        for (size_t i = 0; i < scene.Characters.size(); ++i) {
            const auto& c = scene.Characters[i];
            if (c.Asset.empty()) continue;
            const bool active = !c.Shape && !c.Sculpt;
            models.push_back({{"object", i}, {"name", c.Name}, {"active", active}, {"asset", c.Asset}});
            if (!active) continue;
            try {
                (void)load_model(c.Asset);
            } catch (const std::exception& e) {
                throw std::runtime_error("the model of object " + std::to_string(i) + " (" + c.Name + ") cannot be loaded: " + e.what());
            }
        }
        report["models"] = models;
        report["warnings"] = warnings;
        report["result"] = "PASS";
    } catch (const std::exception& e) {
        report["error"] = e.what();
    }
    return report;
}

json verify_template(const fs::path& file, Kind kind) {
    json report = {{"file", generic(fs::absolute(file))}, {"kind", kind_name(kind)}, {"result", "FAIL"}};
    try {
        const auto text = read_file(file);
        report["bytes"] = text.size();
        report["sha256"] = sha_of(text);
        const json raw = json::parse(text);
        const auto sc = scan(raw, kind);
        if (sc.legacy) throw std::runtime_error("File still embeds geometry or predates version 2");
        const auto bundle = fs::absolute(file).parent_path() / L"assets";
        EngineAssets::ScopedRoot scope(bundle);
        const auto ids = EngineAssets::referenced_ids(raw);
        json assets = json::array();
        bool assetsOk = true;
        for (const auto& id : ids) {
            const auto problem = disk_check(id);
            assets.push_back({{"id", id}, {"ok", problem.empty()}, {"problem", problem}});
            assetsOk = assetsOk && problem.empty();
        }
        report["assets"] = assets;
        report["bundle_folder"] = generic(bundle);
        if (!assetsOk) throw std::runtime_error("one or more referenced assets are missing or damaged in the assets folder beside the file");
        std::string encoded;
        if (kind == Kind::Object) encoded = encode_object_template(decode_object_template(text));
        else encoded = encode_object_group(decode_object_group(text));
        report["stable"] = encoded == text;
        if (encoded != text) throw std::runtime_error("decode then encode does not reproduce the file text");
        report["warnings"] = json::array();
        report["result"] = "PASS";
    } catch (const std::exception& e) {
        report["error"] = e.what();
    }
    return report;
}

json verify_document(const fs::path& file, const std::optional<fs::path>& library) {
    const auto kind = kind_of_path(file);
    if (kind == Kind::Scene) {
        if (!library) throw std::runtime_error("Verifying a scene needs --library");
        return verify_scene(file, *library);
    }
    return verify_template(file, kind);
}

fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR text = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &text))) return {};
    fs::path p(text);
    CoTaskMemFree(text);
    return p;
}
bool reparse_point(const fs::path& p) {
    const auto attributes = GetFileAttributesW(p.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
std::vector<fs::path> find_documents(const fs::path& root, std::string& problem) {
    static const std::set<std::string> skipDirs{"out", "publication", ".git", "node_modules"};
    std::vector<fs::path> found;
    std::error_code error;
    if (!fs::is_directory(root, error)) {
        problem = "folder does not exist";
        return found;
    }
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, error), end; it != end; it.increment(error)) {
        if (error) {
            problem = error.message();
            break;
        }
        if (it->is_directory(error)) {
            if (skipDirs.contains(lower(utf8(it->path().filename().wstring()))) || reparse_point(it->path())) it.disable_recursion_pending();
            continue;
        }
        if (is_document_name(lower(utf8(it->path().filename().wstring())))) found.push_back(it->path());
    }
    std::sort(found.begin(), found.end());
    return found;
}

json inventory_entry(const fs::path& file, const fs::path& root, const std::string& prefix, const std::string& location, bool open) {
    json e;
    std::error_code error;
    const auto kind = kind_of_path(file);
    const auto relative = file.lexically_relative(root);
    e["path"] = generic(file);
    e["kind"] = kind_name(kind);
    e["relative"] = generic(relative);
    e["bytes"] = fs::file_size(file, error);
    if (location.empty()) {
        e["stage_relative"] = prefix.empty() ? generic(relative) : generic(from_utf8(prefix) / relative);
        e["class"] = classify(file);
    } else {
        e["location"] = location;
        e["class"] = OutsideClass;
    }
    if (!open) {
        e["status"] = "not opened (outside the repo)";
        return e;
    }
    try {
        const auto text = read_file(file);
        e["sha256"] = sha_of(text);
        const auto sc = scan(json::parse(text), kind);
        e["schema"] = sc.schema;
        e["objects"] = sc.objects;
        e["embedded_sculpts"] = sc.sculpts;
        e["embedded_sculpt_bytes"] = sc.sculptBytes;
        e["embedded_terrain_bytes"] = sc.terrainBytes;
        e["model_links"] = sc.models;
        e["status"] = sc.legacy ? "legacy" : "key card";
    } catch (const std::exception& ex) {
        e["status"] = "unreadable";
        e["error"] = ex.what();
    }
    return e;
}

std::string mib(double bytes) {
    std::ostringstream s;
    s.setf(std::ios::fixed);
    s.precision(2);
    s << bytes / 1048576.0 << " MiB";
    return s.str();
}

struct Location {
    std::string label;
    fs::path path;
};
std::vector<Location> extra_locations(const std::vector<std::string>& more) {
    std::vector<Location> out;
    auto add = [&](const std::string& label, const fs::path& path) {
        if (path.empty()) return;
        for (const auto& existing : out)
            if (same_path(existing.path, path)) return;
        out.push_back({label, path});
    };
    if (const auto local = known_folder(FOLDERID_LocalAppData); !local.empty()) add("AppData/Local/Starlight", local / L"Starlight");
    wchar_t configured[32768];
    const auto length = GetEnvironmentVariableW(L"STARLIGHT_DATA_ROOT", configured, 32768);
    if (length && length < 32768) add("STARLIGHT_DATA_ROOT", fs::path(configured));
    if (const auto profile = known_folder(FOLDERID_Profile); !profile.empty()) add("Documents (profile folder)", profile / L"Documents");
    add("Documents (known folder)", known_folder(FOLDERID_Documents));
    for (const auto& m : more) add("extra-root", fs::absolute(from_utf8(m)));
    return out;
}

int run_inventory(const std::vector<std::string>& folders, const fs::path& out, bool extraScan, bool inspectOutside, const std::vector<std::string>& moreRoots) {
    json files = json::array();
    json rootsJson = json::array();
    std::vector<fs::path> roots;
    std::set<std::string> seen, prefixes;
    for (const auto& folderText : folders) {
        const auto root = fs::absolute(from_utf8(folderText)).lexically_normal();
        std::error_code error;
        if (!fs::is_directory(root, error)) throw std::runtime_error("Inventory folder does not exist: " + generic(root));
        std::string base = utf8(root.filename().wstring());
        if (base.empty()) base = lower(utf8(root.root_name().wstring())).substr(0, 1);
        if (base.empty()) base = "root";
        std::string prefix = base;
        for (int n = 2; prefixes.contains(lower(prefix)); ++n) prefix = base + "-" + std::to_string(n);
        prefixes.insert(lower(prefix));
        const bool multiple = folders.size() > 1;
        std::string problem;
        const auto found = find_documents(root, problem);
        size_t added = 0;
        for (const auto& f : found) {
            const auto key = path_key(f);
            if (!seen.insert(key).second) continue;
            files.push_back(inventory_entry(f, root, multiple ? prefix : std::string{}, {}, true));
            ++added;
        }
        roots.push_back(root);
        rootsJson.push_back({{"path", generic(root)}, {"stage_prefix", multiple ? json(prefix) : json(nullptr)}, {"documents", added}, {"problem", problem}});
    }
    json locations = json::array();
    if (extraScan) {
        for (const auto& loc : extra_locations(moreRoots)) {
            std::string problem;
            std::error_code error;
            const bool exists = fs::is_directory(loc.path, error);
            size_t listed = 0;
            if (exists) {
                const auto root = fs::absolute(loc.path).lexically_normal();
                for (const auto& f : find_documents(root, problem)) {
                    bool repo = false;
                    for (const auto& r : roots)
                        if (inside(f, r)) repo = true;
                    if (repo) continue;
                    if (!seen.insert(path_key(f)).second) continue;
                    files.push_back(inventory_entry(f, root, {}, loc.label, inspectOutside));
                    ++listed;
                }
            } else problem = "folder does not exist";
            locations.push_back({{"label", loc.label}, {"path", generic(loc.path)}, {"exists", exists}, {"documents", listed}, {"problem", problem}});
        }
    }
    std::map<std::string, size_t> counts, legacyCounts, kinds;
    uint64_t totalBytes = 0, embeddedBytes = 0;
    for (const auto& f : files) {
        ++counts[f.at("class").get<std::string>()];
        ++kinds[f.at("kind").get<std::string>()];
        if (f.at("status") == "legacy") ++legacyCounts[f.at("class").get<std::string>()];
        totalBytes += f.at("bytes").get<uint64_t>();
        if (f.contains("embedded_terrain_bytes")) embeddedBytes += f.at("embedded_terrain_bytes").get<uint64_t>() + f.at("embedded_sculpt_bytes").get<uint64_t>();
    }
    json doc = {{"files", files}, {"roots", rootsJson}, {"extra_locations", locations}, {"counts", counts}, {"kinds", kinds}, {"legacy_counts", legacyCounts},
                {"total_bytes", totalBytes}, {"embedded_geometry_bytes", embeddedBytes}};
    write_text(out, doc.dump(2));
    std::ostringstream md;
    md << "# Scene inventory\n\n";
    md << "Documents found: " << files.size() << " (";
    bool firstKind = true;
    for (const auto& [k, v] : kinds) {
        md << (firstKind ? "" : ", ") << v << " " << k;
        firstKind = false;
    }
    md << "), total size " << mib(double(totalBytes)) << ", embedded geometry text " << mib(double(embeddedBytes)) << ".\n\n";
    md << "Roots scanned:\n\n";
    for (const auto& r : rootsJson) md << "- " << r.at("path").get<std::string>() << ": " << r.at("documents").dump() << " documents\n";
    if (!locations.empty()) {
        md << "\nExtra locations scanned (inventory only, never converted):\n\n";
        for (const auto& l : locations)
            md << "- " << l.at("label").get<std::string>() << " (" << l.at("path").get<std::string>() << "): " << (l.at("exists").get<bool>() ? std::to_string(l.at("documents").get<size_t>()) + " documents" : std::string("folder not present"))
               << (l.at("problem").get<std::string>().empty() || !l.at("exists").get<bool>() ? "" : "; note: " + l.at("problem").get<std::string>()) << "\n";
    }
    md << "\n| Class | Documents | Still in the old format |\n|---|---|---|\n";
    for (const auto& [k, v] : counts) md << "| " << k << " | " << v << " | " << legacyCounts[k] << " |\n";
    md << "\n| Path | Kind | Class | Schema | Objects | Embedded sculpts | Embedded terrain bytes | Model links | Bytes | Status |\n|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& f : files) {
        const auto shown = f.contains("location") ? "[" + f.at("location").get<std::string>() + "] " + f.at("relative").get<std::string>() : f.at("relative").get<std::string>();
        md << "| " << shown << " | " << f.at("kind").get<std::string>() << " | " << f.at("class").get<std::string>() << " | " << (f.contains("schema") ? f.at("schema").dump() : "-") << " | "
           << (f.contains("objects") ? f.at("objects").dump() : "-") << " | " << (f.contains("embedded_sculpts") ? f.at("embedded_sculpts").dump() : "-") << " | "
           << (f.contains("embedded_terrain_bytes") ? f.at("embedded_terrain_bytes").dump() : "-") << " | " << (f.contains("model_links") ? f.at("model_links").dump() : "-") << " | "
           << f.at("bytes").dump() << " | " << f.at("status").get<std::string>() << " |\n";
    }
    auto mdPath = out;
    mdPath.replace_extension(L".md");
    write_text(mdPath, md.str());
    std::cout << "Documents: " << files.size() << "\n";
    for (const auto& [k, v] : counts) std::cout << "  " << k << ": " << v << " (old format: " << legacyCounts[k] << ")\n";
    for (const auto& l : locations) std::cout << "  extra location " << l.at("label").get<std::string>() << ": " << l.at("documents").dump() << " documents\n";
    std::cout << "Inventory written to " << generic(out) << "\n";
    sweep_locks(out.parent_path(), false);
    return 0;
}

int run_convert_all(const fs::path& inventoryPath, Options o, const fs::path& stage) {
    o.sweep = false;
    const auto inventory = json::parse(read_file(inventoryPath));
    json results = json::array();
    std::map<std::string, std::set<std::string>> assetScenes;
    std::map<std::string, size_t> assetUses;
    std::map<std::string, json> modelUse;
    uint64_t before = 0, after = 0, copiedBytes = 0;
    size_t pass = 0, failed = 0, already = 0, regenerate = 0, outside = 0, warningCount = 0;
    std::map<std::string, size_t> addedTotals, kindPass;
    for (const auto& f : inventory.at("files")) {
        const auto cls = f.at("class").get<std::string>();
        const auto kindText = f.contains("kind") ? f.at("kind").get<std::string>() : std::string(kind_name(kind_of_path(from_utf8(f.at("path").get<std::string>()))));
        const auto rel = f.contains("stage_relative") ? f.at("stage_relative").get<std::string>() : f.at("relative").get<std::string>();
        json row = {{"path", f.at("path")}, {"relative", rel}, {"class", cls}, {"kind", kindText}, {"bytes_before", f.at("bytes")}};
        if (cls == "generated-check") {
            row["result"] = "regenerate";
            ++regenerate;
            results.push_back(row);
            continue;
        }
        if (cls == OutsideClass) {
            row["result"] = "outside repo (inventory only)";
            row["location"] = f.value("location", std::string{});
            ++outside;
            results.push_back(row);
            continue;
        }
        if (f.at("status") == "unreadable") {
            row["result"] = "FAIL";
            row["error"] = "unreadable in inventory: " + f.value("error", "");
            ++failed;
            results.push_back(row);
            continue;
        }
        const auto source = from_utf8(f.at("path").get<std::string>());
        const auto target = stage / from_utf8(rel);
        Options local = o;
        local.relative = rel;
        json report;
        try {
            if (sha_of(read_file(source)) != lower(f.at("sha256").get<std::string>())) {
                report = {{"result", "FAIL"}, {"error", "file changed since the inventory was made"}};
            } else report = convert_file(source, target, local);
        } catch (const std::exception& e) {
            report = {{"result", "FAIL"}, {"error", e.what()}};
        }
        const auto result = report.value("result", std::string("FAIL"));
        row["result"] = result;
        if (result == "PASS" || result == "already key card") {
            const bool converted = result == "PASS";
            if (converted) ++pass;
            else ++already;
            ++kindPass[kindText];
            row["bytes_after"] = report.at("new_bytes");
            row["asset_file_bytes"] = report.at("asset_file_bytes");
            row["sculpt_references"] = report.at("sculpt_assets").size();
            row["terrain_asset"] = report.at("terrain_asset");
            row["defaults_added"] = report.at("defaults_added");
            row["sha256_before"] = report.at("original_sha256");
            row["sha256_after"] = report.at("new_sha256");
            row["copied_verbatim"] = report.at("copied_verbatim");
            row["warnings"] = report.at("warnings");
            if (report.contains("models")) row["models"] = report.at("models");
            if (report.contains("bundle_folder")) row["bundle_folder"] = report.at("bundle_folder");
            warningCount += report.at("warnings").size();
            if (converted) {
                before += f.at("bytes").get<uint64_t>();
                after += report.at("new_bytes").get<uint64_t>();
            } else copiedBytes += report.at("new_bytes").get<uint64_t>();
            std::set<std::string> ids;
            for (const auto& id : report.at("asset_ids")) {
                ids.insert(id.get<std::string>());
                ++assetUses[id.get<std::string>()];
            }
            for (const auto& id : ids) assetScenes[id].insert(rel);
            for (const auto& [k, v] : report.at("defaults_added").items()) addedTotals[k] += v.get<size_t>();
            if (report.contains("models"))
                for (const auto& m : report.at("models")) {
                    if (!m.at("active").get<bool>()) continue;
                    auto& use = modelUse[lower(m.at("resolved").get<std::string>())];
                    if (use.is_null()) use = {{"path", m.at("resolved")}, {"asset", m.at("asset")}, {"exists", m.at("exists")}, {"bytes", m.at("bytes")}, {"scenes", json::array()}};
                    use["scenes"].push_back(rel);
                }
        } else {
            ++failed;
            row["error"] = report.value("error", std::string{});
            if (report.contains("real_differences")) row["real_differences"] = report.at("real_differences");
        }
        results.push_back(row);
    }
    uint64_t libraryBytes = 0, sharedAssets = 0, reusedInScene = 0;
    json assets = json::array();
    for (const auto& [id, scenes] : assetScenes) {
        const auto bytes = asset_file_bytes(id);
        libraryBytes += bytes;
        if (scenes.size() > 1) ++sharedAssets;
        if (assetUses[id] > scenes.size()) ++reusedInScene;
        assets.push_back({{"id", id}, {"bytes", bytes}, {"scenes", scenes}, {"references", assetUses[id]}});
    }
    json models = json::array();
    size_t missingModels = 0;
    for (const auto& [key, use] : modelUse) {
        models.push_back(use);
        if (!use.at("exists").get<bool>()) ++missingModels;
    }
    json summary = {{"pass", pass}, {"fail", failed}, {"already_key_card", already}, {"regenerate", regenerate}, {"outside_repo_inventory_only", outside}, {"warnings", warningCount},
                    {"scene_bytes_before", before}, {"scene_bytes_after", after}, {"already_key_card_bytes", copiedBytes}, {"library_bytes", libraryBytes},
                    {"distinct_assets", assetScenes.size()}, {"assets_shared_between_scenes", sharedAssets}, {"assets_referenced_more_than_once_overall", reusedInScene},
                    {"distinct_model_files", models.size()}, {"missing_model_files", missingModels}, {"defaults_added_totals", addedTotals}, {"files", results}, {"assets", assets},
                    {"model_dependencies", models}};
    write_text(stage / L"conversion-summary.json", summary.dump(2));
    std::ostringstream md;
    md << "# Scene conversion summary\n\n";
    md << "Converted PASS: " << pass << ", FAIL: " << failed << ", already key card (copied and verified): " << already << ", to regenerate (not converted): " << regenerate
       << ", outside the repo (inventory only): " << outside << ", warnings: " << warningCount << ".\n\n";
    md << "Document text converted: before " << mib(double(before)) << ", after " << mib(double(after)) << "; already-key-card text copied: " << mib(double(copiedBytes)) << "; library holds "
       << assetScenes.size() << " distinct assets (" << mib(double(libraryBytes)) << "), " << sharedAssets << " shared between documents.\n\n";
    md << "| Path | Class | Kind | Result | Bytes before | Bytes after | Sculpt refs | Terrain asset | Model links | Note |\n|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& r : results) {
        size_t active = 0, missing = 0;
        if (r.contains("models"))
            for (const auto& m : r.at("models"))
                if (m.at("active").get<bool>()) {
                    ++active;
                    if (!m.at("exists").get<bool>()) ++missing;
                }
        std::string note;
        if (r.at("result") == "already key card") note = r.at("copied_verbatim").get<bool>() ? "already key card (copied byte for byte)" : "already key card (rewritten: text made canonical)";
        else if (r.at("result") == "outside repo (inventory only)") note = r.value("location", std::string{});
        md << "| " << r.at("relative").get<std::string>() << " | " << r.at("class").get<std::string>() << " | " << r.at("kind").get<std::string>() << " | " << r.at("result").get<std::string>() << " | "
           << r.at("bytes_before").dump() << " | " << (r.contains("bytes_after") ? r.at("bytes_after").dump() : "-") << " | " << (r.contains("sculpt_references") ? r.at("sculpt_references").dump() : "-") << " | "
           << (r.contains("terrain_asset") && !r.at("terrain_asset").is_null() ? r.at("terrain_asset").get<std::string>().substr(0, 20) : "-") << " | "
           << (r.contains("models") ? std::to_string(active) + (missing ? " (" + std::to_string(missing) + " missing)" : "") : "-") << " | " << note << " |\n";
    }
    if (failed) {
        md << "\n## Failures\n\n";
        for (const auto& r : results)
            if (r.at("result") == "FAIL") md << "- " << r.at("relative").get<std::string>() << ": " << r.value("error", std::string{}) << "\n";
    }
    if (warningCount) {
        md << "\n## Warnings\n\n";
        for (const auto& r : results)
            if (r.contains("warnings"))
                for (const auto& w : r.at("warnings")) md << "- " << r.at("relative").get<std::string>() << ": " << w.get<std::string>() << "\n";
    }
    md << "\n## Model imports per scene (each GLB is stored once in the shared library; the scene keeps the model id)\n\n";
    for (const auto& r : results) {
        if (!r.contains("models")) continue;
        bool header = false;
        for (const auto& m : r.at("models")) {
            if (!m.at("active").get<bool>()) continue;
            if (!header) {
                md << "- " << r.at("relative").get<std::string>() << "\n";
                header = true;
            }
            md << "  - object " << m.at("object").dump() << " (" << m.at("name").get<std::string>() << "): " << m.at("resolved").get<std::string>() << " - "
               << (m.at("exists").get<bool>() ? "found, " + m.at("bytes").dump() + " bytes" : std::string("MISSING")) << (m.at("asset").get<std::string>().empty() ? std::string{} : "; stored as " + m.at("asset").get<std::string>()) << "\n";
        }
    }
    md << "\nDistinct model files: " << models.size() << ", missing: " << missingModels << ".\n";
    if (!addedTotals.empty()) {
        md << "\n## Fields the engine added with defaults (field path: count)\n\n";
        for (const auto& [k, v] : addedTotals) md << "- " << k << ": " << v << "\n";
    }
    md << "\n## Assets used by more than one document\n\n";
    for (const auto& a : assets)
        if (a.at("scenes").size() > 1) md << "- " << a.at("id").get<std::string>() << " (" << a.at("bytes").dump() << " bytes): " << a.at("scenes").size() << " documents\n";
    write_text(stage / L"conversion-summary.md", md.str());
    sweep_locks(o.library, false);
    sweep_locks(o.archive, true);
    sweep_locks(stage, true);
    std::cout << "PASS " << pass << ", FAIL " << failed << ", already key card " << already << ", regenerate " << regenerate << ", outside repo " << outside << ", warnings " << warningCount << "\n";
    std::cout << "Scene bytes " << before << " -> " << after << "; library " << libraryBytes << " bytes in " << assetScenes.size() << " assets (" << sharedAssets << " shared between documents)\n";
    return failed ? 1 : 0;
}

int run_verify_all(const fs::path& stage, const fs::path& library, const fs::path& out) {
    std::vector<fs::path> files;
    std::error_code error;
    if (!fs::is_directory(stage, error)) throw std::runtime_error("Stage folder does not exist: " + generic(stage));
    for (fs::recursive_directory_iterator it(stage, fs::directory_options::skip_permission_denied, error), end; it != end && !error; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        if (is_document_name(lower(utf8(it->path().filename().wstring())))) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    json results = json::array();
    size_t pass = 0, failed = 0, warningCount = 0;
    for (const auto& f : files) {
        json report;
        try {
            report = verify_document(f, library);
        } catch (const std::exception& e) {
            report = {{"file", generic(f)}, {"result", "FAIL"}, {"error", e.what()}};
        }
        json row = {{"relative", generic(f.lexically_relative(stage))}, {"kind", report.value("kind", std::string{})}, {"result", report.at("result")}, {"sha256", report.value("sha256", std::string{})}};
        if (report.at("result") == "PASS") {
            ++pass;
            row["warnings"] = report.at("warnings");
            warningCount += report.at("warnings").size();
        } else {
            ++failed;
            row["error"] = report.value("error", std::string{});
        }
        results.push_back(row);
    }
    json doc = {{"stage", generic(stage)}, {"library", generic(library)}, {"pass", pass}, {"fail", failed}, {"warnings", warningCount}, {"files", results}};
    write_text(out, doc.dump(2));
    std::ostringstream md;
    md << "# Stage verification\n\nVerified " << files.size() << " documents under " << generic(stage) << ": PASS " << pass << ", FAIL " << failed << ", warnings " << warningCount << ".\n\n";
    md << "| Path | Kind | Result | Note |\n|---|---|---|---|\n";
    for (const auto& r : results)
        md << "| " << r.at("relative").get<std::string>() << " | " << r.at("kind").get<std::string>() << " | " << r.at("result").get<std::string>() << " | "
           << (r.contains("error") ? r.at("error").get<std::string>() : r.at("warnings").empty() ? std::string{} : std::to_string(r.at("warnings").size()) + " warning(s)") << " |\n";
    if (warningCount) {
        md << "\n## Warnings\n\n";
        for (const auto& r : results)
            if (r.contains("warnings"))
                for (const auto& w : r.at("warnings")) md << "- " << r.at("relative").get<std::string>() << ": " << w.get<std::string>() << "\n";
    }
    auto mdPath = out;
    mdPath.replace_extension(L".md");
    write_text(mdPath, md.str());
    sweep_locks(out.parent_path(), false);
    std::cout << "Verified " << files.size() << ": PASS " << pass << ", FAIL " << failed << ", warnings " << warningCount << "\n";
    return failed ? 1 : 0;
}

using Arguments = std::map<std::string, std::vector<std::string>>;
Arguments parse_args(int argc, wchar_t** argv) {
    Arguments options;
    std::string current;
    for (int i = 1; i < argc; ++i) {
        const auto token = utf8(argv[i]);
        if (token.rfind("--", 0) == 0) {
            current = token;
            options[current];
        } else if (!current.empty()) options[current].push_back(token);
        else throw std::runtime_error("Unexpected argument " + token);
    }
    return options;
}
fs::path single(const Arguments& o, const std::string& key) {
    auto it = o.find(key);
    if (it == o.end() || it->second.size() != 1) throw std::runtime_error("Option " + key + " needs exactly one value");
    return fs::absolute(from_utf8(it->second.front()));
}
bool has(const Arguments& o, const std::string& key) { return o.contains(key); }
void read_source_libraries(const Arguments& o, Options& opts) {
    if (!has(o, "--source-library")) return;
    for (const auto& folder : o.at("--source-library")) opts.sourceLibraries.push_back(fs::absolute(from_utf8(folder)));
}

} // namespace

int wmain(int argc, wchar_t** argv) try {
    const auto o = parse_args(argc, argv);
    if (has(o, "--inventory")) {
        if (o.at("--inventory").empty()) throw std::runtime_error("--inventory needs at least one folder");
        std::vector<std::string> more;
        if (has(o, "--extra-root")) more = o.at("--extra-root");
        return run_inventory(o.at("--inventory"), single(o, "--out"), !has(o, "--no-extra-scan"), has(o, "--inspect-outside"), more);
    }
    if (has(o, "--convert")) {
        Options opts;
        opts.library = single(o, "--library");
        opts.archive = single(o, "--archive");
        opts.inPlace = has(o, "--in-place");
        read_source_libraries(o, opts);
        if (has(o, "--relative") && !o.at("--relative").empty()) opts.relative = o.at("--relative").front();
        EngineAssets::set_root(opts.library);
        const auto report = convert_file(single(o, "--convert"), single(o, "--out"), opts);
        std::cout << report.dump(2) << "\n";
        const auto result = report.at("result").get<std::string>();
        return result == "PASS" || result == "already key card" ? 0 : 1;
    }
    if (has(o, "--convert-all")) {
        Options opts;
        opts.library = single(o, "--library");
        opts.archive = single(o, "--archive");
        read_source_libraries(o, opts);
        EngineAssets::set_root(opts.library);
        return run_convert_all(single(o, "--convert-all"), opts, single(o, "--stage"));
    }
    if (has(o, "--verify")) {
        std::optional<fs::path> library;
        if (has(o, "--library")) library = single(o, "--library");
        const auto report = verify_document(single(o, "--verify"), library);
        std::cout << report.dump(2) << "\n";
        return report.at("result") == "PASS" ? 0 : 1;
    }
    if (has(o, "--verify-all")) return run_verify_all(single(o, "--verify-all"), single(o, "--library"), single(o, "--out"));
    std::cerr << "Usage:\n"
                 "  --inventory <folder>... --out <inventory.json> [--no-extra-scan] [--extra-root <folder>...] [--inspect-outside]\n"
                 "  --convert <in> --library <dir> --archive <dir> --out <out> [--relative <name>] [--in-place] [--source-library <dir>...]\n"
                 "  --convert-all <inventory.json> --library <dir> --archive <dir> --stage <dir> [--source-library <dir>...]\n"
                 "  --verify <converted> [--library <dir>]   (library is required for scenes; objects and groups use the assets folder beside the file)\n"
                 "  --verify-all <stage dir> --library <dir> --out <verify.json>\n"
                 "  <in> is a .starscene, .starobject or .stargroup file.\n";
    return 2;
} catch (const Refused& e) {
    std::cerr << "REFUSED: " << e.what() << "\n";
    return 2;
} catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 2;
}
