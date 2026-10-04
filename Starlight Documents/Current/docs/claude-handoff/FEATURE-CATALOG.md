# Current feature catalogue — reading guide

This catalogue preserves the latest generated feature inventory as a source-navigation reference, not new runtime verification. Current overrides: the latest complete audit passed 10/13; editor controls timed out, fullscreen telemetry visibility failed and denoiser object motion failed. Historical denoiser successes do not supersede that failure. GPU fog/smoke density transport exists within the documented bounded domain; older generic atmosphere wording must not be read as its absence. The scene-packet ABI has implementation and contract checks; the generated unknown label is a detector limitation to reconcile, not proof of missing code. Export checks have run, but exhaustive clean-machine/platform certification remains open.

Read CURRENT-ENGINE-WORKUP.md for the current checkpoint. The physics bounds and absent cloth/soft-body/ragdoll systems in the older README remain important. Source markers alone are not runtime acceptance.

---

# Starlight feature reference

Starlight is a native Windows world/scene creator and D3D12 rendering/simulation toolkit, accompanied by a separate native project-module runtime. Its purpose is approachable creation: place content, shape a world, adjust its appearance, save it and preview its behavior through a familiar classic interface.

Current project documentation lives in `Starlight Documents/Current`; older documents outside Current remain historical. The current source snapshot and dated validation take precedence. A roadmap entry, source marker and passing test are three different kinds of evidence.

## Reading the status labels

**Implemented** means the curated execution path has corroborating source markers. **Partial** identifies a working slice with important missing product scope. **Planned/scaffolded** comes from catalogue metadata and is not delivered functionality. **Unknown** means required support is missing. No feature is marked universally complete merely because its name appears in source. Runtime check outcomes are dated separately.

## Feature map

| System | What it is for | Implementation coverage |
|---|---|---|
| Dynamic world physics closeout | Make supported worlds move and react. | Bounded closeout passes; mesh/textures next, performance last. Fixed fluid domains, conservative collision and unsupported-fracture fallback remain. |
| Character movement and grappling | Move continuously through an authored world. | Step 4 accepted within documented scope; current physics closeout fixtures pass |
| Telegraph combat and combat actors | Fight and heal while moving using visible aimed areas. | Step 4A accepted with agreed placeholder damage/health; current exported combat checks pass |
| Crowd control and boss break bars | Apply explicit control restrictions and coordinated boss vulnerability windows. | Implemented runtime components and integrations; exact current test outcomes and remaining acceptance are reported separately |
| Discoverable editor help | Find a way forward without leaving the project. | Implemented text guidance; live UI acceptance pending. Not animated demonstrations, guided task detection or the full three-mode startup |
| Walk and build preview | Make building part of exploring the world. | Experimental code foundation; live input and visual review, objectives, experience and unlocks remain pending |
| Adventure gameplay foundation | Make a small playable adventure without writing code. | Bounded authored rules plus the movement and combat foundation described below; not arbitrary scripting or the complete Interactive building experience |
| Windows adventure export | Share an authored adventure as a standalone Windows game. | Windows x64 / Direct3D 12 foundation; clean-machine, signing, other platforms and visible player acceptance remain unverified |
| Native application and build | The desktop application used to create and preview scenes. | Implemented foundation |
| Classic creator interface | Keep world creation approachable and familiar. | Implemented workflow; broader polish remains bounded by tested cases |
| Independent game preview | Preview the authored scene inside the editor. | Implemented inline workflow; exhaustive parity remains open |
| Scene data and compatibility | Represent the world the user has authored. | Implemented foundation |
| Scene packet ABI | Carry prepared scene geometry and settings into the native viewport. | Unknown — required source support missing |
| Local automation | Allow local tools or an assistant to inspect and edit scenes consistently. | Partial product coverage: implemented local commands |
| Persistence and editing safety | Save work and reverse or cancel edits. | Implemented workflows; long-session memory remains a concern |
| Terrain authoring | Build and paint the ground of a scene. | Partial terrain system: finite heightfield, no streamed world/caves |
| Object geometry and sculpting | Place solids precisely and reshape their actual mesh. | Partial sculpting system: core brushes and placement |
| Props and effects | Populate scenes with basic geometry, nature and visual effects. | Implemented prop set; broader nature systems planned |
| Static model import | Bring a model into the scene as preview geometry. | Partial asset pipeline: static GLB preview |
| Reusable assets and scene composition | Reuse authored content across scenes without rebuilding it. | Implemented bounded content workflow; thumbnails and imported-model templates remain outside scope; Windows adventure packaging is tracked separately |
| Render ownership and updates | Keep rendering and simulation working while the UI processes interaction. | Implemented ownership; optimization and failure recovery incomplete |
| Native rendering contract | Render actual frames at the chosen display resolution. | Implemented configuration; dated visible benchmark evidence is workload-specific |
| Inline hardware ray tracing | Offer a selectable quality ladder for ray-based lighting. | Partial rendering system: implemented tiers, bounded materials |
| Native temporal denoiser | Stabilize noisy ray-traced lighting without vendor reconstruction. | Implemented algorithm; dated native-wavelet diagnostic runs passed, broader quality and stability remain unverified |
| Optional native Ray Reconstruction | Offer an alternative vendor denoiser at native resolution. | Implemented optional integration; portability and current runtime validation bounded |
| Atmosphere and lighting | Set scene mood and illuminate the world. | Partial atmosphere: bounded froxels/lights, no full weather/smoke simulation |
| Rigid-body simulation | Let configured props fall, tumble and interact with collision geometry. | Bounded physics and lifecycle closeout fixtures pass; arbitrary-scale performance remains unverified |
| Water and lava flow | Pour finite water/lava and observe terrain-bound movement. | Partial simulation: surface flow, not general 3D fluids |
| Routes and simulation controls | Make placed objects traverse authored paths during preview. | Partial movement system: authored routes and controls |
| Local music | Play local music while creating. | Implemented editor convenience |
| Texture foundation | Load compressed texture data correctly in supported rendering paths. | Partial asset/material pipeline |
| Project host boundary | Launch native projects with selected scenes and presets. | Implemented host; general export/distribution incomplete |
| Crowd runtime | Exercise large numbers of skinned instances in the separate game project runtime. | Implemented specialized runtime; scope-specific results |
| Persistent CPU workers | Reuse CPU threads for parallel preparation. | Implemented pool; general scheduling roadmap pending |
| Command recording and GPU lifetime | Submit GPU work while preserving resource ownership. | Implemented serial submission foundation |
| Diagnostics and existing audits | Explain failures and retain reproducible evidence. | Implemented tooling; validation gaps explicit |
| Diagnostic benchmark path | Attempt unattended measurement without background presentation throttling. | Implemented diagnostic path; broader reliability validation incomplete |

