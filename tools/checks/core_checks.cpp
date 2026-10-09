#include "rejection_note.hpp"
#include "engine_assets.hpp"
#include "solid_fixture.hpp"
#include "commands.hpp"
#include "geometry.hpp"
#include "terrain_volume.hpp"
#include "mesh.hpp"
#include "platform.hpp"
#include "render_scene.hpp"
#include "../../deep/engine/persistent_workers.h"
#include "assets.hpp"
#include "interactions.hpp"
#include "object_template.hpp"
#include "asset_library.hpp"
#include "adventure.hpp"
#include "environment_effects.hpp"
#include "crystal_geometry.hpp"
#include "grapple_motion.hpp"
#include "player_presentation.hpp"
#include "gameplay_audio.hpp"
#include "frame_pacing.hpp"
#include "pacing_log.hpp"
#include "combat_control.hpp"
#include "quest_model.hpp"
#include "object_behaviors.hpp"
#include "adventure_template.hpp"
#include "tutorial_progress.hpp"
#include <cstring>
#include <thread>
#include <iostream>
using namespace starlight::editor;
#include "physics_boundary_checks.inl"
#include "physics_abi_checks.inl"
#include "physics_step_checks.inl"
#include "device_fault_checks.inl"
#include "scene_preparation_checks.inl"
#include "scene_validation_checks.inl"
#include "file_io_checks.inl"
#include "asset_platform_checks.inl"
#include "asset_scan_checks.inl"
#include "error_throttle_checks.inl"
#include "render_device_checks.inl"
#include "project_module_checks.inl"
#include "lightning_target_checks.inl"
#include "gameplay_audit_checks.inl"
#include "missing_asset_save_checks.inl"
#include "obstacle_removal_checks.inl"
#include "core_contract_lead_checks.inl"
#include "liquid_terrain_audit_checks.inl"
#include "glb_index_budget_checks.inl"
#include "glb_fuzz_checks.inl"
#include "id_remap_checks.inl"
#include "editor_state_checks.inl"
#include "migration_path_checks.inl"
#include "preset_validate_checks.inl"
#include "decode_fuzz_checks.inl"
#include "history_stroke_checks.inl"
#include "contact_index_checks.inl"
#include "mesh_reduce_checks.inl"
#include "terrain_brush_checks.inl"
#include "lightning_seed_checks.inl"
#include "rain_checks.inl"
#include "object_cache_checks.inl"
#include "cache_pin_checks.inl"
#include "key_card_checks.inl"
#include "graphics_runtime_checks.inl"
#include "adversarial_parser_checks.inl"
#include "club_schema_checks.inl"
#include "asset_root_checks.inl"
#include "asset_store_fuzz_checks.inl"
#include "active_scene_guard_checks.inl"
#include "environment_effects.hpp"
#include "render_split_checks.inl"
#include "lightning_graphics_checks.inl"
#include "lighting_fixtures.inl"
#include "water_wave_checks.inl"
#include "liquid_surface_checks.inl"
#include "liquid_obstacle_surface_checks.inl"
#include "foliage_material_checks.inl"
#include "terrain_material_checks.inl"
#include "lava_surface_checks.inl"
#include "lava_passage_geometry_checks.inl"
#include "rock_fracture_checks.inl"
#include "destruction_dust_checks.inl"
#include "object_destruction_checks.inl"
#ifdef STARLIGHT_CHARACTER_CONTROLLER
#include "terrain_destruction_checks.inl"
#include "physics_fracture_audit_checks.inl"
#endif
// Keep the large controller fixtures out of main's permanently reserved frame.
// Other phases must retain room for nested collision/mesh operations on Windows.
__declspec(noinline) static void character_controller_checks(const auto& require,const fs::path& folder){
#include "character_controller_checks.inl"
}
// STARLIGHT_CHECK_GROUPS=name1,name2 runs only those check groups (names as in the calls below; "list" prints them) and then exits 3.
// Unset, every group runs and nothing changes. The long inline checks after the groups only run in a full pass.
static bool groups_filtered() { const char *env = std::getenv("STARLIGHT_CHECK_GROUPS"); return env && *env; }
static bool group_wanted(const char *name) {
    const char *env = std::getenv("STARLIGHT_CHECK_GROUPS");
    if (!env || !*env) return true;
    if (std::string(env) == "list") { std::cout << name << "\n"; return false; }
    return ("," + std::string(env) + ",").find("," + std::string(name) + ",") != std::string::npos;
}
#define RUN_GROUP(name, ...) do { if (group_wanted(#name)) { __VA_ARGS__; } } while (0)
static int checks_main(int argc, char **argv) {
    std::vector<std::string> checks;
    fs::path folder = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "Starlight-native-checks";
    fs::create_directories(folder);
    {std::error_code removed;fs::remove(folder / "result.json", removed);fs::remove(folder / "progress.log", removed);}
    // One line per passed check, flushed as it passes: a crash or kill leaves a record of exactly which checks had passed.
    std::ofstream progress(folder / "progress.log", std::ios::binary);
    auto require = [&](bool ok, const char *name) {
        if (!ok)
            throw std::runtime_error(name);
        checks.emplace_back(name);
        progress << name << "\n";
        progress.flush();
    };
    EngineAssets::ScopedRoot checkLibrary(folder / "engine-assets");
    try {
        RUN_GROUP(active_scene_guard_checks, active_scene_guard_checks(require,folder,argc>2?fs::absolute(fs::path(argv[2])).parent_path().parent_path().lexically_normal():fs::path()));
        RUN_GROUP(physics_boundary_checks, physics_boundary_checks(require));
        RUN_GROUP(physics_abi_checks, physics_abi_checks(require));
        RUN_GROUP(physics_step_checks, physics_step_checks(require));
        RUN_GROUP(device_fault_checks, device_fault_checks(require));
        RUN_GROUP(scene_preparation_checks, scene_preparation_checks(require));
        RUN_GROUP(scene_validation_checks, scene_validation_checks(require));
        RUN_GROUP(file_io_checks, file_io_checks(require,folder));
        RUN_GROUP(asset_platform_checks, asset_platform_checks(require,folder));
        RUN_GROUP(asset_scan_checks, asset_scan_checks(require,folder));
        RUN_GROUP(error_throttle_checks, error_throttle_checks(require));
        RUN_GROUP(project_module_checks, project_module_checks(require,folder));
        RUN_GROUP(render_device_checks, render_device_checks(require));
        RUN_GROUP(lightning_target_checks, lightning_target_checks(require));
        RUN_GROUP(gameplay_audit_checks, gameplay_audit_checks(require));
        RUN_GROUP(missing_asset_save_checks, missing_asset_save_checks(require));
        RUN_GROUP(obstacle_removal_checks, obstacle_removal_checks(require));
        RUN_GROUP(core_contract_lead_checks, core_contract_lead_checks(require));
        RUN_GROUP(liquid_terrain_audit_checks, liquid_terrain_audit_checks(require));
        RUN_GROUP(glb_index_budget_checks, glb_index_budget_checks(require,folder));
        RUN_GROUP(glb_fuzz_checks, glb_fuzz_checks(require,folder,argc>2?fs::absolute(fs::path(argv[2])).parent_path().parent_path().lexically_normal():fs::path()));
        RUN_GROUP(id_remap_checks, id_remap_checks(require));
        RUN_GROUP(editor_state_checks, editor_state_checks(require,folder));
        RUN_GROUP(history_stroke_checks, history_stroke_checks(require));
        RUN_GROUP(migration_path_checks, migration_path_checks(require));
        RUN_GROUP(preset_validate_checks, preset_validate_checks(require,folder));
        RUN_GROUP(decode_fuzz_checks, decode_fuzz_checks(require,folder));
        RUN_GROUP(contact_index_checks, contact_index_checks(require));
        RUN_GROUP(mesh_reduce_checks, mesh_reduce_checks(require));
        RUN_GROUP(terrain_brush_checks, terrain_brush_checks(require));
        RUN_GROUP(lightning_seed_checks, lightning_seed_checks(require));
        RUN_GROUP(cache_pin_checks, cache_pin_checks(require));
        RUN_GROUP(rain_checks, rain_checks(require,folder));
        RUN_GROUP(object_cache_checks, object_cache_checks(require,folder));
        RUN_GROUP(key_card_checks, key_card_checks(require,folder));
        RUN_GROUP(graphics_runtime_checks, graphics_runtime_checks(require,folder));
        RUN_GROUP(adversarial_parser_checks, adversarial_parser_checks(require,folder));
        RUN_GROUP(club_schema_checks, club_schema_checks(require,folder));
        RUN_GROUP(model_asset_checks, model_asset_checks(require,folder));
        RUN_GROUP(asset_root_checks, asset_root_checks(require,folder));
        RUN_GROUP(asset_store_fuzz_checks, asset_store_fuzz_checks(require,folder));
        RUN_GROUP(render_split_checks, render_split_checks(require));
        RUN_GROUP(fracture_pose_checks, fracture_pose_checks(require));
        RUN_GROUP(ray_tree_checks, ray_tree_checks(require));
        RUN_GROUP(lightning_graphics_checks, lightning_graphics_checks(require,folder));
        lighting_fixtures(folder);
        RUN_GROUP(water_wave_checks, water_wave_checks(require,folder));
        RUN_GROUP(liquid_surface_checks, liquid_surface_checks(require,folder));
        RUN_GROUP(liquid_obstacle_surface_checks, liquid_obstacle_surface_checks(require,folder));
        RUN_GROUP(foliage_material_checks, foliage_material_checks(require,folder));
        RUN_GROUP(terrain_material_checks, terrain_material_checks(require,folder));
        RUN_GROUP(lava_surface_checks, lava_surface_checks(require,folder));
        RUN_GROUP(lava_passage_geometry_checks, lava_passage_geometry_checks(require));
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        RUN_GROUP(rock_fracture_checks, rock_fracture_checks(require,folder));
        RUN_GROUP(destruction_dust_checks, destruction_dust_checks(require,folder));
        RUN_GROUP(object_destruction_checks, object_destruction_checks(require,folder));
        RUN_GROUP(terrain_destruction_checks, terrain_destruction_checks(require,folder));
        RUN_GROUP(physics_fracture_audit_checks, physics_fracture_audit_checks(require));
#endif
        if (groups_filtered()) {
            std::cout << "STARLIGHT_CHECK_GROUPS is set: only the listed groups ran (" << checks.size() << " checks). This is a development run, not a full pass, and exits 3 so it cannot be mistaken for one.\n";
            atomic_write(folder / "result.json", json{{"schema", "starlight-check-result-2"}, {"success", true}, {"filtered", true}, {"total", checks.size()}, {"argv0", argv[0]}, {"checks", checks}}.dump(2));
            return 3;
        }
        {
            auto owner=std::make_unique<SceneData>();auto& saved=*owner;
            auto baseline=json::parse(saved.encode());baseline.erase("ShadowQuality");
            require(SceneData::decode(baseline.dump()).ShadowQuality==2,"Legacy scenes retain default Medium shadow quality");
            for(int quality=0;quality<4;++quality){saved.ShadowQuality=quality;require(SceneData::decode(saved.encode()).ShadowQuality==quality,"Saved shadow preset round trips without changing its value");}
            for(const auto& invalid:std::array<json,4>{json(-1),json(4),json(1.5),json("High")}){auto data=baseline;data["ShadowQuality"]=invalid;bool rejected=false;try{SceneData::decode(data.dump());}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}require(rejected,"Invalid shadow quality is rejected instead of silently truncating");}
        }
        {
            auto volume=[](const Mesh& mesh){double result=0;for(size_t i=0;i<mesh.indices.size();i+=3){auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;result+=a.dot(b.cross(c))/6.;}return result;};
            Mesh round,stretched,moved;Vec3 center{1,2,3},velocity{2,4,-1};
            append_particle_surface(round,center,velocity,.02,1,731,{1,1,1},7.2f);
            append_particle_surface(stretched,center,velocity,.02,3,731,{1,1,1},7.2f);
            append_particle_surface(moved,center+Vec3{.2,.4,0},velocity+Vec3{1,0,1},.02,2,731,{1,1,1},7.2f);
            std::map<std::pair<int,int>,int> edges;bool finite=true,outward=true,history=true;
            for(size_t i=0;i<stretched.indices.size();i+=3){auto a=stretched.vertices[stretched.indices[i]],b=stretched.vertices[stretched.indices[i+1]],c=stretched.vertices[stretched.indices[i+2]];outward&=(b.position-a.position).cross(c.position-a.position).dot((a.position+b.position+c.position)/3-center)>1e-15;for(int e=0;e<3;++e){int x=stretched.indices[i+e],y=stretched.indices[i+(e+1)%3];edges[{std::min(x,y),std::max(x,y)}]++;}}
            for(size_t i=0;i<stretched.vertices.size();++i){const auto& v=stretched.vertices[i];finite&=std::isfinite(v.position.x+v.position.y+v.position.z)&&std::abs(v.normal.length()-1)<1e-10&&v.normal.dot(v.position-center)>0;history&=v.motionIdentity&&v.motionIdentity==moved.vertices[i].motionIdentity;}
            atomic_write(folder/"particle-surface-diagnostics.json",json{{"outward",outward},{"finiteNormals",finite},{"closed",std::all_of(edges.begin(),edges.end(),[](const auto& edge){return edge.second==2;})},{"vertices",stretched.vertices.size()},{"indices",stretched.indices.size()},{"volume",volume(stretched)}}.dump(2));
            require(outward&&finite&&std::all_of(edges.begin(),edges.end(),[](const auto& edge){return edge.second==2;}),"Splash and spark particles are closed outward surfaces with smooth finite normals");
            require(std::abs(volume(round)-volume(stretched))<1e-12&&volume(round)>0,"Velocity stretch preserves droplet volume instead of inflating thin splash sheets");
            require(history,"Particle vertex history survives movement and velocity-driven deformation");
            Mesh rejected;append_particle_surface(rejected,center,velocity,0,1,731,{1,1,1},0);require(rejected.vertices.empty(),"Expired zero-size particle geometry is not submitted");
            SceneData splashScene;splashScene.Name="Three dimensional splash droplets";splashScene.FogDensity=0;splashScene.Hour=15;
            Character backdrop;backdrop.Shape="cube";backdrop.Id="splash-backdrop";backdrop.Y=0;backdrop.Z=-1.2;backdrop.Width=3;backdrop.Height=3;backdrop.Depth=.2;backdrop.Color="#CB6D40";splashScene.Characters={backdrop};
            EnvironmentEffects effects;effects.splash({{0,.8,0},{0,0,0},1,false});auto drops=effects.geometry(splashScene);bool material=true;
            for(const auto& part:drops)for(const auto& v:part.mesh.vertices)material&=std::abs(v.kind-7.2)<1e-5&&v.color.y>0&&std::abs(v.normal.length()-1)<1e-8;
            require(material&&!drops.empty()&&drops[0].mesh.indices.size()==effects.splash_count()*particle_surface().indices.size(),"Runtime water splashes use bounded 3D dielectric meshes without shoreline foam");
            auto saveSplash=[&](const char* name,const SceneData& scene,const std::vector<MeshPart>& overlay){auto packet=render_scene(scene,nullptr,overlay,nullptr,true);atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(packet.data()),packet.size()));};
            saveSplash("water-droplets",splashScene,drops);saveSplash("water-droplets-empty",splashScene,{});
            auto blue=splashScene;blue.Characters[0].Color="#3F7ACB";saveSplash("water-droplets-blue",blue,drops);saveSplash("water-droplets-blue-empty",blue,{});
            effects.step(splashScene,.025,.025);saveSplash("water-droplets-moved",splashScene,effects.geometry(splashScene));
            effects.reset();effects.splash({{0,.8,0},{0,0,0},1,true});auto molten=effects.geometry(splashScene);require(!molten.empty()&&molten[0].mesh.vertices[0].kind>8,"Lava droplets use molten crust shading on closed moving meshes");saveSplash("lava-droplets",splashScene,molten);
            effects.reset();for(int burst=0;burst<80;++burst)effects.splash({{(burst%8-3.5)*.18,.5+(burst/8)*.07,-.1}, {},1,false});
            auto particleStart=std::chrono::steady_clock::now();auto dense=effects.geometry(splashScene);double particleMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-particleStart).count();
            require(effects.splash_count()==1024&&dense.size()==1&&dense[0].mesh.vertices.size()==1024*particle_surface().vertices.size(),"Repeated splash bursts respect the particle cap and bounded shared mesh topology");
            atomic_write(folder/"particle-dense-budget.json",json{{"particles",effects.splash_count()},{"vertices",dense[0].mesh.vertices.size()},{"triangles",dense[0].mesh.indices.size()/3},{"geometryMs",particleMs},{"cpuMeshBytes",dense[0].mesh.vertices.capacity()*sizeof(Vertex)+dense[0].mesh.indices.capacity()*sizeof(int)}}.dump(2));saveSplash("water-droplets-dense",splashScene,dense);
            std::vector<double> timings;
            for(int trial=0;trial<15;++trial){auto start=std::chrono::steady_clock::now();auto sample=effects.geometry(splashScene);timings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());require(sample[0].mesh.vertices.size()==dense[0].mesh.vertices.size()&&sample[0].mesh.vertices.back().motionIdentity==dense[0].mesh.vertices.back().motionIdentity,"Repeated dense splash reconstruction preserves geometry and motion identity");}
            auto ordered=timings;std::sort(ordered.begin(),ordered.end());
            atomic_write(folder/"particle-rebuild-timing.json",json{{"samplesMs",timings},{"medianMs",ordered[ordered.size()/2]},{"vertices",dense[0].mesh.vertices.size()},{"triangles",dense[0].mesh.indices.size()/3}}.dump(2));
            json inventory=json::array();for(const auto& kind:shapes){Character object;object.Shape=kind;auto parts=object_parts(object);size_t vertices=0,triangles=0;for(const auto& part:parts){vertices+=part.mesh.vertices.size();triangles+=part.mesh.indices.size()/3;}inventory.push_back({{"shape",kind},{"parts",parts.size()},{"vertices",vertices},{"triangles",triangles},{"visualAcceptance","pending"}});}
            atomic_write(folder/"placeable-object-inventory.json",inventory.dump(2));
            SceneData magic;magic.Name="Chaos rock and luminous shard materials";magic.Hour=19;magic.FogDensity=0;
            Character rock;rock.Shape="chaos-rock";rock.Id="magic-rock";rock.X=-1;rock.Size=1.8;rock.Color="#9679CB";
            Character shard;shard.Shape="light-shard";shard.Id="magic-shard";shard.X=1.2;shard.Size=.8;shard.Color="#6C8EBF";
            magic.Characters={rock,shard};saveSplash("magic-stones",magic,{});auto tinted=magic;tinted.Characters[0].Color="#D53F26";tinted.Characters[1].Color="#D53F26";saveSplash("magic-stones-tinted",tinted,{});auto day=magic;day.Hour=15;saveSplash("magic-stones-day",day,{});
            const auto& crystal=crystal_surface();std::map<std::tuple<int64_t,int64_t,int64_t>,int> welded;std::vector<int> remap;Vec3 sum;bool crystalNormals=true;
            for(const auto& vertex:crystal.vertices){auto p=vertex.position;sum=sum+p;auto [at,added]=welded.emplace(std::tuple{std::llround(p.x*1e8),std::llround(p.y*1e8),std::llround(p.z*1e8)},int(welded.size()));remap.push_back(at->second);crystalNormals&=std::abs(vertex.normal.length()-1)<1e-10&&vertex.kind==11;}
            std::map<std::pair<int,int>,int> crystalEdges;bool crystalFaces=true;
            for(size_t i=0;i<crystal.indices.size();i+=3){auto a=crystal.vertices[crystal.indices[i]],b=crystal.vertices[crystal.indices[i+1]],c=crystal.vertices[crystal.indices[i+2]];auto cross=(b.position-a.position).cross(c.position-a.position);crystalFaces&=cross.length()>1e-10&&cross.dot(a.normal)>0;for(int e=0;e<3;++e){int x=remap[crystal.indices[i+e]],y=remap[crystal.indices[i+(e+1)%3]];crystalEdges[{std::min(x,y),std::max(x,y)}]++;}}
            require(crystalFaces&&crystalNormals&&volume(crystal)>0&&std::all_of(crystalEdges.begin(),crystalEdges.end(),[](const auto& edge){return edge.second==2;}),"Beveled crystal has a closed nondegenerate boundary with stable facet normals");
            auto bounds=object_bounds(shard);require(bounds.high.y>1.9&&bounds.low.y==0,"Rebuilt light shards retain an upright grounded authored transform");
            json positions=json::array();for(size_t i=0;i<std::min(size_t(20),crystal.vertices.size());++i){auto p=crystal.vertices[i].position;positions.push_back({p.x,p.y,p.z});}atomic_write(folder/"rebuilt-shard-reference.json",json{{"light-shard",json::array({json{{"vertices",crystal.vertices.size()},{"indices",crystal.indices.size()},{"positions",positions},{"sum",{sum.x,sum.y,sum.z}}}})}}.dump(2));
            SceneData materialScene;materialScene.Name="Authored surfaces on solid primitives";materialScene.FogDensity=0;materialScene.Hour=15;bool authoredSurface=true;
            for(int surface=1;surface<=6;++surface){Character object;object.Shape=surface==2?"rectangle":surface==3?"trapezoid":surface==4?"octagon":"cube";object.Surface=surface;object.Id="material-primitive-"+std::to_string(surface);object.Size=.7;object.X=(surface-3.5)*.9;materialScene.Characters.push_back(object);}
            auto reloaded=SceneData::decode(materialScene.encode());for(size_t i=0;i<reloaded.Characters.size();++i){const auto& object=reloaded.Characters[i];auto part=object_parts(object);for(const auto& vertex:part[0].mesh.vertices)authoredSurface&=vertex.kind==object.Surface;auto plain=object;plain.Surface=0;auto original=object_parts(plain);authoredSurface&=original[0].mesh.indices==part[0].mesh.indices;for(size_t v=0;v<part[0].mesh.vertices.size();++v)authoredSurface&=(part[0].mesh.vertices[v].position-original[0].mesh.vertices[v].position).length()<1e-12;}
            require(authoredSurface,"Primitive surface choices survive save and reload and reach rendering without altering solid geometry");saveSplash("material-primitives",materialScene,{});for(auto& object:materialScene.Characters)object.Surface=0;saveSplash("material-primitives-plain",materialScene,{});
            SceneData meadow;meadow.Name="Grass field - current overhaul prototype";meadow.Hour=15;meadow.FogDensity=.008;meadow.Terrain.emplace();
            for(int z=0;z<TerrainData::N;++z)for(int x=0;x<TerrainData::N;++x){double px=x*TerrainData::Step-TerrainData::Half,pz=z*TerrainData::Step-TerrainData::Half;meadow.Terrain->Heights[z*TerrainData::N+x]=.13*std::sin(px*.38)*std::cos(pz*.29);}
            for(int z=0;z<12;++z)for(int x=0;x<12;++x){Character patch;patch.Shape="grass";patch.Id="meadow-grass-"+std::to_string(z*12+x);patch.X=(x-5.5)*.8;patch.Z=(z-5.5)*.8-3;patch.Y=meadow.Terrain->height_at(patch.X,patch.Z);patch.Color=(x+z)%3==0?"#779952":(x+z)%3==1?"#9BAE67":"#638A46";patch.RotationY=std::fmod(x*137.+z*97.,360.);meadow.Characters.push_back(patch);}
            EnvironmentEffects meadowEffects;meadowEffects.step(meadow,.025,0);saveSplash("grass-field",meadow,meadowEffects.geometry(meadow));
            for(int tick=1;tick<=30;++tick)meadowEffects.step(meadow,.025,tick*.025);
            saveSplash("grass-field-wind",meadow,meadowEffects.geometry(meadow));atomic_write(folder/"grass-field.starscene",meadow.encode());
            std::vector<double> grassTimings;
            for(int tick=0;tick<15;++tick){auto start=std::chrono::steady_clock::now();meadowEffects.step(meadow,.025,.8+tick*.025);grassTimings.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
            auto orderedGrass=grassTimings;std::sort(orderedGrass.begin(),orderedGrass.end());
            atomic_write(folder/"grass-update-timing.json",json{{"patches",meadow.Characters.size()},{"blades",meadow.Characters.size()*100},{"samplesMs",grassTimings},{"medianMs",orderedGrass[orderedGrass.size()/2]}}.dump(2));
        }
        {
            SceneData fire;Character emitter;emitter.Shape="fire";fire.Characters={emitter};EnvironmentEffects effects;
            for(int i=0;i<20;++i)effects.step(fire,.025,i*.025);require(effects.flame_count()>0,"Fire emits independently moving physical flame particles");
            auto first=render_scene(fire,nullptr,effects.geometry(fire),nullptr,true);effects.step(fire,.025,.525);require(first!=render_scene(fire,nullptr,effects.geometry(fire),nullptr,true),"Fire geometry evolves rather than retaining static cones");
            fire.Characters.clear();for(int i=0;i<60;++i)effects.step(fire,.025,.55+i*.025);require(effects.flame_count()==0&&!effects.active(fire),"Removing a fire emitter allows existing particles to expire without endless emission");
            effects.reset();require(effects.flame_count()==0,"Reset clears fire simulation state");
        }
        {
            ProjectilePhysics shots;ProjectileTarget wall{"wall",{{-.5,0,-2.01},{.5,3,-1.99}},{{-.5,0,-2.01},{.5,3,-1.99}}};
            shots.fire({0,1,0},{0,0,-1},500,0);auto impacts=shots.step(.02,nullptr,std::span<const ProjectileTarget>(&wall,1));
            require(impacts.size()==1&&impacts[0].target=="wall"&&shots.shots().empty(),"Fast projectiles sweep thin walls and retire at the first impact");
            require(shots.step(.02,nullptr,std::span<const ProjectileTarget>(&wall,1)).empty(),"A retired projectile cannot apply a duplicate impact");
            ProjectileTarget crossing{"moving",{{-1.1,.5,-1.1},{-.9,1.5,-.9}},{{.9,.5,-1.1},{1.1,1.5,-.9}}};shots.fire({0,1,0},{0,0,-1},100,0);
            require(shots.step(.02,nullptr,std::span<const ProjectileTarget>(&crossing,1)).size()==1,"Relative sweeps catch a moving target crossing between frame endpoints");
            shots.fire({0,2,0},{1,0,0},5,9.81);for(int i=0;i<10;++i)shots.step(.05,nullptr,{});require(!shots.shots().empty()&&shots.shots()[0].position.y<1,"Projectile gravity changes the actual flight trajectory");
            shots.clear();shots.fire({0,1,0},{1,0,0},500,0);shots.step(.05,nullptr,{});require(shots.shots().empty(),"Projectiles retire at their bounded travel range");
            shots.clear();shots.fire({0,1,-2},{0,0,-1},5,0);require(shots.step(0,nullptr,std::span<const ProjectileTarget>(&wall,1)).empty()&&shots.shots().size()==1&&shots.shots()[0].age==0,"Paused projectile updates neither collide nor retire an overlapping shot");
            TerrainData impactSlope;for(int z=0;z<TerrainData::N;++z)for(int x=0;x<TerrainData::N;++x)impactSlope.Heights[z*TerrainData::N+x]=2+(x*TerrainData::Step-TerrainData::Half)*.5;
            shots.clear();shots.fire({0,3,0},{0,-1,0},100,0);auto slopeHits=shots.step(.02,&impactSlope,{});require(slopeHits.size()==1&&slopeHits[0].normal.dot(Vec3{-.5,1,0}.unit())>.999,"Projectile terrain contacts return the actual slope normal for outward impact effects");
            ProjectilePhysics fastRender,slowRender;fastRender.fire({0,10,0},{1,0,0},5,9.81);slowRender.fire({0,10,0},{1,0,0},5,9.81);for(int i=0;i<240;++i)fastRender.step(1./480,nullptr,{});for(int i=0;i<10;++i)slowRender.step(.05,nullptr,{});require((fastRender.shots()[0].position-slowRender.shots()[0].position).length()<1e-8,"Projectile trajectories agree at 480Hz and slow 50ms updates");
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;level.Game->ProjectileSpeed=5;
            Character target;target.Shape="cube";target.GameplayRole="breakable";target.Z=-4;target.Height=2;level.Characters={target};AdventurePlayer player;player.start(level);player.fire_button(true);player.fire_button(false);
            for(int i=0;i<305;++i)player.move(0,0,1./120.);
            require(player.scene().Characters.size()==1&&!player.presentation().projectiles.empty(),"Physical shot mode leaves its target alive while the shot travels");
            for(int i=0;i<150;++i)player.move(0,0,1./120.);
            require(player.scene().Characters.empty()&&player.presentation().projectiles.empty(),"Projectile impact invokes gameplay destruction only on arrival");
            auto restored=SceneData::decode(level.encode());require(restored.Game->ProjectileSpeed==5,"Projectile behavior persists through scene save and load");
            player.start(level);require(player.presentation().projectiles.empty(),"Restart clears all projectiles");
            Character barrier;barrier.Shape="cube";barrier.Breakable=false;barrier.Z=-2;barrier.Width=4;barrier.Height=3;barrier.Depth=.03;level.Characters.push_back(barrier);player.start(level);player.fire_button(true);player.fire_button(false);for(int i=0;i<600;++i)player.move(0,0,1./120.);require(player.scene().Characters.size()==2,"A physical shot hits intervening scenery instead of damaging a target behind it");

        }
        {
            Character cube;cube.Shape="cube";auto source=shape_geometry(cube).front().mesh;
            auto volume=[](const Mesh& m){double sum=0;for(size_t i=0;i<m.indices.size();i+=3)sum+=m.vertices[m.indices[i]].position.dot(m.vertices[m.indices[i+1]].position.cross(m.vertices[m.indices[i+2]].position))/6.;return std::abs(sum);};
            require(closed_convex_mesh(source),"Fracture recognizes closed convex topology independent of the object name");
            auto open=source;open.indices.resize(open.indices.size()-3);require(!closed_convex_mesh(open),"Open surfaces are not falsely treated as closed fracture solids");
            Character sculpt=cube;sculpt.Shape.reset();sculpt.Sculpt=prepare_sculpt(source);require(closed_convex_mesh(object_mesh(sculpt)),"Convex sculpted meshes qualify for physical fracture");
            auto pieces=fracture_convex(source,{.4,.5,.6});double total=0;bool valid=true;
            for(const auto& piece:pieces){total+=volume(piece.part->mesh);valid=valid&&piece.radius>0&&std::isfinite(piece.radius);}
            require(pieces.size()==16&&valid,"Convex fracture creates sixteen bounded physical pieces");
            require(std::abs(total-volume(source))<1e-6,"Closed fracture caps preserve the source cube volume");
            for(const char* kind:{"rectangle","trapezoid","octagon"}){Character c;c.Shape=kind;auto mesh=shape_geometry(c).front().mesh;auto cut=fracture_convex(mesh,{1,1,1});double cutVolume=0;for(const auto& piece:cut)cutVolume+=volume(piece.part->mesh);require(cut.size()==16&&std::abs(cutVolume-volume(mesh))<1e-6,"Primitive fracture retains closed volume across supported shapes");}

        }
        {
            FramePacingMonitor monitor;
            for(int i=0;i<=400;i++){
                double t=i/400.;monitor.frame(t);
                monitor.pose("moving","Moving",{}, {double(i/8)},t);
                monitor.pose("static","Static",{}, {0},t);
                monitor.effect("static",{t},"rendered-effect-time",t);
            }
            auto snapshot=monitor.snapshot(1);
            require(std::abs(snapshot.fps-400)<.01,"Frame pacing samples completed frames independently of pose changes");
            require(std::abs(snapshot.p99-2.5)<.001,"Frame pacing percentiles preserve individual intervals");
            auto moving=std::find_if(snapshot.objects.begin(),snapshot.objects.end(),[](const auto& o){return o.id=="moving";});
            auto stationary=std::find_if(snapshot.objects.begin(),snapshot.objects.end(),[](const auto& o){return o.id=="static";});
            require(moving!=snapshot.objects.end() && std::abs(moving->updatesPerSecond-50)<.01,"Object monitor distinguishes 50 pose updates from 400 rendered frames");
            require(stationary!=snapshot.objects.end() && !stationary->moving,"Static scenery is not classified as slow animation");
            require(stationary!=snapshot.objects.end() && std::abs(stationary->effectUpdatesPerSecond-400)<.01 && stationary->effectMeasurement=="rendered-effect-time","Shader effect updates are recorded separately from unchanged object transforms");
            monitor.frame(1.1);require(monitor.snapshot(1.1).worst>99,"Frame pacing retains a single 100 ms hitch");
            auto hitched=monitor.snapshot(1.1);
            require(std::abs(hitched.low1-1000./22.)<.001 && std::abs(hitched.low01-10)<.001,"One-percent lows average the slowest frames rather than substituting a percentile");
            auto logPath=folder/"pacing-roundtrip.jsonl";
            {PacingLog log(logPath);log.push(hitched,{{1,1.1,100,3,4,.5}},1.1);}
            std::ifstream timingFile(logPath);std::string headerLine,sampleLine,endLine;std::getline(timingFile,headerLine);std::getline(timingFile,sampleLine);std::getline(timingFile,endLine);
            auto sample=json::parse(sampleLine),end=json::parse(endLine);
            require(sample["frames"].size()==1 && sample["overlay"]["graphMs"].size()==401 && end["type"]=="end" && end["droppedRecords"]==0,"Timing logger drains every queued frame and overlay sample before shutdown");

            monitor.frame(4);require(monitor.snapshot(4).objects.empty(),"Removed object readings expire");
        }

        {
            MeshPart a;a.color={.2,.4,.6};a.mesh.vertices={{{0,0,0}},{{1,0,0}},{{0,1,0}}};a.mesh.indices={0,1,2};
            auto b=a;b.mesh.vertices[0].position.z=1;auto glow=a;glow.emissive=true;
            auto merged=batch_presentation({a,b,glow});
            require(merged.size()==2 && merged[0].mesh.vertices.size()==6 && merged[0].mesh.indices==std::vector<int>({0,1,2,3,4,5}) && merged[0].mesh.vertices[3].position.z==1 && merged[1].emissive,"Presentation batching preserves triangle offsets, world positions and emissive separation");
            AdventurePlayer::Presentation p;p.showBody=false;p.showTelegraph=true;p.attackAim={0,0,-1};p.channelProgress=.5;
            auto cone=player_presentation(p);for(auto& part:cone)part.mesh.validate();
            require(cone.size()==2,"Moving cone uses two material batches instead of one allocation per edge");
        }
        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            AdventurePlayer player;player.start(level);
            player.cameraDistance=0;
            auto initial=player.presentation();
            require(!initial.showBody && initial.showWeapons && !player_presentation(initial).empty(),"First person shows dual pistols without a body");
            player.fire_button(true);player.fire_button(false);
            for(int i=0;i<49;++i)player.move(0,0,.05);
            require(player.presentation().shots==0 && player.presentation().shotFlash==0,"Weapon effects wait for the committed attack to resolve");
            player.move(0,0,.05);
            require(player.presentation().shots==1 && player.presentation().shotFlash>0,"Resolved shot emits one event and a muzzle flash even on a miss");
            for(int i=0;i<5;++i)player.move(0,0,.05);
            require(player.presentation().shots==1 && player.presentation().shotFlash==0,"Released attack does not repeat and muzzle flash expires");
            player.fire_button(true);player.move(0,0,.05);player.cancel_mobility_input();
            for(int i=0;i<60;++i)player.move(0,0,.05);
            require(player.presentation().shots==1,"Cancelled windup produces no phantom sound or muzzle flash");
            player.start(level);
            require(player.presentation().epoch!=initial.epoch && player.presentation().shots==0 && player.presentation().shotFlash==0,"Restart clears presentation events and changes the audio epoch");
            player.start(level,false);player.cameraDistance=0;
            require(!player.presentation().showWeapons && player_presentation(player.presentation()).empty(),"Authoring exploration does not introduce combat weapons");
            for(auto cue:{GameplayCue::Shot,GameplayCue::Shatter,GameplayCue::Impact,GameplayCue::Chirp,GameplayCue::Lightning}){
                auto pcm=gameplay_sound(cue);long energy=0;int peak=0;
                for(auto sample:pcm){peak=std::max(peak,std::abs(int(sample)));energy+=std::abs(int(sample));}
                require(!pcm.empty() && pcm.front()==0 && std::abs(int(pcm.back()))<2 && peak<7000 && energy>10000,"Synthesized placeholder cue is audible data with bounded gain and click-free endpoints");
            }
        }
        {
            GrappleMotion g;MovementResources resources;g.press(resources,true,{0,0,0},{0,0,-20},{0,0,1},false);
            for(int i=0;i<15;i++)g.step({0,0,0},{},1./60.);
            require(g.flying() && std::abs(g.hook_position().z+10)<.001,"Hook travels forty metres per second before attachment");
            g.release();require(g.state==GrappleMotion::State::Free && !g.flying(),"Releasing during hook flight cancels attachment");
            SceneData s;Character c;c.GrappleMode=2;s.Characters={c};require(SceneData::decode(s.encode()).Characters[0].GrappleMode==2,"Creator grapple mode survives scene round trip");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;level.Terrain.emplace();
            for(int z=0;z<TerrainData::N;z++)for(int x=0;x<TerrainData::N;x++)level.Terrain->Heights[z*TerrainData::N+x]=2+(x*TerrainData::Step-TerrainData::Half)*.1;
            Character enemy;enemy.Shape="cube";enemy.Id="slope-npc";enemy.X=-5;enemy.Y=1.5;enemy.CombatHealth=100;enemy.CombatPursuit=true;level.Characters={enemy};
            AdventurePlayer p;p.start(level);for(int i=0;i<300;i++)p.move(0,0,1./120.);
            auto c=p.scene().Characters[0];require(c.X>-3 && std::abs(c.Y-level.Terrain->height_at(c.X,c.Z))<.03,"Pursuing NPC follows walkable terrain elevation");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=10;
            Character a;a.Shape="cube";a.Id="patrolling-npc";a.CombatHealth=100;a.Speed=2;a.Route={{8,0}};level.Characters={a};auto saved=level.encode();
            AdventurePlayer p;p.start(level);for(int i=0;i<120;i++)p.move(0,0,1./120.);double before=p.scene().Characters[0].X;
            require(before>1 && before<2.1,"Combat NPC follows authored route through runtime steering");
            p.control_actor(a.Id,ControlEffect::Stun,{1,true});for(int i=0;i<120;i++)p.move(0,0,1./120.);
            require(std::abs(p.scene().Characters[0].X-before)<.001,"Stun pauses authored NPC route");
            for(int i=0;i<150;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].X>before && p.scene().Characters[0].X<before+1,"NPC resumes route without catch-up teleport");
            require(level.encode()==saved,"Runtime route control preserves authored route");
        }

        {
            TerrainData cave;cave.Volumes.push_back({0,-5,0,4,true});
            auto floor=terrain_support_height(cave,0,0,-8.8);
            require(floor&&std::abs(*floor+9)<.002,"Local support queries select the cave floor rather than the roof or old heightfield");
            require(!terrain_support_height(cave,0,0,-5),"Actors without nearby support do not teleport to another terrain layer");
            Bounds body{{-.2,-9,-.2},{.2,-7.5,.2}};
            auto support=terrain_body_support(cave,body,{},-8.8);
            require(support&&*support<-8.9&&terrain_body_clear(cave,body,{0,*support+9,0}),"Cave floor support considers the complete actor footprint");
            require(!terrain_body_clear(cave,body,{3.9,2,0})&&!terrain_body_clear(cave,body,{0,7,0}),"Volumetric actor clearance rejects cave walls and low ceilings");
            TerrainData mound;mound.Volumes.push_back({0,0,0,1,false});
            auto raised=terrain_support_height(mound,0,0,.9);
            require(raised&&std::abs(*raised-1)<.002,"Local actor support finds added terrain above the legacy ground");
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnY=-7.2;level.Game->SpawnZ=0;level.Terrain=cave;
            Character enemy;enemy.Shape="cube";enemy.Id="cave-patrol";enemy.X=-1;enemy.Y=-8.8;enemy.Width=.4;enemy.Depth=.4;enemy.Height=1;enemy.CombatHealth=100;enemy.Speed=1.5;enemy.Route={{2,0}};level.Characters={enemy};auto authored=level.encode();
            AdventurePlayer player;player.start(level);bool inside=true;
            for(int tick=0;tick<240;++tick){player.move(0,0,1./120.);inside=inside&&terrain_body_clear(cave,object_bounds(player.scene().Characters.front()));}
            auto moved=player.scene().Characters.front();
            require(moved.X>.5&&moved.Y<-8&&inside,"Patrolling NPC traverses the carved cave floor while preserving volumetric body clearance");
            require(level.encode()==authored,"Cave navigation changes only runtime actor poses");
            enemy.Route={{8,0}};level.Characters={enemy};player.start(level);
            for(int tick=0;tick<600;++tick)player.move(0,0,1./120.);
            auto blocked=player.scene().Characters.front();
            require(blocked.X<3.2&&blocked.Y<-5&&terrain_body_clear(cave,object_bounds(blocked)),"Cave navigation stops at unwalkable curved walls instead of climbing to the heightfield");
            TerrainData lowCave;lowCave.Volumes.push_back({0,-2,0,1.5,true});level.Terrain=lowCave;
            enemy.X=0;enemy.Z=0;enemy.Y=-3.45;enemy.Width=.2;enemy.Depth=.2;enemy.Height=1.5;enemy.Route.clear();level.Characters={enemy};level.Game->SpawnY=-1.8;level.Game->SpawnZ=-1;
            player.start(level);player.control_actor(enemy.Id,ControlEffect::Launch,{1,true});inside=true;double top=-10;
            for(int tick=0;tick<480;++tick){player.move(0,0,1./120.);auto b=object_bounds(player.scene().Characters.front());inside=inside&&terrain_body_clear(lowCave,b);top=std::max(top,b.high.y);}
            auto landed=player.scene().Characters.front();
            require(inside&&top<-.45&&landed.Y<-2&&!player.combat_actors().at(enemy.Id).launchAirborne,"Launched NPC sweeps the cave walls and ceiling, then lands inside the carved volume");
            atomic_write(folder/"npc-volume-contact.json",json{{"patrolX",moved.X},{"patrolY",moved.Y},{"blockedX",blocked.X},{"blockedY",blocked.Y},{"launchTop",top},{"landedY",landed.Y},{"bodyClear",inside}}.dump(2));
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.Id="launched-npc";enemy.Z=-3;enemy.CombatHealth=100;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.control_actor(enemy.Id,ControlEffect::Launch,{1,true});for(int i=0;i<60;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Y>1 && p.scene().Characters[0].Z<-5,"NPC launch physically lifts and throws actor backward");
            for(int i=0;i<100;i++)p.move(0,0,1./120.);
            require(std::abs(p.scene().Characters[0].Y)<.01 && p.combat_actors().at(enemy.Id).control.active==ControlEffect::Launch,"NPC launch lands before its recovery timer ends");
            for(int i=0;i<150;i++)p.move(0,0,1./120.);
            require(!p.combat_actors().at(enemy.Id).control.active,"NPC launch releases control after landing recovery");
            enemy.Z=-8;level.Characters={enemy};p.start(level);p.control_actor(enemy.Id,ControlEffect::Tether,{1,true});for(int i=0;i<70;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z>=-5.6,"NPC tether pulls actor back inside attachment boundary");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character a;a.Shape="cube";a.Id="taunter";a.Z=-4;a.CombatHealth=100;Character b=a;b.Id="hidden-target";b.X=1;level.Characters={a,b};
            AdventurePlayer p;p.start(level);p.select_target();if(p.selected_target()!=b.Id)p.select_target();
            p.apply_control(ControlEffect::Taunt,{combat_identity(a.Id),false},true);p.move(0,0,.01);
            require(p.selected_target().empty() && !p.combat_display().hasTarget,"Taunt clears hidden target marker and health display");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;Character enemy;enemy.Shape="cube";enemy.CombatHealth=100;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.control_actor(enemy.Id,ControlEffect::Taunt,{999999,false});p.move(0,0,.01);
            require(!p.combat_actors().at(enemy.Id).control.active,"NPC taunt clears when its source no longer exists");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=20;
            Character a;a.Shape="cube";a.Id="npc-finisher";a.CombatHealth=100;a.CombatTeam=1;a.CombatAutoAttack=true;
            Character b=a;b.Id="npc-victim";b.CombatHealth=10;b.CombatTeam=2;b.CombatAutoAttack=false;b.Z=-2;level.Characters={a,b};auto authored=level.encode();
            AdventurePlayer p;p.start(level);for(int i=0;i<190;i++)p.move(0,0,1./120.);
            require(p.rules().removed.contains(b.Id) && p.scene().Characters.size()==1,"NPC kill removes defeated runtime actor and collision target");
            require(level.encode()==authored,"NPC death leaves authored scene untouched");
            p.start(level);require(p.scene().Characters.size()==2 && p.combat_actors().at(b.Id).health==10,"Restart restores NPC defeated by another NPC");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.Id="displaced-npc";enemy.Z=-5;enemy.CombatHealth=100;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.control_actor(enemy.Id,ControlEffect::Knockback,{1,true});for(int i=0;i<60;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z<-7.8,"NPC knockback physically displaces away from source");
            p.start(level);p.control_actor(enemy.Id,ControlEffect::Pull,{1,true});for(int i=0;i<60;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z>=-1.1 && p.scene().Characters[0].Z<=-1,"NPC pull stops near source without snapping onto it");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.Id="feared-npc";enemy.Z=-3;enemy.CombatHealth=100;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.control_actor(enemy.Id,ControlEffect::Fear,{1,true});for(int i=0;i<120;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z<-4,"Fear drives NPC away from source without requiring pursuit setting");
            Character wall;wall.Shape="cube";wall.Z=-5;wall.Width=8;wall.Height=4;level.Characters.push_back(wall);p.start(level);p.control_actor(enemy.Id,ControlEffect::Fear,{1,true});
            for(int i=0;i<210;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z>-4.1,"NPC fear respects obstructing wall");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=20;
            Character friendly;friendly.Shape="cube";friendly.Id="friendly";friendly.CombatTeam=1;friendly.CombatHealth=100;friendly.CombatAutoAttack=true;
            Character enemy=friendly;enemy.Id="hostile";enemy.CombatTeam=2;enemy.CombatSightRange=20;enemy.Z=-2;level.Characters={friendly,enemy};
            AdventurePlayer p;p.start(level);for(int i=0;i<190;i++)p.move(0,0,1./120.);
            require(p.combat_actors().at(friendly.Id).health==90 && p.combat_actors().at(enemy.Id).health==90,"Friendly and enemy NPCs exchange Swipe damage");
            require(p.health()==100,"NPC exchange does not damage distant player");
            p.start(level);p.control_actor(enemy.Id,ControlEffect::Taunt,{1,true});
            auto target=p.actor_target(enemy,p.combat_actors().at(enemy.Id));
            require(!target,"Taunted NPC does not substitute a nearby friendly for out-of-range taunter");
            p.position.z=4;target=p.actor_target(enemy,p.combat_actors().at(enemy.Id));
            require(target && target->id.empty(),"Taunted NPC selects player source instead of nearer friendly");
        }

        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            level.Terrain.emplace();for(auto& h:level.Terrain->Heights)h=3;
            Character enemy;enemy.Shape="cube";enemy.Z=-2;enemy.CombatHealth=100;enemy.CombatAutoAttack=true;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.fire_button(true);p.fire_button(false);
            for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.health()==100 && p.combat_actors().at(enemy.Id).health==100,"Terrain blocks both player attack and enemy Swipe");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;AdventurePlayer p;p.start(level);
            for(unsigned key: {'2','3','4','5','R'}){p.ability_button(key,true);p.move(0,0,.01);require(!p.basic_attack().winding,"Unassigned ability slot remains disabled");}
            p.ability_button('1',true);p.ability_button('1',false);p.move(0,0,.01);
            require(p.basic_attack().winding,"Basic attack runs through shared slot dispatch");
        }

        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character enemy;enemy.Shape="cube";enemy.Z=-2;enemy.CombatHealth=100;enemy.CombatAutoAttack=true;level.Characters={enemy};
            for(int effect=0;effect<14;effect++){
                level.Characters[0].CombatControlEffect=effect;AdventurePlayer p;p.start(level);
                for(int i=0;i<180;i++)p.move(0,0,1./120.);
                require(p.crowd_control().active==static_cast<ControlEffect>(effect),"Authored enemy swipe applies selected crowd control after landed channel");
                require(SceneData::decode(level.encode()).Characters[0].CombatControlEffect==effect,"Authored control effect survives scene round trip");
            }
            level.Characters[0].CombatTeam=1;AdventurePlayer p;p.start(level);for(int i=0;i<180;i++)p.move(0,0,1./120.);
            require(!p.crowd_control().active && p.health()==100,"Friendly swipe never damages or controls player");
        }
        {
            HitChance first,second;int hits=0;bool repeatable=true;
            for(int i=0;i<10000;i++){bool a=first.roll(.5);hits+=a;repeatable&=a==second.roll(.5);}
            require(repeatable && hits>4800 && hits<5200,"Blind seeded hit rolls halve hit probability repeatably");
            SceneData level;level.Game.emplace();level.Game->Collision=false;level.Game->SpawnZ=0;
            Character a;a.Shape="cube";a.Z=-3;a.CombatHealth=100;Character b=a;b.Id="other-enemy";b.X=1;level.Characters={a,b};
            AdventurePlayer p;p.start(level);p.apply_control(ControlEffect::Taunt,{combat_identity(a.Id),false},true);p.move(0,0,.01);
            require(p.visible_scene().Characters.size()==1 && p.scene().Characters.size()==2,"Taunt hides other enemies without removing collision scene");
            p.select_target();require(p.selected_target()==a.Id,"Taunt selects only its source");
            p.healing_cast(b.Id,true);p.move(0,0,.01);
            require(p.presentation().actorTelegraphs.empty(),"Taunt hides other enemy healing cones");
            require(!p.unobstructed({1,1,0},{1,1,-6},"",a.Id),"Hidden enemy remains a physical healing obstruction");
            p.cleanse_control();require(p.visible_scene().Characters.size()==2,"Removing taunt restores enemy awareness");
            require(p.presentation().actorTelegraphs.size()==1,"Removing taunt restores active healing cone");

            p.start(level);p.apply_control(ControlEffect::Blind,{9,true},true);Character distantActor=a;distantActor.Z=-40;
            require(p.sight_range()==30 && !p.perceived(distantActor) && p.perceived(a),"Blind halves sight distance while preserving near targets");
            p.cleanse_control();require(p.sight_range()==60 && p.perceived(distantActor),"Cleanse restores sight range");
            level.Game->SightRange=20;a.CombatSightRange=18;a.CombatPursuit=true;a.Z=-15;level.Characters={a};
            auto restored=SceneData::decode(level.encode());require(restored.Game->SightRange==20 && restored.Characters[0].CombatSightRange==18,"Player and NPC sight ranges survive scene save and load");
            p.start(restored);p.control_actor(a.Id,ControlEffect::Blind,{1,true});for(int i=0;i<120;i++)p.move(0,0,1./120.);
            require(std::abs(p.scene().Characters[0].Z+15)<.001,"Blinded NPC cannot pursue beyond half its configured sight range");
            for(int i=0;i<360;i++)p.move(0,0,1./120.);
            require(p.scene().Characters[0].Z>-14,"NPC resumes pursuit within normal sight range after Blind ends");
            p.apply_control(ControlEffect::Blind,{9,true},true);require(p.sight_range()==10,"Player Blind halves the configured baseline instead of a fixed constant");
        }
        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;AdventurePlayer p;p.start(level);p.take_damage(40);
            p.healing_cast("",true);p.move(0,0,.05);p.cancel_mobility_input();for(int i=0;i<240;i++)p.move(0,0,1./120.);
            require(p.health()==60 && p.presentation().actorTelegraphs.empty(),"Focus cancellation clears player healing without delayed recovery");
            p.healing_cast("",true);p.apply_control(ControlEffect::Silence,{7,true},true);for(int i=0;i<180;i++)p.move(0,0,1./120.);
            require(p.health()==60,"Silence blocks non-basic healing ability");
            Character wall;wall.Shape="cube";wall.Z=-1;wall.Width=5;wall.Height=4;level.Characters={wall};p.start(level);
            require(!p.unobstructed({0,1,0},{0,1,-2},"",""),"Healing line of sight respects intervening wall");
        }
        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;Character enemy;enemy.Shape="cube";enemy.CombatHealth=100;enemy.Z=-2;level.Characters={enemy};
            AdventurePlayer p;p.start(level);p.take_damage(40);p.healing_cast("",true);p.move(0,0,.05);
            require(p.presentation().actorTelegraphs.back().kind==TelegraphKind::Healing,"Player healing publishes green cone");
            for(int i=0;i<180;i++)p.move(0,0,1./120.);
            require(p.health()==80 && p.combat_actors().at(enemy.Id).health==100,"Player healing restores health without healing hostile actor");
            p.healing_cast("",false);p.healing_cast(enemy.Id,true);p.move(0,0,.05);
            bool orange=false;for(auto t:p.presentation().actorTelegraphs)orange|=t.kind==TelegraphKind::EnemyHealing;
            require(orange,"Enemy healing publishes orange cone");
            p.start(level);require(p.presentation().actorTelegraphs.empty(),"Reset clears healing channels");
        }
        {
            SceneData level;level.Game.emplace();level.Game->Collision=false;AdventurePlayer p;p.start(level);
            p.fire_button(true);p.fire_button(false);for(int i=0;i<60;i++)p.move(0,0,1./120.);
            auto before=p.presentation();p.position.x+=2;p.yaw=.7;p.pitch=.1;p.cameraYaw=-1;
            auto after=p.presentation();
            require((after.attackOrigin-before.attackOrigin-Vec3{2,0,0}).length()<1e-8 && after.channelProgress==before.channelProgress,"Player cone follows caster translation without resetting channel");
            require((after.attackAim-p.attack_direction()).length()<1e-8 && after.attackAim.x>0,"Player cone follows character facing independently of camera orbit");
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            Character enemy;enemy.Shape="cube";enemy.CombatHealth=100;enemy.CombatAutoAttack=true;level.Characters={enemy};
            AdventurePlayer p;
            for(double degrees:{12.,15.,18.,90.,145.,230.,315.}){
                auto angle=degrees*3.141592653589793/180;p.start(level);p.position={2*std::sin(angle),1.7,2*std::cos(angle)};
                for(int i=0;i<60;i++)p.move(0,0,1./120.);auto a=p.presentation().actorTelegraphs;
                require(a.size()==1 && (a[0].aim-Vec3{std::sin(angle),0,std::cos(angle)}).length()<1e-8,"Enemy telegraph aims at target at arbitrary full-circle heading");
            }
            p.start(level);p.position={0,1.7,2};for(int i=0;i<90;i++)p.move(0,0,1./120.);
            p.position={2,1.7,0};p.move(0,0,1./120.);auto a=p.presentation().actorTelegraphs;
            require(a.size()==1 && a[0].aim.x>0 && a[0].aim.x<.06 && a[0].progress>.5,"Active enemy cone turns gradually without restarting fill");
            for(int i=0;i<29;i++)p.move(0,0,1./120.);
            require((p.presentation().actorTelegraphs[0].aim-Vec3{1,0,0}).length()<1e-8,"Enemy cone reaches new aim through bounded turning");
            for(int i=0;i<60;i++)p.move(0,0,1./120.);

            require(p.health()==90,"Enemy hit resolves using updated advertised aim");
        }
        {
            GroundSteering steering;Vec3 at{},target{7,0,-11};double maximum=0;
            for(int i=0;i<600;i++){auto delta=steering.step(at,target,1./120.,2.5,2);maximum=std::max(maximum,delta.length());at=at+delta;}
            require(maximum<=2.5/120.+1e-9 && (at.unit()-target.unit()).length()<1e-8,"Pursuit keeps continuous heading with speed-bounded displacement");
            for(int i=0;i<1200;i++)at=at+steering.step(at,target,1./120.,2.5,2);
            require(std::abs((target-at).length()-2)<.01,"Pursuit brakes at stable attack spacing");
            auto previous=at;at=at+steering.step(at,{-100,0,100},10,2.5,2);
            require((at-previous).length()<=.125+1e-9,"Target reversal and frame stall cannot teleport pursuer");
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            Character enemy;enemy.Shape="cube";enemy.X=3;enemy.Z=-8;enemy.CombatHealth=100;enemy.CombatPursuit=true;level.Characters={enemy};auto saved=level.encode();
            AdventurePlayer p;p.start(level);Vec3 last{enemy.X,enemy.Y,enemy.Z};bool bounded=true;
            for(int i=0;i<600;i++){p.move(0,0,1./120.);auto c=p.scene().Characters[0];Vec3 next{c.X,c.Y,c.Z};bounded&=(next-last).length()<=2.5/120.+1e-8;last=next;}
            require(bounded && std::hypot(last.x,last.z)<2.1,"Runtime enemy follows smoothly and stops near player");
            require(level.encode()==saved,"Pursuit never changes authored enemy positions");
            p.start(level);require(p.scene().Characters[0].X==3 && p.scene().Characters[0].Z==-8,"Reset restores original enemy position");
            Character wall;wall.Shape="cube";wall.Width=20;wall.Height=5;wall.Depth=.5;wall.Z=-4;level.Characters.push_back(wall);p.start(level);
            bool crossedWall=false;last={enemy.X,enemy.Y,enemy.Z};bounded=true;
            for(int i=0;i<1200;i++){p.move(0,0,1./120.);const auto& moved=p.scene().Characters[0];auto b=object_bounds(moved),w=object_bounds(wall);crossedWall|=b.low.x<w.high.x && b.high.x>w.low.x && b.low.z<w.high.z && b.high.z>w.low.z;Vec3 next{moved.X,moved.Y,moved.Z};bounded&=(next-last).length()<=2.5/120.+1e-8;last=next;}
            require(!crossedWall && bounded,"Pursuit routes around wall without intersecting it or teleporting");
            wall.Width=4;level.Characters[1]=wall;level.Characters[0].X=0;p.start(level);
            for(int i=0;i<1200;i++)p.move(0,0,1./120.);
            require(std::hypot(p.scene().Characters[0].X,p.scene().Characters[0].Z)<2.2,"Pursuit reaches attack distance around intervening obstacle");

        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            for(double degrees:{12.,15.,18.,47.,123.,271.}){
                double angle=degrees*3.141592653589793/180;
                AdventurePlayer p;p.start(level);p.yaw=angle;auto start=p.position;
                for(int i=0;i<120;i++)p.move(0,1,1./120.);
                auto delta=p.position-start;auto expected=Vec3{std::sin(angle),0,-std::cos(angle)};
                require((delta.unit()-expected).length()<1e-8,"Forward movement preserves arbitrary world heading without compass snapping");
                p.start(level);start=p.position;for(int i=0;i<120;i++)p.move(std::sin(angle),std::cos(angle),1./120.);
                delta=p.position-start;
                require((delta.unit()-expected).length()<1e-8 && std::abs(delta.length()-level.Game->MoveSpeed)<1e-8,"Continuous directional input preserves arbitrary angle and movement speed");
            }
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            Character enemy;enemy.Shape="cube";enemy.Z=-2;enemy.CombatHealth=100;enemy.CombatAutoAttack=true;level.Characters={enemy};
            AdventurePlayer p;p.start(level);
            for(int i=0;i<90;i++)p.move(0,0,1./120.);
            auto visual=p.presentation();
            require(p.health()==100 && visual.actorTelegraphs.size()==1 && visual.actorTelegraphs[0].kind==TelegraphKind::Enemy,"Enemy swipe advertises red channel before damage");
            require(std::abs(visual.actorTelegraphs[0].progress-.5)<1e-8,"Swipe fills halfway after three quarters of a second");
            for(int i=0;i<90;i++)p.move(0,0,1./120.);
            require(p.health()==90,"Enemy swipe hits after one and a half seconds");
            for(int i=0;i<180;i++)p.move(0,0,1./120.);
            require(p.health()==80,"Enemy swipe repeats complete cycles");
            p.start(level);for(int i=0;i<90;i++)p.move(0,0,1./120.);p.position.x=4;
            for(int i=0;i<100;i++)p.move(0,0,1./120.);
            require(p.health()==100,"Moving outside advertised swipe avoids damage");
            p.start(level);p.control_actor(enemy.Id,ControlEffect::Stun,{1,true});for(int i=0;i<180;i++)p.move(0,0,1./120.);
            require(p.health()==100 && p.presentation().actorTelegraphs.empty(),"Stun blocks enemy swipe and telegraph");
            level.Characters[0].CombatTeam=1;p.start(level);for(int i=0;i<360;i++)p.move(0,0,1./120.);
            require(p.health()==100,"Friendly actor never swipes player");
            require(SceneData::decode(level.encode()).Characters[0].CombatAutoAttack,"Enemy attack setting persists");
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            Character enemy;enemy.Shape="cube";enemy.Z=-4;enemy.Y=1;enemy.CombatHealth=100;enemy.CombatTeam=2;enemy.CombatBoss=true;level.Characters={enemy};
            auto saved=level.encode();auto restored=SceneData::decode(saved);
            require(restored.SchemaVersion==16 && restored.Characters[0].CombatHealth==100 && restored.Characters[0].CombatBoss,"Combat actor and boss settings round-trip through scene schema");
            AdventurePlayer p;p.start(restored);p.select_target();p.fire_button(true);p.fire_button(false);for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.combat_actors().at(enemy.Id).health==90 && p.scene().Characters.size()==1,"Basic attack damages combat actor without prematurely removing it");
            for(int i=0;i<4;i++)p.control_actor(enemy.Id,ControlEffect::Stun,{1,true});
            require(p.combat_actors().at(enemy.Id).bar.phase==BossBreakBar::Phase::Staggered,"Repeated player CC breaks authored boss bar");
            p.fire_button(true);p.fire_button(false);for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.combat_actors().at(enemy.Id).health==77.5,"Boss stagger increases real attack damage by twenty-five percent");
            require(p.heal_actor(enemy.Id,20,1)==0 && p.heal_actor(enemy.Id,20,2)==20,"Healing respects actor affiliation");
            require(restored.encode()==saved,"Runtime actor health and break bar do not change authored scene");
            p.start(restored);require(p.combat_actors().at(enemy.Id).health==100 && p.combat_actors().at(enemy.Id).bar.strength==1000,"Reset restores boss health and break strength");
            restored.Characters[0].CombatTeam=1;p.start(restored);p.select_target();
            require(p.selected_target().empty() && !p.control_actor(enemy.Id,ControlEffect::Stun,{1,true}),"Friendly combat actors reject hostile selection and CC");
        }
        {
            for(auto kind:{TelegraphKind::Friendly,TelegraphKind::Enemy,TelegraphKind::Healing,TelegraphKind::EnemyHealing}){
                AdventurePlayer::Presentation p;p.showBody=false;p.showTelegraph=true;p.attackAim={0,0,-1};p.channelProgress=.5;p.telegraphKind=kind;
                auto parts=player_presentation(p);auto expected=telegraph_color(kind);
                require(!parts.empty() && (parts.front().color-expected).length()<1e-9 && (parts.back().color-expected*.4).length()<1e-9,"Telegraph category colors both cone outline and channel fill");
                for(const auto& part:parts)part.mesh.validate();
                double radius=0;for(const auto& v:parts.back().mesh.vertices)radius=std::max(radius,std::hypot(v.position.x,v.position.z));
                require(std::abs(radius-10)<1e-8,"Half-channel fill reaches half cone range");
            }
        }
        {
            TerrainData slope;
            for(int z=0;z<TerrainData::N;z++)for(int x=0;x<TerrainData::N;x++)slope.Heights[z*TerrainData::N+x]=-(z*TerrainData::Step-TerrainData::Half)*.2;
            AdventurePlayer::Presentation p;p.showBody=false;p.showTelegraph=true;p.attackAim={0,0,-1};p.channelProgress=.5;p.telegraphTerrain=&slope;
            auto parts=player_presentation(p);bool follows=true;double reach=0;
            for(const auto& v:parts.back().mesh.vertices){follows=follows && std::abs(v.position.y-slope.height_at(v.position.x,v.position.z)-telegraph_elevation(p.telegraphKind))<1e-8;reach=std::max(reach,std::hypot(v.position.x,v.position.z));}
            require(follows && std::abs(reach-10)<1e-8,"Channel fill conforms to sloped terrain while preserving horizontal progress");
            Vec3 contact{};
            require(surface_telegraph_contact(&slope,{0,0,0},{0,0,-1},{-.2,3,-15.2},{.2,4.8,-14.8},contact) && contact.y>=3,"Ground cone hits an uphill target above the caster plane");
            require(!surface_telegraph_contact(&slope,{0,0,0},{0,0,-1},{-.2,0,-15.2},{.2,1,-14.8},contact),"Ground cone rejects a target below the sloping surface");
            auto airborne=telegraph_surface(&slope,{0,5,0},{0,0,-1},{0,5,-10});
            require(airborne.y==5,"Airborne cone retains its aimed plane instead of snapping to terrain");
        }
        {
            BasicAttack a;a.press(true);int shots=0;
            for(int i=0;i<600;i++)shots+=a.advance(1./120.,false);
            require(shots==2,"Held basic attack repeats complete two-and-a-half-second cycles");
            a={};a.press(true);a.advance(.05,false);a.press(false);
            require(!a.advance(.05,false) && a.winding,"Releasing attack preserves the active firing cycle");
            shots=0;for(int i=0;i<600;i++)shots+=a.advance(1./120.,false);
            require(shots==1 && !a.winding,"Tap completes exactly one firing cycle without repeating");
            a.press(true);a.advance(.05,false);a.advance(.05,true);
            require(!a.winding,"Action block cancels attack windup");
            require(inside_basic_telegraph({0,2,0},{0,0,-1},{.9,2,-19}) && !inside_basic_telegraph({0,2,0},{0,0,-1},{2,2,-5}),"Cone telegraph widens from its origin and respects range");
            Vec3 contact{};
            require(telegraph_contact({0,0,0},{0,0,-1},{1.1,.2,-5.2},{2,1.8,-4.8},contact) && inside_basic_telegraph({0,0,0},{0,0,-1},contact),"Cone catches partial target side overlap and returns a valid contact");
            require(!telegraph_contact({0,0,0},{0,0,-1},{2,.2,-5.2},{3,1.8,-4.8},contact),"Cone rejects a wholly outside target");
            require(telegraph_contact({0,0,0},{0,0,-1},{-.5,.2,-21},{.5,1.8,-19.5},contact) && !telegraph_contact({0,0,0},{0,0,-1},{-.5,.2,-22},{.5,1.8,-20.1},contact),"Cone range accepts partial overlap but rejects targets beyond its edge");
            require(!telegraph_contact({0,0,0},{0,0,-1},{-.5,2.1,-5},{.5,3,-4},contact) && telegraph_contact({0,0,0},{0,0,-1},{-30,-1,-30},{30,4,30},contact),"Cone respects height and detects an enclosing target");
            auto level=SceneData{};level.Game.emplace();level.Game->SpawnZ=0;level.Game->Collision=false;
            Character left;left.Shape="cube";left.GameplayRole="breakable";left.X=-.6;left.Y=1;left.Z=-4;left.Width=.4;
            Character right=left;right.Id="second-attack-target";right.X=.6;level.Characters={left,right};auto original=level.encode();
            AdventurePlayer p;p.start(level);p.select_target();for(int i=0;i<60;i++)p.move(0,0,1./120.);
            require(!p.selected_target().empty() && p.scene().Characters.size()==2,"Tab selects without automatically firing");
            p.fire_button(true);for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.scene().Characters.empty() && level.encode()==original,"Manual area attack breaks both eligible targets and preserves authoring");
            require(p.presentation().fragments.size()==32,"Two broken targets emit sixteen cosmetic fragments each");
            const auto presentationFragments=p.presentation().fragments;
            require(std::all_of(presentationFragments.begin(),presentationFragments.end(),[](const auto& f){return bool(f.part);}),"Broken cubes retain cut source geometry rather than placeholder cubes");
