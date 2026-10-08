#include "graphics_runtime.hpp"
#include "../src/json.hpp"
#include <algorithm>
#include <cctype>
#include <regex>

namespace starlight::gfxrt {
using json = nlohmann::json;

namespace {
struct Rule { const char* feature; std::vector<const char*> components; };
const std::vector<const char*> kStreamline{"nvidia.streamline.interposer", "nvidia.streamline.common"};
const std::vector<Rule>& rules() {
    static const std::vector<Rule> r{
        {"streamline", {"nvidia.streamline.interposer", "nvidia.streamline.common"}},
        {"dlss-sr", {"nvidia.streamline.interposer", "nvidia.streamline.common", "nvidia.streamline.dlss", "nvidia.ngx.dlss"}},
        {"dlss-fg", {"nvidia.streamline.interposer", "nvidia.streamline.common", "nvidia.streamline.dlss-g", "nvidia.ngx.dlss-g"}},
        {"reflex", {"nvidia.streamline.interposer", "nvidia.streamline.common", "nvidia.streamline.reflex", "nvidia.streamline.pcl"}},
        {"ray-reconstruction", {"nvidia.streamline.interposer", "nvidia.streamline.common", "nvidia.streamline.dlss", "nvidia.ngx.dlss", "nvidia.streamline.dlss-d", "nvidia.ngx.dlss-d"}},
        {"fsr-sr", {"amd.ffx.loader", "amd.ffx.upscaler"}},
        {"fsr-fg", {"amd.ffx.loader", "amd.ffx.framegeneration"}},
        {"xess-sr", {"intel.xess.sr"}},
        {"xess-fg", {"intel.xess.fg", "intel.xell"}},
    };
    return r;
}
}

bool known_feature(std::string_view f) {
    for (auto& r : rules()) if (f == r.feature) return true;
    return false;
}

std::vector<std::string> required_components_for(const std::set<std::string>& features, std::string& error) {
    std::set<std::string> ids;
    for (auto& f : features) {
        bool found = false;
        for (auto& r : rules()) if (f == r.feature) { found = true; for (auto c : r.components) ids.insert(c); }
        if (!found) { error = "Unknown feature '" + f + "'."; return {}; }
    }
    return {ids.begin(), ids.end()};
}

ExportPlan plan_export(const Store& store, const std::set<std::string>& features) {
    ExportPlan plan;
    std::string err;
    auto ids = required_components_for(features, err);
    if (!err.empty()) { plan.message = err; return plan; }
    auto res = store.resolve();
    std::map<std::string, std::pair<const ResolvedFamily*, Component>> index;
    std::map<std::string, SetManifest> manifests;
    for (auto& rf : res.families) {
        if (rf.setId.empty()) continue;
        auto m = store.manifest(rf.setId);
        if (!m) continue;
        manifests[rf.setId] = *m;
        for (auto& c : m->components) index.emplace(c.id, std::make_pair(&rf, c));
    }
    std::set<std::string> want(ids.begin(), ids.end());
    std::vector<std::string> queue(ids.begin(), ids.end());
    std::set<std::string> done;
    while (!queue.empty()) {
        auto id = queue.back();
        queue.pop_back();
        if (!done.insert(id).second) continue;
        auto it = index.find(id);
        if (it == index.end()) { plan.missing.push_back(id); continue; }
        for (auto& r : it->second.second.requires_) {
            if (!r.release.empty()) {
                auto o = index.find(r.id);
                if (o != index.end() && o->second.second.release != r.release) {
                    plan.missing.push_back(r.id + " release " + r.release + " (active copy is release " + o->second.second.release + ")");
                    continue;
                }
            }
            want.insert(r.id);
            queue.push_back(r.id);
        }
    }
    if (!plan.missing.empty()) {
        std::string m;
        for (auto& x : plan.missing) m += (m.empty() ? "" : ", ") + x;
        plan.message = "Cannot build a complete export. Missing or mismatched: " + m + ".";
        return plan;
    }
    std::map<std::string, std::string> pathHash;
    std::map<std::string, std::pair<std::string, std::string>> group;
    for (auto& id : want) {
        auto& [rf, c] = index.at(id);
        auto [git, fresh] = group.emplace(c.matched_set, std::make_pair(c.release, c.id));
        if (!fresh && git->second.first != c.release) {
            plan.message = "Matched set '" + c.matched_set + "' would mix releases " + git->second.first + " and " + c.release + "; refusing (Starlight conservative policy).";
            return plan;
        }
        plan.componentIds.push_back(id);
        for (auto& f : c.files) {
            auto key = f.path;
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (auto p = pathHash.find(key); p != pathHash.end()) {
                if (p->second != f.sha256) { plan.message = "Two components provide different files at " + f.path + "."; return plan; }
                continue;
            }
            pathHash[key] = f.sha256;
            bool lic = std::find(c.licence_files.begin(), c.licence_files.end(), f.path) != c.licence_files.end();
            plan.files.push_back({rf->setId, id, f.path, f.sha256, rf->dir / from_utf8(f.path), lic});
        }
    }
    plan.ok = true;
    plan.message = "Export plan has " + std::to_string(plan.componentIds.size()) + " component(s) and " + std::to_string(plan.files.size()) + " file(s).";
    return plan;
}

Result write_export(const ExportPlan& plan, const fs::path& dest) {
    if (!plan.ok) return Result::fail("The export plan is not complete: " + plan.message);
    std::error_code ec;
    if (fs::exists(dest, ec)) return Result::fail("Export destination already exists.");
    auto staging = dest;
    staging += L".staging";
    fs::remove_all(staging, ec);
    try {
        for (auto& f : plan.files) {
            if (sha256_file(f.source) != f.sha256) throw GrError("Source file changed: " + f.relativePath);
            auto to = staging / from_utf8(f.relativePath);
            fs::create_directories(to.parent_path());
            fs::copy_file(f.source, to, fs::copy_options::none);
            if (sha256_file(to) != f.sha256) throw GrError("Copied file differs: " + f.relativePath);
        }
        fs::rename(staging, dest);
    } catch (const std::exception& e) {
        fs::remove_all(staging, ec);
        return Result::fail(e.what());
    }
    return Result::good("Export written.");
}

std::string redact_text(std::string_view in) {
    std::string s(in);
    static const std::regex path(R"([A-Za-z]:[\\/][^\s"'<>|]*)");
    static const std::regex unc(R"(\\\\[^\s"'<>|]+)");
    static const std::regex mail(R"([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,})");
    static const std::regex ip(R"(\b\d{1,3}(\.\d{1,3}){3}\b)");
    static const std::regex longHex(R"(\b[0-9A-Fa-f]{32,}\b)");
    static const std::regex guid(R"(\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?)");
    s = std::regex_replace(s, path, "<path>");
    s = std::regex_replace(s, unc, "<path>");
    s = std::regex_replace(s, mail, "<email>");
    s = std::regex_replace(s, guid, "<id>");
    s = std::regex_replace(s, longHex, "<id>");
    s = std::regex_replace(s, ip, "<ip>");
    return s;
}

Report build_report(const Resolution& res, const ReportInput& in) {
    json j{{"schema", 1}, {"components", json::array()}};
    for (auto& f : res.families)
        if (!f.setId.empty()) j["components"].push_back({{"family", f.family}, {"set", f.setId}, {"version", f.version}, {"source", f.source}, {"locked", f.locked}, {"component_ids", f.componentIds}});
    std::string code = in.failureCode.empty() ? "unspecified" : in.failureCode;
    bool codeOk = code.size() <= 64;
    for (unsigned char c : code) if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.')) codeOk = false;
    j["failure_code"] = codeOk ? code : "invalid-code";
    auto desc = in.description.substr(0, 4000);
    j["description"] = desc;
    Report r;
    if (in.includeDiagnostics) {
        j["diagnostics"] = redact_text(in.diagnostics.substr(0, 20000));
        r.diagnosticsIncluded = true;
    }
    r.json = j.dump(2);
    std::string p = "PROBLEM REPORT PREVIEW (nothing has been sent; it is only saved if you choose to save it)\n\nComponents and versions:\n";
    for (auto& c : j["components"]) p += "  " + c["family"].get<std::string>() + ": " + c["version"].get<std::string>() + " (set " + c["set"].get<std::string>() + ", " + c["source"].get<std::string>() + (c["locked"].get<bool>() ? ", locked" : "") + ")\n";
    p += "\nFailure code: " + j["failure_code"].get<std::string>() + "\n\nYour description:\n" + desc + "\n";
    if (r.diagnosticsIncluded) p += "\nExtra diagnostics you chose to include (paths, addresses and identifiers removed):\n" + j["diagnostics"].get<std::string>() + "\n";
    else p += "\nExtra diagnostics: not included.\n";
    p += "\nThis report contains no system inventory, hardware list, account or machine identifier.\n";
    r.preview = std::move(p);
    return r;
}

Result save_report(const Report& r, const fs::path& path) {
    std::error_code ec;
    if (fs::exists(path, ec)) return Result::fail("A file already exists at that location; choose a new name.");
    try {
        atomic_replace_file(path, r.json, nullptr, "report", false);
    } catch (const std::exception& e) {
        return Result::fail(e.what());
    }
    return Result::good("Report saved locally. Nothing was uploaded.");
}

}