## Dynamic world physics closeout

**Purpose:** Make supported worlds move and react.

**Implementation:** Bounded closeout passes; mesh/textures next, performance last. Fixed fluid domains, conservative collision and unsupported-fracture fallback remain..

**User workflow / behavior:** Author physical objects, constraints, projectiles, destruction, lightning, grass, fire, fog and liquids; Play and exported games share runtime behavior.

**Connections:** See PHYSICS-CLOSEOUT-CHECKS-20260927.md for 772 core checks and the visible/editor/export evidence. Test counts are not universal certification.

**Limits:** Bounded acceptance only. Unsupported fracture uses generic debris; conservative collision, heightfield liquids and fixed fog domains remain. Cloth, soft bodies and ragdolls are absent.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/adventure.hpp:421](D:/projects/Starlight-Engine/editor/cpp/adventure.hpp:421)
- [editor/cpp/body_constraints.hpp:9](D:/projects/Starlight-Engine/editor/cpp/body_constraints.hpp:9)
- [tools/checks/physics_boundary_checks.inl:12](D:/projects/Starlight-Engine/tools/checks/physics_boundary_checks.inl:12)

## Character movement and grappling

**Purpose:** Move continuously through an authored world.

**Implementation:** Step 4 accepted within documented scope; current physics closeout fixtures pass.

**User workflow / behavior:** WASD and live facing drive movement; Shift sprints, Space jumps, V dodges or lunges, Control changes posture or slides, and G grapples. Resources and traversal states are shared between Play and the standalone player.

**Connections:** Controller, collision, terrain, moving anchors, camera and runtime presentation are connected; source and dated checks are separate evidence.

**Limits:** Source presence is not complete visible or clean-machine validation. Consult the current requirement sign-off for acceptance and remaining gates.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/adventure_controller.hpp:8](D:/projects/Starlight-Engine/editor/cpp/adventure_controller.hpp:8)
- [editor/cpp/grapple_motion.hpp:8](D:/projects/Starlight-Engine/editor/cpp/grapple_motion.hpp:8)
- [editor/cpp/adventure.hpp:852](D:/projects/Starlight-Engine/editor/cpp/adventure.hpp:852)

## Telegraph combat and combat actors

**Purpose:** Fight and heal while moving using visible aimed areas.

**Implementation:** Step 4A accepted with agreed placeholder damage/health; current exported combat checks pass.

**User workflow / behavior:** Player attack uses a repeating 2.5-second cycle; release completes the current cycle. Enemy Swipe uses 1.5 seconds. Cones follow live caster position/facing and fill with channel progress. Friendly damage/healing are blue/green; hostile damage/healing red/orange.

**Connections:** Runtime health, target selection, pursuit, collision, line of sight, breakable objects and healing share the adventure runtime without changing saved scene definitions.

**Limits:** Damage and health values are provisional. Finished character assets and complete milestone sign-off remain deferred or open; see current evidence.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/adventure.hpp:25](D:/projects/Starlight-Engine/editor/cpp/adventure.hpp:25)
- [editor/cpp/adventure.hpp:176](D:/projects/Starlight-Engine/editor/cpp/adventure.hpp:176)
- [editor/cpp/combat_control.hpp:118](D:/projects/Starlight-Engine/editor/cpp/combat_control.hpp:118)

## Crowd control and boss break bars

**Purpose:** Apply explicit control restrictions and coordinated boss vulnerability windows.

**Implementation:** Implemented runtime components and integrations; exact current test outcomes and remaining acceptance are reported separately.

**User workflow / behavior:** Stun, knockback, pull, launch, knockdown, root, slow, silence, fear, taunt, sleep, blind, disorient and tether have separate rules. Player immunity follows control; boss CC instead damages an intact 1,000-point bar, with up to eight seconds stagger, 25 percent extra damage and 90-second refill.

**Connections:** CombatControl, CombatActor and BossBreakBar connect runtime actions and displays. Friendly/self CC protection, player anti-chain rules and boss contribution rules are separate.

**Limits:** Boss behavior is distinct from PvP immunity. Per-effect source and tests must be inspected; this catalogue does not certify balance or full gameplay completion.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/combat_control.hpp:32](D:/projects/Starlight-Engine/editor/cpp/combat_control.hpp:32)
- [editor/cpp/combat_control.hpp:80](D:/projects/Starlight-Engine/editor/cpp/combat_control.hpp:80)
- [editor/cpp/combat_control.hpp:10](D:/projects/Starlight-Engine/editor/cpp/combat_control.hpp:10)

## Discoverable editor help

**Purpose:** Find a way forward without leaving the project.

**Implementation:** Implemented text guidance; live UI acceptance pending. Not animated demonstrations, guided task detection or the full three-mode startup.

**User workflow / behavior:** Help / Tutorials and F1 open optional searchable lessons, with Back, Next, Restart and saved reading positions. Close or Escape returns to creating.

**Connections:** Shared controls remain available across current editing experiences. Stable lesson IDs preserve progress through catalog changes; malformed preferences fall back safely.

**Limits:** Text guidance with saved reading positions; live appearance and input verification pending. Not interactive task tracking.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/tutorials.inl:112](D:/projects/Starlight-Engine/editor/cpp/tutorials.inl:112)
- [editor/cpp/tutorial_progress.hpp:24](D:/projects/Starlight-Engine/editor/cpp/tutorial_progress.hpp:24)
- [editor/cpp/editor_main.cpp:121](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:121)

## Walk and build preview

**Purpose:** Make building part of exploring the world.

**Implementation:** Experimental code foundation; live input and visual review, objectives, experience and unlocks remain pending.

**User workflow / behavior:** Walk at player height, right-drag to look, and use existing editing tools. Play starts a separate test; Pause restores authored objects at the tested viewpoint.

**Connections:** Shares scene data, Undo, asset insertion and snapping. Returning to ordinary editing preserves authored changes. Core checks cover viewpoint/data separation, not full GUI acceptance.

**Limits:** Experimental preview. Physical input, complete visual acceptance, objectives, XP and unlocks are not established.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/editor_main.cpp:121](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:121)
- [editor/cpp/adventure.hpp:677](D:/projects/Starlight-Engine/editor/cpp/adventure.hpp:677)
- [editor/cpp/classic_completion.inl:42](D:/projects/Starlight-Engine/editor/cpp/classic_completion.inl:42)

