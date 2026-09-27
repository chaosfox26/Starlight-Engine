# Starlight optimization audit

Date: 2026-09-17. Target: the local Ryzen 9 9950X3D-family / RTX 5090 system.

**Primary finding:** avoid repeated work and GPU drains before introducing wider SIMD, more allocator frameworks, or a replacement entity system. Full-scene regeneration, renderer recreation during resize, unchanged-scene DXR work, and physics lookup reconstruction are the strongest opportunities.

## Scope, method and evidence

The automated sweep read **2,930 source/build/script files, 3,878,157 lines**, recording hashes, duplicate-file groups and **35,630 lexical review sites**. The current engine/editor/project/physics source families contain **89 files**: engine 25, editor 54, physics 2, projects 8. These counts include tests and fallback generated headers, not just shipping translation units. The other files include 2,419 historical optimization-result source files and 299 navigation-toolchain files. Historical copies are not evidence of runtime bloat. Current CMake target declarations were inspected to distinguish those copies from compiled modules.

The sweep excluded `.git`, `out`, `build`, `bin`, `third_party`, `node_modules`, `audit-runs`, `generated`, and `__pycache__` directory names. Vendored Jolt internals are outside first-party refactoring scope. This is broad automated lexical coverage with structural inspection of the relevant live paths, not a compiler-proven whole-program reachability analysis or a manual review of every archived line.

A native layout probe was compiled and executed using **MSVC x64 C++20 Release `/O2 /MD`**. Production engine code was not changed by this audit. The scanner, source manifest, review sites and layout results are saved locally and linked at the end.

Evidence distinctions:

- **Verified structural:** source performs the identified work, or compiled `sizeof` establishes a footprint. The time cost remains workload-dependent.
- **Previously measured:** exact outcomes from the completed audit, subject to its failures.
- **Candidate:** an optimization or locality concern needing controlled A/B measurements. Cache misses, false sharing, vtable overhead and AVX-512 gains were **not measured with hardware counters**.

The preceding run passed 52/52 UI assertions, but recorded no helper-window creation stack. Its scene test returned mesh/BLAS/upload ownership to baseline, with +20 KiB private bytes and +3 handles after 80 load/unload cycles; the handle threshold failed. All new renderer comparisons failed with device loss, and all archived comparisons failed foreground acquisition. Consequently, **no credible FPS improvement percentage can currently be assigned**.

## 1. Critical Bottlenecks

### 1. Full-scene regeneration during simulation and editing — P1

Sources: [editor/cpp/native_session.cpp:185](D:/projects/Starlight-Engine/editor/cpp/native_session.cpp:185), [editor/cpp/render_scene.cpp:38](D:/projects/Starlight-Engine/editor/cpp/render_scene.cpp:38), [editor/cpp/assets.cpp:31](D:/projects/Starlight-Engine/editor/cpp/assets.cpp:31).

Running physics or fluids rebuilds the render packet at roughly 20 Hz, including unchanged terrain, object meshes and overlays. Imported meshes go through `load_glb`, which reads and parses the file; no persistent asset cache is present in that function. Tool-state changes can also trigger a rebuild.

**Refactor:** separate geometry/material revisions from instance poses, lighting and overlays. Retain immutable mesh assets and submit only changed instance data or dirty terrain regions. Asset keys must incorporate content/file revision, not just a path.

**Expected work reduction:** unchanged GLB reads/parses and terrain regeneration become zero after warm-up; update cost follows changed data instead of total scene geometry. Actual frame-time gain requires measurement.

### 2. Resize recreates the entire viewport — P1

Sources: [editor/cpp/native_session.cpp:191](D:/projects/Starlight-Engine/editor/cpp/native_session.cpp:191), [editor/cpp/editor_main.cpp:723](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:723).

Every accepted dimension change destroys and recreates the viewport renderer, including device-dependent initialization. Frequent `WM_SIZE` events can therefore trigger repeated expensive initialization rather than resizing only affected resources.

**Refactor:** coalesce dimensions, defer live-sizing work, then resize swapchain/HDR/depth/denoiser resources on the owning render thread. Retain device, pipelines and reusable mesh caches. Suspend minimized rendering.

**Expected work reduction:** one settled resize rather than potentially many complete initializations. Measure device creation counts and resize stalls; this is primarily responsiveness and lifetime improvement.

### 3. DXR prepares unchanged geometry every frame — P1

Sources: [editor/native/viewport.cpp:105](D:/projects/Starlight-Engine/editor/native/viewport.cpp:105), [deep/engine/dxr.h:65](D:/projects/Starlight-Engine/deep/engine/dxr.h:65), [deep/engine/dxr.h:94](D:/projects/Starlight-Engine/deep/engine/dxr.h:94), [deep/engine/dxr.h:78](D:/projects/Starlight-Engine/deep/engine/dxr.h:78).

`sl_frame` constructs a temporary geometry vector. Active DXR preparation builds a fresh topology map even when unchanged. Drawing computes object inverse-transpose matrices, uploads instance descriptions, queries TLAS prebuild requirements and builds/refits TLAS each frame.

**Refactor:** explicit topology and transform revisions; reusable topology arrays; prebuild requirements cached by capacity/flags; cached normal matrices; skip TLAS updates for static instances. Continue updating lighting, temporal history and animated effects independently.

**Expected work reduction:** static scenes can perform zero TLAS refits and zero object-matrix inversions after initialization. Moving instances still need correct updates. Do not confuse unchanged lighting with unchanged geometry.

