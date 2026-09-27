# Starlight audit — shareable result

Mode: both

Bounded checks only. Not all features, visual states, hardware or long-soak conditions are tested. Gate 0 certification remains separate.

Feature inventory alone does not run tests. Failures and not-run checks are not passes.

- build: **pass** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\build.log
- audit-contract: **pass** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- native-core: **pass** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- native-diagnostics: **pass** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- native-no-managed: **pass** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- native-no-managed-policy: **pass** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- native-textures: **pass** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- native-classic-ui: **pass** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- native-adventure-export: **pass** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- native-session: **pass** — Executed by Gate 0 lifecycle-traced with diagnostic controls; not run twice
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- native-render-lifecycle-visible: **fail** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- renderer-fallbacks: **pass** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json
- denoiser-quality: **fail** — 
  Evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\stages.partial.json

## Explicit findings

- dynamic transform produces object motion vectors — D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\denoiser-quality\result.json
- visible-editor — D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\audit-verdict.json
- Visible editor step failed: Fullscreen live telemetry — D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\visible-editor\result.json
- Fullscreen live telemetry — D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\visible-editor\result.json

## Coverage still outside this standard pass

- Specialized denoiser and fallback checks run only in error/combined mode; missing fixture inputs are reported as not-run.
- Legacy-versus-native comparative profiling is not part of this standard Gate 0 pass.
- Human GUI appearance acceptance, all nested menus/DPI, long-soak leak freedom and all future features are not certified.

# Generated feature and evidence briefing

Declared records are not trusted proof. No last-write-wins issue closure, whole-feature completion, or binary/source equivalence is inferred.

This report is generated from the current feature model and discovered evidence. Unlinked legacy tests remain review work, not silently converted passes.

## Dynamic world physics closeout

**Feature ID:** `dynamic-world-physics-closeout`

**Implementation:** Unknown — required source support missing

**Verification:** No explicitly linked runtime evidence

**Limits:** Bounded acceptance only. Unsupported fracture uses generic debris; conservative collision, heightfield liquids and fixed fog domains remain. Cloth, soft bodies and ragdolls are absent.

**Source:** editor/cpp/adventure.hpp, editor/cpp/body_constraints.hpp, tools/checks/physics_boundary_checks.inl

## Character movement and grappling

**Feature ID:** `character-movement-and-grappling`

**Implementation:** Step 4 accepted within documented scope; current physics closeout fixtures pass

**Verification:** No explicitly linked runtime evidence

**Limits:** Source presence is not complete visible or clean-machine validation. Consult the current requirement sign-off for acceptance and remaining gates.

**Source:** editor/cpp/adventure.hpp, editor/cpp/adventure_controller.hpp, editor/cpp/grapple_motion.hpp

## Telegraph combat and combat actors

**Feature ID:** `telegraph-combat-and-combat-actors`

**Implementation:** Step 4A accepted with agreed placeholder damage/health; current exported combat checks pass

**Verification:** No explicitly linked runtime evidence

**Limits:** Damage and health values are provisional. Finished character assets and complete milestone sign-off remain deferred or open; see current evidence.

**Source:** editor/cpp/adventure.hpp, editor/cpp/combat_control.hpp

## Crowd control and boss break bars

**Feature ID:** `crowd-control-and-boss-break-bars`

**Implementation:** Implemented runtime components and integrations; exact current test outcomes and remaining acceptance are reported separately

**Verification:** No explicitly linked runtime evidence

**Limits:** Boss behavior is distinct from PvP immunity. Per-effect source and tests must be inspected; this catalogue does not certify balance or full gameplay completion.

**Source:** editor/cpp/combat_control.hpp

## Discoverable editor help

**Feature ID:** `discoverable-editor-help`

**Implementation:** Implemented text guidance; live UI acceptance pending. Not animated demonstrations, guided task detection or the full three-mode startup

**Verification:** No explicitly linked runtime evidence

**Limits:** Text guidance with saved reading positions; live appearance and input verification pending. Not interactive task tracking.

**Source:** editor/cpp/editor_main.cpp, editor/cpp/tutorial_progress.hpp, editor/cpp/tutorials.inl

## Walk and build preview

**Feature ID:** `walk-and-build-preview`

**Implementation:** Experimental code foundation; live input and visual review, objectives, experience and unlocks remain pending

**Verification:** No explicitly linked runtime evidence

**Limits:** Experimental preview. Physical input, complete visual acceptance, objectives, XP and unlocks are not established.

**Source:** editor/cpp/adventure.hpp, editor/cpp/classic_completion.inl, editor/cpp/editor_main.cpp

## Adventure gameplay foundation

**Feature ID:** `adventure-gameplay-foundation`

**Implementation:** Bounded authored rules plus the movement and combat foundation described below; not arbitrary scripting or the complete Interactive building experience

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** Collection, interaction and spherical area events with item/objective conditions and objective/door/finish actions. Shared character movement and combat now exist; final Step 4/4A acceptance remains open. No arbitrary scripting. Check current validation evidence separately.

**Source:** editor/cpp/adventure.hpp, editor/cpp/editor_main.cpp, editor/cpp/gameplay_rules.hpp, editor/cpp/scene.hpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-1.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-core
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-7.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-adventure-export
## Windows adventure export

**Feature ID:** `windows-adventure-export`

**Implementation:** Windows x64 / Direct3D 12 foundation; clean-machine, signing, other platforms and visible player acceptance remain unverified

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** Package validation does not prove visible play, clean-machine compatibility, code signing, or non-Windows support.

**Source:** CMakeLists.txt, editor/cpp/game_export.cpp, editor/cpp/player_main.cpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-7.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-adventure-export
## Native application and build

**Feature ID:** `native-application-and-build`

**Implementation:** Implemented foundation

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** Windows/D3D12 is the current build contract. Compiling successfully does not validate runtime behavior or portable packaging.

**Source:** Build-Native.py, CMakeLists.txt, editor/cpp/editor_main.cpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-3.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-no-managed
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-4.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-no-managed-policy
## Classic creator interface

**Feature ID:** `classic-creator-interface`

**Implementation:** Implemented workflow; broader polish remains bounded by tested cases

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** A catalogue entry is not proof of a working subsystem. Latest saved UI evidence is described separately.

**Source:** editor/cpp/classic_completion.inl, editor/cpp/editor_main.cpp, editor/cpp/named_colors.hpp, tools/checks/classic_ui_checks.inl

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-6.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-classic-ui
## Independent game preview

**Feature ID:** `independent-game-preview`

**Implementation:** Implemented inline workflow; exhaustive parity remains open

**Verification:** No explicitly linked runtime evidence

**Limits:** Independent views consume additional resources; this is not a demonstrated shared GPU resource pool or a packaged game exporter.

**Source:** editor/cpp/classic_completion.inl, editor/cpp/game_preview.hpp

## Scene data and compatibility

**Feature ID:** `scene-data-and-compatibility`

**Implementation:** Implemented foundation

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** This is a record/vector model, not evidence of a general archetype ECS. Saved authored state differs from a simulation checkpoint.

**Source:** editor/cpp/scene.cpp, editor/cpp/scene.hpp, editor/cpp/scene_json.cpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-1.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-core
## Scene packet ABI

**Feature ID:** `scene-packet-abi`

**Implementation:** Unknown — required source support missing

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** Native exports remain active ABI boundaries even though the managed editor was retired.

**Source:** editor/native/viewport.cpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-1.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-core
## Local automation

**Feature ID:** `local-automation`

**Implementation:** Partial product coverage: implemented local commands

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** This is local command automation. It is not an embedded AI model, cloud service, or complete GUI command parity.

**Source:** editor/cpp/command_runner.cpp, editor/cpp/commands.cpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-1.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-core
## Persistence and editing safety

**Feature ID:** `persistence-and-editing-safety`

**Implementation:** Implemented workflows; long-session memory remains a concern

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** Whole-scene history and asset portability need separate memory and distribution policies. No unlimited-history safety claim is made.

**Source:** editor/cpp/commands.cpp, editor/cpp/mesh.hpp, editor/cpp/platform.cpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-1.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-core
## Terrain authoring

**Feature ID:** `terrain-authoring`

**Implementation:** Partial terrain system: finite heightfield, no streamed world/caves

**Verification:** No explicitly linked runtime evidence

**Limits:** A heightfield cannot encode caves or stacked surfaces. Rendering currently rebuilds geometry; this is not world streaming.

**Source:** editor/cpp/geometry.cpp, editor/cpp/scene.hpp

