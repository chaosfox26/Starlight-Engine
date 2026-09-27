> **Newer implementation:** native-resolution Ray Reconstruction and the temporal wavelet fallback are documented in [editor/DENOISING-RELEASE.md](editor/DENOISING-RELEASE.md). This file describes the earlier checkpoint where denoising claims differ.

# Starlight Engine — Architecture and Current State

**Snapshot: 17 September 2026, delivered DXR release.**  
**Audience:** Gemini or another technical collaborator preparing implementation prompts.  
**Workspace:** `D:/projects/Starlight-Engine/`. Relative source paths in this document are under that root.  
**Current executable:** `editor/bin/dxr-release/Starlight.Editor.exe`.

## Evidence, scope, and interpretation

This is a current implementation specification, not a description of a finished production engine. It consolidates source inspection, recovery handoffs, earlier architecture records, release notes, and saved validation results. No new builds or benchmarks were run for this documentation task. The delivered binaries were checked against the release manifest in the preceding continuation.

“Implemented” means there is an execution path; “verified” means the named saved tests exercise it; neither means production-scale coverage. “Proposed” means future work, not a committed milestone. Earlier development history is reconstructed from retained records; there is no complete commit-by-commit history available for this workspace. Exact dates are stated only where recorded. This document covers the known modules and milestones rather than inventing an exhaustive historical ledger.

The current document supersedes outdated feature-status claims in `STARLIGHT-TECHNICAL-SPECIFICATION.md` (16 September snapshot). That document remains useful for older implementation detail, but its assertions that physics, DXR, day/night, or volumetrics are absent are now obsolete. The authoritative release evidence is `editor/checks/dxr/final-validation.json`, supported by `Starlight Documents/Current/editor/DXR-RELEASE.md`, `Starlight Documents/Current/editor/CHAOS-RELEASE.md`, and `Starlight Documents/Current/editor/WORLD-QUALITY-RELEASE.md`.

### Essential corrections for future prompts

| Potential assumption | Actual state |
|---|---|
| Pure raster only | Native raster is the default; optional hardware DXR tiers are implemented. The prohibition concerns upscaling/frame generation, not ray tracing. |
| RT PSOs, hit groups, shader binding tables | Absent by design: compute PSOs execute inline `RayQuery`. |
| Shader Model 6.6+ required | Current RT shaders target `cs_6_5`; do not raise requirements without a reason. |
| Agility SDK deployment already established | D3D12 is established; inspected renderer/host sources do not establish an exported Agility SDK version/path or packaged Agility runtime. |
| Modern ECS/fiber scheduler | Creator uses C# object records and native arrays/caches; renderer has persistent range workers, not a universal ECS task graph. |
| Unlimited terrain/mesh streaming | No delivered chunk-streaming or virtualized-geometry system. |
| 3D fluid physics | Conservative 2D surface flow with solid occupancy and approximate buoyancy. |
| Photographic terrain textures | Procedural shading and vertex material blending; no photographic texture library added. |
| Creator characters inherit the crowd engine | Static creator GLB previews and separate skinned crowd workloads have different capabilities. |
| Complete game engine/export pipeline | Working scene creator and separate project runtime; full gameplay authoring/export remain unfinished. |

# 1. Engine Core Vision and Guiding Philosophy

## 1.1 Product direction

Starlight aims to provide approachable world building, interactive testing, and eventually a broad engine/creator suite. The intended UX resembles building games and RTS tools: place props, draw routes, sculpt terrain/objects, and preview without writing code for basic actions. Free access, voluntary donations, publicly readable development, cross-platform operation, 2D and VR are recorded ambitions, not proof of shipped distribution, licensing, ports, or product completeness.

The latest delivered product is a Windows x64 C#/.NET 10 WPF creator embedding a native C++ D3D12 renderer and a separate Jolt physics DLL. The modular project runtime is C++. The implementation remains experimental in several visual, simulation, and content-pipeline areas.

## 1.2 Native performance mandate

The creator requests native rendering explicitly: `modeRequest="native"`, `upsRequest="off"`, frame generation off. Its HDR path bypasses upscaler evaluation. Render/output dimensions are checked in saved tests. Do not introduce DLSS, FSR, XeSS, reduced-resolution reconstruction, or generated frames as the route to target performance.

Vendor integration code still exists in `deep/engine/upscale/` and older runtime settings. The mandate does not mean those historical sources have been deleted or every standalone preset has been rewritten. They are disabled/bypassed in the delivered creator path.

| Target | Complete frame interval | Interpretation |
|---|---:|---|
| 60 FPS | 16.667 ms | Intended baseline target for requested visual/simulation work. |
| 120 FPS | 8.333 ms | Higher target, dependent on hardware and workload. |
| Creator ceiling | 500 FPS / 2 ms | Present cap, not a guaranteed rate. |

There is no enforced per-subsystem budget allocator. CPU simulation, preparation, GPU rendering, synchronization, and presentation must be measured separately; their times cannot always be summed because work overlaps. Future budgets should reserve margin for dynamic editing and simulation and track P95/P99, not spend the entire target on a frozen render scene. Specific proposed budget allocations are not existing engine behavior.

## 1.3 Platforms, hardware, and workload boundaries

Windows x64/D3D12 is the verified platform. RT requires hardware DXR 1.1 and SM6.5; unsupported devices fall back to raster. Saved current graphics measurements identify an RTX 5090. Historical machine records list 64 GB RAM and a Ryzen 9950X3D-family CPU, with inconsistent later suffix naming; no newly audited CPU specification is asserted here.

Separate runtime crowd experiments include 100,000-instance acceptance requirements and GPU skinning paths. These do not establish that the creator can simulate 100,000 independent gameplay entities. The editor rejects packets with 8,192 or more mesh parts; that is a transport/draw-part bound, not an entity performance promise.

