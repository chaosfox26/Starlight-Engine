#include "platform.hpp"
#include <atomic>
#include <bcrypt.h>
#include <fstream>
#include <iomanip>
#include <shlobj.h>
#include <shobjidl.h>
#include <sstream>
#include <wrl/client.h>
namespace starlight::editor {
using Microsoft::WRL::ComPtr;
namespace {
fs::path native_file_path(const fs::path& path) {
    auto native=fs::absolute(path).lexically_normal().make_preferred().wstring();
    if(native.rfind(L"\\\\?\\",0)==0)return fs::path(native);
    return fs::path(native.rfind(L"\\\\",0)==0 ? L"\\\\?\\UNC\\"+native.substr(2) : L"\\\\?\\"+native);
}
void check(HRESULT hr) {
    if (FAILED(hr))
        throw std::runtime_error("Windows operation failed: " +
                                 std::to_string(static_cast<unsigned long>(hr)));
}
} // namespace
std::wstring wide(std::string_view s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n);
    return out;
}
std::string utf8(std::wstring_view s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0, nullptr,
                                nullptr);
    if (!n)
        throw std::runtime_error("Invalid Unicode path");
    std::string out(n, 0);
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), int(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}
std::string new_id() {
    GUID id{};
    check(CoCreateGuid(&id));
    wchar_t text[40];
    StringFromGUID2(id, text, 40);
    std::string out;
    for (auto c : std::wstring_view(text))
        if (c != '{' && c != '}' && c != '-')
            out += char(std::tolower(c));
    return out;
}
std::string read_file(const fs::path &p) {
    std::ifstream f(native_file_path(p), std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open " + utf8(p.wstring()));
    std::string data((std::istreambuf_iterator<char>(f)), {});
    if (f.bad())
        throw std::runtime_error("Read failed");
    return data;
}
std::string hash_file(const fs::path &p) {
    return hash_bytes(read_file(p));
}
std::string hash_bytes(std::string_view data) {
    unsigned char hash[32];
    if (data.size() > ULONG_MAX ||
        BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
                   ULONG(data.size()), hash, 32) < 0)
        throw std::runtime_error("SHA256 failed");
    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0');
    for (auto b : hash)
        out << std::setw(2) << int(b);
    return out.str();
}
void atomic_write(const fs::path &path, std::string_view data, bool backup,
                  const std::optional<std::string> &expected) {
    // Staging folders and unique temporary suffixes can exceed MAX_PATH even
    // when the user's chosen destination does not. Use the Unicode extended
    // path form for our Win32 file calls without depending on machine policy.
    auto p = native_file_path(path);
    fs::create_directories(p.parent_path());
    Handle lock(CreateFileW((p.wstring() + L".lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!lock)
        throw std::runtime_error("File is busy");
    if (expected && (!fs::exists(p) || _stricmp(hash_file(p).c_str(), expected->c_str())))
        throw std::runtime_error("File changed; reopen before saving");
    auto temp = fs::path(p.wstring() + L"." + wide(new_id()) + L".tmp");
    try {
        {
            Handle f(CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                 nullptr));
            if (!f)
                throw std::runtime_error("Cannot create temporary file (Windows error "+std::to_string(GetLastError())+")");
            size_t offset = 0;
            while (offset < data.size()) {
                DWORD written = 0, n = DWORD(std::min<size_t>(data.size() - offset, 1 << 20));
                if (!WriteFile(f.value, data.data() + offset, n, &written, nullptr) || !written)
                    throw std::runtime_error("Write failed");
                offset += written;
            }
            if (!FlushFileBuffers(f.value))
                throw std::runtime_error("File flush failed");
        }
        if (fs::exists(p) && backup) {
            auto previous = p.wstring() + L"." + wide(new_id()) + L".bak";
            if (!ReplaceFileW(p.c_str(), temp.c_str(), previous.c_str(), 0, nullptr, nullptr))
                throw std::runtime_error("Cannot replace file; original preserved");
        } else if (!MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot commit file");
    } catch (...) {
        std::error_code ec;
        fs::remove(temp, ec);
        throw;
    }
}
fs::path application_data() {
    PWSTR text = nullptr;
    check(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &text));
    fs::path p(text);
    CoTaskMemFree(text);
    return p / L"Starlight";
}
void diagnostic(std::string_view operation, std::string_view message) noexcept {
    try {
        auto p = application_data() / L"Diagnostics" / L"NativeEditor" / (wide(new_id()) + L".json");
        atomic_write(
            p, json{{"operation", operation}, {"message", message}, {"pid", GetCurrentProcessId()}}.dump(2));
    } catch (...) {
    }
}
void save_scene(const SceneData &scene, const fs::path &path, const std::optional<std::string> &expected) {
    auto saved = scene;
    const auto base = fs::absolute(path).parent_path().lexically_normal();
    for (auto &object : saved.Characters) {
        auto asset = fs::path(wide(object.Asset));
        if (asset.is_absolute()) {
            auto relative = asset.lexically_normal().lexically_relative(base);
            if (!relative.empty() && *relative.begin() != L"..")
                object.Asset = utf8(relative.generic_wstring());
        }
    }
    atomic_write(path, saved.encode(), true, expected);
}
SceneData load_scene(const fs::path &path) {
    auto scene = SceneData::decode(read_file(path));
    for (auto &object : scene.Characters) {
        if (object.Asset.empty()) continue;
        auto asset = fs::path(wide(object.Asset));
        if (asset.is_relative())
            object.Asset = utf8((fs::absolute(path).parent_path() / asset).lexically_normal().wstring());
    }
    return scene;
}
namespace {
fs::path shell_path(IShellItem *item) {
    PWSTR text = nullptr;
    check(item->GetDisplayName(SIGDN_FILESYSPATH, &text));
    fs::path p(text);
    CoTaskMemFree(text);
    return p;
}
class FolderEvents final : public IFileDialogEvents {
    std::atomic<ULONG> refs{1};

  public:
    fs::path folder;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (!out)
            return E_POINTER;
        if (id == IID_IUnknown || id == IID_IFileDialogEvents) {
            *out = static_cast<IFileDialogEvents *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        auto n = --refs;
        if (!n)
            delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE OnFileOk(IFileDialog *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFolderChanging(IFileDialog *, IShellItem *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFolderChange(IFileDialog *d) override {
        try {
            ComPtr<IShellItem> item;
            if (SUCCEEDED(d->GetFolder(&item)))
                folder = shell_path(item.Get());
        } catch (...) {
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSelectionChange(IFileDialog *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShareViolation(IFileDialog *, IShellItem *,
                                               FDE_SHAREVIOLATION_RESPONSE *r) override {
        *r = FDESVR_DEFAULT;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnTypeChange(IFileDialog *) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnOverwrite(IFileDialog *, IShellItem *, FDE_OVERWRITE_RESPONSE *r) override {
        *r = FDEOR_DEFAULT;
        return S_OK;
    }
};
} // namespace
std::vector<fs::path> file_dialog(HWND owner, std::string_view purpose,
                                  const std::vector<std::pair<std::wstring, std::wstring>> &filters,
                                  bool save, bool multiple, const fs::path &fallback, std::wstring_view title, bool folders) {
    ComPtr<IFileDialog> dialog;
    check(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                           IID_PPV_ARGS(&dialog)));
    DWORD flags = 0;
    check(dialog->GetOptions(&flags));
    flags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR;
    flags |= save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST;
    if (folders) flags |= FOS_PICKFOLDERS;
    if (multiple && !save)
        flags |= FOS_ALLOWMULTISELECT;
    check(dialog->SetOptions(flags));
    if (!title.empty()) check(dialog->SetTitle(std::wstring(title).c_str()));
    std::vector<COMDLG_FILTERSPEC> specs;
    for (const auto &f : filters)
        specs.push_back({f.first.c_str(), f.second.c_str()});
    if (!specs.empty())
        check(dialog->SetFileTypes(UINT(specs.size()), specs.data()));
    if (save && !filters.empty() && filters[0].second.starts_with(L"*."))
        check(dialog->SetDefaultExtension(filters[0].second.c_str() + 2));
    auto history = application_data() / L"native-dialog-folders.json";
    json state = json::object();
    try {
        state = json::parse(read_file(history));
        if (!state.is_object())
            state = json::object();
    } catch (...) {
    }
    fs::path initial = fallback;
    try {
        initial = wide(state.value(std::string(purpose), state.value("last", utf8(fallback.wstring()))));
    } catch (...) {
    }
    if (!initial.empty() && fs::is_directory(initial)) {
        initial = fs::absolute(initial).lexically_normal();
        ComPtr<IShellItem> item;
        if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr, IID_PPV_ARGS(&item))))
            check(dialog->SetFolder(item.Get()));
    }
    ComPtr<FolderEvents> events;
    events.Attach(new FolderEvents);
    DWORD cookie = 0;
    check(dialog->Advise(events.Get(), &cookie));
    HRESULT result = dialog->Show(owner);
    dialog->Unadvise(cookie);
    if (!events->folder.empty()) {
        state[std::string(purpose)] = state["last"] = utf8(events->folder.wstring());
        try {
            atomic_write(history, state.dump(2));
        } catch (const std::exception &e) {
            diagnostic("folder-history", e.what());
        }
    }
    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
        return {};
    check(result);
    std::vector<fs::path> paths;
    if (multiple && !save) {
        ComPtr<IFileOpenDialog> open;
        check(dialog.As(&open));
        ComPtr<IShellItemArray> items;
        check(open->GetResults(&items));
        DWORD count;
        check(items->GetCount(&count));
        for (DWORD i = 0; i < count; i++) {
            ComPtr<IShellItem> item;
            check(items->GetItemAt(i, &item));
            paths.push_back(shell_path(item.Get()));
        }
    } else {
        ComPtr<IShellItem> item;
        check(dialog->GetResult(&item));
        paths.push_back(shell_path(item.Get()));
    }
    if (!paths.empty()) {
        state[std::string(purpose)] = state["last"] = utf8(paths.front().parent_path().wstring());
        try {
            atomic_write(history, state.dump(2));
        } catch (const std::exception &e) {
            diagnostic("folder-history", e.what());
        }
    }
    return paths;
}
} // namespace starlight::editor