## Object geometry and sculpting

**Feature ID:** `object-geometry-and-sculpting`

**Implementation:** Partial sculpting system: core brushes and placement

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** Do not infer topology repair, adaptive remeshing, UV authoring or a production sculpting package.

**Source:** editor/cpp/interactions.cpp, editor/cpp/mesh.cpp, editor/cpp/scene.hpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-1.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-core
## Props and effects

**Feature ID:** `props-and-effects`

**Implementation:** Implemented prop set; broader nature systems planned

**Verification:** No explicitly linked runtime evidence

**Limits:** Procedural props are not a world scatter, foliage simulation, fracture or particle system.

**Source:** deep/engine/chaos_lightning.h, editor/cpp/geometry.cpp, editor/cpp/scene.hpp

## Static model import

**Feature ID:** `static-model-import`

**Implementation:** Partial asset pipeline: static GLB preview

**Verification:** No explicitly linked runtime evidence

**Limits:** Creator static import must not inherit the separate SampleGame crowd runtime’s skinning/animation capability. Full imported material/animation authoring is not established.

**Source:** editor/cpp/assets.cpp

## Reusable assets and scene composition

**Feature ID:** `reusable-assets-and-scene-composition`

**Implementation:** Implemented bounded content workflow; thumbnails and imported-model templates remain outside scope; Windows adventure packaging is tracked separately

**Verification:** No explicitly linked runtime evidence

**Limits:** Text browser has no thumbnails. Native templates exclude imported models; portable scene folders support embedded GLB dependencies. This is editor-content reuse, not standalone game export. GUI visual acceptance and clean-machine validation remain separate.

**Source:** editor/cpp/asset_browser.inl, editor/cpp/object_template.cpp, editor/cpp/portable_scene.cpp, editor/cpp/selection_clipboard.inl

## Render ownership and updates

**Feature ID:** `render-ownership-and-updates`

**Implementation:** Implemented ownership; optimization and failure recovery incomplete

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** Resize now preserves the viewport/device and replaces size-dependent images; scene changes can still rebuild geometry. Worker ownership is not a proof of race freedom.

**Source:** editor/cpp/native_session.cpp, editor/cpp/native_session.hpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native.feature-evidence.json: fail; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=Gate 0 bounded lifecycle, debug tiers, measurements and visible stages
## Native rendering contract

**Feature ID:** `native-rendering-contract`

**Implementation:** Implemented configuration; dated visible benchmark evidence is workload-specific

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** No super resolution or frame generation is allowed for current acceptance. Native resolution does not guarantee alias-free output.

**Source:** editor/native/edge_aa.h, editor/native/viewport.cpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native.feature-evidence.json: fail; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=Gate 0 bounded lifecycle, debug tiers, measurements and visible stages
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\renderer-fallbacks.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload={'name': 'renderer-fallbacks', 'packet': 'D:\\projects\\Starlight-Engine\\audit-runs\\run-20260917-192850\\comparison.bin', 'sha256': '8c1f65801d1c6ddafb3210714102b0d4b7e8ab160066bb9f473621daf44f0810'}
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\denoiser-quality.feature-evidence.json: fail; indexed source matches; transitive build inputs not established; build identity recorded=True; workload={'name': 'denoiser-quality', 'packet': 'D:\\projects\\Starlight-Engine\\editor\\checks\\denoise\\regression\\chaos-rt.packet', 'sha256': '6751c0f9912dc75da4e3fe77f3a8e0d8d3bff3425c4fe8c06fe3b53ed6daf95f'}
## Inline hardware ray tracing

**Feature ID:** `inline-hardware-ray-tracing`

**Implementation:** Partial rendering system: implemented tiers, bounded materials

**Verification:** No explicitly linked runtime evidence

**Limits:** This is not an RT state-object/shader-table pipeline. Historical device-loss failures remain dated evidence; current throughput requires a successful run with explicit workload, native dimensions and presentation conditions.

**Source:** deep/engine/dxr.h, deep/engine/dxr.hlsl, tools/build/cmake_shaders.py

## Native temporal denoiser

**Feature ID:** `native-temporal-denoiser`

**Implementation:** Implemented algorithm; dated native-wavelet diagnostic runs passed, broader quality and stability remain unverified

**Verification:** No explicitly linked runtime evidence

**Limits:** ABI mode 0 is native wavelet. Existence of the implementation is not fresh runtime validation or proof of zero ghosting.

**Source:** deep/engine/svgf.hlsl

## Optional native Ray Reconstruction

**Feature ID:** `optional-native-ray-reconstruction`

**Implementation:** Implemented optional integration; portability and current runtime validation bounded

**Verification:** No explicitly linked runtime evidence

**Limits:** RR must remain 1:1. Vendor capability, local SDK/DLL paths and fallback behavior require explicit validation; no super-resolution authorization follows from RR.

**Source:** deep/engine/ray_reconstruction.h, editor/native/viewport.cpp

## Atmosphere and lighting

**Feature ID:** `atmosphere-and-lighting`

**Implementation:** Partial atmosphere: bounded froxels/lights, no full weather/smoke simulation

**Verification:** No explicitly linked runtime evidence

**Limits:** Bounded froxel fog and procedural skies are not a full weather or fluid-smoke solver. Raster and ray visibility differ.

**Source:** deep/engine/atmosphere.hlsl, editor/cpp/scene.hpp, editor/native/viewport.cpp

## Rigid-body simulation

**Feature ID:** `rigid-body-simulation`

**Implementation:** Bounded physics and lifecycle closeout fixtures pass; arbitrary-scale performance remains unverified

**Verification:** No explicitly linked runtime evidence

**Limits:** Async collision cooks and four-worker Jolt scheduling are not a global concurrency budget. High-density coupled performance remains unverified.

**Source:** deep/Physics/PhysicsWorld.cpp, editor/cpp/physics.cpp

## Water and lava flow

**Feature ID:** `water-and-lava-flow`

**Implementation:** Partial simulation: surface flow, not general 3D fluids

**Verification:** No explicitly linked runtime evidence

**Limits:** This is terrain-bound surface transport, not a general 3D incompressible fluid solver or guaranteed two-way pressure coupling.

**Source:** editor/cpp/flow.cpp, editor/cpp/flow.hpp

## Routes and simulation controls

**Feature ID:** `routes-and-simulation-controls`

**Implementation:** Partial movement system: authored routes and controls

**Verification:** No explicitly linked runtime evidence

**Limits:** Route playback is distinct from navigation, skeletal locomotion, collision-aware character controllers and animation graphs.

**Source:** editor/cpp/native_session.cpp, editor/cpp/scene.hpp

## Local music

**Feature ID:** `local-music`

**Implementation:** Implemented editor convenience

**Verification:** No explicitly linked runtime evidence

**Limits:** Editor music is not a spatial audio engine or exported game soundtrack system.

**Source:** editor/cpp/classic_completion.inl, editor/cpp/music.hpp

## Texture foundation

**Feature ID:** `texture-foundation`

**Implementation:** Partial asset/material pipeline

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** A BC5/BC7 loader is not a complete editor PBR material library, offline cooker or asynchronous residency manager.

**Source:** deep/engine/model/texture.cpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-5.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-textures
## Project host boundary

**Feature ID:** `project-host-boundary`

**Implementation:** Implemented host; general export/distribution incomplete

**Verification:** No explicitly linked runtime evidence

**Limits:** Creator scene Play and project-DLL execution are separate workflows. See current publication and closeout evidence for tested export cases; project-host and editor-player workflows remain distinct.

**Source:** CMakeLists.txt, deep/src/runtime.cpp, editor/cpp/projects.cpp

## Crowd runtime

**Feature ID:** `crowd-runtime`

**Implementation:** Implemented specialized runtime; scope-specific results

**Verification:** No explicitly linked runtime evidence

**Limits:** Do not attribute large-crowd figures to the editor or to independent gameplay entities; inherited results are workload-specific.

**Source:** deep/projects/SampleGame/project.cpp

## Persistent CPU workers

**Feature ID:** `persistent-cpu-workers`

**Implementation:** Implemented pool; general scheduling roadmap pending

**Verification:** No explicitly linked runtime evidence

**Limits:** This class does not implement a general task DAG, fibers or work stealing. Wider thread counts require measured gains, not utilization alone.

**Source:** deep/engine/persistent_workers.h

## Command recording and GPU lifetime

**Feature ID:** `command-recording-and-gpu-lifetime`

**Implementation:** Implemented serial submission foundation

