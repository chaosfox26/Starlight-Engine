#include "graphics_runtime.hpp"
#include "../src/json.hpp"
#include <algorithm>
#include <cctype>

namespace starlight::gfxrt {
using json = nlohmann::json;

static void only_keys(const json& j, std::initializer_list<const char*> allowed, const char* where) {
    if (!j.is_object()) throw GrError(std::string(where) + " must be an object");
    for (auto it = j.begin(); it != j.end(); ++it) {
        bool known = false;
        for (auto a : allowed) if (it.key() == a) known = true;
        if (!known) throw GrError(std::string("Unknown key '") + it.key() + "' in " + where);
    }
}
static std::string str_of(const json& j, const char* key, const char* where, bool required = true) {
    if (!j.contains(key)) {
        if (required) throw GrError(std::string("Missing '") + key + "' in " + where);
        return {};
    }
    if (!j[key].is_string()) throw GrError(std::string("'") + key + "' in " + where + " must be text");
    return j[key].get<std::string>();
}
static std::vector<std::string> strs_of(const json& j, const char* key, const char* where) {
    std::vector<std::string> out;
    if (!j.contains(key)) return out;
    if (!j[key].is_array()) throw GrError(std::string("'") + key + "' in " + where + " must be a list");
    for (auto& v : j[key]) {
        if (!v.is_string()) throw GrError(std::string("'") + key + "' entries in " + where + " must be text");
        out.push_back(v.get<std::string>());
    }
    return out;
}

SetManifest parse_manifest(std::string_view text) {
    json j;
    try { j = json::parse(text); } catch (const std::exception& e) { throw GrError(std::string("Manifest is not valid JSON: ") + e.what()); }
    only_keys(j, {"schema", "set_id", "family", "category", "channel", "version", "components"}, "manifest");
    SetManifest m;
    if (!j.contains("schema") || !j["schema"].is_number_integer()) throw GrError("Missing 'schema' in manifest");
    m.schema = j["schema"].get<int>();
    m.set_id = str_of(j, "set_id", "manifest");
    m.family = str_of(j, "family", "manifest");
    auto cat = parse_category(str_of(j, "category", "manifest"));
    if (!cat) throw GrError("Unknown category (use upscaling, frame-generation or latency-reduction)");
    m.category = *cat;
    auto ch = parse_channel(str_of(j, "channel", "manifest"));
    if (!ch) throw GrError("Unknown channel (use stable or experimental)");
    m.channel = *ch;
    m.version = str_of(j, "version", "manifest");
    if (!j.contains("components") || !j["components"].is_array()) throw GrError("Missing 'components' list in manifest");
    for (auto& c : j["components"]) {
        only_keys(c, {"id", "vendor", "version", "release", "api_family", "api_major", "matched_set", "signer", "licence_files", "files", "requires", "conflicts"}, "component");
        Component k;
        k.id = str_of(c, "id", "component");
        k.vendor = str_of(c, "vendor", "component");
        k.version = str_of(c, "version", "component");
        k.release = str_of(c, "release", "component");
        k.api_family = str_of(c, "api_family", "component");
        if (!c.contains("api_major") || !c["api_major"].is_number_integer()) throw GrError("Missing 'api_major' in component " + k.id);
        k.api_major = c["api_major"].get<int>();
        k.matched_set = str_of(c, "matched_set", "component");
        k.signer = str_of(c, "signer", "component");
        k.licence_files = strs_of(c, "licence_files", "component");
        k.conflicts = strs_of(c, "conflicts", "component");
        if (!c.contains("files") || !c["files"].is_array()) throw GrError("Missing 'files' list in component " + k.id);
        for (auto& f : c["files"]) {
            only_keys(f, {"path", "size", "sha256"}, "file entry");
            FileEntry e;
            e.path = str_of(f, "path", "file entry");
            if (!f.contains("size") || !f["size"].is_number_unsigned()) throw GrError("Missing 'size' for " + e.path);
            e.size = f["size"].get<std::uint64_t>();
            e.sha256 = str_of(f, "sha256", "file entry");
            k.files.push_back(std::move(e));
        }
        if (c.contains("requires")) {
            if (!c["requires"].is_array()) throw GrError("'requires' must be a list");
            for (auto& r : c["requires"]) {
                only_keys(r, {"id", "release", "origin"}, "requirement");
                k.requires_.push_back({str_of(r, "id", "requirement"), str_of(r, "release", "requirement", false), str_of(r, "origin", "requirement")});
            }
        }
        m.components.push_back(std::move(k));
    }
    return m;
}

std::string manifest_to_json(const SetManifest& m) {
    json j{{"schema", m.schema}, {"set_id", m.set_id}, {"family", m.family}, {"category", category_name(m.category)},
           {"channel", channel_name(m.channel)}, {"version", m.version}, {"components", json::array()}};
    for (auto& c : m.components) {
        json k{{"id", c.id}, {"vendor", c.vendor}, {"version", c.version}, {"release", c.release}, {"api_family", c.api_family},
               {"api_major", c.api_major}, {"matched_set", c.matched_set}, {"signer", c.signer}, {"licence_files", c.licence_files},
               {"conflicts", c.conflicts}, {"files", json::array()}, {"requires", json::array()}};
        for (auto& f : c.files) k["files"].push_back({{"path", f.path}, {"size", f.size}, {"sha256", f.sha256}});
        for (auto& r : c.requires_) k["requires"].push_back({{"id", r.id}, {"release", r.release}, {"origin", r.origin}});
        j["components"].push_back(std::move(k));
    }
    return j.dump(2);
}

static bool ident_ok(const std::string& s) {
    if (s.empty() || s.size() > 64 || !(std::islower(static_cast<unsigned char>(s[0])) || std::isdigit(static_cast<unsigned char>(s[0])))) return false;
    for (unsigned char c : s) if (!(std::islower(c) || std::isdigit(c) || c == '.' || c == '-' || c == '_')) return false;
    return true;
}
static bool hex64(const std::string& s) {
    if (s.size() != 64) return false;
    for (unsigned char c : s) if (!(std::isdigit(c) || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

void validate_manifest(const SetManifest& m) {
    if (m.schema != 1) throw GrError("Unsupported manifest schema " + std::to_string(m.schema));
    if (!ident_ok(m.set_id)) throw GrError("Set id must be lowercase letters, digits, dot, dash or underscore");
    if (!ident_ok(m.family)) throw GrError("Family name is not valid");
    if (!parse_version(m.version)) throw GrError("Set version must be numeric like 1.2.3");
    if (m.components.empty()) throw GrError("A set needs at least one component");
    std::set<std::string> ids, paths;
    std::map<std::string, std::string> groupRelease;
    for (auto& c : m.components) {
        if (!ident_ok(c.id)) throw GrError("Component id '" + c.id + "' is not valid");
        if (!ids.insert(c.id).second) throw GrError("Duplicate component id " + c.id);
        if (c.vendor.empty()) throw GrError("Component " + c.id + " has no vendor");
        auto v = parse_version(c.version);
        if (!v) throw GrError("Component " + c.id + " has a bad version");
        if (c.vendor == "intel" && v->parts.size() >= 3 && v->parts[2] >= 90) throw GrError("Component " + c.id + " is an Intel development build (patch 90 or higher)");
        if (c.release.empty() || c.api_family.empty() || c.api_major <= 0) throw GrError("Component " + c.id + " needs release, api_family and api_major");
        if (c.matched_set.empty()) throw GrError("Component " + c.id + " names no matched set");
        if (c.signer.empty()) throw GrError("Component " + c.id + " names no signer");
        auto [it, fresh] = groupRelease.emplace(c.matched_set, c.release);
        if (!fresh && it->second != c.release)
            throw GrError("Matched set '" + c.matched_set + "' mixes releases " + it->second + " and " + c.release + " (Starlight conservative policy, not a vendor statement)");
        if (c.files.empty()) throw GrError("Component " + c.id + " lists no files");
        std::set<std::string> own;
        for (auto& f : c.files) {
            std::string why;
            if (!safe_relative_path(f.path, why)) throw GrError("Unsafe path '" + f.path + "' (" + why + ")");
            if (!paths.insert(fold_name(f.path)).second) throw GrError("Duplicate or case-colliding path " + f.path);
            if (!hex64(f.sha256)) throw GrError("File " + f.path + " needs a lowercase SHA-256");
            own.insert(f.path);
        }
        if (c.licence_files.empty()) throw GrError("Component " + c.id + " lists no licence file");
        for (auto& l : c.licence_files) if (!own.count(l)) throw GrError("Licence file " + l + " is not one of the files of " + c.id);
        for (auto& r : c.requires_) {
            if (!ident_ok(r.id) || r.id == c.id) throw GrError("Component " + c.id + " has a bad requirement");
            if (r.origin != "vendor" && r.origin != "starlight-policy") throw GrError("Requirement origin must be vendor or starlight-policy");
        }
        for (auto& x : c.conflicts) if (x == c.id) throw GrError("Component " + c.id + " conflicts with itself");
    }
    for (auto& c : m.components) {
        for (auto& r : c.requires_) {
            if (auto* other = m.find(r.id); other && !r.release.empty() && other->release != r.release)
                throw GrError("Component " + c.id + " requires " + r.id + " release " + r.release + " but the set holds " + other->release);
        }
        for (auto& x : c.conflicts) if (m.find(x)) throw GrError("Component " + c.id + " conflicts with " + x + " inside the same set");
    }
}

const Component* SetManifest::find(std::string_view id) const {
    for (auto& c : components) if (c.id == id) return &c;
    return nullptr;
}

Result verify_set_folder(const fs::path& dir, const SetManifest& m) {
    std::set<std::string> expected;
    try {
        for (auto& c : m.components) {
            for (auto& f : c.files) {
                auto p = dir / from_utf8(f.path);
                expected.insert(fold_name(f.path));
                if (is_reparse_or_link(p)) return Result::fail("File is a link: " + f.path);
                std::error_code ec;
                if (!fs::is_regular_file(p, ec)) return Result::fail("File is missing: " + f.path);
                if (fs::file_size(p, ec) != f.size) return Result::fail("File size differs: " + f.path);
                if (sha256_file(p) != f.sha256) return Result::fail("File content changed: " + f.path);
            }
        }
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (is_reparse_or_link(it->path())) return Result::fail("Unexpected link in set folder");
            if (!it->is_regular_file()) continue;
            auto rel = fold_name(utf8(fs::relative(it->path(), dir)));
            std::replace(rel.begin(), rel.end(), '\\', '/');
            if (rel != "set.json" && !expected.count(rel)) return Result::fail("Unexpected file in set folder: " + rel);
        }
    } catch (const std::exception& e) {
        return Result::fail(e.what());
    }
    return Result::good();
}

}
