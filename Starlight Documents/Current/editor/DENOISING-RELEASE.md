# Native-resolution Ray Reconstruction and temporal denoising

Release snapshot: 17 September 2026. Current staged executable: `editor/bin/denoise-final/Starlight.Editor.exe`. Exact binary hashes, final measurements, and validation scope are recorded in `editor/checks/denoise/final-validation.json` when deployment validation completes.

## Controls and compatibility

Main menu → Raytracing Quality now includes **Ray Reconstruction** (native resolution is stated in its tooltip and status). Checked requests NVIDIA RR; unchecked selects the built-in temporal wavelet denoiser. Click Apply raytracing. Both retain the selected DXR tier. Missing/unsupported/failed RR falls back automatically and exposes the reason in status. New preference defaults to enabled, but scenes with DXR Off still use raster.

Input and output dimensions are identical, including 2560×1440 and 3840×2160. No Super Resolution scaling, reduced internal resolution, frame generation or Streamline swapchain hooking is enabled. NVIDIA calls the native 1:1 quality setting `NVSDK_NGX_PerfQuality_Value_DLAA`; that enum does not reduce the render size.

`RayReconstructionEnabled` is an additive optional field in JSON schema 7. Old schema-7 editors can read the geometry but ignore this preference; missing fields default to true in this release. Native packet V7 carries the preference and retains V1–V6 support. Native binaries must match their managed editor. Applying the preference preserves Jolt and playback; undo/save roundtrip is tested.

`STARLIGHT_DISABLE_DXR=1` disables rays and uses raster; it cannot sensibly denoise a nonexistent DXR signal. `STARLIGHT_DISABLE_RR=1`, disabling the UI checkbox, or an unavailable DLL retain rays with native denoising.

## External module integration

The supplied archive `D:/Upscaler updates/export_dlss_d_v310.9.0.0_C45E1FFE3EC65E02A6466D3780757A6C.zip` contains `nvngx_dlssd.dll`. That file was extracted beside the archive, with the archive preserved. Its file version is 310.9.0.0 and its Authenticode signature validated as NVIDIA Corporation. The release manifest records its SHA256.

`deep/engine/ray_reconstruction.h` loads the explicit DLL path using `LoadLibraryExW` with restricted dependency search flags. `STARLIGHT_RR_DIRECTORY` optionally selects another directory. The default is `D:/Upscaler updates`; the RR binary is not silently copied over older engine vendor DLLs.

The feature DLL is not a standalone public C function that can be guessed and called with an invented structure. The implementation uses the existing official NGX headers and `nvsdk_ngx_d.lib` from the installed SDK under the original workspace's Streamline `external/ngx-sdk` directory. Those are build dependencies; the original files are not modified.

Initialization uses `NVSDK_NGX_D3D12_Init_with_ProjectID` with a project GUID/custom engine identifier and explicit feature search path. Capability comes from `NVSDK_NGX_D3D12_GetCapabilityParameters` and `SuperSamplingDenoising.Available`. `NGX_D3D12_CREATE_DLSSD_EXT` creates `NVSDK_NGX_Feature_RayReconstruction` at width=targetWidth and height=targetHeight, HDR input, packed normal/roughness, linear depth and native DLAA quality. `NGX_D3D12_EVALUATE_DLSSD_EXT` records feature work on the existing Direct command list.

Calls are serialized across editor viewport devices by a mutex. GPU retirement precedes `ReleaseFeature`, parameter destruction, device-specific `Shutdown1`, and `FreeLibrary`. Init/evaluate failure reports status and selects native denoising. A failed initialization is not retried every frame; recreate the viewport/application after repairing the installation.

RR evaluation has succeeded on the local RTX 5090. Capability failure is exercised with overrides/missing paths; broad non-NVIDIA hardware testing has not been performed.

## Trace and guide buffers

Compute inline `RayQuery::TraceRayInline` and `cs_6_5` remain. No RT state objects, hit groups or shader tables were added. Primary rays use pixel centers so material, depth and motion guides align. Stochastic lighting and secondary paths remain stochastic; the earlier path-tier random primary subpixel jitter was removed rather than feeding inconsistent camera jitter to RR.

