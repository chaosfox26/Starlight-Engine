# Starlight native C++ migration — 2026-09-17

> **Subsequent interface restoration:** ChaosFox prefers the former design. The native editor now restores its canvas-first arrangement, Starlight icon/branding, blue-purple rounded panels, compact corner menus, floating build dock and purple playback controls. Native C++ remains mandatory; migration is not permission to redesign the interface. The fixed-sidebar appearance described in the original migration record below has been superseded. See `INTERFACE-RESTORATION.md`.

## Delivered architecture

The active Starlight editor and engine build is native C++. The former 47 active C# modules were replaced by C++ implementations under `editor/cpp`, with the existing C++ D3D12 renderer and Jolt physics compiled through the root CMake project. C# sources, WPF XAML, C# project definitions, managed output folders and historical managed backups were relocated intact outside the repository. No managed runtime is required by the delivered editor.

**Launch:** `D:/projects/Starlight-Engine/Launch-Starlight-Editor.cmd`, or `out/native/Release/Starlight.NativeEditor.exe`. Both existing desktop shortcuts target this executable. The native UI uses Windows controls and fixed panels; it is a functional replacement, not a reproduction of the former WPF visual design.

**Recovery archive:** `D:/projects/_recovery/starlight-managed-retired-20260917`. It contains the prior working editor, source, source hashes, original launch/build scripts and retirement manifest. Recovery is by copying archived paths back to their original repository-relative locations. The archive is historical material, not part of the native build. Existing user scene/asset files were not migrated or rewritten.

## Discovery scope and dependency order

The initial recursive `D:/projects` inventory identified 1,833 managed-related artifacts: 335 inside Starlight and 1,498 outside it. Many were generated outputs or backups. Forty-seven files were active editor C# modules. Sibling repositories and vendor SDK examples outside Starlight were inventoried, not modified. The inventory also includes `.sln`, `.props` and `.targets`; these extensions alone do not imply managed code. Native CMake/MSVC/Jolt generated projects are retained.

See `DEPENDENCIES.md`, `dependency-audit.json`, and `workspace-artifacts.txt` for the original inventory and dependency graph. Migration followed:

```text
Windows RAII and math
  -> schemas and serialization
  -> geometry, model ingestion, commands and simulation
  -> project/preset handling and native render-session ownership
  -> native UI, input, history and playback
  -> native regression tests and runtime dependency verification
  -> managed retirement and launcher cutover
```

## Module translation

| Former component(s) | Native implementation | Responsibilities |
|---|---|---|
| SceneData, CharacterSettings | scene.hpp, scene.cpp, scene_json.cpp | Schema 1–7 migration, character properties/routes, validation, schema 7 output |
| TerrainData | scene.cpp, geometry.cpp | 97×97 height/paint storage, bilinear samples, terrain brushes, 193×193 render mesh, clipped water plane |
| ObjectGeometry, NatureGeometry, EffectGeometry, SurfaceDetail | geometry.cpp, color.hpp, named_colors.hpp | All 15 shapes/nature/effect types, original seeded nature geometry, native lightning ribbons, material attributes |
| GlbModel | assets.cpp | GLB 2 static triangle import, node transforms, base-color factors, bounds and malformed-input validation |
| TerrainFlow, FlowObstacles | flow.cpp | 48×48 liquid transport, water/lava separation, emissions, conservative transfers, solid footprints, liquid surface meshes |
| PhysicsWorld | physics.cpp | Direct native Jolt API calls, asynchronous collision cooking, fixed-step simulation and buoyancy inputs |
| NativeScene | render_scene.cpp | Native V7 scene packet encoding, lights/atmosphere, actor meshes, routes and overlays |
| NativeViewport, NativeEditor | native_session.cpp | HWND renderer ownership on a native worker, serialized scene changes, playback, captures and telemetry |
| ObjectSnapping, PointerInteraction, SculptTools | interactions.cpp, mesh.cpp, editor_main.cpp | Ray picking, gizmos, group transforms, snapping, subdivided sculpt meshes and brushes |
| EditorHistory | mesh.cpp | Value-snapshot undo/redo, stroke grouping, transaction rollback |
| EditorCommands, FeatureCatalog | commands.cpp, command_runner.cpp, features.hpp | Provider-independent JSON command interface, validated operations, feature status catalog |
| DialogHistory, Diagnostics | platform.cpp, deep/engine/crash_diagnostics.h | Native file dialogs, browse/save history, atomic files, logs and minidumps |
| ProjectWindow | projects.cpp, editor_main.cpp | `.star` loading, `.startest` reconciliation, isolated temporary checks, native host launch |
| LocalMusic | music.hpp, editor_main.cpp | Native WinMM MCI playback, playlists, pause/stop/next/volume |
| MainWindow, App, BuildDock, BuildingTools, ChaosTools, EffectsTools, TerrainTools, FlowTools, WorldNavigation | editor_main.cpp | Native application lifecycle, controls, inspector, editing and camera routing |
| FullScreenCanvas, ForegroundFocus, EdgeDimming, WindowTheme | editor_main.cpp / Win32 lifecycle | Native window/full-screen behavior and UI presentation; WPF styling/edge dimming are replaced rather than retained |
| FeaturePanels | features.hpp / command discovery | Original future-feature statuses retained as metadata; the old WPF feature-preview overlay is retired |
| All managed *Checks modules | core_checks.cpp, session_checks.cpp, diagnostic_checks.cpp, migration validation scripts | Native behavior, lifecycle, diagnostics and render-fallback regression coverage; old WPF-specific layout assertions are not applicable |