### 4. Mesh upload and removal perform redundant passes and queue drains — P1

Sources: [editor/native/viewport.cpp:93](D:/projects/Starlight-Engine/editor/native/viewport.cpp:93), [editor/native/viewport.cpp:99](D:/projects/Starlight-Engine/editor/native/viewport.cpp:99), [deep/engine/dxr.h:60](D:/projects/Starlight-Engine/deep/engine/dxr.h:60).

Upload constructs temporary source/GPU maps, hashes complete mesh byte streams and searches the new part list once for every old part. Removed geometry can trigger a full BLAS-cache retirement, including survivors. Queue flushes synchronize these changes conservatively.

**Refactor:** stable mesh IDs/content revisions; O(old + new) removal detection; survivor BLAS reuse; per-resource retirement using the last fence that references it.

**Expected work reduction:** removal detection improves from worst-case O(old parts × new parts) to linear work. Unchanged byte hashing and unrelated BLAS reconstruction disappear. Never replace fence-safe retirement with immediate destruction.

### 5. Physics tick allocations and full obstacle-mask reconstruction — P1

Sources: [editor/cpp/physics.cpp:85](D:/projects/Starlight-Engine/editor/cpp/physics.cpp:85), [editor/cpp/native_session.cpp:111](D:/projects/Starlight-Engine/editor/cpp/native_session.cpp:111), [editor/cpp/flow.cpp:6](D:/projects/Starlight-Engine/editor/cpp/flow.cpp:6).

Each ready physics step builds an ID-to-actor hash table, liquid vector and pose vector. The wrapper reports poses for dynamic bodies without filtering unchanged/sleeping poses, so a positive pose count can trigger obstacle rebuilding for all eligible objects.

**Refactor:** cache stable actor indices with generation validation; retain pose/liquid scratch buffers; report moved bodies; separate static obstacle coverage from dynamic dirty-cell updates.

**Expected work reduction:** per-step actor-map node allocation drops from O(entity count) to zero between topology revisions; unchanged static obstacle work disappears. Preserve buoyancy and scene-removal correctness.

### 6. Transform and bounds recomputation inside vertex loops — P2

Sources: [editor/cpp/mesh.cpp:51](D:/projects/Starlight-Engine/editor/cpp/mesh.cpp:51), [editor/cpp/mesh.cpp:60](D:/projects/Starlight-Engine/editor/cpp/mesh.cpp:60), [editor/cpp/interactions.cpp:4](D:/projects/Starlight-Engine/editor/cpp/interactions.cpp:4).

Euler rotations recompute trigonometry within per-vertex `world_point`/`local_point` calls. Snapping computes candidate bounds by reconstructing object meshes and transforming their vertices, potentially rereading GLBs.

**Refactor:** cache forward/inverse/normal matrices by transform revision and local bounds by mesh revision. Use conservative transformed bounds for broad-phase filtering, preserving exact checks where necessary.

**Expected work reduction:** the Euler path changes from six sin/cos evaluations per vertex to six per changed object. Cached bounds remove repeated asset parsing/copying from candidate traversal.

### 7. Routes are remeasured and retraversed every frame — P2

Sources: [editor/cpp/native_session.cpp:206](D:/projects/Starlight-Engine/editor/cpp/native_session.cpp:206), [editor/native/viewport.cpp:36](D:/projects/Starlight-Engine/editor/native/viewport.cpp:36).

Every render iteration recomputes duration by walking all authored route segments. Viewport drawing then traverses route segments again for placement.

**Refactor:** cache route lengths/prefix distances on route edits; use binary search or monotonic segment cursors for playback.

**Expected work reduction:** duration lookup becomes O(1), while evaluation becomes O(log segments), or amortized O(1) during forward traversal. Repeat, backward reset and character-follow must remain correct.

### 8. Edit preflight creates and discards a render artifact — P2

Sources: [editor/cpp/editor_main.cpp:1019](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:1019), [editor/cpp/editor_main.cpp:999](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:999).

The edit path builds a complete packet to validate assets/capacity, discards it, then refreshes the scene into the worker, which builds the packet again. Refresh also resets the object list and reconstructs inspector contents.

**Refactor:** transfer the successful validated artifact to the worker, and distinguish selection/list changes from geometry/material/transform changes.

**Expected work reduction:** remove at least one redundant full packet construction in this path. Preserve preflight and transaction rollback: deleting validation is not an equivalent optimization.

### 9. Crowd motion history churns associative nodes — P2

Sources: [deep/projects/GameProject/project.cpp:445](D:/projects/Starlight-Engine/deep/projects/GameProject/project.cpp:445), [deep/projects/GameProject/project.cpp:212](D:/projects/Starlight-Engine/deep/projects/GameProject/project.cpp:212).

GameProject clears and repopulates a node-based transform-history map each frame, despite largely stable entity IDs. Retaining the hash bucket array does not retain erased map nodes.

**Refactor:** stable compact entity slots, generation checks and double-buffered transform arrays. A pool allocator is an interim improvement but does not remove pointer chasing.

**Expected work reduction:** O(visible entities) node destruction/allocation becomes contiguous overwrite. This applies to the project runtime, not automatically to the editor preview.

### 10. All denoiser working sets are allocated simultaneously — P2

Source: [deep/engine/dxr.h:48](D:/projects/Starlight-Engine/deep/engine/dxr.h:48).

