#include "graphics_runtime.hpp"
#include "../src/json.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>

namespace starlight::gfxrt {
using json = nlohmann::json;

namespace {
std::atomic<unsigned> stagingCounter{0};

json state_to_json(const State& s) {
    json j{{"schema", s.schema}, {"serial", s.serial}, {"catalog_serial", s.catalogSerial}, {"update_mode", s.updateMode},
           {"revoked", s.revoked}, {"events", s.events}, {"families", json::object()}, {"sets", json::object()}};
    for (auto& [name, f] : s.families) {
        json fj{{"channel", channel_name(f.channel)}, {"ota_allowed", f.otaAllowed}, {"active", f.active}, {"previous", f.previous},
                {"last_known_good", f.lastKnownGood}, {"lock", f.lock}};
        if (f.pending) fj["pending"] = {{"set", f.pending->set}, {"from", f.pending->from}, {"boots", f.pending->boots}};
        j["families"][name] = std::move(fj);
    }
    for (auto& [id, r] : s.sets) j["sets"][id] = {{"trust", r.trust}, {"family", r.family}, {"bad", r.bad}};
    return j;
}
State state_from_json(const json& j) {
    State s;
    if (j.value("schema", 0) != 1) throw GrError("Unsupported state schema");
    s.serial = j.value("serial", 0ull);
    s.catalogSerial = j.value("catalog_serial", 0ull);
    s.updateMode = j.value("update_mode", std::string("unset"));
    if (s.updateMode != "unset" && s.updateMode != "automatic" && s.updateMode != "manual") throw GrError("Bad update mode");
    if (j.contains("revoked")) for (auto& v : j["revoked"]) s.revoked.push_back(v.get<std::string>());
    if (j.contains("events")) for (auto& v : j["events"]) s.events.push_back(v.get<std::string>());
    if (j.contains("families")) {
        for (auto it = j["families"].begin(); it != j["families"].end(); ++it) {
            FamilyState f;
            auto& v = it.value();
            auto ch = parse_channel(v.value("channel", std::string("stable")));
            if (!ch) throw GrError("Bad channel in state");
            f.channel = *ch;
            f.otaAllowed = v.value("ota_allowed", false);
            f.active = v.value("active", std::string());
            f.previous = v.value("previous", std::string());
            f.lastKnownGood = v.value("last_known_good", std::string());
            f.lock = v.value("lock", std::string());
            if (v.contains("pending")) f.pending = Pending{v["pending"].value("set", std::string()), v["pending"].value("from", std::string()), v["pending"].value("boots", 0)};
            s.families[it.key()] = std::move(f);
        }
    }
    if (j.contains("sets"))
        for (auto it = j["sets"].begin(); it != j["sets"].end(); ++it)
            s.sets[it.key()] = SetRecord{it.value().value("trust", std::string("local")), it.value().value("family", std::string()), it.value().value("bad", false)};
    return s;
}

struct Violation {
    std::string dependentFamily, dependentComponent, needId, needRelease, text;
    bool fixable = true;
};
using Chosen = std::vector<std::pair<std::string, const SetManifest*>>;

std::string origin_label(const std::string& o) { return o == "vendor" ? "vendor-documented rule" : "Starlight conservative policy, not a vendor statement"; }

std::vector<Violation> find_violations(const Chosen& chosen) {
    std::vector<Violation> out;
    std::multimap<std::string, const Component*> byId;
    for (auto& [fam, set] : chosen) for (auto& c : set->components) byId.emplace(c.id, &c);
    std::map<std::string, std::pair<std::string, std::string>> groups;
    for (auto& [fam, set] : chosen) {
        for (auto& c : set->components) {
            for (auto& r : c.requires_) {
                bool found = false;
                std::string present;
                auto range = byId.equal_range(r.id);
                for (auto it = range.first; it != range.second; ++it) {
                    if (r.release.empty() || it->second->release == r.release) found = true;
                    else present = " (present as release " + it->second->release + ")";
                }
                if (!found) {
                    Violation v{fam, c.id, r.id, r.release, {}, true};
                    v.text = c.id + " in set " + set->set_id + " needs " + r.id + (r.release.empty() ? "" : " release " + r.release) + " [" + origin_label(r.origin) + "] but the chosen combination " + (present.empty() ? "does not contain it" : "has it" + present);
                    out.push_back(std::move(v));
                }
            }
            for (auto& x : c.conflicts) {
                if (byId.count(x)) {
                    Violation v{fam, c.id, x, {}, c.id + " cannot be used together with " + x, false};
                    out.push_back(std::move(v));
                }
            }
            auto [it, fresh] = groups.emplace(c.matched_set, std::make_pair(c.release, c.id));
            if (!fresh && it->second.first != c.release) {
                Violation v{fam, c.id, {}, {}, "Matched set '" + c.matched_set + "' would mix release " + it->second.first + " (" + it->second.second + ") with release " + c.release + " (" + c.id + "); Starlight conservative policy, not a vendor statement", false};
                out.push_back(std::move(v));
            }
        }
    }
    return out;
}

std::string join(const std::vector<std::string>& v, const char* sep) {
    std::string s;
    for (auto& x : v) { if (!s.empty()) s += sep; s += x; }
    return s;
}
}

std::vector<std::string> check_combination(const std::vector<const SetManifest*>& sets) {
    Chosen c;
    for (auto* s : sets) c.emplace_back(s->family, s);
    std::vector<std::string> out;
    for (auto& v : find_violations(c)) out.push_back(v.text);
    return out;
}

const ResolvedFamily* Resolution::find(std::string_view family) const {
    for (auto& f : families) if (f.family == family) return &f;
    return nullptr;
}
std::string Resolution::to_json() const {
    json j{{"families", json::array()}, {"warnings", warnings}};
    for (auto& f : families)
        j["families"].push_back({{"family", f.family}, {"category", f.category}, {"set", f.setId}, {"version", f.version}, {"release", f.release},
                                 {"source", f.source}, {"locked", f.locked}, {"ota_allowed", f.otaAllowed}, {"fallback", f.fallback},
                                 {"note", f.note}, {"components", f.componentIds}, {"directory", utf8(f.dir)}});
    return j.dump(2, ' ', false, json::error_handler_t::replace);
}

Store::Store(StoreOptions options) : options_(std::move(options)) {
    if (options_.root.empty()) throw GrError("Store needs an explicit root folder");
    fs::create_directories(options_.root / "sets");
    std::error_code ec;
    if (!options_.bundledRoot.empty() && fs::is_directory(options_.bundledRoot, ec)) {
        for (auto& e : fs::directory_iterator(options_.bundledRoot, ec)) {
            if (!e.is_directory() || !fs::is_regular_file(e.path() / "set.json")) continue;
            try {
                auto m = parse_manifest(read_text(e.path() / "set.json"));
                validate_manifest(m);
                if (m.set_id != utf8(e.path().filename())) continue;
                bundled_[m.set_id] = e.path();
                bundledManifests_[m.set_id] = std::move(m);
            } catch (const GrError&) {}
        }
    }
    sweep();
    load();
}

// Staging that belongs to another live process is that process's operation in progress and is not swept (audit DEP-07). The shared temporary state files are still removed; a store-wide lock would be needed to protect them.
static bool process_alive(unsigned long pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return GetLastError() != ERROR_INVALID_PARAMETER;
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

void Store::sweep() {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(options_.root / "sets", ec)) {
        auto name = utf8(e.path().filename());
        if (name.rfind(".staging-", 0) != 0) continue;
        const unsigned long pid = std::strtoul(name.c_str() + 9, nullptr, 10);
        if (pid && pid != GetCurrentProcessId() && process_alive(pid)) continue;
        fs::remove_all(e.path(), ec);
    }
    for (auto n : {L"state.json.tmp", L"state.json.bak.tmp"}) fs::remove(options_.root / n, ec);
}

// Damaged state copies kept by load() so a reset never destroys the evidence (audit GR-3).
static std::vector<std::string> kept_damaged_state(const fs::path& root) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(root, ec)) {
        auto name = utf8(e.path().filename());
        if (name.rfind("state.json", 0) == 0 && name.find(".corrupt-") != std::string::npos) out.push_back(name);
    }
    return out;
}