**Verification:** No explicitly linked runtime evidence

**Limits:** Future parallel recording needs per-worker/per-slot ownership, reserved bindings and ordered resource transitions.

**Source:** deep/engine/render_device.h

## Diagnostics and existing audits

**Feature ID:** `diagnostics-and-existing-audits`

**Implementation:** Implemented tooling; validation gaps explicit

**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

**Limits:** Checks are evidence for their exact run, not blanket certification. This analyzer does not run them or change failed assertions.

**Source:** CMakeLists.txt, tools/audit/audit.cpp

- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-0.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=audit-contract
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-2.feature-evidence.json: pass; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=native-diagnostics
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native.feature-evidence.json: fail; indexed source matches; transitive build inputs not established; build identity recorded=True; workload=Gate 0 bounded lifecycle, debug tiers, measurements and visible stages
## Diagnostic benchmark path

**Feature ID:** `diagnostic-benchmark-path`

**Implementation:** Implemented diagnostic path; broader reliability validation incomplete

**Verification:** No explicitly linked runtime evidence

**Limits:** Historical renderer failures are preserved; newer Gate 0 evidence and the reliability checkpoint record an isolated output-target correction. A source scan alone does not certify matched performance or lifecycle reliability.

**Source:** deep/engine/render_device.h, editor/native/viewport.cpp


## Evidence requiring review

- audit-runs/editor-menu-comparison-20260923/camera-edge-parity-checks.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/camera-edge-parity-checks.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/character-polish-checks.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/character-polish-checks.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/character-polish-checks.json.status.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/environment-parity-checks.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/environment-parity-checks.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/inline-play-checks.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/inline-play-checks.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/live-character-checks.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/live-character-checks.json.status.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/live-character-followup-checks.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/live-character-followup-checks.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/live-character-followup-checks.json.status.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/main-menu-checks.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/main-menu-checks.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/native-unpaced-baseline/manifest.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/reference-menu-followup-checks.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/reference-menu-followup-checks.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/section-layout-checks.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/section-layout-checks.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/section-layout-checks.json.status.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/settings-panel-checks.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/settings-panel-checks.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/editor-menu-comparison-20260923/settings-panel-checks.json.status.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/async-only.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/interactive-visible.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/launched-validation.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/lifecycle-origins/lifecycle-memory.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/lifecycle-origins/session-results.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/lifecycle-pumped/lifecycle-memory.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/lifecycle-pumped/session-results.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/lifecycle-traced/lifecycle-memory.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/lifecycle-traced/session-results.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/repro-before/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/repro-fixed.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-20260922/visible-render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/audit-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/gate0-runs.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/runs.partial.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/scope.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/visible-editor/evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/visible-editor/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/visible-editor/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/visible-editor/result.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/visible-presentation/evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/visible-presentation/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-auto-fullscreen-20260923-003541/visible-presentation/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-components-20260923/component-physics/component-control.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-components-20260923/component-physics/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-components-20260923/component-renderer/lifecycle-memory.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-components-20260923/component-renderer/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-components-20260923/component-renderer/session-results.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-components-20260923/gate0-runs.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-components-20260923/runs.partial.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/debug-tier-0/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/debug-tier-0/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/debug-tier-1/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/debug-tier-1/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/debug-tier-2/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/debug-tier-2/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/debug-tier-3/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/debug-tier-3/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/debug-tier-4/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/debug-tier-4/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/gate0-runs.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/lifecycle-idle-control/lifecycle-memory.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/lifecycle-idle-control/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/lifecycle-idle-control/session-results.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/lifecycle-traced/lifecycle-memory.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/lifecycle-traced/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/lifecycle-traced/session-results.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/measurement-0/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/measurement-0/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/measurement-1/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/measurement-1/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/runs.partial.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/scope.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-validation-20260922/workload.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-visible-integrated-20260923-002728/audit-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-visible-integrated-20260923-002728/gate0-runs.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-visible-integrated-20260923-002728/runs.partial.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-visible-integrated-20260923-002728/scope.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-visible-integrated-20260923-002728/visible-editor/evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-visible-integrated-20260923-002728/visible-editor/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-visible-integrated-20260923-002728/visible-editor/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-visible-integrated-20260923-002728/visible-presentation/evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-visible-integrated-20260923-002728/visible-presentation/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/gate0-visible-integrated-20260923-002728/visible-presentation/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/binary-identities.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/comparisons.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/comparisons.partial.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/cpu-stress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-0-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-0-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-1-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-1-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-2-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-2-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-2-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/describe-2-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/findings.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/hardware.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-0-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-0-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-1-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-1-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-2-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-2-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-2-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/inspect-2-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/legacy-baselines.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-classic-ui/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-classic-ui/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-classic-ui/result.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-core/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-core/request.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-core/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-diagnostics/diagnostics.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-diagnostics/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-session/lifecycle-memory.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-session/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-session/session-results.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/native-textures/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/runs.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/runs.partial.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/scan-skipped.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/source-manifest.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/source-review-sites.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-0-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-0-0-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-0-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-0-0-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-0-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-0-1-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-0-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-0-1-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-1-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-1-0-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-1-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-1-0-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-1-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-1-1-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-1-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-1-1-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-2-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-2-0-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-2-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-2-0-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-2-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-2-1-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-2-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-2-1-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-3-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-3-0-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-3-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-3-0-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-3-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-3-1-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-3-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-3-1-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-4-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-4-0-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-4-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-4-0-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-4-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-4-1-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-4-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/tier-4-1-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-190948/workload.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/binary-identities.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/comparisons.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/comparisons.partial.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/cpu-stress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-0-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-0-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-1-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-1-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-2-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-2-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-2-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/describe-2-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/findings.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/hardware.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-0-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-0-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-1-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-1-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-2-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-2-new/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-2-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/inspect-2-old/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/legacy-baselines.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-classic-ui/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-classic-ui/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-classic-ui/result.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-core/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-core/request.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-core/result.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-diagnostics/diagnostics.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-diagnostics/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-session/lifecycle-memory.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-session/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-session/session-results.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/native-textures/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/runs.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/runs.partial.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/scan-skipped.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/source-manifest.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/source-review-sites.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-0-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-0-0-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-0-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-0-0-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-0-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-0-1-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-0-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-0-1-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-1-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-1-0-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-1-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-1-0-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-1-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-1-1-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-1-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-1-1-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-2-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-2-0-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-2-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-2-0-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-2-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-2-1-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-2-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-2-1-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-3-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-3-0-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-3-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-3-0-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-3-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-3-1-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-3-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-3-1-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-4-0-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-4-0-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-4-0-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-4-0-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-4-1-new/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-4-1-new/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-4-1-old/process.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/tier-4-1-old/render.json: Legacy/unrecognized schema; no inferred feature verdict
- audit-runs/run-20260917-192850/workload.json: Legacy/unrecognized schema; no inferred feature verdict
- migration/classic-completion-validation.json: Legacy/unrecognized schema; no inferred feature verdict
- out/classic/classic-ui-Debug.json: Legacy/unrecognized schema; no inferred feature verdict
- out/classic/classic-ui-Debug.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- out/classic/classic-ui-Release.json: Legacy/unrecognized schema; no inferred feature verdict
- out/classic/classic-ui-Release.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- out/optimization-audit/layouts.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\audit-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-0\evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-0\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-0\render.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-1\evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-1\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-1\render.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-2\evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-2\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-2\render.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-3\evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-3\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-3\render.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-4\evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-4\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\debug-tier-4\render.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\gate0-runs.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\lifecycle-idle-control\lifecycle-memory.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\lifecycle-idle-control\lifecycle-progress.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\lifecycle-idle-control\lifecycle-startup.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\lifecycle-idle-control\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\lifecycle-idle-control\session-results.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\lifecycle-traced\lifecycle-memory.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\lifecycle-traced\lifecycle-progress.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\lifecycle-traced\lifecycle-startup.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\lifecycle-traced\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\lifecycle-traced\session-results.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\measurement-0\evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\measurement-0\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\measurement-0\render.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\measurement-1\evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\measurement-1\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\measurement-1\render.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\progress.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\runs.partial.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\scope.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\visible-editor\evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\visible-editor\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\visible-editor\result.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\visible-editor\result.json.progress.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\visible-presentation\evidence-verdict.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\visible-presentation\process.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\visible-presentation\render.json: Legacy/unrecognized schema; no inferred feature verdict
- D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\native-diagnostics\workload.json: Legacy/unrecognized schema; no inferred feature verdict


