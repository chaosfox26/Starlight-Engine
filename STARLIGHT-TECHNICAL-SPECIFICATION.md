> **Newer implementation:** native-resolution Ray Reconstruction and the temporal wavelet fallback are documented in [editor/DENOISING-RELEASE.md](editor/DENOISING-RELEASE.md). This file describes the earlier checkpoint where denoising claims differ.

# Starlight Engine and Creator Suite
> **Latest implementation (2026-09-17):** multi-tier DXR 1.1 is now implemented. See [DXR-RELEASE.md](editor/DXR-RELEASE.md) for code architecture, verified tier behavior, native performance, and reference-rendering limits.
> **Newer implementation:** the chaos/HDR/physics release adds capabilities beyond this snapshot. Read [CHAOS-RELEASE.md](editor/CHAOS-RELEASE.md) for delivered changes, tests and remaining limits.
## Current-state technical architecture and capability specification

**Snapshot:** 2026-09-16, after the `world-quality-release` editor update.  
**Audience:** Gemini or another outside technical reviewer assessing architecture, workflows and development priorities.  
**Purpose:** describe the implementation that exists, identify its boundaries, and provide a reliable basis for analysis. This is not an aspirational product specification.

## Reading conventions and evidence scope

- **Implemented:** an execution path exists in the current source/build.
- **Verified:** supported by the saved checks or visual review identified below; verification is limited to those cases.
- **Experimental:** functional but limited, approximate, or lacking production-scale validation.
- **Planned / absent:** not delivered through the current creator workflow.
- **Proposed:** a recommendation in this document, not an approved commitment or existing feature.

This specification combines current source inspection with saved build, test and handoff records. No new engine benchmarks were run to produce this document. It is a targeted architecture review, not an exhaustive security, correctness or performance audit of every source file. Local paths identify evidence on the creator's Windows machine; giving this document to a cloud AI does not give that AI filesystem access.

**Critical corrections to terminology in the supplied outline:**

| Suggested term | Actual current implementation |
|---|---|
| Higher-resolution terrain textures | Native viewport resolution was corrected; terrain has procedural shader detail and interpolated vertex colors. No photographic texture pack was installed. |
| Multi-layer biomes | Five paint categories with color/material interpolation; no biome rules, splat-map authoring or ecosystem simulation. |
| Procedural scatter | Procedural geometry inside a manually placed object, including pebble clusters and grass tufts. No general world scatter brush. |
| Dynamic grass blades | Actual blade geometry, but no wind animation, bending or interaction. |
| Surface-following character navigation | Ground snapping exists during editing; route playback interpolates X/Z and does not continuously sample terrain height. |
| Lava crack displacement | Animated shader color/emission patterns. Cracks do not displace geometry. |
| Approximate soft shadow maps | Soft shadow approximation from spherical object bounds; no shadow-map render pass. |
| Ambient occlusion | No editor SSAO/GTAO pass or geometric AO bake. Hemisphere lighting and procedural relief are separate effects. |
| Timeline / animation pending queues | Playback clocks and UI/render update queues exist. No animation timeline, clip queue or animation graph is delivered. |

# 1. Core Architecture and Runtime Performance

## 1.1 Product intent and current deployment boundary

Starlight is intended to become a free, voluntarily donation-supported, publicly readable engine and creator suite. The UX goal is approachable, playful world creation inspired by building games and RTS controls: place objects, draw movement routes, sculpt a world, and test it without requiring code for basic operations.

The delivered editor is a **Windows desktop application using C#/.NET 10 WPF with an embedded native C++ DirectX 12 renderer**. The broader project runtime is C++. Windows/macOS/Linux, 2D, VR, complete game export and full provider-independent AI workflow parity are future goals. They are not all supported today. The existing files do not establish that a public repository, final licensing scheme or donation infrastructure has been shipped.

## 1.2 Three distinct layers and an older compatibility host

Workspace root: `D:/projects/Starlight-Engine/`.

| Layer | Location | Responsibility |
|---|---|---|
| Original GameProject reference | `<separate-game-project>/starlight-realms/native-dx12/` | Preserved renderer/runtime reference and original tools. Not overwritten by the latest editor changes. |
| Modular Starlight runtime | `deep/` | C++ project host, shared rendering implementation, project DLLs, settings reconciliation and controlled engine experiments. |
| Creator editor | `editor/` | WPF UX, editable scene data, geometry creation, terrain, sculpting, media and native preview integration. |
| Early compatibility host | Root `bin/`, root `projects/`, `.starlight.json` manifests | Earlier process-launch separation experiment. Not the same architecture as the later `.star`/DLL runtime. |

The root `README.md` describes the **older first separation boundary**. Some `deep/` overview files are effectively empty. Current code, project manifests, editor release notes and recovery handoffs take precedence over those older summaries.

Current editor executable:

```text
D:/projects/Starlight-Engine/editor/bin/world-quality-release/Starlight.Editor.exe
```

The actual desktop shortcut at `C:/Users/user/OneDrive/Desktop/Starlight Editor.lnk`, the legacy local desktop shortcut, and `Launch-Starlight-Editor.cmd` point to this build. Prior builds remain on disk; their presence does not indicate which build is current.

## 1.3 Modular C++ project host

`deep/src/runtime.cpp` implements the later project host. It reads a `.star` project and a `.startest` test preset, selects a named scene, reconciles settings, and loads the project's DLL using `LoadLibraryExW`. It resolves the exported `StarlightRunProject` entry point and invokes it. The project module is unloaded after that call returns.

