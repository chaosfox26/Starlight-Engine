# Native interface restoration

2026-09-17. ChaosFox's requirement: preserve the old Starlight editor's distinct appearance during the native C++ migration.

## Reference actually inspected

Opened the archived working application at `D:/projects/_recovery/starlight-managed-retired-20260917/editor/bin/audit-release/Starlight.Editor.exe`. Inspected its live canvas, header, Builder popup, Shapes dock, Sculpting dock and playback controls. Cross-checked `App.xaml`, `MainWindow.xaml`, `BuildDock.cs`, `NatureGeometry.cs`, `SculptTools.cs`, `WindowTheme.cs` and `FullScreenCanvas.cs` in the same archive. This supersedes the initial approximation based only on source and prior screenshots.

## Implemented in native C++

- Original star icon in the title bar and header, semibold STARLIGHT branding, compact hamburger and four corner buttons.
- Original three-stop navy / near-black / purple background, smaller control typography, dark rounded buttons, cyan hover and pink selection accents.
- Canvas remains full width when menus, scene list or settings overlays open.
- Centered rounded pill dock with original vector artwork translated to GDI+ paths: cube, rectangle, trapezoid, octagon, Move, Rotate, Scale; terrain brush/stamp/water icons.
- Separate Nature dock: Rock, Boulder, Pebbles, Grass tuft, Chaos Lightning, Chaos Rock, Light-Bound Shard, Craxis Smoke.
- Sculpt dock: Pull, Push, Smooth, Flatten and native radius/strength sliders, Undo/Redo/Close.
- Dock shares the bottom row with playback when space permits and moves above playback when they would overlap.
- Small playback panel: Repeat, status, Reset, purple Play/Pause toggle. Character Follow is available in the Characters overlay; route drawing and selection remain accessible under More.
- Smaller live FPS/GPU telemetry; no invented FPS cap or performance claim.
- Native child backgrounds follow their parent's gradient, avoiding opaque rectangles behind labels and rounded controls. Tooltips and native button/checkbox/slider accessibility remain available.
- Borderless full-screen viewport uses the whole client area with controls overlaid.

Implementation: `editor/cpp/editor_main.cpp`, `editor_style.hpp`, `editor_icons.hpp`. Native Win32/GDI+ controls, C++ rendering and simulation remain active; no managed runtime restored. Launchers still target `out/native/Release/Starlight.NativeEditor.exe`.

## Validation and scope

Debug and Release builds and all four CTest suites pass; logs: `interface-style-debug.log`, `interface-style-build.log`. Actual UI checks exercised Builder switching, cube placement through the icon dock, saving an isolated `original-dock-check.starscene`, Nature labels and sculpt radius slider input. The running application was visually compared with the archived editor.

Preview: `native-interface-restored.png`.

This restores the original main visual language and tool layout. It does not claim pixel-identical WPF transparency/animation or identical arrangement of every detailed inspector. Existing native settings remain in overlays. Underlying renderer, physics and project behavior were not redesigned in this appearance pass. Terrain pour buttons retain the native editor's camera-centered placement semantics.

The pre-restoration native UI source is backed up at `D:/projects/_recovery/starlight-native-ui-before-style-20260917/editor_main.cpp`. The archived original WPF application is reference material, not the product launch target.
