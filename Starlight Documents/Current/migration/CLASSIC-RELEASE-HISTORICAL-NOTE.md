## Starlight classic UI execution — 2026-09-17 (LATEST)

Core classic workflow is implemented in native C++, not just documented: starter character card/Place/Import selection, Beginner/Advanced disclosure with Build/Body sections, conditional lightning/light fields, task-specific Terrain/Shapes gear forms above the dock, named surfaces/RT tiers, camera speed sliders, separate Terrain/Sculpt brush settings, ground-click pours with undo, Save/Discard/Cancel, corrected tooltip registration lifetime. Full WPF parity is NOT complete: separate windowed preview, full More catalogue/music layout, project modal polish, drag placement/DPI/overflow work remain.

Current launch target is D:/projects/Starlight-Engine/out/classic/Release/Starlight.NativeEditor.exe. Root Launch-Starlight-Editor.cmd and the desktop Starlight Editor shortcut point there; .before-classic backups retained. Original out/native builds remain intact. The pre-existing user editor was not closed/restarted or assigned a test scene by this task; it was no longer running in later process checks. Our isolated character scene was saved as migration/classic-character-check.starscene before closing its test window. Always inspect fresh visible/process state before closing any editor.

Debug and Release six-suite native CTest passes; 19 UI checks, 34 texture checks. Evidence: migration/classic-final-validation.json, classic-*-build.log, out/classic/classic-ui-{Debug,Release}.json. UI lifetime checks park the render worker and then verify resume; window inventories and tooltip counts remain stable, USER/GDI high-water marks do not grow across the two measured batches. This does not certify universal leak freedom: a small non-window USER variation under concurrent frame/UI automation remains unattributed; failed concurrent-inspection sample preserved. Existing renderer-wide device-loss/long-soak limitations remain.

Texture foundation: validated BC5/BC7 DX10 DDS loading, load-time mip selection, format-aware D3D12 uploads/SRVs, BC5 normal-Z reconstruction, GPU block readback checks. The editor defaults still use procedural shading. No photographic 2K/4K albedo/normal/ORM library, complete editor PBR material path, or asynchronous residency/eviction manager was shipped. Do not call that asset phase complete.

Reports: migration/CLASSIC-RESTORATION-EXECUTION.md, TEXTURE-PIPELINE-AUDIT.md, COMPETITIVE-GAP-ANALYSIS.md (official Unreal/Unity sources, prioritized roadmap). Visuals: migration/classic-advanced.jpg and classic-terrain-settings.jpg. No new 4K FPS or Unreal/Unity speedup claim. Continue from the explicit remaining checklist while preserving the beginner-friendly classic interface and native-resolution/no-frame-generation mandate.

---