void Store::load() {
    auto main = options_.root / "state.json", bak = options_.root / "state.json.bak";
    std::error_code ec;
    std::vector<fs::path> unparseable;
    std::string unreadable;
    for (auto* p : {&main, &bak}) {
        if (!fs::is_regular_file(*p, ec)) continue;
        std::string text;
        bool read = false;
        for (int attempt = 0; attempt < 5 && !read; ++attempt) {
            try { text = read_text(*p); read = true; } catch (const GrError&) { Sleep(30); }
        }
        if (!read) { unreadable += (unreadable.empty() ? "" : ", ") + utf8(p->filename()); continue; }
        try {
            state_ = state_from_json(json::parse(text));
            if (p == &bak) state_.events.push_back("State file was damaged; restored the previous copy.");
            else if (!unreadable.empty()) state_.events.push_back("A state file could not be read; used the previous copy.");
            return;
        } catch (const std::exception&) { unparseable.push_back(*p); }
    }
    state_ = State{};
    std::string kept;
    for (auto& p : unparseable) {
        auto to = p;
        for (unsigned long long n = GetTickCount64();; ++n) {
            to = p;
            to += L".corrupt-" + std::to_wstring(n);
            if (!fs::exists(to, ec)) break;
        }
        fs::rename(p, to, ec);
        if (!ec) kept += (kept.empty() ? "" : ", ") + utf8(to.filename());
    }
    if (!unparseable.empty() || !unreadable.empty())
        state_.events.push_back("The state files could not be used, so locks, revoked sets and the catalog serial were reset." + (kept.empty() ? std::string() : " Damaged copies kept as " + kept + ".") + " Review before pruning.");
}

void Store::save(State next) {
    next.serial = state_.serial + 1;
    if (next.events.size() > 20) next.events.erase(next.events.begin(), next.events.end() - 20);
    atomic_replace_file(options_.root / "state.json", state_to_json(next).dump(2, ' ', false, json::error_handler_t::replace), options_.fault, "state", true);
    state_ = std::move(next);
}

fs::path Store::set_dir(const std::string& id) const {
    if (auto it = bundled_.find(id); it != bundled_.end()) return it->second;
    return options_.root / "sets" / from_utf8(id);
}

