// Adversarial input for the small parsers: every editor command is applied with each of its keys deleted or replaced by a value of the wrong type or
// range, and every starter scene is decoded with single fields damaged. Allowed outcomes: a normal std::exception refusal that leaves the scene file
// byte-identical, or a success whose saved scene loads again. Anything else (other exception types, a changed file after a refusal, an unloadable save)
// is a defect and is printed.
namespace advtest {
inline void leaves(const json& node, std::string pointer, std::vector<std::string>& out, int depth = 0) {
    out.push_back(pointer);
    if (depth > 5) return;
    if (node.is_object()) for (auto it = node.begin(); it != node.end(); ++it) {
        std::string key;
        for (char c : it.key()) { if (c == '~') key += "~0"; else if (c == '/') key += "~1"; else key += c; }
        leaves(it.value(), pointer + "/" + key, out, depth + 1);
    }
    else if (node.is_array()) for (size_t i = 0; i < node.size() && i < 2; ++i) leaves(node[i], pointer + "/" + std::to_string(i), out, depth + 1);
}
inline std::vector<json> damage(const json& original, size_t stride = 1) {
    std::vector<std::string> pointers;
    leaves(original, "", pointers);
    static const json replacements[] = {nullptr, "x", -1, 1e308, 4294967297ll, true, json::array(), json::object()};
    std::vector<json> result;
    if (pointers.size() <= 24) stride = 1;
    for (size_t p = 0; p < pointers.size(); p += stride) {
        const auto& at = pointers[p];
        json::json_pointer jp(at);
        for (const auto& r : replacements) {
            if (at.empty()) { if (r != original) result.push_back(r); continue; }
            if (original.at(jp) == r) continue;
            auto copy = original;
            copy[jp] = r;
            result.push_back(std::move(copy));
        }
        if (!at.empty()) {
            auto parent = jp.parent_pointer();
            if (original.at(parent).is_object()) {
                auto copy = original;
                copy.at(parent).erase(jp.back());
                result.push_back(std::move(copy));
            }
        }
    }
    return result;
}
}

