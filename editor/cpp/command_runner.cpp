#include "commands.hpp"
#include "adventure_template.hpp"
#include "assets.hpp"
#include "engine_assets.hpp"
#include <iostream>
using namespace starlight::editor;
namespace {
json asset_root_report(const std::string &action) {
    json list = json::array();
    bool healthy = true;
    for (const auto &status : EngineAssets::root_status()) {
        if (!status.Problem.empty()) healthy = false;
        list.push_back({{"path", utf8(status.Path.wstring())},
                        {"origin", status.Origin},
                        {"role", status.Primary ? "primary writable" : status.Writable ? "writable" : "read-only"},
                        {"label", status.Label},
                        {"present", status.Present},
                        {"usable", status.Usable},
                        {"problem", status.Problem}});
    }
    return {{"success", true}, {"action", action}, {"healthy", healthy}, {"registryFile", utf8(EngineAssets::registry_file().wstring())}, {"roots", list}, {"text", EngineAssets::describe_roots()}};
}
json asset_root_command(const std::vector<std::wstring> &args) {
    const auto command = utf8(args[0]);
    if (command == "--asset-root-list" && args.size() == 1) return asset_root_report("list");
    if (args.size() < 2) throw std::runtime_error(command + " needs a folder");
    bool writable = false;
    std::string label;
    for (size_t i = 2; i < args.size(); ++i) {
        if (command == "--asset-root-remove") throw std::runtime_error("--asset-root-remove takes only the folder");
        if (args[i] == L"--writable") writable = true;
        else if (args[i] == L"--read-only") writable = false;
        else if (args[i] == L"--label" && i + 1 < args.size()) label = utf8(args[++i]);
        else throw std::runtime_error("Unknown option " + utf8(args[i]));
    }
    const auto folder = fs::absolute(args[1]);
    if (command == "--asset-root-add") EngineAssets::register_root(folder, writable, label);
    else if (command == "--asset-root-init") EngineAssets::initialize_root(folder, writable, label);
    else if (command == "--asset-root-remove") {
        if (!EngineAssets::unregister_root(folder)) throw std::runtime_error("The folder is not registered; nothing was changed");
    } else
        throw std::runtime_error("Unknown asset root command " + command);
    return asset_root_report(command.substr(2));
}
}
int starlight::editor::run_commands(int argc, wchar_t **argv) {
    fs::path result;
    json report;
    int code = 0;
    try {
        std::vector<std::wstring> args;
        for (int i = 1; i < argc; i++) {
            if (std::wstring_view(argv[i]) == L"--result") {
                if (++i >= argc)
                    throw std::runtime_error("Missing result path");
                result = argv[i];
            } else
                args.emplace_back(argv[i]);
        }
        if (args.size() == 1 && args[0] == L"--describe")
            report = describe_commands();
        else if (args.size() == 2 && args[0] == L"--inspect")
            report = inspect_scene(args[1]);
        else if (args.size() == 2 && args[0] == L"--new-adventure") {
            if(fs::exists(args[1]))throw std::runtime_error("Starter scene destination already exists");
            save_scene(adventure_template(),args[1]);report={{"success",true}};
        }
        else if (args.size() == 4 && args[0] == L"--export-game")
            report = export_playable_game(load_scene(args[1]), args[2], args[3]);
        else if (args.size() == 3 && args[0] == L"--export-portable")
            report = export_portable_scene(load_scene(args[1]), args[2]);
        else if (args.size() == 2 && args[0] == L"--verify-portable")
            report = verify_portable_scene(args[1]);
        else if (args.size() == 2 && args[0] == L"--import-model")
        {
            report = {{"success", true}, {"kind", "model"}, {"asset", import_model(fs::absolute(args[1]))}};
            if (const auto note = model_skipped_note(*load_model_bytes(report["asset"].get<std::string>())); !note.empty()) report["note"] = note;
        }
        else if (args.size() == 2 && args[0] == L"--import-texture")
            report = {{"success", true}, {"kind", "texture"}, {"asset", import_texture(fs::absolute(args[1]))}};
        else if (!args.empty() && args[0].starts_with(L"--asset-root-"))
            report = asset_root_command(args);
        else if (args.size() == 3 && args[0] == L"--import-blob")
            report = {{"success", true}, {"kind", "blob"}, {"type", utf8(args[1])}, {"asset", import_blob(fs::absolute(args[2]), utf8(args[1]))}};
        else if (args.size() == 2 && args[0] == L"--asset-ready") {
            const auto ready = check_asset_ready(utf8(args[1]));
            report = {{"success", ready.ready}, {"asset", utf8(args[1])}, {"detail", ready.detail}};
            code = ready.ready ? 0 : 1;
        } else if (args.size() == 3 && args[0] == L"--validate-atlas") {
            const auto count = validate_uv_atlas_for_model(utf8(args[1]), utf8(args[2]));
            report = {{"success", true}, {"atlas", utf8(args[1])}, {"model", utf8(args[2])}, {"vertices", count}};
        } else if (args.size() == 2 && args[0] == L"--feature") {
            report = invoke_feature(utf8(args[1]));
            code = 1;
        } else if (args.size() == 2 && args[0] == L"--commands")
            report = apply_commands(args[1]);
        else
            throw std::runtime_error(
                "Use --describe, --inspect <scene>, --commands <request>, --new-adventure <new-scene>, --import-model <file.glb>, --import-texture <file.png|jpg>, --import-blob <type> <file>, --asset-ready <asset-id>, --validate-atlas <blob-id> <model-id>, --asset-root-list, --asset-root-add|--asset-root-init <folder> [--writable] [--label <text>], --asset-root-remove <folder>, --export-portable <scene> <new-folder>, or --export-game <scene> <new-folder> <runtime-folder>; optional --result <report>");
    } catch (const std::exception &e) {
        report = {{"success", false}, {"error", e.what()}};
        diagnostic("command", e.what());
        code = 1;
    }
    try {
        auto text = report.dump(2);
        if (!result.empty())
            atomic_write(result, text);
        std::cout << text << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
    return code;
}
