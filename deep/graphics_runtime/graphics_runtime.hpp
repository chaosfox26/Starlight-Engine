#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace starlight::gfxrt {
namespace fs = std::filesystem;

struct GrError : std::runtime_error { using std::runtime_error::runtime_error; };
struct InjectedCrash { std::string step; };
using FaultHook = std::function<void(std::string_view)>;

struct Result {
    bool ok = true;
    std::string message;
    static Result fail(std::string m) { return {false, std::move(m)}; }
    static Result good(std::string m = {}) { return {true, std::move(m)}; }
};

std::string utf8(const fs::path&);
fs::path from_utf8(std::string_view);
std::string sha256_bytes(std::string_view data);
std::string sha256_file(const fs::path&);
std::uint32_t crc32_bytes(std::string_view data);
std::string read_text(const fs::path&);
bool safe_relative_path(std::string_view path, std::string& why);
std::string fold_name(std::string_view utf8Name);
bool is_reparse_or_link(const fs::path&);
void atomic_replace_file(const fs::path& target, std::string_view data, const FaultHook& fault, std::string_view stepPrefix, bool keepBackup);

struct Version {
    std::vector<unsigned> parts;
    bool operator==(const Version& o) const { return compare(o) == 0; }
    int compare(const Version& o) const;
    std::string str() const;
};
std::optional<Version> parse_version(std::string_view);

enum class Channel { Stable, Experimental };
enum class Category { Upscaling, FrameGeneration, LatencyReduction };
const char* channel_name(Channel);
std::optional<Channel> parse_channel(std::string_view);
const char* category_name(Category);
std::optional<Category> parse_category(std::string_view);

struct FileEntry { std::string path; std::uint64_t size = 0; std::string sha256; };
struct Requirement { std::string id, release, origin; };
struct Component {
    std::string id, vendor, version, release, api_family, matched_set, signer;
    int api_major = 0;
    std::vector<std::string> licence_files;
    std::vector<FileEntry> files;
    std::vector<Requirement> requires_;
    std::vector<std::string> conflicts;
};
struct SetManifest {
    int schema = 1;
    std::string set_id, family, version, release;
    Category category = Category::Upscaling;
    Channel channel = Channel::Stable;
    std::vector<Component> components;
    const Component* find(std::string_view id) const;
};
SetManifest parse_manifest(std::string_view text);
std::string manifest_to_json(const SetManifest&);
void validate_manifest(const SetManifest&);
Result verify_set_folder(const fs::path& dir, const SetManifest& manifest);

struct ZipLimits {
    std::size_t maxEntries = 4096;
    std::uint64_t maxTotal = 1ull << 30, maxFile = 1ull << 29, ratioFloor = 1u << 16;
    unsigned maxRatio = 100;
};
Result extract_zip(const fs::path& zip, const fs::path& freshFolder, const ZipLimits& limits = {});
Result extract_zip_bytes(std::string_view zip, const fs::path& freshFolder, const ZipLimits& limits = {});
bool inflate_raw(std::string_view in, std::string& out, std::uint64_t expected, std::string& error);

struct EngineApi {
    std::map<std::string, int> major{{"sl", 2}, {"ffx", 2}, {"xess", 3}, {"test", 1}};
    static EngineApi parse(std::string_view spec);
};

struct CatalogEntry {
    std::string set_id, family, version, release, api_family, manifest_sha256;
    Category category = Category::Upscaling;
    Channel channel = Channel::Stable;
    int api_major = 0;
    bool auto_ok = false;
};
struct Catalog {
    std::uint64_t serial = 0;
    std::vector<CatalogEntry> entries;
    std::vector<std::string> revoked;
    std::map<std::string, std::string> min_safe;
};
struct TrustStore { std::map<std::string, std::vector<std::uint8_t>> keys; };
struct CatalogResult { bool ok = false; std::string message; Catalog catalog; std::string keyId; };
CatalogResult verify_catalog(std::string_view signedText, const TrustStore& trust);
class CatalogProvider {
public:
    virtual ~CatalogProvider() = default;
    virtual std::optional<std::string> fetch() = 0;
};
class FileCatalogProvider : public CatalogProvider {
    fs::path path_;
public:
    explicit FileCatalogProvider(fs::path p) : path_(std::move(p)) {}
    std::optional<std::string> fetch() override;
};
class MemoryCatalogProvider : public CatalogProvider {
    std::optional<std::string> text_;
public:
    explicit MemoryCatalogProvider(std::optional<std::string> t) : text_(std::move(t)) {}
    std::optional<std::string> fetch() override { return text_; }
};
class PackageSource {
public:
    virtual ~PackageSource() = default;
    virtual std::optional<fs::path> fetch(const CatalogEntry&) = 0;
};
class LocalDirectoryPackageSource : public PackageSource {
    fs::path dir_;
public:
    explicit LocalDirectoryPackageSource(fs::path d) : dir_(std::move(d)) {}
    std::optional<fs::path> fetch(const CatalogEntry&) override;
};
namespace testing {
struct KeyPair { std::vector<std::uint8_t> publicBlob, privateBlob; };
KeyPair generate_test_keypair();
std::string sign_catalog_payload(const KeyPair&, std::string_view keyId, std::string_view payload);
std::string catalog_payload_json(const Catalog&);
}