DXR allocates 26 full-resolution images for both RR and native wavelet paths. Their formats total **212 bytes/pixel**, or **1,758,412,800 bytes (~1.64 GiB) at 3840×2160**, excluding alignment, acceleration structures and other renderer targets.

**Refactor:** shared guides/output plus active-mode working sets; allocate fallback buffers before switching and retire inactive sets behind fences. Preserve correct descriptors for every bound slot.

**Expected footprint reduction:** wavelet-only images 9–22 total 128 bytes/pixel, approximately **1,012.5 MiB at 4K**, potentially deferrable in RR-only operation. This is calculated residency opportunity, not measured bandwidth or FPS gain.

### Measurement prerequisite

[deep/engine/render_device.h:1852](D:/projects/Starlight-Engine/deep/engine/render_device.h:1852) rotates swapchain backbuffer indices without presenting while rendering still copies into swapchain targets. This is a strong suspect for the failed benchmark, not a confirmed DRED diagnosis. A real offscreen path should own independent output resources and preserve slot fences/barriers. Capture debug-layer/device-removal evidence before optimization comparisons. The immutable archived DLL lacks this ABI; do not silently substitute foreground presentation and call its FPS comparable.

## 2. Immediate Bloat Removal

### Small, scoped cleanup candidates

1. **Unused `Viewport::error` string:** [editor/native/viewport.cpp:26](D:/projects/Starlight-Engine/editor/native/viewport.cpp:26) has no member reads/writes in the inspected implementation; error reporting uses thread-local `failure`. Remove the member after a native build. The measured string control object is 32 bytes; this is a small fixed saving, not a major performance win.
2. **Parser-only `Part::baseY`:** [editor/native/viewport.cpp:17](D:/projects/Starlight-Engine/editor/native/viewport.cpp:17) stores a value used only for packet decoding/validation. Decode into a local scalar and retain the finite-value check. Keep the field's bytes in existing packet formats. Actual retained-struct savings depend on alignment.
3. **Frame-local DXR input vector:** skip construction when RT is disabled; reserve and retain storage otherwise. Consolidate topology data with the draw list where ownership permits, rather than adding another generic container layer.
4. **Discarded preflight packet:** preserve the validated artifact instead of recomputing it. Treat this as duplicate-work removal, not relaxed schema safety.
5. **Unused bilinear initialization in HDR-only editor operation:** [deep/engine/upscale/upscale_off.cpp:41](D:/projects/Starlight-Engine/deep/engine/upscale/upscale_off.cpp:41) compiles a compute shader and creates a PSO, root signature and heap, while [deep/engine/render_device.h:1819](D:/projects/Starlight-Engine/deep/engine/render_device.h:1819) bypasses its evaluation for HDR. Make non-HDR resources lazy or specialize native-HDR initialization. Other runtime paths still use this backend, so deleting the whole implementation is unsafe.

### Conditional deletions and abstractions to retain

- [deep/engine/upscale/upscale_stubs.cpp:9](D:/projects/Starlight-Engine/deep/engine/upscale/upscale_stubs.cpp:9) supplies linked factory symbols for unavailable backends. Remove factory calls and stubs together only under an explicit native-only build policy. Their main cost is compatibility/code-size complexity, not proven frame-critical vtable overhead. RR remains distinct from super resolution/frame generation.
- Native `sl_*`/`slp_*` exports are active native DLL ABI boundaries, not CLR bridges or IPC. Ownership wrappers join workers and release GPU/physics state. Batch payloads before considering boundary deletion.
- [deep/engine/render_device.h:422](D:/projects/Starlight-Engine/deep/engine/render_device.h:422) retains an A/B thread-spawning fallback. Remove only after project settings and diagnostic consumers are checked. Persistent mode already reuses worker threads.
- Snapshot duplicates are repository disk/search bloat, not necessarily executable bloat. Exclude recovery/evidence trees from production source discovery and IDE indexing. Do not delete recovery material merely because hashes match current files.
- A lexical search cannot prove exported methods or virtual functions unused. Obtain linker and PE import/export evidence before deleting externally callable entry points.
- Repeated schema checks are not universally redundant. Decode, encode, mesh import and packet parsing protect different boundaries. Deduplicate only through immutable validated artifacts with explicit invalidation; retain checks on untrusted input.

## 3. High-Impact Optimizations

### A. Native memory layout

Compiled shallow footprints; heap-owned vector/string data are excluded:

| Type | Bytes | Alignment |
|---|---:|---:|
| Vec3 | 24 | 8 |
| Vertex | 80 | 8 |
| Mesh | 48 | 8 |
| MeshPart | 80 | 8 |
| Character | 464 | 8 |
| SceneData | 240 | 8 |
| TerrainData | 64 | 8 |
| TerrainFlow | 92,432 | 8 |
| FlowObstacleMask | 120 | 8 |
| RenderStatus | 128 | 8 |
| CameraState | 56 | 8 |
| SceneHistory | 296 | 8 |
| std::string | 32 | 8 |

**Vertex conversion:** [editor/cpp/mesh.hpp:19](D:/projects/Starlight-Engine/editor/cpp/mesh.hpp:19) contains three 24-byte double vectors, a four-byte kind and four bytes of tail padding. Its packed render/wire equivalent is 40 bytes. A float render-cache representation halves vertex storage, but should not silently change authoring precision or file formats. Convert once per geometry revision.