#ifdef STARLIGHT_CHARACTER_CONTROLLER
            auto spinningLevel=level;spinningLevel.Game->Collision=true;spinningLevel.Characters.resize(1);auto& rotating=spinningLevel.Characters[0];rotating.Dynamic=true;rotating.VelocityX=3;rotating.AngularVelocityY=180;AdventureController movingSource(spinningLevel,{0,1.7,0});auto sample=movingSource.body_motion(rotating.Id,{rotating.X+1,rotating.Y+.5,rotating.Z});require(std::abs(sample.first.x-3)<.001&&std::abs(sample.first.z+3.141592653589793)<.001&&std::abs(sample.second.y-3.141592653589793)<.001,"Fragment velocity samples include source translation and rotation at the fracture point");
            SceneData grappleLevel;grappleLevel.Game.emplace();grappleLevel.Game->SpawnZ=0;grappleLevel.Terrain.emplace();Character grappleWall;grappleWall.Shape="cube";grappleWall.Width=grappleWall.Height=8;grappleWall.Z=-5;grappleLevel.Characters={grappleWall};AdventurePlayer tinyFrames;tinyFrames.start(grappleLevel);tinyFrames.grapple_button(true);for(int i=0;i<600;++i){tinyFrames.move(0,0,1./480.);tinyFrames.move(0,0,.0000001);}require(tinyFrames.grapple_motion().state==GrappleMotion::State::Wall,"Grapple remains attached when timer and rendered-frame updates arrive almost together");
            Character barrier;barrier.Shape="cube";barrier.Width=3;barrier.Height=1.2;barrier.Z=-2;grappleLevel.Characters.push_back(barrier);AdventurePlayer obstructed;obstructed.start(grappleLevel);obstructed.grapple_button(true);for(int i=0;i<240;++i)obstructed.move(0,0,1./120.);require(obstructed.grapple_motion().state==GrappleMotion::State::Free&&obstructed.position.z> -2,"A genuinely obstructed grapple still releases without crossing the blocking body");
            auto physicalLevel=level;physicalLevel.Game->Collision=true;AdventurePlayer physical;physical.start(physicalLevel);physical.fire_button(true);physical.fire_button(false);
            for(int i=0;i<310;++i)physical.move(0,0,1./120.);
            auto firstPieces=physical.presentation().fragments;
            require(firstPieces.size()==32,"Gameplay fracture registers pieces in the character collision world");
            auto environmentBodies=physical.environment_objects();require(environmentBodies.size()==firstPieces.size(),"Physical fragments publish collision bounds for fog, particles, liquids and strikes");for(const auto& body:environmentBodies){auto b=object_bounds(body);require(std::isfinite(b.low.x)&&b.high.y>b.low.y,"Published fragment bounds remain finite and nondegenerate");}
            for(int i=0;i<120;++i)physical.move(0,0,1./120.);
            auto movedPieces=physical.presentation().fragments;bool tumbled=false,aboveGround=true;
            for(size_t i=0;i<movedPieces.size();++i){const auto& f=movedPieces[i];tumbled=tumbled||std::abs(f.rotation[0])+std::abs(f.rotation[1])+std::abs(f.rotation[2])>.01;for(const auto& v:f.part->mesh.vertices){Vec3 q{f.rotation[0],f.rotation[1],f.rotation[2]};auto t=q.cross(v.position)*2;auto at=f.position+v.position+t*f.rotation[3]+q.cross(t);aboveGround=aboveGround&&at.y>-.08;}}
            require(tumbled&&aboveGround,"Rigid fracture pieces tumble and remain above terrain after impact");
            for(int i=0;i<1000;++i)physical.move(0,0,1./120.);
            require(physical.presentation().fragments.empty(),"Expired rigid fragments are removed from gameplay");
            physical.start(physicalLevel);require(physical.presentation().fragments.empty(),"Restart clears fracture presentation and replaces its collision world");
