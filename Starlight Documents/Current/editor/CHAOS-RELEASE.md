# Chaos, atmosphere and physics release

Implemented 2026-09-17 in `editor/bin/chaos-release/Starlight.Editor.exe`.

## Creator controls

- **Builder → Nature:** Chaos Lightning, Chaos Rock, Light-Bound Shard and Craxis Smoke join Rock, Boulder, Pebbles and Grass tuft. Existing move/rotate/scale handles work. Handles are emissive so they remain usable at night.
- **Character/object settings:** a selected lightning prop exposes local target X/Y/Z and branch spread. Rigid-body settings expose simulation enablement and density in kg/m³. New Rock and Boulder placements enable simulation by default. Raise them or put them on a slope, then press Play.
- **Main menu:** time of day, automatic day/night cycle, day duration and fog density. Apply atmosphere commits settings as an undoable scene edit.
- **Environment:** light brightness/range plus cone angle. A 180° setting is a point light; smaller angles create spotlights. Rotation aims the cone. Chaos props also emit actual local light.
- Play/Pause holds rigid-body motion; Reset restores authored positions and rotations. Saving resets the transient simulation before saving authored state.

## Implemented native paths

`deep/engine/chaos_lightning.h` generates deterministic recursive midpoint-displacement arcs with secondary branches and crossed ribbon geometry. The C ABI exposes this geometry to C#. The material shader animates positional jitter, flicker, purple/pink emission and a white-hot core. A synchronized strike-point light follows the transformed endpoint.

`editor/native/world_surface.hlsl` adds rough, dark corrupted-rock shading with animated emissive fissures and luminous shard material. These use the existing indexed mesh path and GGX lighting; this release does not enable the previously rejected mesh-shader experiment.

`deep/engine/render_device.h` supports an opt-in RGBA16F linear scene target; the creator enables it. The swap chain remains RGBA8 SDR. The creator skips the upscaler evaluation, composites fog in linear space, applies an ACES-style fitted tone curve and sRGB conversion, and preserves native-resolution spatial edge smoothing. Local-light capacity is 32, with an expanded and compile-time-checked frame constant-buffer allocation.

`deep/engine/atmosphere.hlsl` and `atmosphere.h` allocate 160×90×64 RGBA16F froxel volumes. Compute passes inject advected density noise, directional/local light scattering and up to 16 transformed Craxis smoke volumes, then integrate transmittance over logarithmic distance slices. The final pass samples the integrated volume against scene depth at native output resolution. The sky/key/ambient lighting respond to the day/night settings.

`deep/Physics/PhysicsWorld.cpp` wraps Jolt in a separate native DLL. It uses a fixed 1/60-second accumulator, four Jolt workers, convex dynamic collision shapes, static triangle meshes, continuous collision detection, contact friction, contact-dependent rolling resistance and damping. Terrain is divided into 36 tiles of 16×16 cells; asynchronous cooking retains old colliders until replacements are ready. Newer edits coalesce while a tile cooks. The editor keeps this terrain cache through resets and submits edited terrain while building. Prop collision geometry is prepared asynchronously when starting play.

Jolt is pinned to **v5.3.0, commit `0373ec0dd762e4bc2f6acdb08371ee84fa23c6db`**, with its MIT license retained in `deep/third_party/JoltPhysics/LICENSE`. Integration follows its native API rather than substituting visual animation for physics. [Upstream source and examples](https://github.com/jrouwe/JoltPhysics/tree/v5.3.0).

`FlowObstacles.cs` rasterizes transformed solid triangles into four samples per flow cell, tracking occupied vertical intervals and coverage. `TerrainFlow.cs` converts conserved liquid quantity to free-surface elevation around solid volume, so barriers affect transfer potential and submerged objects displace liquid. Moving rigid bodies refresh the mask. Native hydrostatic buoyancy uses water/lava density relative to body density, with linear/angular fluid drag.

## Validation and performance

Native build command: `python editor/native/build.py` (also builds Jolt/physics through CMake). Managed build: `dotnet build editor/Starlight.Editor.csproj -c Release -o editor/bin/chaos-release`.

Saved checks cover dock placement, endpoints/spread, transforms, undo/redo, save/reopen, native dimensions, dirty-tile cooking, falling/rolling rotation, fixed ticks, floating, obstacle blocking, displacement/conservation, and editor Play/Pause/Reset. Existing sculpt, terrain, flow, pointer/transform and corner-menu checks were also run. Reports are in `editor/checks/chaos/` and the existing check directories. Windows computer-use inspection confirmed a physical dock click places a second visible lightning prop; that test placement was undone.

Focused mixed-scene rendering measurements on this machine:

| Native output | FPS | GPU average | P95 frame interval | P99 |
|---|---:|---:|---:|---:|
| 2560×1440 | 421.899 | 0.786 ms | 2.642 ms | 2.834 ms |
| 3840×2160 | 414.378 | 1.887 ms | 2.634 ms | 2.736 ms |

Both used the 500 FPS ceiling, with upscaling and frame generation disabled. Every measured frame retained foreground focus. The mixed packet includes four chaos props, 48 additional rock/grass objects, terrain and a frozen liquid snapshot. These exceed the requested 120/60 FPS rendering targets **for this workload**. They are not large-world or simultaneous high-density physics/fluid performance guarantees. Earlier background/partially focused runs were retained separately; they exhibit Windows presentation throttling around 20 FPS and are not used for target acceptance. Exact reports: `focused-1440.json`, `focused-2160.json`.

## Boundaries and compatibility

- Fluids remain a conservative 48×48 surface-flow approximation with obstacle occupancy. No 3D pressure/momentum solver, splashing, stacked liquid layers, erosion or water/lava reactions. Subcell thin obstacles and overlapping vertical intervals remain approximate. Buoyancy samples the local liquid surface, not a fully coupled pressure field.
- Fog provides volumetric scattering and spotlight beams, but has no geometry shadow-map injection, multiple scattering or gameplay smoke dynamics. Existing surface shadows remain bounding-volume approximations. Smoke volumes currently use spherical bounds derived from prop scale.
- Terrain storage remains 97×97. Dynamic concave sculpted props use convex collision hulls. Colliders catch up asynchronously; this is not live terrain sculpting during active play. Light sources are not shadow-casting lights in this release.
- Schema **6** saves the new authoring fields and reads schemas 1–6. Older editors reject schema 6. Runtime body/fluid state is not serialized. Original user scene files were not migrated in place.
- The creator enables the shared HDR path. Previously built standalone project DLLs were not redeployed or benchmarked. The paused optimization worker and original GameProject reference engine were untouched.

Pre-change source/native backup: `editor/backups/chaos-upgrade-20260917/`. Earlier releases remain available.
