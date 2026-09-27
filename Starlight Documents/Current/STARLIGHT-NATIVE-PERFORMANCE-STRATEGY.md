# Starlight: Native-Resolution Rendering and Simulation Strategy
> **Latest implementation (2026-09-17):** multi-tier DXR 1.1 is now implemented. See [DXR-RELEASE.md](editor/DXR-RELEASE.md) for code architecture, verified tier behavior, native performance, and reference-rendering limits.
> **Implementation update:** a bounded first set of chaos, HDR, fog, rigid-body and obstacle-flow features has since shipped. See [the release record](editor/CHAOS-RELEASE.md). This strategy remains a broader roadmap, not a claim that all its modules are complete.

**Date:** 2026-09-16  
**Status:** Proposed engineering roadmap; no implementation or new benchmark is implied.  
**Audience:** Gemini and engineers evaluating Starlight's next development stages.  
**Companion:** [Current-state technical specification](STARLIGHT-TECHNICAL-SPECIFICATION.md).

## 1. Mandate and realistic acceptance criteria

Build high visual fidelity and interactive simulation through efficient geometry, memory management, scheduling, and rendering. The baseline must render at the display's native resolution with **no DLSS, FSR, XeSS, dynamic scene-resolution reduction, or generated frames**. Existing optional upscaler integrations must not participate in baseline measurements. Record these settings in every benchmark artifact.

Native resolution does not imply infinitely detailed geometry or unlimited simulation. World size can grow through streaming while resident working sets remain finite. Fog can have predictable cost through fixed work limits, but arbitrary overlapping lights, fluid volumes, and visible geometry cannot have constant cost. Overload behavior must be explicit and measurable.

Proposed initial performance tiers are native 2560×1440 at 120 actual frames/s and native 3840×2160 at 60 actual frames/s on the development machine. These are **validation targets, not measured capabilities or promises**. Benchmark first and revise content envelopes before promising them publicly. The editor's current 500 FPS ceiling is not a production scene performance target.

Require separate reporting of CPU critical-path time, GPU frame time, presentation intervals, input latency, and P50/P95/P99 frame times. Record hardware, driver, resolution, scene revision, camera path, active bodies, fluid cells/particles, visible triangles, memory residency, and warm/cold cache conditions. CPU and GPU budgets overlap; do not add them as if they execute serially.

### Suggested initial GPU budget at the 120 FPS tier

| Work | Planning allocation |
|---|---:|
| Visibility, depth, opaque geometry and materials | 2.8 ms |
| Shadow rendering, surface lighting and AO | 1.6 ms |
| Water/lava surface rendering and transparency | 0.7 ms |
| Atmosphere and volumetric fog | 0.7 ms |
| GPU fluid simulation | 1.0 ms |
| Native AA, post-processing and composition | 0.5 ms |
| Headroom | 1.03 ms |

These allocations sum to 8.33 ms and deliberately expose how little room remains for expensive fluids. They are starting hypotheses. GPU simulation consumes the same GPU budget as rendering, even when dispatched on an asynchronous compute queue. Measure actual overlap and contention.

Full scene shading stays native. Reduced-resolution *internal representations*, such as shadow maps and a volumetric integration grid, are permitted and must be disclosed; they are not whole-frame hardware reconstruction. If full-resolution volumetric ray marching is required, benchmark it as a separate, more expensive mode. Optional temporal AA must consume native-resolution frames, with a spatial-AA alternative for evaluation; no synthetic intermediate frames.

## 2. Starting point and architectural constraint

The current creator uses a C# WPF shell and an embedded C++ DX12 viewport. It has native-resolution rendering, inexpensive spatial edge smoothing, a fixed-size heightfield, mesh sculpting, procedural surface appearance, manually placed nature objects, and a two-dimensional depth-based water/lava flow model. The current flow model has no rigid-body coupling or general solid obstacle collision. Shadows approximate spherical object bounds; there is no production shadow-map system or volumetric atmosphere.

The editor scene and modular C++ project runtime are separate execution paths. Managed scene construction and mesh rebuild/upload remain scalability constraints. Existing persistent range workers are useful, but are not a general dependency scheduler. Native mesh-shader experiments were rejected; they must not be described as accepted performance gains.