No production multiplayer client/server, entity replication, rollback networking, or remote authoritative simulation is delivered through the creator. External agent coordination infrastructure is separate from the game runtime.

## 1.4 Architectural boundaries

```text
WPF controls / input / history
  -> SceneData, Character records, terrain, sculpt meshes, liquid sources
  -> WPF geometry assembly and NativeScene V6 packet
  -> NativeViewport render thread / PInvoke
  -> Starlight.Viewport.dll / D3D12 / raster + optional DXR

Editor simulation controls
  -> managed flow and obstacle mask
  -> Starlight.Physics.dll / Jolt / body snapshots
  -> scene transforms and liquid geometry -> native viewport

Separate project-launch workflow
  -> .star project + .startest preset
  -> deep/bin/Starlight.exe
  -> LoadLibraryExW(project DLL) -> StarlightRunProject
```

The original GameProject reference at `<separate-game-project>/starlight-realms/native-dx12/` is preserved. `deep/` contains the modular runtime/shared rendering lineage. `editor/` contains the creator. Root `bin/`, `projects/`, and `.starlight.json` artifacts include an older process-launch experiment. Their presence does not identify the active architecture or launch target.

The `.star` examples use schema 3 and describe project DLLs and scenes. `ProjectWindow` reconciles a copied preset and launches the native project host. Creator `.starscene` Play is not an automatic game exporter or a conversion into those project DLLs. Previously built standalone project DLLs were not redeployed by the latest editor upgrades.

# 2. Rendering Pipeline and DirectX 12 Architecture

## 2.1 Frame execution

```text
Wait for retiring frame slot (three slots)
  -> update frame / instance constants and camera
  -> raster scene, motion and depth
  -> if DXR active: TLAS build/update -> Trace compute -> history/filter
  -> froxel density/light injection -> depth integration
  -> composite chosen linear scene + atmosphere
  -> ACES-style fitted tone curve / SDR conversion
  -> native edge-aware spatial AA
  -> copy final RGBA8 image to swapchain -> Present
```

The creator enables an opt-in `R16G16B16A16_FLOAT` scene buffer in the shared renderer. The swapchain remains `R8G8B8A8_UNORM` SDR; a linear HDR scene buffer is not HDR-monitor output. Depth is `D32_FLOAT`; the inherited motion target is `R16G16_FLOAT`. Framebuffers are single-sample.

`editor/native/edge_aa.h` implements an 8×8-group compute filter using luminance contrast and neighboring samples. It is native spatial smoothing, not MSAA, TAA, DLAA, or temporal reconstruction. Procedural surfaces also use derivatives to limit high-frequency detail. Native resolution fixes framebuffer undersampling but does not guarantee every silhouette or shader pattern is alias-free.

## 2.2 DXR device checks and execution model

`deep/engine/dxr.h`, `DxrPass::init`, queries `D3D12_FEATURE_D3D12_OPTIONS5` and checks `RaytracingTier >= D3D12_RAYTRACING_TIER_1_1`. It queries `D3D12_FEATURE_SHADER_MODEL` for `D3D_SHADER_MODEL_6_5` and obtains `ID3D12Device5` and `ID3D12GraphicsCommandList4` from the existing device/list.

Unsupported capability or initialization failure selects raster and reports status. `STARLIGHT_DISABLE_DXR=1` forces the unsupported path for testing. This verifies fallback behavior on the test machine; it is not a test on a second unsupported GPU.

`dxr.hlsl` executes `RayQuery::TraceRayInline` within compute shaders. `CreateComputePipelineState` creates `Trace` and `Denoise` PSOs. There is no `ID3D12StateObject`, raygen export, hit group, miss shader, shader identifier table, or `DispatchRays` call in this implementation. Hit reconstruction, material evaluation, misses/environment, and bounce loops are handled explicitly in compute shader code.

## 2.3 BLAS, TLAS, and scene invalidation

- `DxrItem` couples a 64-bit geometry key, mesh pointer, world matrix, and color. A `std::map<uint64_t, Blas>` caches triangle acceleration structures by geometry content. Identical meshes share geometry/BLAS.
- Actual indexed triangles cover terrain, shapes, nature, imported parts, liquid surfaces, and chaos ribbons. Smoke is a volume, not an opaque triangle shell. RT does not substitute raster bounding-sphere proxies for mesh intersections.
- Packed vertex/index buffers support hit reconstruction. Per-instance records carry world and inverse-transpose normal matrices, base color/emission and packed geometry offsets.
- `BuildRaytracingAccelerationStructure` builds BLAS/TLAS. BLAS favors fast tracing. Stable instance counts use in-place TLAS updates; topology/instance-set changes rebuild as required. Capacity growth waits for earlier work to retire.
- Transform-only motion from editing, routes, or physics reuses BLAS and updates TLAS. Terrain/sculpt mesh changes invalidate their geometry keys. Unchanged meshes retain cache entries.
- Geometry-set changes currently flush and repack hit buffers synchronously. Upload/build scratch lifetime is protected by GPU completion. Large or continuous sculpting can hitch; asynchronous incremental BLAS/chunk streaming is not delivered.
- Lightning has shared small transform jitter in RT mode so raster depth and ray geometry agree. Separate raster vertex jitter is suppressed in that mode.

Terrain collision tiles are unrelated to render chunks: the rendered terrain is still one regenerated mesh. No GPU-skinned dynamic BLAS integration is established for creator characters.

## 2.4 Quality tiers