**Terrain amplification:** [editor/cpp/geometry.cpp:219](D:/projects/Starlight-Engine/editor/cpp/geometry.cpp:219) expands the 97×97 heightfield into 37,249 preview vertices and 221,184 ground indices. That is 2,979,920 vertex bytes plus 884,736 index bytes at measured editor strides, before shoreline data. The float ground packet payload is approximately 2,374,696 bytes. At 20 rebuilds/sec, unchanged ground alone contributes **47.5 MB/sec of packet bytes**, plus construction, normal computation, validation and hashing. This is a calculated workload volume, not measured memory-controller traffic.

**Obstacle masks:** [editor/cpp/flow.hpp:4](D:/projects/Starlight-Engine/editor/cpp/flow.hpp:4) owns five vectors whose arrays total **202,752 bytes**, beyond its 120-byte shallow size. Reuse arrays and dirty-update cells; do not put them back on a 1 MiB worker stack.

**Mixed authoring/runtime objects:** Character's 464-byte shallow record combines strings, route/sculpt data, authoring settings and frequently accessed transform/physics values. Add a compact derived instance array keyed by stable editor ID rather than rewriting the entire schema into a new ECS. Pointer chasing and cache locality are structural concerns; no cache-miss rate was collected.

**History:** [editor/cpp/mesh.hpp:43](D:/projects/Starlight-Engine/editor/cpp/mesh.hpp:43) copies the full scene at transaction begin and has no count/byte budget. One terrain snapshot owns approximately 110.3 KiB of height/paint arrays before meshes, routes and strings. Use operation deltas or tile copy-on-write with a configurable undo memory budget. Preserve exact undo/redo and one stroke per transaction.

**Existing reuse worth preserving:** flow transfer storage retains capacity after `clear`; GPU constant/upload buffers already use frame slots; [deep/projects/GameProject/project.cpp:769](D:/projects/Starlight-Engine/deep/projects/GameProject/project.cpp:769) reuses bucket storage. Reserve predictable capacities before introducing arena or pool frameworks. Do not add another allocator layer over already stable buffers.

### B. Win32 GUI, dispatch and hidden work

| Evidence | Opportunity | Required verification |
|---|---|---|
| [editor/cpp/editor_main.cpp:719](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:719) and [editor/cpp/music.hpp:67](D:/projects/Starlight-Engine/editor/cpp/music.hpp:67) poll MCI while music plays. | Use completion notification or a slower dedicated playback poll, separate from brush/camera cadence. | Count MCI calls during play/pause; preserve playlist completion. Hidden music should continue playing when requested. |
| [editor/cpp/editor_main.cpp:1838](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:1838) updates telemetry inside a modulo window. | Use elapsed-time scheduling and only assign changed label text. | No missed/doubled refresh windows under delayed message delivery. |
| [editor/cpp/editor_main.cpp:723](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:723) moves controls with repaint enabled and recreates rounded regions. | Cache rectangles/region dimensions, batch via DeferWindowPos, invalidate only changed bounds. | Preserve overflow, scrolling, modal bounds and DPI. Free a new region if ownership transfer through SetWindowRgn fails. |
| [editor/cpp/editor_main.cpp:1906](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:1906) unconditionally layouts the main editor; the main worker lacks the preview's minimize policy. | Suspend minimized rendering and coalesce sizing; resume with latest dimensions. | No frame growth while minimized, correct restore, no tiny-device recreation. Continue save/task work. |
| [editor/cpp/game_preview.hpp:306](D:/projects/Starlight-Engine/editor/cpp/game_preview.hpp:306) already parks its session and kills its timer. | Preserve that behavior; avoid copying scenes into an existing hidden preview on every refresh. Store a revision and sync before showing it. | Latest scene appears on reopen; hidden preview submits no frames. Retained parked resources are a reuse policy, not automatically a leak. |
| [editor/cpp/editor_main.cpp:999](D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp:999) resets lists and regenerates inspector controls. | Apply changed rows/properties; reuse controls for unchanged schemas; suppress redraw during bulk mutations. | Preserve selection, focus, scroll position, tooltips and visible feedback. |

The successful 52-check UI run demonstrates bounded lifetime behavior, not low layout CPU. Hidden visual panels should not rebuild unless their content or geometry changes; background audio and pending project tasks must continue where intended. The earlier system-helper window was not traced in the last run, so it must not be silently whitelisted.

### C. Compute, SIMD and worker scheduling

1. **Cache matrices before widening instructions.** DirectXMath already serves rendering. Keep inverse-transpose normals for non-uniform scale and quaternion semantics. Do not substitute hand-written AVX-512 without comparing generated code and timings.
2. **Good SIMD candidates:** contiguous vertex transform/packing, terrain height/color interpolation and fluid surface/edge calculations. Keep scalar reference kernels and tails. Gate optional AVX-512 with CPUID and OS extended-state support; the CPU model name alone is not a dispatch mechanism. Compare scalar/compiler-vectorized, AVX2 and AVX-512 variants before selecting one.
3. **Fluid scatter hazards:** neighboring edges update overlapping cells. Naive parallelization races; wide scatter is not automatically fast. Precompute surfaces, derive edge fluxes independently, then deterministically gather contributions per cell. Validate conservation and obstacle blocking.
4. **False sharing remains a candidate:** [deep/engine/persistent_workers.h:43](D:/projects/Starlight-Engine/deep/engine/persistent_workers.h:43) partitions contiguous chunks and uses mutex-protected completion. Chunk boundaries and mutable bucket metadata deserve measurement, but false sharing was not observed. Align only proven high-write per-worker statistics/scratch, not every record indiscriminately.
5. **Oversubscription:** [deep/Physics/PhysicsWorld.cpp:54](D:/projects/Starlight-Engine/deep/Physics/PhysicsWorld.cpp:54) can launch an asynchronous cook per dirty terrain tile—36 on a full update—while Jolt owns four workers and rendering may have its own pool. Move cooks to a bounded persistent queue with generation cancellation and a shared CPU budget. More runnable threads is not equivalent to more useful work.
6. **Granularity:** [deep/engine/render_device.h:70](D:/projects/Starlight-Engine/deep/engine/render_device.h:70) applies a 2,000-item cutoff to kernels with different cost/item. Tune gather, pose, packing and cooking separately using 1/2/4/8/16/32-worker sweeps and tail-latency measurements. Do not impose affinity or high process priorities based on assumptions.
7. **Vtables are not the first target:** presentation has frame-level virtual calls, while much larger loops reconstruct geometry/maps. Specialize where it removes unused initialization/resources; retain sound ownership abstractions.

