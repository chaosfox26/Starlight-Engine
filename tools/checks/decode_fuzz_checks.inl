// Adversarial decoding: a deterministic mutation fuzzer for scene, reusable object, reusable group, project and test preset readers.
// Only a clean std::exception may refuse a mutated file; anything accepted must save and load again to the same bytes.
// STARLIGHT_DECODE_FUZZ_ROUNDS=<n> raises the rounds per seed for a longer hunt (default is short and fixed).
#include "projects.hpp"
#include <chrono>
#include <limits>
namespace decode_fuzz {
using nlohmann::json;
struct Rng {
    uint64_t state;
    uint64_t next() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
    size_t below(size_t n) { return size_t(next() % n); }
};
inline void nodes(json& at, std::vector<json*>& all) {
    all.push_back(&at);
    if (at.is_structured()) for (auto& child : at) nodes(child, all);
}
inline void leaves(const json& at, const json::json_pointer& path, std::vector<json::json_pointer>& all) {
    if (at.is_object()) for (auto it = at.begin(); it != at.end(); ++it) leaves(it.value(), path / it.key(), all);
    else if (at.is_array()) for (size_t i = 0; i < at.size(); ++i) leaves(at[i], path / i, all);
    else all.push_back(path);
}
inline const std::vector<std::string>& raw_values() {
    static const std::vector<std::string> values = {"1e999", "-1e999", "1e-999", "123456789012345678901234567890", "-0", "18446744073709551616",
        "\"\xff\xfe\"", "\"\xc0\x80\"", "\"\xe0\x80\x80\"", "\"\xed\xa0\x80\"", "\"abc\xc3\"", "\"\\u0000\"", "\"\xf4\x90\x80\x80\"",
        "1,\"Id\":\"dup\",\"Id\":\"dup\"", "1,\"Shape\":\"cube\",\"Shape\":\"light\"", "0,\"JointTargetId\":\"dup\",\"Id\":\"dup\"", "1,2,3,4", "null,null",
        std::string(100000, '[') + std::string(100000, ']'), [] { std::string s; for (int i = 0; i < 50000; ++i) s += "{\"k\":"; s += "1"; return s + std::string(50000, '}'); }()};
    return values;
}
inline json special(Rng& rng) {
    switch (rng.below(30)) {
    case 0: return nullptr;
    case 1: return true;
    case 2: return false;
    case 3: return 0;
    case 4: return -1;
    case 5: return 2147483647;
    case 6: return 2147483648LL;
    case 7: return 4294967297ULL;
    case 8: return std::numeric_limits<int64_t>::min();
    case 9: return std::numeric_limits<uint64_t>::max();
    case 10: return 1e308;
    case 11: return -1e308;
    case 12: return 0.5;
    case 13: return -0.0;
    case 14: return "";
    case 15: return std::string(70000, 'A');
    case 16: return std::string("a\0b", 3);
    case 17: return "model-" + std::string(64, 'a');
    case 18: return json::array();
    case 19: return json::object();
    case 20: return json::array({0, 0, 0});
    case 21: return json(std::vector<int>(20000, 0));
    case 22: { json o = json::object(); for (int i = 0; i < 3000; ++i) o["k" + std::to_string(i)] = i; return o; }
    default: return "@@RAW" + std::to_string(rng.below(raw_values().size())) + "@@";
    }
}
inline std::string mutate(json document, Rng& rng) {
    for (size_t pass = 1 + rng.below(3); pass--;) {
        std::vector<json*> all;
        nodes(document, all);
        json& target = *all[rng.below(all.size())];
        switch (rng.below(7)) {
        case 0: case 1: case 2: target = special(rng); break;
        case 3: if (target.is_object() && !target.empty()) { auto it = target.begin(); std::advance(it, rng.below(target.size())); target.erase(it); } break;
        case 4: { const json other = *all[rng.below(all.size())]; target = other; break; }
        case 5: if (target.is_array() && !target.empty()) { const json first = target.front(); for (size_t n = rng.below(6000); n--;) target.push_back(first); } break;
        default: { json wrapped = target; for (size_t depth = 1 + rng.below(150); depth--;) wrapped = rng.below(2) ? json::array({wrapped}) : json{{"k", wrapped}}; target = wrapped; }
        }
    }
    std::string text = document.dump();
    for (size_t at; (at = text.find("\"@@RAW")) != std::string::npos;) {
        const size_t end = text.find("@@\"", at + 6);
        const size_t index = std::stoul(text.substr(at + 6, end - at - 6));
        text.replace(at, end + 3 - at, raw_values()[index]);
    }
    if (rng.below(3) == 0 && !text.empty()) {
        switch (rng.below(5)) {
        case 0: text[rng.below(text.size())] = char(rng.below(256)); break;
        case 1: text.resize(rng.below(text.size())); break;
        case 2: { const size_t from = rng.below(text.size()); text.erase(from, rng.below(64)); break; }
        case 3: { const size_t from = rng.below(text.size()); text.insert(from, text.substr(from, rng.below(512))); break; }
        default: text.insert(rng.below(text.size()), std::string(1 + rng.below(4), char(0x80 + rng.below(128)))); break;
        }
    }
    if (rng.below(40) == 0) text = std::string(1 + rng.below(200000), '[') + text;
    return text;
}
}
static void decode_fuzz_checks(const auto& require, const fs::path& folder) {
    using decode_fuzz::json;
    size_t rounds = 120;
    if (const char* env = std::getenv("STARLIGHT_DECODE_FUZZ_ROUNDS")) rounds = std::max<size_t>(1, std::strtoull(env, nullptr, 10));
    {
        // Findings from the hunt, kept as plain cases so they stay red-first if they regress.
        auto refuses = [](auto&& decode) { try { decode(); } catch (const std::exception&) { return true; } return false; };
        Character fighter; fighter.Id = "fighter"; fighter.Shape = "cube"; fighter.CombatHealth = 5; fighter.GrappleMode = 1; fighter.CombatTeam = 1; fighter.CombatControlEffect = 2;
        SceneData scene; scene.Characters = {fighter};
        const json base = json::parse(scene.encode());
        require(SceneData::decode(base.dump()).Characters[0].CombatTeam == 1, "Decode fuzz: the whole-number seed itself decodes");
        bool wrapped = false;
        for (const char* key : {"GrappleMode", "CombatTeam", "CombatControlEffect"})
            for (const json& value : {json(4294967297ULL), json(4294967296ULL), json(1.5), json(1e-300)}) {
                json bad = base; bad["Characters"][0][key] = value;
                wrapped |= !refuses([&] { SceneData::decode(bad.dump()); });
            }
        require(!wrapped, "Decode fuzz: an object setting that is not a 32-bit whole number is refused instead of wrapping or truncating");
        Character one = fighter, other = fighter;
        one.Id = "twin"; other.Id = "other"; other.Dynamic = true; other.JointKind = 3; other.JointTargetId = "twin";
        json group = json::parse(encode_object_group({one, other}));
        group["Objects"].push_back(group["Objects"][0]);
        require(refuses([&] { decode_object_group(group.dump()); }), "Decode fuzz: a reusable group that repeats an object identity is refused instead of linking joints to the later copy");
    }
    std::vector<std::string> problems;
    size_t refused = 0, accepted = 0;
    size_t cases = 0;
    auto attempt = [&](const std::string& name, const std::string& text, auto&& read, auto&& save, bool stable, const json::json_pointer* where, const json* replaced) {
        ++cases;
        const auto began = std::chrono::steady_clock::now();
        std::string saved, fault;
        try { saved = save(read(text)); ++accepted; }
        catch (const std::bad_alloc&) { fault = "ran out of memory"; }
        catch (const std::length_error&) { fault = "hit a container length limit"; }
        catch (const std::exception&) { ++refused; }
        catch (...) { fault = "threw something that is not a standard exception"; }
        if (fault.empty() && stable && !saved.empty()) {
            try { if (save(read(saved)) != saved) fault = "was accepted but does not save to stable bytes"; }
            catch (const std::exception& e) { fault = std::string("was accepted but its own saved bytes are refused: ") + e.what(); }
            catch (...) { fault = "was accepted but its own saved bytes threw a non-standard exception"; }
        }
        if (fault.empty() && where && replaced && replaced->is_number() && !saved.empty()) {
            const auto stored = json::parse(saved);
            if (stored.contains(*where) && stored.at(*where).is_number() && stored.at(*where).get<double>() != replaced->get<double>())
                fault = "was accepted but " + where->to_string() + " silently changed from " + replaced->dump() + " to " + stored.at(*where).dump();
        }
        if (fault.empty() && std::chrono::steady_clock::now() - began > std::chrono::seconds(5)) fault = "took more than five seconds";
        if (!fault.empty()) {
            const auto keep = folder / (L"decode-fuzz-" + std::to_wstring(problems.size()) + L".txt");
            std::ofstream(keep, std::ios::binary).write(text.data(), std::streamsize(text.size()));
            problems.push_back(name + " " + fault + " (input kept in " + keep.string() + ")");
        }
    };
    auto hunt = [&](const std::string& name, const json& seed, uint64_t salt, size_t count, auto&& read, auto&& save, bool stable = true) {
        decode_fuzz::Rng rng{0x9E3779B97F4A7C15ull ^ (salt * 0xD1B54A32D192ED03ull)};
        for (size_t round = 0; round < count; ++round) attempt(name + " round " + std::to_string(round), decode_fuzz::mutate(seed, rng), read, save, stable, nullptr, nullptr);
    };
    // Every leaf of the seed gets every awkward number in turn; an accepted number must come back unchanged (no wrap, truncation or clamp).
    auto sweep = [&](const std::string& name, const json& seed, uint64_t salt, size_t leafLimit, auto&& read, auto&& save) {
        std::vector<json::json_pointer> leaves;
        decode_fuzz::leaves(seed, json::json_pointer(), leaves);
        decode_fuzz::Rng rng{0x2545F4914F6CDD1Dull ^ salt};
        if (leaves.size() > leafLimit) {
            for (size_t i = 0; i < leafLimit; ++i) std::swap(leaves[i], leaves[i + rng.below(leaves.size() - i)]);
            leaves.resize(leafLimit);
        }
        const json values[] = {4294967297ULL, 4294967296ULL, 2147483648LL, 1.5, -1, 1e30, 3.0, 18446744073709551615ULL};
        for (const auto& at : leaves)
            for (const auto& value : values) {
                json document = seed;
                document[at] = value;
                attempt(name + " " + at.to_string() + "=" + value.dump(), document.dump(), read, save, true, &at, &value);
            }
    };
    auto find_starter = [](const char* folderName, const char* file) {
        for (auto base = fs::current_path(); !base.empty(); base = base.parent_path()) {
            auto candidate = base / "starters" / folderName / file;
            if (fs::exists(candidate)) return candidate;
            if (base == base.parent_path()) break;
        }
        return fs::path();
    };
    auto scene_read = [](const std::string& text) { return SceneData::decode(text); };
    auto scene_save = [](const SceneData& scene) { return scene.encode(); };
    {
        SceneData built;
        built.Name = "Fuzz seed";
        built.Game = GameSettings{};
        GameplayRule rule; rule.Id = "rule-a"; rule.Actions = {GameplayAction{"complete-objective", "goal"}}; rule.Conditions = {GameplayCondition{"has-key", "blue"}};
        built.Rules = {rule};
        QuestDefinition quest; quest.Id = "quest-a"; quest.RuleIds = {"rule-a"}; quest.StepSizes = {1};
        built.Quests = {quest};
        built.LiquidSources = {LiquidSource{1, 1, false, 2}};
        Character cube; cube.Id = "cube-a"; cube.Shape = "cube"; cube.Route = {{1, 1}, {2, 2}};
        Character light; light.Id = "light-a"; light.Shape = "light"; light.Follow = LightFollow{}; light.Follow->Housing = LightFollowHousing{}; light.Follow->Beam = LightFollowBeam{};
        Character body; body.Id = "body-a"; body.Shape = "cube"; body.Dynamic = true; body.JointKind = 3; body.JointTargetId = "cube-a"; body.JointSegments = 8;
        Character carved; carved.Id = "carved-a"; carved.Sculpt = SculptMesh{{0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 1, 2}, {}}; carved.Sculpt->touch();
        Character rich; rich.Id = "rich-a"; rich.Shape = "cube"; rich.CombatHealth = 5; rich.CombatTeam = 1; rich.CombatBoss = true; rich.GrappleMode = 1; rich.CombatControlEffect = 2;
        rich.GameplayRole = "key"; rich.GameplayKey = "blue"; rich.Glow = .5; rich.Group = "g1"; rich.Surface = 12; rich.Foliage = FoliageMaterial{};
        light.LightInner = .5;
        Character ring; ring.Id = "ring-a"; ring.Shape = "ring"; ring.RingInner = .5;
        Character bolt; bolt.Id = "bolt-a"; bolt.Shape = "chaos-lightning"; bolt.LightningTargetId = "rich-a"; bolt.LightningChains = 2; bolt.LightningDamage = 3;
        built.Characters = {cube, light, body, carved, rich, ring, bolt};
        built.Terrain = TerrainData{};
        built.Terrain->Volumes = {TerrainVolume{0, 0, 0, 1, true}};
        built.Lighting = SceneLighting{}; built.Lighting->Modulators["Dark"]["Key"] = -1;
        SceneMood mood; mood.Id = "calm"; built.Moods = {mood};
        built.View = SceneView{}; built.View->Follow = ViewFollow{};
        SceneZone zone; zone.Id = "z1"; zone.Radius = 2; built.Zones = {zone};
        built.Crowd = SceneCrowd{};
        hunt("built scene", json::parse(built.encode()), 1, rounds * 2, scene_read, scene_save);
        sweep("built scene", json::parse(built.encode()), 1, 300, scene_read, scene_save);
    }
    const struct { const char* folderName; const char* file; size_t divisor; } starters[] = {{"empty-demo", "empty-demo.starscene", 1}, {"club-room", "club-room.starscene", 4}, {"moonseed-glade", "moonseed-glade.starscene", 40}};
    uint64_t salt = 2;
    for (const auto& starter : starters) {
        const auto path = find_starter(starter.folderName, starter.file);
        if (path.empty()) { std::cout << "Fuzz: NOT RUN: the starter scene " << starter.file << " is not in this checkout, so it was not fuzzed" << std::endl; continue; }
        hunt(starter.file, json::parse(read_file(path)), salt, std::max<size_t>(3, rounds / starter.divisor), scene_read, scene_save);
        sweep(starter.file, json::parse(read_file(path)), salt++, starter.divisor == 1 ? 100000 : 150 / starter.divisor, scene_read, scene_save);
    }
    {
        Character cube; cube.Shape = "cube"; cube.Route = {{1, 1}};
        Character carved; carved.Sculpt = SculptMesh{{0, 0, 0, 1, 0, 0, 0, 1, 0}, {0, 1, 2}, {}}; carved.Sculpt->touch();
        auto object_read = [](const std::string& t) { return decode_object_template(t); };
        auto object_save = [](const Character& c) { return encode_object_template(c); };
        hunt("reusable object", json::parse(encode_object_template(cube)), 10, rounds, object_read, object_save);
        sweep("reusable object", json::parse(encode_object_template(cube)), 10, 100000, object_read, object_save);
        hunt("reusable sculpt object", json::parse(encode_object_template(carved)), 11, rounds, object_read, object_save);
        Character first = cube, second = cube; first.Id = "one"; second.Id = "two"; second.Dynamic = true; second.JointKind = 3; second.JointTargetId = "one";
        auto group_read = [](const std::string& t) { return decode_object_group(t); };
        auto group_save = [](const std::vector<Character>& g) { return encode_object_group(g); };
        hunt("reusable group", json::parse(encode_object_group({first, second})), 12, rounds, group_read, group_save);
        sweep("reusable group", json::parse(encode_object_group({first, second})), 12, 100000, group_read, group_save);
    }
    {
        const json preset = {{"schemaVersion", 2}, {"name", "fuzz"}, {"settings", {{"resolution", {1280, 720}}, {"displayMode", "windowed"}, {"frameLimit", 600}, {"durationMs", 60000}, {"upscaler", "off"}, {"quality", "native"}, {"frameGeneration", false}, {"vsync", false}, {"culling", true}, {"foreground", false}}}};
        auto preset_read = [](const std::string& t) { auto p = json::parse(t); starlight::preset::validate(p); return p; };
        auto preset_save = [](const json& p) { return p.dump(); };
        hunt("test preset", preset, 13, rounds * 4, preset_read, preset_save);
        sweep("test preset", preset, 13, 100000, preset_read, preset_save);
    }
    {
        const json project = {{"schemaVersion", 3}, {"defaultScene", "main"}, {"scenes", {{"main", {{"arguments", json::array()}}}, {"other", {{"arguments", json::array()}}}}}};
        const auto file = folder / L"fuzz.star";
        hunt("project file", project, 14, rounds * 2, [&](const std::string& t) { std::ofstream(file, std::ios::binary).write(t.data(), std::streamsize(t.size())); return ProjectData::load(file); },
             [](const ProjectData& p) { std::string names = p.selected; for (const auto& s : p.scenes) names += "|" + s; return names; }, false);
    }
    for (const auto& problem : problems) std::cout << "DECODE-FUZZ " << problem << "\n";
    std::cout << "DECODE-FUZZ cases " << cases << ", accepted " << accepted << ", refused " << refused << ", faults " << problems.size() << "\n";
    require(refused > 0 && accepted > 0, "Decode fuzz: the mutations include both refused and accepted files");
    require(problems.empty(), "Decode fuzz: mutated scene, object, group, project and preset files are refused cleanly or round-trip stably");
}