| Tier | Implemented surface/light behavior |
|---|---|
| 0 — Off | Existing native HDR raster, proxy grounding shadows, froxel atmosphere and spatial AA. |
| 1 — Low | Primary triangle query/material evaluation; GGX direct lighting; one importance-sampled direct-light visibility query choosing sun/local light, with sun angular jitter. No secondary surface bounce. |
| 2 — Medium | Tier 1 plus cosine-weighted RTAO within 1.5 m and one specular reflection hit with lighting/environment evaluation. |
| 3 — High | Visibility for every contributing direct light, reflection, one cosine-weighted diffuse GI bounce, finite-step medium transmission and geometry-occluded froxel light injection. |
| 4 — Path tracing | Stochastic opaque-material reference integration, Lambert/GGX mixture and PDF compensation, sampled direct lighting, emission/environment, 2–4 secondary surface bounces, Russian roulette after the second interaction, accumulation and spatial filtering. |

Tier 1's one-ray budget describes shadow visibility, not total traversal: a separate primary query finds geometry. Tier 4's configurable count is secondary surface bounces, beyond the primary interaction. Tier 3 evaluates all contributing lights while Tier 4 samples them; tier number need not imply monotonically increasing cost.

RT controls are in Main menu → Raytracing Quality: enable, preset, bounce count, freeze reference animation, Apply. New scenes default to disabled RT, with stored preset 1 and bounce count 3. Applying settings marks the scene dirty and adds undo when not playing; it does not reset Jolt or Play/Pause state.

## 2.5 Temporal handling and image-quality limits

History invalidates on camera, geometry/material/instance, light, smoke/fog, quality, or bounce changes. Live animation conservatively resets accumulation. Freeze reference animation holds the visual clock for day/night, material animation, and lightning; it does not pause routes or physics. Pause simulation separately for a static reference. Unfreezing resumes the running visual clock.

History is linear HDR, capped at weight 1024. A separate frame/RNG sequence continues beyond that cap. The spatial filter is depth/normal-aware, 3×3 for hybrid tiers and 5×5 for path tracing. There is no motion reprojection, disocclusion repair, advanced temporal denoiser, or learned reconstruction. Live GI around bright emitters can remain visibly noisy. Hybrid rough reflection is a simple single-ray approximation.

The integrator uses the current opaque material model. There is no dielectric water refraction, alpha-tested foliage transport, subsurface scattering, or production offline-renderer parity. Fog combines finite-step transmission and a single-scattering froxel approximation; it is not unbiased volumetric path tracing.

## 2.6 Root signatures and descriptor organization

The shared raster root signature has six parameters: instance CBV `b0`, frame/light CBV `b1`, vertex joint-palette root SRV `t0`, bounded texture SRV table beginning at `t1`, per-instance structured root SRV `t7`, and one root constant at `b2` for group base. Its static anisotropic wrap sampler is `s0`, max anisotropy 8. This shared signature supports broader runtime paths; it does not mean creator GLB imports support skinning/textured PBR.

DXR also has six root parameters: four root SRVs at `t0`–`t3`, a CBV at `b0`, and one table containing four SRVs at `t4`–`t7` plus four UAVs at `u0`–`u3`. Each of three fence slots owns an eight-entry shader-visible `CBV_SRV_UAV` heap. The texture views cover scene depth/color, raw rays, normals, history and filtered output. This is explicit bounded binding, not an engine-wide bindless material/texture system. The inspected DXR root signature has no sampler heap.

Raster RTV/DSV heaps are separate from shader-visible CBV/SRV/UAV heaps. Atmosphere descriptors are assembled in a CPU staging heap and copied into a retired frame-slot heap, selecting raster or ray output safely. Do not overwrite descriptors still referenced by submitted GPU work.

## 2.7 Memory, uploads, synchronization, and queues

`RenderDevice` creates a `D3D12_COMMAND_LIST_TYPE_DIRECT` queue and direct command allocators/list. Raster, compute dispatch, AS work and copies are sequenced on that path. No dedicated asynchronous compute/copy queue overlap is established for the delivered creator. Compute shaders do not imply a separate compute queue.

`ComPtr` owns D3D12 objects. DXR uses committed default-heap resources for GPU images/AS and upload heaps for mapped constants/instances/AS descriptions. Constant stride is `(sizeof(Constants)+255)&~255`; per-slot slices prevent in-flight overwrite. Shared instance constants also use 256-byte alignment. This is fixed frame-slot allocation, not a general streaming ring allocator or virtual memory residency manager.

Build/hash structs in ordinary CPU memory, then `memcpy` into mapped upload memory. **Never read or perform read-modify-write hashing on write-combined uploads.** That caused the initial ~114 FPS CPU limit and was corrected in the delivered build.

AS resources remain in `D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE`; UAV barriers order build/read dependencies. Ray textures transition between UAV and non-pixel SRV use. Scratch buffers requiring UAV state are created in COMMON and explicitly transitioned, resolving a debug-layer warning. Timestamp queries resolve into readback resources; timing uses queue timestamp frequency.

Four additional full-resolution RGBA16F ray images require about 265 MB decimal at 3840×2160, excluding AS, packed geometry, raster targets and atmosphere. There is no demonstrated global memory budget/residency policy for arbitrarily large worlds.

## 2.8 Lighting, surfaces, atmosphere, and chaos

Terrain has procedural grass/earth/sand/stone/snow detail and interpolated vertex paint, not texture-array biome authoring. Raster grounding shadows use up to 16 spherical bounds; they are not cascaded shadow maps and do not establish terrain self-shadowing. RT tiers provide actual triangle visibility. The creator has no separate SSAO/GTAO pass; geometric RTAO is Tier 2+.