### D. Validation and ownership

- Cache route duration, light lists, selected actor index and static meshes by separate topology/transform/material/light/camera/playback/overlay revisions. Camera changes must not reparse assets.
- [editor/cpp/native_session.cpp:103](D:/projects/Starlight-Engine/editor/cpp/native_session.cpp:103) destroys the physics wrapper on reset, and later recreation replaces its jobs, temporary allocator and terrain state. Reuse the world under a defined clear/rebuild contract where possible. The native implementation already separates prop clearing; terrain/jobs need explicit lifetime rules before reuse.
- Refine full BLAS retirement to per-resource fence retirement. Static shared render targets and descriptor tables need not be destroyed at every scene unload; removed scene references must be released safely.
- Replace broad mutable “already validated” flags with immutable validated scene/mesh artifacts. Preserve malformed-file rejection, import stride checks, draw limits and atomic edit rollback.
- Define canonical playback ownership across editor/session/preview. Some local button-transition state represents an asynchronous request and is not redundant merely because it resembles session state.

## 4. Implementation Plan

Changes should be independently reviewable. Expected gains below mean work eliminated or bytes saved, not validated FPS increases.

1. [ ] **Repair measurement correctness first:** independent offscreen output resources, D3D12 debug/DRED evidence, valid GPU completion, explicit denoiser status, native resolution and P50/P95. Separate unsupported archived tests from valid comparisons. Never remove safety waits to inflate FPS.
2. [ ] **Small cleanup:** remove unused Viewport error storage, localize parser-only baseY, reuse/reserve geometry arrays and fix telemetry elapsed-time scheduling. Preserve wire ABI. Build Debug/Release and run core/UI/session tests.
3. [ ] **Cache transforms/routes/bounds:** revision-based matrices and prefix distances. Compare non-uniform scaling, physics quaternions, route repeat/follow, reset and snap output against the existing implementation.
4. [ ] **Stop physics tick allocation:** stable actor indices/generations and retained pose/liquid buffers; moved-body detection. Target zero wrapper allocations between topology changes. Validate sleeping bodies, removals and buoyancy.
5. [ ] **Bound collision cooking:** persistent limited queue, deduplicated dirty tiles and cancellation/generation handling; preserve unchanged terrain colliders. Test rapid sculpting, scene switching and shutdown while cooking.
6. [ ] **Separate assets from instance updates:** immutable mesh cache, reusable preflight result, dirty terrain/overlay updates. Target zero unchanged asset reads and zero static terrain rebuilds during moving-prop playback. Keep corrupt-asset rollback intact.
7. [ ] **Make DXR incremental:** cache topology/prebuild sizes and normal matrices; skip static TLAS updates; retain survivors; fence-retire removed resources. Validate disocclusion and animated terrain/lighting separately.
8. [ ] **Reduce GUI and resize churn:** coalesced layout, cached regions, hidden-preview revision sync, minimize suspension and resize-in-place. Run the 52 checks plus live sizing, DPI, idle/minimize and restore probes while preserving the classic appearance.
9. [ ] **Reduce footprint:** float render cache, budgeted undo history and active-denoiser working sets. Compare vertex/material output and save/load round trips; measure actual VRAM residency separately from process private bytes.
10. [ ] **Crowd/SIMD experiments:** generation-indexed motion history, then kernel-specific worker and instruction-width tests. One change per A/B run; reject numerical or tail-latency regressions even when averages improve.
11. [ ] **Build cleanup last:** native-only presentation specialization and unreachable factories after import/export/build proof. Exclude archived snapshots from indexing; preserve recovery evidence and externally callable ABI.

### Acceptance criteria

- Intended denoiser and native resolution confirmed by successful frames; no device loss or hidden 20 FPS presentation limit. Comparable tests must have equivalent presentation contracts.
- Scene unload retires removed mesh/BLAS/upload ownership. Explain handle deltas with actual owner/stack evidence; **do not simply widen the failed +3-handle threshold**.
- Measure affected-module allocations and CPU/GPU time with identical inputs, including frame P95. The 60/120 FPS budgets are 16.67/8.33 ms; available failed benchmark results cannot prove either target.
- Preserve no super resolution, no frame generation, exact undo/redo, classic UI behavior and exception-safe worker/device shutdown.

### Reproducible artifacts

