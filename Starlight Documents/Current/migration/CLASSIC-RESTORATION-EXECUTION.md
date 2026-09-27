# Classic native interface — execution pass

This pass implements the core layout and command changes from `OLD-INTERFACE-SPECIFICATION.md`. It retains the pure native C++ product. It is not a claim of complete WPF parity or of a completed texture-library replacement.

## Implemented

- The canvas keeps its full dimensions while floating menus and settings open. Existing star branding, palette, corner buttons, vector builder docks and compact playback strip remain.
- Characters has a starter card, Place character, Import character, Character settings, Advanced, Follow, Move character, Draw route and the scene list. Import chooses/validates the next GLB; Place creates an instance. The bundled fox is selected when present.
- Beginner character settings show name, speed and size, plus route/removal controls. Advanced reveals exact placement, expandable Build and Body controls. Lightning target/branch fields and light controls appear only for relevant objects. Hidden fields are preserved when applying simple edits.
- Terrain and Shapes gear buttons open separate settings above their building bar. Terrain owns brush size/strength, named surfaces, shape stamping, pour placement/removal and still-water controls. Shapes owns snapping and camera views. Camera pan/tilt speed controls have their own small panel.
- Terrain and Sculpt keep separate brush sizes and strengths, with classic defaults. Paint selection persists when other menus are opened.
- Terrain surfaces, object surface categories and ray tracing quality use names rather than technical indices. Internal IDs remain unchanged for compatibility.
- Water/lava commands arm a ground-click placement operation. Placement creates terrain when needed, disables the still-water plane and participates in undo/redo. Escape cancels an armed pour.
- Draw Route switches out of sculpt/terrain interaction, ending the prior stroke. The old sculpt-dock interception bug is not retained.
- Main menu no longer duplicates Full screen; atmosphere/RT and camera settings are reachable there. Scene rename remains available in scene settings.
- Dirty-scene confirmation offers Save, Discard and Cancel. Canceling a Save As operation aborts closing/replacement.
- Branding is hidden in fullscreen. Dynamic form resizing repaints the panel; sliders and exact advanced values synchronize when the sliders move.
- Dynamic menu/dock/form controls retire their tooltip registrations before window destruction. The regression test checks active registrations as well as GDI and USER counts.

## Native files

| File | Role |
|---|---|
| `editor/cpp/editor_main.cpp` | Real Win32 controls, event routing, native scene commands, layout and lifetime changes |
| `editor/cpp/classic_ui_checks.inl` | Exercises actual controls and commands on an isolated scene; runs menu/form churn |
| `editor/cpp/native_session.hpp/.cpp` | Can park between frames for an isolated UI lifetime check, then resume; stop wakes the worker before join |
| `editor/cpp/texture_checks.cpp` | DDS validation and compressed GPU upload/readback checks |
| `deep/engine/model/texture.h/.cpp` | BC5/BC7 format-aware images, DDS mip selection and validated upload |
| `deep/projects/GameProject/project.cpp` | Runtime texture SRVs respect the actual resource format |
| `deep/engine/render_device.h` | Runtime model shader reconstructs normal Z for BC5 |
| `Build-Native.py`, `CMakeLists.txt` | Isolated output option and native UI/texture CTest integration |

The pre-change UI source is retained in `editor/cpp/editor_main.cpp.before-classic`; the initial translation script is `migration/restore_classic_controls.py`. The script is historical, one-shot and not a build step; subsequent reviewed fixes are in the C++ source.

## Validation and evidence

The six CTest suites cover core scene behavior, controlled diagnostic crashes, the no-managed guard, textures, classic UI and native sessions. Final per-configuration evidence is stored under `out/classic/Testing/Temporary/`, `out/classic/classic-ui-Release.json`, `out/classic/classic-ui-Debug.json` and the two `migration/classic-*-build.log` files. Read the final results rather than assuming every intermediate run passed.

The UI harness checks beginner/advanced visibility, hidden-property preservation, task-specific gear panels, paint persistence, click-to-pour, undo/redo, route switching and stable canvas dimensions. Its 80-cycle lifetime test recreates docks, menus and forms and compares GDI objects, USER objects and active tooltip registrations. This specifically tests UI churn; it does not certify all renderer handle lifetimes or a long-duration memory soak.

Live inspection opened a separate native editor, placed the starter character, opened beginner/advanced settings and saved `classic-character-check.starscene`. `classic-advanced.jpg` captures that work. That test instance was then closed. The pre-existing user editor was not closed, restarted or assigned a test scene by this work. Tests and builds use separate output paths.

The strengthened tooltip test exposed an intermediate failure: 52 registrations grew to 1,252 over 80 cycles. Retiring the tooltip control's enumerated stored identity corrected the growth (22 before/after). The test now waits for rendered frames before taking its baseline and pumps the Windows message queue between cycles. Initial device/Windows startup otherwise changes the process-wide USER count during the test.

Further runs showed a small process-wide USER-count variation (99–101) despite identical before/after inventories of every owned window class and stable GDI/tooltip counts. The precise non-window USER object behind that variation is not identified. The final churn check therefore records all 80 samples and compares high-water marks of two equal 40-cycle batches, with no increasing allowance. It separately requires identical owned-window inventories, no retained GDI growth and an exactly equal, nonzero tooltip count. This is a bounded-growth regression check, not proof of zero leaks throughout Windows, the driver or the renderer.

For scope isolation, the final UI churn measurement parks the native render worker between frames, keeping its device and resources alive. It verifies the frame counter stays fixed, then resumes rendering and checks frame progress. Functional UI commands are exercised before parking, and the separate native-session suite exercises live rendering. Release's final UI sample has GDI 27→27, USER 99→99 (both batch peaks 100), tooltips 22→22 and identical window inventories. All 19 UI checks pass. This separation does not dismiss the small variation observed with concurrent frame presentation.

Do not run live desktop automation against another editor instance during the lifetime test. One such Debug run recorded a USER high-water increase (106→108) while its owned-window inventory and GDI/tooltip counts stayed unchanged; that failed sample is retained as `classic-ui-concurrent-inspection.json`. A clean isolated rerun is required. The result is not evidence that every possible OS/UI-automation interaction is leak-free.

## Remaining classic restoration work

1. Restore the separate windowed game preview; current native playback is still inside the editor viewport.
2. Restore the full More catalogue and dedicated music-player layout; the existing native music commands remain available.
3. Complete the old project/preset modal layout and all remaining named color controls; current project commands retain the existing native panel.
4. Starter-card drag placement, horizontal overflow behavior for all dock widths, keyboard tab traversal, multiple-DPI/minimum-size walkthroughs and exact WPF visual polish remain to be completed.
5. Fullscreen Escape should distinguish canceling an active operation from exiting fullscreen; the existing cancellation path was preserved, not fully rewritten here.

## Texture and competitive follow-through

`TEXTURE-PIPELINE-AUDIT.md` separates the implemented compressed loader from the still-unimplemented editor PBR material library and streaming manager. Default procedural assets have not been relabeled as 2K/4K PBR assets.

`COMPETITIVE-GAP-ANALYSIS.md` compares current architecture, rendering, authoring, packaging and profiling with official Unreal/Unity documentation and gives prioritized acceptance gates. No comparative engine speedup or new native-4K performance claim is made by this interface pass.