## Adventure gameplay foundation

**Purpose:** Make a small playable adventure without writing code.

**Implementation:** Bounded authored rules plus the movement and combat foundation described below; not arbitrary scripting or the complete Interactive building experience.

**User workflow / behavior:** New starter adventure creates a key, door and exit level with save protection. Existing scenes can opt into walking controls; Beginner and Advanced expose different settings on the same data. Play uses WASD, right-drag and E.

**Connections:** Static-box collision and key links use an independent play copy; Reset preserves authored objects. The starter path has code-driven walking and interaction checks, distinct from human usability review.

**Limits:** Collection, interaction and spherical area events with item/objective conditions and objective/door/finish actions. Shared character movement and combat now exist; final Step 4/4A acceptance remains open. No arbitrary scripting. Check current validation evidence separately.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/adventure.hpp:25](D:/projects/Starlight-Engine/editor/cpp/adventure.hpp:25)
- [editor/cpp/gameplay_rules.hpp:8](D:/projects/Starlight-Engine/editor/cpp/gameplay_rules.hpp:8)
- [editor/cpp/scene.hpp:125](D:/projects/Starlight-Engine/editor/cpp/scene.hpp:125)
- [editor/cpp/editor_main.cpp:1368](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:1368)

## Windows adventure export

**Purpose:** Share an authored adventure as a standalone Windows game.

**Implementation:** Windows x64 / Direct3D 12 foundation; clean-machine, signing, other platforms and visible player acceptance remain unverified.

**User workflow / behavior:** Export playable game writes a new folder with a player, engine/runtime libraries and portable scene content. Existing folders are refused; missing runtime files abort the export.

**Connections:** The player shares movement and interaction code with the editor. Package hashes and content validation are diagnostic integrity checks, not signatures. Hidden startup rendering is distinct from visible input verification.

**Limits:** Package validation does not prove visible play, clean-machine compatibility, code signing, or non-Windows support.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/game_export.cpp:4](D:/projects/Starlight-Engine/editor/cpp/game_export.cpp:4)
- [editor/cpp/player_main.cpp:107](D:/projects/Starlight-Engine/editor/cpp/player_main.cpp:107)
- [CMakeLists.txt:75](D:/projects/Starlight-Engine/CMakeLists.txt:75)

## Native application and build

**Purpose:** The desktop application used to create and preview scenes.

**Implementation:** Implemented foundation.

**User workflow / behavior:** Open the native editor through its launcher. The active product is native C++; Python is build/analysis tooling.

**Connections:** The editor uses the scene core and NativeSession; the separate project host loads native project modules.

**Limits:** Windows/D3D12 is the current build contract. Compiling successfully does not validate runtime behavior or portable packaging.

**Evidence:** Source support is corroboration, not a new runtime test.
- [CMakeLists.txt:72](D:/projects/Starlight-Engine/CMakeLists.txt:72)
- [editor/cpp/editor_main.cpp:3152](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:3152)
- [Build-Native.py:1](D:/projects/Starlight-Engine/Build-Native.py:1)

## Classic creator interface

**Purpose:** Keep world creation approachable and familiar.

**Implementation:** Implemented workflow; broader polish remains bounded by tested cases.

**User workflow / behavior:** Use floating Builder, Characters, Environment and More menus over the canvas. Simple settings hide advanced transforms/body controls. Terrain and Shapes have dedicated gear panels; named colors and quality choices replace numeric labels.

**Connections:** Controls edit authored scene data. Advanced disclosure preserves hidden properties; the feature catalogue also contains future ideas.

**Limits:** A catalogue entry is not proof of a working subsystem. Latest saved UI evidence is described separately.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/editor_main.cpp:117](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:117)
- [tools/checks/classic_ui_checks.inl:158](D:/projects/Starlight-Engine/tools/checks/classic_ui_checks.inl:158)
- [editor/cpp/classic_completion.inl:16](D:/projects/Starlight-Engine/editor/cpp/classic_completion.inl:16)
- [editor/cpp/named_colors.hpp:44](D:/projects/Starlight-Engine/editor/cpp/named_colors.hpp:44)

## Independent game preview

**Purpose:** Preview the authored scene inside the editor.

**Implementation:** Implemented inline workflow; exhaustive parity remains open.

**User workflow / behavior:** Play uses the existing editor canvas and session in both windowed and fullscreen modes. Pause holds simulation; Reset returns to the authored start.

**Connections:** Retained GamePreview support is not the active Play command path. Inline preview is not a packaged executable export.

**Limits:** Independent views consume additional resources; this is not a demonstrated shared GPU resource pool or a packaged game exporter.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/classic_completion.inl:28](D:/projects/Starlight-Engine/editor/cpp/classic_completion.inl:28)
- [editor/cpp/classic_completion.inl:59](D:/projects/Starlight-Engine/editor/cpp/classic_completion.inl:59)
- [editor/cpp/game_preview.hpp:16](D:/projects/Starlight-Engine/editor/cpp/game_preview.hpp:16)

## Scene data and compatibility

**Purpose:** Represent the world the user has authored.

**Implementation:** Implemented foundation.

**User workflow / behavior:** A scene contains objects, terrain, pours, environment and rendering preferences. Character records also represent shapes/effects; a record is not necessarily an animated person.

**Connections:** Scene JSON is separate from renderer packets and project manifests. Authored state is separate from transient simulation.

**Limits:** This is a record/vector model, not evidence of a general archetype ECS. Saved authored state differs from a simulation checkpoint.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/scene.hpp:171](D:/projects/Starlight-Engine/editor/cpp/scene.hpp:171)
- [editor/cpp/scene_json.cpp:9](D:/projects/Starlight-Engine/editor/cpp/scene_json.cpp:9)
- [editor/cpp/scene.cpp:23](D:/projects/Starlight-Engine/editor/cpp/scene.cpp:23)

## Scene packet ABI

**Purpose:** Carry prepared scene geometry and settings into the native viewport.

**Implementation:** Unknown — required source support missing.

**User workflow / behavior:** The editor writes the V7 binary scene packet; the parser retains V1–V7 support. The scene JSON schema and project schema use independent version numbers.

**Connections:** The packet connects editor geometry to GPU resource creation and viewport rendering.

**Limits:** Native exports remain active ABI boundaries even though the managed editor was retired.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/native/viewport.cpp:178](D:/projects/Starlight-Engine/editor/native/viewport.cpp:178)
- Required support missing: `editor/cpp/render_scene.cpp` / `0x37564c53`. Description above requires review.