- [Automated scanner](D:/projects/Starlight-Engine/tools/audit/optimization_scan.py)
- [Source hashes, coverage and duplicate groups](D:/projects/Starlight-Engine/out/optimization-audit/manifest.json)
- [All lexical review sites](D:/projects/Starlight-Engine/out/optimization-audit/review-sites.json)
- [Compiled native layout results](D:/projects/Starlight-Engine/out/optimization-audit/layouts.json)
- [Layout probe](D:/projects/Starlight-Engine/out/optimization-audit/layout_probe.cpp) and [build log](D:/projects/Starlight-Engine/out/optimization-audit/layout-build.log)
- [Previous run's reviewed limitations](D:/projects/Starlight-Engine/audit-runs/run-20260917-192850/engine_audit_findings.md)

No production pruning, PMU trace, sanitizer-based leak/race proof, or new renderer benchmark was performed in this static audit. The report identifies concrete execution-ready opportunities and the evidence required to accept each change.

## 5. Multi-Core Optimization and Task Parallelism — 16 cores / 32 logical processors

This section adds the requested concurrency priority. The objective is to **make all 32 logical processors available to sufficiently parallel workloads while reducing frame-critical latency**, not force 100% CPU use during small or GPU-bound frames. Thirty-two runnable jobs can be slower than sixteen when SMT siblings compete for execution resources or cache. Evaluate both configurations and retain headroom for the GUI, audio and OS.

### 5.1 Job system, saturation and synchronization

| Verified implementation | Consequence | Action |
|---|---|---|
| [deep/projects/GameProject/project.cpp:1252](D:/projects/Starlight-Engine/deep/projects/GameProject/project.cpp:1252) defaults the project renderer to `physicalCoreCount()`. [deep/engine/render_device.h:72](D:/projects/Starlight-Engine/deep/engine/render_device.h:72) counts physical cores, not all logical processors. | Automatic mode targets 16 participants on the requested 16-core machine; it does not automatically exercise 32 SMT contexts. The submitting thread is one participant. | Expose logical-worker and topology policies separately from the existing physical-core default. Compare 16 vs 32 on each workload rather than globally replacing the default. |
| [deep/engine/render_device.h:70](D:/projects/Starlight-Engine/deep/engine/render_device.h:70) and [deep/engine/render_device.h:71](D:/projects/Starlight-Engine/deep/engine/render_device.h:71) impose a 2,000-item serial threshold and a 512-item minimum chunk. | A 2,000-item job uses at most four participants; enough requested workers alone cannot saturate 32 threads. Approximately 16,000 items are needed to reach 32 chunks under this policy. | Use kernel-specific grain sizes based on cost, not a common item count. Log requested participants, actual participants and useful item counts. |
| [deep/engine/persistent_workers.h:43](D:/projects/Starlight-Engine/deep/engine/persistent_workers.h:43) creates persistent native threads and assigns one contiguous range per participant. | There is **no work-stealing deque, fiber scheduler or dependency graph in this first-party dispatcher**. A worker that finishes its range cannot help a slower range. | Add dynamically claimable coarse microbatches or local-first deques where work is heterogeneous. Keep the static contiguous path for uniform bandwidth kernels. |
| [deep/engine/persistent_workers.h:40](D:/projects/Starlight-Engine/deep/engine/persistent_workers.h:40) wakes the grown pool, including inactive workers when a later job requests fewer participants. Completion decrements use a shared mutex. | Potential unnecessary wakeups and completion serialization. This is structural overhead, not a measured lockup. Tiny tasks would amplify it. | Instrument wake counts, empty wakeups, completion-lock wait and final-worker tail before redesign. Consider bounded active worker sets or a semaphore-backed queue. |
| [deep/engine/persistent_workers.h:61](D:/projects/Starlight-Engine/deep/engine/persistent_workers.h:61) blocks the submitter at the end of each invocation. | Gather, pose and bucket emission form global stage barriers; variable pose costs can leave otherwise usable workers idle. | Replace unnecessary global joins with dependency counters for independent batches, while retaining true data dependencies and frame-slot ownership barriers. |
| [deep/Physics/PhysicsWorld.cpp:41](D:/projects/Starlight-Engine/deep/Physics/PhysicsWorld.cpp:41) has four Jolt workers; [deep/Physics/PhysicsWorld.cpp:54](D:/projects/Starlight-Engine/deep/Physics/PhysicsWorld.cpp:54) separately launches terrain cooks. | Rendering, cooking, Jolt and preview sessions can compete without a shared CPU budget. Jolt may contain scheduling mechanisms internally; its vendored implementation was not audited here. | Bound cooking concurrency first. Tune Jolt and renderer worker allocations together; do not independently set every subsystem to 32 workers. |

**Starvation and deadlock checks:** the persistent pool's condition-variable waits release its mutex, and task bodies execute outside the lock. No actual mutex deadlock is established by this static review. The class explicitly supports one submitting thread. Concurrent or nested submissions into the same instance would overwrite shared task/generation state or violate its waiting contract. Add debug assertions for the submitting thread and nested execution, and stress shutdown, exceptions, cancellation and pool growth. Either run nested work inline or introduce explicit cooperative scheduling; do not allow workers to block waiting for child jobs queued exclusively to the same exhausted pool.

A shared completion counter updated by all workers is **true sharing**, not something `alignas(64)` alone fixes. Reduce update frequency with chunk-level completions and per-worker aggregation before considering atomic contention tricks. Conversely, adjacent independent per-worker counters can be false-sharing candidates.

### 5.2 Concrete task-graph opportunities

The current GameProject crowd path already has parallel gather/culling, disjoint pose-palette writes and parallel instance-bucket construction. Sources: [deep/projects/GameProject/project.cpp:680](D:/projects/Starlight-Engine/deep/projects/GameProject/project.cpp:680), [deep/projects/GameProject/project.cpp:752](D:/projects/Starlight-Engine/deep/projects/GameProject/project.cpp:752), [deep/projects/GameProject/project.cpp:774](D:/projects/Starlight-Engine/deep/projects/GameProject/project.cpp:774). Preserve that work instead of proposing parallelization as though it did not exist.

Its static ranges can be imbalanced: pose workers skip invalid and far-LOD items while near characters require actual pose solves. A range with mostly culled/far characters may finish much earlier than a range of complex near characters. First compact the active near-pose indices, then distribute similarly weighted batches. Keep the small shared far-pose pool cheap; scheduling every tiny far pose independently can cost more than serial evaluation.

Proposed staged dependencies:

```text
Immutable scene/camera snapshot
  ├─ visibility + LOD batches ──> compact visible/near-pose indices
  │                                ├─ pose batches ──> instance staging
  │                                └─ static instance staging
  ├─ changed mesh decode/validation ──> CPU upload descriptions
  └─ physics prep / dirty obstacle tiles ──> ordered simulation result

Ready instance/asset batches
  └─ reserve upload + descriptor ranges
       └─ independent command-list recording
            └─ ordered queue submission ──> RT / atmosphere / composite
```

- **Scene traversal/culling:** immutable input snapshots, disjoint output ranges or thread-local visible lists followed by prefix-sum compaction. Do not concurrently mutate `scene.Characters`, UI selection or shared unordered maps.
- **Animation:** near-pose jobs write distinct palette slots. Keep frame-slot fences before CPU overwrites mapped GPU data; a job-completion barrier does not replace a GPU fence.
- **Physics prep:** parallelize mesh conversion and obstacle-cell preparation using immutable transforms; submit finalized changes at the physics world's allowed synchronization point. Do not assume arbitrary concurrent Jolt world mutation is safe.
- **Asset processing:** parallelize independent file reads, decode and validation on a bounded queue. Produce immutable meshes and upload requests. Device-resource/descriptor publication remains ordered and fence-safe. Limit staging bytes, not only job count.
- **Editor UI:** Win32 control creation, destruction, layout and user-facing state remain on the owning UI thread. Workers publish completed results/revisions; they do not directly mutate controls. Coalesce completion notifications.
- **Fairness:** reserve service for frame-critical work, but age/deadline background requests so continuous rendering cannot indefinitely starve imports or collision cooking. Track oldest queued job and cancellation latency.

### 5.3 Cache coherency, CCD topology and NUMA

**Current evidence:** first-party scheduler code queries `RelationProcessorCore` for a core count. The inspected engine/physics/project/editor paths contain no CPU-set assignment, thread-affinity policy, NUMA placement policy or cache-domain enumeration. Therefore there is no explicit guarantee that a task remains within a CCD or preferentially uses a larger-cache CCD. This does not prove that Windows schedules it poorly.

Do not infer CCD mapping from logical processor numbers, equate CCDs with NUMA nodes, or assume only one CCD has stacked cache. The earlier hardware report's CPU brand string includes `9950X3D2`; the requested target name alone is insufficient to select an asymmetric-cache policy. Discover and record the topology actually exposed by this machine.

Recommended topology layer:

1. Enumerate cores, SMT siblings, processor groups, last-level cache sharing and NUMA nodes using the applicable Windows processor-topology and CPU-set APIs. Correlate processor masks rather than assuming even/odd SMT numbering. Record unavailable relationships explicitly.
2. Compare Windows-default scheduling with soft CPU-set preferences for cache-sensitive groups, plus a topology-aware physical-core-first policy. Avoid hard pinning every subsystem; it can worsen OS scheduling, thermals, SMT contention and task starvation.
3. Keep recurrent pose/transform/physics batches near their last-level-cache domain when profitable. Reuse per-worker scratch allocated/touched by its owning worker. Prefer local stealing, then cross-domain stealing to avoid an idle core while work remains.
4. Treat immutable mesh/animation data as shared read-mostly storage; avoid copying large assets merely to claim locality. Keep frequently written state partitioned. On systems reporting one NUMA node, CCD locality is a cache concern, not evidence of remote NUMA memory.
5. Adopt a larger-cache-domain preference only after topology evidence and A/B timing justify it. Cache-sensitive and compute-heavy kernels can prefer different placements. Preserve an unpinned fallback.

**64-byte alignment review:** the first-party pool has ordinary scalar state under a mutex and no explicitly cache-line-aligned worker records. GameProject's crowd arrays have disjoint elements, but chunk boundaries can still share a cache line. Nested bucket vectors own separate storage; this is not proof of false sharing, and their metadata should not be padded wholesale.

For new frequently written per-worker counters or queue producer/consumer state, use separate `alignas(64)` records on this target and assert both alignment and cache-line-multiple stride. Ensure the allocator preserves over-alignment. Place independently written producer and consumer counters on different lines; keep tightly coupled fields that are written together in the same record. This does **not** mean every mutex or atomic benefits from its own line. Verify actual structure offsets, array stride and coherence/CPU sampling before increasing footprint.

Shared-mutable candidates and safer replacements:

| Current/candidate state | Preferred first step | Important constraint |
|---|---|---|
| Per-frame ID maps and motion histories | Stable slots plus double-buffered transform arrays | Generation-check removals; retain only required previous-frame state. |
| Shared instance/descriptor cursors | Prefix-sum reservations and disjoint worker ranges | Publish the final counts once; no concurrent increments of current non-atomic cursors. |
| Asset completion and UI notifications | Bounded producer/consumer staging queues | Use SPSC only when there truly is one producer and one consumer; multi-worker publication needs MPSC or merging. |
| Per-worker timing/statistics | Thread-local or cache-line-separated buffers, reduced after completion | Do not add a global log mutex or file write per task. |
| Pool work distribution | Start with a bounded, simple queue and coarse tasks; consider measured local deques | Lock-free is not automatically faster and introduces publication, reclamation and shutdown obligations. |

### 5.4 Multi-threaded render submission

**Verified current architecture:** [deep/engine/render_device.h:816](D:/projects/Starlight-Engine/deep/engine/render_device.h:816) creates one direct allocator per frame slot, and [deep/engine/render_device.h:818](D:/projects/Starlight-Engine/deep/engine/render_device.h:818) creates a shared direct command list. [deep/engine/render_device.h:1353](D:/projects/Starlight-Engine/deep/engine/render_device.h:1353) submits one list for that path. GameProject likewise submits one main list. Its parallel bucket emission constructs **CPU instance data**, not concurrent D3D12 command lists; `drawLook`, merging and API recording remain serial.

The existing `lastCpuRecMs`, gather/pose/emit/merge timings are useful frame/stage wall times. They do not provide per-worker command-recording latency; the recording interval can include waits for CPU work. No per-worker D3D12 recording profile was executed in this static extension, and the failed offscreen harness still prevents a valid throughput comparison.

A safe concurrent recording design requires:

1. **Allocator/list ownership per worker and frame slot.** Allocate a bounded set of command allocators and direct command lists. No allocator may be reset until every submitted list using it has retired on the GPU. No two workers record the same list simultaneously.
2. **Prepartitioned bindings.** Reserve constant/instance/palette upload ranges and descriptor ranges before jobs begin. Current counters, command-list pointers and resource-state tracking are not made thread-safe simply by calling existing draw functions from multiple threads.
3. **Explicit resource-state boundaries.** A coordinator or render graph computes transitions. Worker lists consume declared entry states and produce declared exit states. Keep cross-pass barriers, UAV dependencies, AS builds and RT/atmosphere/composite ordering correct.
4. **Coarse draw batches.** Group by pass/material/mesh, then record enough draws per list to amortize setup and closing. Start with 2/4/8 recording workers, not 32 tiny lists. Increasing the CPU job pool to 32 does not require 32 submission lists.
5. **Deterministic final submission.** Submit a dependency-ordered list array on the direct queue. CPU parallel recording does not require multiple GPU direct queues. Consider copy/compute queues only with explicit cross-queue fences and proven overlap benefits.
6. **Bundles only for stable repeated work.** They are an optional experiment, not a required abstraction; dynamic bindings and state restrictions can erase their benefit. Retain a serial reference path.

Record, per worker and frame: queued/ready/start/finish timestamps, useful item and draw counts, recording duration, scheduler wait, longest idle interval and completion-tail delay. Record centrally: merge time, final-list wait, submission time, slot-fence wait, GPU timestamps and frame P50/P95/P99. Use preallocated per-worker telemetry and flush after a run. For short frames, aggregate over many frames so timestamp overhead does not dominate.

### 5.5 Prioritized concurrency acceptance plan

1. [ ] Inventory actual 16-core/32-logical topology, SMT/cache masks and current effective pool sizes. Record the unmodified scheduler baseline before changing affinity.
2. [ ] Instrument the existing pool without per-task allocations or log locks. Report useful work, wakeup overhead, idle/straggler tails, queue age and completion-lock wait; do not label high CPU utilization alone a pass.
3. [ ] Bound terrain cooking and coordinate its budget with Jolt, rendering and preview sessions. Stress nested submission rejection, exceptions, pool growth, cancellation and shutdown; distinguish genuine deadlocks from GPU-fence waits.
4. [ ] Compact near-pose work and introduce dynamic coarse batching where cost varies. Preserve local contiguous scheduling for uniform kernels. Validate exactly-once execution, deterministic output and bounded background-job delay.
5. [ ] Compare 1/2/4/8/16/32 participants and kernel-specific grains, including 16 physical cores versus 32 SMT contexts. Test simultaneous rendering, cooking and imports, not only an isolated fully parallel loop.
6. [ ] Compare default scheduling, physical-first placement and measured cache-domain preferences. Retain policies only if frame-tail latency or useful throughput improves without starvation.
7. [ ] Introduce per-worker/per-slot command recording behind a feature flag with reserved bindings and explicit resource transitions. First validate with D3D12 debug/GPU validation; obtain performance measurements separately without their overhead.
8. [ ] Accept parallel recording only if reduced CPU frame-boundary latency exceeds merge/list overhead and does not regress GPU time, native-resolution output, UI response or lifetime checks.

Expected gains are reduced serial work, fewer idle stragglers and better locality. **No 2× speedup from SMT, no guaranteed 32-thread saturation, and no verified absence of starvation/lockups is claimed.** Actual gains depend on the serial critical path, GPU bottlenecks, task size and topology measurements.