### Ownership and threading

- Scene/model data uses value types, `std::vector`, `std::optional`, strings, unordered maps/sets and explicit validation. There is no reflection or garbage collector.
- Dense fluid obstacle masks use owned vector storage. A regression test exposed `0xc00000fd` stack overflow from large by-value masks on the default Windows thread stack; moving their arrays to heap-backed value storage fixed it without raising the stack limit.
- The native render session owns the viewport and physics through RAII. A `std::jthread`, mutex and condition variable serialize pending scenes, camera changes, playback and captures. Scene updates coalesce rather than creating an unbounded task queue.
- Native Jolt keeps its existing worker/collision-cooker architecture and 60 Hz fixed step. Project checks use a native child process with owned process/thread handles and a timeout, launched from a native asynchronous task.
- Undo stores scene value snapshots; a failed command batch preserves the original scene bytes. Saves use SHA-256 optimistic concurrency, a lock, flushed temporary output, replacement and backups.

### Native boundaries

The editor links to native `Starlight.Viewport.dll` and `Starlight.Physics.dll`. Their C-linkage exports are a native DLL ABI called directly from C++; no P/Invoke, marshaling, C++/CLI or managed bridge remains. Windows file dialogs use `IFileDialog`/`IFileDialogEvents` directly. Native COM for dialogs/WIC and native WinMM are OS facilities, not CLR dependencies. The local JSON command interface and native project-host child process remain intentional tool/runtime interfaces.

## Unified build

```powershell
python Build-Native.py --config Debug --test
python Build-Native.py --config Release --test
```

`CMakePresets.json` selects Ninja Multi-Config and `out/native`. The bootstrap discovers the installed x64 MSVC environment and Ninja. It does not invoke MSBuild or dotnet. `editor/native/build.py` and `editor/check-editor.ps1` now delegate to this same build.

Targets include the editor core/session libraries, editor GUI, command CLI, project host, viewport DLL, physics DLL/Jolt, Empty/game project DLLs, and native checks. Project modules are rebuilt from this repository's C++ source. The existing copied historical C++ source snapshots are not alternative active build definitions.

The editor is C++20; existing project/runtime modules remain C++17. HLSL and generated shader headers compile into the native viewport. Windows SDK DXC compiles inline ray queries at `cs_6_5`. Python is a build/test tool, not an installed editor runtime. MSVC's native C/C++ runtime and Windows/D3D12 are required. NGX headers/libraries are a native SDK dependency selected by `STARLIGHT_NGX_SDK`; the local default currently references the installed neighboring SDK. Optional RR DLL loading retains native 1:1 reconstruction and graceful fallback. No super resolution or frame generation backend is enabled.

## Verification results

| Validation | Result | Evidence |
|---|---|---|
| Clean native Debug and Release compilation, subsequent regression builds | Pass | ninja-debug.log, ninja-release.log |
| Native core behavior | 468 assertions pass | out/native/checks/result.json |
| Native physics/session behavior | 10 checks pass per configuration | out/native/session-checks/Debug and Release/session-results.json |
| Native crash route | Controlled child exception writes a valid MDMP to Diagnostics/NativeEditor | out/native/diagnostic-checks/Debug and Release/diagnostics.json |
| Managed artifact/runtime scan | No violations; 1,337 PE images inspected in repository including generated/historical native output | no-managed-results.json |
| Delivered PE headers/imports | Debug and Release are x64, zero CLR header, no CLR-host imports | validation-debug/native-pe.json, validation-release/native-pe.json |
| Live editor modules | No coreclr/clr/mscoree/hostfxr/hostpolicy loaded in the inspected native session | Manual process-module inspection |
| Existing scene compatibility | 29/30 exact decoded scene matches; remaining scene lacks an ID, so both implementations generate a different valid GUID | parity/scene-parity.json and paired results |
| Seeded geometry | Topology, sums and sampled vertices match exported managed fixtures | geometry-reference.json and native-core assertions |
| GLB validation | Valid triangle plus fractional count/type, overflowing offset, invalid stride and cyclic node rejection | Native core assertions, fixtures |
| Native renderer modes | All 9 cases pass at requested 960×540 native dimensions | validation-release/render-matrix.json |
| Actual editor UI | Prop/gizmo movement, Save As, accepted-path history, cancel-only history across restart, project and preset loading exercised successfully | ui-validation.json; ui-native-save.starscene |
| Rebuilt native project modules | Empty and GameProject launch, exit 0 and write reports using isolated 640×360 native presets | validation-release/project-launches.json |