#endif
            auto partialLevel=level;partialLevel.Characters={left};partialLevel.Characters[0].X=1.45;partialLevel.Characters[0].Z=-5;partialLevel.Characters[0].Width=.8;
            AdventurePlayer partialPlayer;partialPlayer.start(partialLevel);partialPlayer.fire_button(true);partialPlayer.fire_button(false);
            for(int i=0;i<310;i++)partialPlayer.move(0,0,1./120.);
            require(partialPlayer.scene().Characters.empty(),"Actual attack breaks a target whose edge enters the cone while its centre remains outside");
            p.fire_button(false);for(int i=0;i<300;i++)p.move(0,0,1./120.);
            require(!p.presentation().fragments.empty(),"Physical debris remains available to collide and settle beyond the old cosmetic timeout");
            for(int i=0;i<720;i++)p.move(0,0,1./120.);
            require(p.presentation().fragments.empty(),"Physical debris is retired after its bounded eight-second lifetime");
            Character wall;wall.Shape="cube";wall.Breakable=false;wall.Width=5;wall.Height=4;wall.Depth=.2;wall.Z=-2;level.Characters.push_back(wall);
            p.start(level);p.fire_button(true);for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.scene().Characters.size()==3,"Solid wall blocks aimed attacks to targets behind it");
            level.Characters.pop_back();p.start(level);p.select_target();p.yaw=p.cameraYaw=1.5;p.fire_button(true);
            for(int i=0;i<310;i++)p.move(0,0,1./120.);
            require(p.scene().Characters.size()==2,"Selecting a target never redirects an off-target attack");
            p.start(level);p.fire_button(true);p.move(0,0,.05);p.dodge(1,0);p.move(0,0,.05);
            require(!p.basic_attack().winding && p.scene().Characters.size()==2,"Dodge cancels an unfinished shot before targets are hit");
        }
        {
            CombatControl c;c.player=true;ControlSource first{1,true},other{2,true};
            require(c.apply(ControlEffect::Stun,first,false)==CombatControl::Result::Rejected && !c.active,"Friendly CC is rejected without immunity");
            require(c.apply(ControlEffect::Stun,first,true,true)==CombatControl::Result::Rejected && !c.active,"Self CC is rejected without immunity");
            c.apply(ControlEffect::Stun,first,true);
            require(c.blocks_basic() && c.blocks_mobility(),"Stun locks attacks and voluntary mobility");
            require(c.apply(ControlEffect::Fear,other,true)==CombatControl::Result::Rejected && c.remaining==2,"Other attacker cannot replace or extend active CC");
            require(c.apply(ControlEffect::Slow,first,true)==CombatControl::Result::CancelledBySource && !c.active && c.immunity==5,"Same attacker early CC cancels control and starts immunity");
            c.advance(2);c.apply(ControlEffect::Blind,other,true);
            require(c.immunity==3 && !c.active,"Blocked CC does not extend immunity");
            c.advance(3);c.apply(ControlEffect::Sleep,first,true);c.hit(1);
            require(c.waking && c.remaining==1.5 && c.immunity==0,"Damage starts sleep waking before immunity");
            c.advance(.5);c.hit(1);require(c.remaining==1,"Repeated damage does not restart waking");
            c.advance(1);require(!c.active && c.immunity==5,"Sleep immunity begins after waking finishes");
            c.advance(5);c.apply(ControlEffect::Fear,first,true);c.hit(1);
            require(!c.active && c.immunity==5,"Damaging hit breaks fear and grants player immunity");
            c.advance(5);c.apply(ControlEffect::Root,first,true);
            require(c.blocks_mobility() && !c.blocks_basic(),"Root permits basic fire while blocking mobility");
            c.cleanse();require(c.immunity==5 && !c.active,"Early cleanse immediately grants immunity");
            CombatControl npc;npc.apply(ControlEffect::Silence,first,true);
            require(npc.blocks_skills() && !npc.blocks_basic() && !npc.blocks_mobility(),"Silence preserves basic fire and movement");
            npc.advance(3);require(npc.immunity==0 && !npc.active,"Ordinary NPC receives no post-control immunity");
            npc.apply(ControlEffect::Disorient,first,true);
            require(npc.blocks_basic() && !npc.blocks_mobility() && npc.movement_factor()==.75,"Disorient permits slower movement while blocking weapons");
            npc.cleanse();npc.apply(ControlEffect::Slow,first,true);
            require(npc.movement_factor()==.67 && npc.firing_factor()==.67,"Slow reduces both movement and firing rate by thirty-three percent");
            BossBreakBar boss;for(int i=0;i<4;i++)boss.apply(ControlEffect::Stun);
            require(boss.phase==BossBreakBar::Phase::Staggered && boss.damage_factor()==1.25 && boss.remaining==8,"Four stuns break boss bar for eight-second damage window");
            require(!boss.apply(ControlEffect::Stun) && boss.remaining==8,"Further CC cannot extend boss stagger");
            boss.advance(8);boss.advance(45);
            require(boss.phase==BossBreakBar::Phase::Recovering && boss.strength==500 && !boss.apply(ControlEffect::Stun),"Boss recovery is protected and halfway full after forty-five seconds");
            boss.advance(45);require(boss.phase==BossBreakBar::Phase::Ready && boss.strength==1000 && boss.apply(ControlEffect::Blind),"Boss accepts CC again only after ninety-second refill");
            const double contributions[]={250,150,150,250,200,150,75,100,200,100,200,75,150,100};
            bool allContributions=true;for(int i=0;i<14;i++){BossBreakBar bar;bar.apply(static_cast<ControlEffect>(i));allContributions=allContributions && bar.strength==1000-contributions[i];}
            require(allContributions,"All fourteen boss CC contributions match the authored baseline");
            BossBreakBar composite;
            require(composite.apply_composite({ControlEffect::Slow,ControlEffect::Stun},17) && composite.strength==750 && !composite.apply(ControlEffect::Stun,17) && composite.strength==750,"Composite boss event uses strongest CC once and rejects duplicate delivery");
            for(double frame:{1./30,1./60,1./144}){
                BossBreakBar bar;for(int i=0;i<4;i++)bar.apply(ControlEffect::Stun);
                auto elapsed=[&](double duration){while(duration>1e-10){double dt=std::min(frame,duration);bar.advance(dt);duration-=dt;}};
                elapsed(7.9);bool valid=bar.phase==BossBreakBar::Phase::Staggered && !bar.apply(ControlEffect::Launch);
                elapsed(.2);valid=valid && bar.phase==BossBreakBar::Phase::Recovering && bar.damage_factor()==1 && !bar.apply(ControlEffect::Stun);
                elapsed(89.8);valid=valid && bar.phase==BossBreakBar::Phase::Recovering && bar.strength<1000;
                elapsed(.2);valid=valid && bar.phase==BossBreakBar::Phase::Ready && bar.strength==1000 && bar.apply(ControlEffect::Stun);
                require(valid,"Boss stagger and protected refill preserve phase boundaries at varied frame rates");
            }
        }
        {
            AdventurePlayer::Presentation p;p.showBody=false;
            require(player_presentation(p).empty(),"First-person placeholder produces no body geometry");
            p.showTether=true;p.tetherAnchor={0,5,-4};
            auto tether=player_presentation(p);for(const auto& part:tether)part.mesh.validate();
            require(tether.size()==2,"First-person grapple retains a visible tether and hook");
            p.showBody=true;
            auto body=player_presentation(p);for(const auto& part:body)part.mesh.validate();
            require(body.size()>tether.size(),"Third-person placeholder adds body geometry without gameplay objects");
        }
        {
            MovementResources resource;
            GrappleMotion grapple;
            require(!grapple.press(resource,false,{0,2,0},{0,2,-5},{0,0,1},false) && resource.grappleCharges==1,"Missed grapple spends one charge");
            grapple.press(resource,false,{0,2,0},{0,2,-5},{0,0,1},false);
            require(resource.grappleCharges==1,"Held grapple cannot repeatedly spend charges");
            grapple.release();
            require(grapple.press(resource,true,{0,2,0},{0,2,-5},{0,0,1},false),"Fresh grapple starts wall pull");
            grapple.release();require(grapple.state==GrappleMotion::State::Free,"Releasing before wall arrival detaches");
            resource={};grapple.reset();
            grapple.press(resource,true,{0,2,-4.5},{0,2,-5},{0,0,1},false);
            for(int i=0;i<4;i++)grapple.step({0,2,-4.5},{},.01);grapple.release();
            require(grapple.state==GrappleMotion::State::Wall,"Wall attachment survives G release");
            auto jump=grapple.wall_jump();
            require(grapple.state==GrappleMotion::State::Free && jump.y==5 && jump.z==4,"Wall jump detaches upward and outward");
            grapple.state=GrappleMotion::State::Wall;grapple.anchorVelocity={3,0,0};auto movingJump=grapple.wall_jump();require(movingJump.x==3&&movingJump.y==5,"Wall jump retains moving-anchor momentum");
            grapple.release();resource={};grapple.press(resource,true,{0,2,0},{0,2,-5},{0,0,1},false);require(grapple.anchorVelocity.length()==0,"A new grapple cannot inherit stale velocity from its previous anchor");
            GrappleMotion slowPull;MovementResources slowResources;Vec3 slowPosition{0,2,0};slowPull.press(slowResources,true,slowPosition,{0,2,-5},{0,0,1},false);bool overshot=false;for(int i=0;i<40;++i){slowPosition=slowPosition+slowPull.step(slowPosition,{},.05)*.05;overshot|=slowPosition.z< -4.500001;}require(!overshot&&slowPull.state==GrappleMotion::State::Wall,"Slow-frame grapple movement uses the full elapsed interval without overshooting its attachment point");
            grapple.reset();resource={};
            grapple.press(resource,true,{0,0,0},{0,5,0},{0,-1,0},true);
            for(int i=0;i<7;i++)grapple.step({0,0,0},{},.02);auto v=grapple.step({0,0,0},{1,0,0},.02);grapple.release();
            require(grapple.state==GrappleMotion::State::Free && (grapple.velocity-v).length()<1e-9 && v.x>0,"Swing release preserves steering momentum");
        }
        {
            MovementResources m;
            m.advance(10,true,true);
            require(std::abs(m.stamina-350)<1e-8,"Sprint drains exactly fifteen stamina per second");
            m.advance(2,false,true);
            require(std::abs(m.stamina-400)<1e-8,"Released Shift restores twenty-five stamina per second");
            m.advance(10,true,false);
            require(m.stamina==400,"Stationary held Shift does not consume sprint stamina");
            m.advance(400./15,true,true);
            require(m.stamina==0 && m.exhausted,"Empty stamina starts exhausted recovery");
            require(m.advance(19,true,true)==0 && m.stamina==475,"Held Shift recovers stamina without early sprint restart");
            m.advance(1,true,true);
            require(m.stamina==500 && !m.exhausted,"Held Shift recovers to full in twenty seconds");
            require(m.advance(1,true,true)==1 && m.stamina==485,"Held Shift automatically resumes sprint after full recovery");
            m.advance(.4,false,false);
            require(m.stamina==495,"Releasing Shift recovers from a partial stamina balance");
            m.advance(1,false,false);
            require(m.stamina==500,"Partial recovery caps at five hundred");
            require(m.spend_dodge() && m.spend_dodge() && !m.spend_dodge(),"Two separate dodge charges prevent a third dodge");
            m.advance(4.9,false,false);
            require(m.dodgeCharges==0,"Dodge charge does not return early");
            m.advance(.11,false,false);
            require(m.dodgeCharges==1,"One dodge charge returns after five seconds");
            m.advance(5,false,false);
            require(m.dodgeCharges==2 && m.dodgeRecharge==0,"Second dodge returns sequentially and recharge stops at full");
            require(m.spend_grapple() && m.spend_grapple() && !m.spend_grapple(),"Grapple has two independent charges");
            m.advance(9.9,false,false);require(m.grappleCharges==0,"Grapple does not recharge before ten seconds");
            m.advance(.2,false,false);require(m.grappleCharges==1,"One grapple charge returns every ten seconds");
            m.advance(10,false,false);require(m.grappleCharges==2 && m.dodgeCharges==2,"Grapple recharge is independent of dodge charges");
        }
        {
            std::vector<TutorialSection> catalog{{"start",3},{"save",2},{"build",4}};
            TutorialProgress progress(3);progress.selected=2;progress.positions={1,1,3};
            auto encoded=encode_tutorial_progress(catalog,progress);
            auto file=folder/"tutorial-progress.json";atomic_write(file,encoded);
            auto restored=decode_tutorial_progress(catalog,read_file(file));
            require(restored.selected==2 && restored.positions==progress.positions,"Tutorial reading positions survive saved-file round trip");
            auto reordered=decode_tutorial_progress({{"build",4},{"new",2},{"save",2}},encoded);
            require(reordered.selected==0 && reordered.positions==std::vector<size_t>({3,0,1}),"Tutorial IDs preserve progress after reordering and adding lessons");
            auto shortened=decode_tutorial_progress({{"start",1},{"save",2}},encoded);
            require(shortened.selected==0 && shortened.positions==std::vector<size_t>({0,1}),"Removed lesson and shortened steps recover safely");
            for(auto bad:{"broken","{}","1 2 1","1 99 0 0 0","1 0 0 0 0 extra"}){
                auto clean=decode_tutorial_progress(catalog,bad);
                require(clean.selected==0 && clean.positions==std::vector<size_t>(3,0),"Invalid tutorial preferences do not prevent help opening");
            }
            auto old=decode_tutorial_progress(catalog,"1 2 1 1 3");
            require(old.selected==2 && old.positions==progress.positions,"First tutorial progress format remains readable");
            auto invalid=nlohmann::json::parse(encoded);invalid["positions"]["build"]=-1;
            auto bounded=decode_tutorial_progress(catalog,invalid.dump());
            require(bounded.positions[2]==0 && bounded.positions[1]==1,"Invalid individual step cannot escape lesson bounds");
        }
        SceneData s;
        require(hash_bytes("")=="E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855",
                "SHA256 empty input matches the standard vector");
        require(hash_bytes("abc")=="BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD",
                "SHA256 text matches the standard vector");
        {
            auto file=folder/"atomic-reader-conflict.txt";
            atomic_write(file,"original");
            HANDLE reader=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
            require(reader!=INVALID_HANDLE_VALUE,"Reader conflict fixture opens");
            std::jthread release([reader]{Sleep(120);CloseHandle(reader);});
            atomic_write(file,"updated");
            release.join();
            require(read_file(file)=="updated","Atomic commit survives a temporary report reader");
            Handle held(CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr));
            require(bool(held),"Persistent reader fixture opens");
            bool rejected=false;
            try{atomic_write(file,"must not replace");}catch(const std::exception&){rejected=true;}
            require(rejected && read_file(file)=="updated","Persistent reader conflict fails with original preserved");
        }
        {
            auto nested=folder;
            while(nested.wstring().size()<290)nested/=L"long-path-fixture";
            auto file=nested/L"saved.txt";
            atomic_write(file,"first");
            atomic_write(file,"second",true,hash_file(file));
            require(read_file(file)=="second","Atomic save and backup work beyond MAX_PATH");
        }
        {
            auto library = folder / ("library-" + new_id());
            atomic_write(library / "models" / "Oak.GLB", "index only");
            atomic_write(library / "props.starobject", "index only");
            atomic_write(library / "arrangement.stargroup", "index only");
            atomic_write(library / "world.starscene", "index only");
            atomic_write(library / "ignore.txt", "not an asset");
            auto index = scan_asset_library(library);
            require(index.entries.size() == 4 && !index.partial && index.errors == 0,
                "Asset library indexes supported file types including uppercase extensions");
            require(filter_asset_library(index, L"OAK").size() == 1 && filter_asset_library(index, L"models/").size() == 1 &&
                filter_asset_library(index, L"missing").empty(), "Asset library search filters names and relative paths case insensitively");
            require(filter_asset_library(index, L"", L".GLB").size() == 1 &&
                filter_asset_library(index, L"oak", L".starobject").empty() &&
                filter_asset_library(index, L"props", L".starobject").size() == 1,
                "Asset type filter combines with search and accepts uppercase model extensions");
        }
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
        for (const auto *older : {"{\"SchemaVersion\":6}", "{\"SchemaVersion\":15}", "{}"}) {
            bool conversion = false;
            try { SceneData::decode(older); } catch (const ConversionRequired &) { conversion = true; }
            require(conversion, "A scene older than the key-card schema is refused with a conversion-required error, never silently read");
        }
        {
            bool conversion = false;
            try { SceneData::decode("{\"SchemaVersion\":16,\"Characters\":[{\"Sculpt\":{\"Positions\":[0,0,0],\"Indices\":[0,0,0]}}]}"); } catch (const ConversionRequired &) { conversion = true; }
            require(conversion, "A key card that embeds sculpt geometry is refused with a conversion-required error");
            conversion = false;
            try { SceneData::decode("{\"SchemaVersion\":16,\"Terrain\":{\"Heights\":[]}}"); } catch (const ConversionRequired &) { conversion = true; }
            require(conversion, "A key card that embeds terrain grids is refused with a conversion-required error");
        }
        for (const auto *text : {"null", "{\"SchemaVersion\":16,\"Characters\":null}", "{\"SchemaVersion\":99}",
                                 "{\"SchemaVersion\":16,\"PathBounces\":99}", "{\"SchemaVersion\":16,\"Terrain\":{\"Asset\":\"terrain-0\"}}",
                                 "{\"SchemaVersion\":16.5}", "{\"SchemaVersion\":16,\"PathBounces\":4294967297}",
                                 "{\"SchemaVersion\":16,\"Characters\":[{\"Sculpt\":{\"Asset\":\"sculpt-0\"}}]}"}) {
            bool rejected = false;
            try {
                SceneData::decode(text);
            } catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}
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
        } catch(...){stale=true;note_rejection(__FILE__,__LINE__);}
        require(stale, "Stale edit rejected");
        configure(c, 2, -1, 2.25, .75);
        require(c.Route[0].X == 5 && c.Route[0].Z == -1, "Character configure moves route");
        bool legacyAccepted = true;
        try {
            SculptMesh{}.validate();
            SculptMesh wide{{9999, 0, 0}, {}};
            wide.validate();
        } catch(...){legacyAccepted=false;note_rejection(__FILE__,__LINE__);}
        require(legacyAccepted, "Legacy sculpt limits preserved");
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
        {
            Character object;
            object.Shape = "rock"; object.Name = "Reusable stone"; object.Color = "#81716A";
            object.Dynamic = true; object.Density = 2800; object.Width = 1.7;
            object.X = 2; object.Z = -3; object.Route = {{3,-2},{4,-1}};
            const auto saved = folder / "stone.starobject";
            save_object_template(object, saved);
            const auto savedHash = hash_file(saved);
            auto first = load_object_template(saved), second = load_object_template(saved);
            require(first.Id != second.Id && first.Id != object.Id && first.Color == object.Color &&
                first.Dynamic && first.Density == 2800 && first.Width == 1.7,
                "Reusable objects preserve settings with independent identities");
            place_object_template(first, 5, 1);
            require(first.X == 5 && first.Z == 1 && first.Route[0].X == 6 && first.Route[0].Z == 2 &&
                hash_file(saved) == savedHash, "Reusable object placement translates route without altering saved file");
            auto sculptCopy = decode_object_template(encode_object_template(sculpt));
            require(sculptCopy.Sculpt && sculptCopy.Sculpt->Positions == sculpt.Sculpt->Positions &&
                sculptCopy.Sculpt->Indices == sculpt.Sculpt->Indices, "Reusable object preserves sculpt geometry");
            SceneData target; SceneHistory edits;
            edits.begin(target); target.Characters.push_back(first); edits.commit();
            require(edits.undo(target) && target.Characters.empty() && edits.redo(target) &&
                target.Characters.front().Id == first.Id, "Reusable object insertion supports Undo and Redo");
            auto invalid = json::parse(read_file(saved)); invalid["Version"] = 3;
            bool refused = false;
            try { decode_object_template(invalid.dump()); } catch(...){refused=true;note_rejection(__FILE__,__LINE__);}
            require(refused, "Unknown reusable object version is rejected");
            invalid["Version"] = 2; invalid["Object"]["Asset"] = "outside.glb";
            refused = false;
            try { decode_object_template(invalid.dump()); } catch(...){refused=true;note_rejection(__FILE__,__LINE__);}
            require(refused, "Reusable objects reject hidden external asset dependencies");
            Character imported; imported.Asset = "missing.glb";
            refused = false;
            try { encode_object_template(imported); } catch(...){refused=true;note_rejection(__FILE__,__LINE__);}
            require(refused, "A reusable object that names its model by file path is refused");
            auto secondObject = object; secondObject.Id = new_id(); secondObject.X += 3;
            secondObject.Y = 2; secondObject.Route = {{7,4}};
            auto groupFile = folder / "arrangement.stargroup";
            save_object_group({object, secondObject}, groupFile);
            auto group = load_object_group(groupFile);
            require(group.size() == 2 && group[0].Id != object.Id && group[1].Id != secondObject.Id && group[0].Id != group[1].Id,
                "Reusable groups assign independent identities to every member");
            SceneData placement;
            place_object_group(group, placement, 1.24, 2.24, true, false, false);
            require(group[0].X == 1 && group[0].Z == 2 && group[1].X == 4 &&
                group[1].Y - group[0].Y == 2 && group[1].Route[0].X == 6 && group[1].Route[0].Z == 9,
                "Group placement preserves spacing and routes while snapping its anchor");
            SceneHistory groupHistory; groupHistory.begin(placement); placement.Characters = group; groupHistory.commit();
            require(groupHistory.undo(placement) && placement.Characters.empty() && groupHistory.redo(placement) && placement.Characters.size() == 2,
                "Whole group insertion is a single Undo and Redo transaction");
            auto badGroup = json::parse(read_file(groupFile)); badGroup["Objects"] = json::array(); refused = false;
            try { decode_object_group(badGroup.dump()); } catch(...){refused=true;note_rejection(__FILE__,__LINE__);}
            require(refused, "Empty reusable group is rejected");
        }
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
        {   // 100 blades; each has three columns of rings (the folded cross section) and a tip. Full detail has 7 rings; STARLIGHT_MESH_DETAIL removes rings, nothing else.
            const double detail = mesh_detail();
            const int rings = detail >= 1 ? 7 : std::clamp(int(std::lround(1 + 6 * detail)), 3, 7);
            require(grassMesh.vertices.size() == size_t(100 * (rings * 3 + 1)) && grassMesh.indices.size() == size_t(100 * ((rings - 1) * 4 + 2) * 3),
                    "Curved grass blades have folded cross sections and tapered tips");
        }
        bool grassValid=true;size_t roots=0;
        for(const auto& v:grassMesh.vertices){grassValid&=std::isfinite(v.normal.x+v.normal.y+v.normal.z)&&std::abs(v.normal.length()-1)<1e-8&&v.color.x>=0&&v.color.x<=1&&v.color.y>=0&&v.color.y<=1&&v.color.z>=0&&v.color.z<=1;roots+=v.position.y==0;}
        for(size_t i=0;i<grassMesh.indices.size();i+=3){auto a=grassMesh.vertices[grassMesh.indices[i]].position,b=grassMesh.vertices[grassMesh.indices[i+1]].position,c=grassMesh.vertices[grassMesh.indices[i+2]].position;grassValid&=(b-a).cross(c-a).length()>1e-12;}
        require(grassValid&&roots==300,"Blade geometry has finite unit normals, anchored roots, valid leaf coordinates and no degenerate tips");
        Vec3 grassSum{};json grassPositions=json::array();for(size_t i=0;i<grassMesh.vertices.size();++i){auto p=grassMesh.vertices[i].position;grassSum=grassSum+p;if(i<20)grassPositions.push_back({p.x,p.y,p.z});}
        atomic_write(folder/"rebuilt-grass-reference.json",json{{"grass",json::array({json{{"vertices",grassMesh.vertices.size()},{"indices",grassMesh.indices.size()},{"positions",grassPositions},{"sum",{grassSum.x,grassSum.y,grassSum.z}}}})}}.dump(2));
        TerrainData carved;
        carved.Volumes.push_back({0,-2,0,1,true});
        require(carved.density_at(0,-2,0)>0 && carved.density_at(0,-.5,0)<0 && carved.density_at(0,-3.5,0)<0,"Carved terrain retains a solid roof and floor");
        auto caveRoof=terrain_hit(&carved,{0,-2,0},{0,1,0});
        require(caveRoof && std::abs(caveRoof->y+1)<.01,"Picking from inside a cavity finds its roof");
        carved.Volumes.push_back({0,1,0,2,false});
        require(carved.density_at(0,1,0)<0 && carved.density_at(3,1,0)>0,"Terrain union creates material above the heightfield");
        auto restoredTerrain=json(carved).get<TerrainData>();
        require(restoredTerrain.Volumes.size()==2 && restoredTerrain.density_at(0,1,0)==carved.density_at(0,1,0),"Volumetric terrain scene roundtrip");
        auto terrainBuildStart=std::chrono::steady_clock::now();
        auto caveGeometry=terrain_geometry(carved);
        double terrainBuildMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-terrainBuildStart).count();
        auto cacheStart=std::chrono::steady_clock::now();auto cachedCave=terrain_geometry(carved);
        double terrainCacheMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cacheStart).count();
        auto originalPosition=caveGeometry[0].mesh.vertices[0].position;cachedCave[0].mesh.vertices[0].position.x+=1;
        require((terrain_geometry(carved)[0].mesh.vertices[0].position-originalPosition).length()<1e-12,"Terrain mesh cache returns independently owned geometry");
        atomic_write(folder/"terrain-metrics.json",json{{"buildMs",terrainBuildMs},{"cachedCopyMs",terrainCacheMs},{"vertices",caveGeometry[0].mesh.vertices.size()},{"triangles",caveGeometry[0].mesh.indices.size()/3},{"meshBytes",caveGeometry[0].mesh.vertices.size()*sizeof(Vertex)+caveGeometry[0].mesh.indices.size()*sizeof(int)}}.dump(2));
        require(!caveGeometry.empty() && !caveGeometry[0].mesh.indices.empty(),"Volumetric terrain extracts visible geometry");
        {
            TerrainVolumeCache editing;editing.get(carved);auto changed=carved;changed.Volumes.push_back({1.875,.1,1.875,.6,true});
            auto start=std::chrono::steady_clock::now();auto incremental=editing.get(changed);
            double updateMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();size_t rebuilt=editing.rebuiltChunks;
            TerrainVolumeCache clean;auto fresh=clean.get(changed);
            bool identical=incremental.indices==fresh.indices && incremental.vertices.size()==fresh.vertices.size();
            if(identical)for(size_t i=0;i<fresh.vertices.size();++i){const auto& a=incremental.vertices[i];const auto& b=fresh.vertices[i];if((a.position-b.position).length()>1e-8 || (a.normal-b.normal).length()>1e-8 || (a.color-b.color).length()>1e-8 || std::abs(a.kind-b.kind)>1e-6){identical=false;break;}}
            require(identical,"Localized volume edits match a complete rebuild across chunk boundaries");
            require(rebuilt>0 && rebuilt<169,"A small terrain stamp rebuilds only affected chunks");
            auto undone=editing.get(carved);require(undone.indices==caveGeometry[0].mesh.indices && undone.vertices.size()==caveGeometry[0].mesh.vertices.size(),"Undoing a terrain stamp restores the original chunk topology");
            atomic_write(folder/"terrain-local-update.json",json{{"updateMs",updateMs},{"rebuiltChunks",rebuilt},{"totalChunks",169},{"vertices",incremental.vertices.size()}}.dump(2));
        }

        bool upper=false,lower=false,finiteVolumeNormals=true;
        for(const auto& v:caveGeometry[0].mesh.vertices){upper=upper || v.position.y>2;lower=lower || v.position.y<-20;finiteVolumeNormals=finiteVolumeNormals && std::isfinite(v.normal.x+v.normal.y+v.normal.z);}
        require(upper && lower && finiteVolumeNormals,"Volume mesh includes raised mass, closed bottom and finite normals");
        std::map<std::pair<int,int>,int> volumeEdges;
        const auto& volumeMesh=caveGeometry[0].mesh;
        for(size_t i=0;i<volumeMesh.indices.size();i+=3)for(int e=0;e<3;++e){int a=volumeMesh.indices[i+e],b=volumeMesh.indices[i+(e+1)%3];volumeEdges[{std::min(a,b),std::max(a,b)}]++;}
        require(std::all_of(volumeEdges.begin(),volumeEdges.end(),[](const auto& edge){return edge.second==2;}),"Volume terrain has no open triangle edges");