Current examples:

- `deep/projects/Empty/Empty.star` → `Empty.dll`, scene `empty`.
- `deep/projects/GameProject/GameProject.star` → `GameProject.dll`, scenes including `crowd`, `club`, and `live-bar`.
- The GameProject manifest still references model and texture directories in the original workspace by absolute path.

The current `.star` examples use schema version **3**. A project declares its module, common arguments, named scenes, default scene and supported test capabilities. The crowd scene declares checks such as zero culled instances and at least 100,000 skinned instances. Those declarations are acceptance requirements, not proof that every launch satisfies them.

The WPF `ProjectWindow` can select a project, select a scene, load/edit a preset, reconcile a working copy, save the result and launch `deep/bin/Starlight.exe`. It uses `ProcessStartInfo.ArgumentList` and first requests a launch plan. Reconciliation runs on a copied preset and has a bounded timeout.

**Integration boundary:** native editor scene Play uses `Starlight.Viewport.dll`; it does not automatically convert a `.starscene` into a complete `.star` project or export an installable game. These two launch paths share renderer lineage but are not interchangeable.

## 1.4 Creator application and native bridge

```text
WPF controls and input
    → SceneData / Character / TerrainData / SculptMesh / LiquidSources
    → WPF Model3D geometry assembly
    → NativeScene V4 binary packet
    → P/Invoke → Starlight.Viewport.dll
    → RenderDevice → DirectX 12 → swap chain

Project/preset panel
    → deep/bin/Starlight.exe + .star + .startest
    → project DLL → project runtime
```

`NativeViewport.cs` is an `HwndHost`. It creates a native child window and runs a dedicated managed background thread that owns its native rendering context. Scene updates are passed through an atomically exchanged pending packet; camera state is also exchanged between threads. This decouples rendering from WPF control updates, but it is not a general engine-wide task graph.

Each viewport owns its renderer/context. Resizing recreates the native context and resubmits the current scene. The windowed external Play viewport is a second rendering context, not merely another view into one shared GPU resource pool. Native errors are exposed as a failure string and displayed in editor status. Device-loss recovery beyond the existing lifecycle is not established here.

The native child uses `SS_NOTIFY`. An earlier missing flag caused Windows hit testing to skip the viewport even while synthetic direct-message tests passed. The flag and a hit-test regression check remain essential.

## 1.5 Scene transport and GPU resource reuse

The current packet magic is `0x34564c53` (**NativeScene V4**). The native reader also accepts V1–V3; these packet versions are separate from JSON scene schema versions.

Packets contain:

- Part count and up to three local-light records.
- Per-part emissive/effect information, base color and actor placement/playback fields.
- X/Z route points.
- A local 4×4 transform matrix.
- Vertices and 32-bit triangle indices.

The current editor vertex has ten 32-bit floats: position XYZ, normal XYZ, color RGB and a material-kind value, totaling **40 bytes**. This must not be confused with the **96-byte skinned vertex records** retained in the separate crowd renderer.

Managed mesh byte arrays are cached by mesh identity using `ConditionalWeakTable`. The native side hashes vertex/index data and reuses GPU meshes with matching keys. Local geometry is separated from transforms, so moving/scaling an unchanged object does not require geometry reupload. Incoming packets still serialize and examine scene data; this is not a complete fine-grained incremental scene protocol.

New geometry is uploaded after synchronization. Removed resources are retired after prior GPU work completes. Terrain and fluid mesh changes still incur regeneration and upload. A scene's part count must stay below the native instance capacity; managed transport rejects 8,192 or more parts. This is a draw-part limit, not a promise to render that many arbitrarily complex objects interactively.

## 1.6 Multi-core scheduling

`deep/engine/persistent_workers.h` provides a persistent range-worker pool used by the engine's preparation paths:

- One submitting thread per renderer.
- Contiguous index ranges divide a submitted count into chunks.
- The calling thread executes the first chunk; worker threads execute remaining chunks.
- Generation counters and condition variables wake existing workers.
- The caller waits for completion; exceptions are captured and rethrown.
- Threads grow as needed and are joined on destruction before module unload.

This avoids repeatedly creating/joining worker threads. It is **not** a general work-stealing scheduler, dependency DAG, asynchronous ECS task system or completed parallel physics/AI framework. The presence of renderer workers does not imply terrain brush operations or every editor module are parallelized. WPF-side geometry work can still block interaction.

## 1.7 Render pipeline, native resolution and anti-aliasing

The shared renderer uses DirectX 12 resources, command allocators/lists, descriptor heaps, explicit resource barriers and fences. Important native-preview stages are:

1. Frame pacing / acquisition of an available in-flight slot.
2. Camera and per-frame/per-instance constant updates.
3. Background and scene rendering into color, motion-vector and depth targets.
4. Backend evaluation into the output resource.
5. Editor edge-aware compute smoothing.
6. Copy to the swap-chain backbuffer and present.

The scene targets include `R8G8B8A8_UNORM` color, `R16G16_FLOAT` motion vectors and `D32_FLOAT` depth. The output is `R8G8B8A8_UNORM`. Three backbuffers/in-flight slots are supported. Fences prevent reusing a slot before its work finishes. The default in-flight count is three; the runtime has controls for fewer frames in flight.

**Native-resolution correction:** the old editor set upscaling to off but inherited `modeRequest="quality"`. It could therefore render below output resolution while claiming native resolution in the UI. The current editor explicitly requests `modeRequest="native"`, `upsRequest="off"`, no frame generation, and no vsync. A runtime check verifies render width equals output width. Earlier editor-native/parity claims require reevaluation if they predate this fix.