std::optional<SetManifest> Store::manifest_any(const std::string& id) const {
    if (auto it = bundledManifests_.find(id); it != bundledManifests_.end()) return it->second;
    auto rec = state_.sets.find(id);
    if (rec == state_.sets.end()) return std::nullopt;
    try {
        auto m = parse_manifest(read_text(options_.root / "sets" / from_utf8(id) / "set.json"));
        validate_manifest(m);
        if (m.set_id != id) return std::nullopt;
        return m;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}
std::optional<SetManifest> Store::manifest(const std::string& id) const { return manifest_any(id); }

bool Store::usable(const std::string& id, const EngineApi* api, std::string& why) const {
    auto m = manifest_any(id);
    if (!m) { why = "Set " + id + " is not installed or its manifest is damaged."; return false; }
    if (auto rec = state_.sets.find(id); rec != state_.sets.end() && rec->second.bad) { why = "Set " + id + " failed a previous launch and is marked bad."; return false; }
    if (std::find(state_.revoked.begin(), state_.revoked.end(), id) != state_.revoked.end()) { why = "Set " + id + " has been revoked by the catalog."; return false; }
    if (api) {
        for (auto& c : m->components) {
            auto it = api->major.find(c.api_family);
            if (it == api->major.end() || it->second != c.api_major) {
                why = "Set " + id + " targets " + c.api_family + " API major " + std::to_string(c.api_major) + " but this build uses " +
                      (it == api->major.end() ? std::string("none") : std::to_string(it->second)) + "; major changes are manual and need a new build.";
                return false;
            }
        }
    }
    auto v = verify_set_folder(set_dir(id), *m);
    if (!v.ok) { why = "Set " + id + " failed verification: " + v.message; return false; }
    return true;
}

static std::string newest_bundled(const std::map<std::string, SetManifest>& all, const std::string& family) {
    std::string best;
    std::optional<Version> bv;
    for (auto& [id, m] : all) {
        if (m.family != family) continue;
        auto v = parse_version(m.version);
        if (!bv || v->compare(*bv) > 0) { best = id; bv = v; }
    }
    return best;
}

std::string Store::baseline(const std::string& family, const FamilyState& f) const {
    if (!f.active.empty()) return f.active;
    if (!f.lastKnownGood.empty()) return f.lastKnownGood;
    return newest_bundled(bundledManifests_, family);
}

std::map<std::string, std::string> Store::intended() const {
    std::set<std::string> names;
    for (auto& [n, f] : state_.families) names.insert(n);
    for (auto& [id, m] : bundledManifests_) names.insert(m.family);
    for (auto& [id, r] : state_.sets) if (!r.family.empty()) names.insert(r.family);
    std::map<std::string, std::string> out;
    for (auto& n : names) {
        FamilyState f;
        if (auto it = state_.families.find(n); it != state_.families.end()) f = it->second;
        std::string pick = !f.lock.empty() ? f.lock : f.pending ? f.pending->set : !f.active.empty() ? f.active : !f.lastKnownGood.empty() ? f.lastKnownGood : newest_bundled(bundledManifests_, n);
        if (!pick.empty()) out[n] = pick;
    }
    return out;
}

Resolution Store::resolve() const {
    Resolution r;
    std::set<std::string> names;
    for (auto& [n, f] : state_.families) names.insert(n);
    for (auto& [id, m] : bundledManifests_) names.insert(m.family);
    for (auto& [id, rec] : state_.sets) if (!rec.family.empty()) names.insert(rec.family);
    for (auto& n : names) {
        FamilyState f;
        if (auto it = state_.families.find(n); it != state_.families.end()) f = it->second;
        ResolvedFamily rf;
        rf.family = n;
        rf.locked = !f.lock.empty();
        rf.otaAllowed = f.otaAllowed && !rf.locked;
        struct Tier { std::string id, source; };
        std::vector<Tier> tiers;
        if (rf.locked) {
            tiers.push_back({f.lock, "locked"});
        } else {
            if (f.pending && f.pending->boots >= 1) tiers.push_back({f.pending->set, "trial"});
            if (!f.active.empty()) tiers.push_back({f.active, "active"});
            if (!f.lastKnownGood.empty()) tiers.push_back({f.lastKnownGood, "last-known-good"});
        }
        if (auto b = newest_bundled(bundledManifests_, n); !b.empty()) tiers.push_back({b, "bundled"});
        bool first = true;
        for (auto& t : tiers) {
            std::string why;
            if (usable(t.id, nullptr, why)) {
                auto m = *manifest_any(t.id);
                rf.setId = t.id;
                rf.source = t.source;
                rf.version = m.version;
                rf.release = m.components.front().release;
                rf.category = category_name(m.category);
                rf.dir = set_dir(t.id);
                rf.fallback = !first;
                for (auto& c : m.components) rf.componentIds.push_back(c.id);
                if (!first) rf.note = "Fell back because the preferred set could not be used.";
                break;
            }
            r.warnings.push_back(n + ": " + why + (t.source == "locked" ? " The lock is kept; fix or unlock it." : ""));
            first = false;
        }
        if (rf.setId.empty()) rf.source = "none";
        r.families.push_back(std::move(rf));
    }
    return r;
}

Resolution Store::begin_launch() {
    if (graphicsInitialized_) throw GrError("begin_launch must run before graphics initialisation");
    State s = state_;
    bool changed = false;
    for (auto& [name, f] : s.families) {
        if (!f.pending) continue;
        if (!f.lock.empty()) {
            s.events.push_back(name + ": pending trial of " + f.pending->set + " dropped because the family is locked.");
            f.pending.reset();
            changed = true;
        } else if (f.pending->boots >= 1) {
            revert_pending(s, name, "the previous launch with this set never reached a confirmed good state");
            changed = true;
        } else {
            f.pending->boots = 1;
            changed = true;
        }
    }
    if (changed) save(std::move(s));
    auto res = resolve();
    for (auto& f : res.families) if (!f.setId.empty()) loaded_.insert(f.setId);
    return res;
}

void Store::revert_pending(State& s, const std::string& family, const std::string& reason) const {
    auto& f = s.families[family];
    if (!f.pending) return;
    if (auto it = s.sets.find(f.pending->set); it != s.sets.end()) it->second.bad = true;
    s.events.push_back(family + ": reverted to " + (f.active.empty() ? std::string("the bundled set") : f.active) + " because " + reason + " (set " + f.pending->set + " marked bad).");
    f.pending.reset();
}

Result Store::confirm_good() {
    State s = state_;
    std::vector<std::string> done;
    // A set is only promoted when it is the one resolve() really picks now (audit GR-1); the CLI runs this in another process, so loaded_ cannot be used.
    const auto res = resolve();
    for (auto& [name, f] : s.families) {
        const auto* rf = res.find(name);
        const bool resolvedAs = rf && !rf->setId.empty();
        if (f.pending && f.pending->boots >= 1 && f.lock.empty()) {
            if (resolvedAs && rf->source == "trial" && rf->setId == f.pending->set) {
                if (auto base = baseline(name, f); base != f.pending->set) f.previous = base;
                f.active = f.pending->set;
                f.lastKnownGood = f.active;
                done.push_back(name + " now runs " + f.active);
                f.pending.reset();
            } else {
                revert_pending(s, name, "the trial set could not be used at launch");
            }
        } else if (!f.active.empty() && f.lastKnownGood != f.active && f.lock.empty() && !f.pending) {
            if (resolvedAs && rf->source == "active" && rf->setId == f.active) f.lastKnownGood = f.active;
        }
    }
    save(std::move(s));
    return Result::good(join(done, "; "));
}

Result Store::report_launch_failure(const std::string& reason) {
    State s = state_;
    auto res = resolve();
    for (auto& rf : res.families) {
        auto& f = s.families[rf.family];
        if (rf.locked) { s.events.push_back(rf.family + ": launch failed with locked set " + rf.setId + "; the lock was left in place."); continue; }
        if (f.pending && f.pending->boots >= 1) { revert_pending(s, rf.family, reason); continue; }
        if (rf.source == "active" && f.active != f.lastKnownGood) {
            if (auto it = s.sets.find(f.active); it != s.sets.end()) it->second.bad = true;
            s.events.push_back(rf.family + ": set " + f.active + " failed (" + reason + "); falling back to the last known good set.");
            f.active = f.lastKnownGood;
        }
    }
    save(std::move(s));
    return Result::good();
}

std::vector<std::string> Store::verify_loaded(const Resolution& res, const std::map<std::string, std::string>& loaded) const {
    std::vector<std::string> problems;
    for (auto& f : res.families) {
        if (f.setId.empty()) continue;
        auto it = loaded.find(f.family);
        if (it == loaded.end()) { if (f.locked) problems.push_back(f.family + ": pinned to " + f.version + " but the host reported no loaded version."); continue; }
        if (it->second != f.version) problems.push_back(f.family + (f.locked ? " (pinned)" : "") + ": expected " + f.version + " but the host reports " + it->second + ".");
        if (f.locked && f.otaAllowed) problems.push_back(f.family + ": locked but vendor updates are still allowed.");
    }
    return problems;
}

std::vector<SetInfo> Store::list_sets() const {
    std::vector<SetInfo> out;
    for (auto& [id, m] : bundledManifests_) out.push_back({id, m.family, m.version, channel_name(m.channel), "bundled", utf8(bundled_.at(id)), false, true});
    for (auto& [id, rec] : state_.sets) {
        auto m = manifest_any(id);
        out.push_back({id, rec.family, m ? m->version : "?", m ? channel_name(m->channel) : "?", rec.trust, utf8(options_.root / "sets" / from_utf8(id)), rec.bad, false});
    }
    return out;
}

Result Store::stage_prepared(const fs::path& folder, StageTrust trust, const CatalogEntry* expect) {
    try {
        auto text = read_text(folder / "set.json");
        auto m = parse_manifest(text);
        validate_manifest(m);
        if (trust == StageTrust::Catalog) {
            if (!expect) return Result::fail("A catalog-trusted package needs its catalog entry.");
            if (m.set_id != expect->set_id || m.family != expect->family || m.version != expect->version || m.channel != expect->channel || sha256_bytes(text) != expect->manifest_sha256)
                return Result::fail("Package does not match its signed catalog entry; refusing it.");
        }
        if (std::find(state_.revoked.begin(), state_.revoked.end(), m.set_id) != state_.revoked.end()) return Result::fail("Set " + m.set_id + " is revoked.");
        if (bundledManifests_.count(m.set_id)) return Result::fail("Set id collides with a bundled set.");
        auto v = verify_set_folder(folder, m);
        if (!v.ok) return Result::fail("Package failed verification: " + v.message);
        if (options_.fault) options_.fault("stage.verified");
        auto target = options_.root / "sets" / from_utf8(m.set_id);
        std::error_code ec;
        if (fs::exists(target, ec)) {
            if (sha256_file(target / "set.json") != sha256_bytes(text) || !verify_set_folder(target, m).ok) return Result::fail("A different set with this id is already installed; refusing to replace it.");
            fs::remove_all(folder, ec);
        } else {
            fs::rename(folder, target, ec);
            if (ec) return Result::fail("Could not move the verified set into place.");
        }
        if (options_.fault) options_.fault("stage.renamed");
        State s = state_;
        auto& rec = s.sets[m.set_id];
        if (rec.trust.empty() || trust == StageTrust::Catalog) rec.trust = trust == StageTrust::Catalog ? "catalog" : "local";
        rec.family = m.family;
        s.families[m.family];
        save(std::move(s));
        return Result::good("Staged " + m.set_id + " (" + m.version + "). It is not active yet.");
    } catch (const GrError& e) {
        return Result::fail(e.what());
    } catch (const std::system_error& e) {
        return Result::fail(e.what());
    }
}

Result Store::stage_folder(const fs::path& package, StageTrust trust, const CatalogEntry* expect) {
    std::error_code ec;
    if (!fs::is_directory(package, ec) || !fs::is_regular_file(package / "set.json", ec)) return Result::fail("Package folder needs a set.json.");
    for (auto it = fs::recursive_directory_iterator(package, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
        if (is_reparse_or_link(it->path())) return Result::fail("Package contains a link; refusing it.");
    auto staging = options_.root / "sets" / from_utf8(".staging-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(stagingCounter++));
    fs::copy(package, staging, fs::copy_options::recursive, ec);
    if (ec) { fs::remove_all(staging, ec); return Result::fail("Could not copy the package into staging."); }
    if (options_.fault) options_.fault("stage.copied");
    auto r = stage_prepared(staging, trust, expect);
    fs::remove_all(staging, ec);
    return r;
}

Result Store::stage_zip(const fs::path& zip, StageTrust trust, const CatalogEntry* expect) {
    auto staging = options_.root / "sets" / from_utf8(".staging-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(stagingCounter++));
    auto x = extract_zip(zip, staging, options_.zip);
    if (!x.ok) return x;
    if (options_.fault) options_.fault("stage.extracted");
    auto r = stage_prepared(staging, trust, expect);
    std::error_code ec;
    fs::remove_all(staging, ec);
    return r;
}

ActivationPlan Store::solve(const std::string& family, const std::string& setId, const EngineApi& api, bool) const {
    ActivationPlan plan;
    auto fail = [&](std::string m) { plan.ok = false; plan.message = std::move(m); plan.changes.clear(); return plan; };
    auto fit = state_.families.find(family);
    if (fit != state_.families.end() && !fit->second.lock.empty()) {
        if (fit->second.lock == setId) { plan.ok = true; plan.message = family + " is already locked to " + setId + "."; return plan; }
        return fail(family + " is locked to " + fit->second.lock + ". Unlock it before changing the version.");
    }
    auto target = manifest_any(setId);
    if (!target) return fail("Set " + setId + " is not installed.");
    if (target->family != family) return fail("Set " + setId + " belongs to family " + target->family + ", not " + family + ".");
    std::string why;
    if (!usable(setId, &api, why)) return fail(why);

    auto selection = intended();
    selection[family] = setId;
    std::set<std::string> touched{family};
    plan.changes.push_back({family, setId, "requested"});
    std::map<std::string, SetManifest> cache;
    auto load = [&](const std::string& id) -> const SetManifest* {
        if (!cache.count(id)) { auto m = manifest_any(id); if (!m) return nullptr; cache[id] = *m; }
        return &cache[id];
    };
    auto chosen = [&] {
        Chosen c;
        for (auto& [f, id] : selection) if (auto* m = load(id)) c.emplace_back(f, m);
        return c;
    };
    std::vector<std::string> candidates;
    for (auto& [id, m] : bundledManifests_) candidates.push_back(id);
    for (auto& [id, r] : state_.sets) candidates.push_back(id);

    for (int round = 0; round < 8; ++round) {
        auto vs = find_violations(chosen());
        if (vs.empty()) break;
        auto v = vs.front();
        bool fixed = false;
        if (v.fixable) {
            std::string best;
            std::optional<Version> bestV;
            std::string bestFam;
            for (auto& id : candidates) {
                auto* m = load(id);
                if (!m) continue;
                auto* comp = m->find(v.needId);
                if (!comp || (!v.needRelease.empty() && comp->release != v.needRelease)) continue;
                if (std::string w; !usable(id, &api, w)) continue;
                auto lk = state_.families.find(m->family);
                if (lk != state_.families.end() && !lk->second.lock.empty() && lk->second.lock != id) continue;
                auto ver = parse_version(m->version);
                if (!bestV || ver->compare(*bestV) > 0) { best = id; bestV = ver; bestFam = m->family; }
            }
            if (!best.empty() && selection[bestFam] != best && !touched.count(bestFam)) {
                selection[bestFam] = best;
                touched.insert(bestFam);
                plan.changes.push_back({bestFam, best, v.dependentComponent + " needs " + v.needId + (v.needRelease.empty() ? "" : " release " + v.needRelease)});
                fixed = true;
            }
        }
        if (!fixed) {
            std::vector<std::string> texts;
            for (auto& x : vs) texts.push_back(x.text);
            std::string lockNote;
            if (v.fixable) {
                for (auto& [n, fs2] : state_.families)
                    if (!fs2.lock.empty()) lockNote += " Family " + n + " is locked to " + fs2.lock + " and was not changed.";
            }
            return fail("Refusing this combination: " + join(texts, " | ") + lockNote);
        }
    }
    if (auto vs = find_violations(chosen()); !vs.empty()) {
        std::vector<std::string> texts;
        for (auto& x : vs) texts.push_back(x.text);
        return fail("Refusing this combination: " + join(texts, " | "));
    }
    for (auto& c : plan.changes) {
        auto pit = state_.families.find(c.family);
        if (pit != state_.families.end() && pit->second.pending && pit->second.pending->boots >= 1) return fail("A trial of " + pit->second.pending->set + " is in progress for " + c.family + ". Confirm or revert it before choosing another version.");
    }
    auto base = intended();
    std::vector<PlanChange> kept;
    for (auto& c : plan.changes) if (base[c.family] != c.setId || c.family == family) kept.push_back(c);
    plan.changes = std::move(kept);
    for (auto& c : plan.changes) {
        if (c.why != "requested")
            plan.explanations.push_back("Linked update: " + c.family + " also moves to " + c.setId + " because " + c.why + ". Matched components update together (Starlight conservative policy unless the manifest marks the rule vendor-documented).");
    }
    plan.ok = true;
    plan.message = plan.changes.size() > 1 ? "Activation plan includes " + std::to_string(plan.changes.size() - 1) + " linked change(s)." : "Activation plan ready.";
    return plan;
}

ActivationPlan Store::plan_activation(const std::string& family, const std::string& setId, const EngineApi& api) const { return solve(family, setId, api, false); }

void Store::apply_plan(State& s, const ActivationPlan& plan, bool forceDirect) const {
    for (auto& c : plan.changes) {
        auto& f = s.families[c.family];
        bool known = bundledManifests_.count(c.setId) || c.setId == f.lastKnownGood || c.setId == f.previous || c.setId == f.active;
        if (forceDirect || known) {
            if (auto base = baseline(c.family, f); c.setId != base) f.previous = base;
            f.active = c.setId;
            f.pending.reset();
        } else {
            f.pending = Pending{c.setId, baseline(c.family, f), 0};
        }
        s.events.push_back(c.family + ": " + (f.pending ? "will try " : "now selects ") + c.setId + " at next launch (" + c.why + ").");
    }
}

ActivationPlan Store::activate(const std::string& family, const std::string& setId, const EngineApi& api) {
    auto plan = solve(family, setId, api, false);
    if (!plan.ok || plan.changes.empty()) return plan;
    if (options_.fault) options_.fault("activate.planned");
    State s = state_;
    apply_plan(s, plan, false);
    save(std::move(s));
    plan.message = "Selected for the next launch; nothing already loaded was touched.";
    return plan;
}

ActivationPlan Store::restore_previous(const std::string& family, const EngineApi& api) {
    ActivationPlan plan;
    auto it = state_.families.find(family);
    if (it == state_.families.end() || it->second.previous.empty()) { plan.message = "There is no previous version to restore for " + family + "."; return plan; }
    if (!it->second.lock.empty()) { plan.message = family + " is locked to " + it->second.lock + ". Unlock it first."; return plan; }
    plan = solve(family, it->second.previous, api, true);
    if (!plan.ok) return plan;
    if (options_.fault) options_.fault("activate.planned");
    State s = state_;
    apply_plan(s, plan, true);
    save(std::move(s));
    plan.message = "Restored the previous version for the next launch.";
    return plan;
}

ActivationPlan Store::restore_stable(const std::string& family, const EngineApi& api) {
    ActivationPlan plan;
    auto fit = state_.families.find(family);
    if (fit != state_.families.end() && !fit->second.lock.empty()) { plan.message = family + " is locked to " + fit->second.lock + ". Unlock it first."; return plan; }
    std::string best;
    std::optional<Version> bestV;
    for (auto& si : list_sets()) {
        if (si.family != family || si.channel != "stable" || si.trust == "local") continue;
        std::string why;
        if (!usable(si.setId, &api, why)) continue;
        auto v = parse_version(si.version);
        if (!bestV || v->compare(*bestV) > 0) { best = si.setId; bestV = v; }
    }
    if (best.empty()) { plan.message = "No Starlight-verified stable set is installed for " + family + "."; return plan; }
    State s = state_;
    s.families[family].channel = Channel::Stable;
    plan = solve(family, best, api, false);
    if (!plan.ok) return plan;
    apply_plan(s, plan, false);
    if (options_.fault) options_.fault("activate.planned");
    save(std::move(s));
    plan.message = "Stable channel restored; " + best + " is selected for the next launch.";
    return plan;
}

Result Store::lock(const std::string& family) {
    auto fit = state_.families.find(family);
    if (fit != state_.families.end()) {
        if (!fit->second.lock.empty()) return Result::good(family + " is already locked to " + fit->second.lock + ".");
        if (fit->second.pending) return Result::fail("A trial of " + fit->second.pending->set + " is in progress. Finish or revert it before locking.");
    }
    auto res = resolve();
    auto* rf = res.find(family);
    if (!rf || rf->setId.empty()) return Result::fail("There is no usable version of " + family + " to lock.");
    State s = state_;
    s.families[family].lock = rf->setId;
    s.events.push_back(family + ": locked to " + rf->setId + " " + rf->version + "; vendor updates disabled for it.");
    save(std::move(s));
    return Result::good("Locked " + family + " to " + rf->version + ".");
}
Result Store::unlock(const std::string& family) {
    auto fit = state_.families.find(family);
    if (fit == state_.families.end() || fit->second.lock.empty()) return Result::good(family + " is not locked.");
    State s = state_;
    auto& f = s.families[family];
    if (f.active.empty()) f.active = f.lock;
    s.events.push_back(family + ": unlocked by the user.");
    f.lock.clear();
    save(std::move(s));
    return Result::good("Unlocked " + family + ".");
}
Result Store::set_channel(const std::string& family, Channel c) {
    State s = state_;
    s.families[family].channel = c;
    save(std::move(s));
    return Result::good(family + " follows the " + channel_name(c) + " channel.");
}
Result Store::set_mode(const std::string& mode) {
    if (mode != "automatic" && mode != "manual") return Result::fail("Update mode must be automatic or manual.");
    State s = state_;
    s.updateMode = mode;
    save(std::move(s));
    return Result::good("Update mode is " + mode + ".");
}
Result Store::set_ota(const std::string& family, bool allow) {
    State s = state_;
    s.families[family].otaAllowed = allow;
    bool locked = !s.families[family].lock.empty();
    save(std::move(s));
    return Result::good(std::string("Vendor update policy for ") + family + ": " + (allow && !locked ? "allowed" : "denied") + (allow && locked ? " (locked, so it stays denied)" : "") + ".");
}

Result Store::apply_catalog(const Catalog& c) {
    if (c.serial < state_.catalogSerial) return Result::fail("Catalog is older than the one already accepted (serial " + std::to_string(c.serial) + " < " + std::to_string(state_.catalogSerial) + "); refusing a rollback.");
    State s = state_;
    s.catalogSerial = c.serial;
    for (auto& r : c.revoked) {
        if (std::find(s.revoked.begin(), s.revoked.end(), r) == s.revoked.end()) s.revoked.push_back(r);
        for (auto& [n, f] : s.families)
            if (f.lock == r) s.events.push_back(n + ": the locked set " + r + " was revoked by the catalog. The lock is kept; please review.");
    }
    save(std::move(s));
    return Result::good();
}

Candidate Store::candidate(const Catalog& cat, const std::string& family, Channel channel, const EngineApi& api) const {
    Candidate out;
    FamilyState fs0;
    if (auto it = state_.families.find(family); it != state_.families.end()) fs0 = it->second;
    auto res = resolve();
    const ResolvedFamily* cur = res.find(family);
    std::optional<Version> curV;
    std::string curFamily;
    int curMajor = 0;
    if (cur && !cur->setId.empty()) {
        curV = parse_version(cur->version);
        if (auto m = manifest_any(cur->setId)) { curFamily = m->components.front().api_family; curMajor = m->components.front().api_major; }
    }
    if (!fs0.lock.empty()) {
        out.kind = CandidateKind::Locked;
        out.reason = "Locked to " + (cur ? cur->version : fs0.lock) + " until you unlock it.";
        return out;
    }
    const CatalogEntry* best = nullptr;
    std::optional<Version> bestV;
    int incompatible = 0;
    for (auto& e : cat.entries) {
        if (e.family != family) continue;
        if (channel == Channel::Stable && e.channel != Channel::Stable) continue;
        if (std::find(cat.revoked.begin(), cat.revoked.end(), e.set_id) != cat.revoked.end()) continue;
        if (std::find(state_.revoked.begin(), state_.revoked.end(), e.set_id) != state_.revoked.end()) continue;
        auto v = parse_version(e.version);
        if (!v) continue;
        if (auto ms = cat.min_safe.find(family); ms != cat.min_safe.end() && v->compare(*parse_version(ms->second)) < 0) continue;
        auto am = api.major.find(e.api_family);
        if (am == api.major.end() || am->second != e.api_major) { ++incompatible; continue; }
        if (!bestV || v->compare(*bestV) > 0) { best = &e; bestV = v; }
    }
    if (!best) {
        out.reason = incompatible ? std::to_string(incompatible) + " release(s) need a different API major than this build supports and are not offered." :
                     std::string("No compatible ") + channel_name(channel) + " candidate is published for this family.";
        return out;
    }
    if (curV && bestV->compare(*curV) <= 0) {
        out.kind = CandidateKind::UpToDate;
        out.reason = std::string("Already on the newest compatible ") + channel_name(channel) + " release.";
        return out;
    }
    out.kind = CandidateKind::Available;
    out.setId = best->set_id;
    out.version = best->version;
    out.manualRequired = !best->auto_ok || best->channel != Channel::Stable || (curMajor && (curMajor != best->api_major || curFamily != best->api_family));
    out.autoEligible = state_.updateMode == "automatic" && channel == Channel::Stable && !out.manualRequired;
    out.reason = out.manualRequired ? "Available, but applying it is a manual step." : "Available.";
    return out;
}

std::vector<std::string> Store::auto_update(const Catalog& cat, PackageSource& source, const EngineApi& api) {
    std::vector<std::string> log;
    if (state_.updateMode != "automatic") { log.push_back("Automatic updates are not enabled; nothing was changed."); return log; }
    std::set<std::string> families;
    for (auto& e : cat.entries) families.insert(e.family);
    for (auto& f : families) {
        FamilyState fs0;
        if (auto it = state_.families.find(f); it != state_.families.end()) fs0 = it->second;
        if (!fs0.lock.empty()) { log.push_back(f + ": locked, skipped."); continue; }
        if (fs0.channel != Channel::Stable) { log.push_back(f + ": follows the experimental channel, which is manual only."); continue; }
        auto c = candidate(cat, f, Channel::Stable, api);
        if (c.kind != CandidateKind::Available || !c.autoEligible) { log.push_back(f + ": " + c.reason); continue; }
        const CatalogEntry* entry = nullptr;
        for (auto& e : cat.entries) if (e.set_id == c.setId) entry = &e;
        auto pkg = source.fetch(*entry);
        if (!pkg) { log.push_back(f + ": package for " + c.setId + " is not available locally."); continue; }
        auto sr = fs::is_directory(*pkg) ? stage_folder(*pkg, StageTrust::Catalog, entry) : stage_zip(*pkg, StageTrust::Catalog, entry);
        if (!sr.ok) { log.push_back(f + ": " + sr.message); continue; }
        auto ar = activate(f, c.setId, api);
        log.push_back(f + ": " + (ar.ok ? "selected " + c.setId + " for the next launch (trial)." : ar.message));
    }
    return log;
}

Inventory Store::inventory() const {
    Inventory inv;
    std::map<std::string, std::string> why;
    auto add = [&](const std::string& id, const std::string& r) { if (id.empty()) return; auto& w = why[id]; w += (w.empty() ? "" : ", ") + r; };
    for (auto& [n, f] : state_.families) {
        add(f.active, "active"); add(f.previous, "previous"); add(f.lastKnownGood, "last known good"); add(f.lock, "pinned");
        if (f.pending) { add(f.pending->set, "pending trial"); add(f.pending->from, "trial origin"); }
    }
    for (auto& id : loaded_) add(id, "loaded by this process");
    for (auto& [f, id] : intended()) add(id, "selected (with the sets it links to)");
    std::error_code ec;
    for (auto& e : fs::directory_iterator(options_.root / "sets", ec)) {
        if (!e.is_directory()) continue;
        auto name = utf8(e.path().filename());
        if (!fs::exists(e.path() / "set.json")) { inv.keep.push_back(name); inv.reason[name] = "not a Starlight set folder; never touched"; continue; }
        if (why.count(name)) { inv.keep.push_back(name); inv.reason[name] = why[name]; }
        else if (!state_.sets.count(name)) { inv.removable.push_back(name); inv.reason[name] = "unrecorded folder left by an interrupted stage"; }
        else { inv.removable.push_back(name); inv.reason[name] = "not referenced by any selection"; }
    }
    return inv;
}

Result Store::prune(bool confirm) {
    if (auto kept = kept_damaged_state(options_.root); !kept.empty()) return Result::fail("Prune refused: a damaged state file was kept (" + join(kept, ", ") + "), so installed sets may look unrecorded. Review it first.");
    auto inv = inventory();
    if (!confirm) return Result::fail("Dry run: " + std::to_string(inv.removable.size()) + " set folder(s) could be removed (" + join(inv.removable, ", ") + "). Run with --confirm to remove them.");
    State s = state_;
    for (auto& id : inv.removable) s.sets.erase(id);
    save(std::move(s));
    std::error_code ec;
    for (auto& id : inv.removable) fs::remove_all(options_.root / "sets" / from_utf8(id), ec);
    return Result::good("Removed " + std::to_string(inv.removable.size()) + " unreferenced set folder(s).");
}

std::string Store::status_json(const std::optional<Catalog>& cat, const EngineApi& api) const {
    auto res = resolve();
    json j{{"update_mode", state_.updateMode}, {"first_launch_choice_needed", state_.updateMode == "unset"}, {"events", state_.events},
           {"warnings", res.warnings}, {"categories", {{"upscaling", json::array()}, {"frame-generation", json::array()}, {"latency-reduction", json::array()}}}};
    for (auto& rf : res.families) {
        FamilyState f;
        if (auto it = state_.families.find(rf.family); it != state_.families.end()) f = it->second;
        json fj{{"family", rf.family}, {"channel", channel_name(f.channel)}, {"loaded", {{"set", rf.setId}, {"version", rf.version}, {"source", rf.source}}},
                {"locked", !f.lock.empty()}, {"lock_set", f.lock}, {"ota_effective", rf.otaAllowed}, {"ota_policy_allows", f.otaAllowed},
                {"previous", f.previous}, {"last_known_good", f.lastKnownGood}, {"installed", json::array()}, {"linked", json::array()}};
        if (f.pending) fj["pending"] = {{"set", f.pending->set}, {"boots", f.pending->boots}};
        for (auto& si : list_sets()) if (si.family == rf.family) fj["installed"].push_back({{"set", si.setId}, {"version", si.version}, {"channel", si.channel}, {"trust", si.trust}, {"bad", si.bad}});
        if (!rf.setId.empty()) {
            auto m = *manifest_any(rf.setId);
            for (auto& c : m.components)
                for (auto& r : c.requires_)
                    for (auto& other : res.families) {
                        if (other.family == rf.family || other.setId.empty()) continue;
                        auto om = manifest_any(other.setId);
                        if (om && om->find(r.id)) fj["linked"].push_back("Moves together with " + other.family + " (" + other.version + ") because " + c.id + " needs " + r.id + " [" + origin_label(r.origin) + "]");
                    }
        }
        if (cat) {
            json cj = json::object();
            for (auto ch : {Channel::Stable, Channel::Experimental}) {
                auto c = candidate(*cat, rf.family, ch, api);
                static const char* kinds[] = {"none", "up-to-date", "available", "locked"};
                cj[channel_name(ch)] = {{"state", kinds[static_cast<int>(c.kind)]}, {"set", c.setId}, {"version", c.version}, {"reason", c.reason}, {"manual_required", c.manualRequired}, {"auto_eligible", c.autoEligible}};
            }
            fj["candidates"] = std::move(cj);
        }
        j["categories"][rf.category.empty() ? "upscaling" : rf.category].push_back(std::move(fj));
    }
    return j.dump(2, ' ', false, json::error_handler_t::replace);
}

}