#ifdef STARLIGHT_CHARACTER_CONTROLLER
        {
            SceneData underground;underground.Game.emplace();underground.Terrain.emplace();underground.Terrain->Volumes.push_back({0,-4,0,3,true});
            AdventureController caveController(underground,{0,-2.3,0});Vec3 eye{0,-2.3,0};
            for(int tick=0;tick<480;++tick)eye=caveController.step(eye,{},1./120.,false,tick/120.,{});
            require(eye.y<-4.8 && eye.y>-5.6,"Character settles on the carved cavity floor rather than original heightfield");
            AdventurePlayer viewer;viewer.start(underground);viewer.position={0,-4,0};viewer.cameraDistance=5;viewer.cameraYaw=viewer.cameraPitch=0;
            auto boom=(viewer.camera_eye()-viewer.view_position()).length();
            require(boom>2.5 && boom<3,"Third-person camera extends inside a cave and stops at its actual wall");
        }
#endif
        TerrainData flat;
        auto terrain = terrain_geometry(flat);
        require(terrain.size() == 1 && terrain[0].mesh.vertices.size() == 193 * 193 + 192 * 16 + 4 &&
                    terrain[0].mesh.indices.size() == 192 * 192 * 6 + 192 * 24 + 6,
                "Terrain surface, closed perimeter and underside sampling");
        require(terrain[0].mesh.vertices.back().position.y == -1 && terrain[0].mesh.vertices.back().normal.y < -.99,
                "Terrain underside has depth and faces downward");
        flat.WaterEnabled = true;
        flat.WaterLevel = .5;
        auto wet = terrain_geometry(flat);
        require(wet.size() == 2 && wet[1].mesh.indices.size() == 192 * 192 * 6,
                "Water clipping on submerged plane");
        stamp_shape(flat, cube);
        require(flat.height_at(0, 0) == 1 && flat.height_at(2, 2) == 0,
                "Terrain stamp raises footprint only");
        {
            json snapshots=json::object();
            for(const auto* kind:{"rock","boulder","pebbles","chaos-rock","fire"}){
                Character stone;stone.Shape=kind;auto mesh=shape_geometry(stone)[0].mesh;std::map<std::pair<int,int>,int> edges;bool valid=true;Vec3 sum;
                for(const auto& v:mesh.vertices){sum=sum+v.position;valid=valid && std::abs(v.normal.length()-1)<1e-6 && v.position.y>=-1e-9;}
                for(size_t i=0;i<mesh.indices.size();i+=3){auto a=mesh.vertices[mesh.indices[i]].position,b=mesh.vertices[mesh.indices[i+1]].position,c=mesh.vertices[mesh.indices[i+2]].position;valid=valid && (b-a).cross(c-a).length()>1e-12;for(int e=0;e<3;++e){int x=mesh.indices[i+e],y=mesh.indices[i+(e+1)%3];edges[{std::min(x,y),std::max(x,y)}]++;}}
                require(valid && std::all_of(edges.begin(),edges.end(),[](const auto& edge){return edge.second==2;}),"Rebuilt assets and fire proxy have closed nondegenerate surfaces and unit normals");
                json positions=json::array();for(size_t i=0;i<std::min(size_t(20),mesh.vertices.size());++i){auto p=mesh.vertices[i].position;positions.push_back({p.x,p.y,p.z});}
                snapshots[kind]=json::array({json{{"vertices",mesh.vertices.size()},{"indices",mesh.indices.size()},{"positions",positions},{"sum",{sum.x,sum.y,sum.z}}}});
            }
            atomic_write(folder/"rebuilt-stone-reference.json",snapshots.dump(2));
        }
        if (argc > 2) {
            auto reference = json::parse(read_file(argv[2]));
            for (auto it = reference.begin(); it != reference.end(); ++it) {
                Character shape;
                shape.Shape = it.key();
                auto actual = shape_geometry(shape);
                require(actual.size() == it.value().size(), "Canonical built-in geometry part count");
                for (size_t p = 0; p < actual.size(); p++) {
                    const auto &expected = it.value()[p];
                    const auto &mesh = actual[p].mesh;
                    require(mesh.vertices.size() == expected.at("vertices").get<size_t>() &&
                                mesh.indices.size() == expected.at("indices").get<size_t>(),
                            "Canonical built-in geometry topology");
                    Vec3 sum;
                    for (const auto &v : mesh.vertices)
                        sum = sum + v.position;
                    auto e = expected.at("sum").get<std::vector<double>>();
                    require((sum - Vec3{e[0], e[1], e[2]}).length() < 1e-8,
                            "Canonical built-in geometry position sums");
                    for (size_t i = 0; i < expected.at("positions").size(); i++) {
                        auto q = expected.at("positions")[i].get<std::vector<double>>();
                        require((mesh.vertices[i].position - Vec3{q[0], q[1], q[2]}).length() < 1e-12,
                                "Canonical built-in seeded vertex coordinates");
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
        } catch(...){invalid=true;note_rejection(__FILE__,__LINE__);}
        require(invalid && hash_file(file) == hash, "Failed command batch preserves original bytes");
        {
            auto source = folder / "stone.starobject";
            ops["expectedSha256"] = hash_file(file);
            ops["operations"] = json::array({json{{"op","insert-object"},{"file","stone.starobject"},
                {"expectedSha256",hash_file(source)},{"name","Inserted stone"},{"x",5},{"z",1}}});
            atomic_write(request, ops.dump()); apply_commands(request);
            auto inserted = SceneData::decode(read_file(file)).Characters.back();
            require(inserted.Name == "Inserted stone" && inserted.Route[0].X == 6 && inserted.Route[0].Z == 2,
                "Command inserts saved object with relative path and translated route");
            ops["expectedSha256"] = hash_file(file);
            ops["operations"] = json::array({json{{"op","duplicate"},{"id",inserted.Id},{"x",7},{"z",2}}});
            atomic_write(request, ops.dump()); apply_commands(request);
            auto duplicated = SceneData::decode(read_file(file)).Characters.back();
            require(duplicated.Id != inserted.Id && duplicated.Name == "Inserted stone copy" &&
                duplicated.Route[0].X == 8 && duplicated.Route[0].Z == 3 && duplicated.Density == inserted.Density,
                "Command duplication preserves settings and moves route independently");
            auto unchanged = hash_file(file);
            ops["expectedSha256"] = unchanged;
            ops["operations"].push_back(json{{"op","insert-object"},{"file","stone.starobject"},{"expectedSha256","stale"}});
            atomic_write(request, ops.dump()); bool rejected = false;
            try { apply_commands(request); } catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}
            require(rejected && hash_file(file) == unchanged,
                "Stale object file rolls back entire command batch including earlier duplication");
            SceneData from; from.Hour = 1;
            Character item; item.Shape = "cube"; item.X = 2; item.Route = {{3,1}};
            from.Characters.push_back(item);
            auto sourceScene = folder / "merge-source.starscene"; save_scene(from, sourceScene);
            auto originalWorld = load_scene(file).Hour;
            auto originalCount = load_scene(file).Characters.size();
            ops["expectedSha256"] = hash_file(file);
            ops["operations"] = json::array({json{{"op","merge-scene"},{"file","merge-source.starscene"},
                {"expectedSha256",hash_file(sourceScene)},{"x",4},{"z",2}}});
            atomic_write(request, ops.dump()); apply_commands(request);
            auto merged = load_scene(file);
            require(merged.Characters.size() == originalCount + 1 && merged.Hour == originalWorld &&
                merged.Characters.back().Id != item.Id && merged.Characters.back().Route[0].X == 5,
                "Scene composition preserves destination world and gives imported objects fresh identities");
            auto priorGroupCount = merged.Characters.size();
            ops["expectedSha256"] = hash_file(file);
            ops["operations"] = json::array({json{{"op","insert-group"},{"file","arrangement.stargroup"},
                {"expectedSha256",hash_file(folder / "arrangement.stargroup")},{"x",1},{"z",2}}});
            atomic_write(request, ops.dump()); apply_commands(request);
            auto insertedGroup = load_scene(file);
            require(insertedGroup.Characters.size() == priorGroupCount + 2 &&
                insertedGroup.Characters.back().X - insertedGroup.Characters[priorGroupCount].X == 3,
                "Guarded group command inserts the complete arrangement with spacing intact");
        }
        auto flowFixture=std::unique_ptr<TerrainFlow>(new TerrainFlow(TerrainData{}, {{0, 0, false, 2}, {2, 0, true, 1}}));auto& flow=*flowFixture;
        for (int i = 0; i < 200; i++)
            flow.step(.025);
        require(std::abs(flow.volume() - 3) < 1e-10 && std::abs(flow.emitted - 3) < 1e-10,
                "Closed fluid volume conserved after emission");
        {
            TerrainData lake;lake.WaterEnabled=true;lake.WaterLevel=.2;
            auto waterFixture=std::unique_ptr<TerrainFlow>(new TerrainFlow(lake,{}));auto& water=*waterFixture;double volume=water.volume();
            require(std::abs(volume-24*24*.2)<1e-8,"Authored water initializes a finite simulated volume");
            for(int i=0;i<50;++i)water.step(.02);
            require(std::abs(water.volume()-volume)<1e-8,"Initially resting authored water conserves mass");
            double area=0;for(const auto& part:water.geometry())for(size_t j=0;j<part.mesh.indices.size();j+=3){auto a=part.mesh.vertices[part.mesh.indices[j]].position,b=part.mesh.vertices[part.mesh.indices[j+1]].position,c=part.mesh.vertices[part.mesh.indices[j+2]].position;area+=(b-a).cross(c-a).length()*.5;}
            require(std::abs(area-24*24)<1e-7,"Shared liquid vertices preserve complete lake surface coverage");
            Character pool;pool.Shape="lava";pool.X=2;pool.Y=.3;
            std::vector<Character> pools{pool};auto moltenFixture=std::unique_ptr<TerrainFlow>(new TerrainFlow(TerrainData{}, {},pools));auto& molten=*moltenFixture;
            double lavaVolume=molten.volume();auto initial=molten.lava;
            for(int i=0;i<100;++i)molten.step(.01);
            require(lavaVolume>0 && std::abs(molten.volume()-lavaVolume)<1e-8 && molten.lava!=initial,"Authored lava pool spreads while conserving its finite volume");
        }
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
            const auto fixtureModel = import_model(fixture);
            {
                auto stored = load_model(fixtureModel);
                require(EngineAssets::valid_id(AssetKind::Model, fixtureModel) && stored->size() == parts.size() && stored->front().mesh.vertices.size() == 3,
                    "Importing a GLB stores it in the shared library under a content id that the shared parser reads back");
            }
            {
                SceneData broken; Character imported;
                imported.Asset = "model-" + std::string(64, 'a'); imported.Name = "Route keeper";
                imported.Width = 2; imported.X = 3; imported.Route = {{4,5}};
                broken.Characters.push_back(imported);
                auto brokenFile = folder / "repair.starscene"; save_scene(broken, brokenFile, {}, true);
                auto brokenInspection = inspect_scene(brokenFile);
                require(brokenInspection.at("missingAssetCount") == 1 && brokenInspection.at("assets").size() == 1,
                    "Scene inspection identifies missing imported models without opening renderer");
                auto repairDraft = broken; repairDraft.Characters.push_back(imported); repairDraft.Characters.back().Id = new_id();
                size_t prompts = 0;
                auto repairedCount = repair_scene_models(repairDraft, [&](const std::string&, const std::string&) -> std::optional<fs::path> { ++prompts; return fixture; });
                require(repairedCount && *repairedCount == 1 && prompts == 1 &&
                    repairDraft.Characters[0].Asset == repairDraft.Characters[1].Asset && repairDraft.Characters[0].Asset == fixtureModel &&
                    inspect_scene(brokenFile).at("missingAssetCount") == 1,
                    "Guided model repair resolves shared references once without writing the scene");
                auto cancelDraft = broken;
                auto extraMissing = imported; extraMissing.Id = new_id(); extraMissing.Asset = "model-" + std::string(64, 'b');
                cancelDraft.Characters.push_back(extraMissing);
                const auto cancelledBefore = cancelDraft.encode(); prompts = 0;
                auto cancelled = repair_scene_models(cancelDraft, [&](const std::string&, const std::string&) -> std::optional<fs::path> {
                    if (++prompts == 1) return fixture;
                    return std::nullopt;
                });
                require(!cancelled && cancelDraft.encode() == cancelledBefore,
                    "Cancelling later model repair preserves the complete original scene draft");
                auto replacement = folder / "replacement.glb"; atomic_write(replacement, read_file(fixture));
                json repair{{"schemaVersion",1},{"scenePath",utf8(fs::absolute(brokenFile).wstring())},
                    {"expectedSha256",hash_file(brokenFile)},
                    {"operations",json::array({json{{"op","replace-model"},{"id",imported.Id},{"asset","replacement.glb"}}})}};
                atomic_write(request, repair.dump()); apply_commands(request);
                auto repaired = load_scene(brokenFile);
                require(inspect_scene(brokenFile).at("missingAssetCount") == 0, "Scene inspection clears missing model finding after relink");
                require(repaired.Characters[0].Id == imported.Id && repaired.Characters[0].Name == imported.Name &&
                    repaired.Characters[0].Width == 2 && repaired.Characters[0].X == 3 &&
                    repaired.Characters[0].Route[0].Z == 5 && render_scene(repaired).size() > 0,
                    "Missing model can be relinked without losing object identity or route");
                auto intact = hash_file(brokenFile);
                atomic_write(replacement, "invalid GLB"); repair["expectedSha256"] = intact;
                atomic_write(request, repair.dump()); bool rejected = false;
                try { apply_commands(request); } catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}
                require((rejected && hash_file(brokenFile) == intact) && last_rejection().find("Invalid GLB") != std::string::npos, "Invalid replacement model leaves scene bytes unchanged");
            }
            {
                SceneData portable;
                Character model; model.Asset = fixtureModel;
                portable.Characters = {model, model}; portable.Characters.back().Id = new_id();
                auto destination = folder / ("portable-" + new_id());
                auto report = export_portable_scene(portable, destination);
                require(report.at("assetCount") == 1, "Portable export deduplicates shared model bytes");
                auto selectionFolder = folder / ("selection-" + new_id());
                portable.Terrain.emplace(); portable.Hour = 2;
                export_scene_selection(portable, {portable.Characters.back().Id}, selectionFolder);
                auto subset = load_scene(selectionFolder / "scene.starscene");
                require(subset.Characters.size() == 1 && subset.Characters.front().Id == portable.Characters.back().Id &&
                    !subset.Terrain && verify_portable_scene(selectionFolder).at("success").get<bool>(),
                    "Portable selection includes only requested objects and required assets, without terrain");
                auto mergeTarget = folder / "portable-merge-target.starscene";
                SceneData emptyTarget; emptyTarget.Hour = 22; save_scene(emptyTarget, mergeTarget);
                json mergeRequest{{"schemaVersion",1},{"scenePath",utf8(fs::absolute(mergeTarget).wstring())},
                    {"expectedSha256",hash_file(mergeTarget)},{"operations",json::array({json{{"op","merge-scene"},
                    {"file",utf8(fs::absolute(selectionFolder / "scene.starscene").wstring())},
                    {"expectedSha256",hash_file(selectionFolder / "scene.starscene")},{"x",2},{"z",3}}})}};
                atomic_write(request, mergeRequest.dump()); apply_commands(request);
                auto composed = load_scene(mergeTarget);
                require(composed.Hour == 22 && composed.Characters.size() == 1 &&
                    composed.Characters.front().Id != subset.Characters.front().Id && render_scene(composed).size() > 0,
                    "Portable imported-model composition resolves the model assets bundled with the source scene");
                bool missingSelection = false;
                auto absentSelection = folder / ("missing-selection-" + new_id());
                try { export_scene_selection(portable, {"missing"}, absentSelection); } catch(...){missingSelection=true;note_rejection(__FILE__,__LINE__);}
                require(missingSelection && !fs::exists(absentSelection), "Unknown selected object cannot produce an incomplete export");
                auto relocated = folder / ("relocated-" + new_id());
                fs::rename(destination, relocated);
                require(verify_portable_scene(relocated).at("success").get<bool>(), "Relocated export passes integrity verification");
                auto sceneFile = relocated / "scene.starscene";
                auto reopened = load_scene(sceneFile);
                require(reopened.Characters[0].Asset == fixtureModel && EngineAssets::exists(AssetKind::Model, reopened.Characters[0].Asset) &&
                    render_scene(reopened).size() > 0, "Relocated portable scene resolves and renders its bundled model by id");
                {
                    EngineAssets::ScopedRoot isolated(folder / ("isolated-" + new_id()));
                    auto fromBundle = load_scene(sceneFile);
                    auto located = EngineAssets::locate(fromBundle.Characters[0].Asset);
                    require(!located.empty() && fs::equivalent(located.parent_path(), relocated / "assets") && !fs::exists(EngineAssets::file_for(fromBundle.Characters[0].Asset)),
                        "A library that never held the model finds it in the scene's bundle through the bundle search root");
                    save_scene(fromBundle, folder / ("from-bundle-" + new_id() + ".starscene"));
                    require(fs::is_regular_file(EngineAssets::file_for(fromBundle.Characters[0].Asset)), "Saving a scene opened from a bundle copies its model into the library");
                }
                save_scene(reopened, sceneFile, hash_file(sceneFile));
                auto onDisk = SceneData::decode(read_file(sceneFile));
                require(onDisk.Characters[0].Asset == fixtureModel, "Saving a reopened portable scene keeps the model id");
                bool refused = false;
                auto before = hash_file(sceneFile);
                try { export_portable_scene(portable, relocated); } catch(...){refused=true;note_rejection(__FILE__,__LINE__);}
                require((refused && hash_file(sceneFile) == before) && last_rejection().find("Choose a new folder") != std::string::npos, "Portable export refuses to overwrite existing folders");
                portable.Characters[0].Asset = "model-" + std::string(64, 'c');
                auto failed = folder / ("failed-export-" + new_id()); refused = false;
                try { export_portable_scene(portable, failed); } catch(...){refused=true;note_rejection(__FILE__,__LINE__);}
                require((refused && !fs::exists(failed)) && last_rejection().find("missing from the asset library") != std::string::npos, "Missing model does not publish a partial portable scene");
                auto manifestFile = relocated / "manifest.json";
                auto manifest = json::parse(read_file(manifestFile));
                manifest["sceneSha256"] = hash_file(sceneFile);
                atomic_write(manifestFile, manifest.dump());
                require(verify_portable_scene(relocated).at("success").get<bool>(), "Intact exported assets pass before corruption test");
                auto escaped = manifest; escaped["assets"][0]["path"] = utf8(fs::absolute(fixture).wstring());
                atomic_write(manifestFile, escaped.dump()); refused = false;
                try { verify_portable_scene(relocated); } catch(...){refused=true;note_rejection(__FILE__,__LINE__);}
                require((refused) && last_rejection().find("Invalid portable scene path") != std::string::npos, "Portable verification rejects absolute asset paths");
                atomic_write(manifestFile, manifest.dump());
                auto assetPath = relocated / "assets" / starlight::editor::wide(reopened.Characters[0].Asset + ".slasset");
                atomic_write(assetPath, "damaged"); refused = false;
                try { verify_portable_scene(relocated); } catch(...){refused=true;note_rejection(__FILE__,__LINE__);}
                require((refused) && last_rejection().find("changed since export") != std::string::npos, "Portable verification rejects damaged asset bytes");
            }
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
        #include "gameplay_rules_checks.inl"
        #include "quest_checks.inl"
        {
            SceneData adventure;adventure.Game.emplace();adventure.Game->MoveSpeed=6.25;
            adventure.Game->LookSensitivity=.37;adventure.Game->Collision=false;
            auto restored=SceneData::decode(adventure.encode());
            require(restored.SchemaVersion==16 && restored.Game && restored.Game->MoveSpeed==6.25 &&
                    restored.Game->LookSensitivity==.37 && !restored.Game->Collision,
                    "Adventure player settings survive scene save and load");
            require(!SceneData::decode("{\"SchemaVersion\":16}").Game,"Legacy scenes do not acquire gameplay implicitly");
            for(auto field:{"MoveSpeed","LookSensitivity","PlayerRadius"}){
                auto invalid=json(adventure);invalid["Game"][field]=-1;
                bool rejected=false;try{SceneData::decode(invalid.dump());}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}
                require(rejected,"Invalid gameplay tuning rejected");
            }
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=2;
            Character wall;wall.Shape="cube";wall.Width=4;wall.Height=3;wall.Depth=.1;level.Characters.push_back(wall);
            auto original=level.encode();AdventurePlayer player;player.start(level);
            for(int i=0;i<60;i++)player.move(0,1,.05);
            require(player.position.z>=.3,"Adventure movement cannot cross a thin wall");
            require(level.encode()==original,"Adventure play does not mutate authored scene");
            level.Game->Collision=false;player.start(level);for(int i=0;i<60;i++)player.move(0,1,.05);
            require(player.position.z<0,"Adventure collision setting is honored");
            level.Characters.clear();player.start(level);player.move(1,1,.05);
            require(std::abs(std::hypot(player.position.x,player.position.z-2)-.2)<1e-8,"Diagonal movement is normalized");
            player.look(0,100000);require(std::abs(player.pitch)<=1.45,"Adventure look pitch is bounded");
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=2;level.Game->SpawnY=.5;
            Character door;door.Shape="cube";door.Height=2;door.GameplayRole="door";door.GameplayKey="blue";
            Character key;key.Shape="cube";key.X=3;key.GameplayRole="key";key.GameplayKey="blue";
            Character exit;exit.Shape="cube";exit.X=6;exit.GameplayRole="exit";exit.GameplayKey="blue";
            level.Characters={door,key,exit};AdventurePlayer player;player.start(level);
            require(player.interact()=="Requires key: blue","Locked door rejects missing key");
            player.position.x=3;require(player.interact()=="Collected key: blue","Matching key collected");
            player.position.x=0;require(player.interact()=="Door opened.","Matching key opens door");
            player.position.x=6;require(player.interact()=="Adventure complete!" && player.finished,"Exit completes adventure");
            require(level.Characters.size()==3 && player.scene().Characters.size()==1,"Gameplay mutations remain in play copy");
            auto saved=SceneData::decode(level.encode());require(saved.Characters[0].GameplayKey=="blue","Gameplay roles and key links persist");
            auto reusable=decode_object_template(encode_object_template(key));require(reusable.GameplayRole=="key" && reusable.GameplayKey=="blue","Reusable objects preserve gameplay interaction");
        }
        {
            auto level=adventure_template();auto original=level.encode();
            AdventurePlayer player;player.start(SceneData::decode(original));
            for(int i=0;i<5;i++)player.move(0,1,.05);
            require(player.interact()=="Collected key: gold","Starter key reachable by walking from spawn");
            for(int i=0;i<40;i++)player.move(0,1,.05);
            require(player.position.z>0,"Starter locked door blocks passage");
            require(player.interact()=="Door opened.","Starter collected key opens actual doorway");
            for(int i=0;i<13;i++)player.move(0,1,.05);
            require(player.interact()=="Adventure complete!","Starter adventure completes through walking and interactions");
            require(level.encode()==original,"Starter play preserves authoring data");
            player.start(level);
            require(!player.finished && player.scene().Characters.size()==level.Characters.size(),"Starter restart restores all objects and progress");
            save_scene(level,folder/"starter-adventure.starscene");
        }
        {
            SceneData level;Character object;object.Shape="cube";object.X=.37;object.Z=-.37;
            object.Route={{.37,-.37}};
            snap_object(level,object,true,false,false,.25);
            require(object.X==.25 && object.Z==-.25 && object.Route[0].X==.25 && object.Route[0].Z==-.25,
                "Custom grid snaps negative coordinates and preserves attached route offsets");
            std::vector<Character> group={object,object};group[1].Id=new_id();group[1].X+=.17;
            place_object_group(group,level,1.13,0,true,false,false,.25);
            require(group[0].X==1.25 && std::abs(group[1].X-group[0].X-.17)<1e-9,
                "Custom grid preserves grouped object spacing");
            require(snap_increment(23,15)==30 && snap_increment(-23,15)==-30,"Rotation snap supports both directions");
            bool rejected=false;try{snap_increment(1,0);}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}
            require(rejected,"Invalid snap spacing rejected");
        }
        SceneData render;
        {
            auto starter=adventure_template();
            require(adventure_readiness(starter).empty(),"Starter adventure has no basic setup findings");
            auto collectionScene=starter;
            const auto collectible=std::find_if(collectionScene.Characters.begin(),collectionScene.Characters.end(),[](const auto& c){return c.GameplayRole=="key";});
            GameplayRule collectionRule;collectionRule.Id="readiness-collection";collectionRule.Name="Collection setup";collectionRule.Event="item-collected";collectionRule.Subject=collectible->Id;
            collectionRule.Actions.push_back({"complete-objective","collected"});collectionScene.Rules.push_back(collectionRule);
            auto collectionFindings=adventure_readiness(collectionScene);
            require(std::any_of(collectionFindings.begin(),collectionFindings.end(),[](const auto& f){return f.find("collection event expects an item key")!=std::string::npos;}),
                    "Collection setup identifies an object ID used instead of its emitted item key");
            collectionScene.Rules.back().Subject=collectible->GameplayKey;
            require(adventure_readiness(collectionScene).empty(),"Collection setup accepts the actual emitted item key");
            collectionScene.Rules.back().Subject.clear();
            require(adventure_readiness(collectionScene).empty(),"Collection setup preserves wildcard events");
            collectionScene.Rules.back().Subject="not-an-item";collectionScene.Rules.back().Enabled=false;
            require(adventure_readiness(collectionScene).empty(),"Collection setup ignores disabled events");
            starter.Characters.erase(std::remove_if(starter.Characters.begin(),starter.Characters.end(),[](const auto& c){return c.GameplayRole=="key";}),starter.Characters.end());
            auto findings=adventure_readiness(starter);
            require(std::count_if(findings.begin(),findings.end(),[](const auto& f){return f.find("no collectible key")!=std::string::npos;})==2,
                    "Gameplay setup identifies missing door and exit keys");
            starter.Game->SpawnZ=0;
            findings=adventure_readiness(starter);
            require(std::any_of(findings.begin(),findings.end(),[](const auto& f){return f.find("inside a solid")!=std::string::npos;}),
                    "Gameplay setup identifies blocked player start");
        }
        {
            auto level=adventure_template();AdventurePlayer builder;builder.start(level);
            builder.move(1,0,.05);builder.look(80,-12);
            const auto position=builder.position;const auto yaw=builder.yaw,pitch=builder.pitch;
            Character object;object.Shape="cube";object.X=2;level.Characters.push_back(object);
            builder.rebuild_for_authoring(level);
            require(builder.position.x==position.x && builder.position.y==position.y && builder.position.z==position.z &&
                    builder.yaw==yaw && builder.pitch==pitch,"Building refresh preserves explorer viewpoint");
            require(builder.scene().Characters.size()==level.Characters.size(),"Building refresh uses the same authored objects");
            auto original=level.encode();AdventurePlayer test;test.start(level);test.position={0,1.7,5};test.interact();
            builder.rebuild_for_authoring(level);
            require(level.encode()==original && builder.scene().Characters.size()==level.Characters.size(),
                    "Returning from gameplay retains authored key rather than collected play state");
            SceneData empty;empty.Game.emplace();builder.start(empty);
            Character wall;wall.Shape="cube";wall.Width=4;wall.Height=3;wall.Depth=.1;wall.Z=5;
            empty.Characters.push_back(wall);builder.rebuild_for_authoring(empty);
            for(int i=0;i<20;i++)builder.move(0,1,.05);
            require(builder.position.z>5,"New building edits update explorer collision");
            empty.Characters.clear();builder.rebuild_for_authoring(empty);
            for(int i=0;i<20;i++)builder.move(0,1,.05);
            require(builder.position.z<5,"Removed building objects no longer block exploration");
        }
        character_controller_checks(require,folder);
        render.Terrain.emplace();
        Character rock;
        rock.Shape = "chaos-rock";
        render.Characters.push_back(rock);
        {
            ParallelMetrics metrics;PersistentWorkers workers;metrics.enabled=true;
            std::vector<int> visited(300,0);
            metrics.run(300,3,[&](const auto& callback){workers.run(300,3,callback);},
                [&](int lo,int hi,int){for(int i=lo;i<hi;++i)++visited[i];});
            require(std::all_of(visited.begin(),visited.end(),[](int n){return n==1;}) && metrics.reading.parallelBatches==1 && metrics.reading.participants==3 && metrics.reading.items==300 && metrics.reading.chunkMs.size()==3,
                "Multicore monitor records actual dispatched participants and work without changing results");
            metrics.reset();
            metrics.run(10,1,[&](const auto& callback){callback(0,10,0);},[](int,int,int){});
            require(metrics.reading.serialBatches==1 && metrics.reading.parallelBatches==0 && metrics.reading.participants==1 && metrics.reading.callerWaitMs==0,
                "Multicore monitor distinguishes serial fallback from parallel execution");
            metrics.reset();metrics.enabled=false;
            metrics.run(10,1,[&](const auto& callback){callback(0,10,0);},[](int,int,int){});
            require(metrics.reading.items==0 && metrics.reading.chunkMs.empty(),"Disabled multicore monitor leaves collection off");
            PacingSnapshot recorded;recorded.multicore.parallelBatches=2;recorded.multicore.chunkMs={.1,.2};
            PacingFrameRecord raw;raw.multicore=recorded.multicore;raw.applicationForeground=true;raw.windowVisible=true;
            auto logPath=folder/"multicore-roundtrip.jsonl";
            {PacingLog log(logPath);log.push(recorded,{raw},1);}
            std::ifstream file(logPath);std::string line;std::getline(file,line);std::getline(file,line);auto sample=json::parse(line);
            require(sample["overlay"]["multicore"]["parallelBatches"]==2 && sample["frames"][0][18]["chunkMs"][1]==.2,
                "Continuous log preserves per-frame and overlay worker readings");
            require(sample["frames"][0][22]==true && sample["frames"][0][23]==false && sample["frames"][0][24]==true,
                "Pacing log preserves sampled foreground, minimization and visibility states");
        }
        {
            AdventurePlayer::Presentation p;p.showTelegraph=true;p.channelProgress=.6;p.actorTelegraphs.push_back({{2,0,3},{0,0,-1},.5,TelegraphKind::Enemy});
            BattlePresentationWorkers workers;ParallelWorkReading reading;
            auto parallel=workers.build(p,reading);
            require(render_scene(render,nullptr,parallel)==render_scene(render,nullptr,player_presentation(p)) && reading.parallelBatches==1 && reading.participants==2,
                "Parallel player and boss effects preserve the serial render packet exactly");
            p.actorTelegraphs.clear();workers.build(p,reading);
            require(reading.serialBatches==1 && reading.participants==1,"Single battle job avoids waking extra workers");
        }
        {
            AdventurePlayer::Presentation p;p.showBody=false;p.showTelegraph=true;p.channelProgress=.6;
            p.attackOrigin={2,0,3};p.attackAim={1,0,0};
            auto cpu=player_presentation(p),gpu=player_presentation(p,true);
            auto instance=std::find_if(gpu.begin(),gpu.end(),[](const auto& part){return part.gpuTelegraph.has_value();});
            require(instance!=gpu.end() && instance->mesh.vertices.empty() && std::abs(instance->gpuTelegraph->x-2)<1e-6 && std::abs(instance->gpuTelegraph->progress-.6)<1e-6,
                "Flat attack fills publish compact GPU instances instead of rebuilt CPU fill vertices");
            auto countVertices=[](const auto& parts){size_t count=0;for(const auto& part:parts)count+=part.mesh.vertices.size();return count;};
            require(countVertices(cpu)-countVertices(gpu)==size_t(24*int(std::ceil(p.telegraphRange/.5))*4),"Resident fill removes exactly the CPU fill grid and preserves outline geometry");
            TerrainData terrain;terrain.Heights[0]=1;p.telegraphTerrain=&terrain;
            auto sloped=player_presentation(p,true);
            require(std::none_of(sloped.begin(),sloped.end(),[](const auto& part){return part.gpuTelegraph.has_value();}) && render_scene(render,nullptr,sloped)==render_scene(render,nullptr,player_presentation(p)),
                "Non-flat terrain retains the identical terrain-following CPU effect");
        }
        {
            PhysicalParticle debris;debris.position={0,1,0};debris.velocity={100,0,0};
            std::vector<Bounds> obstacles{{{.5,0,-1},{.6,2,1}}};
            step_particle(debris,.02,nullptr,obstacles);
            require(debris.position.x<.5 && debris.velocity.x<0,"Fast debris sweeps against thin obstacles instead of tunneling");
            PhysicalParticle pausedParticle;pausedParticle.position={.55,1,0};pausedParticle.velocity={1,2,3};auto pausedPosition=pausedParticle.position,pausedVelocity=pausedParticle.velocity;step_particle(pausedParticle,0,nullptr,obstacles);require((pausedParticle.position-pausedPosition).length()==0&&(pausedParticle.velocity-pausedVelocity).length()==0&&pausedParticle.age==0,"Paused physical particles retain position and velocity even inside an obstacle");
            PhysicalParticle engulfed;engulfed.position={.55,1,0};step_particle(engulfed,.01,nullptr,obstacles);
            require(engulfed.position.x<=.5-engulfed.radius || engulfed.position.x>=.6+engulfed.radius,"A moving obstacle cannot retain an engulfed particle inside its collision volume");
            PhysicalParticle falling;falling.position={0,2,0};
            for(int i=0;i<1200;++i)step_particle(falling,1./240,nullptr,{});
            require(falling.position.y>=falling.radius-1e-6 && falling.position.y<falling.radius+.01 && std::abs(falling.velocity.y)<.1,
                "Debris collides, bounces and settles on terrain instead of falling through it");
            TerrainData cave;cave.Volumes.push_back({0,-2,0,1,true});PhysicalParticle underground;underground.position={0,-2,0};
            for(int i=0;i<1200;++i)step_particle(underground,1./240,&cave,{});
            require(underground.position.y>-2.95&&underground.position.y<-2.9&&std::abs(underground.velocity.y)<.1,"Particles settle on a carved cavity floor without snapping to the old ground plane");
            PhysicalParticle roof;roof.position={0,-2,0};roof.velocity={0,100,0};step_particle(roof,.02,&cave,{}, {},{});
            require(roof.position.y<-1.05&&roof.velocity.y<=0,"Fast particles sweep the solid cave ceiling instead of passing through it");
            PhysicalParticle side;side.position={0,-2,0};side.velocity={100,0,0};step_particle(side,.02,&cave,{}, {},{});
            require(side.position.x<.95&&side.velocity.x<=0,"Fast particles collide with carved side walls in three dimensions");
            TerrainData raised;raised.Volumes.push_back({0,2,0,1,false});PhysicalParticle impact;impact.position={-2,2,0};impact.velocity={100,0,0};step_particle(impact,.02,&raised,{}, {},{});
            require(impact.position.x<-1.05&&impact.velocity.x<=0,"Particles collide with raised terrain volumes above the original heightfield");
            PhysicalParticle edited;edited.position={0,2,0};edited.velocity={0,-1,0};step_particle(edited,.01,&raised,{}, {},{});
            require(raised.density_at(edited.position.x,edited.position.y,edited.position.z)>=edited.radius-1e-4,"Terrain edits eject engulfed particles into air rather than retaining them inside solid mass");
            TerrainData buried;std::fill(buried.Heights.begin(),buried.Heights.end(),20);buried.Volumes.push_back({0,0,0,.25,false});PhysicalParticle trapped;trapped.position={0,0,0};step_particle(trapped,.01,&buried,{}, {},{});
            require(buried.density_at(trapped.position.x,trapped.position.y,trapped.position.z)>=trapped.radius-1e-4,"Symmetric deep terrain overlap finds a nearby escape instead of choosing an unreachable upward surface");
        }
        {
            SceneData storm;Character bolt;bolt.Shape="chaos-lightning";bolt.TargetX=0;bolt.TargetY=-1;bolt.TargetZ=0;storm.Characters={bolt};
            EnvironmentEffects effects;
            for(int i=0;i<8;++i)effects.step(storm,.01,i*.01);
            auto hit=effects.contact(bolt.Id);
            require(hit && std::abs(hit->y)<.001 && effects.spark_count()>0,"Lightning reaches terrain and emits independently simulated impact sparks");
            auto first=render_scene(storm,nullptr,effects.geometry(storm),nullptr,true);
            effects.step(storm,.02,.1);
            require(first!=render_scene(storm,nullptr,effects.geometry(storm),nullptr,true),"Lightning sparks move between rendered scene packets");
            Character block;block.Shape="cube";block.Y=1;storm.Characters.push_back(block);
            effects.step(storm,.01,.11);hit=effects.contact(bolt.Id);
            auto box=object_bounds(block);
            require(hit && std::abs(hit->y-box.high.y)<.001,"Lightning terminates at the nearest solid object before terrain");
            RenderSceneCache effectCache;
            render_scene(storm,nullptr,effects.geometry(storm),&effectCache,true);
            require(render_scene(storm,nullptr,{},&effectCache,false)==render_scene(storm),"Leaving runtime effects restores authored lightning in cached scene packets");
            effects.reset();require(!effects.contact(bolt.Id) && effects.spark_count()==0,"Scene reset clears lightning contacts and particles");
            storm.Characters[0].LightningTargetId=block.Id;storm.Characters[0].TargetX=0;storm.Characters[0].TargetY=.5;storm.Characters[0].TargetZ=0;
            storm.Characters[1].X=1;effects.step(storm,.03,.14);auto contact=effects.contact(bolt.Id);require(contact&&contact->x>.4,"Lightning follows a referenced moving object and contacts its surface");
            auto saved=SceneData::decode(storm.encode());require(saved.Characters[0].LightningTargetId==block.Id,"Lightning target references persist through scene serialization");
            auto grouped=decode_object_group(encode_object_group(storm.Characters));require(grouped[0].LightningTargetId==grouped[1].Id&&grouped[1].Id!=block.Id,"Reusable lightning groups reconnect to their copied targets");
            auto offset=grouped[0].TargetX;SceneData placement;place_object_group(grouped,placement,4,3,false,false,false);require(grouped[0].TargetX==offset,"Moving a lightning group preserves target-local offsets");
            bool missingTargetRejected=false;try{encode_object_template(storm.Characters[0]);}catch(...){missingTargetRejected=true;note_rejection(__FILE__,__LINE__);}require(missingTargetRejected,"Reusable emitters cannot silently lose required target references");
            storm.Characters.pop_back();effects.step(storm,.03,.17);require(!effects.contact(bolt.Id),"Removing a lightning target stops the stale discharge");

        }
        {
            SceneData moving;moving.Terrain.emplace();moving.Terrain->WaterEnabled=true;moving.Terrain->WaterLevel=.1;
            Character fixed;fixed.Shape="grass";Character body;body.Shape="rock";body.Dynamic=true;body.Y=2;moving.Characters={fixed,body};
            auto liquidFixture=std::unique_ptr<TerrainFlow>(new TerrainFlow(*moving.Terrain,{}));auto& liquid=*liquidFixture;RenderSceneCache cache;
            auto first=render_scene(moving,&liquid,{},&cache);
            require(first==render_scene(moving,&liquid),"Cached static geometry with liquid and rigid body preserves the complete packet");
            moving.Characters[1].Y=1;
            auto second=render_scene(moving,&liquid,{},&cache);
            require(first!=second && second==render_scene(moving,&liquid),"Rigid-body motion updates without invalidating static terrain and grass");
        }
        {
            SceneData material;Character c;c.Shape="cube";c.Friction=.2;c.Restitution=.8;material.Characters={c};auto restored=SceneData::decode(material.encode());
            require(restored.Characters[0].Friction==.2&&restored.Characters[0].Restitution==.8,"Physical materials survive scene save and load");
            auto old=json::parse(material.encode());old["Characters"][0].erase("Friction");old["Characters"][0].erase("Restitution");auto defaults=SceneData::decode(old.dump());
            require(defaults.Characters[0].Friction==.72&&defaults.Characters[0].Restitution==.03,"Older scenes retain existing physical material defaults");
            bool rejected=false;try{c.Restitution=1.1;c.validate();}catch(...){rejected=true;note_rejection(__FILE__,__LINE__);}require(rejected,"Invalid bounce coefficients cannot enter a scene");
        }
        {
            TerrainData sloped;sloped.WaterEnabled=true;sloped.WaterLevel=2;
            for(int z=0;z<TerrainData::N;++z)for(int x=0;x<TerrainData::N;++x)sloped.Heights[z*TerrainData::N+x]=x*.01;
            auto water=std::make_unique<TerrainFlow>(sloped,std::vector<LiquidSource>{});bool flat=true;for(const auto& part:water->geometry())for(const auto& v:part.mesh.vertices)flat=flat&&std::abs(v.position.y-2.009)<1e-8;
            require(flat,"A level liquid surface remains level over sloping terrain without cell-edge spikes");
            sloped.Lava=true;auto lava=std::make_unique<TerrainFlow>(sloped,std::vector<LiquidSource>{});require(lava->snapshot().at(0,0).second==2800,"Shared liquid snapshots distinguish lava density from water");
        }
        #ifdef STARLIGHT_CHARACTER_CONTROLLER
        {
            SceneData moving;moving.Game.emplace();Character platform;platform.Shape="cube";platform.Width=platform.Depth=4;platform.Route={{10,0}};platform.Speed=2;moving.Characters={platform};AdventureController controller(moving,{0,2.7,0});Vec3 eye{0,2.7,0};
            for(int i=0;i<90;++i)eye=controller.step(eye,{},1./120.,false,i/120.,{});
            require(eye.x>.7&&std::abs(eye.y-2.7)<.15,"A moving platform carries a stationary character without slipping through it");
            auto takeoff=eye;eye=controller.step(eye,{},1./120.,true,90./120.,{});for(int i=91;i<121;++i)eye=controller.step(eye,{},1./120.,false,i/120.,{});
            require(eye.x>takeoff.x+.35&&eye.y>takeoff.y+.5,"Jumping retains platform horizontal momentum across airborne frames");
        }
        {
            TerrainData terrain;terrain.WaterEnabled=true;terrain.WaterLevel=2;auto liquidFlowFixture=std::unique_ptr<TerrainFlow>(new TerrainFlow(terrain,{}));auto& flow=*liquidFlowFixture;auto liquid=std::make_shared<const LiquidSnapshot>(flow.snapshot());
            require(liquid->at(0,0).second==1000&&liquid->at(20,0).second==0&&flow.liquid_at(20,0).second==0,"Liquid snapshots retain density and reject positions outside the simulated domain");
            auto floatHeight=[&](double density){SceneData scene;scene.Game.emplace();Character c;c.Shape="cube";c.Dynamic=true;c.X=3;c.Y=2;c.Density=density;scene.Characters={c};AdventurePlayer p;p.start(scene);p.liquids(liquid);for(int i=0;i<600;++i)p.move(0,0,1./120.);return p.scene().Characters.front().Y;};
            auto light=floatHeight(500),heavy=floatHeight(5000);
            require(light>1&&light<2.2&&heavy<.2,"Shared gameplay bodies float or sink according to liquid and body density");
            SceneData scene;scene.Game.emplace();AdventureController controller(scene,{0,1.7,0});controller.liquids(liquid);Character cube;cube.Shape="cube";auto piece=fracture_convex(shape_geometry(cube).front().mesh,{1,1,1}).front();auto id=controller.add_debris(piece.part->mesh,{3,1,0},{},500,.72,.03);Vec3 eye{0,1.7,0},at;std::array<double,4> rotation;
            for(int i=0;i<600;++i)eye=controller.step(eye,{},1./120.,false,i/120.,{});controller.debris_pose(id,at,rotation);
            require(at.y>1.5&&at.y<2.5,"Fracture bodies receive buoyancy in the same character world");controller.remove_debris(id);
            bool steppedAfterRemoval=true;try{for(int i=0;i<5;++i)eye=controller.step(eye,{},1./120.,false,5+i/120.,{});}catch(...){steppedAfterRemoval=false;note_rejection(__FILE__,__LINE__);}
            require(steppedAfterRemoval,"Removing floating debris clears liquid-force registrations (the liquid is still attached while stepping on)");
            controller.liquids(nullptr);
        }
        {
            SceneData level;level.Game.emplace();Character body;body.Shape="cube";body.Dynamic=true;body.X=3;body.Y=4;body.Size=.3;body.JointKind=1;level.Characters={body};
            AdventurePlayer player;player.start(level);for(int i=0;i<240;++i)player.move(0,0,1./120.);
            require(std::abs(player.scene().Characters[0].Y-4)<.02,"Fixed world joints hold a body against gravity");
            level.Characters[0].JointKind=3;level.Characters[0].JointTargetY=2;level.Characters[0].JointLength=2;
            player.start(level);for(int i=0;i<240;++i)player.move(0,0,1./120.);
            require(std::abs(player.scene().Characters[0].Y-4)<.05,"Rope joints enforce a maximum distance to the world anchor");
            level.Characters[0].JointKind=4;level.Characters[0].JointLength=1;
            player.start(level);for(int i=0;i<480;++i)player.move(0,0,1./120.);
            require(player.scene().Characters[0].Y>4.7&&player.scene().Characters[0].Y<5.1,"Spring joints converge toward their rest length under gravity");
            level.Characters[0].JointKind=2;level.Characters[0].JointY=1;level.Characters[0].JointX=1;level.Characters[0].JointAxisY=0;level.Characters[0].JointAxisZ=1;
            player.start(level);for(int i=0;i<180;++i)player.move(0,0,1./120.);
            auto pivot=world_point(player.scene().Characters[0],{1,1,0});auto original=world_point(level.Characters[0],{1,1,0});
            require((pivot-original).length()<.03,"Hinges preserve their pivot as an offset body swings");
            level.Characters[0].JointAngleMin=level.Characters[0].JointAngleMax=0;player.start(level);for(int i=0;i<180;++i)player.move(0,0,1./120.);
            require(std::abs(player.scene().Characters[0].X-level.Characters[0].X)<.03&&std::abs(player.scene().Characters[0].Y-level.Characters[0].Y)<.03,"Zero-width hinge limits stop gravity-driven rotation at the authored angle");
            level.Characters[0].JointAngleMin=-180;level.Characters[0].JointAngleMax=180;
            level.Characters[0].JointKind=5;player.start(level);for(int i=0;i<180;++i)player.move(0,0,1./120.);
            require((world_point(player.scene().Characters[0],{1,1,0})-original).length()<.03,"Ball joints preserve their pivot while allowing free rotation");
            level.Characters[0].JointKind=6;level.Characters[0].JointAxisY=1;level.Characters[0].JointAxisZ=0;level.Characters[0].JointLength=1;
            player.start(level);for(int i=0;i<240;++i)player.move(0,0,1./120.);
            require(std::abs(player.scene().Characters[0].Y-3)<.04&&std::abs(player.scene().Characters[0].X-3)<.01,"Slider joints constrain translation to their axis and travel limit");
            Character target;target.Shape="cube";target.X=3;target.Y=6;target.Size=.2;
            level.Characters[0].JointKind=3;level.Characters[0].JointX=level.Characters[0].JointY=0;level.Characters[0].JointTargetY=0;level.Characters[0].JointTargetId=target.Id;level.Characters[0].JointLength=2;level.Characters.push_back(target);
            auto restored=SceneData::decode(level.encode());require(restored.Characters[0].JointTargetId==target.Id&&restored.Characters[0].JointKind==3,"Joint settings and references survive save and reload");
            auto copies=decode_object_group(encode_object_group(level.Characters));require(copies[0].JointTargetId==copies[1].Id,"Copied body groups reconnect joints to copied targets");
            AdventureController controller(level,{0,1.7,0});Vec3 eye{0,1.7,0};for(int i=0;i<120;++i)eye=controller.step(eye,{},1./120.,false,i/120.,{});
            controller.sync_dynamic(level);double held=level.Characters[0].Y;
            for(int i=0;i<120;++i)eye=controller.step(eye,{},1./120.,false,1+i/120.,{target.Id});controller.sync_dynamic(level);
            require(held>3.9&&level.Characters[0].Y<2,"Removing a joint target detaches the body without a stale constraint");
            bool invalid=false;try{auto c=body;c.JointTargetId=c.Id;c.validate();}catch(...){invalid=true;note_rejection(__FILE__,__LINE__);}require(invalid,"Self-referencing joints are rejected");
        }
        {
            TerrainData currentTerrain;auto movingWater=std::make_unique<TerrainFlow>(currentTerrain,std::vector<LiquidSource>{});movingWater->water.fill(1);for(int z=0;z<48;++z)for(int x=0;x<24;++x)movingWater->water[z*48+x]=2;double waterVolume=movingWater->volume();movingWater->step(.025);auto current=movingWater->velocity_at(-.25,.25);auto currentSnapshot=std::make_shared<LiquidSnapshot>(movingWater->snapshot());require(current.x>0&&std::abs(current.z)<1e-8&&std::abs(movingWater->volume()-waterVolume)<1e-8,"Liquid transport publishes conservative currents from high water toward low water");require((currentSnapshot->velocity_at(-.25,.25)-current).length()<1e-8&&currentSnapshot->velocity_at(13,0).length()==0,"Immutable liquid snapshots preserve current samples and reject dry world boundaries");
            SceneData drivenScene;drivenScene.Game.emplace();Character driven;driven.Shape="cube";driven.Dynamic=true;driven.Size=.5;driven.Y=4;driven.X=3;driven.CombatHealth=100;drivenScene.Characters={driven};AdventureController drivenController(drivenScene,{0,1.7,0});Vec3 drivenEye{0,1.7,0};for(int i=0;i<180;++i){drivenController.sync_dynamic(drivenScene);auto c=drivenScene.Characters[0];drivenController.move_actor(c.Id,{c.X-.5/120,c.Y,c.Z},1./120.);drivenEye=drivenController.step(drivenEye,{},1./120.,false,i/120.,{});}drivenController.sync_dynamic(drivenScene);require(drivenScene.Characters[0].Y<.2&&drivenScene.Characters[0].X<2.6,"Driving a dynamic combat body preserves gravity instead of switching it to kinematic mode");
            SceneData driftScene;driftScene.Game.emplace();Character floating;floating.Shape="cube";floating.Dynamic=true;floating.Size=.4;floating.Density=500;floating.X=3;floating.Y=1.8;driftScene.Characters={floating};AdventureController driftController(driftScene,{0,1.7,0});for(auto& cell:currentSnapshot->cells)cell={2.5,1000};for(auto& velocity:currentSnapshot->currents)velocity={1,0,0};driftController.liquids(currentSnapshot);Vec3 driftEye{0,1.7,0};for(int i=0;i<240;++i)driftEye=driftController.step(driftEye,{},1./120.,false,i/120.,{});driftController.sync_dynamic(driftScene);require(driftScene.Characters[0].X>3.4,"Shared-world floating props are carried horizontally by the simulated liquid current");require(driftEye.x>.4,"Liquid currents carry an immersed player without movement input");
            TerrainData terrain;terrain.WaterEnabled=true;terrain.WaterLevel=2;auto flow=std::make_unique<TerrainFlow>(terrain,std::vector<LiquidSource>{});double volume=flow->volume();
            flow->impulse(0,0,1);double peak=flow->wave_energy();for(int i=0;i<30;++i)flow->step(1./120.);
            require(std::abs(flow->liquid_at(.5,0).first-2)>1e-5,"Liquid impulses propagate to neighboring surface cells");
            for(int i=0;i<1200;++i)flow->step(1./120.);
            require(flow->wave_energy()<peak*.001&&std::abs(flow->volume()-volume)<1e-8,"Surface waves damp without creating or deleting fluid volume");
            Character falling;falling.Shape="cube";falling.Dynamic=true;falling.Y=2.1;flow->interact(std::vector<Character>{falling},.025,0);falling.Y=1.9;flow->interact(std::vector<Character>{falling},.025,.025);
            require(!flow->splashes().empty()&&flow->wave_energy()>0,"A body crossing the water surface generates waves and a splash");
            auto risingWater=std::make_unique<TerrainFlow>(terrain,std::vector<LiquidSource>{});falling.Y=2.1;risingWater->interact(std::vector<Character>{falling},.025,0);risingWater->water.fill(2.3);falling.Y=1.9;risingWater->interact(std::vector<Character>{falling},.025,.025);require(!risingWater->splashes().empty(),"Falling-body contact remains detectable when displaced water rises between samples");
            EnvironmentEffects effects;for(const auto& event:flow->splashes())effects.splash(event);require(effects.splash_count()>0,"Water impacts create independently moving droplets");
            SceneData waterScene;waterScene.Terrain=terrain;for(int i=0;i<200;++i)effects.step(waterScene,.01,i*.01,flow.get());require(effects.splash_count()==0,"Droplets reenter liquid or expire without accumulating");
        }
        {
            SceneData storm;storm.Game.emplace();Character bolt;bolt.Shape="chaos-lightning";bolt.X=3;bolt.Y=3;bolt.TargetX=0;bolt.TargetY=-3;bolt.TargetZ=0;bolt.LightningDamage=7;bolt.LightningChains=1;bolt.BranchSpread=0;bolt.LightningChainRadius=3;bolt.CombatTeam=1;
            Character target;target.Shape="cube";target.X=3;target.CombatHealth=100;target.CombatTeam=2;Character chained=target;chained.Id=new_id();chained.X=5;storm.Characters={bolt,target,chained};
            auto path=trace_lightning(bolt,storm,0);require(path.size()==2&&path[0].hit==target.Id&&path[1].hit==chained.Id,"Lightning chains to another body without revisiting its first contact");
            AdventurePlayer player;player.start(storm);for(int i=0;i<6;++i)player.move(0,0,.01);
            require(player.combat_actors().at(target.Id).health==93&&player.combat_actors().at(chained.Id).health==93,"A lightning discharge damages both contacted hostile bodies once");
            require(player.presentation().lightning==1,"Lightning audio receives one event per discharge rather than one per frame");
            for(int i=0;i<10;++i)player.move(0,0,.01);require(player.combat_actors().at(target.Id).health==93,"Lightning damage is not multiplied by render frame rate");
            auto restored=SceneData::decode(storm.encode());require(restored.Characters[0].LightningDamage==7&&restored.Characters[0].LightningChains==1,"Lightning damage and chain settings survive serialization");
            player.start(storm);require(player.presentation().lightning==0,"Restart clears lightning discharge and audio history");
        }
        {
            SceneData fire;Character emitter;emitter.Shape="fire";emitter.ParticleRate=0;emitter.ParticleLifetime=2;emitter.ParticleWindX=3;fire.Characters={emitter};EnvironmentEffects effects;
            for(int i=0;i<20;++i)effects.step(fire,.025,i*.025);require(effects.flame_count()==0,"Disabled particle emission produces no particles");
            fire.Characters[0].ParticleRate=100;for(int i=0;i<20;++i)effects.step(fire,.025,.5+i*.025);require(effects.flame_count()==50,"Authored particle emission rate is independent of frame count");
            auto saved=SceneData::decode(fire.encode());require(saved.Characters[0].ParticleWindX==3&&saved.Characters[0].ParticleLifetime==2&&saved.Characters[0].ParticleRate==100,"Particle forces and lifetime survive save and reload");
            fire.Characters.clear();for(int i=0;i<120;++i)effects.step(fire,.025,1+i*.025);require(effects.flame_count()==0,"Removing an emitter lets its independent particles expire");
        }
        {
            SceneData level;level.Game.emplace();Character body;body.Shape="cube";body.Dynamic=true;body.Density=50;body.X=3;body.Y=2;level.Characters={body};AdventureController controller(level,{0,1.7,0});
            controller.impulse(body.Id,{3.4,2.8,0},{400,0,0});Vec3 eye{0,1.7,0};for(int i=0;i<60;++i)eye=controller.step(eye,{},1./120.,false,i/120.,{});controller.sync_dynamic(level);auto& moved=level.Characters[0];
            require(moved.X>3.2&&moved.PhysicsRotation&&std::abs((*moved.PhysicsRotation)[2])>.01,"Off-center impacts transfer linear and angular momentum to physical props");
            level.Game->ProjectileMass=.1;auto saved=SceneData::decode(level.encode());require(saved.Game->ProjectileMass==.1,"Projectile mass survives scene save and reload");
        }
        {
            SceneData storm;storm.Game.emplace();storm.Game->SpawnZ=0;Character bolt;bolt.Shape="chaos-lightning";bolt.Y=3;bolt.TargetX=0;bolt.TargetY=-3;bolt.TargetZ=0;bolt.LightningDamage=9;bolt.CombatTeam=2;storm.Characters={bolt};AdventurePlayer player;player.start(storm);
            auto path=trace_lightning(bolt,storm,0,player.body_bounds());require(path.front().hit==lightning_player_id&&path.front().target.y>1.7,"Lightning intersects the player's collision body before reaching the ground");
            for(int i=0;i<6;++i)player.move(0,0,.01);require(player.health()==91,"Hostile lightning applies one discharge of damage to the player");
            storm.Characters[0].CombatTeam=1;player.start(storm);for(int i=0;i<6;++i)player.move(0,0,.01);require(player.health()==100,"Friendly lightning preserves player team immunity");
        }
        {
            auto swim=[](double dt){SceneData level;level.Game.emplace();AdventureController controller(level,{0,1.7,0});auto water=std::make_shared<LiquidSnapshot>();for(auto& c:water->cells)c={2.5,1000};controller.liquids(water);Vec3 eye{0,1.7,0};for(int i=0;i<int(12/dt);++i)eye=controller.step(eye,{},dt,false,i*dt,{});return eye;};
            auto slow=swim(1./30.),fast=swim(1./240.);require(slow.y>2.35&&slow.y<2.65,"Submerged character rises and settles with its head above the water surface");
            require((slow-fast).length()<.04,"Character buoyancy remains consistent across render rates");
            SceneData level;level.Game.emplace();AdventureController controller(level,{0,2.5,0});auto water=std::make_shared<LiquidSnapshot>();for(auto& c:water->cells)c={2.5,1000};controller.liquids(water);Vec3 eye{0,2.5,0};eye=controller.step(eye,{},.025,true,0,{});require(eye.y>2.53,"Jump input provides upward swimming motion while immersed");
            for(auto& cell:water->cells)cell={-INFINITY,0};eye=controller.step(eye,{},.025,false,.025,{});require(std::isfinite(eye.y),"Leaving a liquid through a dry snapshot cell restores gravity without an invalid height");
            controller.liquids({});for(int i=0;i<300;++i)eye=controller.step(eye,{},1./120.,false,i/120.,{});require(eye.y<1.75,"Leaving liquid restores normal gravity and ground contact");
        }
        {
            SceneData level;level.Game.emplace();Character body;body.Shape="cube";body.Dynamic=true;body.X=3;body.Y=10;body.VelocityX=2;body.AngularVelocityY=90;body.JointAngleMin=-30;body.JointAngleMax=60;level.Characters={body};
            auto restored=SceneData::decode(level.encode());require(restored.Characters[0].VelocityX==2&&restored.Characters[0].AngularVelocityY==90&&restored.Characters[0].JointAngleMin==-30&&restored.Characters[0].JointAngleMax==60,"Authored launch velocity, spin and hinge limits survive save and reload");
            AdventurePlayer player;player.start(level);for(int i=0;i<30;++i)player.move(0,0,1./120.);auto moved=player.scene().Characters[0];require(moved.X>3.45&&moved.PhysicsRotation&&std::abs((*moved.PhysicsRotation)[1])>.15,"Gameplay dynamic bodies begin with authored linear and angular velocity");
            player.start(level);for(int i=0;i<30;++i)player.move(0,0,1./120.);require(std::abs(player.scene().Characters[0].X-moved.X)<.001,"Restart restores authored body momentum without carrying runtime velocity");
        }
        {
            SceneData level;level.Game.emplace();Character weight;weight.Shape="cube";weight.Dynamic=true;weight.Size=.2;weight.Density=1000;weight.X=3;weight.Y=3;weight.JointKind=3;weight.JointY=1;weight.JointTargetY=2.2;weight.JointLength=2;weight.JointSegments=12;level.Characters={weight};
            AdventurePlayer player;player.start(level);for(int i=0;i<360;++i)player.move(0,0,1./120.);auto body=player.scene().Characters[0];require(body.JointRuntimePoints.size()==13,"Segmented rope publishes every physical link endpoint");
            double length=0;for(size_t i=1;i<body.JointRuntimePoints.size();++i){auto a=body.JointRuntimePoints[i-1],b=body.JointRuntimePoints[i];length+=Vec3{a[0]-b[0],a[1]-b[1],a[2]-b[2]}.length();}require(length>1.95&&length<2.08&&body.Y>2.9,"Segmented rope supports its load without stretching beyond solver tolerance");
            auto saved=SceneData::decode(level.encode());require(saved.Characters[0].JointSegments==12&&saved.Characters[0].JointRuntimePoints.empty(),"Rope settings persist while simulated link positions remain runtime-only");
            Character anchor;anchor.Shape="cube";anchor.X=3;anchor.Y=5.2;anchor.Size=.1;level.Characters.push_back(anchor);level.Characters[0].JointTargetId=anchor.Id;level.Characters[0].JointTargetY=0;AdventureController controller(level,{0,1.7,0});Vec3 eye{0,1.7,0};for(int i=0;i<30;++i)eye=controller.step(eye,{},1./120.,false,i/120.,{});for(int i=0;i<180;++i)eye=controller.step(eye,{},1./120.,false,i/120.,{anchor.Id});controller.sync_dynamic(level);require(level.Characters[0].Y<1&&level.Characters[0].JointRuntimePoints.empty(),"Removing a rope anchor releases the load and removes its physical links");
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;Character shooter;shooter.Shape="cube";shooter.Z=-6;shooter.RotationY=180;shooter.CombatHealth=100;shooter.CombatTeam=2;shooter.CombatAutoAttack=true;shooter.CombatProjectileSpeed=4;level.Characters={shooter};AdventurePlayer player;player.start(level);bool flying=false,delayed=false;for(int i=0;i<600;++i){player.move(0,0,1./120.);if(!flying&&!player.presentation().projectiles.empty()){flying=true;delayed=player.health()==100;}}require(flying&&delayed,"Enemy attacks launch traveling projectiles before player damage is applied");require(player.health()<100,"Enemy projectile impacts damage the contacted player");
            auto saved=SceneData::decode(level.encode());require(saved.Characters[0].CombatProjectileSpeed==4,"Actor projectile settings survive scene save and reload");
            ProjectilePhysics shots;shots.fire({0,1,0},{1,0,0},100,0,"owner",2,.1,3,.75);std::vector<ProjectileTarget> targets{{"owner",{{-.5,0,-.5},{.5,2,.5}},{{-.5,0,-.5},{.5,2,.5}}},{"victim",{{1,0,-.5},{2,2,.5}},{{1,0,-.5},{2,2,.5}}}};auto hits=shots.step(.02,nullptr,targets);require(hits.size()==1&&hits[0].target=="victim"&&hits[0].team==2&&hits[0].mass==.1&&hits[0].control==3&&hits[0].accuracy==.75,"Projectiles ignore their shooter and retain team, momentum and control metadata at impact");
        }
        {
            auto right=spatial_sound({10,0,0},{},{0,0,-1},{0,1,0}),left=spatial_sound({-10,0,0},{},{0,0,-1},{0,1,0}),distantSound=spatial_sound({343,0,0},{},{0,0,-1},{0,1,0});require(right.pan>.99&&left.pan<-.99&&right.gain==.5&&std::abs(distantSound.delay-1)<1e-9&&distantSound.gain<right.gain,"Spatial thunder carries direction, distance attenuation and sound travel delay");
            std::array<short,3> mono{1000,-1000,0};auto stereo=stereo_sound(mono,1,1);require(stereo.size()==6&&stereo[0]==0&&stereo[1]==1000&&stereo[3]==-1000,"Stereo panning preserves signed audio samples in the intended channel");
            SceneData storm;storm.Game.emplace();Character bolt;bolt.Shape="chaos-lightning";bolt.X=8;bolt.TargetX=0;bolt.TargetY=0;storm.Characters={bolt};AdventurePlayer player;player.start(storm);for(int i=0;i<6;++i)player.move(0,0,.01);auto p=player.presentation();require(p.lightningSounds.size()==1&&p.lightningSounds[0].serial==p.lightning&&p.lightningSounds[0].position.x==8,"Lightning records one spatial sound event per physical discharge");player.start(storm);require(player.presentation().lightningSounds.empty(),"Scene restart clears pending lightning sound history");
        }
        {
            SceneData level;level.Game.emplace();Character model;model.Asset=import_model(solid_glb_fixture(folder));model.Dynamic=true;model.X=3;model.Y=3;model.VelocityX=1;level.Characters={model};level.validate();AdventurePlayer player;player.start(level);for(int i=0;i<240;++i)player.move(0,0,1./120.);auto moved=player.scene().Characters[0];require(moved.Y<.2&&moved.X>3.5&&moved.PhysicsRotation.has_value(),"Imported solid models participate in shared gameplay rigid-body physics");
            auto sculpt=model;sculpt.Asset.clear();Character cube;cube.Shape="cube";sculpt.Sculpt=sculpt_from(object_mesh(cube));sculpt.validate();require(!sculpt.Shape&&sculpt.Dynamic,"Standalone sculpt geometry can be authored as a dynamic body without a primitive shape tag");
        }
        {
            auto concave=concave_solid_fixture();require(!closed_convex_mesh(concave),"Concave L-shaped solid is not mislabeled as convex");auto pieces=fracture_mesh(concave,{.4,.5,.6});double total=0;bool convex=true;for(const auto& piece:pieces){convex&=closed_convex_mesh(piece.part->mesh);double v=0;for(size_t i=0;i<piece.part->mesh.indices.size();i+=3){auto& m=piece.part->mesh;auto a=m.vertices[m.indices[i]].position,b=m.vertices[m.indices[i+1]].position,c=m.vertices[m.indices[i+2]].position;v+=a.dot(b.cross(c))/6.;}total+=std::abs(v);}require(pieces.size()==20&&convex&&std::abs(total-3)<1e-6,"Concave star-shaped fracture produces closed convex pieces conserving source volume");
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;Character target;target.Sculpt=sculpt_from(concave);target.GameplayRole="breakable";target.X=-.5;target.Z=-4;level.Characters={target};AdventurePlayer player;player.start(level);player.fire_button(true);player.fire_button(false);for(int i=0;i<360;++i)player.move(0,0,1./120.);require(player.scene().Characters.empty()&&player.presentation().fragments.size()==20,"Concave sculpt destruction creates physical source-shaped pieces in gameplay");
            concave.indices.resize(concave.indices.size()-3);require(fracture_star_shaped(concave,{1,1,1}).empty(),"Open surfaces are rejected by solid tetrahedral fracture");
        }
        {
            Character routed;routed.Shape="cube";routed.Width=3;routed.Depth=1;routed.Route={{4,0}};routed.Speed=1;auto posed=route_pose(routed,1);auto bounds=object_bounds(posed);require(std::abs(posed.X-1)<1e-9&&std::abs(bounds.high.x-bounds.low.x-1)<1e-9&&std::abs(bounds.high.z-bounds.low.z-3)<1e-9,"Routed collision and effect bounds use the same heading as visible geometry");require(routed.X==0&&routed.Route.size()==1&&!routed.PhysicsRotation,"Sampling a routed pose preserves authored route data");
            Mesh panel;for(auto p:{Vec3{-1,0,-1},Vec3{1,0,-1},Vec3{1,0,1},Vec3{-1,0,1}})panel.vertices.push_back({p});panel.indices={0,2,1,0,3,2};auto shards=fracture_mesh(panel,{.2,.4,.8});require(shards.size()==2,"Open surface panels create source-shaped physical shards");for(const auto& shard:shards)require(closed_convex_mesh(shard.part->mesh)&&shard.radius>0,"Thin panel shards acquire closed collision thickness");
            auto ring=torus_fixture();auto shells=fracture_surface(ring,{.5,.6,.7},false);require(shells.size()>1&&shells.size()<=64,"Closed meshes with holes retain source-shaped physical shell fragments");size_t shellTriangles=0;bool opening=true;for(const auto& shard:shells){shellTriangles+=shard.part->mesh.indices.size()/3;for(const auto& vertex:shard.part->mesh.vertices){auto at=vertex.position+shard.center;opening&=std::hypot(at.x,at.z)>1.45;}}require(opening&&shellTriangles==ring.indices.size()/3*8,"Hollow fracture preserves the visible opening and every thickened source face");auto hollowPieces=fracture_mesh(ring,{.5,.6,.7});require(hollowPieces.size()>1,"Automatic fracture supports a closed object with a through-hole");
            auto uShape=u_solid_fixture();require(fracture_star_shaped(uShape,{1,1,1}).empty(),"U-shaped source has no single visibility kernel");auto uPieces=fracture_mesh(uShape,{1,1,1});require(uPieces.size()>1,"Non-star-shaped concave objects decompose into physical fragments");double uVolume=0;for(const auto& piece:uPieces){require(closed_convex_mesh(piece.part->mesh),"Concave split produces watertight convex collision fragments");auto& m=piece.part->mesh;double volume=0;for(size_t i=0;i<m.indices.size();i+=3)volume+=m.vertices[m.indices[i]].position.dot(m.vertices[m.indices[i+1]].position.cross(m.vertices[m.indices[i+2]].position))/6.;uVolume+=std::abs(volume);}require(std::abs(uVolume-7)<1e-7,"Concave fracture preserves solid volume without filling the U-shaped opening");
            SceneData meadow;Character grass;grass.Shape="grass";meadow.Characters={grass};EnvironmentEffects plants;plants.step(meadow,.025,.025);auto original=plants.geometry(meadow);Bounds contact{{-.6,0,-.6},{.6,1,.6}};
            for(int i=0;i<40;++i)plants.step(meadow,.025,(i+2)*.025,nullptr,contact);auto pressed=plants.geometry(meadow);bool roots=true,bent=false;for(size_t i=0;i<original[0].mesh.vertices.size();++i){auto a=original[0].mesh.vertices[i].position,b=pressed[0].mesh.vertices[i].position;if(a.y<1e-9)roots&=(a-b).length()<1e-9;else bent|=(a-b).length()>.03;}require(roots&&bent,"Grass bends around contacting bodies while its roots stay fixed");
            auto before=pressed[0].mesh.vertices.back().position;for(int i=0;i<80;++i)plants.step(meadow,.025,1.05);auto recovered=plants.geometry(meadow);require((recovered[0].mesh.vertices.back().position-before).length()>.001,"Grass releases contact deformation and recovers under wind");plants.reset();require(plants.geometry(meadow).empty(),"Scene reset clears plant deformation state");
            PhysicalParticle coarse,fine;coarse.position=fine.position={0,20,0};coarse.velocity=fine.velocity={2,1,0};coarse.drag=fine.drag=.6;
            for(int i=0;i<30;++i)step_particle(coarse,1./30,nullptr,{}, {.3,0,.15},{0,4.19,0});
            for(int i=0;i<240;++i)step_particle(fine,1./240,nullptr,{}, {.3,0,.15},{0,4.19,0});
            require((coarse.position-fine.position).length()<1e-8&&(coarse.velocity-fine.velocity).length()<1e-8,"Particle lift and wind integrate consistently at 30 and 240 updates per second");
            auto liquid=std::make_unique<TerrainFlow>(TerrainData{},std::vector<LiquidSource>{});liquid->water.fill(1);liquid->water[TerrainFlow::index(.25,.25)]=2;
            auto left=liquid->liquid_at(-.00001,.25),right=liquid->liquid_at(.00001,.25);require(std::abs(left.first-right.first)<.0001,"Buoyancy surface stays continuous across wet cell boundaries");
            SceneData burning;Character burner;burner.Shape="fire";burner.ParticleRate=100;burning.Characters={burner};EnvironmentEffects burningEffects;
            for(int i=0;i<10;++i)burningEffects.step(burning,.025,i*.025);require(burningEffects.flame_count()>0,"Dry fire emits physical flames before flooding");
            burningEffects.step(burning,.025,.275,liquid.get());require(burningEffects.flame_count()==0,"Flooding extinguishes existing flames and submerged fire emission");
            auto submergedFire=burningEffects.geometry(burning);require(std::none_of(submergedFire.begin(),submergedFire.end(),[](const auto& part){return !part.mesh.vertices.empty()&&part.mesh.vertices.front().kind==12;}),"Flooding also extinguishes the volumetric fire core");
            for(int i=0;i<40;++i)burningEffects.step(burning,.025,.3+i*.025,liquid.get());require(burningEffects.flame_count()==0,"Submerged emitters do not accumulate deferred bursts");
            liquid->water.fill(0);liquid->lava.fill(1);burningEffects.step(burning,.025,1.325,liquid.get());require(burningEffects.flame_count()>0&&burningEffects.flame_count()<=3,"Lava permits fire without releasing a stored underwater burst");
            auto relitFire=burningEffects.geometry(burning);require(std::any_of(relitFire.begin(),relitFire.end(),[](const auto& part){return !part.mesh.vertices.empty()&&part.mesh.vertices.front().kind==12;}),"Volumetric fire resumes when an emitter is no longer submerged");
            burningEffects.reset();require(burningEffects.flame_count()==0,"Reset clears liquid-reactive fire state");liquid->lava.fill(0);liquid->water.fill(1);liquid->water[TerrainFlow::index(.25,.25)]=2;
            require(std::abs(atmosphere_surface(nullptr,liquid.get(),.1,.4)-liquid->liquid_at(.1,.4).first)<1e-10,"Fog air boundary follows the actual liquid surface");
            require(atmosphere_surface(nullptr,liquid.get(),20,0)==0,"Fog outside the liquid domain retains its ground boundary");
            SceneData plume;Character source;source.Shape="craxis-smoke";source.Route={{4,0}};source.Speed=1;plume.Characters={source};auto startSources=smoke_sources(plume,0,nullptr),movedSources=smoke_sources(plume,2,nullptr);require(movedSources.size()==1&&movedSources[0][0]>startSources[0][0]+1.9,"Transported smoke emission follows moving authored sources");
            auto chaosSources=fog_emitters(plume,2,nullptr);require(chaosSources.size()==1&&chaosSources[0][4]==1&&chaosSources[0][0]==movedSources[0][0],"Chaos fog retains its identity while following the authored route");
            SceneData cloudScene;cloudScene.Name="Layered animated chaos clouds";cloudScene.FogDensity=0;cloudScene.Hour=19;source.Route.clear();source.Speed=1;source.Y=2;source.Size=2;source.Id="cloud-volume";cloudScene.Characters={source};
            Character behind;behind.Shape="cube";behind.Id="cloud-backdrop";behind.X=0;behind.Y=.5;behind.Z=-4;behind.Width=4;behind.Height=3;behind.Color="#939393";cloudScene.Characters.push_back(behind);
            auto saveCloud=[&](const char* name,const SceneData& scene){auto packet=render_scene(scene);atomic_write(folder/(std::string(name)+".bin"),std::string(reinterpret_cast<const char*>(packet.data()),packet.size()));atomic_write(folder/(std::string(name)+".scene.json"),scene.encode());};
            saveCloud("chaos-clouds",cloudScene);auto clearCloud=cloudScene;clearCloud.Characters.erase(clearCloud.Characters.begin());saveCloud("chaos-clouds-empty",clearCloud);
            Character wall=behind;wall.Id="cloud-front-wall";wall.Y=0;wall.Z=2.8;wall.Width=8;wall.Height=7;wall.Depth=.3;auto hiddenCloud=cloudScene;hiddenCloud.Characters.push_back(wall);saveCloud("chaos-clouds-occluded",hiddenCloud);
            plume.Characters=burning.Characters;require(smoke_sources(plume,0,nullptr).size()==1&&smoke_sources(plume,0,liquid.get()).empty(),"Fire smoke emits in air and stops when water extinguishes its source");
            auto sootSources=fog_emitters(plume,0,nullptr);require(sootSources.size()==1&&sootSources[0][4]==0&&fog_emitters(plume,0,liquid.get()).empty(),"Ordinary fire smoke occupies a distinct channel and obeys extinction");
            plume.Characters[0].Route={{4,0}};plume.Characters[0].Speed=1;auto movingLights=environment_lights(plume,2,nullptr);require(movingLights.size()==1&&movingLights[0][0]>1.9&&environment_lights(plume,2,liquid.get()).empty(),"Fire lighting follows the moving emitter and extinguishes underwater");
            auto windWater=std::make_unique<TerrainFlow>(TerrainData{},std::vector<LiquidSource>{});windWater->water.fill(1);auto waterVolume=windWater->volume();windWater->wind(2,1);for(int i=0;i<240;++i)windWater->step(1./120.);double windEnergy=windWater->wave_energy();require(windEnergy>1e-5&&std::abs(windWater->liquid_at(1,2).first-1)>1e-5&&std::abs(windWater->volume()-waterVolume)<1e-8,"Atmospheric wind excites the simulated water surface without changing fluid volume");windWater->wind(0,0);for(int i=0;i<1200;++i)windWater->step(1./120.);require(windWater->wave_energy()<windEnergy*.01,"Wind-driven waves dissipate after the forcing stops");
            auto snapshot=liquid->snapshot();require(std::abs(snapshot.at(.1,.4).first-liquid->liquid_at(.1,.4).first)<1e-10,"Shared gameplay and standalone liquid sampling agree between grid points");
            liquid->water[TerrainFlow::index(.75,.25)]=0;require(liquid->liquid_at(.75,.25).second==0,"Surface interpolation does not turn dry cells into liquid");
            SceneData level;Character bolt;bolt.Shape="chaos-lightning";bolt.Y=3;bolt.TargetX=0;bolt.TargetY=-3;bolt.BranchSpread=2;level.Characters={bolt};auto arcs=trace_lightning(bolt,level,.06);require(arcs.size()==4&&arcs[1].branch&&arcs[2].branch&&arcs[3].branch,"Lightning emits independently traced physical branch paths");bool bounded=true;for(const auto& arc:arcs)for(auto point:arc.path)bounded&=point.y>=-1e-5;require(bounded&&arcs[0].contact,"Lightning main and branch paths terminate at terrain instead of crossing it");
            EnvironmentEffects stormEffects;stormEffects.step(level,.03,.03);require(stormEffects.lightning_sounds().size()==1&&stormEffects.lightning_sounds()[0].time==.03,"Ordinary environment playback publishes one timed thunder event");stormEffects.step(level,.02,.05);require(stormEffects.lightning_sounds().size()==1,"Environment thunder is not resubmitted every rendered frame");auto epoch=stormEffects.epoch();stormEffects.reset();require(stormEffects.lightning_sounds().empty()&&stormEffects.epoch()!=epoch,"Environment reset clears pending thunder and changes its audio epoch");
            auto engulfed=level;Character enclosure;enclosure.Shape="cube";enclosure.Y=5;enclosure.Height=2;enclosure.Width=enclosure.Depth=2;engulfed.Characters.push_back(enclosure);auto trapped=trace_lightning(bolt,engulfed,.06);require(trapped.front().contact&&trapped.front().hit==enclosure.Id&&(trapped.front().target-trapped.front().source).length()<1e-8,"Lightning beginning inside a moving solid terminates immediately on that body");
            for(int seed=0;seed<256;++seed){auto probe=bolt;probe.Id="terrain-regression-"+std::to_string(seed);auto paths=trace_lightning(probe,level,.06);bool above=true;for(const auto& path:paths)for(auto point:path.path)above&=point.y>=-1e-5;require(above,("Lightning terrain contact stays above ground for deterministic seed "+std::to_string(seed)).c_str());}
            auto next=trace_lightning(bolt,level,.28);require((arcs[0].path[1]-next[0].path[1]).length()>.001,"Successive discharges change their physical path topology");
        }
        {
            auto rebound=[](double bounce){SceneData level;level.Game.emplace();Character c;c.Shape="cube";c.Dynamic=true;c.X=3;c.Y=3;c.Restitution=bounce;level.Characters={c};AdventurePlayer p;p.start(level);double peak=-100;for(int i=0;i<240;++i){p.move(0,0,1./120.);if(i>=100)peak=std::max(peak,p.scene().Characters.front().Y);}return peak;};
            auto soft=rebound(0),elastic=rebound(.9);
            require(elastic>soft+.5,"Authored restitution changes actual rigid-body rebound height");
        }
        {
            SceneData level;level.Game.emplace();level.Game->SpawnZ=0;
            Character prop;prop.Shape="cube";prop.Dynamic=true;prop.X=2;prop.Y=3;prop.Density=100;prop.Width=prop.Height=prop.Depth=.4;level.Characters={prop};
            auto authored=level.encode();AdventurePlayer player;player.start(level);
            for(int i=0;i<180;++i)player.move(0,0,1./120.);
            require(player.scene().Characters.front().Y<.5 && !player.dynamic_objects().empty() && level.encode()==authored,"Gameplay dynamic props share the character world, fall and preserve authored state");
            level.Characters.front().X=0;level.Characters.front().Y=0;level.Characters.front().Z=-1;
            level.Characters.front().Height=1;level.Characters.front().Density=50;
            player.start(level);for(int i=0;i<180;++i)player.move(0,1,1./120.);
            if(player.scene().Characters.front().Z>=-1.1)std::cerr<<"Push diagnostic: body "<<player.scene().Characters.front().Z<<" player "<<player.position.z<<"\n";
            require(player.scene().Characters.front().Z<-1.1,"Character contact pushes a lightweight dynamic prop in the shared collision world");
        }
        #endif
        RenderSceneCache geometryCache;
        auto overlayParts=shape_geometry(rock);
        require(render_scene(render,nullptr,overlayParts,&geometryCache)==render_scene(render,nullptr,overlayParts),
                "Cached static scene plus dynamic effects matches complete render packet");
        overlayParts.front().mesh.vertices.front().position.x+=.75;
        require(render_scene(render,nullptr,overlayParts,&geometryCache)==render_scene(render,nullptr,overlayParts),
                "Changing effects preserve cached scene and identical geometry");
        render.Characters.front().X+=2;
        geometryCache.clear();
        require(render_scene(render,nullptr,{},&geometryCache)==render_scene(render),
                "Scene edit invalidation refreshes cached geometry");
        {
            SceneData showcase;showcase.Name="Solid terrain arch";showcase.FogDensity=.006;showcase.Hour=14;showcase.Terrain.emplace();
            showcase.Terrain->Volumes.push_back({0,2,0,5,false});
            for(double z:{-4.,-2.,0.,2.,4.})showcase.Terrain->Volumes.push_back({0,2,z,2.6,true});
            auto encoded=showcase.encode();auto roundtrip=SceneData::decode(encoded);
            require(roundtrip.Terrain && roundtrip.Terrain->Volumes.size()==6,"Solid terrain showcase preserves its complete operation stack");
            atomic_write(folder/"solid-terrain-arch.scene",encoded);
            auto preview=render_scene(roundtrip);
            atomic_write(folder/"solid-terrain-arch.bin",std::string_view(reinterpret_cast<const char*>(preview.data()),preview.size()));
        }
        {
            SceneData liquid;liquid.Name="Liquid material basin";liquid.Hour=14;liquid.FogDensity=.008;liquid.Terrain.emplace();auto& bed=*liquid.Terrain;bed.WaterEnabled=true;bed.WaterLevel=0;
            for(int z=0;z<97;++z)for(int x=0;x<97;++x){double px=x*.25-12,pz=z*.25-12;bed.Heights[z*97+x]=.35-1.5*std::exp(-(px*px+pz*pz)/34.);bed.Paint[z*97+x]=2;}
            Character stone;stone.Shape="boulder";stone.Color="#73777C";stone.X=5;stone.Z=1;stone.Size=2;liquid.Characters={stone};
            for(bool lava:{false,true}){bed.Lava=lava;auto preview=render_scene(liquid);auto name=lava?"lava-basin":"water-basin";atomic_write(folder/(std::string(name)+".bin"),std::string_view(reinterpret_cast<const char*>(preview.data()),preview.size()));atomic_write(folder/(std::string(name)+".scene"),liquid.encode());}
        }
        {
            SceneData flames;flames.Name="Volumetric fire reference";flames.Hour=20;flames.FogDensity=.003;
            Character emitter;emitter.Shape="fire";emitter.Color="#FF5909";emitter.Size=2;emitter.Z=0;flames.Characters={emitter};
            auto preview=render_scene(flames);atomic_write(folder/"volumetric-fire.bin",std::string_view(reinterpret_cast<const char*>(preview.data()),preview.size()));atomic_write(folder/"volumetric-fire.scene",flames.encode());
            Character wall;wall.Shape="cube";wall.Color="#585860";wall.Width=5;wall.Height=6;wall.Depth=.5;wall.Z=3;flames.Characters.push_back(wall);
            preview=render_scene(flames);atomic_write(folder/"volumetric-fire-occluded.bin",std::string_view(reinterpret_cast<const char*>(preview.data()),preview.size()));
            flames.Characters.clear();preview=render_scene(flames);atomic_write(folder/"volumetric-fire-empty.bin",std::string_view(reinterpret_cast<const char*>(preview.data()),preview.size()));
        }
        auto packet = render_scene(render);
        atomic_write(folder / "native-scene.bin",
                     std::string_view(reinterpret_cast<const char *>(packet.data()), packet.size()));
        require(packet.size() > 1000, "Native renderer packet emitted");
        generated_scene_readback_checks(require,folder);
        atomic_write(folder / "result.json", json{{"schema", "starlight-check-result-2"}, {"success", true}, {"total", checks.size()}, {"argv0", argv[0]}, {"checks", checks}}.dump(2));
        std::cout << "PASS " << checks.size() << " native checks\n";
        return 0;
    } catch (const std::exception &e) {
        atomic_write(folder / "result.json",
                     json{{"schema", "starlight-check-result-2"}, {"success", false}, {"total", checks.size()}, {"argv0", argv[0]}, {"checks", checks}, {"error", e.what()}}.dump(2));
        std::cerr << e.what() << '\n';
        return 1;
    }
}

// The checks run in one very large function; a few more inlined locals once exceeded the default 1 MB stack. Give it room.
#include <windows.h>
namespace { struct ChecksArgs { int argc; char **argv; int result; }; }
int main(int argc, char **argv) {
    if (!std::getenv("STARLIGHT_MESH_DETAIL")) SetEnvironmentVariableA("STARLIGHT_MESH_DETAIL", "1");   // the canonical geometry reference is the full-detail mesh; the product default (0.33) is not what these checks measure
    ChecksArgs args{argc, argv, 1};
    HANDLE thread = CreateThread(nullptr, 64u * 1024 * 1024, [](LPVOID parameter) -> DWORD {
        auto *a = static_cast<ChecksArgs *>(parameter);
        a->result = checks_main(a->argc, a->argv);
        return 0;
    }, &args, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
    if (!thread) return checks_main(argc, argv);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return args.result;
}