## Local automation

**Purpose:** Allow local tools or an assistant to inspect and edit scenes consistently.

**Implementation:** Partial product coverage: implemented local commands.

**User workflow / behavior:** Describe capabilities, inspect a scene, or apply a validated command batch: add, move, route, rename, remove, configure, shape, transform, terrain brush, water and stamp.

**Connections:** Commands use expected scene hashes and persistence rules; they do not embed an AI provider or duplicate every GUI action.

**Limits:** This is local command automation. It is not an embedded AI model, cloud service, or complete GUI command parity.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/commands.cpp:44](D:/projects/Starlight-Engine/editor/cpp/commands.cpp:44)
- [editor/cpp/commands.cpp:89](D:/projects/Starlight-Engine/editor/cpp/commands.cpp:89)
- [editor/cpp/command_runner.cpp:19](D:/projects/Starlight-Engine/editor/cpp/command_runner.cpp:19)

## Persistence and editing safety

**Purpose:** Save work and reverse or cancel edits.

**Implementation:** Implemented workflows; long-session memory remains a concern.

**User workflow / behavior:** Use save/open and undo/redo. Brushes are transactions; canceled work can return to the previous scene. Save/Discard/Cancel protects replacement/closing; stale scene writes are rejected.

**Connections:** Scene history stores scene state; validation and atomic file replacement protect persistence. It is not a bounded delta-history system.

**Limits:** Whole-scene history and asset portability need separate memory and distribution policies. No unlimited-history safety claim is made.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/platform.cpp:113](D:/projects/Starlight-Engine/editor/cpp/platform.cpp:113)
- [editor/cpp/mesh.hpp:46](D:/projects/Starlight-Engine/editor/cpp/mesh.hpp:46)
- [editor/cpp/commands.cpp:89](D:/projects/Starlight-Engine/editor/cpp/commands.cpp:89)

## Terrain authoring

**Purpose:** Build and paint the ground of a scene.

**Implementation:** Partial terrain system: finite heightfield, no streamed world/caves.

**User workflow / behavior:** Raise/lower, smooth, flatten and paint terrain; place hill/crater/plateau stamps or stamp a selected shape. Terrain brush settings are independent from object sculpt settings.

**Connections:** The 97×97 authored heightfield becomes a 193×193 display mesh. Physics uses collision tiles; liquid surfaces use the terrain as their bed.

**Limits:** A heightfield cannot encode caves or stacked surfaces. Rendering currently rebuilds geometry; this is not world streaming.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/scene.hpp:24](D:/projects/Starlight-Engine/editor/cpp/scene.hpp:24)
- [editor/cpp/scene.hpp:25](D:/projects/Starlight-Engine/editor/cpp/scene.hpp:25)
- [editor/cpp/geometry.cpp:221](D:/projects/Starlight-Engine/editor/cpp/geometry.cpp:221)

## Object geometry and sculpting

**Purpose:** Place solids precisely and reshape their actual mesh.

**Implementation:** Partial sculpting system: core brushes and placement.

**User workflow / behavior:** Translate, rotate, scale and snap objects. Pull, Push, Smooth and Flatten edit a mesh copy; undo/cancel preserves the source asset. Starter placement is bounded and supports adjustable grid spacing. Rotation handles can snap their edited axis on release; grouped placement preserves relative spacing.

**Connections:** Transforms affect rendering, bounds and collision preparation. Sculpting is separate from terrain height brushes.

**Limits:** Do not infer topology repair, adaptive remeshing, UV authoring or a production sculpting package.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/mesh.cpp:32](D:/projects/Starlight-Engine/editor/cpp/mesh.cpp:32)
- [editor/cpp/interactions.cpp:13](D:/projects/Starlight-Engine/editor/cpp/interactions.cpp:13)
- [editor/cpp/scene.hpp:18](D:/projects/Starlight-Engine/editor/cpp/scene.hpp:18)

## Props and effects

**Purpose:** Populate scenes with basic geometry, nature and visual effects.

**Implementation:** Implemented prop set; broader nature systems planned.

**User workflow / behavior:** Place basic solids, rock/boulder/pebble/grass objects, fire/light/lava helpers, Chaos Lightning, Chaos Rock, Light-Bound Shard and Craxis Smoke.

**Connections:** Procedural geometry, materials, lights and volumes create these appearances. Supported fracture and wind/contact grass motion now have explicit physics checks; asset appearance upgrades remain Task 2.

**Limits:** Procedural props are not a world scatter, foliage simulation, fracture or particle system.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/scene.hpp:185](D:/projects/Starlight-Engine/editor/cpp/scene.hpp:185)
- [editor/cpp/geometry.cpp:2](D:/projects/Starlight-Engine/editor/cpp/geometry.cpp:2)
- [deep/engine/chaos_lightning.h:21](D:/projects/Starlight-Engine/deep/engine/chaos_lightning.h:21)

## Static model import

**Purpose:** Bring a model into the scene as preview geometry.

**Implementation:** Partial asset pipeline: static GLB preview.

**User workflow / behavior:** Choose a GLB model, validate it, then place and transform instances. The importer handles static mesh geometry and validates its buffers/accessors/hierarchy.

**Connections:** Imported objects feed scene geometry and optional sculpt/collision preparation. Creator import is distinct from the skinned crowd runtime.

**Limits:** Creator static import must not inherit the separate game project crowd runtime’s skinning/animation capability. Full imported material/animation authoring is not established.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/assets.cpp:28](D:/projects/Starlight-Engine/editor/cpp/assets.cpp:28)
- [editor/cpp/assets.cpp:60](D:/projects/Starlight-Engine/editor/cpp/assets.cpp:60)

## Reusable assets and scene composition

**Purpose:** Reuse authored content across scenes without rebuilding it.

**Implementation:** Implemented bounded content workflow; thumbnails and imported-model templates remain outside scope; Windows adventure packaging is tracked separately.

**User workflow / behavior:** Browse a remembered folder with text/type filters. Insert models, native templates/groups or scene objects; copy/paste selections during an editor session; repair missing models; export portable scene/selection folders.

**Connections:** Insertions receive independent identities and one Undo. Groups retain spacing and translated routes. Portable folders carry embedded GLBs and hash manifests; editor-session copied models retain original file references. [Superseded 2026-10-04: models are content-addressed library assets referenced by id, not by file path, and portable folders carry `assets/<id>.slasset` files with SHA-256 manifests.]