The 468 assertions include repeated per-vertex comparisons; they are not 468 distinct subsystems. The former managed test suite is archived. These are new native tests, not a claim that every former WPF-specific test was reproduced.

### Renderer matrix

| Case | Active tier | RR active | Result |
|---|---:|---|---|
| Raster | 0 | No | Pass |
| RT shadows | 1 | No | Pass |
| RT AO/reflections | 2 | No | Pass |
| RT hybrid | 3 | No | Pass |
| Reference path tier, four bounces | 4 | No | Pass |
| RR automatic | 3 | Yes | Pass |
| RR disabled | 3 | No | Pass |
| DXR disabled | 0 | No | Pass |
| Missing RR directory | 3 | No | Pass |

These hidden-window runs are deliberately focus-throttled to approximately 20 FPS. They validate native dimensions and functional mode/fallback behavior. Their FPS is not a throughput measurement, and they establish no 60/120 FPS or 4K performance guarantee. The renderer implementation itself was reused, not replaced with a new ray tracer.

### File handling and diagnostics fixes during translation

`IFileDialogEvents::OnFolderChange` records the last browsed folder even if the dialog is canceled. Accepted full paths additionally record their parent directory. History is persisted in `%LOCALAPPDATA%/Starlight/native-dialog-folders.json`. A fresh native session reopened the canceled-to folder successfully. This also removes the former WPF mixed-separator `ShellUtil` path entirely.

The editor installs a native unhandled exception handler and writes minidumps under `%LOCALAPPDATA%/Starlight/Diagnostics/NativeEditor`. The project host retains `Diagnostics/Engine`. The launcher no longer uses any DOTNET dump variables. Shutdown telemetry records rendered width, frames, GPU/ray/denoise time, tier and RR state as native JSON. Global user environment settings were not changed.

## Limits and remaining engineering risks

- This migration removes managed dependencies; it does not certify that every engine subsystem is bug-free. Forty same-viewport scene replacements measured bounded private-memory growth and one additional process handle after warm-up, rather than exact zero growth. See `lifecycle-memory.json` for both configurations. No unbounded scene-switch growth was seen in this bounded test.
- The prior minimal DirectX reproduction still shows two OS handles per swap-chain create/destroy. Native resize currently recreates the viewport, so this remains a known concern. Its cause is not proven to be Starlight, the driver, Windows or an overlay; no claim of zero leaks is made.
- Device loss, allocation failure and multi-hour workloads have not been exhaustively fault-injected. The existing native renderer's fence-wait behavior remains a risk. Error reporting does not provide automatic renderer-device recovery.
- Imported geometry is static preview geometry. Skeletal animation, full texture/material ingestion, additive scenes, custom docking/multi-monitor panel layouts and advanced scripted entities were not implemented by the prior editor and were not invented as part of the migration.
- The native UI replaces the old glass styling, overlay feature previews and edge-dimming behavior. Panels use native controls; scene-key and enum-like fields accept explicit values. A visual design/polish pass remains worthwhile, but there is no managed fallback UI in the active application.
- Native scene/geometry rebuilding during interactive edits is still coarse; undo copies are not a bounded delta store, and imported geometry can be reread. Further profiling is needed before high-density production-use claims.
- The build currently targets this Windows/x64 D3D12 environment and uses the installed NGX SDK path. A redistributable SDK/package arrangement and non-Windows renderer are separate work.

## Reproduce checks

```powershell
python Build-Native.py --config Debug --test
python Build-Native.py --config Release --test
python migration/validate_native.py --config Release
python migration/validate_native.py --config Debug --pe-only
python migration/verify_no_managed.py
```

Do not rerun the retirement script: it intentionally refuses to overwrite the existing recovery archive. Initial discovery is historical; rerunning `discover.py` would replace the original inventory. Use `verify_no_managed.py` for the current-state guard.
