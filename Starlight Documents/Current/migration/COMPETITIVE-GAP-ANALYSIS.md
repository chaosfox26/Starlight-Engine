# Starlight vs. Unreal Engine vs. Unity

Date: 2026-09-17. Scope: this repository's native editor and runtime, compared with documented Unreal Engine 5 and Unity 6 capabilities. This is a capability assessment, not a comparative benchmark. No matched Unreal/Unity projects were run. Performance advantages remain hypotheses until measured on equivalent content, settings and hardware.

## Position

Starlight has a credible specialist foundation: a native Windows editor, direct D3D12 ownership, native-resolution HDR rendering, inline DXR, two denoising paths, Jolt-backed bodies, terrain editing and a small build-and-test workflow. Its strongest opportunity is making a constrained set of creation tasks easy and fast. It is not currently a general-purpose replacement for either established engine.

The largest gaps are the content pipeline, world scale, authoring completeness, observability and reliable distribution. Adding another rendering toggle does less for everyday quality than ensuring imported materials, saved projects and packaged scenes behave consistently.

## Core systems and architecture

| Area | Starlight: source-backed current state | Unreal / Unity reference | Practical gap |
|---|---|---|---|
| Native foundation | Win32/GDI+ C++ editor, C++ runtime, CMake/Ninja; no active CLR dependency. RAII ownership and standard containers. | Unreal exposes C++ engine/application development; Unity offers engine systems plus its managed authoring ecosystem and native execution options. Language alone does not establish comparative speed. | Keep native-only architecture while adding capabilities behind small interfaces. |
| Task scheduling | `PersistentWorkers` implements renderer-owned worker threads, generation-based wakeup, static chunks, main-thread participation, completion barriers and exception propagation. Serial/granularity fallbacks remain in `render_device.h`. | Unreal's Tasks System supports task dependencies on a shared scheduler. Unity's Job System exposes parallel jobs and dependencies. | Starlight's pool is not a general dependency graph or work-stealing scheduler; scene preparation, import/collision jobs and render work need explicit scheduling and budgets. |
| Scene data | Editor uses `SceneData`, vectors of `Character`, mesh parts and serialized scene snapshots; runtime has specialized draw/instance buckets. | Established engines expose broader entity/component and asset lifecycles. | Do not describe the editor as a fully archetyped ECS. Avoid copying/rebuilding entire scene geometry for every small change; add versioned dirty regions and stable asset handles first. |
| Memory | Smart pointers/COM ownership, frame-slot resources and GPU fences; model caches and history snapshots still have growth risks. | Unreal Memory Insights can attribute and query allocations; Unity provides memory/profiling tools. | Add per-subsystem allocation accounting, cache budgets, high-water marks and automated long-soak checks. |
| Startup | Native startup avoids a managed editor startup path, but no comparable cold-start measurements were taken here. | Both comparison engines support configurable production builds. | Measure cold/warm first frame, usable editor, shader/cache misses and project import separately. Do not claim a startup win from binary language or size. |