**Limits:** Text browser has no thumbnails. Native templates exclude imported models; portable scene folders support embedded GLB dependencies. [Superseded 2026-10-04: reusable objects and groups (Version 2) may contain imported models, bundled beside the file; portable folders bundle every referenced engine asset. Compiled and headless-checked only.] This is editor-content reuse, not standalone game export. GUI visual acceptance and clean-machine validation remain separate.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/asset_browser.inl:82](D:/projects/Starlight-Engine/editor/cpp/asset_browser.inl:82)
- [editor/cpp/selection_clipboard.inl:3](D:/projects/Starlight-Engine/editor/cpp/selection_clipboard.inl:3)
- [editor/cpp/object_template.cpp:52](D:/projects/Starlight-Engine/editor/cpp/object_template.cpp:52)
- [editor/cpp/portable_scene.cpp:17](D:/projects/Starlight-Engine/editor/cpp/portable_scene.cpp:17)

## Render ownership and updates

**Purpose:** Keep rendering and simulation working while the UI processes interaction.

**Implementation:** Implemented ownership; optimization and failure recovery incomplete.

**User workflow / behavior:** A session receives coalesced scene/camera/playback changes, renders frames and exposes status/capture. Hidden preview sessions can suspend and resume.

**Connections:** The session coordinates physics, flow and viewport submission. Scene refresh and resize still contain expensive reconstruction paths.

**Limits:** Resize now preserves the viewport/device and replaces size-dependent images; scene changes can still rebuild geometry. Worker ownership is not a proof of race freedom.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/native_session.hpp:39](D:/projects/Starlight-Engine/editor/cpp/native_session.hpp:39)
- [editor/cpp/native_session.cpp:105](D:/projects/Starlight-Engine/editor/cpp/native_session.cpp:105)
- [editor/cpp/native_session.cpp:54](D:/projects/Starlight-Engine/editor/cpp/native_session.cpp:54)

## Native rendering contract

**Purpose:** Render actual frames at the chosen display resolution.

**Implementation:** Implemented configuration; dated visible benchmark evidence is workload-specific.

**User workflow / behavior:** Current acceptance requires render/output dimensions to match, no super resolution and no frame generation. HDR scene shading and spatial edge smoothing precede presentation.

**Connections:** Native-resolution RR is an optional denoiser, not permission to enable spatial upscaling. Existing runtime vendor source is not proof it is active in the editor.

**Limits:** No super resolution or frame generation is allowed for current acceptance. Native resolution does not guarantee alias-free output.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/native/viewport.cpp:162](D:/projects/Starlight-Engine/editor/native/viewport.cpp:162)
- [editor/native/viewport.cpp:162](D:/projects/Starlight-Engine/editor/native/viewport.cpp:162)
- [editor/native/edge_aa.h:14](D:/projects/Starlight-Engine/editor/native/edge_aa.h:14)

## Inline hardware ray tracing

**Purpose:** Offer a selectable quality ladder for ray-based lighting.

**Implementation:** Partial rendering system: implemented tiers, bounded materials.

**User workflow / behavior:** Choose Off, Low, Medium, High or Path tracing. The implementation ranges from direct visibility to AO/reflections, diffuse GI and sampled multi-bounce opaque transport; freeze/bounce settings support reference views.

**Connections:** Inline compute RayQuery uses BLAS/TLAS geometry, then passes noisy lighting and guides to denoising. Unsupported capability can fall back to raster.

**Limits:** This is not an RT state-object/shader-table pipeline. Historical device-loss failures remain dated evidence; current throughput requires a successful run with explicit workload, native dimensions and presentation conditions.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/engine/dxr.hlsl:30](D:/projects/Starlight-Engine/deep/engine/dxr.hlsl:30)
- [deep/engine/dxr.h:94](D:/projects/Starlight-Engine/deep/engine/dxr.h:94)
- [tools/build/cmake_shaders.py:14](D:/projects/Starlight-Engine/tools/build/cmake_shaders.py:14)

## Native temporal denoiser

**Purpose:** Stabilize noisy ray-traced lighting without vendor reconstruction.

**Implementation:** Implemented algorithm; dated native-wavelet diagnostic runs passed, broader quality and stability remain unverified.

**User workflow / behavior:** Mode 0 selects the native temporal wavelet path: prefilter, history reprojection/rejection, atrous filtering and composition.

**Connections:** Depth, normal, motion and material guides support temporal decisions. Emission/history handling is part of the rendering pipeline.

**Limits:** ABI mode 0 is native wavelet. Existence of the implementation is not fresh runtime validation or proof of zero ghosting.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/engine/svgf.hlsl:14](D:/projects/Starlight-Engine/deep/engine/svgf.hlsl:14)
- [deep/engine/svgf.hlsl:26](D:/projects/Starlight-Engine/deep/engine/svgf.hlsl:26)
- [deep/engine/svgf.hlsl:50](D:/projects/Starlight-Engine/deep/engine/svgf.hlsl:50)
- [deep/engine/svgf.hlsl:1](D:/projects/Starlight-Engine/deep/engine/svgf.hlsl:1)

## Optional native Ray Reconstruction

**Purpose:** Offer an alternative vendor denoiser at native resolution.

**Implementation:** Implemented optional integration; portability and current runtime validation bounded.

**User workflow / behavior:** Mode 1 requests NVIDIA RR through NGX at 1:1 input/output dimensions. Missing or failed support can fall back to the native filter with status reporting.

**Connections:** RR depends on local SDK/runtime availability and guide buffers. Preserve ABI 0=wavelet, 1=RR.

**Limits:** RR must remain 1:1. Vendor capability, local SDK/DLL paths and fallback behavior require explicit validation; no super-resolution authorization follows from RR.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/engine/ray_reconstruction.h:30](D:/projects/Starlight-Engine/deep/engine/ray_reconstruction.h:30)
- [deep/engine/ray_reconstruction.h:24](D:/projects/Starlight-Engine/deep/engine/ray_reconstruction.h:24)
- [editor/native/viewport.cpp:298](D:/projects/Starlight-Engine/editor/native/viewport.cpp:298)

## Atmosphere and lighting

**Purpose:** Set scene mood and illuminate the world.

**Implementation:** Partial atmosphere: bounded froxels/lights, no full weather/smoke simulation.

**User workflow / behavior:** Configure time of day/day cycle, fog, local lights and smoke. Water/lava/chaos appearances combine procedural shading, emission and explicit lighting.