Directional key, sky fill, sky gradient and ambient lighting vary with a 24-hour time-of-day setting and configurable automatic cycle duration. Local light capacity is 32. Point/cone lights expose brightness, reach and cone angle; 180° means point light, smaller angles give aimed spotlights. There is no astronomical sky/cloud/weather simulation.

`deep/engine/chaos_lightning.h` produces deterministic recursive midpoint-displacement arcs, secondary branches and crossed ribbons, exposed to C# through a C ABI. Props have endpoint/spread controls, transforms and undo/save. Purple/pink emissive shading, a white core and flicker accompany a local strike-point light. Chaos Rock has rough dark surfaces with animated emissive fissures; Light-Bound Shard has luminous material and local light. Fissures are shading, not fracture/displacement physics.

`atmosphere.h/.hlsl` uses 160×90×64 RGBA16F froxels with logarithmic depth, advected density noise, up to 16 transformed smoke volumes, directional/local scattering and integration of transmittance. Craxis smoke uses spherical bounds derived from prop scale. Tiers 3–4 add triangle-occluded injection. It is a fixed bounded approximation, not a fluid smoke solver, multiple-scattering volume integrator, or free performance regardless of light count.

Water has animated ripple normals, shallow/deep color, foam and specular response. Raster reflection uses procedural sky; RT can query scene surfaces within its opaque model. Lava has animated crust/hot fissure emission. Neither has physical wave displacement, dielectric refraction, caustics, water/lava reactions or cooling. Bloom, cinematic grading, depth of field and motion blur are not established creator features. Emission and explicit lights must not be conflated: a glowing shader alone is not a general raster GI source.

# 3. Core Engine Systems and Creator Architecture

## 3.1 Threading and ownership

`NativeViewport.cs` is an `HwndHost` with a dedicated managed background render thread owning its native context. Pending scene packets are atomically exchanged; camera state has a separate handoff. WPF owns interaction and much geometry preparation. Resizing recreates the native context and reapplies scene/RT settings. Windowed Play can own a separate native context; resources are not a single globally shared pool across views.

`deep/engine/persistent_workers.h` provides persistent contiguous-range workers. A submitting thread divides a count into chunks, executes the first chunk, wakes workers using generation counters/condition variables, waits, and rethrows captured errors. Workers grow as needed and join before destruction/module unload. This replaces repeated thread creation in renderer preparation. It is not fiber scheduling, work stealing, an arbitrary dependency graph or a general AI/gameplay job system.

Jolt has its own four-worker job system. Terrain/prop collision preparation has asynchronous work. These mechanisms are not unified into one global scheduler. UI-side terrain/sculpt generation can still cause stalls.

## 3.2 Entity/data model and persistence

`SceneData` holds object/character records, terrain, liquid sources, environment and RT settings. `Character` also represents shapes, rocks and effects. Records contain stable identity, asset/shape kind, transforms, color/material, route/speed, floor constraint, light/body parameters and optional sculpt mesh. This is not an archetype ECS.

Native scene parsing produces mesh/instance arrays and caches. Managed mesh bytes are cached with `ConditionalWeakTable`; native geometry content hashes reuse GPU meshes. Transform-only edits avoid geometry uploads, but whole packets still serialize/examine scene data. There is no general scene octree, streaming partition, or incremental entity-delta protocol established. Jolt broadphase and DXR BVHs serve their own subsystems.

JSON scene schema **7** reads versions 1–7. It stores authored settings and sculpt geometry, not live liquid depths or rigid-body state. RT fields are `RaytracingEnabled`, `RaytracingQuality`, `PathBounces`, and `FreezeRaytracingAnimation`. Native packet **V6** reads V1–V6; these numbers are independent of JSON and `.star` versions. V6 adds RT fields after atmosphere/smoke.

Current editor vertices contain ten 32-bit floats: position XYZ, normal XYZ, color RGB and material kind, 40 bytes. This differs from the separate crowd runtime's retained 96-byte skinned vertices. Scene saving uses atomic replacement/backups and revision/stale-file checks. Older editor binaries cannot read schema 7; use preserved older scene backups for rollback. Absolute asset paths and snapshot-size growth remain portability/scaling concerns.

## 3.3 Terrain and mesh sculpting

Terrain is a single 24×24 m heightfield spanning X/Z −12…12, stored at 97×97 samples (0.25 m), bounded to ±20 m in Y, with paint categories and bilinear sampling. Rendering resamples to 193×193 (0.125 m), 73,728 triangles. Extra render samples do not add independent editable height information. Caves, overhangs and stacked surfaces cannot be represented by `y=h(x,z)`.

Terrain tools: raise/lower, smooth, flatten, paint, hill/crater/plateau stamps and stamping a selected transformed shape's upper surface while retaining that shape. Brush falloff is `(1-d²)²` inside normalized radius. Smooth/flatten use exponential strength/time blending. Radius/strength are adjustable; terrain input ray-marches/refines height crossings. Nominal brush ticks are 50 ms. Rendering still rebuilds the full terrain; no continuous brush-path interpolation or dirty GPU patch upload has been delivered.

Object sculpting is separate: Pull, Push, Smooth, Flatten deform actual local mesh vertices after bounds/triangle picking, copy, weld and bounded subdivision. It supports solid shapes/rocks/imported static meshes, excluding grass/effects; imports above 80,000 triangles are refused. A stroke is one undo transaction; right mouse reverses push/pull, Escape rolls back, capture loss/release ends strokes. No voxel cutting, boolean fusion, remeshing, topology repair, UV authoring or advanced large-mesh acceleration is implemented.

## 3.4 Placement, nature, camera, and history

