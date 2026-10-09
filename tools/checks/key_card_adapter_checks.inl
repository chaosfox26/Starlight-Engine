// Hostile key cards through the host's key card adapter (keycard::loadKeyCard). Every damaged card is either refused with a message and an empty KeyCard,
// or accepted with finite geometry, colour and lighting values. Any other exception type, content left behind by a refusal, or a non-finite value is a defect.
namespace kcadv {
inline bool finite3(const float (&v)[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
inline bool finite_rgb(const keycard::Rgb& c) { return std::isfinite(c.r) && std::isfinite(c.g) && std::isfinite(c.b); }
inline const char* first_nonfinite(const keycard::KeyCard& card) {
    for (const auto& p : card.parts) {
        if (!finite3(p.origin)) return "part origin";
        if (!finite3(p.axisX) || !finite3(p.axisY) || !finite3(p.axisZ)) return "part axes";
        if (!finite_rgb(p.color) || !std::isfinite(p.glow) || !std::isfinite(p.ringInner)) return "part colour, glow or ring";
    }
    for (const auto& l : card.lights) {
        if (!finite3(l.position) || !finite3(l.direction)) return "light position or direction";
        if (!finite_rgb(l.color) || !std::isfinite(l.intensity) || !std::isfinite(l.range) || !std::isfinite(l.coneDegrees) || !std::isfinite(l.innerRatio)) return "light colour or beam";
        const auto& r = l.rig;
        if (l.follow && (!std::isfinite(r.offsetX) || !std::isfinite(r.offsetZ) || !std::isfinite(r.lag) || !std::isfinite(r.wobble) || !std::isfinite(r.poolRadius) || !std::isfinite(r.topY) || !std::isfinite(r.restRangeRatio))) return "follow spot rig";
        if (l.follow && (!finite3(r.housingSize) || !finite_rgb(r.housingColor) || !std::isfinite(r.haze) || !std::isfinite(r.pool) || !std::isfinite(r.innerPool))) return "follow spot housing or beam";
    }
    for (const auto& z : card.zones)
        if (!std::isfinite(z.x) || !std::isfinite(z.z) || !std::isfinite(z.radius) || !std::isfinite(z.width) || !std::isfinite(z.depth) || !std::isfinite(z.height)) return "zone";
    const auto& g = card.lighting;
    if (!finite3(g.keyDirection) || !std::isfinite(g.keyIntensity) || !std::isfinite(g.keyIntensityFlat)) return "lighting key";
    if (!finite_rgb(g.ambient) || !std::isfinite(g.ambientGround) || !finite_rgb(g.clear) || !std::isfinite(g.fogMix)) return "lighting ambient, clear or fog";
    for (const auto& m : card.moods)
        if (!finite_rgb(m.clear) || !finite_rgb(m.ambient) || !std::isfinite(m.ambientGround) || !std::isfinite(m.keyIntensity) || !std::isfinite(m.fogMix)) return "mood";
    const auto& v = card.view;
    if (!std::isfinite(v.fovDegrees) || !std::isfinite(v.nearPlane) || !std::isfinite(v.farPlane) || !std::isfinite(v.orbitRate) || !std::isfinite(v.farDistance) || !std::isfinite(v.farPerDistance) || !std::isfinite(v.farBase)) return "view camera";
    if (!std::isfinite(v.targetHeight) || !std::isfinite(v.targetHeightPerY) || !std::isfinite(v.defaultDistance) || !std::isfinite(v.defaultHeight) || !std::isfinite(v.idleDelaySeconds) || !std::isfinite(v.idlePeriodSeconds)) return "view follow or idle orbit";
    if (card.hasSpawn && !finite3(card.spawn)) return "spawn";
    return nullptr;
}
}

template<class Require> void key_card_adapter_checks(Require require, const fs::path& folder) {
    const auto work = folder / "key-card-adapter-checks";
    fs::remove_all(work);
    fs::create_directories(work);
    const auto card = work / "card.starscene";

    Character cube; cube.Shape = "cube"; cube.Name = "Cube"; cube.Width = 2;
    Character lamp; lamp.Shape = "light"; lamp.Name = "Lamp"; lamp.Y = 3; lamp.Z = 1;
    LightFollow follow; follow.OffsetX = 1; follow.OffsetZ = 2; follow.Housing.emplace(); follow.Beam.emplace();
    lamp.Follow = follow;
    SceneZone floor; floor.Id = "floor"; floor.Kind = "raised-floor"; floor.Shape = "box"; floor.Width = 4; floor.Depth = 4; floor.Height = 0.2;
    SceneMood dusk; dusk.Id = "dusk";
    SceneData scene;
    scene.Characters = {cube, lamp};
    scene.Zones = {floor};
    scene.Lighting = SceneLighting{};
    scene.Lighting->Modulators["bright"]["Key"] = 1;
    scene.View = SceneView{};
    scene.View->Follow = ViewFollow{};
    scene.Moods = {dusk};
    scene.ActiveMood = "dusk";
    const auto baseText = scene.encode();
    const auto original = json::parse(baseText);

    auto load = [&](const std::string& text, keycard::KeyCard& out) {
        atomic_write(card, text);
        return keycard::loadKeyCard(utf8(fs::absolute(card).wstring()), out);
    };
    keycard::KeyCard good;
    const auto baseError = load(baseText, good);
    require(baseError.empty() && good.lights.size() == 1 && good.parts.size() == 1 && good.zones.size() == 1 && !kcadv::first_nonfinite(good), "Key card adapter: the base card loads with finite values");

    int tried = 0, refused = 0, accepted = 0, defects = 0;
    auto judge = [&](const std::string& text, const std::string& label) {
        keycard::KeyCard out;
        std::string error;
        ++tried;
        try { error = load(text, out); }
        catch (const std::exception& e) { ++defects; std::cout << "DEFECT key card adapter threw: " << label << " (" << e.what() << ")\n"; return; }
        catch (...) { ++defects; std::cout << "DEFECT key card adapter non-standard exception: " << label << "\n"; return; }
        if (!error.empty()) {
            ++refused;
            if (error.find("cannot be loaded") == std::string::npos) { ++defects; std::cout << "DEFECT refusal has no plain message: " << label << "\n"; }
            if (!out.name.empty() || !out.lights.empty() || !out.parts.empty() || !out.zones.empty() || !out.moods.empty() || out.hasSpawn) { ++defects; std::cout << "DEFECT refused key card left content: " << label << "\n"; }
        } else {
            ++accepted;
            if (const char* field = kcadv::first_nonfinite(out)) { ++defects; std::cout << "DEFECT accepted key card has a non-finite " << field << ": " << label << "\n"; }
        }
    };
    for (const auto& bad : advtest::damage(original, 3)) judge(bad.dump(), bad.dump().substr(0, 300));
    judge("", "empty file");
    judge(baseText.substr(0, baseText.size() / 2), "truncated card");
    judge("[]", "array instead of object");
    judge(std::string(300000, '[') + std::string(300000, ']'), "deeply nested card");
    judge(std::string("\xff\xfe\xfd", 3) + baseText, "binary prefix");

    for (const auto& path : {work / "missing.starscene", work, fs::path(work / "card.starscene" / "nested.starscene")}) {
        keycard::KeyCard out;
        std::string error;
        ++tried;
        try { error = keycard::loadKeyCard(utf8(path.wstring()), out); }
        catch (...) { ++defects; std::cout << "DEFECT key card path threw: " << utf8(path.wstring()) << "\n"; continue; }
        if (error.empty() || !out.lights.empty() || !out.parts.empty()) { ++defects; std::cout << "DEFECT missing or unreadable card path accepted: " << utf8(path.wstring()) << "\n"; }
        else ++refused;
    }
    {
        keycard::KeyCard out;
        std::string error;
        ++tried;
        try { error = keycard::loadKeyCard(std::string("\xff\xfe.starscene"), out); }
        catch (...) { ++defects; std::cout << "DEFECT key card path with invalid UTF-8 threw\n"; error.clear(); }
        if (error.empty()) { ++defects; std::cout << "DEFECT key card path with invalid UTF-8 accepted\n"; }
        else ++refused;
    }

    std::cout << "ADVERSARIAL key cards tried=" << tried << " refused=" << refused << " accepted=" << accepted << " defects=" << defects << "\n";
    require(tried > 100 && accepted > 0 && defects == 0, "Key card adapter: damaged key cards are refused with a message and no content, or accepted with finite values, and never throw");
}
