# Starlight Engine — native C++

The active editor, project host, D3D12 renderer and Jolt physics use native C++. No .NET, CLR, WPF, C++/CLI, P/Invoke or NuGet is required.

## Build and launch

```powershell
python Build-Native.py --config Debug --test
python Build-Native.py --config Release --test
```

Run `Launch-Starlight-Editor.cmd`, which starts `out/classic/Release/Starlight.NativeEditor.exe`. To update that launcher build, use `python Build-Native.py --config Release --build-dir out/classic`. The default build preset instead targets `out/native`; project modules are built beneath the chosen build directory.

Requirements: Windows x64, Visual Studio C++ tools, Windows SDK/DXC 10.0.26100.0, CMake and Python. The bootstrap discovers Ninja from Visual Studio. Native NGX SDK headers/libraries are configured through `STARLIGHT_NGX_SDK`; the current default references the installed neighboring SDK. Python is used only for build/test orchestration and shader generation.

CMake/Ninja is the unified build. The older native build and editor-check entry points delegate to it. In an initialized x64 C++ developer shell, use `cmake --preset native`, `cmake --build --preset native-release`, and `ctest --preset native-release` directly.

## Editing

- Add props from the shape list. Move/Rotate/Scale expose handles; Object properties provide exact transforms, surfaces, lights and physics.
- Terrain and Sculpt modes use the brush selection and Camera & brush settings. Right mouse reverses raise/sculpt strokes. The Terrain menu adds water/lava and stamps selected shapes.
- Route draws paths. Play/Pause/Stop, Repeat and Follow control the preview.
- Middle drag turns the camera; wheel zooms; arrows and viewport edges pan while the viewport has focus. Camera & brush provides speed and snapping controls.
- Ctrl+O/S/Z/Y/D open, save, undo, redo and duplicate. F11 changes full-screen mode. Music supports multiple tracks, next and volume controls.
- Projects opens `.star` projects and `.startest` presets. Editor scenes use `.starscene`.
- Save and reuse shapes/sculpts individually or as groups; Duplicate/Delete act on the current selection. Replace imported models without losing their transforms or routes. See [reusable objects and groups](docs/REUSABLE-OBJECTS.md).
- Browse asset folders with name and type filters, then insert models, saved objects, groups, or scene objects. Copy/Paste selected objects across scenes within the editor session; Focus frames a selected group together.
- Export entire scenes or selected objects with their embedded GLB models, then add them to another scene without replacing its world settings. See [portable scenes](docs/PORTABLE-SCENES.md) for integrity checking and command automation. This is editor content portability, not standalone game packaging.

The native interface restores the earlier canvas-first design: Starlight branding, dark blue/purple gradients, rounded floating panels, corner menus, a floating build dock and purple playback controls. The implementation stays native C++. Custom docking and several previously planned engine features remain unimplemented.

## Technical record

See [the native migration report](migration/NATIVE-MIGRATION.md) for architecture, module mapping, validation evidence and known limits. See [the local AI interface](editor/AI-INTERFACE.md) for JSON command operations.

Retired managed sources/binaries and the previous working editor are preserved outside the repository at `D:/projects/_recovery/starlight-managed-retired-20260917`. User scenes/assets and sibling repositories were not modified. Older specification/audit documents describe historical releases; the native migration report supersedes their build and launch instructions.


## Tool organization

Starlight-owned tools are categorized under [tools](../../tools/README.md). The [tool map](../../TOOLS-MAP-2026-09-23.md) records their purpose, outputs and integration gaps. Root build and launcher commands remain compatibility entry points.