`native/edge_aa.h` implements a lightweight edge-aware compute filter: it reads a center pixel and four neighboring pixels, compares luminance contrast, selects an edge direction and applies bounded blending. It uses 8×8 thread groups. It is **not** MSAA, TAA, DLAA, a temporal reconstruction system or a guarantee that all fine-pattern aliasing disappears. Framebuffers remain single-sample. Procedural detail uses screen derivatives to reduce high-frequency noise aliasing.

## 1.8 Vendor graphics technologies and frame pacing

The engine source contains the upscaler/backend abstraction and DLSS, FSR and XeSS integration paths, with frame-generation/latency support where available. The build references vendor SDKs. Runtime settings/presets distinguish upscaler, quality, frame generation, vsync, culling, resolution, display mode and foreground requirements.

These runtime integrations do not mean DLSS is active in the editor: the current creator viewport explicitly uses native rendering with upscaling and frame generation off. SDK paths are machine-specific; exact supported vendor features and current driver compatibility require separate runtime verification. No claim is made that every vendor mode was retested during the world-quality update.

The editor sets a **500 FPS ceiling**. This is a cap, not a guaranteed delivered frame rate. The runtime reads its frame limit from external settings; `deep/settings/native-1440p.startest` specifies native 2560×1440, fullscreen, cap500, no upscaling/FG/vsync/culling, and foreground testing. High-resolution timing and frame-latency synchronization support pacing. CPU frame time, GPU timestamps, wall FPS and generated/presented FPS must be treated as distinct metrics.

## 1.9 Performance evidence and its limits

The documented test machine has an RTX 5090, 64 GB RAM and a Ryzen 9950X3D-family CPU; a later handoff identifies it as 9950X3D2. These are local recorded hardware details, not a portability or minimum-requirement statement.

| Evidence | Result | Interpretation |
|---|---|---|
| Retained crowd changes | Persistent workers and 96-byte vertex layout | Accepted engine changes, not an editor frame-rate promise. |
| Controlled job038 | Reconstructed baseline130.967 FPS vs retained148.583 FPS; paired +13.447%; exact image checks | Matched reconstructed comparison, not the historical executable or sum of unrelated gains. |
| Latest saved retained acceptance | Crowd144.3 FPS; club421.9; Empty415.5 | Specific saved test results. |
| Job043 mesh-shader screen |139.683 vs165.883 FPS, but image mismatch | Rejected; no accepted mesh-shader speedup. |
| Job044 mixed shader pipeline | Candidate failed DX12 mesh-pipeline initialization | Not retained. |
| Latest editor interaction check |150 transforms, zero geometry reuploads; approximately422 FPS diagnostic | Small-scene sustained-edit test. Not a large-world benchmark or editor/standalone parity proof. |
| Historical original engine reports |139.6 FPS claim; another reproduction reported126.7 and115.24 FPS under different runs | Unresolved comparability; do not infer a confirmed regression or universal baseline. |

Engine optimization is **stopped/paused** (`deep/automation/status.json` reports stopped; STOP is retained). The worker's presence is test infrastructure, not a runtime game feature. It must not be restarted merely because this document discusses future work.

# 2. World-Building and Editing Tooling

## 2.1 Scene model and units

The creator uses a 24×24 metre ground domain, X/Z spanning −12 to +12. Y is height. `SceneData` owns characters/objects, optional terrain, liquid sources, name and ground color. The `Character` type also represents building shapes, nature props and effects; its name does not imply a full character-controller or actor-component framework.

Object state includes stable ID, name, asset path or shape type, X/Y/Z, uniform size, per-axis dimensions, Euler rotations, color, speed, route, floor constraint, lighting properties, material kind and optional sculpted mesh. Default imported preview height is normalized to1.8 metres.

## 2.2 Terrain and terraforming

`TerrainData` stores **97×97** height samples at0.25m spacing, plus one integer paint category per sample. Height queries use bilinear interpolation. Heights are constrained to±20m. This is a single-valued heightfield `y=h(x,z)`; it cannot represent caves, overhangs or stacked independent ground surfaces.

The display mesh is **193×193**, sampling at0.125m. Its 192×192 cells produce73,728 terrain triangles. Increased display density interpolates the existing heightfield; it does not add independent editable height samples or recover detail absent from stored data.

Active terrain tools:

| Tool | Operation |
|---|---|
| Raise / Lower | Add/subtract weighted height while brushing. Right mouse lowers in Raise/Lower mode. |
| Smooth | Blend toward a local neighborhood mean, reading a pre-stroke-step height copy. |
| Flatten | Blend toward the height captured at stroke start. |
| Paint | Assign the selected category inside the brush footprint. |
| Hill / Crater | One-click signed radial height stamps. |
| Plateau | One-click flat-top stamp with a tapered boundary. |
| Stamp selected shape | Rasterize the transformed shape's upper surface into terrain; preserve the original shape object. |

For normalized radial distance `d<1`, the height brush uses `w=(1-d²)²`. Raise/lower increments scale with strength, elapsed brush time and weight. Smooth/flatten use an exponential blend of the form `1−exp(−strength×seconds×5×w)`. Radius and strength are user controls. Terrain input ray-marches against the heightfield and refines crossings.

The terrain timer runs nominally every50ms. Mesh rebuilds currently cover the whole terrain; there is no dirty-tile GPU update strategy. The brush samples pointer positions without continuous path interpolation, so fast strokes can leave gaps. Terrain edits stop/reset preview state through the existing edit history path; they do not run as a fully coupled live simulation deformation system.