`dxr.hlsl` now decomposes primary diffuse and specular transport. The path integrator propagates separate first-interaction diffuse/specular weights through subsequent bounces; these are real transport components, not copies of combined color. Native filtering consumes demodulated diffuse/specular radiance and later multiplies by their reflectance guides. Primary emission is kept separate and added after native filtering to protect bright cracks and thin lightning.

Guides include:

- Normalized world-space shading normals with linear roughness in alpha, RGBA16F.
- Linear diffuse and approximate specular reflectance, RGBA16F. Specular reflectance is the existing Schlick-style approximation, not a full integrated material LUT.
- Positive view-space linear depth, R32F, matched to the traced primary surface.
- Current-to-previous pixel displacement, RG16F, including camera and per-instance object motion.
- Native-only motion metadata: expected prior depth and an exact half-float encoded object identity. Encoding preserves distinct IDs across the creator's part limit.
- Specular hit distance, R32F, for RR reflection guidance with world-to-view and view-to-clip matrices. Path-tier distance follows the first sampled continuation; hybrid reflection distance follows its reflected ray.
- Separate diffuse/specular noisy signals, primary emission and noisy combined linear HDR color.

The official RR interface consumes noisy **combined** color plus albedo/normal/depth/motion/reflection guides and returns a fully composed linear image. It does not consume the two native demodulated signals as interchangeable color inputs. RR output therefore is not multiplied by albedo a second time. This intentionally corrects the generic execution diagram in the request.

## Native temporal wavelet denoiser

`deep/engine/svgf.hlsl` is compiled into four compute entry points:

1. **Prefilter:** geometry-weighted 3×3 luminance statistics, excluding the center from the outlier estimate, clamp isolated high-energy samples before spreading. Primary emission bypasses this stage.
2. **Temporal:** reproject to the prior pixel using camera/object motion; reject out-of-bounds, object-ID mismatches, depth-plane differences and incompatible normals. Clamp history to a local color/variance envelope, update first/second luminance moments and history length. Static history is capped at 64; animated lighting uses a shorter 12-frame cap and reactive weighting.
3. **Atrous:** three 5×5 B3-spline wavelet iterations with step sizes 1, 2, 4, independently filtering diffuse/specular signals with depth, normal and variance/luminance edge stopping.
4. **Composite:** remodulate diffuse/specular reflectance and add current primary emission into linear RGBA16F output.

Moments use RGBA32F so bright samples cannot overflow half-precision second moments. History resets for geometry-set changes, denoiser/tier/bounce changes, large camera cuts and transition from RR to native fallback. Smooth camera/object movement reprojects instead of globally clearing history. Animated lighting no longer forces the native history itself to reset every frame; the inherited sample counter still represents frozen/static reference accumulation and is not the native per-pixel history length.

This is a compact SVGF-style implementation, not a full reproduction of the SVGF paper or a production denoiser suite. Reprojection currently selects a nearest prior pixel; no sophisticated multi-tap specular reprojection, adaptive wavelet level, or reservoir resampling is implemented. Clamping is biased and can suppress rare bright contributions. Bounds/thresholds favor stability and may soften fine indirect lighting. Universal zero ghosting, zero flicker, or lossless detail preservation is not claimed.

## Frame order, resource safety, and cost

```text
Existing raster scene/depth
  -> native DXR Trace + primary-surface guide buffers
  -> RR combined-color evaluation OR native Prefilter/Temporal/Atrous/Composite
  -> existing atmosphere injection/integration and HDR composite
  -> existing tone mapping, SDR encoding and native edge AA
  -> Present
```

The RT guide generation occurs in the primary compute trace to match the surfaces actually shaded by the rays, rather than assuming the old raster motion field includes all dynamic instances.

The Direct queue remains the only execution queue for these passes. The bounded root table contains 26 SRVs and 26 UAVs; each of three retiring frame slots owns its shader-visible heap. Per-frame constants and instance uploads retain three fence slots. Ping/pong history is sequenced on the Direct queue; explicit transitions order SRV/UAV writes across passes/frames. SDK calls may change root/heap state, so bindings are restored even after feature-creation failure. All uploads retain the write-only CPU rule.