Shapes include cube, block, trapezoid and octagonal column. Nature includes Rock, Boulder, Pebbles, Grass tuft, Chaos Lightning, Chaos Rock, Light-Bound Shard and Craxis Smoke. Geometry is procedural and cached; placement is manual. Pebble clusters/tufts do not constitute a world scatter tool. No vegetation density brush, wind/bending, ecosystem simulation or foliage LOD is delivered.

Move/rotate/scale handles, numeric fields, group selection/box selection, grid/ground/nearby-object snapping and floor constraints exist. Known interaction debt includes approximate rotation, first-eligible handle picking, snapping mainly at release, incomplete placement ghost preview and inconsistent universal Escape transactions outside dedicated tools. History uses scene snapshots rather than a scalable universal command-delta system.

The corner menus are Builder, Characters, Environment, More; music is under More. Middle drag orbits/tilts, wheel zooms (distance 2–60), edges pan, optional arrows pan. Camera travel multiplier is 0.25–6×; tilt/turn 0.2–4×; Frame world and focus views exist. Travel is elapsed-time based with normalized diagonals. These preferences are session-only. Menus/modal dialogs suppress edge travel. It is not a complete FPS free camera or character-follow controller.

## 3.5 Physics and collision cooking

`deep/Physics/PhysicsWorld.cpp` wraps **Jolt 5.3.0**, pinned commit `0373ec0dd762e4bc2f6acdb08371ee84fa23c6db`; the MIT license is retained under `deep/third_party/JoltPhysics/`. The native DLL uses a 1/60 s fixed-step accumulator, four workers, convex dynamic bodies, static triangle shapes, CCD, friction, damping and contact-dependent rolling resistance.

Terrain collision is cached in 36 tiles, each 16×16 cells. Dirty tiles cook asynchronously; old colliders remain active until replacement, and newer edits coalesce. The editor retains the cache through resets. Prop collision is prepared asynchronously when Play starts. Dynamic concave sculpted shapes use convex hulls, so visual concavity and physical collision differ.

Rock/Boulder default to simulated bodies; settings expose enablement and density. Play permits falling/tumbling; Pause holds state; Reset restores authored transforms. Saving resets transient simulation before writing authored state. RT quality changes preserve running/paused physics. Terrain editing/cooking is not a demonstrated live terrain-deformation workflow during active play, nor is high-density coupled performance established.

## 3.6 Fluid solver and body coupling

`TerrainFlow.cs` maintains separate water/lava fields on a 48×48 grid: 0.5 m cells, 0.25 m² area, closed boundaries. Fixed flow step is 0.05 s; water/lava transport coefficients are 3 and 0.25. A donor limiter bounds outgoing transfer to 45% of available quantity per step. At most eight flow steps are processed per preview tick. Sources default to 2 m³ over 3 s, with a 32-source limit. Still clipped water/lava planes also exist separately from finite pours.

`FlowObstacles.cs` rasterizes transformed solid triangles at four samples per cell into occupied vertical intervals/coverage. Conserved quantity is mapped to free-surface elevation accounting for displaced volume; transfer potential uses this elevation. Moving bodies refresh occupancy. Hydrostatic buoyancy compares local water/lava density and body density, with linear/angular drag.

This is obstacle-aware diffusive surface transport, not a 3D incompressible pressure/momentum solver. Thin/subcell solids, overlapping intervals, stacked fluids and concave occupancy are approximate. No splashes, erosion, free-falling liquid jets, water/lava reactions or fully coupled pressure forces. Save stores sources; Pause holds simulation; Reset clears runtime liquid. Visual material time is distinct from simulation time.

## 3.7 Characters, routes, playback, and media

Creator GLB2 import supports static triangle geometry, indices, node transforms and base-color factors, normalized to 1.8 m. Sparse/external geometry buffers are unsupported. Complete texture/UV/PBR import, skeletal playback, retargeting, IK and animation state machines are not delivered here; separate crowd runtime capabilities do not change this.

Routes are `WalkPoint {X,Z}` lists; traversal consumes elapsed time × speed along horizontal segments and can orient yaw. No continuous terrain-height following, obstacle navigation, navmesh, character collision controller, slope constraints or automatic replanning is implemented. Jolt props do not imply route-driven characters have acquired those features.

Play/Pause/Reset and route Repeat work with routes, liquids and physical props. Windowed Play can open an external native viewport; fullscreen uses the embedded viewport. There is no keyframe timeline, scrubbing, animation queue/graph, deterministic replay contract or persisted simulation checkpoint. Freeze RT reference is a separate visual control.

`LocalMusic` uses Windows MCI for local music controls/volume. It is editor audio, not a spatial sound engine, event mixer or exported soundtrack system.

## 3.8 Assets, shaders, and build tooling

`editor/native/build.py` compiles DXR Trace/Denoise and ray-occluded atmosphere Inject with Windows SDK DXC at `10.0.26100.0/x64/dxc.exe`, using `cs_6_5` and `-O3`, and embeds DXIL in `deep/engine/dxr_shader.h`. The shared raster lineage also contains embedded HLSL; do not assume every shader uses one modern offline shader database. No new runtime DXC dependency is required for the embedded RT binaries.

`prepare_renderer.py` generates `editor/native/render_device_editor.h` from the shared renderer and editor-specific shader/layout changes. Edit authoritative source/generator inputs, not only generated headers. The build also invokes CMake for Jolt/physics, obtains MSVC environment through original-workspace tooling, and builds the viewport DLL/native harness. Machine-specific SDK paths, response files and original tooling/asset references mean clean-machine packaging is not yet proven.

There is no delivered unified asset database, material-node graph, hot-reload transaction system, texture streaming, skeletal creator importer or complete game cooker. Geometry caches and embedded shaders solve narrower problems.

## 3.9 AI interface and external coordination