## 2.3 Object mesh sculpting: distinct from terrain

`SculptTools.cs` provides **Pull, Push, Smooth and Flatten** for a selected solid shape, rock or imported static mesh. These operate on 3D vertex positions and can deform side-facing surfaces; they are not restricted to the terrain's Y-only heightfield.

Processing:

1. Pick an object's approximate bounds, then ray-test its actual triangles.
2. Copy its local mesh and combine source part transforms.
3. Weld coincident vertices using quantized positions.
4. Subdivide low-density geometry with shared edge midpoints, subject to bounded passes/counts.
5. Store editable local positions and indices in `SculptMesh`.
6. Apply the brush in world space, converting modified positions back through the inverse object transform.
7. Rebuild geometry/normals and submit the updated mesh.

The hit normal is oriented toward the viewing ray. Push/Pull displace along that normal with a smooth radial falloff; right mouse reverses their direction. Smooth averages connected neighboring vertices. Flatten projects toward the brush-hit plane. Radius ranges0.08–3m and strength0.05–2 in the current dock.

Imported sources above80,000 triangles are refused before preparation. Grass, fire, lights and lava effects are excluded. Sculpt data validates finite positions, index ranges and size limits. Existing assets are not overwritten: the scene stores a modified mesh copy.

Limits: no adaptive remeshing, topology tools, volume preservation, boolean cuts, holes, symmetry controls, UV repair, sculpt layers, pressure-sensitive pen support or self-intersection prevention. Position welding can join coincident vertices that had distinct original semantics. Detailed large-mesh interaction performance and production sculpt quality remain unproven.

## 2.4 Undo, Redo, cancellation and persistence

Undo/Redo use serialized **whole-scene JSON snapshots** in memory. `Remember()` stops/resets preview, clears Redo and pushes the scene before an edit. A continuous brush stroke has one history entry. Restoring history decodes a snapshot and reconnects selection by stable ID. Sculpt Escape cancels the active stroke using its pre-stroke snapshot. Release, deactivation and capture-loss handling stop active gestures.

This is simple and provides exact scene restoration in tested cases, but it is not an operation/delta log. Large sculpted meshes inflate snapshot memory and serialization cost; no bounded memory policy or disk-backed undo journal is established here.

JSON scene schema is now **5**, reading versions1–5 and upgrading them in memory. It stores sculpt arrays and liquid source settings. Older editor builds cannot read newly saved schema5 files. Save uses a temporary file, an exclusive participating-writer lock, atomic replacement and a timestamped backup of an existing file. SHA256 revision checks reject stale writes. File watching refreshes clean scenes changed externally and preserves unsaved local edits on conflict.

## 2.5 Surface painting, materials and biome limitations

The five terrain categories are grass, earth, sand, stone and snow. Display vertices interpolate neighboring sample colors and material-kind values. The shader applies procedural world-space multi-scale noise, category-specific variation and derivative-based normal perturbation. This replaces the older one-flat-color-per-triangle representation and softens paint transitions.

There are no imported 2K/4K terrain maps, authored albedo/normal/roughness texture sets, terrain texture streaming or user-configurable texture resolution controls. The latest work improves apparent detail and native raster resolution through procedural shading.

There is no biome object, moisture/temperature field, slope-based auto-paint system, layered vegetation distribution or ecological simulation. The material strips in the sample landscape are a demonstration of different paint categories, not a special “material striping” subsystem. Color interpolation and interpolated category numbers are not equivalent to a general physically based multilayer material blend graph.

## 2.6 Nature generation and manual placement

Builder > Nature exposes four manually placed object types:

- **Rock / Boulder:** deformed parametric rounded meshes; different proportions; welded seams/poles; procedural stone detail.
- **Pebbles:** a deterministic cluster of nine small rounded stones inside one object.
- **Grass tuft:**100 tapered blade strips, each with several vertical segments and a randomized position, height and lean.

Geometry is generated with fixed random seeds and cached by shape type. Objects can be translated, rotated and scaled using the existing tools. A pebble cluster or tuft has procedural internal distribution, but there is **no generalized scatter brush**, density map, slope rejection, collision-aware distribution, per-biome population or vegetation LOD system in the creator.

Grass is static geometry. There is no wind field, dynamic bending, player interaction, fire propagation or growth. Repeated props are ordinary scene parts with reused GPU geometry, not a demonstrated large-scale GPU vegetation instancing system. Material defaults can differ between direct local commands and the specialized GUI placement path; complete parity has not been proven.

## 2.7 General placement and transforms

Shapes include cube, rectangular block, trapezoid and octagonal column, alongside nature/effect types. Existing controls provide translation, rotation, per-axis scaling, exact numeric fields, group selection, a selection rectangle and group transforms. Optional grid, terrain-ground and nearby-object snapping are available. The Keep Above Floor constraint clamps an object's transformed bounds against ground when editing invokes it.

Known gaps include first-eligible rather than nearest handle selection, approximate rotation interaction, snapping chiefly at release, no full ghost-placement preview and no universal Escape rollback transaction for all editing tools. Ground snapping is a placement convenience, not collision physics.

## 2.8 Fluid simulation and visual geometry

Two separate facilities coexist:

1. **Still water/lava:** a user-selected common height plane clipped against the terrain.
2. **Finite pours:** conservative experimental surface transport in `TerrainFlow`.