There is no per-frame CPU readback of images. An explicit diagnostic `sl_denoise_capture` synchronizes and reads images only when invoked by the offline validation harness; normal editor frames never call it. GPU timing uses already retired timestamp slots.

There are 26 full-resolution guide/history/filter resources (including two RGBA32F moment buffers, two R32F scalar guides and one RG16F RR motion buffer). Their payload is about **1.76 GB decimal at 4K**, excluding alignment, existing scene resources, AS and NVIDIA's internal allocation. Resources for both paths are retained to permit immediate switching. This is a material memory cost; the fallback is relatively light in compute cost, not a low-memory solution. Future lazy allocation/packing should retain the same lifetime tests.

Saved focused timing reports are `editor/checks/denoise/final-2160-tier*-{native,rr}-focused.json`. They report actual denoiser selection, presented FPS, GPU/ray/AS/denoise timing and foreground counts. Only final manifest-listed hashes/results describe the delivered build. Native RR adds several milliseconds; it is not a performance shortcut or a universal 120 FPS promise.

## Validation and limits

- Clean native and managed builds, plus `--denoise-check` tests all five tiers, both denoisers, actual RR activation, native width, persistence, transforms/BLAS reuse, terrain rebuild, frozen references and running/paused Jolt compatibility.
- `editor/native/check_denoise.py` reads actual raw/filter/guide images to check finite values, static ground temporal variance reduction, camera motion, native disocclusion rejection, camera cuts, animated shader/camera sequences, moving-prop motion and runtime switching.
- Missing DLL selects native while DXR stays active. Forced disabled DXR selects raster and reports zero ray/denoise work.
- DirectX debug logs and physical editor UI checks accompany the manifest. Static variance reduction alone is not proof of image accuracy or absence of motion artifacts.
- Image-sequence metrics use a bounded 640×360 chaos scene; performance runs use a native 4K fixed mixed scene on RTX 5090. These are different validation purposes, not interchangeable quality/performance guarantees.
- No full 3D fluid solver, new physics model, refractive material, world streaming or changes to original GameProject/Kira/paused automation were introduced.


## Final focused 4K measurements

RTX 5090, frozen mixed scene (terrain, liquid, chaos and 48 nature props), 2 s warmup + 3 s measurement, cap 500, three secondary path bounces. Every measured frame retained foreground focus. These are rendering measurements, not simultaneous heavy simulation stress.

| Tier | Native FPS | Native GPU ms | Native filter ms | RR FPS | RR GPU ms | RR ms |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 174.1 | 5.728 | 3.035 | 115.0 | 8.679 | 5.954 |
| 2 | 166.6 | 5.990 | 2.898 | 110.8 | 9.008 | 5.876 |
| 3 | 135.7 | 7.356 | 2.880 | 99.6 | 10.026 | 5.762 |
| 4 | 153.8 | 6.488 | 3.039 | 105.2 | 9.491 | 5.915 |

## Reproduction

```powershell
python editor/native/build.py
dotnet build editor/Starlight.Editor.csproj -c Release -o editor/bin/denoise-final
editor/bin/denoise-final/Starlight.Editor.exe --denoise-check
python editor/native/check_denoise.py
```

The editor check writes its own fixtures under `checks/denoise/regression`, preserving the previous DXR release's fixture. Do not regenerate a fixture while it has unsaved edits open. Build into a fresh directory if the current executable is running.

The native benchmark adds one optional final argument: `0` native temporal denoiser, `1` request RR:

```text
Starlight.NativeCheck.exe packet report capture width height [tier] [headless|focused] [bounces] [denoiser]
```

Telemetry additions are `sl_rr_active`, `sl_rr_status`, `sl_denoiser`; `sl_denoise_ms` measures the selected reconstruction/filter interval. Diagnostic captures and native history index are test APIs. Headless presentation may be throttled by Windows; interpret GPU time independently of headless FPS.

Prior binaries and before-images live in `editor/bin/dxr-release` and `editor/backups/denoise-upgrade-20260917`. Loader dependencies remain local SDK/DLL paths; portable redistribution is not established by this integration.

API guidance: [NVIDIA Ray Reconstruction integration guide](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS_RR.md), plus the installed official NGX D3D12 helper headers used by the implementation. The code uses direct NGX rather than Streamline injection.
