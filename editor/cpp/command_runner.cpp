#include "commands.hpp"
#include "adventure_template.hpp"
#include <iostream>
using namespace starlight::editor;
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
        else if (args.size() == 2 && args[0] == L"--feature") {
            report = invoke_feature(utf8(args[1]));
            code = 1;
        } else if (args.size() == 2 && args[0] == L"--commands")
            report = apply_commands(args[1]);
        else
            throw std::runtime_error(
                "Use --describe, --inspect <scene>, --commands <request>, --new-adventure <new-scene>, --export-portable <scene> <new-folder>, or --export-game <scene> <new-folder> <runtime-folder>; optional --result <report>");
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