The flow grid is **48×48**,0.5m cells,0.25m² cell area, closed boundaries. Water and lava have independent depth arrays. The bed samples the terrain. Each source defaults to2m³ emitted over3 simulation seconds; at most32 sources are supported.

The simulation uses fixed0.05s steps. Adjacent cells compare total surface height `bed+depth`. Proposed transfer scales with height difference, elapsed step and a transport coefficient:3 for water and0.25 for lava. An aggregate donor limiter caps outgoing transport to45% of available donor depth per step. Transfers are accumulated conservatively; nonnegative depths are maintained. Tests verify volume conservation, finite values, downhill movement, basin accumulation and slower lava.

This is an approximate diffusive heightfield transport scheme, **not Navier–Stokes**, momentum-conserving flow or a full pressure/velocity solver. There is no rigid-body coupling, solid-object obstacle blocking, buoyancy, erosion, waterfalls, splashes, caves, independent stacked fluid layers, cooling/solidification or water/lava reaction. The two fluid fields can overlap visually.

Display depth is interpolated onto a finer mesh, clipped at a small wet-depth threshold, and shared vertices reduce seams. Interpolated shores improve the blocky whole-cell silhouette without increasing physical simulation resolution. Evolving liquid geometry still uploads to the native renderer. Up to eight simulation steps are processed per preview tick; sustained overload can cause simulation lag rather than a demonstrated real-time guarantee.

Scene save stores sources, not the instantaneous simulated depth fields. Pause holds simulation; Reset removes runtime liquid while retaining pours. Terrain edits reset simulation. Water/lava surface shaders continue to use their visual time source, so “simulation paused” does not necessarily freeze every shading effect.

# 3. Interactive Playtest, Simulation and Media Controls

## 3.1 Build-and-test workflow

Users can place/import static characters, build terrain and props, draw routes, save a scene and press Play. In windowed mode Play creates an external native scene window. Fullscreen mode uses the embedded native viewport. Play/Pause and Reset controls remain available; route Repeat is optional.

This is a working scene-preview workflow. It does not currently create a complete game, compile user gameplay, package assets or establish parity between editor preview and a shipped project runtime.

## 3.2 Character import and presentation

The editor's GLB loader reads GLB2 triangle primitives, position/index data, node transforms and material base-color factors. It normalizes the combined model to a1.8m height and centers it. Sparse geometry and external geometry buffers are explicitly unsupported by the preview loader.

It does not apply the full imported texture/UV/PBR/skin/animation content as a complete character pipeline. The editor-native mesh stream recomputes normals and carries simple/procedural surface data. The separate game project runtime contains more advanced skinned/crowd and texture paths; those capabilities must not be attributed automatically to creator-imported characters.

No skeletal walking, retargeting, IK, animation blend graph or locomotion state machine is delivered in the creator. Characters translate/turn as static meshes.

## 3.3 Route drawing and traversal

Routes are ordered lists of `WalkPoint {X,Z}`. In route mode, dragging replaces the current route and appends points when squared X/Z separation exceeds0.025m² (about0.158m). Moving an object translates its route by the same X/Z delta.

Traversal consumes `elapsedSeconds×Speed` along piecewise-linear segments. Speed is in metres per second; route duration is total horizontal path length divided by speed. Native playback can orient the object toward a segment using yaw. The editor selects the maximum object route duration for scene completion.

**Absent:** navmesh generation, obstacle avoidance, collision constraints, slope limits, path replanning, terrain-following Y adjustment, foot placement and dynamic reaction to terrain edits. Route samples are X/Z, not full 3D paths. A character's stored Y/local transform is carried into playback; it can intersect changed terrain or float over a depression.

Ground snapping and floor clamping during editing are real, but they do not establish continuous surface-following navigation.

## 3.4 Playback lifecycle and timing

- **Ready:** scene editable; no route movement clock running.
- **Play:** starts/resumes the stopwatch and preview timer. Requires at least one nonzero route duration or a liquid source.
- **Pause:** stops simulation/movement time while preserving current state.
- **Finished:** nonrepeating route-only playback reaches maximum duration and stops.
- **Repeat:** character route time wraps by maximum duration. This is scene-duration repetition, not a per-character animation scheduler.
- **Reset:** clock and generated liquid state are cleared; saved source objects/routes remain.

With liquid sources, route completion no longer ends the fluid scene automatically. Flow simulation uses elapsed preview time while route evaluation may clamp/wrap separately. The external viewport receives scene updates and camera state; closing it stops playback and releases its renderer.

No timeline scrubbing, keyframe editor, clip sequencing, queued animations, event tracks, deterministic replay contract, game-wide fixed-update loop or simulation checkpoint persistence is provided. The render thread, scene-packet handoff and UI timers are implementation queues, not a product-level animation queue.

## 3.5 Camera and viewport UX

Current controls:

- Middle mouse drag: orbit/tilt around the camera focus.
- Wheel: zoom; editor distance bounded2–60.
- Outer window edges: planar camera travel; DPI-aware edge detection.
- Optional arrow-key travel: off by default; inhibited while typing/editing relevant controls or during conflicting gestures.
- Main menu Camera travel speed:0.25–6×.
- Main menu Camera tilt/turn speed:0.2–4×.
- Frame world: resets target/orientation to frame the landscape.
- Additional low, side, above and selected-object focus views exist.

Planar travel is elapsed-time based and normalized so diagonals do not move faster than straight directions. Default nominal travel is roughly four times the earlier implementation. Pan/tilt preferences are session-only. Modal file dialogs and open menu/settings popups suppress edge travel. Navigation has a four-arrow cursor and subtle, click-through dark edge feedback.

