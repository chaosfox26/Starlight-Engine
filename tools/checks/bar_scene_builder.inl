// Builds a brand-new "bar" scene the way a person does in the editor: New scene, pick a shape in the shape list and press Add, change
// its properties (the same edit() the inspector uses), give objects behaviours (assign_object_behavior, the Behavior menu action),
// turn some shapes into sculpted models, set combat values, then Play. Nothing here is a generated scene file and nothing prepares the
// scene for performance. Returns the plan the stress driver needs (who the boss is, which rocks must break, where the player starts).
nlohmann::json build_bar_scene_through_editor() {
    using nlohmann::json;
    command(New);
    auto addShape = [&](const char* kind, const std::string& label, double x, double z, const std::function<void(Character&)>& setup) -> std::string {
        const LRESULT found = SendMessageW(shape, CB_FINDSTRINGEXACT, WPARAM(-1), reinterpret_cast<LPARAM>(wide(kind).c_str()));
        if (found == CB_ERR) throw std::runtime_error(std::string("The editor shape list has no ") + kind);
        SendMessageW(shape, CB_SETCURSEL, WPARAM(found), 0);
        command(Add);
        const size_t index = scene.Characters.size() - 1;
        edit([&] {
            auto& c = scene.Characters[index];
            c.Name = label;
            c.X = x;
            c.Z = z;
            c.Y = 0;
            setup(c);
        }, false);
        return scene.Characters[index].Id;
    };
    auto breakable = [&](const std::string& id, double health) {
        edit([&] {
            assign_object_behavior(scene, id, "breakable");
            for (auto& c : scene.Characters)
                if (c.Id == id) c.DestructionHealth = health;
        }, false);
    };
    edit([&] {
        scene.Name = "Moonlit bar";
        scene.GroundColor = "#2B1A12";
        scene.Hour = 0;
        scene.DayCycle = false;
    }, false);
    const double playerX = -5.5, playerZ = 4.0;
    // Room: back wall, side walls, a short front wall either side of the door.
    addShape("rectangle", "Back wall", 0, -6, [](Character& c) { c.Width = 18; c.Height = 3.2; c.Depth = .4; c.Color = "#3A2A44"; });
    addShape("rectangle", "Left wall", -9, 0, [](Character& c) { c.Width = .4; c.Height = 3.2; c.Depth = 12; c.Color = "#3A2A44"; });
    addShape("rectangle", "Right wall", 9, 0, [](Character& c) { c.Width = .4; c.Height = 3.2; c.Depth = 12; c.Color = "#3A2A44"; });
    addShape("rectangle", "Front wall left", -6, 6, [](Character& c) { c.Width = 6; c.Height = 3.2; c.Depth = .4; c.Color = "#3A2A44"; });
    addShape("rectangle", "Front wall right", 6, 6, [](Character& c) { c.Width = 6; c.Height = 3.2; c.Depth = .4; c.Color = "#3A2A44"; });
    addShape("rectangle", "Bar counter", 1.5, -4.2, [](Character& c) { c.Width = 8; c.Height = 1.1; c.Depth = .9; c.Color = "#6B3E1F"; });
    for (int k = 0; k < 6; ++k)
        addShape("octagon", "Stool " + std::to_string(k + 1), -2.5 + k * 1.2, -3.1, [](Character& c) { c.Width = .55; c.Height = .65; c.Depth = .55; c.Color = "#8A4B2A"; });
    for (int k = 0; k < 5; ++k)
        addShape("octagon", "Table " + std::to_string(k + 1), 2.5 + (k % 3) * 2.2, -0.2 + (k / 3) * 3.0, [](Character& c) { c.Width = 1.4; c.Height = .8; c.Depth = 1.4; c.Color = "#7A4A2A"; });
    // Warm and cool lighting, two fireplaces.
    for (int k = 0; k < 4; ++k)
        addShape("light", "Lantern " + std::to_string(k + 1), -6 + k * 4.5, -1, [k](Character& c) { c.Y = 2.4; c.Color = k % 2 ? "#FF9A3C" : "#6C9BFF"; c.LightIntensity = 8; c.LightRange = 7; });
    addShape("fire", "Fireplace left", -8.3, -2.5, [](Character& c) { c.ParticleRate = 40; c.Color = "#FF8A30"; c.Size = .7; });
    addShape("fire", "Fireplace right", 8.3, -2.5, [](Character& c) { c.ParticleRate = 40; c.Color = "#FF8A30"; c.Size = .7; });
    // Decor made with the sculpt conversion (the editor's own step that turns a shape into a model).
    for (int k = 0; k < 4; ++k)
        addShape("boulder", "Statue " + std::to_string(k + 1), -7.8 + k * 5.2, -5.2, [](Character& c) { c.Size = .9; c.Color = "#9AA0B8"; });
    edit([&] {
        for (auto& c : scene.Characters)
            if (c.Name.rfind("Statue ", 0) == 0) c.Sculpt = prepare_sculpt(object_mesh(c));
    }, false);
    // Breakable stone casks and bottles, with the Breakable behaviour. Staged durability so they break one attack at a time.
    json rocks = json::array();
    auto cask = [&](const std::string& name, const char* kind, double x, double z, double size, double health, const char* group) {
        const auto id = addShape(kind, name, x, z, [size](Character& c) { c.Size = size; c.Color = "#8A7660"; });
        breakable(id, health);
        rocks.push_back({{"id", id}, {"expect", true}, {"group", group}, {"health", health}});
    };
    // The scripted player always fights the boss; the casks stand on the line between them, so ordinary attacks at the boss break them.
    const double laneX = (6.8 - playerX), laneZ = (-1.8 - playerZ), laneLength = std::hypot(laneX, laneZ);
    const double ux = laneX / laneLength, uz = laneZ / laneLength, vx = -uz, vz = ux;
    auto onLane = [&](double distance, double sideways, double& x, double& z) { x = playerX + ux * distance + vx * sideways; z = playerZ + uz * distance + vz * sideways; };
    double cx = 0, cz = 0;
    onLane(1.0, 0, cx, cz); cask("Cask 1", "rock", cx, cz, .5, 10, "early");
    onLane(1.4, -.28, cx, cz); cask("Cask 2", "boulder", cx, cz, .5, 20, "single-0");
    onLane(1.4, .28, cx, cz); cask("Cask 3", "rock", cx, cz, .5, 30, "single-1");
    onLane(1.8, -.36, cx, cz); cask("Cask 4", "rock", cx, cz, .42, 20, "group");
    onLane(1.8, 0, cx, cz); cask("Cask 5", "boulder", cx, cz, .42, 20, "group");
    onLane(1.8, .36, cx, cz); cask("Cask 6", "rock", cx, cz, .42, 20, "group");
    // Enemies, entered like the inspector's combat fields: health, team, automatic attack, pursuit, sight.
    std::string bossId;
    auto fighter = [&](const std::string& name, double x, double z, double size, double health, bool boss) {
        const auto id = addShape("cube", name, x, z, [&](Character& c) {
            c.Size = size; c.Color = boss ? "#B03030" : "#C06020";
            c.CombatHealth = health; c.CombatTeam = 2; c.CombatBoss = boss; c.CombatAutoAttack = true; c.CombatPursuit = true; c.CombatSightRange = 30;
        });
        if (boss) bossId = id;
        return id;
    };
    const double bossX = 6.8, bossZ = -1.8;
    fighter("Bar boss", bossX, bossZ, 1.2, 1.0e6, true);
    // Player start, collision on (the same Game settings the editor offers).
    edit([&] {
        if (!scene.Game) scene.Game.emplace();
        scene.Game->Collision = true;
        scene.Game->SpawnX = playerX;
        scene.Game->SpawnZ = playerZ;
        scene.Game->SpawnY = 1.8;
    }, false);
    refresh();
    // Face the boss from the start (the boss is the pursuit target for the camera heading).
    json plan = {{"name", "bar"}, {"boss", bossId}, {"bossPursuit", true}, {"aimRocks", false}, {"beats", 14}, {"settleMs", 31000}, {"extraBeats", 200},
                 {"amp", .5}, {"ampBreak", 0.0}, {"phaseA", 25.0}, {"heading", std::atan2(bossX - playerX, -(bossZ - playerZ))},
                 {"player", {{"x", playerX}, {"z", playerZ}}}, {"rocks", rocks},
                 {"camera", {{"yawAmp", .45}, {"yawPeriod", 41.0}, {"pitchBase", -.45}, {"pitchAmp", .15}, {"pitchPeriod", 29.0}, {"distBase", 8.0}, {"distAmp", 1.5}, {"distPeriod", 53.0}}},
                 {"settleRoute", json::array({json::array({0, 0, -.45, 8.0}), json::array({6, 0, -.45, 8.0}), json::array({9, 1.0, -.35, 6.0}), json::array({16, 1.0, -.35, 6.0}),
                                              json::array({18.5, -1.2, -.35, 6.0}), json::array({25, -1.2, -.35, 6.0}), json::array({27.5, 0, -.45, 8.0}), json::array({60, 0, -.45, 8.0})})}};
    return plan;
}
