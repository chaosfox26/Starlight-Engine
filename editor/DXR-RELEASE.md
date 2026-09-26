# Starlight DXR 1.1 release

## Entry points

The creator renderer now supports five runtime raytracing presets. Open the main hamburger menu, choose **Raytracing Quality**, enable **DXR 1.1**, select a preset, and click **Apply raytracing**. New scenes default to Off. Settings are saved with the scene.

**Freeze reference animation** holds the current visual clock for a stable path-traced reference. It freezes day/night, material animation and lightning jitter; it does not stop Jolt or route playback. Pause simulation separately when a completely stationary reference is wanted. Moving the camera or editing the scene restarts accumulation. Unfreezing resumes the running visual clock.

Runtime binary: `editor/bin/dxr-release/Starlight.Editor.exe`.

## Implemented tiers

| Tier | Surface transport | Visibility and filtering |
|---|---|---|
| 0 — Off | Existing linear RGBA16F HDR raster path | Existing raster shadows, froxel atmosphere, ACES-style composite and native spatial AA |
| 1 — Low | Ray-query primary surface evaluation with procedural materials and GGX direct lighting | One importance-sampled direct-light visibility query per shaded pixel, choosing sun or local light; small sun-angle jitter supports soft edges. No secondary surface bounce. |
| 2 — Medium | Tier 1 plus one specular reflection bounce | Cosine-weighted RTAO with a 1.5 m radius, sun/local occlusion, reflection hit lighting and environment miss lighting |
| 3 — High | Multi-light direct illumination, reflected light and one cosine-weighted diffuse GI bounce | Each contributing direct light gets visibility; light rays and secondary rays interact with the participating medium; froxel lighting uses geometry occlusion |
| 4 — Path tracing | Stochastic reference integrator for the implemented opaque material/light model; 2–4 secondary surface bounces | Lambert/GGX mixture sampling with PDF compensation, importance-sampled direct lighting, Russian roulette after the second surface interaction, linear temporal accumulation and spatial denoising |

Tier 1's **one ray per pixel** budget refers to direct-light shadow visibility. A separate primary query obtains the triangle/material hit. This implementation does not claim one total traversal per pixel. Pixels with no contributing direct light can issue no shadow query.

Tier 3 evaluates all contributing local lights, whereas Tier 4 samples direct lights stochastically. Consequently Tier 4 can cost less than Tier 3 in a mostly open scene; tier number is not a monotonic performance guarantee. It also has different variance/convergence behavior.

## Native architecture

### Capability and fallback

`deep/engine/dxr.h` queries `D3D12_FEATURE_D3D12_OPTIONS5` for `D3D12_RAYTRACING_TIER_1_1`, verifies shader model 6.5, and obtains `ID3D12Device5` and `ID3D12GraphicsCommandList4` from the existing device/list. Unsupported devices use the raster path and expose a visible status message. Initialization failure also selects raster and reports the reason.