**Connections:** Atmosphere injection/integration/composition follows the surface rendering path. Raster proxy shadows differ from triangle visibility in RT.

**Limits:** Bounded froxel fog and procedural skies are not a full weather or fluid-smoke solver. Raster and ray visibility differ.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/engine/atmosphere.hlsl:81](D:/projects/Starlight-Engine/deep/engine/atmosphere.hlsl:81)
- [editor/cpp/scene.hpp:163](D:/projects/Starlight-Engine/editor/cpp/scene.hpp:163)
- [editor/native/viewport.cpp:181](D:/projects/Starlight-Engine/editor/native/viewport.cpp:181)

## Rigid-body simulation

**Purpose:** Let configured props fall, tumble and interact with collision geometry.

**Implementation:** Bounded physics and lifecycle closeout fixtures pass; arbitrary-scale performance remains unverified.

**User workflow / behavior:** Body settings and density feed Jolt fixed-step simulation. Terrain/prop collision preparation runs asynchronously; playback/reset separates simulation from authored placement.

**Connections:** Body poses update scene transforms and liquid obstacle/coupling inputs. Collision geometry can approximate visual meshes.

**Limits:** Async collision cooks and four-worker Jolt scheduling are not a global concurrency budget. High-density coupled performance remains unverified.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/Physics/PhysicsWorld.cpp:5](D:/projects/Starlight-Engine/deep/Physics/PhysicsWorld.cpp:5)
- [deep/Physics/PhysicsWorld.cpp:166](D:/projects/Starlight-Engine/deep/Physics/PhysicsWorld.cpp:166)
- [editor/cpp/physics.cpp:6](D:/projects/Starlight-Engine/editor/cpp/physics.cpp:6)

## Water and lava flow

**Purpose:** Pour finite water/lava and observe terrain-bound movement.

**Implementation:** Partial simulation: surface flow, not general 3D fluids.

**User workflow / behavior:** Arm a pour, click the ground, then Play/Pause/Reset. Separate water/lava fields evolve on a 48×48 grid, with obstacle coverage and liquid geometry.

**Connections:** Terrain elevation and sampled solid occupancy guide transport; the physics wrapper uses liquid data for approximate body coupling.

**Limits:** This is terrain-bound surface transport, not a general 3D incompressible fluid solver or guaranteed two-way pressure coupling.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/flow.hpp:60](D:/projects/Starlight-Engine/editor/cpp/flow.hpp:60)
- [editor/cpp/flow.hpp:61](D:/projects/Starlight-Engine/editor/cpp/flow.hpp:61)
- [editor/cpp/flow.cpp:125](D:/projects/Starlight-Engine/editor/cpp/flow.cpp:125)
- [editor/cpp/flow.hpp:6](D:/projects/Starlight-Engine/editor/cpp/flow.hpp:6)

## Routes and simulation controls

**Purpose:** Make placed objects traverse authored paths during preview.

**Implementation:** Partial movement system: authored routes and controls.

**User workflow / behavior:** Draw X/Z routes, set speed, Play/Pause/Reset, optionally repeat or follow a character. Escape cancels active operations before leaving fullscreen.

**Connections:** Routes drive transforms separately from skeletal animation and navmesh/character collision. Route completion and fluids have distinct playback concerns.

**Limits:** Route playback is distinct from navigation, skeletal locomotion, collision-aware character controllers and animation graphs.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/scene.hpp:46](D:/projects/Starlight-Engine/editor/cpp/scene.hpp:46)
- [editor/cpp/native_session.cpp:110](D:/projects/Starlight-Engine/editor/cpp/native_session.cpp:110)
- [editor/cpp/native_session.cpp:111](D:/projects/Starlight-Engine/editor/cpp/native_session.cpp:111)

## Local music

**Purpose:** Play local music while creating.

**Implementation:** Implemented editor convenience.

**User workflow / behavior:** The dedicated player provides track selection, playlist next, play/pause, stop and volume.

**Connections:** Windows MCI supplies local playback; this is editor media, not a spatial sound or exported soundtrack engine.

**Limits:** Editor music is not a spatial audio engine or exported game soundtrack system.

**Evidence:** Source support is corroboration, not a new runtime test.
- [editor/cpp/music.hpp:11](D:/projects/Starlight-Engine/editor/cpp/music.hpp:11)
- [editor/cpp/classic_completion.inl:3](D:/projects/Starlight-Engine/editor/cpp/classic_completion.inl:3)

## Texture foundation

**Purpose:** Load compressed texture data correctly in supported rendering paths.

**Implementation:** Partial asset/material pipeline.

**User workflow / behavior:** DDS BC5/BC7 handling preserves authored mips and uses format-aware uploads/views; image-loading fallback remains.

**Connections:** The separate runtime texture path is broader than editor procedural materials. A loader does not supply a full terrain PBR library or streaming manager.

**Limits:** A BC5/BC7 loader is not a complete editor PBR material library, offline cooker or asynchronous residency manager.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/engine/model/texture.cpp:28](D:/projects/Starlight-Engine/deep/engine/model/texture.cpp:28)
- [deep/engine/model/texture.cpp:17](D:/projects/Starlight-Engine/deep/engine/model/texture.cpp:17)
- [deep/engine/model/texture.cpp:17](D:/projects/Starlight-Engine/deep/engine/model/texture.cpp:17)

## Project host boundary

**Purpose:** Launch native projects with selected scenes and presets.

**Implementation:** Implemented host; general export/distribution incomplete.

**User workflow / behavior:** Choose a project manifest, choose a scene, reconcile/check/save preset settings and launch the native host. Empty and GameProject are project modules.

**Connections:** Project schema 3 and .startest settings belong to the project workflow. A creator .starscene is not automatically exported into a standalone game.

**Limits:** Creator scene Play and project-DLL execution are separate workflows. See current publication and closeout evidence for tested export cases; project-host and editor-player workflows remain distinct.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/src/runtime.cpp:59](D:/projects/Starlight-Engine/deep/src/runtime.cpp:59)
- [editor/cpp/projects.cpp:48](D:/projects/Starlight-Engine/editor/cpp/projects.cpp:48)
- [CMakeLists.txt:98](D:/projects/Starlight-Engine/CMakeLists.txt:98)

## Crowd runtime

**Purpose:** Exercise large numbers of skinned instances in the separate game project runtime.

**Implementation:** Implemented specialized runtime; scope-specific results.

**User workflow / behavior:** Gather/cull instances, evaluate poses and build instance buckets using specialized project code.

**Connections:** It shares renderer lineage and worker machinery, but is not the creator static-model importer or 100,000 independent gameplay entities.

