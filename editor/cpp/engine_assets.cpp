#include "engine_assets.hpp"
#include "platform.hpp"
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
namespace starlight::editor {
struct AssetSearchRoot {
    fs::path path;
    int tier = 0;
};
struct EngineAssetsState {
    std::optional<fs::path> primary;
    std::vector<AssetSearchRoot> search;
    std::vector<std::string> notes;
    std::optional<fs::path> registryFile;
    bool discovered = false;
};
namespace {
constexpr char kMagic[4] = {'S', 'L', 'A', 'S'};
constexpr uint32_t kFileVersion = 1;
constexpr size_t kHeaderBytes = 4 + 4 + 4 + 8;
constexpr size_t kMemoryBudget = size_t(1) << 30; // The memory copy is a cache of what the library folder holds; the oldest entries leave first.
struct Roots {
    std::mutex mutex;
    std::unordered_set<std::string> persisted;
    EngineAssetsState state;
};
Roots& roots() {
    static Roots value;
    return value;
}
struct Memory {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<const std::string>> items;
    std::deque<std::string> order;
    size_t bytes = 0;
    void add(const std::string& id, std::shared_ptr<const std::string> payload) {
        std::lock_guard lock(mutex);
        if (items.contains(id)) return;
        bytes += payload->size();
        items.emplace(id, std::move(payload));
        order.push_back(id);
        while (bytes > kMemoryBudget && order.size() > 1) {
            auto oldest = order.front();
            order.pop_front();
            if (auto found = items.find(oldest); found != items.end()) { bytes -= found->second->size(); items.erase(found); }
        }
    }
    std::shared_ptr<const std::string> find(const std::string& id) {
        std::lock_guard lock(mutex);
        auto found = items.find(id);
        return found == items.end() ? nullptr : found->second;
    }
};
Memory& memory() {
    static Memory store;
    return store;
}
const char* prefix(AssetKind kind) {
    switch (kind) {
    case AssetKind::Sculpt: return "sculpt-";
    case AssetKind::Terrain: return "terrain-";
    case AssetKind::Model: return "model-";
    case AssetKind::Texture: return "texture-";
    case AssetKind::Blob: return "blob-";
    }
    throw AssetMissing("Unknown engine asset kind");
}
std::string identity(AssetKind kind, std::string_view payload) {
    std::string keyed;
    keyed.reserve(payload.size() + 4);
    const uint32_t k = uint32_t(kind);
    keyed.append(reinterpret_cast<const char*>(&k), 4);
    keyed.append(payload);
    auto hex = hash_bytes(keyed);
    std::transform(hex.begin(), hex.end(), hex.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return prefix(kind) + hex;
}
template <class T> void put_value(std::string& out, T value) { out.append(reinterpret_cast<const char*>(&value), sizeof(T)); }
template <class T> void put_array(std::string& out, const std::vector<T>& values) { out.append(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(T)); }
struct Reader {
    std::string_view data;
    size_t at = 0;
    template <class T> T value() {
        if (data.size() - at < sizeof(T)) throw AssetMissing("Engine asset is truncated");
        T result;
        std::memcpy(&result, data.data() + at, sizeof(T));
        at += sizeof(T);
        return result;
    }
    template <class T> std::vector<T> array(uint64_t count) {
        if (count > (data.size() - at) / sizeof(T)) throw AssetMissing("Engine asset is truncated");
        std::vector<T> result(count);
        std::memcpy(result.data(), data.data() + at, count * sizeof(T));
        at += count * sizeof(T);
        return result;
    }
};
std::string file_name(const std::string& id) { return id + ".slasset"; }
AssetKind kind_of(const std::string& id) {
    if (id.starts_with("sculpt-")) return AssetKind::Sculpt;
    if (id.starts_with("terrain-")) return AssetKind::Terrain;
    if (id.starts_with("model-")) return AssetKind::Model;
    if (id.starts_with("texture-")) return AssetKind::Texture;
    if (id.starts_with("blob-")) return AssetKind::Blob;
    throw AssetMissing("Scene references an unknown engine asset id: " + id);
}
std::shared_ptr<const std::string> read_verified(AssetKind kind, const std::string& id, const fs::path& path, std::string* whole = nullptr) {
    auto file = read_file(path);
    if (file.size() < kHeaderBytes || std::memcmp(file.data(), kMagic, 4)) throw AssetMissing("Engine asset " + id + " is not a Starlight asset file");
    Reader header{file, 4};
    if (header.value<uint32_t>() != kFileVersion) throw AssetMissing("Engine asset " + id + " has an unsupported file version");
    if (header.value<uint32_t>() != uint32_t(kind)) throw AssetMissing("Engine asset " + id + " is the wrong kind of asset");
    const auto bytes = header.value<uint64_t>();
    if (bytes != file.size() - kHeaderBytes) throw AssetMissing("Engine asset " + id + " is truncated");
    auto payload = std::make_shared<const std::string>(file.substr(kHeaderBytes));
    if (identity(kind, *payload) != id) throw AssetMissing("Engine asset " + id + " is damaged (its contents no longer match its identity)");
    if (whole) *whole = std::move(file);
    return payload;
}
void forget_persisted() {
    std::lock_guard lock(roots().mutex);
    roots().persisted.clear();
}
fs::path normal_folder(fs::path folder) {
    std::error_code error;
    auto absolute = fs::absolute(std::move(folder), error).lexically_normal();
    if (!absolute.has_filename() && absolute.has_parent_path() && absolute != absolute.root_path()) absolute = absolute.parent_path();
    return absolute;
}
std::wstring folded(const fs::path& path) {
    auto text = normal_folder(path).wstring();
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return c == L'/' ? L'\\' : wchar_t(std::towlower(c)); });
    return text;
}
bool same_folder(const fs::path& a, const fs::path& b) { return folded(a) == folded(b); }
std::string describe_path(const fs::path& path) { return utf8(path.wstring()); }
std::string compute_marker_problem(const fs::path& folder) {
    std::error_code error;
    if (!fs::is_directory(folder, error)) return "the folder does not exist (is the drive or share connected?)";
    const auto marker = folder / L"library.json";
    if (!fs::is_regular_file(marker, error)) return "it has no library.json marker, so it is not an initialized asset library";
    json document;
    try {
        document = json::parse(read_file(marker));
    } catch (const std::exception&) {
        return "library.json cannot be read as JSON";
    }
    if (!document.is_object() || !document.contains("format") || !document.at("format").is_string() || document.at("format").get<std::string>() != EngineAssets::MarkerFormat)
        return std::string("library.json is not a Starlight asset library marker (its format must be \"") + EngineAssets::MarkerFormat + "\")";
    if (!document.contains("version") || !document.at("version").is_number_integer()) return "library.json has no integer version";
    const auto version = document.at("version").get<long long>();
    if (version != EngineAssets::MarkerVersion)
        return "library.json is version " + std::to_string(version) + " but this build supports version " + std::to_string(EngineAssets::MarkerVersion) + " only";
    return {};
}
struct MarkerCacheEntry {
    fs::file_time_type stamp;
    uintmax_t size = 0;
    std::string problem;
};
std::string cached_marker_problem(const fs::path& folder) {
    static std::mutex mutex;
    static std::unordered_map<std::wstring, MarkerCacheEntry> cache;
    std::error_code error;
    const auto marker = folder / L"library.json";
    const auto stamp = fs::last_write_time(marker, error);
    if (error) return compute_marker_problem(folder);
    const auto size = fs::file_size(marker, error);
    if (error) return compute_marker_problem(folder);
    const auto key = folded(folder);
    {
        std::lock_guard lock(mutex);
        if (auto found = cache.find(key); found != cache.end() && found->second.stamp == stamp && found->second.size == size) return found->second.problem;
    }
    auto problem = compute_marker_problem(folder);
    std::lock_guard lock(mutex);
    cache[key] = {stamp, size, problem};
    return problem;
}
fs::path default_registry_file() { return application_data() / L"asset-roots.json"; }
fs::path active_registry_file() {
    {
        std::lock_guard lock(roots().mutex);
        if (roots().state.registryFile) return *roots().state.registryFile;
    }
    return default_registry_file();
}
struct RegistryRead {
    bool present = false;
    std::vector<AssetRootEntry> entries;
    std::string error;
};
RegistryRead parse_registry(const fs::path& file) {
    RegistryRead result;
    std::error_code error;
    if (!fs::is_regular_file(file, error)) return result;
    result.present = true;
    json document;
    try {
        document = json::parse(read_file(file));
    } catch (const std::exception& e) {
        result.error = "The asset root registry " + describe_path(file) + " cannot be read: " + e.what();
        return result;
    }
    auto fail = [&](const std::string& why) {
        result.entries.clear();
        result.error = "The asset root registry " + describe_path(file) + " is not usable: " + why;
        return result;
    };
    if (!document.is_object() || !document.contains("format") || !document.at("format").is_string() || document.at("format").get<std::string>() != EngineAssets::RegistryFormat)
        return fail(std::string("its format must be \"") + EngineAssets::RegistryFormat + "\"");
    if (!document.contains("version") || !document.at("version").is_number_integer()) return fail("it has no integer version");
    const auto version = document.at("version").get<long long>();
    if (version != EngineAssets::RegistryVersion)
        return fail("it is version " + std::to_string(version) + " but this build supports version " + std::to_string(EngineAssets::RegistryVersion) + " only");
    if (!document.contains("roots") || !document.at("roots").is_array()) return fail("it has no roots list");
    bool writable = false;
    for (const auto& item : document.at("roots")) {
        if (!item.is_object() || !item.contains("path") || !item.at("path").is_string() || !item.contains("role") || !item.at("role").is_string()) return fail("an entry needs a path and a role");
        AssetRootEntry entry;
        entry.Path = fs::path(wide(item.at("path").get<std::string>()));
        if (entry.Path.empty() || !entry.Path.is_absolute()) return fail("the path \"" + item.at("path").get<std::string>() + "\" is not absolute");
        entry.Path = normal_folder(entry.Path);
        const auto role = item.at("role").get<std::string>();
        if (role != "writable" && role != "read-only") return fail("the role \"" + role + "\" must be \"writable\" or \"read-only\"");
        entry.Writable = role == "writable";
        if (entry.Writable) {
            if (writable) return fail("more than one writable root is registered");
            writable = true;
        }
        if (item.contains("label")) {
            if (!item.at("label").is_string()) return fail("a label must be text");
            entry.Label = item.at("label").get<std::string>();
        }
        for (const auto& other : result.entries)
            if (same_folder(other.Path, entry.Path)) return fail("the root " + describe_path(entry.Path) + " is registered twice");
        result.entries.push_back(std::move(entry));
    }
    return result;
}
RegistryRead read_registry(const fs::path& file) {
    struct Cache {
        std::mutex mutex;
        std::wstring key;
        bool present = false;
        fs::file_time_type stamp;
        uintmax_t size = 0;
        RegistryRead value;
        bool loaded = false;
    };
    static Cache cache;
    std::error_code error;
    const bool present = fs::is_regular_file(file, error);
    fs::file_time_type stamp{};
    uintmax_t size = 0;
    if (present) {
        stamp = fs::last_write_time(file, error);
        size = fs::file_size(file, error);
    }
    const auto key = folded(file);
    {
        std::lock_guard lock(cache.mutex);
        if (cache.loaded && cache.key == key && cache.present == present && cache.stamp == stamp && cache.size == size) return cache.value;
    }
    auto value = parse_registry(file);
    std::lock_guard lock(cache.mutex);
    cache.loaded = true;
    cache.key = key;
    cache.present = present;
    cache.stamp = stamp;
    cache.size = size;
    cache.value = value;
    return value;
}
std::string registry_text(const std::vector<AssetRootEntry>& entries) {
    json list = json::array();
    for (const auto& entry : entries) {
        json item = {{"path", describe_path(entry.Path)}, {"role", entry.Writable ? "writable" : "read-only"}};
        if (!entry.Label.empty()) item["label"] = entry.Label;
        list.push_back(std::move(item));
    }
    return json{{"format", EngineAssets::RegistryFormat}, {"version", EngineAssets::RegistryVersion}, {"roots", list}}.dump(2);
}
void save_registry(const std::vector<AssetRootEntry>& entries) {
    const auto file = active_registry_file();
    const auto text = registry_text(entries);
    atomic_write(file, text);
    auto back = parse_registry(file);
    if (!back.error.empty() || back.entries.size() != entries.size() || read_file(file) != text)
        throw AssetRootError("The asset root registry " + describe_path(file) + " could not be verified after writing");
}
RegistryRead checked_registry() {
    auto read = read_registry(active_registry_file());
    if (!read.error.empty()) throw AssetRootError(read.error);
    return read;
}
fs::path executable_folder() {
    std::wstring buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}
std::optional<fs::path> environment_override() {
    wchar_t configured[32768];
    auto length = GetEnvironmentVariableW(L"STARLIGHT_ASSET_LIBRARY", configured, 32768);
    if (!length || length >= 32768) return std::nullopt;
    fs::path p(configured);
    if (!p.is_absolute()) throw AssetRootError("STARLIGHT_ASSET_LIBRARY must be an absolute folder");
    return normal_folder(p);
}
struct ResolvedRoot {
    fs::path path;
    std::string origin;
    bool writable = false;
    bool primary = false;
    bool durable = false;
    bool markerRequired = false;
    std::string label;
    bool present = false;
    bool usable = false;
    std::string problem;
};
struct Resolution {
    std::vector<ResolvedRoot> items;
    std::vector<std::string> notes;
    bool isolated = false;
};
void judge(ResolvedRoot& item) {
    std::error_code error;
    item.present = fs::is_directory(item.path, error);
    item.problem.clear();
    if (!item.present) {
        if (item.markerRequired) item.problem = "the folder does not exist (is the drive or share connected?)";
        item.usable = !item.markerRequired && item.primary;
        return;
    }
    const bool marked = fs::is_regular_file(item.path / L"library.json", error);
    if (item.markerRequired || marked) item.problem = cached_marker_problem(item.path);
    item.usable = item.problem.empty();
}
std::optional<fs::path> shipped_library_near(const fs::path& folder, std::vector<std::string>* notes) {
    std::error_code error;
    auto at = normal_folder(folder);
    for (int depth = 0; depth < 8 && !at.empty(); ++depth) {
        if (fs::is_regular_file(at / L"library" / L"library.json", error)) {
            const auto library = at / L"library";
            const auto problem = compute_marker_problem(library);
            if (problem.empty()) return library;
            if (notes) notes->push_back("The library found at " + describe_path(library) + " is ignored: " + problem);
            return std::nullopt;
        }
        auto parent = at.parent_path();
        if (parent == at) break;
        at = parent;
    }
    return std::nullopt;
}
void discover_executable_once() {
    {
        std::lock_guard lock(roots().mutex);
        if (roots().state.discovered || roots().state.primary) return;
        roots().state.discovered = true;
    }
    const auto exe = executable_folder();
    if (!exe.empty()) EngineAssets::discover_near(exe);
}
Resolution resolve(bool throwOnRegistryError) {
    discover_executable_once();
    EngineAssetsState snapshot;
    {
        std::lock_guard lock(roots().mutex);
        snapshot = roots().state;
    }
    Resolution result;
    result.notes = snapshot.notes;
    auto push = [&](fs::path path, std::string origin, bool writable, bool primary, bool durable, bool marker, std::string label) {
        for (const auto& existing : result.items)
            if (same_folder(existing.path, path)) return;
        ResolvedRoot item;
        item.path = std::move(path);
        item.origin = std::move(origin);
        item.writable = writable;
        item.primary = primary;
        item.durable = durable;
        item.markerRequired = marker;
        item.label = std::move(label);
        judge(item);
        result.items.push_back(std::move(item));
    };
    std::vector<AssetRootEntry> registry;
    if (snapshot.primary) {
        result.isolated = true;
        push(*snapshot.primary, "explicit", true, true, true, false, {});
    } else if (auto env = environment_override()) {
        result.isolated = true;
        result.notes.push_back("STARLIGHT_ASSET_LIBRARY is set: that folder is the only primary library and the registered roots are not used.");
        push(*env, "environment", true, true, true, false, {});
    } else {
        auto read = read_registry(snapshot.registryFile ? *snapshot.registryFile : default_registry_file());
        if (!read.error.empty()) {
            if (throwOnRegistryError) throw AssetRootError(read.error);
            result.notes.push_back(read.error);
        }
        registry = read.entries;
        const AssetRootEntry* writable = nullptr;
        for (const auto& entry : registry)
            if (entry.Writable) { writable = &entry; break; }
        if (writable) push(writable->Path, "registry", true, true, true, true, writable->Label);
        else push(application_data() / L"AssetLibrary", "default local library", true, true, true, false, {});
    }
    for (const auto& extra : snapshot.search)
        if (extra.tier == 0) push(extra.path, "search root", false, false, false, false, {});
    if (!result.isolated)
        for (const auto& entry : registry) push(entry.Path, "registry", false, false, true, true, entry.Label);
    for (const auto& extra : snapshot.search)
        if (extra.tier == 2) push(extra.path, "shipped library", false, false, false, true, {});
    for (const auto& extra : snapshot.search)
        if (extra.tier == 3) push(extra.path, "bundle assets", false, false, false, false, {});
    return result;
}
std::string problem_text(const Resolution& resolution) {
    std::string text;
    for (const auto& item : resolution.items)
        if (!item.problem.empty()) text += "\n  " + describe_path(item.path) + " (" + item.origin + (item.label.empty() ? "" : ", " + item.label) + "): " + item.problem;
    for (const auto& note : resolution.notes) text += "\n  " + note;
    return text;
}
std::string missing_text(const std::string& id) {
    std::string searched;
    std::string problems;
    try {
        const auto resolution = resolve(false);
        for (const auto& item : resolution.items)
            if (item.usable && item.present) searched += (searched.empty() ? "" : "; ") + describe_path(item.path);
        problems = problem_text(resolution);
    } catch (const std::exception& e) {
        problems = std::string("\n  ") + e.what();
    }
    return "Engine asset " + id + " is missing from the asset library (searched: " + (searched.empty() ? "no usable asset folder" : searched) + ")" +
           (problems.empty() ? "" : ". Unusable asset roots:" + problems);
}
} // namespace
fs::path EngineAssets::root() {
    const auto resolution = resolve(true);
    for (const auto& item : resolution.items)
        if (item.primary) {
            if (!item.usable) throw AssetRootError("The writable asset root " + describe_path(item.path) + " is unusable: " + item.problem);
            return item.path;
        }
    throw AssetRootError("No primary asset root could be resolved");
}
void EngineAssets::set_root(fs::path folder) {
    std::lock_guard lock(roots().mutex);
    roots().state.primary = normal_folder(std::move(folder));
    roots().persisted.clear();
}
namespace {
void add_tiered(fs::path folder, int tier) {
    std::lock_guard lock(roots().mutex);
    folder = normal_folder(std::move(folder));
    auto& search = roots().state.search;
    if (std::find_if(search.begin(), search.end(), [&](const AssetSearchRoot& r) { return same_folder(r.path, folder); }) == search.end()) search.push_back({std::move(folder), tier});
}
}
void EngineAssets::add_search_root(fs::path folder) { add_tiered(std::move(folder), 0); }
void EngineAssets::discover_near(const fs::path& folder) {
    auto start = normal_folder(folder);
    add_tiered(start / L"assets", 3);
    std::vector<std::string> notes;
    if (auto library = shipped_library_near(start, &notes)) add_tiered(*library, 2);
    if (!notes.empty()) {
        std::lock_guard lock(roots().mutex);
        for (auto& note : notes)
            if (std::find(roots().state.notes.begin(), roots().state.notes.end(), note) == roots().state.notes.end()) roots().state.notes.push_back(std::move(note));
    }
}
EngineAssets::ScopedRoot::ScopedRoot(fs::path folder) {
    std::lock_guard lock(roots().mutex);
    previous_ = std::make_unique<EngineAssetsState>(roots().state);
    roots().state.primary = normal_folder(std::move(folder));
    roots().state.search.clear();
    roots().state.notes.clear();
    roots().persisted.clear();
}
EngineAssets::ScopedRoot::~ScopedRoot() {
    std::lock_guard lock(roots().mutex);
    roots().state = *previous_;
    roots().persisted.clear();
}
EngineAssets::ScopedRegistry::ScopedRegistry(fs::path registryFile) {
    std::lock_guard lock(roots().mutex);
    previous_ = std::make_unique<EngineAssetsState>(roots().state);
    EngineAssetsState fresh;
    fresh.registryFile = fs::absolute(std::move(registryFile)).lexically_normal();
    fresh.discovered = true;
    roots().state = std::move(fresh);
    roots().persisted.clear();
}
EngineAssets::ScopedRegistry::~ScopedRegistry() {
    std::lock_guard lock(roots().mutex);
    roots().state = *previous_;
    roots().persisted.clear();
}
fs::path EngineAssets::registry_file() { return active_registry_file(); }
fs::path EngineAssets::cache_folder() { return application_data() / L"AssetCache"; }
std::vector<AssetRootEntry> EngineAssets::registered_roots() { return checked_registry().entries; }
std::string EngineAssets::marker_problem(const fs::path& folder) { return compute_marker_problem(normal_folder(folder)); }
void EngineAssets::register_root(const fs::path& folder, bool writable, const std::string& label) {
    const auto path = normal_folder(folder);
    if (auto problem = compute_marker_problem(path); !problem.empty())
        throw AssetRootError("Cannot register " + describe_path(path) + ": " + problem + ". Use initialize_root (--asset-root-init) to create a new library there.");
    auto entries = checked_registry().entries;
    for (const auto& entry : entries)
        if (writable && entry.Writable && !same_folder(entry.Path, path))
            throw AssetRootError("Only one writable asset root can be registered; " + describe_path(entry.Path) + " already is");
    bool replaced = false;
    for (auto& entry : entries)
        if (same_folder(entry.Path, path)) {
            entry.Writable = writable;
            entry.Label = label;
            replaced = true;
        }
    if (!replaced) entries.push_back({path, writable, label});
    save_registry(entries);
    forget_persisted();
}
void EngineAssets::initialize_root(const fs::path& folder, bool writable, const std::string& label) {
    const auto path = normal_folder(folder);
    std::error_code error;
    if (fs::exists(path, error)) {
        if (!fs::is_directory(path, error)) throw AssetRootError(describe_path(path) + " exists and is not a folder");
        const bool marked = fs::exists(path / L"library.json", error);
        if (marked) {
            if (auto problem = compute_marker_problem(path); !problem.empty()) throw AssetRootError("Cannot initialize " + describe_path(path) + ": " + problem);
        } else if (!fs::is_empty(path, error)) {
            throw AssetRootError("Cannot initialize " + describe_path(path) + ": the folder is not empty and has no library.json marker, so it is not a new asset library");
        }
    }
    if (!fs::exists(path / L"library.json", error))
        atomic_write(path / L"library.json", json{{"format", MarkerFormat}, {"version", MarkerVersion}}.dump());
    if (auto problem = compute_marker_problem(path); !problem.empty()) throw AssetRootError("The marker written to " + describe_path(path) + " could not be verified: " + problem);
    register_root(path, writable, label);
}
bool EngineAssets::unregister_root(const fs::path& folder) {
    const auto path = normal_folder(folder);
    auto entries = checked_registry().entries;
    const auto before = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const AssetRootEntry& e) { return same_folder(e.Path, path); }), entries.end());
    if (entries.size() == before) return false;
    save_registry(entries);
    forget_persisted();
    return true;
}
std::vector<AssetRootStatus> EngineAssets::root_status() {
    std::vector<AssetRootStatus> list;
    Resolution resolution;
    try {
        resolution = resolve(false);
    } catch (const std::exception& e) {
        AssetRootStatus failed;
        failed.Origin = "resolution";
        failed.Problem = e.what();
        list.push_back(std::move(failed));
        return list;
    }
    for (const auto& item : resolution.items) {
        AssetRootStatus status;
        status.Path = item.path;
        status.Origin = item.origin;
        status.Writable = item.writable;
        status.Primary = item.primary;
        status.Label = item.label;
        status.Present = item.present;
        status.Usable = item.usable;
        status.Problem = item.problem;
        list.push_back(std::move(status));
    }
    for (const auto& note : resolution.notes) {
        AssetRootStatus status;
        status.Origin = "note";
        status.Problem = note;
        list.push_back(std::move(status));
    }
    return list;
}
std::string EngineAssets::describe_roots() {
    std::string text = "Asset roots in resolution order (registry file: " + describe_path(active_registry_file()) + "):";
    int number = 0;
    for (const auto& status : root_status()) {
        if (status.Origin == "note" || status.Origin == "resolution") {
            text += "\n  note: " + status.Problem;
            continue;
        }
        text += "\n  " + std::to_string(++number) + ". [" + (status.Primary ? "primary, writable" : status.Writable ? "writable" : "read-only") + "] " + describe_path(status.Path) + " (" + status.Origin +
                (status.Label.empty() ? "" : ", " + status.Label) + ") ";
        if (!status.Problem.empty()) text += "UNUSABLE: " + status.Problem;
        else if (!status.Present) text += status.Primary ? "folder not created yet (created by the first write)" : "folder does not exist (nothing is searched there)";
        else text += "ok";
    }
    return text;
}
std::string EngineAssets::root_problems() {
    try {
        return problem_text(resolve(false));
    } catch (const std::exception& e) {
        return std::string("\n  ") + e.what();
    }
}
std::vector<fs::path> EngineAssets::default_roots() {
    std::vector<fs::path> list;
    auto add = [&](const fs::path& path, bool markerRequired) {
        for (const auto& existing : list)
            if (same_folder(existing, path)) return;
        std::error_code error;
        if (!fs::is_directory(path, error)) return;
        if ((markerRequired || fs::is_regular_file(path / L"library.json", error)) && !cached_marker_problem(path).empty()) return;
        list.push_back(path);
    };
    if (auto env = environment_override()) {
        add(*env, false);
        return list;
    }
    auto read = read_registry(default_registry_file());
    if (!read.error.empty()) throw AssetRootError(read.error);
    const AssetRootEntry* writable = nullptr;
    for (const auto& entry : read.entries)
        if (entry.Writable) { writable = &entry; break; }
    if (writable) add(writable->Path, true);
    else add(application_data() / L"AssetLibrary", false);
    for (const auto& entry : read.entries) add(entry.Path, true);
    const auto exe = executable_folder();
    if (!exe.empty())
        if (auto shipped = shipped_library_near(exe, nullptr)) add(*shipped, true);
    return list;
}
bool EngineAssets::valid_id(AssetKind kind, const std::string& id) {
    const std::string head = prefix(kind);
    if (id.size() != head.size() + 64 || id.compare(0, head.size(), head)) return false;
    return std::all_of(id.begin() + head.size(), id.end(), [](unsigned char c) { return std::isdigit(c) || (c >= 'a' && c <= 'f'); });
}
fs::path EngineAssets::file_for(const std::string& id) { return root() / wide(file_name(id)); }
namespace {
struct Candidate {
    fs::path path;
    bool durable = false;
};
std::vector<Candidate> candidate_files(const std::string& id) {
    std::vector<Candidate> candidates;
    for (const auto& item : resolve(true).items)
        if (item.usable) candidates.push_back({item.path / wide(file_name(id)), item.durable});
    return candidates;
}
}
fs::path EngineAssets::locate(const std::string& id) {
    std::error_code error;
    for (const auto& candidate : candidate_files(id))
        if (fs::is_regular_file(candidate.path, error)) return candidate.path;
    return {};
}
bool EngineAssets::keep(const std::string& id) {
    {
        std::lock_guard lock(roots().mutex);
        if (roots().persisted.contains(id)) return true;
    }
    AssetKind kind;
    try { kind = kind_of(id); } catch (const AssetMissing&) { return false; }
    if (!valid_id(kind, id)) return false;
    const auto home = file_for(id);
    std::error_code error;
    for (const auto& candidate : candidate_files(id)) {
        const auto& path = candidate.path;
        if (!fs::is_regular_file(path, error)) continue;
        std::string whole;
        try { read_verified(kind, id, path, &whole); } catch (const AssetMissing&) { continue; }
        if (path != home && !candidate.durable) {
            atomic_write(home, whole);
            read_verified(kind, id, home);
        }
        std::lock_guard lock(roots().mutex);
        roots().persisted.insert(id);
        return true;
    }
    return false;
}
std::string EngineAssets::put(AssetKind kind, std::string_view payload) {
    auto id = identity(kind, payload);
    {
        std::lock_guard lock(roots().mutex);
        if (roots().persisted.contains(id)) { memory().add(id, std::make_shared<const std::string>(payload)); return id; }
    }
    bool valid = false;
    if (auto existing = locate(id); !existing.empty()) {
        try { read_verified(kind, id, existing); valid = true; } catch (const AssetMissing&) {}
    }
    if (!valid) {
        std::string file;
        file.reserve(kHeaderBytes + payload.size());
        file.append(kMagic, 4);
        put_value<uint32_t>(file, kFileVersion);
        put_value<uint32_t>(file, uint32_t(kind));
        put_value<uint64_t>(file, payload.size());
        file.append(payload);
        const auto home = file_for(id);
        atomic_write(home, file);
        read_verified(kind, id, home);
    }
    memory().add(id, std::make_shared<const std::string>(payload));
    std::lock_guard lock(roots().mutex);
    roots().persisted.insert(id);
    return id;
}
std::string EngineAssets::verified_file_bytes(const std::string& id) {
    auto kind = kind_of(id);
    auto path = locate(id);
    if (path.empty()) throw AssetMissing(missing_text(id));
    std::string whole;
    read_verified(kind, id, path, &whole);
    return whole;
}
void EngineAssets::verify_on_disk(const std::string& id) { (void)verified_file_bytes(id); }
void EngineAssets::drop_memory_cache() {
    auto& store = memory();
    std::lock_guard lock(store.mutex);
    store.items.clear();
    store.order.clear();
    store.bytes = 0;
}
void EngineAssets::bundle_into(const std::set<std::string>& ids, const fs::path& folder) {
    for (const auto& id : ids) {
        auto target = folder / wide(file_name(id));
        std::error_code error;
        if (fs::is_regular_file(target, error)) {
            try { read_verified(kind_of(id), id, target); continue; } catch (const AssetMissing&) {}
        }
        atomic_write(target, verified_file_bytes(id));
    }
}
std::set<std::string> EngineAssets::referenced_ids(const json& document) {
    std::set<std::string> ids;
    auto take = [&](const json& value) {
        if (value.is_object() && value.contains("Asset") && value.at("Asset").is_string()) ids.insert(value.at("Asset").get<std::string>());
    };
    auto character = [&](const json& value) {
        if (!value.is_object()) return;
        if (value.contains("Sculpt")) take(value.at("Sculpt"));
        if (value.contains("Asset") && value.at("Asset").is_string()) {
            const auto id = value.at("Asset").get<std::string>();
            if (valid_id(AssetKind::Model, id)) ids.insert(id);
        }
    };
    if (!document.is_object()) return ids;
    if (document.contains("Characters") && document.at("Characters").is_array()) for (const auto& c : document.at("Characters")) character(c);
    if (document.contains("Objects") && document.at("Objects").is_array()) for (const auto& c : document.at("Objects")) character(c);
    if (document.contains("Object")) character(document.at("Object"));
    if (document.contains("Terrain")) take(document.at("Terrain"));
    for (const auto& id : cast_asset_ids(document)) ids.insert(id);
    return ids;
}
bool EngineAssets::exists(AssetKind kind, const std::string& id) {
    return valid_id(kind, id) && (memory().find(id) || !locate(id).empty());
}
std::shared_ptr<const std::string> EngineAssets::get(AssetKind kind, const std::string& id) {
    if (!valid_id(kind, id)) throw AssetMissing("Scene references an invalid engine asset id: " + id);
    if (auto cached = memory().find(id)) return cached;
    auto path = locate(id);
    if (path.empty()) throw AssetMissing(missing_text(id));
    auto payload = read_verified(kind, id, path);
    memory().add(id, payload);
    return payload;
}
std::string store_sculpt(const SculptMesh& mesh) {
    std::string payload;
    payload.reserve(24 + mesh.Positions.size() * 8 + mesh.Indices.size() * 4 + mesh.MaterialCoordinates.size() * 8);
    put_value<uint64_t>(payload, mesh.Positions.size());
    put_value<uint64_t>(payload, mesh.Indices.size());
    put_value<uint64_t>(payload, mesh.MaterialCoordinates.size());
    put_array(payload, mesh.Positions);
    std::vector<int32_t> indices(mesh.Indices.begin(), mesh.Indices.end());
    put_array(payload, indices);
    put_array(payload, mesh.MaterialCoordinates);
    return EngineAssets::put(AssetKind::Sculpt, payload);
}
SculptMesh load_sculpt(const std::string& id) {
    auto payload = EngineAssets::get(AssetKind::Sculpt, id);
    Reader in{*payload};
    const auto positions = in.value<uint64_t>(), indices = in.value<uint64_t>(), coordinates = in.value<uint64_t>();
    SculptMesh mesh;
    mesh.Positions = in.array<double>(positions);
    auto raw = in.array<int32_t>(indices);
    mesh.Indices.assign(raw.begin(), raw.end());
    mesh.MaterialCoordinates = in.array<double>(coordinates);
    if (in.at != payload->size()) throw AssetMissing("Engine asset " + id + " has trailing data");
    mesh.validate();
    uint64_t stable = 0;
    for (size_t i = 7; i < 22 && i < id.size(); ++i) stable = (stable << 4) | uint64_t(std::isdigit(static_cast<unsigned char>(id[i])) ? id[i] - '0' : id[i] - 'a' + 10);
    mesh.Revision = stable | (1ull << 62);
    return mesh;
}
std::string store_terrain_geometry(const TerrainData& terrain) {
    terrain.validate();
    std::string payload;
    put_value<uint32_t>(payload, uint32_t(TerrainData::N));
    put_array(payload, terrain.Heights);
    std::vector<int32_t> paint(terrain.Paint.begin(), terrain.Paint.end());
    put_array(payload, paint);
    put_value<uint32_t>(payload, uint32_t(terrain.Volumes.size()));
    for (const auto& v : terrain.Volumes) {
        put_value<double>(payload, v.X);
        put_value<double>(payload, v.Y);
        put_value<double>(payload, v.Z);
        put_value<double>(payload, v.Radius);
        put_value<uint32_t>(payload, v.Subtract ? 1u : 0u);
    }
    return EngineAssets::put(AssetKind::Terrain, payload);
}
void load_terrain_geometry(const std::string& id, TerrainData& terrain) {
    auto payload = EngineAssets::get(AssetKind::Terrain, id);
    Reader in{*payload};
    if (in.value<uint32_t>() != uint32_t(TerrainData::N)) throw AssetMissing("Terrain asset " + id + " has a different grid size than this engine build");
    terrain.Heights = in.array<double>(size_t(TerrainData::N) * TerrainData::N);
    auto paint = in.array<int32_t>(size_t(TerrainData::N) * TerrainData::N);
    terrain.Paint.assign(paint.begin(), paint.end());
    const auto volumes = in.value<uint32_t>();
    if (volumes > 128) throw AssetMissing("Terrain asset " + id + " has too many cut operations");
    terrain.Volumes.clear();
    for (uint32_t i = 0; i < volumes; ++i) {
        TerrainVolume v;
        v.X = in.value<double>();
        v.Y = in.value<double>();
        v.Z = in.value<double>();
        v.Radius = in.value<double>();
        v.Subtract = in.value<uint32_t>() != 0;
        terrain.Volumes.push_back(v);
    }
    if (in.at != payload->size()) throw AssetMissing("Terrain asset " + id + " has trailing data");
}
std::set<std::string> scene_model_ids(const SceneData& scene) {
    std::set<std::string> ids;
    for (const auto& c : scene.Characters) if (!c.Shape && !c.Sculpt && !c.Asset.empty()) ids.insert(c.Asset);
    return ids;
}
bool model_available(const std::string& id) {
    try { (void)EngineAssets::get(AssetKind::Model, id); return true; } catch (const AssetMissing&) { return false; }
}
std::set<std::string> scene_asset_ids(const SceneData& scene) {
    std::set<std::string> ids;
    for (const auto& c : scene.Characters) {
        if (c.Sculpt) ids.insert(store_sculpt(*c.Sculpt));
        else if (!c.Shape && !c.Asset.empty()) ids.insert(c.Asset);
    }
    if (scene.Terrain) ids.insert(store_terrain_geometry(*scene.Terrain));
    for (const auto& id : cast_asset_ids(scene)) ids.insert(id);
    return ids;
}
} // namespace starlight::editor