The shaders use DXR 1.1 inline `RayQuery::TraceRayInline` from compute dispatches. There is no ray-generation/miss/hit shader table or `DispatchRays` state object. This is an intentional DXR 1.1 implementation, not software triangle intersection. [Microsoft's DXR 1.1 overview](https://devblogs.microsoft.com/directx/dxr-1-1/) and [DXR functional specification](https://microsoft.github.io/DirectX-Specs/d3d/Raytracing.html) describe this execution model.

`STARLIGHT_DISABLE_DXR=1` is a test override for exercising fallback on capable hardware. It is not evidence from a second, unsupported GPU. `STARLIGHT_D3D_DEBUG=1` enables validation; `STARLIGHT_NATIVE_LOG` selects an early-startup log path.

### Geometry and acceleration structures

- A BLAS cache uses the existing geometry-content hash. Identical meshes share a BLAS, including repeated nature props. Terrain and sculpt edits produce a new geometry key; unchanged meshes retain their BLAS.
- Actual indexed triangles are inserted, not the earlier bounding-sphere shadow proxies. Geometry includes the existing terrain mesh, ordinary shapes, nature meshes, imported mesh parts, water/lava surfaces, and chaos ribbons. Craxis smoke remains a volume, not an opaque mesh.
- Unique mesh vertices and indices are packed into GPU buffers for hit reconstruction. Instance records contain world and inverse-transpose normal matrices, base color/emission, and vertex/index offsets.
- TLAS instance transforms are updated from the same transforms used for raster drawing, including route motion and Jolt snapshots. Stable instance counts use in-place TLAS updates; topology changes rebuild the TLAS. Capacity growth reallocates after prior GPU work retires.
- BLAS builds prefer fast tracing. GPU uploads/build scratch buffers remain alive until the upload/build fence completes. Geometry-set changes currently use a synchronous flush and repack hit buffers; continuous large sculpt operations can therefore hitch.
- The creator's existing scene representation is preserved: this does not introduce terrain chunk streaming, unbounded geometry, GPU skinning, or a virtualized geometry system. A mesh changed by an existing animation/editor path rebuilds when its geometry packet changes; transform-only motion refits.
- Lightning uses a shared small transform jitter in RT mode so raster depth, primary hits and shadow/reflection geometry agree. The branching native ribbon generator and emissive flicker remain in place.

### Resource lifetime and HDR composition

The shared engine device retains its existing swapchain and frame pacing. The DXR module is integrated into `editor/native/viewport.cpp`, which is also used by the native verification executable. Historical standalone project binaries are not redeployed by this change.

The render sequence is:

1. Acquire the existing frame slot and wait for its retiring fence.
2. Rasterize the scene/depth using the existing editor draw path.
3. Build/refit TLAS; trace native-resolution compute rays into a linear RGBA16F image.
4. Update temporal history and run a depth/normal-aware spatial filter into a separate linear RGBA16F output.
5. Inject and integrate the existing 160 × 90 × 64 atmosphere; Tiers 3–4 use inline visibility queries for sun/local-light injection.
6. Composite the selected linear scene with fog, then apply the existing ACES-style tone curve, native spatial AA and SDR encoding.
7. Copy the RGBA8 final output into the swapchain and present with the existing cap/pacing behavior.

AS resources retain `RAYTRACING_ACCELERATION_STRUCTURE` state. Build dependencies use UAV barriers. Ray/filter textures transition explicitly between UAV and non-pixel SRV states. Instance data, constants and shader-visible descriptor tables are indexed by the existing three fence slots. The atmosphere copies its descriptors from a CPU staging heap into the retired slot before replacing its scene SRV; descriptors in flight are not overwritten.

There is no DLSS, FSR, XeSS, reduced-resolution tracing, or frame generation in this path. Four additional full-resolution RGBA16F images use approximately 265 MB at 3840 × 2160, excluding acceleration structures and packed geometry.

### Temporal history

History is reset when camera, geometry/material instances, quality/bounce settings, lights, smoke or fog settings change. Live animated scenes conservatively reset history to avoid stale lighting and motion trails. A frozen reference can accumulate, while the normal live mode relies on the spatial filter for moving scenes.

History weight is capped at 1024 samples. The RNG sequence advances independently of that cap, so sampling continues after the history weight saturates. History is accumulated before tone mapping. Filtering uses a 3 × 3 kernel for hybrid tiers and 5 × 5 for path tracing, constrained by hit depth and normal. This is a basic spatial filter, not a learned denoiser or a motion-reprojected temporal reconstruction system. Live GI near intense emitters can remain visibly noisy; use a frozen Tier 4 reference for accumulation. Hybrid reflections are a simple single-ray approximation rather than fully filtered rough specular transport.

### Material and atmosphere scope

Primary and secondary hits interpolate mesh normals, surface kind and vertex color. Terrain/nature retain procedural detail; water uses animated ripple normals and specular response; lava, lightning, chaos rock and light-bound shards retain their emissive material families. Local emitters continue to illuminate surrounding surfaces independently of sunlight.

The path integrator traces the renderer's current opaque scene model. Water is not a refractive dielectric volume, alpha-tested foliage is not introduced, and there is no subsurface transport. Fog uses finite-step medium transmittance on lighting/secondary rays plus the existing frustum-grid primary-ray composite. It is a single-scattering approximation, not an unbiased volumetric path tracer. These limitations are material when comparing this reference tier with an offline production renderer.

## Editor and persistence

`SceneData.cs` now writes schema 7 and reads schemas 1–7. It validates preset range 0–4 and bounce range 2–4. RT settings include enable, preset, bounce count and freeze-visual-animation state. Earlier scenes default to raster.

`NativeScene.cs` writes viewport packet V6, with RT fields after the atmosphere/smoke block. The native reader retains V1–V5 compatibility. `NativeViewport.cs` owns the native calls on its render thread and exposes support, active tier, ray time, TLAS time, BLAS build count, TLAS refit count, sample count and history reset count. Resizing recreates the native context and reapplies the current scene settings.

Applying RT settings does not call the editor's `Stop`/physics reset path. Changes mark the scene dirty; when not playing they also enter scene undo history. Play, Pause and Reset keep their existing simulation meaning. Scene save retains the existing atomic replacement/stale-file protection.

## Verification and reproduction

Build with:

```powershell
python editor/native/build.py
dotnet build editor/Starlight.Editor.csproj -c Release -o editor/bin/dxr-release
```

The native build compiles SM 6.5 shaders with the installed Windows SDK DXC, embeds the resulting DXIL, regenerates the editor renderer/atmosphere source headers, builds Jolt, then builds the viewport DLL and native harness. No DXC DLL is required at application runtime.

Run `Starlight.Editor.exe --dxr-check` for tier controls, migration/validation, save/load, native-width checks, accumulation/reset, BLAS reuse, terrain rebuilds, frozen chaos accumulation and Jolt Play/Pause/Reset compatibility. Existing `--chaos-check`, `--quality-check`, `--interaction-check`, `--terrain-check`, `--flow-check` and `--chrome-check` cover the earlier features.

The native benchmark remains backward compatible with its original arguments and adds optional tier, presentation mode and bounce count:

```text
Starlight.NativeCheck.exe scene.packet report.json capture.bmp width height [tier] [headless|focused] [bounces]
```

It freezes visual animation to compare a fixed snapshot, warms up for two seconds, then measures three seconds. Focused measurements require every measured frame to be foreground. Headless presentation may be throttled by Windows; use its GPU timestamps, not its presentation FPS, to evaluate render cost.

Reports include FPS, GPU render time, ray-dispatch time, TLAS time, denoise time, frame percentiles, active/requested tier, native render width, BLAS builds, TLAS refits, samples and bounce count. Ray-dispatch timing covers the main surface/path kernel; volumetric injection rays are included in total GPU time. BLAS construction is a cold/scene-edit cost outside the warmed frame measurement.

Final measurements, validation results, source hashes and the unified source diff are under `editor/checks/dxr/`. The release manifest identifies the exact binaries and checks. The bounded mixed-scene benchmark contains terrain, liquid surface, chaos props and 48 repeated rock/grass objects; it is not a heavy simultaneous physics/fluids stress test or a guarantee for arbitrary scenes.

## Focused 4K measurements

NVIDIA GeForce RTX 5090, 3840 × 2160 render and output, native resolution, upscaling and frame generation off. Two-second warmup plus three-second measurement per tier. Every measured frame was foreground. Tier 4 uses three secondary bounces. This is the fixed mixed scene described above.

| Tier | Presented FPS | GPU ms | Surface/path ray ms | TLAS ms | Filter ms | Frame P99 ms |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 419.5 | 1.903 | 0.000 | 0.000 | 0.000 | 2.796 |
| 1 | 319.7 | 3.101 | 0.601 | 0.045 | 0.098 | 3.813 |
| 2 | 273.8 | 3.622 | 1.111 | 0.052 | 0.114 | 4.314 |
| 3 | 186.1 | 5.340 | 2.748 | 0.051 | 0.126 | 6.109 |
| 4 | 248.2 | 4.008 | 1.601 | 0.032 | 0.148 | 4.575 |

The initial implementation read write-combined upload memory while hashing instance data, causing a CPU bottleneck around 114 FPS. The delivered code builds and hashes ordinary CPU structs, then copies them once into upload memory. Initial measurements are retained as diagnostics; use only `final-*` reports for release comparisons.

## Changed modules and rollback

- `deep/engine/dxr.h`, `dxr.hlsl`: capability checks, BLAS/TLAS, primary/secondary transport, history, filtering, GPU timestamps.
- `deep/engine/atmosphere.h`, `atmosphere.hlsl`: per-frame scene descriptors and ray-occluded volumetric lighting.
- `editor/native/viewport.cpp`: scene geometry cache, draw transforms, RT configuration/status and composition.
- `editor/native/build.py`, `prepare_renderer.py`, `standalone.cpp`: DXIL build, matching lightning transforms and per-tier performance harness.
- `editor/MainWindow.xaml`, `ChaosTools.cs`, `NativeScene.cs`, `NativeViewport.cs`, `NativeEditor.cs`, `SceneData.cs`: runtime controls, persistence, bridge and telemetry.
- `editor/DxrChecks.cs`, `WorldQualityChecks.cs`: focused regression coverage and schema migration expectation.

Before-images are in `editor/backups/dxr-upgrade-20260917`. The prior `editor/bin/chaos-release` remains available. This release does not modify the original GameProject engine, Kira, paused automation, Starbus, or the Jolt/terrain-fluid solver implementations.

Schema 7 scene files require this release; use the preserved scene backups when returning to an older editor.
