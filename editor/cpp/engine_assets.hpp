#pragma once
#include "scene.hpp"
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>
namespace starlight::editor {
namespace fs = std::filesystem;
// The shared, engine-managed asset library. Scene files are key cards: configuration and asset references only. Authored geometry (sculpted
// meshes, terrain height/paint grids and terrain cut operations) lives here as content-addressed assets. An asset id is the SHA-256 of its kind and
// payload, so the same geometry is stored once for every scene and object that uses it, and a changed or damaged file can never be mistaken for the
// original. The editor, the runtime, importers and exporters all go through this one library.
enum class AssetKind : uint32_t { Sculpt = 1, Terrain = 2, Model = 3, Texture = 4, Blob = 5 };
// Thrown when a scene still embeds geometry or predates key cards. Active engine paths never read that format; the separate migration tool converts it.
struct ConversionRequired : std::runtime_error {
    using std::runtime_error::runtime_error;
};
// Thrown when a key card names an asset the library does not hold (or holds damaged). Loading must report this, never continue without the geometry.
struct AssetMissing : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct AssetRootError : AssetMissing {
    using AssetMissing::AssetMissing;
};
struct AssetRootEntry {
    fs::path Path;
    bool Writable = false;
    std::string Label;
};
struct AssetRootStatus {
    fs::path Path;
    std::string Origin;
    bool Writable = false;
    bool Primary = false;
    std::string Label;
    bool Present = false;
    bool Usable = false;
    std::string Problem;
};
struct EngineAssetsState;
class EngineAssets {
  public:
    static constexpr int KeyCardSchema = 16;
    static constexpr const char* MarkerFormat = "starlight-asset-library";
    static constexpr int MarkerVersion = 1;
    static constexpr const char* RegistryFormat = "starlight-asset-roots";
    static constexpr int RegistryVersion = 1;
    // Primary library folder (all writes go here). Resolution rules: Starlight Documents/Current/docs/architecture/KEY-CARD-ARCHITECTURE.md.
    static fs::path root();
    static void set_root(fs::path);
    // Extra folders searched (read-only) after the primary one, for example an exported game's bundled "assets" folder.
    static void add_search_root(fs::path);
    // Registers the read-only libraries that travel with a scene location: "<folder>/assets" (a portable bundle) and the shipped engine library,
    // found by walking up from the folder to the first parent that holds "library/library.json".
    static void discover_near(const fs::path& folder);
    static fs::path registry_file();
    static std::vector<AssetRootEntry> registered_roots();
    static void register_root(const fs::path& folder, bool writable, const std::string& label = {});
    static void initialize_root(const fs::path& folder, bool writable, const std::string& label = {});
    static bool unregister_root(const fs::path& folder);
    static std::string marker_problem(const fs::path& folder);
    static std::vector<AssetRootStatus> root_status();
    static std::string describe_roots();
    static std::string root_problems();
    static std::vector<fs::path> default_roots();
    static fs::path cache_folder();
    // Redirects the library for the lifetime of the object (checks and tools); restores the previous configuration on destruction.
    class ScopedRoot {
      public:
        explicit ScopedRoot(fs::path folder);
        ~ScopedRoot();
        ScopedRoot(const ScopedRoot&) = delete;
        ScopedRoot& operator=(const ScopedRoot&) = delete;
      private:
        std::unique_ptr<EngineAssetsState> previous_;
    };
    class ScopedRegistry {
      public:
        explicit ScopedRegistry(fs::path registryFile);
        ~ScopedRegistry();
        ScopedRegistry(const ScopedRegistry&) = delete;
        ScopedRegistry& operator=(const ScopedRegistry&) = delete;
      private:
        std::unique_ptr<EngineAssetsState> previous_;
    };
    // Stores the payload durably (verifying any file already present under that id) and only then makes it available from memory; a failed write throws.
    static std::string put(AssetKind kind, std::string_view payload);
    // Returns the verified payload. Throws AssetMissing with a plain-language message when absent or damaged.
    static std::shared_ptr<const std::string> get(AssetKind kind, const std::string& id);
    static bool exists(AssetKind kind, const std::string& id);
    // Path of the stored file for an id in the primary root (used by exporters and tools to copy exactly the files a scene needs).
    static fs::path file_for(const std::string& id);
    // The file for an id wherever the library finds it (primary root first, then search roots); empty when not found.
    static fs::path locate(const std::string& id);
    static bool valid_id(AssetKind kind, const std::string& id);
    // Reads the stored file for the id from disk (never from the memory cache), verifies its identity, and returns the whole file. Throws AssetMissing.
    static std::string verified_file_bytes(const std::string& id);
    static void verify_on_disk(const std::string& id);
    static void drop_memory_cache();
    static bool keep(const std::string& id);
    // Copies verified library files into a bundle folder (an exported scene's "assets", or the folder beside a reusable object/group).
    static void bundle_into(const std::set<std::string>& ids, const fs::path& folder);
    // The engine asset ids named by scene, object or group JSON text (no geometry is loaded).
    static std::set<std::string> referenced_ids(const json& document);
};
// Geometry <-> library assets. store_* returns the asset id after writing the payload to the library.
std::string store_sculpt(const SculptMesh&);
SculptMesh load_sculpt(const std::string& id);
std::string store_terrain_geometry(const TerrainData&);
// Fills Heights, Paint and Volumes of the terrain from the asset.
void load_terrain_geometry(const std::string& id, TerrainData&);
// Every library asset a scene refers to: what an exporter must bundle and what loading must have before reveal.
std::set<std::string> scene_asset_ids(const SceneData&);
std::set<std::string> scene_model_ids(const SceneData&);
bool model_available(const std::string& id);
} // namespace starlight::editor