# Starlight compared with Unreal Engine 5, Unity and Godot

Selected capability areas, not exhaustive engine parity. Unknown is not absent. No percentages, winner or production-readiness score is inferred.

**Competitor references reviewed:** 2026-09-23. within 90-day review window; not rechecked online this run.

UE5 rolling documentation, Unity 6000.0/Hub, Godot stable 4.x; exact installed builds not tested

Starlight descriptions come from the supplied audit snapshot. Competitor descriptions come from official documentation, not tests on this computer. Planned Starlight modes are not counted as delivered features.

## Animation workflow

**Starlight — Static model import:** Partial asset pipeline: static GLB preview
Choose a GLB model, validate it, then place and transform instances. The importer handles static mesh geometry and validates its buffers/accessors/hierarchy.
**Limits:** Creator static import must not inherit the separate SampleGame crowd runtime’s skinning/animation capability. Full imported material/animation authoring is not established.
**Verification:** No explicitly linked runtime evidence

**Starlight — Routes and simulation controls:** Partial movement system: authored routes and controls
Draw X/Z routes, set speed, Play/Pause/Reset, optionally repeat or follow a character. Escape cancels active operations before leaving fullscreen.
**Limits:** Route playback is distinct from navigation, skeletal locomotion, collision-aware character controllers and animation graphs.
**Verification:** No explicitly linked runtime evidence

**Starlight — Crowd runtime:** Implemented specialized runtime; scope-specific results
Gather/cull instances, evaluate poses and build instance buckets using specialized project code.
**Limits:** Do not attribute large-crowd figures to the editor or to independent gameplay entities; inherited results are workload-specific.
**Verification:** No explicitly linked runtime evidence

