template<class Require> void render_split_checks(Require require){
    auto make_effect_character=[](const char* shape,double x,double y,double z,bool dynamic){Character c;c.Shape=shape;c.X=x;c.Y=y;c.Z=z;c.Dynamic=dynamic;return c;};
    {
        const char *effectShapes[] = {"fire", "light", "chaos-rock", "light-shard", "chaos-lightning", "craxis-smoke", "lava", "grass"};
        const char *otherShapes[] = {"rock", "boulder", "pebbles", "cube", "octagon", "rectangle", "trapezoid"};
        for (auto k : effectShapes) {
            std::optional<std::string> s = k;
            require(is_effect_shape(s), k);
            Character c = make_effect_character(k, 0, 0, 0, true);
            c.Route.clear();
            require(!resting_eligible(c), "effect shapes never rest");
        }
        for (auto k : otherShapes)
            require(!is_effect_shape(std::optional<std::string>(k)), k);
        for (auto k : {"fire", "light", "chaos-rock", "light-shard", "chaos-lightning"})
            require(effect_channels(std::optional<std::string>(k)) & EffectLight, "light channel");
        for (auto k : {"fire", "craxis-smoke"})
            require(effect_channels(std::optional<std::string>(k)) & EffectEmitter, "emitter channel");
        for (auto k : {"grass", "fire", "light", "lava", "chaos-lightning", "light-shard", "craxis-smoke"})
            require((effect_channels(std::optional<std::string>(k)) & EffectPassable) && !blocks_movement(make_effect_character(k, 0, 0, 0, false)), "passable matches blocks_movement");
        require(!(effect_channels(std::optional<std::string>("chaos-rock")) & EffectPassable) && blocks_movement(make_effect_character("chaos-rock", 0, 0, 0, false)), "chaos-rock stays solid");
    }
    {
        SceneData scene;
        scene.Characters.push_back(make_effect_character("rock", -2, 0, 0, false));
        scene.Characters.push_back(make_effect_character("grass", 2, 0, 1, false));
        scene.Characters.push_back(make_effect_character("fire", 0, 0, 2, false));
        scene.Characters.push_back(make_effect_character("light", 1, 1, -2, false));
        scene.Characters.push_back(make_effect_character("light-shard", 3, 0, -1, false));
        scene.Characters.push_back(make_effect_character("craxis-smoke", 3, 0, 3, false));
        scene.Characters[2].ParticleRate = 20;
        scene.Characters.push_back(make_effect_character("chaos-rock", -1, 0, -3, true));
        scene.Characters.push_back(make_effect_character("chaos-rock", 1, 0, -4, true));
        scene.validate();
        for (bool runtime : {false, true}) {
            RenderSceneCache cache;
            std::vector<uint8_t> delta;
            const auto before = bypass_counters().effectDeltaFrames.load();
            const auto actorsBefore = bypass_counters().effectDeltaActors.load();
            const bool usable = render_scene_delta(scene, nullptr, {}, cache, runtime, false, delta);
            require(usable && cache.token != 0 && !cache.packet.empty(), "dynamic light emitters keep a resident base");
            require(bypass_counters().effectDeltaFrames.load() == before + 1 && bypass_counters().effectDeltaActors.load() == actorsBefore + 2, "effect delta frame and actors counted");
            const auto token = cache.token;
            const auto packet = cache.packet;
            require(render_scene(scene, nullptr, {}, &cache, runtime) == render_scene(scene, nullptr, {}, nullptr, runtime), "resident base plus dynamic tail equals the whole packet");
            auto moved = scene;
            moved.Characters[6].X = 1.5;
            moved.Characters[7].Y = 2;
            std::vector<uint8_t> next;
            require(render_scene_delta(moved, nullptr, {}, cache, runtime, false, next), "moved light emitters still use the delta");
            require(cache.token == token && cache.packet == packet, "moving them does not rebuild or change the resident base");
            require(next != delta, "moved emitters change the delta");
            require(render_scene(moved, nullptr, {}, &cache, runtime).size() == render_scene(moved, nullptr, {}, nullptr, runtime).size(), "moved: cached whole has the same size as the uncached whole");
            auto lightsBefore = environment_lights(scene, 0, nullptr);
            auto lightsAfter = environment_lights(moved, 0, nullptr);
            require(lightsBefore.size() == 5 && lightsBefore != lightsAfter, "light channel reflects moved dynamic emitters");
            require(fog_emitters(scene, 0, nullptr) == fog_emitters(moved, 0, nullptr) && fog_emitters(scene, 0, nullptr).size() == 2, "emitter channel lists fire and smoke");
        }
    }
    {
        Character a = make_effect_character("rock", 0, 0, 0, true), b = make_effect_character("rock", 5, 1, 2, true);
        a.Color = b.Color = "#808080";
        a.Size = b.Size = 1.5;
        auto bodyA = resident_body(a), bodyB = resident_body(b);
        require(bodyA == bodyB && !bodyA->parts.empty(), "bodies with the same geometry share one resident body");
        require(bodyA->part_identity(0) == bodyB->part_identity(0) && (bodyA->part_identity(0) >> 63) == 1, "content identity is stable and tagged");
        require(bodyA->part_identity(0) != bodyA->part_identity(1), "part identities differ");
        Character c = a;
        c.Color = "#AA2020";
        auto bodyC = resident_body(c);
        require(bodyC != bodyA && bodyC->key != bodyA->key, "a different color is a different resident body");
        require(bodyC->parts[0]->color.x != bodyA->parts[0]->color.x, "color is not taken from the first body that used the geometry");
        const auto statsBefore = render_geometry_stats();
        std::vector<std::shared_ptr<const ResidentBody>> held;
        held.push_back(bodyA);
        for (int i = 0; i < 700; ++i) {
            Character r = make_effect_character("rock", 0, 0, 0, true);
            r.Size = 0.5 + 0.001 * i;
            auto body = resident_body(r);
            if (i < 3)
                held.push_back(body);
        }
        const auto statsAfter = render_geometry_stats();
        require(statsAfter.bodyEvicted > statsBefore.bodyEvicted, "bounded cache evicts");
        require(statsAfter.bodyEntries <= 600, "entries stay near the bound");
        require(resident_body(a) == bodyA, "an entry still referenced by the caller survives eviction");
        require(resident_body(a)->parts[0].get() == bodyA->parts[0].get(), "same mesh pointer after the sweep");
        auto p1 = shared_parts(a), p2 = shared_parts(b);
        Character scaled = a;
        scaled.Size = 3;
        scaled.Width = 2;
        require(shared_parts(scaled) == p1, "generated parts are shared across scales");
        for (int i = 0; i < 200; ++i)
            require(shared_parts(a) == p1, "repeat lookups hit");
        Character shifted = a;
        shifted.Color = "#102030";
        require(shared_parts(shifted) != p1 && (*shared_parts(shifted))[0].color.x != (*p1)[0].color.x, "color is part of the generator identity");
        require(p1 == p2, "parts are shared between objects with the same geometry");
        require(object_parts(a).size() == p1->size(), "object_parts copies the shared parts");
        auto mesh = object_mesh(a);
        require(!mesh.vertices.empty(), "object_mesh still merges the parts");
        Character bark=make_effect_character("cube",0,0,0,true);bark.Surface=12;bark.Foliage=FoliageMaterial{};
        Character leaf;leaf.Shape.reset();leaf.Surface=13;leaf.Dynamic=true;leaf.Foliage=FoliageMaterial{};
        SculptMesh sculpt;sculpt.Positions={0,0,0,.1,0,0,0,.2,0,0,0,.002};sculpt.Indices={0,2,1,0,1,3,0,3,2,1,2,3};sculpt.MaterialCoordinates={0,0,.37,1,0,.37,0,1,.37,0,0,.37};leaf.Sculpt=sculpt;
        bool barkKind=true,leafKind=true;size_t seen=0;
        for(const auto& part:resident_body(bark)->parts)for(const auto& v:part->mesh.vertices){barkKind&=v.kind==-40.f;++seen;}
        for(const auto& part:resident_body(leaf)->parts)for(const auto& v:part->mesh.vertices){leafKind&=v.kind==-41.f;++seen;}
        require(seen>0&&barkKind&&leafKind,"a Dynamic Bark or Leaf body drawn as a resident instance carries the foliage vertex tags (-40/-41), not the fire/dust volume kinds 12/13 (RS-1)");
        require(shared_parts(bark)->front().mesh.vertices.front().kind==12.f,"the shared authored geometry keeps kind 12 for the CPU side");
    }
}