enum class StageTrust { Catalog, Local };
struct Pending { std::string set, from; int boots = 0; };
struct FamilyState {
    Channel channel = Channel::Stable;
    bool otaAllowed = false;
    std::string active, previous, lastKnownGood, lock;
    std::optional<Pending> pending;
};
struct SetRecord { std::string trust, family; bool bad = false; };
struct State {
    int schema = 1;
    std::uint64_t serial = 0, catalogSerial = 0;
    std::string updateMode = "unset";
    std::map<std::string, FamilyState> families;
    std::map<std::string, SetRecord> sets;
    std::vector<std::string> revoked;
    std::vector<std::string> events;
};

struct ResolvedFamily {
    std::string family, category, setId, version, release, source, note;
    fs::path dir;
    bool locked = false, otaAllowed = false, fallback = false;
    std::vector<std::string> componentIds;
};
struct Resolution {
    std::vector<ResolvedFamily> families;
    std::vector<std::string> warnings;
    const ResolvedFamily* find(std::string_view family) const;
    std::string to_json() const;
};

struct PlanChange { std::string family, setId, why; };
struct ActivationPlan {
    bool ok = false;
    std::string message;
    std::vector<PlanChange> changes;
    std::vector<std::string> explanations;
};

enum class CandidateKind { None, UpToDate, Available, Locked };
struct Candidate {
    CandidateKind kind = CandidateKind::None;
    std::string setId, version, reason;
    bool manualRequired = false, autoEligible = false;
};

struct SetInfo {
    std::string setId, family, version, channel, trust, location;
    bool bad = false, bundled = false;
};
struct Inventory {
    std::vector<std::string> keep, removable;
    std::map<std::string, std::string> reason;
};

struct StoreOptions {
    fs::path root, bundledRoot;
    FaultHook fault;
    ZipLimits zip;
};

class Store {
public:
    explicit Store(StoreOptions options);
    const State& state() const { return state_; }
    const fs::path& root() const { return options_.root; }

    Result stage_folder(const fs::path& package, StageTrust trust, const CatalogEntry* expect = nullptr);
    Result stage_zip(const fs::path& zip, StageTrust trust, const CatalogEntry* expect = nullptr);
    std::vector<SetInfo> list_sets() const;
    std::optional<SetManifest> manifest(const std::string& setId) const;
    fs::path set_dir(const std::string& setId) const;

    ActivationPlan plan_activation(const std::string& family, const std::string& setId, const EngineApi& api) const;
    ActivationPlan activate(const std::string& family, const std::string& setId, const EngineApi& api);
    ActivationPlan restore_previous(const std::string& family, const EngineApi& api);
    ActivationPlan restore_stable(const std::string& family, const EngineApi& api);
    Result lock(const std::string& family);
    Result unlock(const std::string& family);
    Result set_channel(const std::string& family, Channel);
    Result set_mode(const std::string& mode);
    Result set_ota(const std::string& family, bool allow);
    bool first_launch_choice_needed() const { return state_.updateMode == "unset"; }

    Result apply_catalog(const Catalog&);
    Candidate candidate(const Catalog&, const std::string& family, Channel, const EngineApi&) const;
    std::vector<std::string> auto_update(const Catalog&, PackageSource&, const EngineApi&);

    Resolution begin_launch();
    void mark_graphics_initialized() { graphicsInitialized_ = true; }
    bool graphics_initialized() const { return graphicsInitialized_; }
    Result confirm_good();
    Result report_launch_failure(const std::string& reason);
    Resolution resolve() const;
    std::vector<std::string> verify_loaded(const Resolution&, const std::map<std::string, std::string>& loadedVersions) const;

    Inventory inventory() const;
    Result prune(bool confirm);

    std::string status_json(const std::optional<Catalog>&, const EngineApi&) const;

private:
    friend struct StoreAccess;
    StoreOptions options_;
    State state_;
    bool graphicsInitialized_ = false;
    std::map<std::string, fs::path> bundled_;
    std::map<std::string, SetManifest> bundledManifests_;
    std::set<std::string> loaded_;

    void load();
    void save(State next);
    void sweep();
    Result stage_prepared(const fs::path& folder, StageTrust trust, const CatalogEntry* expect);
    std::optional<SetManifest> manifest_any(const std::string& setId) const;
    bool usable(const std::string& setId, const EngineApi* api, std::string& why) const;
    std::map<std::string, std::string> intended() const;
    ActivationPlan solve(const std::string& family, const std::string& setId, const EngineApi& api, bool direct) const;
    void apply_plan(State& s, const ActivationPlan& plan, bool forceDirect) const;
    void revert_pending(State& s, const std::string& family, const std::string& reason) const;
    std::string baseline(const std::string& family, const FamilyState& f) const;
};

std::vector<std::string> check_combination(const std::vector<const SetManifest*>& sets);
fs::path default_installed_root(std::string_view appName);
fs::path portable_root(const fs::path& adjacentFolder);

struct ExportFile { std::string setId, componentId, relativePath, sha256; fs::path source; bool licence = false; };
struct ExportPlan {
    bool ok = false;
    std::string message;
    std::vector<std::string> componentIds, missing;
    std::vector<ExportFile> files;
};
bool known_feature(std::string_view feature);
std::vector<std::string> required_components_for(const std::set<std::string>& features, std::string& error);
ExportPlan plan_export(const Store&, const std::set<std::string>& features);
Result write_export(const ExportPlan&, const fs::path& freshDestination);

struct ReportInput {
    std::string failureCode, description, diagnostics;
    bool includeDiagnostics = false;
};
struct Report { std::string preview, json; bool diagnosticsIncluded = false; };
std::string redact_text(std::string_view);
Report build_report(const Resolution&, const ReportInput&);
Result save_report(const Report&, const fs::path& path);

int cli_main(const std::vector<std::string>& args, std::ostream& out, std::ostream& err);
}
