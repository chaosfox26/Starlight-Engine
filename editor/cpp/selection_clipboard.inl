// Scene-object clipboard is private to this editor session; text clipboard is untouched.
std::vector<Character> copiedObjects;
bool selection_clipboard_command(int id) {
    if (id != CopyObjects && id != PasteObjects) return false;
    if (playing) throw std::runtime_error("Pause playback before copying or pasting objects");
    if (id == CopyObjects) {
        std::vector<Character> copies;
        for (size_t i = 0; i < scene.Characters.size(); ++i)
            if (int(i) == selected || selectionIds.contains(scene.Characters[i].Id)) {
                copies.push_back(scene.Characters[i]);
            }
        if (copies.empty()) throw std::runtime_error("Select objects to copy first");
        copiedObjects = std::move(copies);
        SetWindowTextW(status, L"Objects copied for this editor session. Open another scene and choose Paste copied objects.");
        return true;
    }
    if (copiedObjects.empty()) throw std::runtime_error("Copy some objects first");
    auto copies = copiedObjects;
    std::unordered_set<std::string> validated;
    for (auto &object : copies) {
        if (!object.Shape && !object.Sculpt && validated.insert(object.Asset).second) (void)load_model(object.Asset);
        object.Id = new_id();
    }
    place_object_group(copies, scene, placement_point().x, placement_point().z, gridSnap, objectSnap, groundSnap,gridStep);
    auto previousSelected = selected;
    auto previousIds = selectionIds;
    try {
        edit([&] {
            selected = int(scene.Characters.size()); selectionIds.clear();
            for (auto &object : copies) {
                selectionIds.insert(object.Id); scene.Characters.push_back(std::move(object));
            }
        });
    } catch (...) { selected = previousSelected; selectionIds = previousIds; throw; }
    SetWindowTextW(status, L"Copied objects pasted. Undo removes this insertion. Imported models stay in the shared asset library.");
    return true;
}
