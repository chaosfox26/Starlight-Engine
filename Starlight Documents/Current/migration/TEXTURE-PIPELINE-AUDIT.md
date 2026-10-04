Evidence location: D:/Starlight-Backups/Graphics-overhaul-evidence-20261001/graphics-surface-maps-checks/; prior fire-depth fixtures now live in the neighboring graphics-fire-depth-checks/. Resize and tail-readback tests pass in both paths.

## 2026-10-01 - Shared native surface-map implementation

The editor and player now use five custom generated material tiles (grass, soil, sand, rock and snow) in three Texture2DArray resources: relative albedo/sRGB, tangent normal/linear and AO-roughness-metallic/linear. This is actual mipmapped texture storage and sampling, not solely per-pixel procedural shading. It is an initial custom library, not a photographic asset collection or completed visual acceptance. Kind6 uses the rock layer; authored vertex color remains the material tint. Terrain material transitions blend adjacent array layers. World-space triplanar mapping avoids stretching on vertical terrain and rocks; reconstructed slope guides project onto the geometric tangent plane.

surface_texture_data.h generates periodic tiles and complete mip tails. Albedo mips filter in linear light; normals normalize after filtering; accumulated normal variance increases roughness rather than leaving high-frequency highlights at distance. Raster uses SampleGrad and16x anisotropic filtering; DXR uses explicit distance-based mip selection. The latter is not yet a propagated ray-cone model for secondary paths. Strongly angled views, long motion sequences and specular aliasing still require acceptance work.

surface_textures.h uploads three bounded five-slice arrays once per viewport and retains no CPU mip data after upload completion. Default1024, optional STARLIGHT_SURFACE_RESOLUTION=256/512/1024. This is a diagnostic cap, not yet a saved user-facing quality setting. Exact D3D12 allocation sizes:5308416,21037056,83951616bytes. Uncompressed RGBA8; compression/streaming/imported texture sets and authoring UI remain open. Resources transition for both pixel and compute reads. DXR per-frame heaps append the same three views; raster has a separate surface table. Shader root changes are editor transforms, preserving the generic model renderer. Both native and Python renderer preparation now use one transform manifest.

Water also has opaque scene color/depth guides for raster refraction. The raster draw separates opaque geometry from water, snapshots guides once when needed, then renders water with foreground rejection, refraction and absorption. Guides recreate on resize; their allocation is reported separately from the surface bank. Ray-traced water uses geometric refraction through a candidate-filtered query that skips water triangles, true scene reflection, absorption and a dielectric Fresnel split. Raster reflection still uses the sky approximation. Neither path provides complete underwater camera/stacked-liquid behavior or finished caustics/shore interaction. The eight-wave normal spectrum is an appearance approximation; it does not replace the physics solver or displace the mesh.

Lava now uses advected, domain-warped cellular crust plates with filtered molten gaps, varied hot interiors and roughness/emission. It still lacks solid crust depth, full thermal lighting and realistic fluid/shore geometry. Do not call the overall graphics overhaul complete.

Checks:54 native texture checks pass, including semantic mips, normal variance, tile repeat, material determinism and existing BC5/BC7 WARP upload. Raster/DXR1080p checks with graphics debug enabled pass; GPU readback confirms all15 material/channel mip tails match CPU values at256/512/1024. Red/blue submerged terrain probes change only below-water vertex colors; resulting images differ in both paths, confirming scene-based transmission. Animated still pairs and local GPU/memory metrics are evidence for this host only. Exported runtime and editor workflow acceptance remain pending. Current executable builds include these changes; installed release and distribution package remain unchanged.

# Texture pipeline upgrade: current implementation and next integration

## Audit findings

The native editor's `world_surface.hlsl` shades default grass, earth, stone, water and lava procedurally. Editor mesh packets carry positions, normals and surface/color values; they do not carry a complete PBR material/texture set. Increasing a nonexistent source texture's dimensions cannot improve those defaults. The separate runtime model path in `deep/projects/GameProject/project.cpp` has image texture sets; that does not establish editor material support.