References: [Unreal Tasks System](https://dev.epicgames.com/documentation/unreal-engine/tasks-systems-in-unreal-engine), [Unity Job System](https://docs.unity3d.com/6000.0/Documentation/Manual/job-system.html), [Unreal Memory Insights](https://dev.epicgames.com/documentation/unreal-engine/memory-insights-in-unreal-engine).

## Rendering, materials and scale

| Area | Starlight today | Industry comparison / gap |
|---|---|---|
| D3D12 | Explicit descriptors, barriers, Direct queue submission, fence-retired frame slots; HDR scene and tonemapping. | Direct API access is useful control, not automatic superiority. There is no general render graph or independently scheduled asynchronous copy/compute pipeline demonstrated here. |
| Hardware RT | Inline `RayQuery::TraceRayInline`, compute `cs_6_5`, BLAS caching and TLAS update/refit, selectable RT tiers, raster fallback. No RT state objects, shader binding tables or `DispatchRays`. | Unreal has integrated Lumen lighting/reflections; Unity HDRP offers multiple RT effects. Starlight needs wider scene/material coverage and repeatable image/performance regression fixtures. |
| Denoising | Native temporal/à-trous path and optional native-resolution NGX ray reconstruction. No lower internal resolution or frame generation is required. | Denoising quality must be judged on moving emissives, disocclusions, thin geometry and animation, not just static screenshots. |
| Shader support | Mixed native raster shader compilation and DXC compute compilation. Inline RT requires SM 6.5; do not label the entire engine SM 6.6+ or mesh-shader based. | A supported shader model is not equivalent to shipped features. Add shader variant/capability manifests and compilation diagnostics. |
| Draw scaling | Runtime has specialized batching/instancing and parallel preparation. Editor packet creation has an explicit 8,192 draw ceiling (`render_scene.cpp`). | Unreal Nanite provides virtualized geometry; Unity's SRP Batcher reduces state setup for compatible shaders. Neither has a single universal draw-count guarantee. Starlight needs measured CPU recording cost, geometry culling and residency limits. |
| Terrain | 97×97 editable heightfield, subdivided display geometry, surface categories, procedural detail; fixed authored extent. | This is not world partitioning, virtualized geometry or arbitrary overhang terrain. Chunk streaming, mesh LOD transitions, collision residency and distant terrain are substantial remaining work. |
| Textures | New BC5/BC7 DDS loading, authored mip selection and compressed GPU upload. Existing WIC RGBA fallback remains. | Loader support is not a streaming manager. Unity supports mip streaming; Unreal supports texture virtualization. Starlight still needs asynchronous requests, memory budgets, eviction and fence-safe descriptor replacement. |
| PBR authoring | Editor defaults are procedural/vertex-colored. The separate runtime model path has texture sets; the editor does not yet consume a complete 2K/4K PBR library. | A material schema, UV/tangent preservation or triplanar mapping, linear/sRGB rules, packed ORM sampling and raster/RT agreement must precede claims of high-fidelity asset parity. |
| Atmosphere and shadows | Volumetric path and emissive/local light support coexist with simplified raster shadow proxies. | Quality, cost and occlusion correctness vary across paths. Test the same assets under day/night and all RT tiers; avoid inferring shadow-map parity from the presence of shadows. |

References: [Unreal Nanite](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine), [Unreal Lumen](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine), [Unity HDRP RT setup](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Ray-Tracing-Getting-Started.html), [Unity SRP Batcher](https://docs.unity3d.com/6000.0/Documentation/Manual/SRPBatcher.html), [Unity mip streaming](https://docs.unity3d.com/6000.0/Documentation/Manual/TextureStreaming.html).

## Editor usability and workflow

| Area | Starlight strength | Remaining gap |
|---|---|---|
| Simplicity | Floating controls preserve canvas space; familiar Builder/Characters/Environment/More corners; beginner settings hide unrelated fields. | Complete the audited legacy workflow before expanding the visible menu surface. Current pass restores core controls, not every old window and catalogue view. |
| Asset iteration | Native GLB validation and direct placement, sculpt/terrain tools, scene undo/redo. | No complete animated-GLB authoring, automatic texture/material import, dependency tracking, reimport conflict handling, thumbnails or transactional asset database. |
| Project safety | Native file dialogs/path persistence, validation and save hashing; Save/Discard/Cancel restored. | Additional additive-scene, interrupted import, missing asset, device-loss and long-session recovery tests remain. |
| Playtesting | Direct build/test controls, routes, follow option and physics-backed simulation. | Separate windowed game preview still needs restoration. The current native Play implementation remains in the editor viewport. General animation state machines and scriptable gameplay authoring are not supplied by this UI pass. |
| Packaging | Native CMake builds plus project/preset launch. Isolated build output now supports testing without replacing a running editor. | Build success is not a portable distributable. Add a dependency manifest, asset cooker, package staging, clean-machine validation and relocation-safe project paths. |
| Profiling | Native FPS/GPU/ray/denoiser telemetry and executable regression checks. | No integrated frame timeline, allocation attribution, asset residency inspector or hitch investigation UI comparable to mature profilers. Keep these behind Advanced. |

Unreal documents a build/cook/stage/package/deploy workflow; Unity exposes Build Profiles. These are appropriate benchmarks for Starlight's distribution workflow, without copying their full editor complexity. [Unreal packaging](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project), [Unity Build Profiles](https://docs.unity3d.com/6000.0/Documentation/Manual/build-profiles.html).

## Prioritized roadmap and acceptance gates

1. **Finish classic workflow parity.** Complete separate preview-window lifetime, More catalogue/music panel, named material/color selection and remaining project panel behavior. Gate: audited task walkthroughs, keyboard/accessibility checks at minimum size and multiple DPI values, save/reopen parity and stable UI handle counts.
2. **Ship one complete PBR asset path.** Start with grass, earth and weathered rock: licensed 2K albedo, BC5 normal, BC7 ORM, complete authored mips; 4K optional. Gate: real map sampling in both raster and RT, correct channel/color conventions, no missing material on reopen, verified close-up captures and measured residency.
3. **Budget assets and uploads.** Add an import/cook cache keyed by content and settings, asynchronous mip requests, eviction and per-frame upload limits. Gate: a camera fly-through exceeding VRAM budget must remain within its cap with bounded upload stalls and no descriptor reuse before fence retirement.
4. **Scale the world incrementally.** Introduce terrain chunks, geometric LOD, spatial visibility and independently cooked collision tiles. Gate: fixed camera paths through growing worlds maintain bounded resident memory; seams, physics contacts and fluid obstacles remain coherent.
5. **Measure CPU and GPU work consistently.** Collect p50/p95/p99 frame time, render-thread time, simulation time, GPU passes, memory, uploads and cache misses. Gate: the same native-resolution fixtures run in CI and on target hardware with warmup, focus state and configuration recorded.
6. **Make projects distributable.** Cook referenced assets and shaders, stage pure native dependencies, supply relocatable manifests. Gate: open and run on a clean machine without the repository, SDK directories or author-specific absolute paths.
7. **Expand simulation and animation once the above is stable.** Dependency-aware job scheduling, animation graphs, broader collision/physics queries, fluid/body coupling and scriptable behaviors need clear ownership and synchronization. Gate: deterministic input replays and defined overload behavior under heavy scenes.

## Native-performance measurement contract

- 60 FPS allows 16.67 ms per frame; 120 FPS allows 8.33 ms. These are total target intervals, not additive CPU-plus-GPU budgets: CPU/GPU overlap and queue depth must be reported.
- Render/display dimensions must match. Keep SR and frame generation off; record RT tier and denoiser separately. Native-resolution RR is optional and must not conceal a fallback in results.
- Record GPU/driver, CPU, power mode, focus/throttle, resolution, scene hash, warmup, duration and presentation settings. Report medians and tail latency, not only average FPS.
- Compare identical assets, lighting objectives, camera motion, entity counts and simulation workloads in Starlight, Unreal and Unity. Explicitly identify feature differences where exact equivalence is impossible.
- This restoration pass establishes functional and resource-lifetime evidence, **not a new 4K throughput result or a claim that Starlight beats Unreal or Unity**.
