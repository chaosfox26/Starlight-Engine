#include "projects.hpp"
namespace starlight::editor {
namespace {
std::wstring quote(std::wstring_view s) {
    std::wstring result = L"\"";
    size_t n = 0;
    for (auto c : s) {
        if (c == L'\\') {
            ++n;
            continue;
        }
        result.append(c == L'"' ? n * 2 + 1 : n, L'\\');
        n = 0;
        result += c;
    }
    result.append(n * 2, L'\\');
    return result + L"\"";
}
void process(const fs::path &engine, const std::vector<std::wstring> &args, bool wait) {
    std::wstring command = quote(engine.wstring());
    for (const auto &a : args)
        command += L" " + quote(a);
    STARTUPINFOW startup{sizeof(startup)};
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = wait ? SW_HIDE : SW_SHOWNORMAL;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(engine.c_str(), command.data(), nullptr, nullptr, FALSE, wait ? CREATE_NO_WINDOW : 0,
                        nullptr, engine.parent_path().c_str(), &startup, &pi))
        throw std::runtime_error("Cannot launch native engine: " + std::to_string(GetLastError()));
    Handle thread(pi.hThread), child(pi.hProcess);
    if (wait) {
        DWORD status = WaitForSingleObject(child.value, 20000);
        if (status == WAIT_TIMEOUT) {
            TerminateProcess(child.value, 124);
            WaitForSingleObject(child.value, 5000);
            throw std::runtime_error("Preset check timed out");
        }
        if (status != WAIT_OBJECT_0)
            throw std::runtime_error("Cannot wait for native settings check");
        DWORD code = 1;
        if (!GetExitCodeProcess(child.value, &code) || code)
            throw std::runtime_error("Native preset check rejected settings");
    }
}
} // namespace
ProjectData ProjectData::load(const fs::path &source) {
    auto data = json::parse(read_file(source));
    if (!data.is_object() || native_int(data.at("schemaVersion")) != 3)
        throw std::runtime_error("Project requires schema 3");
    const auto &entries = data.at("scenes");
    if (!entries.is_object() || entries.empty())
        throw std::runtime_error("Project scenes must be a nonempty object");
    ProjectData result;
    result.path = fs::absolute(source).lexically_normal();
    result.selected = data.at("defaultScene").get<std::string>();
    for (auto it = entries.begin(); it != entries.end(); ++it)
        result.scenes.push_back(it.key());
    if (std::find(result.scenes.begin(), result.scenes.end(), result.selected) == result.scenes.end())
        throw std::runtime_error("Project default scene is missing");
    return result;
}
ResolvedPreset resolve_preset(const fs::path &engine, const ProjectData &project, const fs::path &source) {
    if (std::find(project.scenes.begin(), project.scenes.end(), project.selected) == project.scenes.end())
        throw std::runtime_error("Unknown project scene");
    auto folder = application_data() / L"Diagnostics" / L"NativeEditor" / L"presets" / wide(new_id());
    fs::create_directories(folder);
    ResolvedPreset result;
    result.path = folder / L"settings.startest";
    result.plan = folder / L"plan.json";
    auto error = folder / L"launch-error.json";
    atomic_write(result.path, read_file(source));
    try {
        process(engine,
                {project.path.wstring(), L"--preset", result.path.wstring(), L"--scene",
                 wide(project.selected), L"--error", error.wstring(), L"--plan", result.plan.wstring()},
                true);
    } catch (...) {
        if (fs::exists(error))
            throw std::runtime_error(read_file(error));
        throw;
    }
    if (!fs::exists(result.plan))
        throw std::runtime_error("Native engine did not produce a plan");
    result.data = json::parse(read_file(result.path));
    if (result.data.at("schemaVersion") != 2)
        throw std::runtime_error("Preset conversion did not produce schema 2");
    return result;
}
void launch_project(const fs::path &engine, const ProjectData &project, const fs::path &preset) {
    process(engine,
            {project.path.wstring(), L"--preset", preset.wstring(), L"--scene", wide(project.selected)},
            false);
}
} // namespace starlight::editor