The original `deep/engine/model/texture.cpp` loader decoded images with WIC into RGBA8 and built box-filtered mips. Upload rows assumed `width * 4`, incompatible with compressed blocks. Its mip cap is a load-time choice, not demand-driven streaming. Normal maps and albedo also share an overly simple mip-generation policy. These distinctions matter before buying or producing a large asset library.

## Implemented in this pass

- `TexImage::format` carries the image format alongside tightly packed mip data.
- `loadTextureDDS` reads a DX10 DDS header explicitly and accepts BC5 UNORM, BC7 UNORM and BC7 sRGB, one 2D image. It validates dimensions, mip count, supported shape/format and complete file payload before allocating retained mips.
- Load-time resolution selection skips authored high-resolution mips without decoding or resampling blocks. Both 2K and 4K inputs are supported, up to the D3D12 16K dimension limit.
- `loadTextureRGBA` dispatches `.dds` inputs to that loader; existing image callers keep their API.
- `uploadTexture` validates all mip byte lengths, chooses format-appropriate typeless storage and uses `GetCopyableFootprints` row sizes for compressed block copies. Upload resources remain owned by the caller's keep-alive list until GPU completion.
- `textureViewFormat` makes albedo sRGB and guide/data maps linear; the runtime model SRV creation recognizes BC formats instead of forcing RGBA8 views.
- The runtime model shader reconstructs tangent-normal Z from XY, supporting BC5's two channels (and conventional positive-Z RGBA normal maps).
- Native checks cover 2K BC7, 4K BC5, mip selection, zero cap, truncated tail, unsupported format, excessive mips, missing requested mip, malformed GPU upload, fence retirement and compressed block readback.

## PBR contract for the asset phase

| Map | Required authoring convention | GPU target |
|---|---|---|
| Albedo | Base reflectance without baked directional illumination; alpha only where used | 2048² minimum; 4096² close-up option; BC7 sRGB |
| Normal | Tangent-space XY, documented DirectX Y convention; reconstruct Z in shader | Same dimensions; BC5 UNORM, linear |
| ORM | R = ambient occlusion, G = perceptual roughness, B = metalness; unused A = 1 | Same dimensions; BC7 UNORM, linear |
| Emission (optional) | Separate emission color/mask and intensity; avoid baking intensity into albedo | BC7 or HDR format according to dynamic range |

Grass, earth, sand, weathered stone and rock should each have traceable source/license information, real physical tiling scale and a complete mip chain. A 2K BC map is approximately 5.33 MiB with mips; three BC maps are approximately 16 MiB per material. At 4K those totals become about 21.33 MiB per map and 64 MiB per material, excluding allocation alignment and runtime overhead. Thus 4K everywhere is not an appropriate default residency policy.

## Not yet implemented / not claimed

- No new photographic 2K/4K default material library was installed by this pass.
- Default terrain and props still use the existing procedural editor shaders; they have not become PBR merely because the loader supports BC formats.
- No asynchronous demand/eviction manager, tiled-resource residency, BC encoder or full PBR material import UI was added.
- No normal-aware or linear-light offline mip cooker was added. Prefer authored DDS mips for the new compressed path until that cooker is implemented.
- Raster and DXR must share the future material evaluation contract. Connecting maps to only the raster path would cause visible material changes when switching tiers.

Next concrete integration: a versioned native material manifest plus three curated terrain materials; retain UV/tangents or introduce triplanar sampling; bind albedo/normal/ORM per material in both raster and DXR; add budgeted asynchronous residency; then replace the default demonstration scene and capture comparable images. The present change initiates that work at the resource-loader boundary and has executable validation rather than a placeholder loader.


## 2026-10-01: shared procedural material foundation

Added deep/engine/surface_material.hlsl as one shared material evaluator for raster and DXR: world-space multi-scale variation, material-dependent roughness, analytical noise gradients, rock strata and distance/footprint suppression of unresolved detail. Native and Python shader preparation both inline this common source into raster code; DXR includes it directly. CMake tracks the new dependency. This improves the procedural foundation; it does not implement photographic texture import, streaming, or a complete PBR asset library.