Local CLI modes include `--describe`, `--inspect`, `--commands` and `--feature`, with JSON result paths. Protocol version 1 is separate from scene schema. Operations cover add/move/route/rename/remove/configure/shape/transform/terrain-brush/terrain-water/terrain-stamp. Batch requests use an expected SHA256 scene revision and reject stale saves.

This is authorized local file/command automation, not an embedded cloud model, multiplayer transport or remote API server. Specialized sculpt/physics/RT/media/Play UX is not fully represented by a documented equivalent command set. `FeatureCatalog` mixes roadmap/scaffold statuses and is not the authoritative capability matrix.

Starbus is separate external AI coordination infrastructure. It is not a Starlight runtime subsystem or proof of networked gameplay. Kira, original GameProject sources and paused `deep/automation` work are outside this release; future engine prompts must not implicitly restart or modify them.

# 4. Chronological Implementation History

The following is the recoverable milestone sequence. Earlier phases have no precise completion date asserted. Dates on later records may reflect UTC release time rather than local calendar time.

| Order / recorded date | Completed work or outcome | Boundaries / lessons |
|---|---|---|
| Earlier foundation | Preserved GameProject native D3D12 renderer; initial process separation and root manifests. | Original reference remains distinct from Starlight creator. |
| Modular runtime phase | `.star`/`.startest` reconciliation, DLL project host, Empty/GameProject modules, project GUI and launch-plan workflow. | Does not export `.starscene` games. |
| Controlled optimization phase, now paused | Persistent range workers and 96-byte crowd vertices retained. Job038 reconstructed paired comparison recorded 130.967 → 148.583 FPS (+13.447%). | Specific reconstruction, not cumulative/universal gain. Later acceptance recorded crowd 144.3, club 421.9, Empty 415.5 FPS. |
| Rejected optimization experiments | Job043 mesh-shader candidate differed visually; Job044 mixed pipeline failed initialization. | Not accepted improvements; no delivered mesh-shader system inferred. |
| Creator foundation | WPF/native host, static import, route drawing/playback, selection/transforms, scene save/history, local AI commands, project panel, music. | Missing `SS_NOTIFY` once caused real viewport hit testing to fail despite synthetic message tests; fixed and regression retained. |
| Terrain/flow and UX phase | Heightfield tools/stamps/paint, snapping/effects, finite conservative pours, playback integration, fullscreen floating controls. | Surface flow remained approximate; early sculpt menu was a placeholder, superseded below. |
| 16 September world-quality release | Native-resolution correction, spatial AA, procedural surfaces, denser display terrain, better sky/key/proxy shadows, water/lava shading, nature geometry, camera speed controls, four menus, real object sculpting. Scene schema 5 / packet V4. | `ups=off` alone had not forced native dimensions. Popup coordinates caused edge drift; fixed using physical outer-window bounds and input suppression. Rock seam/weld issues fixed. |
| 17 September chaos release | Placeable lightning/chaos rock/shard/smoke; RGBA16F HDR, tone mapping/day-night, 32 lights, froxels; Jolt fixed ticks, async tiled collision, obstacle flow/displacement/buoyancy. Schema 6 / packet V5. | Physics/collision cooking real, but no full 3D fluid solver. Kept standard indexed mesh path. |
| 17 September DXR release | Capability/fallback, triangle BLAS cache, TLAS updates, five tiers, RT fog, accumulation/filter, UI/persistence/telemetry, native benchmark expansion. Schema 7 / packet V6. | Compute inline RT, not shader tables. Native rendering and prior systems retained. |
| DXR optimization/validation | Write-combined upload reads removed; frame-slot descriptor safety; scratch state transition corrected; RNG decoupled from history cap; matching lightning transforms; readable preset text. | Initial ~114 FPS bottleneck was CPU upload-memory access, not a demonstrated GPU ray limit. Final results supersede diagnostics. |
| Deployment and continuation | Launcher/desktop shortcuts point to dxr-release; prior releases/backups retained; hashes verified; demo Apply/Undo/Save exercised. | No outstanding DXR implementation task remains open. This task adds documentation only. |

No synchronization deadlock or BLAS cache-thrashing incident is invented to satisfy the supplied outline. Verified transform tests show geometry reuse; large edit repacking remains known debt. A stale preview save was correctly rejected after a check regenerated its fixture: that was scene protection working, not a reason to bypass it.

# 5. Current State, Validation, and Next Work

## 5.1 Build and deployment status

Native build completed with exit 0 and no compiler warnings. Managed Release build completed with zero warnings/errors. Current launch target is `editor/bin/dxr-release/Starlight.Editor.exe`; `Launch-Starlight-Editor.cmd` and both known desktop shortcuts select it. Previous chaos/world-quality releases and source backups remain available.

The final native viewport SHA256 is `beeebe605db9697c53ef7d59a04a398dbeae56b2e8f37f10686af9c64f123041`. Full hashes are in `editor/checks/dxr/final-validation.json`. No git history was used for this delivery; a 20-file unified source diff is in `editor/checks/dxr/source.diff`, with source hash records alongside it.

Last recorded physical UI state: DXR preview scene, Tier 1 restored after physically applying Tier 3, then Undo and Save. Do not assume a currently open window has remained unedited or overwrite that fixture during a future check.

## 5.2 Verification scope

| Saved check | Count where recorded | Main coverage |
|---|---:|---|
| DXR | 84 | Tiers/UI, migration/rejection, save/load, native dimensions, accumulation/reset, BLAS reuse, terrain rebuild, frozen chaos and Jolt compatibility. |
| Chaos | 30 | Props/settings/history, fixed physics, collision cooking, falling/rolling/floating, obstacle flow/displacement. |
| World quality | 26 | Sculpt brushes/history/save, native size, camera scaling and pointer behavior. |
| Interaction | 33 | Camera/select/transform/capture-loss; 150 transforms with zero geometry uploads. |
| Terrain | 24 | Terrain/stamps/paint/water, roundtrip, snapping, effects and music. |
| Flow | 11 | Conservation, finite depths, downhill/pooling, slower lava and playback. |
| Corner controls | PASS report | Fullscreen/windowed floating controls. |