Consequently, adding a sophisticated fluid shader alone cannot satisfy this roadmap. The central prerequisite is shared native world ownership with incremental editor commands and stable render/simulation snapshots.

## 3. C++ module boundaries

The following paths and interfaces are proposed, not existing APIs.

| Proposed module | Responsibility and ownership |
|---|---|
| `World/` | Stable entity/asset IDs, chunk coordinates, component arrays, revisions and edit transactions |
| `Jobs/` | Dependency handles, priorities, worker-local scratch, cancellation and bounded background queues |
| `Assets/` | Import/cooking, content hashes, mesh LODs, texture mips, collision data and package manifests |
| `Streaming/` | Spatial demand, asynchronous reads/decompression, upload scheduling, residency and eviction |
| `Render/` | DX12 frame graph, GPU scene buffers, visibility, materials, lighting, fog and native presentation |
| `Physics/` | Rigid-body backend adapter, broadphase, contact solving, queries and collision cooking |
| `Fluids/` | Simulation regions, obstacle fields, sparse 3D solver, body coupling and surface extraction |
| `Terrain/` | Heightfield tiles, optional volumetric regions, sculpt edits, dirty bounds and collider revisions |
| `EditorBridge/` | Versioned C ABI for commands, queries, diagnostics and asynchronous completion |
| `Diagnostics/` | CPU/GPU markers, residency counters, replay inputs and reproducible benchmark records |

Use handles across module boundaries rather than owning pointers. Store bulk state in compact arrays; use immutable snapshots or explicit ownership transfer for render and simulation consumption. Keep WPF as the UI shell while moving world algorithms into C++ incrementally.

Illustrative contracts:

```cpp
struct WorldPosition { int64_t cell[3]; float local[3]; };
struct Revision { uint64_t value; };
struct EditCommit { EntityId entity; Revision revision; Bounds dirty; };
struct StepContext { uint64_t tick; double dt; WorldSnapshot world; };

class PhysicsWorld {
public:
    void Step(const StepContext&, Span<const BodyImpulse>);
    BodySnapshot ReadBodies() const;
};
class FluidWorld {
public:
    FluidStepResult Step(const StepContext&, const BodySnapshot&,
                         const ObstacleSnapshot&);
};
class WorldStreamer {
public:
    void Update(const StreamingDemand&, const ResidencyBudget&);
};
```

These sketches omit error handling and synchronization. Cross the managed/native boundary using a versioned C ABI with explicit sizes and lifetimes, not C++ classes or STL containers. Batch edits; publish dirty ranges rather than reserializing the complete world on every brush sample. Replace large whole-scene undo snapshots with bounded, persistent edit deltas and periodic checkpoints.

### Scheduling and frame lifecycle

1. Accept editor/input commands into a transaction queue.
2. Apply transactions at safe world boundaries and schedule affected asset/collider work.
3. Execute fixed simulation ticks against a consistent world revision.
4. Publish body, fluid and render snapshots with generation IDs.
5. Build GPU visibility and render passes; present a genuinely rendered native frame.

Use a bounded fixed-step accumulator, initially evaluating 60 Hz physics. Render interpolation may interpolate simulation transforms; it must not generate display frames. Under overload, report simulation debt or slow simulation explicitly instead of silently discarding physical time. Pause/reset/repeat must govern simulation and visual animation consistently.

## 4. High-density bodies and three-dimensional fluids

### 4.1 Rigid-body foundation