Native build passed. Two-second frozen 1280x720 headless smoke checks passed at tiers0 and1 (out/graphics-overhaul-checks/material-after-tier0.json and tier1.json). GPU times about0.194ms raster and0.605ms tier1 on the current host. These are limited smoke-scene measurements, with a500FPS cap, not an asset-heavy benchmark or evidence of no jaggies. Raster before was0.196ms in that same smoke scene; this is not a meaningful performance gain claim. Dedicated material coverage and motion/aliasing scenes remain required.

## Whole-engine graphics coverage - 1 October 2026

The user explicitly reaffirmed that every placeable object and all terrain are in scope, not just selected effects or resolution increases. The goal is lifelike fantasy, preserving supernatural colors while improving geometry, materials, lighting, motion and physical interaction. No category is accepted as finished merely because it builds or passes behavioral checks. The native core runner writes placeable-object-inventory.json from the live palette; each entry remains visualAcceptance=pending until representative scene evidence is accepted.

| Category | Current implementation | Remaining quality work |
|---|---|---|
| cube, rectangle, trapezoid, octagon | Authored solid primitives/sculpting; saved Surface choices now reach both rendering modes | Material authoring, edge treatment appropriate to shape, close-up and scene acceptance |
| rock, boulder, pebbles | Rebuilt shared-edge stone meshes and filtered material maps | Convincing geological detail, variation and scene lighting |
| chaos-rock | Rebuilt stone, shared filtered rock maps and spatially flowing purple fissures | Richer detail, object-local mapping and emitted-light interaction |
| grass | Curved folded blades, shared leaf-local material/thin-light response and rooted wind/contact bending | Final detail, dense/distant edge stability, scene lighting and performance |
| fire | Animated depth-clipped volume plus physical particles | Flame detail, wind-driven volume, secondary-ray lighting and particle appearance |
| light | Existing point/cone light source and visual marker | Refined source geometry, falloff and material/scene interaction |
| lava | Moving crust-plate shader and existing liquid solver | Solid crust depth, thermal illumination and volume-terrain interaction |
| chaos-lightning | Existing gameplay strike and contact behavior; internal mist arcs upgraded | Authored/runtime external bolt geometry, emitted light, branch motion and stable coverage |
| light-shard | Closed beveled crystal mesh, faceted normals and shared polished magical material | Final detail, optical/lighting response and scene acceptance |
| craxis-smoke | Separate animated chaos field, thin wisps, motes and internal purple arcs | Cloud detail, lighting, reflections, volume-aware terrain exclusion and final acceptance |
| all terrain surfaces | Denser grid, solid volume edits, chunk cache, filtered color/normal/roughness maps | Responsive sculpt updates, complete cave-compatible liquid/fog/NPC behavior; particle contact and bounded NPC cave navigation verified, scene quality |
| water | Scene refraction in raster and ray tracing, gravity-wave normals | Actual wave displacement, shoreline detail, scene reflections and cave-compatible fluid behavior |
| splash droplets and impact sparks | Closed velocity-aligned meshes; dielectric transmission and cave contact verified | Live upload cost, scalable geometry, dense budgets and final visual acceptance |
| sculpted meshes and imported models | Existing source preservation and import paths | PBR authoring/import consistency, normals/materials and editor/export acceptance |
| rain/weather | Optional scene controls, shared bounded 3D drops, wind/contact, splashes and water ripples | Wet surfaces, storm lighting, precise collision, final appearance and live performance |
| every test scene and exported runtime | Existing test assets preserved | Re-run combined scenes, editing, saving, export and hardware/quality performance checks |

Increasing texture dimensions alone does not meet this scope. Existing deliberate primitive shapes remain useful authoring tools; quality changes must preserve their editing and physics behavior. Imported models retain their authored shape rather than being blindly subdivided.