template<class Require> void adversarial_parser_checks(Require require, const fs::path& folder) {
    const auto work = folder / "adversarial-parser-checks";
    fs::remove_all(work);
    fs::create_directories(work);
    EngineAssets::ScopedRoot library(work / "engine-assets");

    SceneData base;
    Character a; a.Shape = "cube"; a.Name = "A"; a.Route = {{1, 1}, {2, 2}};
    Character b; b.Shape = "cube"; b.Name = "B"; b.X = 3;
    Character light; light.Shape = "light"; light.Name = "L";
    base.Characters = {a, b, light};
    const auto scenePath = work / "scene.starscene";
    save_scene(base, scenePath);
    const auto baseBytes = read_file(scenePath);
    save_object_template(a, work / "one.starobject");
    save_object_group({a, b}, work / "pair.stargroup");
    SceneData source; source.Characters = {b};
    save_scene(source, work / "source.starscene");
    const auto club = json::parse(read_file("starters/gameproject-club/club-room.starscene"));

    const std::string idA = a.Id, idB = b.Id, idL = light.Id;
    std::vector<json> ops{
        {{"op", "add"}, {"name", "N"}, {"asset", "missing-model.glb"}, {"x", 1}, {"z", 2}},
        {{"op", "move"}, {"id", idA}, {"x", 1}, {"z", 2}},
        {{"op", "route"}, {"id", idA}, {"points", json::array({json{{"x", 1}, {"z", 2}}})}},
        {{"op", "rename"}, {"id", idA}, {"name", "R"}},
        {{"op", "remove"}, {"id", idB}},
        {{"op", "configure"}, {"id", idA}, {"x", 1}, {"z", 2}, {"speed", 1}, {"size", 1}},
        {{"op", "shape"}, {"shape", "cube"}, {"name", "S"}, {"x", 1}, {"y", 0}, {"z", 1}, {"width", 1}, {"height", 1}, {"depth", 1}, {"color", "#ff0000"}, {"glow", 0}, {"group", "g"}, {"ringInner", .5}, {"lightInner", .5}},
        {{"op", "transform"}, {"id", idA}, {"y", 0}, {"width", 1}, {"height", 1}, {"depth", 1}, {"rotationX", 0}, {"rotationY", 0}, {"rotationZ", 0}, {"keepAboveFloor", true}, {"color", "#ffffff"}, {"lightIntensity", 1}, {"lightRange", 1}, {"glow", 0}, {"group", "g"}, {"ringInner", .5}, {"lightInner", .5}},
        {{"op", "follow"}, {"id", idL}, {"target", "focus"}, {"offsetX", 1}, {"offsetZ", 1}, {"lag", .2}, {"wobble", .1}, {"poolRadius", 1}, {"housing", json{{"Width", 1}, {"Height", 1}, {"Depth", 1}, {"Color", "#ffffff"}}}, {"beam", json{{"Haze", .1}, {"Pool", .2}, {"InnerPool", .3}}}},
        {{"op", "follow"}, {"id", idL}, {"clear", true}},
        {{"op", "terrain-brush"}, {"tool", "raise"}, {"x", 1}, {"z", 1}, {"radius", 1}, {"strength", 1}, {"seconds", .1}, {"target", 0}, {"paint", 1}},
        {{"op", "terrain-water"}, {"enabled", true}, {"level", .5}, {"lava", false}},
        {{"op", "terrain-stamp"}, {"id", idA}},
        {{"op", "terrain-volume"}, {"x", 0}, {"y", 0}, {"z", 0}, {"radius", 1}, {"subtract", true}},
        {{"op", "insert-object"}, {"file", "one.starobject"}, {"expectedSha256", hash_file(work / "one.starobject")}, {"name", "I"}, {"x", 5}, {"z", 1}},
        {{"op", "insert-group"}, {"file", "pair.stargroup"}, {"expectedSha256", hash_file(work / "pair.stargroup")}, {"x", 1}, {"z", 2}},
        {{"op", "merge-scene"}, {"file", "source.starscene"}, {"expectedSha256", hash_file(work / "source.starscene")}, {"x", 4}, {"z", 2}},
        {{"op", "duplicate"}, {"id", idA}, {"name", "D"}, {"x", 2}, {"z", 2}},
        {{"op", "replace-model"}, {"id", idA}, {"asset", "missing-model.glb"}},
    };
    for (const char* block : {"Lighting", "Moods", "ActiveMood", "View", "Zones", "Crowd"})
        ops.push_back({{"op", "set-block"}, {"block", block}, {"value", club.at(block)}});
    ops.push_back({{"op", "set-block"}, {"block", "Cast"}, {"value", club.at("Cast")}, {"verifyAssets", false}});

    const auto requestPath = work / "request.json";
    int tried = 0, refused = 0, saved = 0, defects = 0;
    auto run = [&](const json& request, const std::string& label) {
        atomic_write(scenePath, baseBytes);
        const auto before = hash_file(scenePath);
        json r = request;
        if (r.is_object() && r.contains("expectedSha256") && r.at("expectedSha256") == "@") r["expectedSha256"] = before;
        atomic_write(requestPath, r.dump());
        ++tried;
        try {
            apply_commands(requestPath);
            ++saved;
            try { (void)load_scene(scenePath); }
            catch (...) { ++defects; std::cout << "DEFECT saved scene does not load: " << label << "\n"; }
        } catch (const std::exception&) {
            ++refused;
            if (hash_file(scenePath) != before) { ++defects; std::cout << "DEFECT refused request changed the scene: " << label << "\n"; }
        } catch (...) {
            ++defects;
            std::cout << "DEFECT non-standard exception: " << label << "\n";
        }
    };
    auto wrap = [&](const json& list) {
        return json{{"schemaVersion", 1}, {"scenePath", utf8(fs::absolute(scenePath).wstring())}, {"expectedSha256", "@"}, {"operations", list}};
    };
    for (const auto& op : ops) {
        run(wrap(json::array({op})), op.at("op").get<std::string>() + " untouched");
        for (const auto& bad : advtest::damage(op, 4)) {
            run(wrap(json::array({bad})), bad.dump().substr(0, 300));
        }
    }
    for (const auto& bad : advtest::damage(wrap(json::array({ops[1]})))) run(bad, bad.dump().substr(0, 300));
    for (const auto& list : {json::array({1, 2}), json::array({json::array()}), json::array({nullptr}), json::object(), json::array({json::object()})}) run(wrap(list), list.dump());
    std::cout << "ADVERSARIAL commands tried=" << tried << " refused=" << refused << " saved=" << saved << " defects=" << defects << "\n";
    require(tried > 500 && defects == 0, "Damaged editor command requests are refused cleanly and never corrupt the scene");

    int sceneTried = 0, sceneDefects = 0;
    for (const char* starter : {"starters/empty-demo/empty-demo.starscene", "starters/gameproject-club/club-room.starscene", "starters/moonseed-glade/moonseed-glade.starscene"}) {
        const auto original = json::parse(read_file(starter));
        require(original.is_object(), "Starter scenes are JSON objects");
        for (const auto& damaged : advtest::damage(original, 12)) {
            ++sceneTried;
            try { auto s = SceneData::decode(damaged.dump()); s.validate(); }
            catch (const std::exception&) {}
            catch (...) { ++sceneDefects; std::cout << "DEFECT non-standard exception decoding " << starter << "\n"; }
        }
    }
    {
        const size_t depth = 300000;
        const std::string open(depth, '['), close(depth, ']');
        const std::string deep = open + close;
        int refusedDeep = 0, total = 0;
        auto refuses = [&](auto&& action) { ++total; try { action(); } catch (const std::exception&) { ++refusedDeep; } };
        refuses([&] { decode_object_group("{\"Format\":\"starlight-object-group\",\"Version\":2,\"Objects\":[{\"Name\":" + deep + "}]}"); });
        refuses([&] { decode_object_template("{\"Format\":\"starlight-object\",\"Version\":2,\"Object\":{\"Name\":" + deep + "}}"); });
        refuses([&] { (void)SceneData::decode("{\"SchemaVersion\":16,\"Name\":" + deep + "}"); });
        refuses([&] { (void)SceneData::decode("{\"SchemaVersion\":16,\"Characters\":[{\"Name\":" + deep + "}]}"); });
        refuses([&] { starlight::gfxrt::parse_manifest("{\"schema\":1,\"set_id\":" + deep + "}"); });
        require(total == 5 && refusedDeep == 5, "Reusable objects, groups and scenes nested hundreds of thousands deep are refused without a stack overflow");
    }
    std::cout << "ADVERSARIAL scenes tried=" << sceneTried << " defects=" << sceneDefects << "\n";
    require(sceneTried > 200 && sceneDefects == 0, "Damaged starter scenes are refused or accepted without crashing");
}
