#include "graphics_runtime.hpp"
#include "../src/json.hpp"
#include <iostream>
#include <sstream>

namespace starlight::gfxrt {
using json = nlohmann::json;

namespace {
struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    std::set<std::string> flags;
};
const std::set<std::string> kValueOptions{"--root", "--bundled", "--engine-api", "--catalog", "--trust", "--packages", "--code", "--description", "--diagnostics-file", "--save", "--portable", "--app"};

Args parse_args(const std::vector<std::string>& v, std::size_t from) {
    Args a;
    for (std::size_t i = from; i < v.size(); ++i) {
        if (v[i].rfind("--", 0) != 0) { a.positional.push_back(v[i]); continue; }
        if (kValueOptions.count(v[i])) {
            if (i + 1 >= v.size()) throw GrError("Option " + v[i] + " needs a value");
            auto key = v[i];
            a.options[key] = v[++i];
        } else {
            a.flags.insert(v[i]);
        }
    }
    return a;
}

std::vector<std::uint8_t> read_hex_file(const fs::path& p) {
    auto t = read_text(p);
    while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
    std::vector<std::uint8_t> out;
    auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (std::size_t i = 0; i + 1 < t.size(); i += 2) out.push_back(static_cast<std::uint8_t>(nib(t[i]) * 16 + nib(t[i + 1])));
    return out;
}

int report(std::ostream& out, std::ostream& err, const Result& r) {
    (r.ok ? out : err) << r.message << "\n";
    return r.ok ? 0 : 1;
}
int report(std::ostream& out, std::ostream& err, const ActivationPlan& p) {
    (p.ok ? out : err) << p.message << "\n";
    for (auto& c : p.changes) out << "  " << c.family << " -> " << c.setId << (c.why == "requested" ? "" : "  (linked: " + c.why + ")") << "\n";
    for (auto& e : p.explanations) out << "  " << e << "\n";
    return p.ok ? 0 : 1;
}
}

