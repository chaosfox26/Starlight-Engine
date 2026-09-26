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