The prior-feature suites passed; the DXR regression was rerun after the upload-memory fix. A subsequent XAML-only contrast change was rebuilt and physically verified through select/apply/undo/save; the native benchmark binary did not change. Final D3D12/DXGI debug validation recorded zero warnings/errors. Forced unsupported fallback requested Tier 4, activated Tier 0, and reported zero BLAS/ray work.

These are bounded tests, not exhaustive correctness, device-loss, all-GPU, huge-world or final artistic acceptance.

## 5.3 Current performance evidence

RTX 5090; native 3840×2160 render/output; no upscaling/FG; cap 500; 2 s warmup + 3 s measurement. Every measured frame was foreground. Fixed snapshot includes terrain, liquid, chaos and 48 rock/grass props. Tier 4 uses three secondary bounces. It does not stress heavy physics and flow concurrently.

| Tier | FPS | GPU ms | Main ray ms | TLAS ms | Filter ms | Frame P99 ms |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 419.529 | 1.90321 | 0 | 0 | 0 | 2.796 |
| 1 | 319.715 | 3.10057 | 0.60149 | 0.04511 | 0.09789 | 3.8126 |
| 2 | 273.847 | 3.62241 | 1.11050 | 0.05173 | 0.11358 | 4.3135 |
| 3 | 186.051 | 5.34043 | 2.74750 | 0.05120 | 0.12601 | 6.1089 |
| 4 | 248.157 | 4.00759 | 1.60092 | 0.03209 | 0.14776 | 4.5754 |

Tier 4 can be faster than Tier 3 because direct-light sampling differs. Main ray time excludes froxel injection rays; those remain in total GPU time. Cold BLAS construction is outside the warmed frame measurement. GPU time and presented frame interval are different metrics.

The manifest also records ten native headless tier/resolution runs. Hidden-window presentation was throttled by Windows around 20 FPS; use GPU timestamps, not that FPS, for render-cost interpretation. Use only `final-*` reports for delivered-code comparisons. Short measurements provide useful local evidence but not thermal/endurance or arbitrary-world guarantees.

## 5.4 Operational, partial, and absent capabilities

| Area | Current status | Remaining boundary |
|---|---|---|
| Native viewport / HDR / RT | Operational and checked | Windows only; basic denoising; opaque transport. |
| Terrain/sculpt | Operational and checked | Finite heightfield, full render rebuilds, no topology system. |
| Nature/chaos props | Operational and checked | Manual placement, no scalable scatter/foliage LOD. |
| Physics | Operational Jolt integration | Convex dynamic approximation, collider delay, no high-density coupled acceptance. |
| Fluids | Operational approximate solver | 2D surface flow and sampled obstacles, no 3D pressure solver. |
| Lighting/fog | Operational approximations | Raster proxies, bounded lights, no multiple scattering/weather. |
| Character routes | Operational static-model traversal | No skeletal locomotion/terrain navigation. |
| Scene history/save | Operational | Snapshot cost, absolute paths, version compatibility. |
| AI interface | Partial workflow coverage | Not all GUI tools exposed, catalog status incomplete. |
| ECS/task graph/world streaming | Not delivered as general systems | Existing narrow workers/caches are not substitutes. |
| Gameplay/scripts/export/networking | Not delivered as complete creator systems | Separate project host is real but insufficient. |
| Spatial audio/particles/animation graph | Not delivered creator features | Local music and shader effects are narrower. |

## 5.5 Known debt and bottleneck candidates

1. **Temporal quality:** live GI noise; no motion reprojection, robust disocclusion, or advanced denoising. Frozen reference accumulation cannot substitute for moving-scene quality.
2. **Edit latency:** whole terrain/sculpt generation, serialization and synchronous geometry/BLAS repacking; benchmark sustained strokes and P99 rather than only steady-state frames.
3. **Memory scaling:** native-resolution RT images, duplicated contexts, full snapshots and no global residency policy.
4. **Simulation coupling:** asynchronous collider lag, convex dynamic bodies, coarse obstacle coverage and uncertain high-density physics/flow cost.
5. **Content realism:** procedural materials, incomplete GLB material pipeline, opaque water, approximate reflection roughness, no foliage wind/LOD.
6. **UX/runtime parity:** incomplete navigation/animation, placement/rotation debt, creator versus project-runtime boundaries and incomplete command coverage.
7. **Reproducibility:** fixed local SDK/original-workspace dependencies, generated header transformations, no proven portable package.

These are known limits or candidates requiring measurement, not claims that each is currently the dominant frame bottleneck. There is no unresolved final-build/debug error recorded by the release manifest.

## 5.6 Proposed next milestones — not an active implementation queue

No additional engine work is currently in flight from this documentation request. The old optimization automation and Kira work remain paused. The following ordering is technical advice for future prompts, subject to the owner's choice:

1. **Moving-scene RT quality and profiling:** establish camera/light/body-motion test scenes, capture GPU/CPU/pass timing and image sequences, then add history reprojection/disocclusion and a bounded temporal/spatial denoiser. Preserve native dimensions and fallback.
2. **Incremental terrain/geometry updates:** revisioned dirty regions, render chunks, async preparation and safe GPU retirement; coordinate collision and BLAS invalidation. Measure continuous edits and memory, not just static FPS.
3. **Material/asset pipeline:** durable material identity, texture/PBR ingestion, portable references and cache/version rules before photoreal terrain packs. Define alpha/refraction requirements explicitly.
4. **Nature scaling:** seeded scatter, density/slope exclusions, instancing, LOD and optional wind with matching RT/collision policy.
5. **Gameplay movement:** terrain-following character controller, collision/slopes, skeletal animation and state transitions. Define terrain-edit behavior before promising navigation reactions.
6. **Simulation scale:** body-count/obstacle-update benchmarks and conservation tests; choose whether to extend surface flow or create a distinct 3D solver rather than relabeling current flow.
7. **Production features:** shared scene execution/export, scripting/events, spatial audio and compute particles, automated clean-machine builds and platform-specific acceptance.

Each prompt should name the current source modules, preserve scene migration/undo/save, define a bounded implementation and workload, and ask for before/after evidence. Do not request “integrate Jolt,” “add DXR,” or “implement sculpting” from scratch; those exist. Ask for the specific missing capability.

# Appendix A. Source and Evidence Map

| Path | Responsibility |
|---|---|
| `deep/src/runtime.cpp`, `preset_reconciliation.inc` | Project DLL host and settings reconciliation. |
| `deep/engine/render_device.h` | Shared renderer, frame lifecycle, resources, raster/crowd paths. |
| `deep/engine/persistent_workers.h` | Persistent range workers. |
| `deep/engine/dxr.h`, `dxr.hlsl` | AS/resources, inline transport, history/filter and timings. |
| `deep/engine/atmosphere.h`, `atmosphere.hlsl` | Froxel injection/integration/composite and RT occlusion. |
| `deep/engine/chaos_lightning.h` | Native branching ribbon generation. |
| `deep/Physics/PhysicsWorld.cpp` | Jolt wrapper, collision cooking and body simulation. |
| `editor/MainWindow.xaml`, `.xaml.cs` | Main controls and scene/edit orchestration. |
| `editor/SceneData.cs`, `EditorHistory.cs` | Authored data, persistence/migration and history. |
| `editor/NativeScene.cs`, `NativeViewport.cs`, `NativeEditor.cs` | Packet encoding, native thread/bridge and preview lifecycle. |
| `editor/native/viewport.cpp` | Native packet parsing, geometry reuse, draw/RT integration and exports. |
| `editor/native/world_surface.hlsl`, `edge_aa.h` | Procedural surfaces/effects and spatial AA. |
| `editor/native/build.py`, `prepare_renderer.py`, `standalone.cpp` | Build/generation and native benchmark. |
| `editor/TerrainData.cs`, `TerrainTools.cs`, `SculptTools.cs` | Terrain and mesh authoring. |
| `editor/TerrainFlow.cs`, `FlowTools.cs`, `FlowObstacles.cs` | Flow, playback integration and occupancy. |
| `editor/NatureGeometry.cs`, `ObjectGeometry.cs`, `ChaosTools.cs` | Props, geometry and chaos/RT controls. |
| `editor/BuildingTools.cs`, `PointerInteraction.cs`, `ObjectSnapping.cs`, `WorldNavigation.cs` | Input, transforms/snapping and camera. |
| `editor/GlbModel.cs`, `LocalMusic.cs`, `EditorCommands.cs` | Import, editor audio and local automation. |
| `editor/DxrChecks.cs`, other `*Checks.cs` | Repeatable bounded regression harnesses. |
| `editor/checks/dxr/final-validation.json` | Final binaries, check scope, performance and hashes. |
| `editor/checks/dxr/source.diff`, `changed-sources.json` | Detailed implementation diff/source records. |
| `editor/checks/dxr/final-2160-tier*-focused.json` | Authoritative foreground 4K tier results. |
| `editor/checks/dxr/final-debug.log`, `final-fallback.json` | Debug layer and fallback evidence. |
| `editor/backups/dxr-upgrade-20260917/` | Before-images; prior release directories provide rollback. |

# Appendix B. Reproduction and Preservation

Build from the workspace root with the existing local toolchain:

```powershell
python editor/native/build.py
dotnet build editor/Starlight.Editor.csproj -c Release -o editor/bin/dxr-release
```

If that build is running, use a new output directory; do not overwrite locked binaries or terminate a window with unreviewed edits. These commands are reference instructions, not commands executed to generate this document.

Regression switches: `--dxr-check`, `--chaos-check`, `--quality-check`, `--interaction-check`, `--terrain-check`, `--flow-check`, `--chrome-check`. Some checks regenerate preview fixtures; close an unmodified fixture first and preserve user changes. Never bypass the stale-file guard.

```text
Starlight.NativeCheck.exe scene.packet report.json capture.bmp width height [tier] [headless|focused] [bounces]
```

The benchmark freezes visual time, warms for 2 s and measures for 3 s. Compare identical scene/camera/native size/light state, assert focus for foreground tests, and report cold-build/edit costs separately. Debug settings: `STARLIGHT_D3D_DEBUG=1`, `STARLIGHT_NATIVE_LOG=<absolute path>`; forced fallback: `STARLIGHT_DISABLE_DXR=1`.

Telemetry exports include `sl_rt`, `sl_freeze`, `sl_rt_supported`, `sl_rt_active`, `sl_ray_ms`, `sl_as_ms`, `sl_denoise_ms`, `sl_blas_builds`, `sl_tlas_refits`, `sl_rt_samples`, `sl_history_resets`, `sl_rt_status`.

Preserve authored scenes, prior release binaries and rollback backups. Do not rerun retired one-shot migration helpers. Do not resume paused automation/Kira or alter external infrastructure as an incidental step in renderer work. Local paths in this document identify evidence; sharing the document does not grant another AI filesystem access.