This is an orbit/pan editor camera, not a completed FPS fly camera with arbitrary translation axes. A selected-object focus view is not a character-follow camera controller. The underlying game project's own camera behavior is separate; automatic creator character-follow/free-cam switching is not delivered.

Four persistent corner categories are **Builder, Characters, Environment, More**. Music is under More. Builder selects Terrain, Shapes, Nature or Sculpting; the bottom pill does not duplicate Terrain/Shapes tabs. Fullscreen corner controls and playback float over the native viewport without resizing the canvas when menus open.

## 3.6 Local media

The editor has local music selection/play/pause/next/stop and volume. The current implementation uses Windows MCI through `LocalMusic`, and prior MP3/WAV checks exist; the terrain regression also verifies local silent-WAV decode and controls.

Music is an editor convenience. Playlist state is not persisted as game content. There is no delivered spatial audio engine, positional sound emitter system, audio event graph, exported soundtrack pipeline or game audio mixer.

# 4. Lighting, Shading and Post-Processing

## 4.1 Surface and lighting pipeline

The native editor uses a generated editor-local variant of `RenderDevice`. `native/prepare_renderer.py` reads the staged engine header and applies explicit editor shader/vertex/post-pass changes; `native/render_device_editor.h` is generated output. The shared source is not modified by this generation step.

The static-mesh vertex shader transforms positions, normals and interpolated surface samples. Normals are corrected for nonuniform object scale and flipped for camera-facing two-sided shading when necessary. Surface shading includes linear/sRGB conversion, a microfacet-style BRDF helper, hemispherical ambient light, local lights, tone mapping and distance fog. This is not a material-node editor or a complete physically calibrated rendering workflow.

## 4.2 Sunlight, sky and fog

The current editor installs a stronger fixed directional key, cooler ambient/sky fill and a gradient background. There is modest distance fog. These are lightweight artistic approximations. There is no atmospheric scattering simulation, sun/sky astronomical model, volumetric clouds, weather system, day/night cycle or user-authored environment graph.

Global lighting values are currently code-defined. Local fire/light objects expose color, brightness and reach. The creator supports at most three such local sources combined. Tests establish that changing a local light changes the rendered image; they do not establish physically accurate photometry or a many-light performance budget.

## 4.3 Soft grounding shadows

For up to16 eligible mesh parts, CPU-side geometry bounds produce spherical shadow proxies. The pixel shader tests direction-to-light against these spheres and applies a smooth attenuation. This helps ground objects visually but is not an accurate representation of arbitrary meshes.

There is no depth shadow texture, shadow-camera pass, cascaded sun shadow map, PCF/PCSS implementation or ray-traced shadow traversal. Current proxies use positions at scene upload and do not independently follow route animation. Multipart objects can consume multiple proxy slots. Terrain self-shadowing and local-light occlusion are not established.

## 4.4 Ambient occlusion and normals

The shared runtime contains material/texture AO facilities, but the creator's static procedural path does not implement screen-space AO. Its current shading call uses full material AO before separate shadow approximation. Hemisphere lighting, grain-driven normal changes and darkening from simple shadow bounds should not be described as an ambient-occlusion subsystem.

There is no AO bake/export workflow, bent-normal lighting, SSAO/GTAO tuning panel or validated global illumination solution.

## 4.5 Water rendering

Water shading combines:

- Time-varying sinusoidal normal perturbations.
- View-angle Fresnel weighting.
- Procedural sky-colored reflection.
- Highlight response to the directional key.
- Depth-dependent shallow/deep colors.
- A shallow-depth shoreline foam pattern.

Depth values are derived from liquid-surface height relative to the heightfield and encoded in vertex samples. Water is opaque in the current output. There is no actual view through water, scene refraction, screen-space reflection, planar reflection capture, caustics or reflection of placed objects. Ripple shading does not introduce physical waves into the transport solver.

## 4.6 Lava and other emissive effects

Lava uses animated noise fields to create dark crust and bright orange/yellow fissures, with time variation in brightness. These are **pixel-shader patterns**, not displacement, tessellation or a fracture simulation. The shader participates in tone mapping, but no bloom/glow post-pass has been added. Emissive-looking pixels do not automatically light surrounding geometry.

Fire/light helper geometry uses simple emissive forms and animation/pulsing. Fire sources can also contribute through the limited local-light system. No particle combustion, heat haze, smoke transport or volumetric fire is implemented.

## 4.7 Post-processing boundary

Delivered: tone mapping/color conversion, simple fog, the new edge-aware smoothing pass, and the runtime's separate optional reconstruction technologies. Not established in the creator: bloom, depth of field, motion blur, cinematic color grading, volumetric post effects, temporal AA history management or path tracing.

# 5. Current Capabilities and Roadmap Gap Analysis

## 5.1 Capability matrix