**Limits:** Do not attribute large-crowd figures to the editor or to independent gameplay entities; inherited results are workload-specific.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/projects/GameProject/project.cpp:682](D:/projects/Starlight-Engine/deep/projects/GameProject/project.cpp:682)
- [deep/projects/GameProject/project.cpp:192](D:/projects/Starlight-Engine/deep/projects/GameProject/project.cpp:192)
- [deep/projects/GameProject/project.cpp:189](D:/projects/Starlight-Engine/deep/projects/GameProject/project.cpp:189)

## Persistent CPU workers

**Purpose:** Reuse CPU threads for parallel preparation.

**Implementation:** Implemented pool; general scheduling roadmap pending.

**User workflow / behavior:** Persistent range workers divide contiguous work, let the caller participate and join at completion. Actual participation depends on requested workers and grain sizes.

**Connections:** Jolt and asynchronous collision cooking have separate concurrency budgets. This is not a universal task graph or work-stealing scheduler.

**Limits:** This class does not implement a general task DAG, fibers or work stealing. Wider thread counts require measured gains, not utilization alone.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/engine/persistent_workers.h:51](D:/projects/Starlight-Engine/deep/engine/persistent_workers.h:51)
- [deep/engine/persistent_workers.h:61](D:/projects/Starlight-Engine/deep/engine/persistent_workers.h:61)
- [deep/engine/persistent_workers.h:41](D:/projects/Starlight-Engine/deep/engine/persistent_workers.h:41)

## Command recording and GPU lifetime

**Purpose:** Submit GPU work while preserving resource ownership.

**Implementation:** Implemented serial submission foundation.

**User workflow / behavior:** Frame slots, command allocators/lists, barriers and fences order GPU work. CPU preparation may be parallel while recording/submission remains serial.

**Connections:** Future parallel recording needs reserved bindings and per-worker/per-slot ownership, not simply more CPU threads.

**Limits:** Future parallel recording needs per-worker/per-slot ownership, reserved bindings and ordered resource transitions.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/engine/render_device.h:964](D:/projects/Starlight-Engine/deep/engine/render_device.h:964)
- [deep/engine/render_device.h:962](D:/projects/Starlight-Engine/deep/engine/render_device.h:962)
- [deep/engine/render_device.h:722](D:/projects/Starlight-Engine/deep/engine/render_device.h:722)

## Diagnostics and existing audits

**Purpose:** Explain failures and retain reproducible evidence.

**Implementation:** Implemented tooling; validation gaps explicit.

**User workflow / behavior:** Native suites test core data, diagnostics, textures, sessions and classic UI; the audit harness records isolated runs, process measurements and renderer comparisons.

**Connections:** Saved tests identify exactly what was reached/passed/failed. This knowledge tool reads them; it does not rerun them.

**Limits:** Checks are evidence for their exact run, not blanket certification. This analyzer does not run them or change failed assertions.

**Evidence:** Source support is corroboration, not a new runtime test.
- [CMakeLists.txt:37](D:/projects/Starlight-Engine/CMakeLists.txt:37)
- [CMakeLists.txt:74](D:/projects/Starlight-Engine/CMakeLists.txt:74)
- [tools/audit/audit.cpp:1084](D:/projects/Starlight-Engine/tools/audit/audit.cpp:1084)

## Diagnostic benchmark path

**Purpose:** Attempt unattended measurement without background presentation throttling.

**Implementation:** Implemented diagnostic path; broader reliability validation incomplete.

**User workflow / behavior:** The diagnostic path exposes benchmark controls and auditOffscreen; historical failures and subsequent visible-run results must be distinguished by date and workload.

**Connections:** The 22 September reliability checkpoint documents debug-confirmed invalid backbuffer use and an isolated output-target correction; that historical finding does not establish the outcome of later lifecycle checks. Consult the dated runtime evidence for current bounded results.

**Limits:** Historical renderer failures are preserved; newer Gate 0 evidence and the reliability checkpoint record an isolated output-target correction. A source scan alone does not certify matched performance or lifecycle reliability.

**Evidence:** Source support is corroboration, not a new runtime test.
- [deep/engine/render_device.h:461](D:/projects/Starlight-Engine/deep/engine/render_device.h:461)
- [editor/native/viewport.cpp:152](D:/projects/Starlight-Engine/editor/native/viewport.cpp:152)

## Saved UI behavior checks

These named interactions were exercised in `run-20260917-192850`. They are a useful detailed behavior index, not fresh verification of future changes.

| Interaction | Recorded result |
|---|---|
| beginner hides transforms, physics and irrelevant lightning | Pass |
| beginner edits preserve hidden properties | Pass |
| advanced build/body expand independently | Pass |
| terrain gear opens its own form | Pass |
| paint choice survives menu changes | Pass |
| pour is armed without camera-centered placement | Pass |
| ground click commits one lava pour | Pass |
| pour undo | Pass |
| pour redo | Pass |
| draw route supersedes sculpt mode | Pass |
| shapes gear exposes snapping and views | Pass |
| opening controls preserves full canvas size | Pass |
| More restores all twenty catalogue entries | Pass |
| every catalogue item exposes dependencies proof and availability | Pass |
| dedicated music panel has empty-state button gating and volume | Pass |
| music volume slider dispatches independently of inspector sliders | Pass |
| native music decoder plays selected local WAV | Pass |
| dedicated music toggle pauses playback | Pass |
| Next advances playlist and starts track | Pass |
| Stop resets native music playback | Pass |
| project window is separate modal with scene selector | Pass |
| empty project disables preset checks and launch | Pass |
| modal restores previously disabled test owner without enabling it | Pass |
| loaded project populates scene choices and enables valid preset actions | Pass |
| invalid preset field stays in modal with inline error | Pass |
| checking preset disables duplicate submission and close | Pass |
| busy modal cannot be prematurely closed | Pass |
| native preset reconciliation preserves draft resolution | Pass |
| named Pink floor choice uses original classic hex color | Pass |
| every dock is bounded across narrow and wide window sizes | Pass |
| horizontal scrolling reaches the last close button in every dock | Pass |
| starter placement applies grid snap and scene bounds | Pass |
| starter placement remains inside terrain | Pass |
| Escape cancels captured starter drag without placement | Pass |
| Escape cancels armed pour before fullscreen | Pass |
| Escape closes popup before fullscreen | Pass |
| Escape closes settings before fullscreen | Pass |
| Escape exits fullscreen only after suboperations are closed | Pass |
| windowed Play creates independent preview HWND and session | Pass |
| preview advances while authoring session stays stopped | Pass |
| preview pause freezes simulation time | Pass |
| preview reset restores zero simulation time | Pass |
| closing preview parks its renderer and preserves main window | Pass |
| preview reopen reuses HWND and renderer instead of recreating GPU resources | Pass |
| fullscreen Play uses the primary canvas without revealing preview | Pass |
| renderer finishes startup before lifetime baseline | Pass |
| renderer parks between frames for isolated UI lifetime check | Pass |
| owned window classes and counts remain identical | Pass |
| second churn batch does not increase USER or GDI high-water marks | Pass |
| tooltip registrations exist and retire with controls | Pass |
| UI churn does not submit frames while renderer is parked | Pass |
| renderer resumes after UI changes | Pass |

