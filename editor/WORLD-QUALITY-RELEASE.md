# World quality update — 2026-09-16

Current executable: `bin/world-quality-release/Starlight.Editor.exe`. Both desktop shortcuts and the root launcher point here. Previous `terrain-flow` build and source backup `backups/world-quality-20260916-210702` remain available.

## Using the additions

- Main menu: Camera travel speed, Camera tilt / turn speed, optional arrow navigation, and Frame world. Travel is elapsed-time based, normalizes diagonals, and defaults to roughly four times the prior nominal speed. Sliders are session preferences. Modal file dialogs suspend navigation.
- Builder > Terrain: paint detailed grass, earth, sand, stone and snow. Paint blends across vertices. Terrain display mesh has 193×193 vertices; stored editable heights remain 97×97 with old-scene compatibility.
- Builder > Nature: place a rock, boulder, pebble cluster or 3D grass tuft. Move/rotate/resize with existing shape tools.
- Builder > Sculpting: brush Pull, Push, Smooth or Flatten onto a solid shape, rock or imported mesh. Radius and strength are on the bottom bar. Right mouse reverses push/pull. Each stroke supports Undo/Redo; Escape cancels the active stroke. No volumetric cuts, remeshing, holes or boolean fusion. Imported sources over 80,000 triangles are refused with a message; effects and grass blades are excluded. Save stores the modified mesh.
- Corner categories are Builder, Characters, Environment, More. Music is in More.

## Rendering changes and scope

The previous native viewport disabled upscaling but inherited `modeRequest="quality"`, rendering below output resolution. This build explicitly requests native mode. Runtime checks assert render width equals output width. A lightweight edge-aware compute pass smooths contrast edges. This is not MSAA/TAA or a guarantee that all aliasing is eliminated.

Procedural world-space surfaces provide multi-scale detail and normal variation. These are shader-generated materials, not downloaded photographic textures or a change to imported models' texture resolution. Directional key, sky fill, gradient background and limited soft grounding shadows improve depth. Shadows use spherical bounds for up to 16 mesh parts; they are approximate, not shadow maps, and currently use scene-upload positions rather than following route playback. Existing three local-light limit remains.

Water uses animated ripple normals, a procedural sky reflection, depth coloring and shoreline foam. It remains opaque: no scene refraction, object reflections or physically accurate underwater view. Lava uses animated hot fissures/dark crust; no bloom or new physical reactions. Static and flowing liquids share these shaders. Flow display uses interpolated/clipped shores, while conservative transport stays unchanged on the 48×48 simulation grid. Existing flow physics limits still apply.

NativeScene V4 adds vertex color/material samples; native reader still accepts V1–V3. Scene schema5 saves sculpt meshes and reads schemas1–5. Older editor binaries cannot read newly saved schema5 scenes. The generated native renderer header lives entirely in editor/native, built from the staged engine header plus prepare_renderer.py/world_surface.hlsl. Original engine and paused optimization work are untouched.

## Verification

Clean native and managed builds. `checks/world-quality/result.json` records material geometry, all four brushes, exact Undo/Redo, save/reopen, old-scene migration, native resolution, normalized camera speed, viewport hit testing, pointer sculpt start/release/reverse/cancel, and tool availability. `checks/interaction/result.json` passes existing camera/selection/transform/capture-loss behavior and 150 transformations without geometry reuploads. Terrain checks pass brushes, stamps, blended materials, water, save/reopen, snapping, effects and local music. Flow checks pass conservation, downhill transport, pooling, slower lava and Play/Pause/Reset. Corner controls checks pass fullscreen/windowed layouts. The terrain test was updated to inspect water material and blended terrain, replacing an obsolete count of separate paint meshes.

Native landscape/lava captures were inspected; rock pole/seam geometry was corrected. Visible UI review confirmed camera sliders and the four corner categories. Synthetic window-message tests do not equal user hands-on acceptance. Diagnostic FPS results are small-scene measurements, not a new performance-parity claim. User scenes/backups were preserved; preview is `checks/world-quality/Landscape-preview.starscene`.

Final physical UI verification: SendInput brush drag visibly reshaped a preview rock; Ctrl+Z restored it. Builder-to-Sculpting navigation stayed stable after the physical-window edge fix. Preview remains open; saved scene unchanged.