- **Unreal Engine 5:** Animation Blueprints support character animation logic. [Official source](https://dev.epicgames.com/documentation/unreal-engine/animation-blueprints-in-unreal-engine).
- **Unity 6:** Animation systems support clips and controller-based animation workflows. [Official source](https://docs.unity3d.com/6000.0/Documentation/Manual/AnimationOverview.html).
- **Godot 4:** Animation nodes and an animation editor provide animation authoring. [Official source](https://docs.godotengine.org/en/stable/tutorials/animation/).

**Next validation:** Prove imported character animation through editor authoring and standalone export. Routes and a specialized crowd runtime are not equivalent to that pipeline.

## Gameplay authoring

**Starlight — Adventure gameplay foundation:** Bounded authored rules plus the movement and combat foundation described below; not arbitrary scripting or the complete Interactive building experience
New starter adventure creates a key, door and exit level with save protection. Existing scenes can opt into walking controls; Beginner and Advanced expose different settings on the same data. Play uses WASD, right-drag and E.
**Limits:** Collection, interaction and spherical area events with item/objective conditions and objective/door/finish actions. Shared character movement and combat now exist; final Step 4/4A acceptance remains open. No arbitrary scripting. Check current validation evidence separately.
**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

- Runtime evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-1.feature-evidence.json; pass; indexed source matches; transitive build inputs not established
- Runtime evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-7.feature-evidence.json; pass; indexed source matches; transitive build inputs not established
- **Unreal Engine 5:** Blueprints provides node-based gameplay scripting. [Official source](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprints-visual-scripting-in-unreal-engine).
- **Unity 6:** C# scripting provides programmable gameplay behavior. [Official source](https://docs.unity3d.com/6000.0/Documentation/Manual/scripting.html).
- **Godot 4:** Supports GDScript, C# and native extension language options. [Official source](https://docs.godotengine.org/en/stable/getting_started/step_by_step/scripting_languages.html).

**Next validation:** Assess reusable authored rules beyond the bounded key/door/exit workflow; compare equivalent tasks, not feature names.

## Standalone builds

**Starlight — Windows adventure export:** Windows x64 / Direct3D 12 foundation; clean-machine, signing, other platforms and visible player acceptance remain unverified
Export playable game writes a new folder with a player, engine/runtime libraries and portable scene content. Existing folders are refused; missing runtime files abort the export.
**Limits:** Package validation does not prove visible play, clean-machine compatibility, code signing, or non-Windows support.
**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

- Runtime evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-7.feature-evidence.json; pass; indexed source matches; transitive build inputs not established
- **Unreal Engine 5:** Packaging creates distributable applications for supported target platforms; platform prerequisites apply. [Official source](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project).
- **Unity 6:** Build profiles configure applications for different target platforms. [Official source](https://docs.unity3d.com/6000.0/Documentation/Manual/BuildSettings.html).
- **Godot 4:** Export presets and templates produce platform-specific game builds. [Official source](https://docs.godotengine.org/en/stable/tutorials/export/exporting_projects.html).

**Next validation:** Verify Starlight on a clean machine and explicitly supported target platforms; a local package smoke test is narrower.

## Starting and learning

**Starlight — Discoverable editor help:** Implemented text guidance; live UI acceptance pending. Not animated demonstrations, guided task detection or the full three-mode startup
Help / Tutorials and F1 open optional searchable lessons, with Back, Next, Restart and saved reading positions. Close or Escape returns to creating.
**Limits:** Text guidance with saved reading positions; live appearance and input verification pending. Not interactive task tracking.
**Verification:** No explicitly linked runtime evidence

**Starlight — Classic creator interface:** Implemented workflow; broader polish remains bounded by tested cases
Use floating Builder, Characters, Environment and More menus over the canvas. Simple settings hide advanced transforms/body controls. Terrain and Shapes have dedicated gear panels; named colors and quality choices replace numeric labels.
**Limits:** A catalogue entry is not proof of a working subsystem. Latest saved UI evidence is described separately.
**Verification:** Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues

- Runtime evidence: D:\projects\Starlight-analysis\run-20260927-145737-11584-42661687\test-6.feature-evidence.json; pass; indexed source matches; transitive build inputs not established
**Starlight — Walk and build preview:** Experimental code foundation; live input and visual review, objectives, experience and unlocks remain pending
Walk at player height, right-drag to look, and use existing editing tools. Play starts a separate test; Pause restores authored objects at the tested viewpoint.
**Limits:** Experimental preview. Physical input, complete visual acceptance, objectives, XP and unlocks are not established.
**Verification:** No explicitly linked runtime evidence

- **Unreal Engine 5:** Project templates provide starting configurations and sample gameplay. [Official source](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-templates-reference).
- **Unity 6:** Unity Hub offers project templates. [Official source](https://docs.unity.com/en-us/hub/templates).
- **Godot 4:** Project Manager creates, imports and opens projects. [Official source](https://docs.godotengine.org/en/stable/tutorials/editor/project_manager.html).

**Next validation:** Test beginner task completion, discoverability and recovery with people. Documentation cannot establish which experience is easier or unique.

## Performance comparison

**Not compared.** No matched cross-engine benchmark dataset supplied. Starlight-only FPS and passing tests establish no ranking.

Required for a defensible comparison: exact engine builds, same hardware and drivers, same scene and workload, matched native resolution and visual quality, same presentation and frame-cap policy, warmup and measurement duration, CPU/GPU frame times, percentiles and raw evidence.

## Coverage still to map

The full Starlight feature reference remains authoritative for these unmapped areas. This comparison does not assume competitors lack them.

- Atmosphere and lighting
- Character movement and grappling
- Command recording and GPU lifetime
- Crowd control and boss break bars
- Diagnostic benchmark path
- Diagnostics and existing audits
- Dynamic world physics closeout
- Independent game preview
- Inline hardware ray tracing
- Local automation
- Local music
- Native application and build
- Native rendering contract
- Native temporal denoiser
- Object geometry and sculpting
- Optional native Ray Reconstruction
- Persistence and editing safety
- Persistent CPU workers
- Project host boundary
- Props and effects
- Render ownership and updates
- Reusable assets and scene composition
- Rigid-body simulation
- Scene data and compatibility
- Scene packet ABI
- Telegraph combat and combat actors
- Terrain authoring
- Texture foundation
- Water and lava flow


# Planned scope — not implementation proof

# Starlight product direction — three creation modes

Recorded from Chaos's proposal on 18 September 2026; implementation sequence authorized on 23 September below. **Status: future product direction; the complete three-mode experience is not implemented or runtime-validated.** This direction is separate from the outdated material in `Starlight Documents`. Authorization to implement does not establish completion.

## Implementation checkpoint — 23 September

Latest verification: the fresh `full-assessment-20260923-174415` run passed 551 core checks, 92 editor checks and all 63 visible walkthrough steps, including Help, asset browsing and mode round trips. This supersedes older pending-walkthrough statements below. Human usability and full physical-input acceptance remain open. The overall audit FAILED its lifecycle private-memory allowance (17.58 MiB increase against 16 MiB); handle counts and the second batch were flat. Do not call the full audit clean. The next work is memory attribution, dock readability and Help search clarity, followed by reusable events, conditions and actions shared by authoring, Play and export.

Adventure controls, a starter key/door/exit level, Windows standalone packaging, and adjustable snapping are implemented with bounded checks. A walk-and-build preview now connects first-person exploration to the existing authoring tools and a separate gameplay test copy. Its build and data/viewpoint checks pass; live input and visual acceptance remain open. This is a foundation for Interactive mode, not delivery of objectives, XP, unlocks or the complete three-mode product. See `docs/ADVENTURE-CONTROLS.md` for exact scope and evidence.

## Approved next work — 23 September 2026

Chaos explicitly authorized the following sequence: finish the audit/verification fixes first, then proceed with the shared game-creation foundations. This supersedes the earlier planning-only status. Routine implementation within this scope does not need another go-ahead. Provide concise, concrete updates; do not silently wait between small additions. Preserve unrelated work and record verification honestly.

### First: repair and verify

1. Fix the visible editor audit's startup/foreground handoff. Record the exact failed prerequisite; preserve real foreground and screenshot requirements. Rerun the failed stage rather than all previously passing workloads without a new reason.
2. Fix report freshness: make the current declared run prominent, normalize evidence paths, separate dated historical failures, and avoid claims of closure or completeness unsupported by evidence. Refresh documents using preserved results where valid.
3. Visually verify the new asset browser/type filter and Copy/Paste controls, including shared dark styling, rounded edges, windowed/fullscreen layout, and folder/file-dialog behavior. Command tests do not replace visual acceptance. If capture is unavailable, record that limitation rather than claim success.

### Then: one complete creation-to-export path

1. Implement one adventure game-type preset with player movement, camera, collision, and interaction. This is a new creator-facing game configuration, distinct from existing project-host benchmark presets.
2. Add reusable, saved gameplay rules for collecting a key, unlocking a door, triggering an action, and reaching an exit. Reopening a project must retain behavior; editor Play must execute it correctly.
3. Export a standalone playable build and validate it without relying on the editor/development environment. Existing portable scene export is content portability, not this milestone.
4. Build an Interactive-mode creation sequence using those same systems: construct a room, place a key, connect a door, test it, and export a playable result. Do not hard-code the example in place of reusable engine features or narrow the engine to SampleGame.
5. Add creation objectives, experience, and unlocks tied to meaningful building milestones. Chaos explicitly identified these as part of the desired fun. Details of progression/bypass remain design work; do not make advanced creation depend on an invented grind requirement.

### Building-tool expansion — after the bug fixes

Chaos confirmed on 23 September that the bug fixes above remain first. Expand the existing building tools afterward, alongside the creation workflow; this addition does not displace repair and verification work.

- Original foundation: fixed 0.5 m grid snapping, ground snapping, and snapping near other objects. The 23 September implementation now adds adjustable grid spacing and rotation-on-release snapping; defaults stay unchanged.
- Implemented in code: adjustable spacing and rotation increments in Advanced, simple toggles in Beginner, shared placement across insertion/paste/groups. Core placement checks pass; new control appearance and Interactive-mode access are not verified or complete.
- Beginner should offer a simple snapping toggle and understandable defaults. Advanced should expose precise increments. Both use the same underlying placement rules and preserve project settings across modes.
- Verify predictable placement and rotation, Undo/Redo, and consistent behavior between editor controls and Interactive tools before marking the expansion complete.

Interactive mode is intended as Starlight's headlining creation experience: making meaningful things should itself be enjoyable. Building, exploring, and testing should flow together. Objectives, experience, and unlocks support that experience; they do not substitute for making the basic building loop enjoyable. The resulting world remains an editable project that can become a playable exported game. This is approved direction, not a claim that the experience is already delivered.

### Startup and mode selection — 23 September 2026

Chaos requested a polished, game-like Starlight Engine logo splash followed by a mode-selection screen rather than entering the editor immediately. Smooth animation and transitions are part of the intended experience. This is approved product direction, not implemented or visually verified behavior.

- Arrange three cards left to right: Interactive, Beginner, Advanced. Each has a visual preview and a short description beneath it.
- Interactive previews building through gameplay; Beginner previews approachable creation tools; Advanced previews deeper menus and controls.
- Chaos suggested GIFs showing these experiences. Use short animated demonstrations of the actual supported experience; the final media format remains an implementation choice. Do not present planned capabilities as existing footage.
- Fade from the logo into the cards, with a brief explanation and a clear “Change modes anytime” note. Selecting a card chooses the preferred starting experience; it does not create a separate project or lock the creator into a workflow.
- Allow mode switching from within each mode while preserving the same project, authored work, and hidden advanced settings.
- Keep startup responsive: animation must not impose an artificial loading delay. Provide reduced-motion/static previews, avoid distracting simultaneous playback, and preserve the Starlight dark palette and smooth edges.

### Future menu simplification — 23 September 2026

Chaos identified the hamburger menu as cluttered and requested an eventual cleanup, potentially moving groups of options into new submenus in the top-right corner. Keep the interface simple as capabilities grow. This is recorded direction, not an implemented menu redesign.

- Proposed organization: keep project/file actions in the hamburger menu and group creation, assets, and gameplay actions into clearly named top-right menus. These categories are a starting proposal, not a settled layout.
- Use shallow groups and recognizable labels; avoid replacing one long menu with excessive icons or deeply nested menus. Beginner shows common actions first; Advanced reveals additional controls without losing settings.
- Preserve access to existing actions, shortcuts, Undo/Redo, and authored content. Check discoverability and layout in both windowed and fullscreen modes, with consistent Starlight dark styling and smooth rounded edges.
- Update menu documentation and walkthrough checks when actions actually move. Keep this future cleanup separate from outstanding live verification of the current controls.

### Game-type presets and mode continuity

Game-type selection should configure sensible supported defaults. Beginner exposes a small understandable set of adjustments; Advanced reveals deeper configuration and flexibility. Interactive makes creation itself a playable experience. All three modes operate on the same project and gameplay definitions. Switching modes must preserve hidden/advanced settings and authored content, not reset or fork it.

Prefer selecting the game type at project creation so Play and export agree. Later preset changes should preview their effects and preserve user work. Export validates the selected configuration and dependencies; selecting a genre does not automatically implement missing gameplay. Additional genres follow the proven first path.

### Non-negotiable baseline: confidence, clarity, and control

Chaos explicitly added these three principles to the no-compromise baseline, alongside native performance, responsiveness, and visual polish. They are acceptance requirements for future work, not claims that all current interfaces already meet them.

- **Confidence:** creators can understand what an action will do, see its result, and recover from mistakes where applicable. Help is available without embarrassment or an assumed level of expertise; Advanced users retain access to basic explanations.
- **Clarity:** controls and lesson titles use understandable language. A clearly labeled Help/Tutorial entry point remains visible and consistently located in Interactive, Beginner, and Advanced modes. Restarting or re-enabling tutorials must not be buried in nested menus.
- **Control:** tutorials can be skipped, stopped, resumed, or re-enabled easily. Provide a catalog of clearly named sections so creators can jump directly to what they need rather than replay everything. Preserve work and learning progress; help must not force an unwanted workflow.

Tutorial access must remain available in every mode, with relevant lessons for that mode and access back to fundamentals. Accessibility must not become interruption: keep guidance optional and avoid repeated unsolicited prompts. Verify discoverability by checking that someone who skipped the tutorial can find help, select a particular lesson, and return to creating without losing work in each mode.

### Three practical design rules

Implementation update: the native editor now includes a persistent labeled Help / Tutorials button and F1 access, eight searchable text lessons, Back/Next/Restart, Close/Escape, and per-user saved lesson positions. It uses shared dark panel controls and is independent of Beginner/Advanced/walk-and-build state. This is optional reference guidance, not an interactive tutorial overlay or automatic task completion tracker. Core checks pass (551 checks in tutorial-progress-20260923-171745), including saved-progress recovery and stable lesson IDs. The editor builds; eleven new visible walkthrough checks are prepared but have not run, and live appearance/input verification remains open. Saving preferences failing leaves guidance usable for the current session and reports that limitation when navigating.

These rules turn `STARLIGHT-CORE-PILLARS.md` into implementation and review criteria. They describe required behavior, not completed verification.

1. **Help is always one clear action away.** Keep a visibly labeled Help/Tutorial control in a consistent place across all three modes. It opens a lesson catalog with understandable titles, direct section selection, and Resume. Skipping guidance must never hide the way back. Verify with a fresh-user journey: skip the tutorial, find a specific lesson, and return to the project without losing work. Guidance stays optional.
2. **Changing the experience preserves the creation.** Switching modes preserves the project, selection where applicable, authored content, and hidden Advanced settings. Editing actions provide predictable Undo where applicable; actions that could discard work explain the consequence before it happens. Verify mode round trips with actual edits and Advanced settings, plus Undo/Redo and save/reopen. Future mode controls must not imply capabilities that are not implemented.
3. **Every interaction must feel immediate and look finished.** Menus use clear labels and shallow groups; controls keep consistent dark styling, readable text, and smooth edges in both window modes. Switching menus and scrolling must not flash unfinished backgrounds or introduce avoidable waits. Animation explains a transition without delaying input or startup and supports reduced motion. Measure responsiveness on a stated workload and inspect the visible result; a successful build or check count alone does not establish polish. Preserve the intentional 500 FPS ceiling and the existing native-performance requirements.

### Acceptance requirements

- A nonprogrammer can build the small example through the editor, save/reopen it, play it, export it, and give the result to someone else to launch.
- Native performance, responsiveness, visual polish and Starlight dark colors remain mandatory. Preserve the intentional 500 FPS ceiling; no artificial active-work delays, upscaling or generated frames used to disguise poor native performance.
- Maintain scene integrity, predictable Undo where applicable, and compatibility with existing content. Validate focused changes; preserve failed evidence.
- User-facing goals, implemented code, verified behavior, visual acceptance, known defects and future plans remain separately reported.
- Complete Interactive/Beginner/Advanced modes are not yet delivered. This is the approved implementation direction and completion bar, not a feature-complete claim.

## The idea

### Engine scope and SampleGame — Chaos's clarification, 18 September 2026

**Starlight is intended as a general-purpose engine, not an engine built only for SampleGame or its nightclub slice.** SampleGame is a separate game project powered by Starlight. Its opening can demonstrate engine capabilities and inform development priorities, but must not redefine or replace the broader engine product direction.

Future assistants must preserve this distinction when reading slice plans or estimating delivery. Scene-specific content and choreography belong to SampleGame; reusable engine capabilities belong to Starlight. Do not present a special-case scene implementation as completion of a general engine system, or silently discard wider requirements to meet a showcase deadline. Earlier suggestions to bound the slice were planning proposals, not authorization to narrow Starlight's purpose.

Building the engine out remains the broader objective. This clarification does not assert that the engine is complete, define a final feature-completeness checklist, or require every future capability to be finished before any SampleGame milestone. Engine milestones and game-slice milestones need separate scope and evidence.

Starlight should offer three ways to create. Interactive mode is a building game made with Starlight's own capabilities: the user builds inside a playable experience, and Starlight reconciles that work into the environment or project being developed. In Chaos's words, it is “a game where you build games.” The aspiration is to build freely and bring the result into the project at any point.

| Mode | User's intended experience | Status |
|---|---|---|
| Interactive | Create through a playable building experience; reconcile the construction into the current environment/project. | Proposed. Exact interaction, reconciliation and gameplay-authoring scope remain to be designed. |
| Beginner | Tutorials, basic controls and guided creation, building on the existing approachable interface. | Proposed complete mode. Current simple controls are a foundation; a complete tutorial curriculum is not established by the analysis. |
| Advanced | A development experience for experienced developers. | Proposed complete mode. Existing advanced controls are a foundation, not proof of a delivered separate mode. |

The original spoken proposal did not specify progression locks, VR, multiplayer or AI generation. Chaos's subsequently uploaded sculpting proposal explicitly adds a terraforming tech tree, procedural/AI tools, VR and collaboration as ideas for assessment. See the [complete feasibility and sequencing review](D:/projects/Starlight-Engine/STARLIGHT-SCULPTING-FEASIBILITY-2026-09-18.md). These are proposed extensions, not delivered functionality or a finalized requirement to gate ordinary developer tools behind progression.

## What reconciliation needs to mean

The confirmed intent is that construction in Interactive mode becomes useful project/environment content. Merely walking through a preview would not fulfill that intent.

The following is a **proposed interpretation for later design**, not a settled specification: all three modes work on the same underlying authored project. A user could construct a bridge interactively, refine its materials or behavior in Advanced, and return to Interactive without losing those refinements. Beginner would teach the same concepts through simpler controls.

Design questions remain open:

- Do edits update the project immediately, or become a reviewed, undoable commit? What does switching modes do with unsaved work?
- Which data can be created and changed interactively: geometry, terrain, materials, physics, scripts, game rules, UI, audio? Environment construction alone does not establish complete game creation.
- How are intentional construction edits separated from temporary physics, damage or other simulation changes?
- How are advanced properties preserved when a simpler mode cannot display or edit them?
- Does reconciliation preserve editable objects and relationships, or create derived assets? How do save, undo and conflict handling work?

“Build whatever we want” describes the ambition; supported content types and limits must be defined and tested before claiming unrestricted creation.

## Comparison research — 18 September 2026

**Finding: the broad concept has strong precedents. Do not claim that no other engine/platform offers it, or that Starlight invented game-like game creation.** This is a focused official-source comparison, not an exhaustive survey of all engines, plugins and research. Documentation establishes the capabilities described below; no competitor was installed or tested for this review.

| Engine or platform | Relevant documented capability | Relationship to the proposal |
|---|---|---|
| Unreal Engine | Play/Simulate workflows let developers modify actors and retain supported edits with Keep Simulation Changes. [Epic documentation](https://dev.epicgames.com/documentation/unreal-engine/playing-and-simulating-in-unreal-engine) | Existing precedent for bringing some running-world changes back to authoring. This workflow alone does not establish a complete building-game interface. |
| Fortnite Creative + Unreal Editor for Fortnite (UEFN) | Creative supports building/editing/saving inside Fortnite. Epic describes Live Edit as letting Creative changes update a UEFN project while playtesting. [Fortnite developer tools](https://www.fortnite.com/developer/tools) | The closest documented overlap with playable construction feeding a more powerful editor. UEFN targets Fortnite; distinguish it from stock Unreal Engine and from a general-purpose independent game project. |
| Unity | ProBuilder provides geometry construction and level-design tools inside the editor. [Unity ProBuilder manual](https://docs.unity3d.com/Packages/com.unity.probuilder@6.0/manual/index.html) | Relevant authoring tools, but this source does not demonstrate the proposed integrated three-mode building-game experience. It also cannot establish that no Unity plugin or Unity-built product supplies one. |
| Godot | Godot 4.4 introduced embedded game windows and interactive in-game editing, including selecting scene elements and exploring with the camera while running or paused. [Godot release documentation](https://godotengine.org/releases/4.4/) | Real overlap in interactive runtime inspection/editing. The cited feature is not evidence of a complete building game, automatic persistence of every runtime change or the proposed three-mode project workflow. |
| Dreams | Creation tools let people make games and other content, with learning material and sharing inside a game/creation platform. [PlayStation's Dreams page](https://www.playstation.com/en-gb/games/dreams/) | A direct precedent for the broad “game where you build games” concept. It does not establish the same native Starlight project model. |
| Resonite | In-session development tools create worlds and interactive props; ProtoFlux supports game/system logic with immediate feedback and collaboration. [Resonite features](https://resonite.com/features) | A strong precedent for building and programming within the world itself. General independent game export or parity with Starlight's proposed project workflow is not established by this source. |

## Potential distinction and honest positioning

The opportunity is the quality and integration of the experience: enjoyable in-world construction, approachable teaching, and advanced development as first-class ways of working on one editable general-purpose project. Keeping advanced work intact across mode changes would make that promise concrete. This is a differentiation hypothesis to validate, not a verified unique market position.

Suggested wording for the **vision**: “Starlight aims to let you build through play, learn through guided creation, and develop with advanced tools—within the same project.”

Avoid “the first engine where you can build games inside a game,” “no other engine has this,” and “already implemented.” A future comparison should demonstrate actual workflows, preservation of project data, supported game logic and distribution boundaries before making stronger claims.

## Relationship to current Starlight

The current knowledge snapshot describes scene placement, terrain/sculpt tools, simple/advanced control disclosure, persistence and undo, physics/flow, preview sessions, and a separate project host. Those are possible building blocks. Existing preset reconciliation in the project-host workflow is a different feature and must not be mistaken for the proposed Interactive mode. Existing Play preview is likewise not proof of project-authoring round trips from a building game.

Keep this direction separate from the source-supported feature count and saved test outcomes. The analysis tool carries a dated copy of this document into its knowledge pack so future assistants understand both what exists and what Chaos wants to create next. Update this document when Chaos refines the idea; implementation status needs separate source and test evidence.


# Planned scope — not implementation proof

# Sculpting and Interactive mode proposal: feasibility and sequencing

Assessment date: 18 September 2026. This is an engineering assessment, not an implementation commitment or a benchmark. All 13 pages of Chaos's uploaded *Starlight Engine: Next-Generation In-Engine Sculpting Tool Proposal* were read, including its four pillars, future concepts, eight-step workflow and four-phase timeline.

Source PDF: [Uploaded proposal](C:/Users/Chaos/.codex/codex-remote-attachments/01a0b2e6-f2b0-7f11-98bc-a9469bd78d7b/0708EDE5-55E4-433A-A5C1-4F6E0899E92D/1-Starlight-Engine-Next-Generation-In-Engine-Sculpting-Tool-Proposal.pdf).

Current-engine baseline: [knowledge pack](D:/projects/Starlight-analysis/knowledge-20260918-v3/STARLIGHT-START-HERE.md), source snapshot 18 September 2026, with saved runtime evidence from 17 September. This review used that reusable knowledge pack rather than repeating a source audit. No runtime tests or engine implementation were performed for this assessment.

## Overall judgment

The core vision is feasible: sculpt directly under game lighting, create organic starting shapes, and use a playable authoring experience to change a persistent project. Most individual techniques are established engineering. The complete document combines several major products: a sculpting/asset suite, a terraforming game, a character pipeline, an AI toolset, a VR editor and a networked simulation platform.

The proposed twelve-month schedule is not a supported estimate for all of that at production quality. A tightly selected subset could be a twelve-month objective, depending on staffing and prototype results. For a small team, the entire vision should be treated as a multi-year program with independent releases. Several numerical promises must become measured targets or be replaced, rather than scheduled as guaranteed deliverables.

## Current starting point

Starlight already has basic object sculpting (Pull, Push, Smooth, Flatten), finite heightfield terrain brushes/stamps, static GLB preview, authored scene persistence/undo, native preview, Jolt integration and limited terrain-bound liquid simulation. These are useful foundations.

The current knowledge pack does not establish adaptive topology, multiresolution layers, volumetric terrain, character authoring/rigging, UV authoring/baking, local generative AI, OpenXR, collaborative editing, navmesh agents or a work-stealing fiber scheduler. The separate crowd runtime does not establish an animated character creation pipeline or autonomous ecosystem. Existing project preset reconciliation is not the proposed reconciliation of playable world edits.

Latest saved rendering tests failed with device loss; scene lifecycle also retains a handle-count failure. Those results do not mean the whole editor is unusable, but they prevent credible performance promises for substantially heavier editing workloads.

## Placement within the product

Use one shared geometry/asset/terrain authoring core. Beginner, Advanced and Interactive should invoke the same validated edits, persistence and undo operations, through different controls. This is the recommended architecture, not an existing implementation.

- **Beginner:** guided basic brushes, preset organic shapes, simple symmetry, safe defaults and tutorials.
- **Advanced:** topology controls, sculpt layers, modifiers, UVs, baking, rigs, materials and diagnostics.
- **Interactive:** avatar or free-camera construction, tools represented as playable actions, optional progression, and explicit ways to retain or discard simulation changes.
- **Optional systems:** local AI, VR, networking and remote computation. None should be required to save or reopen ordinary assets.

The uploaded PDF explicitly adds progression, VR, AI and multiplayer ideas that were unspecified in the earlier spoken three-mode proposal. They are now recorded proposals, not inferred requirements or delivered capabilities. My recommendation is to make progression an optional game experience and keep ordinary authoring tools available in Beginner/Advanced; Chaos has not yet decided that policy.

## Feasibility inventory

Stages below refer to the sequence in the next section. Difficulty describes production integration, not whether a demonstration is possible.

| Proposal area / PDF section | Assessment and dependency | Product home / earliest sensible stage |
|---|---|---|
| Half-edge mesh, local splitting/collapsing/flipping, brush/curvature density (3.1–3.2) | Feasible, substantial geometry work. Preserve boundaries and attributes; validate manifoldness and degenerate triangles. Local operation costs do not establish total brush latency or safe concurrent mutation. | Shared geometry core; Advanced controls; C. |
| GPU displacement and normals (3.4) | Feasible after correct edit ownership and region tracking. Begin with a correct CPU topology path and accelerate measured bottlenecks. Hardware tessellation alone does not create persistent editable topology. | Geometry/renderer interface; C. |
| Multiresolution and additive/subtractive/clamped layers (3.3) | Feasible but needs stable correspondence. Arbitrary remeshing invalidates vertex-indexed deltas. Freeze a topology or reproject when changing it; some transitions may be destructive. | Advanced sculpting; C–D. |
| SDF primitives, smooth unions and CSG (4.1) | Feasible in bounded objects. Store an editable construction recipe and derive meshes. Start with simple volumes before large terrain. | All modes through shared assets; C. |
| Dual Contouring, watertight output and sharp features (4.1) | Feasible with explicit topology guarantees, boundary handling and tests. Ordinary Dual Contouring is not automatically manifold. | Geometry core; C. |
| Curves, variable-radius lofts, horns/limbs (4.2) | Feasible and high-value early organic tools. Generated shape curves are not yet animation rigs. | Beginner presets, Advanced controls, Interactive tools; B–C. |
| Symmetry, radial symmetry, lattice and modifier stack (4.3) | Basic symmetry can arrive early. Full stack needs dependency tracking, stable references, caching and clear topology-changing boundaries. | Shared authoring; B then C–D. |
| Rocks, tree trunks, anatomy blocks (4.3) | Feasible procedural assets. Fractured appearance is separate from dynamic fracture; trunk generation is separate from vegetation simulation. | Asset generators; B–C. |
| Auto-rigging, posing and IK (4.2, 6.2) | Template rigs feasible; general arbitrary-creature rigging is much harder. Requires skeleton authoring, skin weights, deformation preview and animation integration. | Character pipeline, primarily Advanced; D. |
| Local inference, text/image alphas (5.1–5.2) | Feasible optional module after finding suitable models, weights and distribution terms. Start with scalar height stamps, preview and explicit Apply. Runtime integration does not create a suitable trained model. | Asset/detail tools; D pilot. |
| Seamless vector displacement and neural stroke variation (5.2–5.3) | More uncertain than image generation. Need geometric meaning, projection direction, seam handling, repeatability and quality evaluation. Saving a low-precision image as 32-bit does not add detail. | Optional Advanced R&D; D–F. |
| Curvature/anatomical alignment (5.3) | Geometric stroke orientation is feasible. Surface curvature alone cannot reveal muscle stress or anatomical intent; that needs rig/semantic input or a model. | Advanced detail tools; D, anatomical version later. |
| Retopology, UVs, LOD and map baking (5.4, 7) | Established but major asset-pipeline work. Automatic quads do not guarantee animation-friendly edge flow; allow manual correction. Normal/AO/height/curvature derive from geometry; roughness needs material information or an estimate. | Shared asset pipeline, Advanced controls; D. |
| Avatar/free-camera editing and project reconciliation (6.1) | Feasible earlier than the full sculpting suite. Needs navigation, selection, edit transactions and a distinction between authored edits and temporary simulation. | Interactive mode; B. |
| Terraforming milestones and tech-tree tiers I–IV (6.2) | Progression logic itself is straightforward. Fertility, watershed, tectonics, muscles, fur and autonomous creatures are separate dependencies. Start with measured terrain edits and simple goals; do not require a planetary simulator. | Optional Interactive game content; B prototype, E expansion. |
| Impacts, craters, raised rims and ejecta (6.3) | A heightfield crater plus bounded debris/effects is a practical first version. Caves, overhangs and true volumetric excavation require a new representation and collision pipeline. | Terrain/gameplay; B effects, E volumes. |
| Geological layers, seams, fossils and crafting resources (6.4, 6.6) | Feasible using authored/procedural resource layers and rules. Excavation must agree with rendering, collision and persistence. It need not be a geological simulator. | Interactive content plus volumetric terrain; E, simple surface prototype earlier. |
| Thermal conversion, aquifers, lakes, hazards and updrafts (6.5) | Rule-based local state changes are feasible. Coupled heat/groundwater/pressure/atmosphere simulation is a much larger numerical project. Current liquids do not establish this. | Environmental gameplay; E approximations, F scientific simulation if desired. |
| 6-DOF VR sculpting and terraforming (6.6) | Feasible after desktop interaction stabilizes. Needs stereo rendering, OpenXR input, UI, comfort, picking and measured frame times. Standard vibration feedback is not physical resistance. | Optional presentation/input layer; F. |
| VR playground during compilation (6.6) | Feasible using a last-good runnable snapshot and isolated background builds. New code is unavailable until built; preserving frame time may require throttling/pausing build or bake work. Queue priorities alone cannot guarantee it. | Build/session tooling plus VR; desktop snapshot prototype before F. |
| Remote Linux simulation at 120 Hz (6.6) | Possible architecture, substantial new work: headless portable simulation, state replication, latency handling and partitioning. A server tick rate is not a WAN round-trip guarantee. Interconnect transport is not a ready simulation service. | Separate networking/simulation program; F. |
| Autonomous erosion/ecology/settlement agents (6.6) | Bounded procedural background jobs are feasible before intelligent agents. Need edit ownership, cancellation, budgets and reviewable changes. Coupled ecology and learned autonomy expand scope substantially. | World-generation tools; E bounded jobs, F autonomy. |
| 100+ NPCs per encampment and changing navmesh (6.6) | Plausible target, workload-dependent. Build agent behavior/navigation, local invalidation and collision synchronization first. Instance rendering counts cannot validate behavior/pathfinding cost. | Gameplay/navigation; E–F. |
| Creature anatomy, fur, tails/ears, flesh and face shapes (6.6) | Template anatomy and secondary bones can be bounded. Groom authoring/rendering, soft bodies, skinning and facial shapes are independent projects. | Character pipeline; D templates, F full suite. |
| Multi-user editing with spatial locks (6.6) | Feasible after single-user transactions work. Locks need authority, leases, disconnect recovery and cross-boundary edit rules. Locks alone do not guarantee conflict-free operation. | Collaboration; F. |
| CPU neural deformation persistence (6.6 technical note) | CPU inference is possible. The combined guarantee of arbitrary exact geometry, sub-kilobyte codes, tiny memory and sub-millisecond reconstruction is not viable as a universal requirement. Use exact conventional persistence first. | Persistence core in A–B; optional compression research F. |

## Recommended development sequence

**A. Establish a dependable editing baseline.** Resolve the measured device-loss/lifecycle failures; measure frame time and memory; define geometry ownership, persistent IDs, versioned saves and bounded undo. Test edit/cancel/undo/reopen, not just rendering. Decide how object meshes, heightfields and future volumes remain distinct representations. Do not make a fiber scheduler or mesh-shader rewrite a prerequisite without profiling evidence.

**B. Deliver one complete playable authoring loop.** Use existing terrain/shapes to build and deform a small scene, enter it, create a crater or move an object, keep selected changes, undo them, save, close and reopen. Add approachable brush improvements, simple generators and a short guided challenge. Preserve Advanced properties when switching modes. Completion means the persisted project agrees with the user's edits and collision, not just an attractive demo.

**C. Add organic geometry capability.** Prototype bounded SDF clay construction and adaptive mesh sculpting as separate paths with a deliberate conversion between them. Add curve lofts, symmetry, brush-local remeshing, attribute transfer and acceleration structures. Establish a measured mesh-size envelope on named hardware; do not begin by promising 50 million polygons.

**D. Turn sculpts into usable game assets.** Add multiresolution/layers after the topology contract; then retopology/UVs, baking, runtime meshes/materials/LOD/collision and template rigs. Finish one complete static asset path before claiming the full animated-creature workflow. Optional scalar AI alpha generation can be tested alongside this stage without blocking it.

**E. Expand Interactive into a terraforming game.** Add chunked volumetric terrain where caves/strata require it, safe collision refresh, resource revelation, progression, rule-based material transitions, local water behavior, foliage and navigation. Use constrained simulations first. Goals involving global equilibrium need separately defined rules and scope.

**F. Add optional advanced programs separately.** VR, background-build playgrounds, multiplayer, remote simulation, full creature/groom tools and advanced neural systems should each earn their place through prototypes and benchmarks. They are not one final miscellaneous task, and need not ship together. Neural compression must never be the only way to reopen user work.

These stages are dependency gates, not calendar promises. Basic usability and an Interactive vertical slice are the first useful releases; professional asset creation is a sequence of substantial milestones; the complete ecosystem/VR/distributed/AI vision is a longer-term program. Staffing, target hardware, world sizes and fidelity choices are still unspecified, so more precise delivery dates would be invented.

## Corrections needed before treating the proposal as a specification

1. **Competitor premise (pp. 1–2):** Unreal already exposes a Dynamic Sculpt tool with remeshing. Nanite limitations cannot establish that Unreal lacks dynamic sculpting. The unique positioning claim needs revision. [Epic Dynamic Sculpt API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/MeshModelingTools/UDynamicMeshSculptTool), [Epic Modeling Mode description](https://dev.epicgames.com/documentation/fortnite/modeling-mode-in-unreal-editor-for-fortnite).
2. **Topology and layers (pp. 3–4):** Local connectivity edits, subdivision layers and procedural modifiers need explicit compatibility rules. The proposed freeze into a hierarchy in workflow step 4 is a sensible boundary; returning to arbitrary remeshing needs reprojection or a warning. [Blender multiresolution reference](https://docs.blender.org/manual/en/4.4/modeling/modifiers/generate/multiresolution.html).
3. **Guaranteed manifold output (p. 4):** Original Dual Contouring can produce non-manifold surfaces; select a constrained/manifold method and test it. [Manifold Dual Contouring research](https://www.cs.wustl.edu/~taoju/research/dualsimp_tvcg.pdf).
4. **50 million polygons at 60+ FPS (p. 4):** Treat as a stretch benchmark with named hardware, brush workload, ray-tracing settings, collision policy and memory budget. For scale, 50 million triangles using three 32-bit indices already require 600 MB just for indices. Geometry, adjacency, layers, undo and acceleration structures add substantially more. CPU picking structures, physics structures and DXR acceleration structures are separate costs. Topology changes cannot generally use the same cheap DXR update path as fixed-topology vertex movement. [DXR specification](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html).
5. **AI implementation and timing (pp. 5–6):** Offline inference is feasible, but model choice, weights, supported operations, GPU memory and quality determine latency. DirectML does not guarantee Tensor Core use or sub-second results. Current ONNX Runtime guidance places DirectML in sustained engineering and recommends WinML for new Windows projects; compare the supported native integration options when implementing. [ONNX Runtime DirectML guidance](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html).
6. **Exact neural saves (p. 10):** A fixed-size latent has finitely many values; it cannot represent every larger arbitrary geometry state exactly. Compact procedural craters can store seed/parameters/operation history, but that is a restricted construction recipe, not general lossless neural compression. If a model approximates a shape, store exact residuals or preserve canonical geometry, including the shared model/version costs. Reconstruction has to account for the output geometry as well as the latent. Deterministic decoding means repeatable, not necessarily identical to the original.
7. **VR during heavy work (p. 8):** 90 Hz allows about 11.1 ms per frame; 120 Hz about 8.3 ms. Background compilation, GPU baking and AI compete for resources. Reserve capacity and reduce/pause background work when necessary. Ordinary OpenXR vibration is not a force-feedback mechanism. [OpenXR haptics](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrHapticVibration.html).
8. **Existing architecture (pp. 8–12):** The PDF's work-stealing fiber system, neural runtime, mesh-shader authoring pipeline and other present-tense statements describe proposals, not verified Starlight features. C++/DX12 alone does not establish them. The 12-month table also omits much of the expanded character, VR, network, navigation and persistence scope; it cannot be read as a delivery plan for the whole document.

## First milestone I would choose

Build a small environment using existing tools; enter Interactive; sculpt a crater and place an object; choose which changes become authored content; switch to Advanced and refine it; return to Interactive; save, undo and reopen without losing either mode's work. Give Beginner a short tutorial for that same loop.

That proves the central Starlight promise early. Organic sculpting, better assets and terraforming systems can then extend a trustworthy workflow rather than postpone the first useful experience until every research feature exists.
