#include "commands.hpp"
#include "geometry.hpp"
#include "mesh.hpp"
#include "platform.hpp"
#include "render_scene.hpp"
#include "assets.hpp"
#include "interactions.hpp"
#include <cstring>
#include <iostream>
using namespace starlight::editor;
int main(int argc, char **argv) {
    std::vector<std::string> checks;
    auto require = [&](bool ok, const char *name) {
        if (!ok)
            throw std::runtime_error(name);
        checks.emplace_back(name);
    };
    fs::path folder = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "Starlight-native-checks";
    fs::create_directories(folder);
    try {
        SceneData s;
        s.Terrain.emplace();
        Character c;
        c.Route = {{3, 0}, {3, 4}};
        c.Speed = 2;
        s.Characters.push_back(c);
        require(c.at(1).X == 2 && c.at(2).Z == 1 && c.at(20).Z == 4,
                "Route interpolation and completion match managed behavior");
        s.Terrain->brush(0, 0, 2, 1, "raise", 0, 0, .05);
        require(s.Terrain->height_at(0, 0) > .09, "Terrain raise");
        auto copy = SceneData::decode(s.encode());
        require(copy.Characters[0].Id == c.Id && copy.Terrain->Heights == s.Terrain->Heights,
                "Scene schema7 roundtrip");
        require(SceneData::decode("{\"SchemaVersion\":6}").SchemaVersion == 7, "Older scene migration");
        for (const auto *text : {"null", "{\"Characters\":null}", "{\"SchemaVersion\":99}",
                                 "{\"PathBounces\":99}", "{\"Terrain\":{\"Heights\":[]}}",
                                 "{\"SchemaVersion\":7.5}", "{\"PathBounces\":4294967297}",
                                 "{\"Characters\":[{\"Sculpt\":{\"Positions\":[0,0,0],\"Indices\":[0.1,0,0]}}]}"}) {
            bool rejected = false;
            try {
                SceneData::decode(text);
            } catch (...) {
                rejected = true;
            }
            require(rejected, "Malformed scene rejected");
        }
        auto file = folder / L"roundtrip.starscene";
        save_scene(s, file);
        auto old = hash_file(file);
        s.Name = "Updated";
        save_scene(s, file, old);
        require(SceneData::decode(read_file(file)).Name == "Updated", "Atomic scene save");
        bool stale = false;
        try {
            save_scene(s, file, old);
        } catch (...) {
            stale = true;
        }
        require(stale, "Stale edit rejected");
        configure(c, 2, -1, 2.25, .75);
        require(c.Route[0].X == 5 && c.Route[0].Z == -1, "Character configure moves route");
        SculptMesh{}.validate();
        SculptMesh wide{{9999, 0, 0}, {}};
        wide.validate();
        require(true, "Legacy sculpt limits preserved");
        Character transform;
        transform.Width = 2;
        transform.Height = .5;
        transform.Depth = 3;
        transform.Size = 1.8;
        transform.X = 4;
        transform.Y = -2;
        transform.Z = 1;
        transform.RotationX = 23;
        transform.RotationY = 81;
        transform.RotationZ = -47;
        Vec3 p{.2, 1, -.7};
        require((local_point(transform, world_point(transform, p)) - p).length() < 1e-12,
                "Sculpt inverse transform with rotation and nonuniform scale");
        Mesh tri;
        tri.vertices = {{{0, 0, 0}}, {{0, 0, 1}}, {{1, 0, 0}}};
        tri.indices = {0, 1, 2};
        Character sculpt;
        auto hit = mesh_hit(tri, sculpt, {.2, 2, .2}, {0, -1, 0});
        require(hit && std::abs(hit->distance - 2) < 1e-12 && hit->normal.y > 0,
                "Nearest triangle picking and facing normal");
        require(!mesh_hit(tri, sculpt, {2, 2, 2}, {0, -1, 0}), "Ray outside triangle rejected");
        sculpt.Sculpt = prepare_sculpt(tri);
        require(sculpt.Sculpt->Indices.size() == 12288, "Sculpt subdivision preserves six-pass limit");
        auto before = *sculpt.Sculpt;
        sculpt_brush(sculpt, {0, 0, 0}, {0, 1, 0}, .5, 1, "pull", false, .05);
        require(sculpt.Sculpt->Positions[1] > .039, "Pull brush modifies local mesh");
        sculpt.Sculpt = before;
        sculpt_brush(sculpt, {0, 0, 0}, {0, 1, 0}, .5, 1, "pull", true, .05);
        require(sculpt.Sculpt->Positions[1] < -.039, "Reverse brush changes direction");
        SceneHistory history;
        auto original = s.Name;
        history.begin(s);
        s.Name = "Stroke";
        history.commit();
        require(history.undo(s) && s.Name == original, "Undo transaction");
        require(history.redo(s) && s.Name == "Stroke", "Redo transaction");
        history.begin(s);
        s.Name = "Cancelled";
        history.cancel(s);
        require(s.Name == "Stroke", "Cancel restores active transaction");
        for (const auto &kind : shapes) {
            Character shape;
            shape.Shape = kind;
            auto parts = shape_geometry(shape);
            require(!parts.empty(), "Shape has native geometry");
            for (const auto &part : parts) {
                part.mesh.validate();
                require(!part.mesh.indices.empty(), "Shape triangles valid");
            }
        }
        Character cube;
        cube.Shape = "cube";
        auto cubeMesh = shape_geometry(cube)[0].mesh;
        require(cubeMesh.vertices.size() == 24 && cubeMesh.indices.size() == 36, "Cube face topology");
        Character grass;
        grass.Shape = "grass";
        auto grassMesh = shape_geometry(grass)[0].mesh;
        require(grassMesh.vertices.size() == 1000 && grassMesh.indices.size() == 2400,
                "Grass blade topology");
        TerrainData flat;
        auto terrain = terrain_geometry(flat);
        require(terrain.size() == 1 && terrain[0].mesh.vertices.size() == 193 * 193 &&
                    terrain[0].mesh.indices.size() == 192 * 192 * 6,
                "Terrain render sampling");
        flat.WaterEnabled = true;
        flat.WaterLevel = .5;
        auto wet = terrain_geometry(flat);
        require(wet.size() == 2 && wet[1].mesh.indices.size() == 192 * 192 * 6,
                "Water clipping on submerged plane");
        stamp_shape(flat, cube);
        require(flat.height_at(0, 0) == 1 && flat.height_at(2, 2) == 0,
                "Terrain stamp raises footprint only");
        if (argc > 2) {
            auto reference = json::parse(read_file(argv[2]));
            for (auto it = reference.begin(); it != reference.end(); ++it) {
                Character shape;
                shape.Shape = it.key();
                auto actual = shape_geometry(shape);
                require(actual.size() == it.value().size(), "Managed/native geometry part count");
                for (size_t p = 0; p < actual.size(); p++) {
                    const auto &expected = it.value()[p];
                    const auto &mesh = actual[p].mesh;
                    require(mesh.vertices.size() == expected.at("vertices").get<size_t>() &&
                                mesh.indices.size() == expected.at("indices").get<size_t>(),
                            "Managed/native geometry topology");
                    Vec3 sum;
                    for (const auto &v : mesh.vertices)
                        sum = sum + v.position;
                    auto e = expected.at("sum").get<std::vector<double>>();
                    require((sum - Vec3{e[0], e[1], e[2]}).length() < 1e-8,
                            "Managed/native geometry position sums");
                    for (size_t i = 0; i < expected.at("positions").size(); i++) {
                        auto q = expected.at("positions")[i].get<std::vector<double>>();
                        require((mesh.vertices[i].position - Vec3{q[0], q[1], q[2]}).length() < 1e-12,
                                "Managed/native seeded vertex coordinates");
                    }
                }
            }
        }
        require(rgb("DarkSlateBlue").z == 139. / 255 && rgb("#abc").x == 170. / 255,
                "Legacy named and short hex colors");
        auto request = folder / "request.json";
        json ops = {{"schemaVersion", 1},
                    {"scenePath", utf8(fs::absolute(file).wstring())},
                    {"expectedSha256", hash_file(file)},
                    {"operations",
                     json::array({json{{"op", "shape"}, {"shape", "cube"}, {"name", "Native command"}}})}};
        atomic_write(request, ops.dump());
        auto response = apply_commands(request);
        require(response.at("success").get<bool>() &&
                    SceneData::decode(read_file(file)).Characters.size() == 2,
                "Native command transaction saves scene");
        auto hash = hash_file(file);
        ops["expectedSha256"] = hash;
        ops["operations"].push_back(json{{"op", "invalid"}, {"id", c.Id}});
        atomic_write(request, ops.dump());
        bool invalid = false;
        try {
            apply_commands(request);
        } catch (...) {
            invalid = true;
        }
        require(invalid && hash_file(file) == hash, "Failed command batch preserves original bytes");
        TerrainFlow flow(TerrainData{}, {{0, 0, false, 2}, {2, 0, true, 1}});
        for (int i = 0; i < 200; i++)
            flow.step(.025);
        require(std::abs(flow.volume() - 3) < 1e-10 && std::abs(flow.emitted - 3) < 1e-10,
                "Closed fluid volume conserved after emission");
        FlowObstacleMask mask;
        mask.add(cubeMesh, cube);
        mask.finish();
        require(mask.coverage[TerrainFlow::index(.1, .1)] == 1 && mask.top[TerrainFlow::index(.1, .1)] == 1,
                "Obstacle voxel footprint samples solid cube");
        flow.set_obstacles(std::move(mask));
        for (int i = 0; i < 40; i++)
            flow.step(.025);
        require(std::abs(flow.volume() - 3) < 1e-10, "Obstacle transport conserves liquid");
        for (const auto &part : flow.geometry())
            part.mesh.validate();
        require(!flow.geometry().empty(), "Fluid native surface mesh");
        SceneData grouped;
        Character first, second;
        first.Shape = second.Shape = "cube";
        first.KeepAboveFloor = second.KeepAboveFloor = false;
        second.X = 2;
        second.Route = {{3, 1}};
        grouped.Characters = {first, second};
        std::unordered_map<std::string, Character> originals{{first.Id, first}, {second.Id, second}};
        grouped.Characters[0].X = 1;
        move_group(grouped, grouped.Characters[0], originals);
        require(grouped.Characters[1].X == 3 && grouped.Characters[1].Route[0].X == 4,
                "Group translation preserves relative route offsets");
        grouped.Characters[0].X = .74;
        snap_object(grouped, grouped.Characters[0], true, false, false);
        require(grouped.Characters[0].X == .5, "Native grid snapping");
        require(terrain_hit(nullptr, {0, 2, 0}, {0, -1, 0}).has_value() &&
                    !terrain_hit(nullptr, {0, 2, 0}, {0, 1, 0}).has_value(),
                "Ground picking rejects rays pointed away from surface");
        if (argc > 2) {
            auto fixture = fs::path(argv[2]).parent_path() / "fixtures/triangle.glb";
            auto parts = load_glb(fixture);
            require(!parts.empty() && parts[0].mesh.vertices.size() == 3, "Native GLB triangle import");
            auto bytes = read_file(fixture);
            uint32_t jsonSize;
            std::memcpy(&jsonSize, bytes.data() + 12, 4);
            auto description = json::parse(bytes.substr(20, jsonSize));
            auto suffix = bytes.substr(20 + jsonSize);
            auto malformed = [&](json document) {
                auto text = document.dump();
                while (text.size() % 4) text += ' ';
                auto result = bytes.substr(0, 20);
                uint32_t size = uint32_t(text.size()), total = uint32_t(20 + size + suffix.size());
                std::memcpy(result.data() + 8, &total, 4);
                std::memcpy(result.data() + 12, &size, 4);
                result += text + suffix;
                auto path = folder / "malformed.glb";
                atomic_write(path, result);
                try { load_glb(path); return false; } catch (const std::exception &) { return true; }
            };
            auto bad = description;
            bad["accessors"][0]["count"] = 3.5;
            require(malformed(bad), "Fractional GLB counts rejected");
            bad = description; bad["accessors"][0]["byteOffset"] = 2147483648ull;
            require(malformed(bad), "Overflow GLB offsets rejected");
            bad = description; bad["accessors"][0]["componentType"] = 5126.5;
            require(malformed(bad), "Fractional GLB component types rejected");
            bad = description; bad["bufferViews"][0]["byteStride"] = 4;
            require(malformed(bad), "Invalid GLB vertex stride rejected");
            bad = description; bad["nodes"][0]["children"] = json::array({0});
            require(malformed(bad), "Cyclic GLB node graph rejected");
        }
        SceneData render;
        render.Terrain.emplace();
        Character rock;
        rock.Shape = "chaos-rock";
        render.Characters.push_back(rock);
        auto packet = render_scene(render);
        atomic_write(folder / "native-scene.bin",
                     std::string_view(reinterpret_cast<const char *>(packet.data()), packet.size()));
        require(packet.size() > 1000, "Native renderer packet emitted");
        atomic_write(folder / "result.json", json{{"success", true}, {"checks", checks}}.dump(2));
        std::cout << "PASS " << checks.size() << " native checks\n";
        return 0;
    } catch (const std::exception &e) {
        atomic_write(folder / "result.json",
                     json{{"success", false}, {"checks", checks}, {"error", e.what()}}.dump(2));
        std::cerr << e.what() << '\n';
        return 1;
    }
}