## Catalogue and planned product scope

These are the actual stored catalogue entries. Status, Available, descriptions and requested Proof are metadata; they can lag implementation. Read the source-supported system descriptions above before interpreting an unavailable catalogue flag. A Proof field is an acceptance aspiration, not a passed test.

| Catalogue feature | Description | Stored status / availability | Dependencies | Requested proof (not a test result) |
|---|---|---|---|---|
| Native Play | Run the scene you made in the real game engine, inside the editor canvas in both windowed and full-screen modes. | experimental / False | Saved scenes and renderer connection | Saved characters and routes run correctly; stopping returns to editing. |
| Playback choices | Play once and finish automatically, or repeat your routes. Keep Play/Pause and Reset. | experimental / False | Route completion and repeat rules | Different route lengths, pause, replay and reset all work without losing edits. |
| Assets & reusable objects | Browse and search asset folders for models, native objects, groups and scenes. Reuse native shapes and sculpts individually or in groups. Replace imported models while retaining object settings. Repair missing model references; export entire scenes or selected objects with embedded GLB assets and an integrity manifest; add objects from saved scenes without replacing the current world. Browser thumbnails remain planned. Windows adventure packaging is tracked separately under Build & export. Update 2026-10-04: models, sculpts, terrain and textures are content-addressed assets in one shared library and scenes are schema 16 key cards; see docs/architecture/KEY-CARD-ARCHITECTURE.md. | experimental / False | Portable asset references and import pipeline | A copied project opens with its assets on another computer. |
| Animation | Make characters walk, turn and change between movements. | scaffolded / False | Animation import and native scene playback | Walking follows the route and transitions without broken poses. |
| Gameplay rules | Adventure walking controls with beginner and advanced settings. Assign collectible keys, matching doors and exits to scene objects. Play uses temporary state; Reset restores authored objects. | experimental / True | Broader triggers, dynamic character collision and complete gameplay authoring remain planned | A small game can be built, saved and played with working rules. |
| Physics | Give objects weight and stop them passing through floors and walls. | partial / True | Transforms and collision shapes | Jolt rigid-body integration with bounded falling-object and terrain checks; broad collision and scale validation remain open. |
| Navigation | Help characters find their way around obstacles. | scaffolded / False | Walkable surfaces and collision data | Characters reach a destination without walking through walls. |
| Materials & lighting | Choose how surfaces look and light your scene. Advanced lighting stays optional. | scaffolded / False | Texture import and renderer materials | Saved appearances match native playback with measured performance cost. |
| Terrain & large scenes | Experimental 24 × 24 metre terrain: sculpt and stamp terrain, blend detailed procedural surfaces, place rocks and grass, and pour water or lava. Object sculpt brushes are available. Large-world streaming and caves remain planned. | experimental / False | Assets, scene storage and streaming | A large scene loads and moves smoothly within a recorded memory budget. |
| 2D games | Build games with pictures, layers, 2D cameras and tile maps. | scaffolded / False | 2D rendering, animation and collision | Create and export a complete small 2D game. |
| Audio | Add music, sound effects and sounds that come from objects in the world. | scaffolded / False | Audio import, playback and scene events | Saved sound settings and spatial audio work in an exported game. |
| Game screens & controls | Build game menus and connect keyboard, mouse or controller actions. | scaffolded / False | Input mapping and runtime UI | A playable game works with declared input devices and readable UI. |
| Performance & debugging | See what slows a scene down and find problems in game rules. | scaffolded / False | Runtime timing and diagnostic events | CPU, GPU, memory and errors are reported without misleading totals. |
| Teamwork & history | Review changes and safely work on a project with other people. | scaffolded / False | Stable asset IDs and conflict handling | Conflicting edits can be inspected and recovered without losing work. |
| Build & export | Export a Windows x64 adventure folder with a standalone player, native engine libraries, C++ runtime files and portable scene assets. Package loading is checked; live standalone play and clean-machine distribution remain unverified. | experimental / True | Clean-machine validation and broader platform packaging | A packaged game runs on a clean computer without the editor. |
| Windows, Linux & macOS | Create and play 2D and 3D games across the three desktop systems. | scaffolded / False | Portable editor, renderer and platform services | Editor workflows and exported games pass checks on each system. |
| VR | Create games for supported headsets and hand controllers. | scaffolded / False | Headset runtime, stereo rendering and input | An exported VR sample passes tracking and frame-time checks on named devices. |
| Graphics technology updates | Choose supported upscaling, frame generation and latency versions for each project. | scaffolded / False | Vendor integrations, compatibility checks and rollback | An update and rollback preserve project settings; native rendering remains available. |
| Steam & cloud saves | Use your Steam account and keep supported project files in Steam Cloud. | scaffolded / False | Own Steam application ID, portable projects and sync conflict handling | Private tests restore files on another machine and recover offline conflicts. |
| AI editor access | Let compatible AI assistants use the same editing tools you use. | scaffolded / False | Shared commands and full editor coverage | Each supported action works through both the GUI and documented AI interface. |
| Walk and build preview | Explore the authored scene at player height with W A S D and right-drag, then use the existing building tools. Play tests a separate gameplay copy; Pause returns to building. | experimental / True | Experimental foundation; objectives, progression, unlocks and complete visual/input acceptance remain unfinished | Shared-scene and viewpoint preservation have core checks. Live controls and usability still need verification. |
| Help and tutorials | Open Help / Tutorials or press F1, search for a lesson, and resume reading where you left off. Guidance is optional and does not change your scene. | experimental / True | Live input and visual acceptance; interactive lesson tracking and animated previews remain future work | Saved progress, catalog changes and malformed preferences have core checks. Visible walkthrough checks are prepared but not yet run. |
