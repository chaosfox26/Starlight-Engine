HWND libraryPanel{}, librarySearch{}, libraryList{}, libraryMessage{};
fs::path libraryFolder;
AssetIndex libraryIndex;
std::vector<size_t> libraryMatches;
std::future<AssetIndex> libraryTask;

void layout_asset_browser() {
    if (!libraryPanel) return;
    unscroll_panel(libraryPanel);
    auto p=[&](int n){return settings_px(n);};
    const int w=std::max(p(320),std::min(p(700),width-60)),h=p(560);
    MoveWindow(GetDlgItem(libraryPanel,CloseAssets),w-p(50),p(12),p(32),p(30),TRUE);
    MoveWindow(librarySearch,p(20),p(104),w-p(40),p(30),TRUE);
    MoveWindow(GetDlgItem(libraryPanel,AssetType),p(20),p(144),w-p(40),p(220),TRUE);
    MoveWindow(libraryList,p(20),p(184),w-p(40),h-p(294),TRUE);
    MoveWindow(GetDlgItem(libraryPanel,InsertAsset),p(20),h-p(100),w-p(40),p(34),TRUE);
    MoveWindow(libraryMessage,p(20),h-p(58),w-p(40),p(48),TRUE);
    finish_scroll_panel(libraryPanel,18,p(58),w,h);
}
void ensure_asset_browser() {
    if (libraryPanel) return;
    if (libraryFolder.empty()) {
        try {
            auto preferences = json::parse(read_file(application_data() / L"asset-browser.json"));
            libraryFolder = wide(preferences.at("folder").get<std::string>());
        } catch (...) {}
    }
    libraryPanel = panel(20, 58, 700, 560);
    control(libraryPanel, L"STATIC", L"ASSET BROWSER", 0, 20, 16, 260, 26);
    control(libraryPanel, L"BUTTON", L"×", CloseAssets, 650, 12, 32, 30);
    control(libraryPanel, L"BUTTON", L"Choose folder…", ChooseAssets, 20, 52, 170, 34);
    control(libraryPanel, L"BUTTON", L"Refresh", RefreshAssets, 202, 52, 94, 34);
    control(libraryPanel, L"STATIC", L"Search assets", 0, 20, 86, 260, 18);
    librarySearch = control(libraryPanel, L"EDIT", L"", SearchAssets, 20, 104, 660, 30, ES_AUTOHSCROLL | WS_TABSTOP);
    SendMessageW(librarySearch, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search names, folders or types…"));
    auto types = control(libraryPanel, L"COMBOBOX", L"", AssetType, 20, 144, 660, 220, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP);
    for (auto label : {L"All asset types", L"Models", L"Saved objects", L"Saved groups", L"Scene objects"})
        SendMessageW(types, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
    SendMessage(types, CB_SETCURSEL, 0, 0);
    libraryList = control(libraryPanel, L"LISTBOX", L"", AssetList, 20, 144, 660, 306, LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP);
    control(libraryPanel, L"BUTTON", L"Add selected asset to scene", InsertAsset, 20, 460, 660, 34);
    libraryMessage = control(libraryPanel, L"STATIC", L"Choose a folder containing models, objects, groups or scenes.", 0, 20, 502, 660, 48);
    EnableWindow(GetDlgItem(libraryPanel, InsertAsset), FALSE);
    ShowWindow(libraryPanel, SW_HIDE);
    layout_asset_browser();
}
void filter_asset_browser() {
    if (!libraryList) return;
    const std::wstring_view extensions[] = {L"", L".glb", L".starobject", L".stargroup", L".starscene"};
    auto type = SendMessage(GetDlgItem(libraryPanel, AssetType), CB_GETCURSEL, 0, 0);
    libraryMatches = filter_asset_library(libraryIndex, wide(text(librarySearch)), extensions[type >= 0 && type < 5 ? type : 0]);
    const auto count = libraryMatches.size();
    if (libraryMatches.size() > 500) libraryMatches.resize(500);
    SendMessage(libraryList, WM_SETREDRAW, FALSE, 0);
    SendMessage(libraryList, LB_RESETCONTENT, 0, 0);
    for (auto index : libraryMatches)
        SendMessageW(libraryList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(libraryIndex.entries[index].label.c_str()));
    if (!libraryMatches.empty()) SendMessage(libraryList, LB_SETCURSEL, 0, 0);
    SendMessage(libraryList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(libraryList, nullptr, FALSE);
    EnableWindow(GetDlgItem(libraryPanel, InsertAsset), !libraryMatches.empty() && !libraryTask.valid());
    auto message = std::to_wstring(count) + L" matching assets";
    if (count > 500) message += L" · Showing first 500; refine your search";
    if (libraryIndex.partial) message += L" · Partial scan; choose a smaller folder";
    if (libraryIndex.errors) message += L" · Some entries could not be read";
    if (!libraryFolder.empty()) message += L" · Folder: " + libraryFolder.wstring();
    SetWindowTextW(libraryMessage, message.c_str());
}
void scan_asset_browser() {
    if (libraryTask.valid() || libraryFolder.empty()) return;
    const auto folder = libraryFolder;
    libraryTask = std::async(std::launch::async, [folder] { return scan_asset_library(folder); });
    for (int id : {ChooseAssets, RefreshAssets, InsertAsset}) EnableWindow(GetDlgItem(libraryPanel, id), FALSE);
    SetWindowTextW(libraryMessage, L"Scanning asset folder… You can continue editing.");
}
void poll_asset_browser() {
    if (!libraryTask.valid() || libraryTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try { libraryIndex = libraryTask.get(); filter_asset_browser(); }
    catch (const std::exception& e) { libraryIndex = {}; filter_asset_browser(); SetWindowTextW(libraryMessage, wide(e.what()).c_str()); }
    for (int id : {ChooseAssets, RefreshAssets}) EnableWindow(GetDlgItem(libraryPanel, id), TRUE);
}
bool asset_browser_command(int id) {
    if (id < BrowseAssets || id > InsertAsset) return false;
    if (id == BrowseAssets) {
        ensure_asset_browser(); toggle_menu(0); ShowWindow(libraryPanel, SW_SHOW);
        SetWindowPos(libraryPanel, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        if (libraryIndex.entries.empty()) scan_asset_browser();
        SetFocus(librarySearch); return true;
    }
    if (!libraryPanel) return true;
    if (id == CloseAssets) { ShowWindow(libraryPanel, SW_HIDE); return true; }
    if (id == ChooseAssets && !libraryTask.valid()) {
        auto folders = file_dialog(window, "asset-library", {}, false, false, libraryFolder, L"Choose asset folder", true);
        if (!folders.empty()) {
            libraryFolder = folders.front();
            try { atomic_write(application_data() / L"asset-browser.json", json{{"folder",utf8(libraryFolder.wstring())}}.dump(2)); }
            catch (const std::exception& e) { diagnostic("asset browser preferences",e.what()); }
            scan_asset_browser();
        }
        return true;
    }
    if (id == RefreshAssets) { scan_asset_browser(); return true; }
    if (id == AssetType) { filter_asset_browser(); return true; }
    if (id != InsertAsset || libraryTask.valid()) return true;
    if (playing) throw std::runtime_error("Pause playback before adding assets");
    int row = int(SendMessage(libraryList, LB_GETCURSEL, 0, 0));
    if (row < 0 || size_t(row) >= libraryMatches.size()) return true;
    const auto path = libraryIndex.entries[libraryMatches[row]].path;
    std::vector<Character> objects;
    auto extension = path.extension().wstring();
    if (!_wcsicmp(extension.c_str(), L".starobject")) objects.push_back(load_object_template(path));
    else if (!_wcsicmp(extension.c_str(), L".stargroup")) objects = load_object_group(path);
    else if (!_wcsicmp(extension.c_str(), L".starscene")) {
        bool repaired = false; auto source = open_scene_data(path, repaired);
        if (!source) return true;
        objects = std::move(source->Characters);
    } else {
        load_glb(path); Character object; object.Asset = utf8(path.wstring()); object.Name = utf8(path.stem().wstring());
        objects.push_back(std::move(object));
    }
    for (auto &object : objects) object.Id = new_id();
    place_object_group(objects, scene, placement_point().x, placement_point().z, gridSnap, objectSnap, groundSnap,gridStep);
    const auto oldSelected = selected; const auto oldIds = selectionIds;
    try {
        edit([&] {
            selected = int(scene.Characters.size()); selectionIds.clear();
            for (auto &object : objects) { selectionIds.insert(object.Id); scene.Characters.push_back(std::move(object)); }
        });
    } catch (...) { selected = oldSelected; selectionIds = oldIds; throw; }
    SetWindowTextW(libraryMessage, L"Asset added. Undo removes this insertion; the library file is unchanged.");
    return true;
}