| Area | Operational now | Important boundary / gap |
|---|---|---|
| Native creator rendering | Windows WPF/native DX12 viewport, native-resolution correction, edge filter | No non-Windows creator port; no full rendering parity certification. |
| Project runtime | `.star` DLL host and `.startest` reconciliation | Separate from creator scene Play/export. |
| Scene persistence | Schema5, migration, atomic backup saves, revision checks | Absolute asset paths; snapshot-size growth; old binaries cannot read schema5. |
| Terrain | Brushes, stamps, painted materials, still liquids | Single heightfield, full rebuilds, no caves/streaming/dirty tiles. |
| Object sculpting | Four brushes, mesh copy, subdivision, Undo/Redo, save | No advanced topology/UV/remesh tools or large-mesh performance guarantee. |
| Nature | Manual procedural prop/tuft placement | No scatter brush, wind, density painting or vegetation LOD. |
| Fluids | Finite conservative pours, pooling, improved visuals | No momentum solver or object/rigid-body coupling. |
| Character movement | Static GLB placement and drawn X/Z routes | No skeletal locomotion, navigation, terrain following or collisions. |
| Lighting | Directional/ambient/local lights, proxy shadows | No shadow maps, SSAO, GI, local-light occlusion or advanced sky. |
| Media | Local editor music player | No spatial/exported game audio pipeline. |
| AI tooling | Local JSON command/inspection interface | Partial GUI coverage; not an embedded model or cloud connector. |
| Game creation | Scene building and preview | No complete scripting/ECS/gameplay authoring, build/export or platform pipeline. |
| Performance tooling | Saved harnesses, reports, GPU/CPU timing and controlled experiments | Paused; results are workload-specific. |

## 5.2 Local AI interface and coverage

The executable accepts noninteractive local commands:

```text
--describe --result <report.json>
--inspect <scene.starscene> --result <report.json>
--commands <request.json> --result <report.json>
--feature <feature-id> --result <report.json>
```

The command envelope/protocol is version1, separate from scene schema5. Current operations are `add`, `move`, `route`, `rename`, `remove`, `configure`, `shape`, `transform`, `terrain-brush`, `terrain-water`, and `terrain-stamp`.

Batch requests carry a scene path and expected SHA256 revision. Operations validate before a save; stale edits are rejected. A local model or external assistant needs authorized local execution access. No provider SDK, model hosting runtime, network listener or cloud-AI account connection is embedded in this interface.

No dedicated commands expose all new sculpt brushes, flow-source authoring, camera controls, music, snapping toggles or native Play state. A generic shape command accepts the current shape validator, including nature types, but does not reproduce all specialized GUI defaults. Complete GUI/AI parity is a goal, not current fact.

`FeatureCatalog` is an imperfect mixed roadmap/discovery list: `Available` is always false; some features have experimental editor-only labels, while broader categories remain scaffolded despite partial functionality. `--feature` reports status rather than running those tools. Do not treat a single catalog flag as an exhaustive audit of current implementation.

## 5.3 Proposed engineering priorities

These are analysis suggestions, not authorization to restart paused work.

1. **Unify scene execution boundaries.** Define which scene/asset APIs are shared by creator, standalone Play and project DLLs. Prove identical scene data, transforms and visual behavior across those paths before promising export parity.
2. **Establish input and visual acceptance gates.** Retain physical hit testing and actual pointer tests, DPI/multimonitor/menu/modal cases, scene-preservation tests, and matched native-resolution screenshots. Compilation and direct-message tests alone previously missed real UX failures.
3. **Scale edit updates and history.** Move toward dirty terrain regions, bounded geometry updates, asynchronous preparation, revisioned object caches and delta/bounded undo. Benchmark sustained strokes and large sculpt scenes, not only static transforms.
4. **Finish terrain-aware movement.** Specify ground-height sampling, slopes and collision before adding navmesh/avoidance, character animation, foot placement and dynamic reactions to terrain changes. Decide what happens when terrain edits invalidate a route.
5. **Strengthen surface and lighting systems.** Separate stable material identity from vertex color; consider authored texture sets, controlled blending, shadow maps and an explicit lighting quality ladder. Measure GPU cost at native resolution and define intended stylized versus realistic art direction.
6. **Expand nature tools deliberately.** Density/scatter controls, deterministic placement seeds, slope/water exclusion, instancing/LOD and optional wind need separate data/runtime support. A generated tuft is not an ecosystem system.
7. **Choose fluid scope.** A stable terrain-building toy, gameplay fluid system and physically ambitious solver have different costs. Define obstacle coupling, save/reset behavior and performance budgets before broadening the solver.
8. **Add gameplay and production pipeline.** Scriptable entity behaviors, events, input mapping, audio, animation, runtime UI, portable assets and packaging are prerequisites for full game creation. Platform and VR plans need explicit backend/editor portability work.
9. **Close AI/UI coverage gaps.** Share validated commands rather than duplicating editor logic. Expose consistent capability metadata, transactions, scene revisions and useful error reporting; add safety appropriate to any future remote connector.

## 5.4 Suggested acceptance criteria for future analysis

- Compare performance using the same scene, camera, native pixel dimensions, visibility/culling, animation time and foreground conditions.
- Report real-render and presented/FG FPS separately; keep CPU and GPU timing distinct.
- Test moving scenes and sustained interaction as well as frozen images.
- Verify source assets and user scenes survive failure, cancellation, migration and undo.
- Specify data limits and failure behavior, not only successful demo cases.
- Demonstrate terrain-following routes, collision and animation separately rather than inferring them from placement or movement.
- Treat cross-platform, export, advanced lighting and cloud-AI integration as unproven until tested on the named destination.

# Appendix A. Code and Evidence Map

All relative paths below are beneath `D:/projects/Starlight-Engine/`.