Evaluate Jolt behind `PhysicsWorld` before considering a custom general-purpose solver. It supplies a C++ rigid-body foundation; it does not supply the proposed water/lava system. Evaluate licensing, integration effort, determinism conditions, large-world precision, and actual workload performance before selecting it. [Jolt source and documentation](https://github.com/jrouwe/JoltPhysics).

Use collision layers, broadphase spatial partitioning, sleeping islands, and selective continuous collision detection. Separate collision complexity from render complexity. Static environments can use cooked triangle meshes; moving concave objects generally need suitable convex compounds or another supported representation. Reserve detailed contact processing for bodies near active gameplay.

Terrain sculpting creates a versioned collision update. Build replacement collision data asynchronously; keep the previous valid collider until the new revision is safe to install. Show pending collision state when it affects playtesting. Resolve penetrations caused by terrain edits through a documented policy, rather than teleporting bodies unpredictably. Never evict collision under an active body merely because its visual tile is offscreen.

### 4.2 Fluid representations by purpose

Retain the existing depth-field flow as an inexpensive option for distant or shallow terrain-bound water. It cannot represent water under a bridge and on the bridge independently, overturning flow, or general three-dimensional obstacle blocking.

For local interactive volumes, prototype a **sparse 3D staggered velocity grid with a liquid free surface**, evaluating a hybrid particle/grid method such as FLIP or APIC. Store velocity, pressure, liquid occupancy, obstacle distance/velocity, and material parameters. Activate spatial bricks around occupied liquid and required boundary/solver neighborhoods. Pressure propagation still spans the connected active domain; sparse storage does not remove global pressure-solve cost.

A pressure-based solid/fluid formulation is an established route to consistent interaction, but implementation details and convergence determine robustness. Use the research as a numerical foundation, not a claim of guaranteed real-time performance. [Batty, Bertails and Bridson: variational solid-fluid coupling](https://www.cs.ubc.ca/labs/imager/tr/2007/Batty_VariationalFluids/variationalFluids.pdf).

Prototype against a particle-constraint alternative only if it offers a measurable advantage for the intended scenes. Avoid maintaining several production solvers before the first one has clear performance and correctness evidence.

### 4.3 Integration sequence

1. Predict body transforms and velocities for the simulation substep.
2. Build/update obstacle signed-distance or cut-cell data, including moving boundary velocities.
3. Transfer/advection steps update liquid velocities and occupancy; apply gravity and applicable viscosity.
4. Enforce solid boundaries and solve pressure to reduce divergence.
5. Extract pressure/viscous forces and torques consistently for body coupling.
6. Resolve the coupled state, update the liquid surface, and publish a coherent tick result.

First deliver static obstacles and one-way moving obstacles. Then add two-way coupling with conservation tests. Do not add independent buoyancy on top of pressure-derived forces if that double-counts the same effect. Strong coupling can require iteration; a single delayed impulse exchange is an approximation and must be labeled accordingly.

A CPU-body/GPU-fluid split introduces synchronization and readback latency. Prototype a small CPU reference path first, then compare bounded-latency GPU coupling with stricter synchronized coupling. Never insert an unconditional GPU readback stall into every render frame. Retain tick IDs and reject stale results. Gameplay requiring precise contacts may need a more tightly coupled execution path than decorative fluids.

### 4.4 Robustness, materials and scale limits

Choose substeps from velocity, cell width and stability requirements, with explicit iteration and active-cell budgets. Monitor pressure residuals, divergence, volume drift, penetration and solver debt. Thin walls smaller than the collision grid require conservative treatment or local refinement. A pretty surface mesh must not conceal leaking collision boundaries.

Water starts with low viscosity and suitable free-surface behavior. Lava adds higher viscosity; temperature-dependent viscosity, cooling, solidification and water/lava reactions belong to later stages. A very viscous fluid can require a different numerical treatment, such as an implicit viscosity solve. Red glow is not a physical lava model.

Scale by limiting high-resolution active regions and scheduling distant regions at lower fidelity where valid. Region transitions require conservative transfer of mass and momentum, including fluxes at interfaces. Do not arbitrarily delete particles to meet a budget. If a domain exceeds supported limits, preserve its state, throttle visibly, or reject further expansion with a clear diagnostic.

Required tests: sealed tank, dam break, thin obstacle, moving gate, floating and sinking bodies, rotating paddle, stacked obstacles, fluid crossing region boundaries, and terrain edits during flow. Use controlled reference scenes to define tolerances for volume drift and momentum accounting, including external forces. Long runs must show no sustained leakage or unstable energy growth beyond the documented tolerance.

## 5. Scalable geometry, terrain and asset realism

### 5.1 Large-world representation

Use integer chunk coordinates plus local positions, with a render origin near the camera. Do not repeatedly accumulate world-scale float translations. Keep render-origin changes separate from simulation coordinates and streaming IDs.

Build independently streamable terrain and mesh pages with bounds, hierarchy links, geometric error, material references and separate collision resources. Terrain heightfields remain efficient for ordinary ground; local volumetric terrain or authored meshes provide caves, cliffs and overhangs. Define seam ownership between representations and rebuild only affected regions after sculpting.

### 5.2 Streaming and LOD pipeline

Cook assets offline into compressed geometry, texture mip chains and collision packages. Runtime demand combines camera visibility, projected error, predicted movement, editor selection and physics proximity. Cap I/O, decompression, upload bytes and resident memory separately. Publish resources only after the relevant GPU fence completes.

Keep a resident parent LOD or low-resolution texture fallback while finer data arrives. Use hysteresis to prevent LOD oscillation and geomorphing or controlled transitions to reduce popping. Terrain seams must remain closed; independently selecting neighboring tile LODs without stitching is insufficient.

Begin with GPU culling and conventional indexed indirect draws. Add meshlets and a mesh-shader path only after a matched benchmark proves a benefit and visual correctness. Feature-query capabilities and retain the indexed path. Microsoft's mesh-shader specification defines the programmable geometry path; using it is not itself evidence of a speedup. [DirectX mesh-shader specification](https://github.com/microsoft/DirectX-Specs/blob/master/d3d/MeshShader.md).

Texture residency can later use sampler feedback on supported hardware; fallback demand estimation must remain available. Feedback helps identify sampled texture regions, while the engine still owns loading, eviction and fallback policy. [DirectX sampler-feedback specification](https://microsoft.github.io/DirectX-Specs/d3d/SamplerFeedback.html).

Unbounded disk-backed world extent is a product goal; unlimited visible detail or resident simulation is not. Define memory headroom against measured available VRAM and process memory budgets. Test rapid travel, teleporting, camera reversals and editor selection of distant objects.

### 5.3 Photoreal surface and nature pipeline

Introduce licensed or original scanned/authored assets: albedo, normals, roughness, metallic where relevant, and optional displacement. Cook mipmaps, appropriate block compression, tangent data and material metadata. Use anisotropic filtering for oblique terrain. Higher image resolution alone cannot fix silhouette aliasing, poor lighting or low geometric detail.

Replace color-only terrain categories with tiled material layers and editable blend masks. Combine height-aware blending, slope rules and controlled variation; retain procedural detail as a supplement. Avoid baking strong directional illumination into albedo. Use real geometric detail near the camera where silhouettes require it and simpler representations at distance.

Nature tools should support deterministic scatter seeds, density/scale/orientation distributions, slope and exclusion masks, plus manual edits. Stream clustered rock instances and vegetation patches. Grass needs distance LOD, bounded alpha overdraw, wind and optional local interaction; millions of transparent blades are not free simply because draw calls are instanced.

## 6. Lighting, water appearance and atmosphere

### 6.1 Lighting foundation

Add linear HDR scene buffers, exposure and tone mapping before tuning advanced effects. Evaluate clustered forward lighting against deferred lighting using Starlight's opaque/vegetation/transparent mix. Maintain explicit limits on lights per cluster and transparent workload, with diagnostics for overflow.

Replace spherical shadow approximations with actual geometry shadowing: directional cascades and a bounded local-light shadow atlas. Cache static contributions, update dirty regions, and prioritize visible lights. Add geometric ambient occlusion, environment lighting and reflection probes as separate, measurable passes. Emission affects material appearance; it needs an explicit lighting mechanism to illuminate neighbors.

Ray tracing can be an optional measured quality tier, since the mandate forbids dependence on upscaling rather than ray tracing. It must not be required to meet the baseline, and its acceleration-structure build/update cost must be included.

### 6.2 Water and lava surface rendering

Render extracted liquid geometry using physically motivated absorption, Fresnel response and suitable reflection/refraction approximations. Handle underwater views, depth discontinuities and screen-space failure cases explicitly. Use probes or environment fallbacks when screen-space reflections miss. Foam should respond to shoreline contact or simulated motion where available.

Lava combines crust geometry/material state, emissive interiors and temperature-driven appearance once temperature exists. Bloom is an optional HDR effect; neither bloom nor animated crack color implies geometric displacement, local lighting, cooling or collision. Keep visual-only animation distinct from simulated state in both code and UI.

### 6.3 Moving volumetric atmosphere

Use atmosphere lookup tables for broad sky/aerial perspective, rebuilt when relevant parameters change. For local fog, prototype a frustum-aligned 3D grid with logarithmic depth distribution. Populate extinction/scattering from authored volumes, height functions and advected noise; inject selected lights and integrate transmittance/scattering along view rays. This general grid approach has established real-time precedent. [Wronski: volumetric fog and atmospheric scattering](https://www.advances.realtimerendering.com/s2014/wronski/bwronski_volumetric_fog_siggraph2014.pdf).

Reproject history only when valid, rejecting it for camera cuts, disocclusions and rapidly changing density or lights. Test ghosting around moving objects and light shafts. Composite at native output resolution with depth-aware treatment at silhouettes. Internal fog-grid resolution is a visible quality setting, not an undisclosed reduction of the main scene resolution.

Bound grid dimensions, integration range, lights per cell, shadowed fog lights and overlapping density volumes. Animate ordinary fog with velocity fields/noise; do not run a full smoke solver throughout the world for ambience. Gameplay smoke can use separately budgeted local simulation domains.

The promised property is **bounded work for a declared configuration**, not constant milliseconds under all content. Quality changes may reduce fog detail, shadow updates or distant decorative density; they must not silently disable gameplay collision or change conserved fluid quantities.

## 7. Delivery order and exit gates

| Stage | Deliverable | Exit evidence |
|---|---|---|
| 0. Establish evidence | Native benchmark harness, markers, settings and memory telemetry | Reproducible baseline across fixed scenes and camera paths; no reconstruction or generated frames |
| 1. Shared world foundation | Native commands, revisions, dirty updates and safe snapshots | Editor and playtest consume the same world data; edit/cancel/undo/save regression checks pass |
| 2. Geometry and material vertical slice | Streamed terrain tiles, mesh LOD, authored PBR assets and real shadows | Large traverse stays within memory/upload budgets, with no holes or missing collision |
| 3. Physics vertical slice | Bodies, cooked terrain/object collision and live-edit replacement | Stable stacking, selective CCD, scene queries and safe sculpt updates |
| 4. Local 3D liquids | Reference solver, static/moving obstacles, then two-way coupling | Tank/gate/paddle tests satisfy declared conservation and stability tolerances |
| 5. Atmosphere and liquid visuals | Bounded fog, HDR, fluid surfaces and explicit quality controls | Native target scenes fit measured budgets; temporal artifacts reviewed |
| 6. Scale and product integration | Streaming transitions, vegetation, persistence and creator controls | Mixed workload soak tests, cold loads, teleport tests and save/reload survive without leaks |

Deliver a small usable scene at each stage. Avoid replacing the entire renderer before a vertical slice demonstrates the new contracts. Do not resume the paused optimization worker merely to execute this plan; implementation is a separate task.

## 8. Verification and decision rules

Benchmark at least: empty/editor overhead; dense rocks and grass; large terrain travel; many active and sleeping bodies; local water interacting with bodies; viscous lava; multiple moving fog lights; and a combined stress scene. Record active counts and spatial distribution so “high density” has a reproducible meaning.

Use repeatable camera/input recordings, separate warm-up, multiple runs and sustained thermal conditions. Report both GPU timestamp duration and actual presentation cadence. Compare candidate changes against the same executable baseline, scene and settings. Existing historical crowd results with mismatched conditions cannot establish a new renderer's advantage.

For pure optimizations, preserve output or explain bounded numeric differences. For intentional visual changes, create explicit new visual acceptance references instead of pretending exact image equality remains possible. Validate native rendering dimensions and disabled reconstruction in automated startup diagnostics.

Maintain independent gates for visual quality, performance, physical correctness, persistence and editor responsiveness. A faster solver that leaks through walls fails; a beautiful scene that stalls sculpting also fails. Reject speculative complexity when a simpler measured solution meets the workload.

## 9. Scope still outside this strategy

This roadmap prepares a credible rendering and simulation foundation. Rivaling Unreal or Unity as a complete creator platform also requires mature asset import, animation, gameplay scripting, navigation, audio, packaging, profiling, documentation and compatibility support. Those systems need separate specifications and integration plans; they are not delivered by adopting a fluid solver or mesh streaming alone.

For outside review, treat every module above as a proposal. Use the companion current-state specification to distinguish existing code from future work. No new performance results were produced while drafting this strategy.