int cli_main(const std::vector<std::string>& argv, std::ostream& out, std::ostream& err) {
    try {
        if (argv.empty() || argv[0] == "help" || argv[0] == "--help") {
            out << "Starlight.GraphicsRuntime <command> [options]\n"
                   "  Location: --root <dir> | --portable <adjacent dir> | --app <name> (per-user), plus --bundled <dir>\n"
                   "  Other: --engine-api sl=2,ffx=2,xess=3  --catalog <file>  --trust <keyId>:<public-key-hex-file>\n"
                   "Commands: status, list, stage <package> [--allow-local], activate <family> <set>, restore-previous <family>,\n"
                   "  restore-stable <family>, lock <family>, unlock <family>, set-channel <family> stable|experimental,\n"
                   "  set-mode automatic|manual, set-ota <family> allow|deny, begin-launch, confirm-good, report-failure <why>,\n"
                   "  check-catalog, auto-update --packages <dir>, export-plan <feature,...>, prune [--confirm],\n"
                   "  report-preview [--code C] [--description T] [--include-diagnostics --diagnostics-file F] [--save out.json]\n";
            return argv.empty() ? 2 : 0;
        }
        const std::string& cmd = argv[0];
        auto a = parse_args(argv, 1);
        StoreOptions o;
        if (a.options.count("--root")) o.root = from_utf8(a.options["--root"]);
        else if (a.options.count("--portable")) o.root = portable_root(from_utf8(a.options["--portable"]));
        else if (a.options.count("--app")) o.root = default_installed_root(a.options["--app"]);
        else { err << "Choose where the managed folder lives: --root, --portable or --app.\n"; return 2; }
        if (a.options.count("--bundled")) o.bundledRoot = from_utf8(a.options["--bundled"]);
        EngineApi api = a.options.count("--engine-api") ? EngineApi::parse(a.options["--engine-api"]) : EngineApi{};
        Store store(o);

        TrustStore trust;
        if (a.options.count("--trust")) {
            auto spec = a.options["--trust"];
            auto colon = spec.find(':');
            if (colon == std::string::npos || colon == 0) throw GrError("--trust needs keyId:path-to-public-key-hex-file");
            trust.keys[spec.substr(0, colon)] = read_hex_file(from_utf8(spec.substr(colon + 1)));
        }
        auto load_catalog = [&]() -> std::optional<Catalog> {
            if (!a.options.count("--catalog")) return std::nullopt;
            FileCatalogProvider prov(from_utf8(a.options["--catalog"]));
            auto text = prov.fetch();
            if (!text) { err << "Catalog is not available; the installed versions are kept.\n"; return std::nullopt; }
            auto r = verify_catalog(*text, trust);
            if (!r.ok) { err << r.message << "\n"; return std::nullopt; }
            return r.catalog;
        };
        auto need = [&](std::size_t n, const char* usage) {
            if (a.positional.size() < n) throw GrError(std::string("Usage: ") + usage);
        };

        if (cmd == "status") {
            auto cat = load_catalog();
            if (cat) { auto r = store.apply_catalog(*cat); if (!r.ok) err << r.message << "\n"; }
            out << store.status_json(cat, api) << "\n";
            return 0;
        }
        if (cmd == "list") {
            for (auto& s : store.list_sets())
                out << s.setId << "  family=" << s.family << "  version=" << s.version << "  channel=" << s.channel << "  trust=" << s.trust << (s.bad ? "  BAD" : "") << "\n";
            return 0;
        }
        if (cmd == "stage") {
            need(1, "stage <package folder or .zip> [--allow-local]");
            auto pkg = from_utf8(a.positional[0]);
            bool zip = !fs::is_directory(pkg);
            std::string text;
            fs::path tmp;
            std::optional<CatalogEntry> entry;
            if (auto cat = load_catalog()) {
                std::string setId;
                if (!zip) setId = parse_manifest(read_text(pkg / "set.json")).set_id;
                else setId = utf8(pkg.stem());
                for (auto& e : cat->entries) if (e.set_id == setId) entry = e;
            }
            if (entry) return report(out, err, zip ? store.stage_zip(pkg, StageTrust::Catalog, &*entry) : store.stage_folder(pkg, StageTrust::Catalog, &*entry));
            if (!a.flags.count("--allow-local")) { err << "This package is not in a verified catalog. Add --allow-local to stage it as an unsigned local package (never offered through channels).\n"; return 1; }
            return report(out, err, zip ? store.stage_zip(pkg, StageTrust::Local) : store.stage_folder(pkg, StageTrust::Local));
        }
        if (cmd == "activate") { need(2, "activate <family> <set>"); return report(out, err, store.activate(a.positional[0], a.positional[1], api)); }
        if (cmd == "restore-previous") { need(1, "restore-previous <family>"); return report(out, err, store.restore_previous(a.positional[0], api)); }
        if (cmd == "restore-stable") { need(1, "restore-stable <family>"); return report(out, err, store.restore_stable(a.positional[0], api)); }
        if (cmd == "lock") { need(1, "lock <family>"); return report(out, err, store.lock(a.positional[0])); }
        if (cmd == "unlock") { need(1, "unlock <family>"); return report(out, err, store.unlock(a.positional[0])); }
        if (cmd == "set-channel") {
            need(2, "set-channel <family> stable|experimental");
            auto ch = parse_channel(a.positional[1]);
            if (!ch) { err << "Channel must be stable or experimental.\n"; return 2; }
            return report(out, err, store.set_channel(a.positional[0], *ch));
        }
        if (cmd == "set-mode") { need(1, "set-mode automatic|manual"); return report(out, err, store.set_mode(a.positional[0])); }
        if (cmd == "set-ota") {
            need(2, "set-ota <family> allow|deny");
            if (a.positional[1] != "allow" && a.positional[1] != "deny") { err << "Use allow or deny.\n"; return 2; }
            return report(out, err, store.set_ota(a.positional[0], a.positional[1] == "allow"));
        }
        if (cmd == "begin-launch") { out << store.begin_launch().to_json() << "\n"; return 0; }
        if (cmd == "confirm-good") return report(out, err, store.confirm_good());
        if (cmd == "report-failure") { need(1, "report-failure <reason>"); return report(out, err, store.report_launch_failure(a.positional[0])); }
        if (cmd == "check-catalog") {
            auto cat = load_catalog();
            if (!cat) return 1;
            auto r = store.apply_catalog(*cat);
            if (!r.ok) { err << r.message << "\n"; return 1; }
            out << store.status_json(cat, api) << "\n";
            return 0;
        }
        if (cmd == "auto-update") {
            auto cat = load_catalog();
            if (!cat) return 1;
            if (!a.options.count("--packages")) { err << "auto-update needs --packages <dir> (local packages only; nothing is downloaded).\n"; return 2; }
            auto r = store.apply_catalog(*cat);
            if (!r.ok) { err << r.message << "\n"; return 1; }
            LocalDirectoryPackageSource src(from_utf8(a.options["--packages"]));
            for (auto& line : store.auto_update(*cat, src, api)) out << line << "\n";
            return 0;
        }
        if (cmd == "export-plan") {
            need(1, "export-plan <feature,feature,...>");
            std::set<std::string> feats;
            std::stringstream ss(a.positional[0]);
            for (std::string f; std::getline(ss, f, ',');) feats.insert(f);
            auto plan = plan_export(store, feats);
            out << plan.message << "\n";
            for (auto& f : plan.files) out << "  " << f.relativePath << "  [" << f.componentId << (f.licence ? ", licence" : "") << "]\n";
            return plan.ok ? 0 : 1;
        }
        if (cmd == "prune") return report(out, err, store.prune(a.flags.count("--confirm") > 0));
        if (cmd == "report-preview") {
            ReportInput in;
            in.failureCode = a.options.count("--code") ? a.options["--code"] : "";
            in.description = a.options.count("--description") ? a.options["--description"] : "";
            if (a.flags.count("--include-diagnostics")) {
                if (!a.options.count("--diagnostics-file")) { err << "--include-diagnostics needs --diagnostics-file <file>.\n"; return 2; }
                in.includeDiagnostics = true;
                in.diagnostics = read_text(from_utf8(a.options["--diagnostics-file"]));
            }
            auto rep = build_report(store.resolve(), in);
            out << rep.preview;
            if (a.options.count("--save")) return report(out, err, save_report(rep, from_utf8(a.options["--save"])));
            return 0;
        }
        err << "Unknown command '" << cmd << "'. Run with --help.\n";
        return 2;
    } catch (const GrError& e) {
        err << e.what() << "\n";
        return 2;
    } catch (const std::exception& e) {
        err << "Error: " << e.what() << "\n";
        return 1;
    }
}

}
