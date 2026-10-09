#include "../../deep/engine/crash_diagnostics.h"
#include "platform.hpp"
using namespace starlight::editor;
int wmain(int argc, wchar_t **argv) try {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--crash-child") {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        starlight_diagnostics::initialize(L"NativeEditor");
        RaiseException(0xe0425354, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 9;
    }
    auto output = argc > 1 ? fs::path(argv[1]) : fs::path(L"diagnostic-checks");
    fs::create_directories(output);
    wchar_t exe[32768]; GetModuleFileNameW(nullptr, exe, 32768);
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --crash-child";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    const auto started = fs::file_time_type::clock::now();
    if (!CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process)) return 2;
    Handle child(process.hProcess), thread(process.hThread);
    if (WaitForSingleObject(child.value, 15000) != WAIT_OBJECT_0) {
        TerminateProcess(child.value, 124); return 3;
    }
    auto directory = application_data() / L"Diagnostics/NativeEditor";
    fs::path dump;
    auto prefix = L"Starlight." + std::to_wstring(process.dwProcessId) + L".";
    std::error_code inventoryError, timeError;
    for (const auto &entry : fs::directory_iterator(directory, inventoryError))
        if (entry.path().filename().wstring().starts_with(prefix) && entry.path().extension() == L".dmp" &&
            entry.last_write_time(timeError) >= started)
            dump = entry.path();
    bool valid = !dump.empty() && read_file(dump).starts_with("MDMP");
    diagnostic("native diagnostic regression", valid ? "Controlled child crash verified" : "Controlled child crash not verified");
    atomic_write(output / "diagnostics.json", json{{"success",valid},{"dump",utf8(dump.wstring())},
        {"childPid",process.dwProcessId},{"controlledException","0xe0425354"},
        {"directory",utf8(directory.wstring())},{"inventoryError",inventoryError.message()}}.dump(2));
    return valid ? 0 : 1;
} catch(const std::exception& error) {
    fprintf(stderr,"Diagnostic check failed: %s\n",error.what());
    return 1;
}