| File / directory | Role |
|---|---|
| `deep/src/runtime.cpp` | `.star` project/preset host and DLL entry point loading. |
| `deep/src/preset_reconciliation.inc` | Preset reconciliation implementation. |
| `deep/projects/{Empty,GameProject}/` | Project manifests/modules and project-specific runtime source. |
| `deep/settings/native-1440p.startest` | Example native performance preset. |
| `deep/engine/render_device.h` | Shared renderer, resource lifecycle, pacing, scene/shader/crowd paths. |
| `deep/engine/persistent_workers.h` | Persistent range-worker pool. |
| `deep/engine/upscale/` | Vendor/backend abstraction and integrations. |
| `deep/automation/{results,changes.jsonl,status.json,STOP}` | Experiment evidence and paused worker state. |
| `editor/MainWindow.xaml` / `.xaml.cs` | Main WPF UX, scene assembly, editing and commands. |
| `editor/SceneData.cs` / `EditorHistory.cs` | Scene/actor data, migration, saving and snapshot history. |
| `editor/NativeViewport.cs` / `NativeEditor.cs` | HwndHost/PInvoke/render thread and Play integration. |
| `editor/NativeScene.cs` | Binary scene transport and managed mesh serialization cache. |
| `editor/native/viewport.cpp` | Native context, packet parsing, resource reuse and drawing. |
| `editor/native/prepare_renderer.py` | Generate editor-local renderer from staged base. |
| `editor/native/world_surface.hlsl` / `edge_aa.h` | Procedural materials/liquids and compute edge smoothing. |
| `editor/TerrainData.cs` / `TerrainTools.cs` | Heightfield data, brushes, mesh generation and UI. |
| `editor/TerrainFlow.cs` / `FlowTools.cs` | Conservative pours, visual mesh and playback coupling. |
| `editor/SculptTools.cs` / `SurfaceDetail.cs` | 3D sculpt interaction/data and vertex material metadata. |
| `editor/NatureGeometry.cs` / `ObjectGeometry.cs` | Procedural nature/shape geometry. |
| `editor/BuildingTools.cs` / `PointerInteraction.cs` | Camera, handles, picking, selection and gesture cleanup. |
| `editor/ObjectSnapping.cs` / `WorldNavigation.cs` | Snapping, Frame world and startup scene support. |
| `editor/GlbModel.cs` | Limited static GLB2 importer. |
| `editor/EffectsTools.cs` / `EffectGeometry.cs` | Local light/effect controls and helper geometry. |
| `editor/LocalMusic.cs` | Windows local media player. |
| `editor/ProjectWindow.cs` | Project/preset GUI and external runtime launch. |
| `editor/EditorCommands.cs` / `AI-INTERFACE.md` | Local AI protocol; document contains older build/schema references. |
| `editor/FeatureCatalog.cs` | Mixed roadmap/discovery statuses, not full capability truth. |
| `editor/WORLD-QUALITY-RELEASE.md` | Latest rendering/sculpt/nature release details and limitations. |
| `INTERACTION-RESEARCH.md` | Previously saved editor/game interaction research. |

## Saved current editor checks

| Evidence | Coverage |
|---|---|
| `editor/checks/world-quality/result.json` | Geometry, brushes, exact history/save, migration, native width, camera modifiers, input start/release/cancel and tool availability. |
| `editor/checks/interaction/result.json` | Hit testing, camera, selection, transforms, release/capture loss and150 transforms with no geometry reupload. |
| `editor/checks/terrain/result.json` | Brushes, stamps, blended paint/water, scene roundtrip, snapping, effects and music. |
| `editor/checks/flow/result.json` | Conservation, finite depths, downhill movement, slower lava, basin pooling and playback/reset. |
| `editor/checks/corner-controls/result.txt` | Floating fullscreen controls, edge feedback and windowed restoration. |
| `editor/checks/world-quality/{landscape,lava}.bmp` | Inspected native visual captures; not comprehensive art acceptance. |
| `editor/checks/world-quality/Landscape-preview.starscene` | Sample landscape used for visual review. |

The final visible UI check used actual input injection to reshape a preview rock, then Ctrl+Z restored it. This supports the basic brush/undo workflow. It does not constitute the creator's final visual acceptance or validation of every mesh.

# Appendix B. Build, Preservation and Operating Constraints

- Managed build: `dotnet build editor/Starlight.Editor.csproj -c Release -o editor/bin/<new-staged-build>`.
- Native build: run `editor/native/build.py`; it regenerates the editor header and uses the existing MSVC environment/tooling and staged engine library.
- Native build paths and original asset/SDK references are machine-specific; clean-machine reproducibility and redistributable packaging are not proven.
- Build into a new directory when an existing editor executable is open. Do not overwrite a live build or terminate windows with unsaved scenes.
- Source backup before world-quality changes: `editor/backups/world-quality-20260916-210702/`.
- Desktop shortcuts and root launcher have `before-world-quality` backups. Original engine archives remain preserved.
- One-time migration helpers such as `upgrade_world.py`, `finish_world.py` and `finalize_world.py` are historical implementation helpers, not a normal rebuild pipeline to rerun unchanged.
- Canonical recovery documents are `D:/projects/_recovery/STARLIGHT-EDITOR-CONTEXT.md` and `ASTRA-HANDOFF.md`. Older sections elsewhere may conflict; newest verified source/build state wins.
- Kira character work and engine optimization remain paused. This documentation task does not authorize resuming them, changing infrastructure, publishing software or discarding scenes.

**Current capability boundary:** Starlight provides a functioning experimental Windows native scene creator with editable terrain and meshes, procedural surfaces/nature, limited fluid transport, static route playback, local media and partial AI commands. It also has a separate modular C++ project runtime and substantial controlled rendering experiments. Cross-platform production workflows, complete game authoring and export remain unfinished.
