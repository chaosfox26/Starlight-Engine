# Starlight original editor — interface and behavior specification

**Reference date: September 17, 2026. Purpose: preserve Chaos's familiar, simple editor while rebuilding its interface in native C++.**

This document records the original interface, its code paths, live observations, and differences in the current C++ editor. It does not authorize restoring a managed runtime, redesigning the engine, or implementing the historical future-feature catalogue. Chaos's current requirements take precedence over old comments, labels, and prompts.

## 1. The experience to preserve

The original editor is a world canvas with a small number of temporary controls. Its default workflow is **place a character, draw a route, press Play**. Builder tools occupy a rounded floating bar. Detailed settings open beside the task that needs them. The world does not shrink when a menu opens.

The character settings have **Beginner and Advanced modes**. Beginner presents a name and two sliders; advanced exposes exact numbers. Build and rigid-body details remain collapsible. A native implementation that exposes every object property in one long form loses this important separation even if all the underlying functions exist.

Preserve these relationships:

- Characters owns placement, importing, selection, movement, route drawing, and character settings.
- Builder owns Terrain, Shapes, Nature, Sculpting, and hiding the building bar.
- The terrain gear opens terrain settings above the terrain bar; the shapes gear opens snapping, floor color, and view shortcuts above the shapes bar.
- Environment owns placing and adjusting fire, lava pools, and local lights.
- The hamburger owns scene files, projects/presets, scene framing, atmosphere, ray tracing, and camera speed.
- More owns the local music player and an explicitly labelled future-tools catalogue.
- Repeat, state, Reset, and Play/Pause form one compact playback panel.

Do not equate a familiar palette with complete interface parity. Placement, grouping, disclosure, labels, defaults, shortcuts, and editing/playback transitions are part of the design.

## 2. Evidence and scope

### Authoritative reference

- Archived source root: `D:/projects/_recovery/starlight-managed-retired-20260917/editor/`.
- Application actually opened: `D:/projects/_recovery/starlight-managed-retired-20260917/editor/bin/audit-release/Starlight.Editor.exe`.
- Current native implementation compared: `D:/projects/Starlight-Engine/editor/cpp/editor_main.cpp`, `editor_style.hpp`, and `editor_icons.hpp`.
- Current product executable remains `D:/projects/Starlight-Engine/out/native/Release/Starlight.NativeEditor.exe`.

The reference analysis covers the 47 authored top-level C# files and both XAML files. UI markup, runtime control constructors, handlers, input routing, persistence, native presentation, and playback were examined. Supporting model/geometry/flow/import code was inspected to distinguish actual behavior from stale UI text. Validation sources were inventoried; their historical assertions are not fresh test results. Generated `obj`/`bin` code and older backup variants are not additional current interface designs.

### Companion evidence

- [Complete control, source, and catalogue inventory](D:/projects/Starlight-Engine/Starlight Documents/Current/migration/OLD-INTERFACE-CONTROLS.md): 130 XAML declarations, 56 resolved event bindings, 49 source hashes, all 20 catalogue detail records, and dynamic control constructor references.
- [Visual reference gallery](D:/projects/Starlight-Engine/migration/old-interface-reference/index.html): actual application captures, including detached popup images.
- [Machine-readable inventory](D:/projects/Starlight-Engine/migration/old-interface-reference/source-inventory.json).
- [Earlier native appearance pass](D:/projects/Starlight-Engine/Starlight Documents/Current/migration/INTERFACE-RESTORATION.md): historical partial restoration, superseded by this more complete parity assessment.

**Evidence terminology:** “Live” means opened/observed in the running original; “source” means traced implementation. Every control is mapped, but this documentation pass is not an assertion that every destructive action, file type, numeric boundary, project launch, or shader mode was exercised. No engine performance certification follows from FPS text visible in screenshots.

Popup captures matter: a WPF popup may extend beyond the main window. The whole-window screenshot then crops it, while a companion image captures the complete popup. This alone is not evidence of screen clipping. Some captures immediately after a transition show a transient position; settled captures take precedence. Accessibility trees may lag one action and can include controls hidden behind the native viewport.

### Key visual references

Original Characters menu:

![Original Characters menu](D:/projects/Starlight-Engine/migration/old-interface-reference/characters-2.jpg)

Original Beginner settings:

![Original Beginner settings](D:/projects/Starlight-Engine/migration/old-interface-reference/beginner-settings-2.jpg)

Original shape settings anchored above the bar:

![Original shape settings](D:/projects/Starlight-Engine/migration/old-interface-reference/shape-settings-3.jpg)

## 3. Visual construction

### Window and canvas

| Element | Original specification |
|---|---|
| Main window | Title “Starlight Engine Editor”; nominal 1380 × 900; minimum 1050 × 720; Segoe UI; star icon |
| Root | Margin 14; diagonal three-stop gradient `#14213B` at 0, `#090D18` at .45, `#24132E` at 1 |
| Header | 48-high row, bottom margin 8; hamburger, star image, STARLIGHT, scene name at left |
| Right header | Builder ▲, Characters ♟, Environment ☀, More ◇, in that runtime order |
| World | Full available canvas; `#0B1121`; corner radius 16; thin border; grid and selected-object marker |
| Footer | 28-high row; live native FPS/GPU/cap/resolution text left; “Native scene builder · Walking animation pending” right |
| General ink | `#EFF2FA`; secondary text is subdued blue-grey; headings use cyan or pink |

The gradient is part of the surrounding shell, not an extra viewport tint. Popup panels use translucent dark navy/purple surfaces rather than permanent opaque side columns.

### Shared controls and panels

| Style | Values and states |
|---|---|
| Menu surface | Rounded 12; padding 20; border `#778A96B6`; margin `0,8,0,0` |
| Panel gradient | `#EE252C45` → `#E513192A` at .48 → `#ED1C1830` (ARGB) |
| Button | Background `#252B43`; border `#3F4865`; radius 7; margin 4; padding `16,9`; hover border `#62B3FF`; disabled opacity .4 |
| Menu toggle | Radius 7; padding `10,5`; margin 4; hover cyan `#76C9FF`; checked pink `#E776E9` |
| Text box | Background `#0C101A`; pale text; border `#3F4865`; padding 8; margin `0,4,0,12` |
| List | Background `#121522`; no surrounding border; item padding 12 |
| Tooltip | Dark `#F0212940`; border `#7A97BD`; padding `12,8`; wrapped max width 360; button initial delay 1000 ms |
| Dock | Radius 32; padding `12,8`; border ARGB(190,126,153,199); vertical gradient ARGB(248,41,51,77) → ARGB(247,22,20,37) |
| Dock icon | Button 40 × 38, margin 2, padding 3; vector artwork 22 × 22; stroke 1.8, `#BFD9F4`, rounded joins |
| Selected tool | Pink border and dark magenta fill, distinct from cyan hover |

The original combo boxes and the two toggles inside Characters sometimes retain the default light WPF theme, yielding poor contrast. This is a captured old defect, not a requirement to recreate unreadable controls.

`App.xaml`, `MainWindow.xaml`, `WindowTheme.cs`, and `BuildDock.cs` are the primary styling sources. Icon paths are actual geometry strings in `BuildDock.cs`; use those rather than substituting unrelated Unicode symbols.

## 4. Modes and layout state

### Beginner versus Advanced

| State | What changes |
|---|---|
| Beginner | Exact Values section hidden; heading “BEGINNER · MAKE IT YOURS”; status describes place/draw/play |
| Advanced | X, Z, exact speed, exact size, Apply exact values and error text become visible; heading “ADVANCED · EXACT CONTROL” |

The two states use the same scene and canvas. The same menus, sliders, selection, routes, build tools, and playback remain available. Toggling the mode does not create another scene or alter object data.

`ChangeMode` switches `AdvancedFields.Visibility`. The Characters toggle initially says “Advanced controls”; after interaction it uses `A` or `A✓`. Preserve a clear mode indication, but the cryptic abbreviated label need not be treated as desirable usability.

Beginner still permits opening the collapsed “Build: height, size and floor” and “Rigid body” sections. Lightning properties appear only for lightning. Thus the exact original implementation is more nuanced than “Beginner has no technical settings.” The essential requirement is progressive disclosure.

### Windowed versus full-screen

These are separate from Beginner/Advanced.

Windowed mode has title bar, branding, margins, rounded canvas, and footer. Full-screen saves style, resize mode, bounds, and window state; uses `WindowStyle.None`, `ResizeMode.NoResize`, maximized bounds, zero root margin, and zero header/footer rows. The canvas loses its border/radius. The header's branding disappears, but separate corner popup hosts retain the menu buttons. Playback and the active tool dock remain over the canvas. Exiting restores the saved window.

`F11` toggles full-screen. `Escape` exits full-screen, except an active sculpt stroke handles Escape first by cancelling that stroke. The hamburger label changes between Enter full screen and Exit full screen.

Current source keeps corner controls visible through `TrackFullScreenMouse` and `UpdateChrome`; retained fade helpers and old tests do not establish active edge-autohiding. The live captures confirmed visible corner controls. Edge navigation has a separate 48-pixel dark gradient indicator; it is click-through and nonactivating.

### Popup and dock placement

- Opening a menu does not resize the canvas.
- Only the selected Builder section fills the floating bar; choosing its active section again can hide it.
- Bar is centered near the bottom, with an 18-pixel bottom offset when it fits alongside playback. If their bounds overlap, it moves above playback using playback height plus 30 pixels.
- Horizontal scrolling handles a bar wider than available space; maximum content width 1100, additionally limited by canvas width.
- Terrain/shape settings are anchored above their own gear, with vertical offset −10.
- Deactivation closes floating tool/settings hosts; pointer interactions end so navigation and strokes do not remain stuck.
- Runtime native hosts solve WPF `HwndHost` airspace ordering. Declared Hint/PreviewBadge text is not proof it visibly overlays the native child.

## 5. Complete menu and button map

### 5.1 Hamburger / Main menu

Width 250; scroll content capped at 620. Order:

| Control | Action / semantics |
|---|---|
| Enter/Exit full screen | Switch display layout; preserve scene and restore window bounds on exit |
| New scene | Stop interactions, offer save/discard/cancel if dirty, reset scene/path/watcher/selection/history |
| Open scene… | Same dirty-scene protection; `.starscene` picker; decode and preload object models before replacing current scene |
| Save scene | Stop/reset preview, commit pending name, choose path only if unsaved; save with stale-file protection and backups |
| Projects & presets… | Open separate modal project/preset window |
| Frame world | Reset camera focus to origin, yaw 0, pitch .67, distance 19 |
| Time of day | Slider 0–24; scene default 15 |
| Run day/night cycle | Scene toggle; default false |
| Day duration | Text seconds, default 180; validated 10–36000 |
| Fog density | Slider 0–.3; default .018 |
| Apply atmosphere | Validate, remember edit, apply scene atmosphere, refresh |
| Enable DXR 1.1 | Enable preference; default false for new scenes |
| Raytracing Quality | Off; Low: shadows; Medium: AO/reflections; High: hybrid; Path tracing (tiers 0–4) |
| Path bounces | Stepped slider 2–4; default 3 |
| Freeze reference animation | Reference-render animation preference, distinct from Play/Pause |
| Ray Reconstruction | Default checked preference; actual use depends on tracing/support/runtime success |
| Apply raytracing | Apply enabled/tier/bounces/freeze/RR together; preserve playback clock |
| RT status | Capability, active tier, ray time, AS time, samples, native denoiser time, RR status |
| Camera travel speed | Slider .25–6, default 1; multiplier display |
| Camera tilt / turn speed | Slider .2–4, default 1; multiplier display |
| Arrow keys move camera | Opt-in, initially unchecked; disabled by typing/selector/slider focus conditions |

Atmosphere and RT are under this menu in the original; they are not part of the small Environment effect form. RT preferences are not a guarantee that the feature is active; actual status is essential. Native-resolution RR is distinct from old project-preset upscaling controls.

### 5.2 Builder

Width 245. Buttons in order: **Terrain, Shapes, Nature, Sculpting, Hide building bar**. The first four select the bar's contents; Hide returns space to the canvas. They are not persistent side-panel tabs.

#### Terrain bar

Ordered icons: **Raise/lower, Smooth, Flatten, Paint, Hill stamp, Plateau stamp, Crater stamp, Water pour, Lava pour, Still water/lava settings**, then **Undo, Redo, Settings, Close**.

| Tool | Interaction |
|---|---|
| Raise/lower | Hold left to raise, right to lower; radial falloff; repeated stroke updates |
| Smooth | Soften neighbouring height samples under brush |
| Flatten | Capture starting height and move affected samples toward it |
| Paint | Assign chosen surface type under brush |
| Hill | Single-click rounded raised stamp |
| Plateau | Single-click flattened raised stamp |
| Crater | Single-click rounded depression |
| Water/Lava pour | Arm source-placement tool, then click ground at intended position; Play emits liquid |
| Still water/lava | Open settings; height determines fill below that plane |
| Undo/Redo | Scene history; continuous brush operation is one undo unit |
| Gear | Toggle terrain settings immediately above bar |
| Close | Hide bar and terrain settings; clear active terrain tool |

Terrain settings width 310, scroll cap 570:

1. Active-tool/help label.
2. Brush size .4–5 metres, default 2.
3. Brush strength .2–4, default 1.
4. Material selector: Detailed grass, Rich earth, Sand, Weathered stone, Snow white.
5. Stamp selected shape: copy selected building shape into terrain, preserving the original object.
6. Flowing liquids section: Place water pour, Place lava pour, Remove all pours, explanatory text.
7. Still water/lava section: Use lava instead of water, Show water, water height (default −.5, validated ±20), Apply water level.
8. Ground-size/save/undo help.

Actual data is a 97 × 97 single heightfield over 24 × 24 metres at .25 m spacing, with a denser 193 × 193 rendered surface. Heights are bounded ±20. Paint is a material-index field; displayed colors/kinds are interpolated, with native procedural surface detail. No texture-file browser or resolution dropdown exists here.

Pour source count is at most 32; default source volume is 2 and it emits over three seconds. Selecting a pour does not place it at the camera automatically. A click creates terrain if necessary and hides the still-water plane. Flow is a 48 × 48 surface transport grid with object masks, not unrestricted 3D fluid simulation.

#### Shapes bar

Ordered icons: **Cube, Rectangular block, Trapezoid, Eight-sided column, Move, Rotate, Scale, Undo, Redo, Settings, Close**.

Shape buttons create/select a primitive near the camera focus. Transform buttons choose the colored gizmo behavior. Move drags axes; Rotate changes the selected axis; Scale changes dimensions. Selection and routes are preserved appropriately by transform operations.

Shape settings width 270, scroll cap 570:

- Snap to grid (0.5 m).
- Snap to ground.
- Snap to nearby objects (implementation threshold .25 m).
- Floor color: Dark blue `#182033`, Black `#050505`, Purple `#592D80`, Pink `#C65394`, Green `#356F45`, Sand `#B29B6D`.
- Camera views: Side, Above, Low.
- Focus selected object.
- Scroll-to-zoom and middle-drag hint.

These are task-specific settings, not an entrance to an unrelated universal inspector. View presets adjust camera pitch (Side 0, Above 1.48, Low −.06), with above-ground camera safeguards. Focus uses object bounds and distance clamping.

#### Nature bar

Ordered text buttons: **Rock, Boulder, Pebbles, Grass tuft, Chaos Lightning, Chaos Rock, Light-Bound Shard, ShadowEnemy Smoke**, then **Undo, Redo, Close**. There is no settings gear in this bar.

Buttons use 12-point text, width 86 or 126 for longer labels, height 38. Clicking adds a selectable object near the camera. It participates in the same transform workflow. Rock and Boulder default to dynamic bodies. Lightning's local endpoint and branch spread appear in its selected-object settings. Smoke uses a volume proxy. There is no density-based scatter UI or imported nature-asset browser in this menu.

Live verification placed Chaos Lightning and showed its glow, transform gizmo, scene-list entry, and conditional property controls.

#### Sculpting bar

**Pull, Push, Smooth, Flatten**, followed by two compact inline sliders labelled **Brush radius / strength**, then **Undo, Redo, Close**. No gear.

- Radius .08–3 m, default .45; strength .05–2, default .65.
- Left brush operates on the selected/hit mesh; right reverses Push/Pull.
- Escape cancels the active stroke and restores the preceding scene snapshot.
- Ordinary geometry, imported meshes, and rocks can be sculpted. Fire, light, lava, grass, lightning, and smoke are rejected with guidance.
- Initial mesh preparation welds coincident vertices and subdivides small meshes. Input above 80,000 triangles is refused with a message; this is not unlimited dynamic topology.
- Stroke timer is 35 ms. Coordinates are transformed between world and object space; Smooth uses mesh adjacency and Flatten uses the hit plane.

### 5.3 Characters

Width 260. Ordered content:

1. YOUR CHARACTERS heading.
2. Rounded starter asset card (`#263550`, radius 10, padding 14), cyan star, asset filename, “Drag onto the floor”. The reference asset label is `fox`; the live model is the existing fox-eared character asset.
3. + Place character.
4. Import character… (`.glb`).
5. Character settings toggle.
6. Advanced controls toggle.
7. Move character.
8. Draw character route.
9. IN YOUR SCENE list, maximum height 220.

Import validates the model and changes the current asset card; it does not itself place an actor. The Place button places at the default origin; dragging the card requests floor-position placement. Source routes both through `Place`. Live testing used the Place button, not asset-card drag/drop.

Draw Route closes the popup, arms route drawing, and lets a floor drag replace the selected object's route. Points are accepted after squared X/Z separation exceeds .025. Move drags the actor and translates its route with it. The scene list includes shapes and effects as well as imported characters; “Characters” was the original user-facing home, not a strict ECS category.

#### Character settings

Width 270; scroll cap 540. The popup is anchored under Characters and closes the asset menu when opened.

| Section | Controls and behavior |
|---|---|
| Always visible | Mode heading; Name (commit on lost focus); Walking speed slider .2–5, default 1.5 m/s, readout; Size slider .3–3, default 1 |
| Advanced only | Exact X/Z, speed and size in a two-column layout; Apply exact values; inline error text. Position ±12, speed .2–5, size .3–3 |
| Build expander | Object height Y; width/height/depth; yaw angle; Keep above floor; Apply changes |
| Rigid body expander | Simulate during Play; density kg/m³ (water = 1000); Apply body settings. Density validated 50–20000; invalid body types rejected |
| Lightning only | Local target X/Y/Z; branch spread 0–5; Apply lightning. Endpoints limited to ±30 |
| Bottom actions | Route-point count; Clear route; Remove character |

The build form validates a candidate object before replacing the selected item and clamps to the floor when requested. Errors are surfaced without introducing another navigation hierarchy. Existing expander/scroll state can remain when a different object is selected.

### 5.4 Environment

Width 310; scroll cap 560. **Add fire, Add lava pool, Add light**, followed by the effect editor:

- Color text, default `#FF651C`.
- Brightness 0–100, default 25.
- Reach .1–50 m, default 6.
- Cone 5–180 degrees, default 180 (point light).
- Apply selected effect and status/help.

Adding creates an ordinary selectable scene prop. The effect form updates the selected relevant object; terrain pours are a different system. The original bottom help incorrectly describes a three-light/no-shadow limitation despite the later 32-local-light/native lighting path.

### 5.5 More and music

More is a 740-wide popup, not another editing dock. It contains Music player, a pink COMING SOON heading, explanatory text, and a 370-high two-column catalogue (left list 260 wide, details to right). Selection shows title, status, description, “What needs to come first”, “How we will know it works”, Check availability, and a response.

All 20 entries, in order:

1. Native Play
2. Playback choices
3. Assets & reusable objects
4. Animation
5. Gameplay rules
6. Physics
7. Navigation
8. Materials & lighting
9. Terrain & large scenes
10. 2D games
11. Audio
12. Game screens & controls
13. Performance & debugging
14. Teamwork & history
15. Build & export
16. Windows, Linux & macOS
17. VR
18. Graphics technology updates
19. Steam & cloud saves
20. AI editor access

Every description/dependency/proof string is preserved in the companion inventory. Native Play, Playback choices, and Terrain & large scenes are labelled experimental; other entries are scaffolded/coming soon. `Available` is false for all discovery entries. Check availability explains `editor_only` or `not_implemented`; it does not run that subsystem. Broader Physics/AI/material roadmap entries do not negate narrower foundations already in the code.

Music opens a separate 280-wide popup with **YOUR MUSIC**, track status, **Choose songs…**, **Play/Pause, Next, Stop**, and a **volume slider 0–1, default .5**. Multi-select accepts mp3/wav/wma/m4a or all files. This is local listening, not authored game/spatial audio. No music file was played during the audit. The hidden Music header toggle remains the placement anchor, which produces an awkward old popup position; preserve the dedicated simple player, not that positioning quirk.

### 5.6 Projects & presets

Separate modal window: title “Projects & presets · Starlight”, nominal 630 × 810, minimum 560 × 600, centered on owner; dark RGB(13,17,29), scrollable content, margin 26. Heading “Choose your world”, 27-point, then “Your project supplies the scene. Your preset supplies the test settings.”

| Order | Field/action |
|---:|---|
| 1 | Project path/status, Open project… (`.star`) |
| 2 | Scene combo populated by project |
| 3 | Preset path/status, Load preset… (`.startest`) |
| 4 | Width, Height |
| 5 | Display: fullscreen/windowed/borderless |
| 6 | Maximum frames per second |
| 7 | Test duration in milliseconds |
| 8 | Historical Upscaling combo: off/dlss/fsr/xess/auto |
| 9 | Historical Quality combo: native/quality/balanced/performance/ultraperformance |
| 10 | Frame generation, VSync, Culling, Bring game to foreground |
| 11 | Save preset as… |
| 12 | Check settings / reconcile |
| 13 | Launch selected project |
| 14 | Inline status/error |

**Historical upscaling and frame-generation fields are recorded for accuracy, not restoration requirements. The current native-resolution/no-frame-generation mandate wins.**

Project selection validates schema/default scene and clears the preceding preset. Preset loading requires a project, makes a working copy, and invokes the launcher planning path to reconcile supported settings. Busy work disables controls/closing. A settings check has a 20-second timeout and does not launch a game. Save As writes a checked copy atomically with backup if replacing a file. Launch checks the draft and launches the chosen project/scene. Failure is inline and logged. Live empty-state Check settings produced “Load a preset first.” This pass did not launch existing user projects.

## 6. File handling, history, and error UX

The original uses shared `DialogHistory.Show` for OS file pickers, with purpose-specific directory keys and stable dialog GUIDs:

| Entry | Picker/filter | Purpose |
|---|---|---|
| Open/Save scene | `.starscene` | scenes |
| Import character | `.glb` | models |
| Choose songs | mp3/wav/wma/m4a/all; multiple | music |
| Open project | `.star` | projects |
| Load/Save preset | `.startest` | presets |

Directories are canonicalized; accepted selections update the per-purpose/last directory. The old version did not persist cancel-only browsing. The current native implementation improved this; do not regress it to reproduce an old defect.

Dirty New/Open/Close uses **Save your scene before continuing? Yes / No / Cancel**. Yes attempts Save and cancels the pending action if save fails/cancels. Save protects against an external modification using a stored SHA-256 and creates timestamped backups when replacing. File watchers reload clean scenes; dirty scenes receive a conflict message instead of silently losing edits.

Undo/Redo uses serialized `SceneData` snapshots, selection IDs, and stop/reset behavior. The ordinary `Remember` method clears redo and marks dirty. Pointer/brush code groups a drag/stroke rather than adding one entry per frame. Slider handlers can record multiple changes while dragging; do not confuse that with the stroke grouping contract.

Invalid exact values show inline error text; general import/open errors use a Starlight message box and status; project/preset errors use the modal's message area. Diagnostics in the archived editor route to `%LOCALAPPDATA%/Starlight/Diagnostics/Editor`. The native product's separate diagnostic path must remain native-specific.

## 7. Viewport interaction contract

| Input | Original behavior |
|---|---|
| Scroll | Native zoom distance bounded 2–60; fallback WPF-only path uses 5–28 |
| Middle drag | Orbit/look yaw and pitch, scaled by .006 × tilt multiplier; pitch clamped −1.4 to 1.5 |
| Screen edge | Pan within 14 DPI-scaled pixels of window border; distance-aware and elapsed-time-scaled |
| Arrow keys | Optional pan; blocked when editing a textbox/selector/slider and in modal/inactive states |
| Left object drag | Pick closest object bounds, move selected object/group, translate routes |
| Colored gizmo drag | Axis move/rotation/scale; selection group follows selected-object transform |
| Right drag | Box selection outside terrain/sculpt special handling; movement threshold before box appears |
| Route drag | Replace route from floor points; render a pink line |
| Ctrl+S | Save |
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| F11 | Toggle full-screen |
| Escape | Cancel active sculpt stroke first; otherwise exit full-screen |
| Focus loss / capture end | Stop drag, brush and navigation state |

Pan step is `distance * .6 * PanSpeed * elapsedSeconds`, normalized for diagonal input; elapsed time is capped .05 s. Camera panning is suppressed over tools, open menus, active drawing/strokes, gizmos, and modal interactions. Snapping applies grid, nearby-object, ground, then above-floor constraints.

Imported GLB is static geometry preview with normalization to 1.8 m height. The parser handles triangle positions/indices and base color factors, with size/accessor/node-depth checks; it does not create a skeletal animation editor or a full texture-material asset workflow.

## 8. Playback and authoring lifecycle

The floating panel contains **Repeat → state → Reset → purple Play/Pause**. Initially state is Ready. Playback reports Playing/time, Paused/time, or Finished. It is not a timeline editor or animation queue.

```text
Ready --Play--> Playing --Pause--> Paused --Play--> Playing
Playing --last route completes, no repeat/flow/physics--> Finished
Finished --Play--> Playing from beginning
Any preview state --Reset--> authored start; routes and pours retained
```

- Play requires a route, flow source, or active physics body. The no-content response asks for a route or pour.
- Route duration is the longest path length divided by speed. Repeat cycles route time and returns actors to their starts.
- Physics and flow can keep simulation running beyond route completion.
- Reset clears transient flow, restores physics start state, resets clock, and retains authored scene content.
- **Windowed Play creates a separate “Starlight — native scene playback” window**, with the scene and two bottom buttons: Play / Pause and Reset. Editor preview remains at authoring time while the game viewport runs.
- **Full-screen Play runs inside the canvas** when no separate game window already exists.
- Closing the game window pauses and asynchronously shuts down its native viewport; main editor remains available.
- Repeat is sampled when playback starts/resumes; it is not a universally immediate simulation setting.
- Walking/route motion is not evidence of imported skeletal animation; the footer explicitly notes pending walking animation work.

Live checks placed the starter character, drew a floor route, observed it move in the separate window, paused, reset it to the authored position, closed that preview, and then played/paused inside full-screen. A transient white surface at window creation resolved to a rendered scene on the next observation; do not represent the first capture as a persistent rendering failure.

## 9. Source architecture behind the interface

| Layer | Files / responsibilities |
|---|---|
| App/style | App.xaml, App.xaml.cs, WindowTheme.cs: resources, startup, global lifecycle, dark native title bar |
| Main window | MainWindow.xaml + .cs: visual declarations, selection, character workflows, scene lifecycle, mode changes |
| Runtime dock | BuildDock.cs: section switching, vector icons, placement, popup anchors, active states |
| Editing input | BuildingTools.cs, PointerInteraction.cs, ObjectSnapping.cs, WorldNavigation.cs, EdgeDimming.cs |
| Terrain | TerrainTools.cs, TerrainData.cs: brush lifecycle, samples, rendered mesh, still fill, stamping |
| Props/mesh | ObjectGeometry.cs, NatureGeometry.cs, ChaosTools.cs, EffectGeometry.cs, SurfaceDetail.cs |
| Sculpt | SculptTools.cs: mesh prep, ray hit, brush deformation, cancellation/history |
| Simulation | TerrainFlow.cs, FlowTools.cs, FlowObstacles.cs, PhysicsWorld.cs |
| Native presentation | NativeEditor.cs, NativeViewport.cs, NativeScene.cs: HWND host, native packet V7, render worker, scene/camera sync, game preview |
| Full-screen/focus | FullScreenCanvas.cs, ForegroundFocus.cs: saved bounds, corner hosts, foreground requests |
| Persistence | SceneData.cs, EditorHistory.cs, DialogHistory.cs, CharacterSettings.cs |
| Projects | ProjectWindow.cs: schema selection, draft/reconcile/launch, modal form |
| Secondary UI | LocalMusic.cs, FeaturePanels.cs, FeatureCatalog.cs |
| External commands | EditorCommands.cs: inspect/apply operations, expected-hash validation, backup, scene-watch integration |
| Diagnostics/tests | Diagnostics.cs and the Checks files listed in the source inventory |

```text
XAML + runtime dock constructors
        ↓ user event / native pointer message
MainWindow editing state (mode, selected IDs, open popup, active tool)
        ↓ validated operation + history boundary
SceneData / Character / TerrainData / sculpt mesh / liquid sources
        ↓ geometry/material construction
NativeScene packet + camera/time inputs
        ↓ NativeViewport worker / HWND
Native renderer and optional game-preview window
```

When translating, keep editor UI state separate from saved scene data and from transient simulation. Replace WPF mechanisms with native ownership and event routing while retaining user-facing relationships. `Popup` is an implementation detail; a canvas-preserving, properly anchored native overlay is the requirement. Do not import the original CLR/PInvoke architecture back into the product.

## 10. Current native parity gaps

Compared against the C++ source after the earlier style restoration. These are **remaining differences**, not changes completed by this documentation pass.

| Priority | Original | Current native difference | Restoration acceptance |
|---|---|---|---|
| P0 | Beginner/Advanced and collapsible details | No corresponding Beginner mode; long object form exposes transform, body, light, lightning and material IDs together | Default simple name/speed/size; optional exact values; collapsed Build/Body; relevant-only effect fields |
| P0 | Characters contains asset card, Place, Move, Draw Route and settings | Asset overlay has Import, Object settings, Follow and list; route is under More | Restore original character workflow and control homes without extra steps |
| P0 | Terrain and shape gears open distinct settings above bars | Gear leads to generic Camera & brush inspector | Restore separate brush/material/pour and snapping/floor/view popups |
| P0 | Named material selectors and RT presets | Native form exposes numeric Ground surface 0–4, Surface 0–11 and RT quality 0–4 | Use meaningful labels and bounded controls; hide technical IDs from ordinary workflow |
| P0 | Pour button arms ground-click placement | Current native pour action places near camera | Restore click-to-place with clear armed state |
| P1 | Character popup anchored at its top-right button | Native asset panel positioned at left x=24 | Anchor to owning Characters control and clamp to screen |
| P1 | Windowed separate game preview / full-screen inline | Native preview currently inline | Restore original display-dependent behavior with reliable reset/shutdown |
| P1 | Main menu contains atmosphere, RT and camera sliders in original order | Native moves these into generic inspector; Full screen duplicated in hamburger | Restore coherent menu order and remove accidental duplication |
| P1 | Environment small effect form | Native adds terrain pour/stamp actions and routes fields to a broad world form | Keep effects and terrain tasks at their original homes |
| P1 | Dedicated music popup, one play toggle and slider | Native More has many music commands and volume up/down rows | Restore compact local player |
| P1 | Save / Don't save / Cancel protection | Native discard dialog is Yes/No without save opportunity | Preserve recovery-friendly original semantics |
| P1 | Project modal with named selectors | Native uses a generic inspector page and editable text fields | Preserve simple project→scene→preset flow and validation messages |
| P2 | More's explicitly labelled catalogue | Current More repurposed as route/music command list | Keep future catalogue separate from active editing tasks if retained |
| P2 | Full-screen hides branding and puts controls in corners | Native paint_chrome still draws logo/scene text; normal offsets retained for some controls | Match old uncluttered full-screen composition |
| Retain | Star branding, palette, original Shapes/Terrain paths, Nature/Sculpt buttons, compact playback | Already translated in earlier native appearance pass | Do not regress while restoring missing behavior |
| Retain improvement | File dialogs remember accepted paths | Native also remembers cancel-only browsing | Keep the improvement |

The native source references for this comparison are `toggle_menu`, `initialize`, `layout`, `paint_chrome`, `inspector_fields`, `discard`, and pour command handlers in `editor/cpp/editor_main.cpp`. The earlier appearance pass did not establish complete parity.

## 11. Old defects, historical wording, and limits not to copy blindly

1. **Sculpt intercepts route input.** Live reproduction: open Sculpting, choose Characters → Draw character route, drag on floor; no route appears. Close sculpt bar and repeat the drag; route appears. Source: `SetRoute` clears terrain state but leaves `buildMode == "sculpt"` and `buildDockOpen`; `SculptPointer` consumes mouse input first. Native mode switching should explicitly terminate incompatible tools.
2. **Default light controls have poor contrast.** Original combo boxes and mode toggles can be pale text on pale backgrounds. Match the dark visual language with readable control states.
3. **Terrain help is outdated.** It says “solid ground colors” and objects do not block flow; later code carries interpolated surface kinds and `FlowObstacleMask`. Update words to actual implemented limits, without claiming full 3D fluid simulation.
4. **Environment help is outdated.** The three-light/no-shadow statement does not describe the later native 32-local-light path.
5. **Catalogue is aspirational.** Physics, AI, and materials pages describe broad unfinished tools even though narrower foundations exist. Its graphics-technology vendor text conflicts with the current no-upscaling/no-frame-generation mandate.
6. **WPF-only declarations differ from native-host output.** Hint labels and preview badges in the tree may be occluded. Hidden Music/Shapes toggles and unattached dock tabs are not additional required visible menus.
7. **Full-screen helper history differs from active behavior.** Retained hide/fade code is not proof controls currently auto-hide.
8. **Scene fields are broader than a control's allowed range.** Use each UI validator, not just JSON permissiveness, when reproducing an input.
9. **Viewport telemetry is not a benchmark.** Empty-scene FPS from this audit does not establish native 4K/120 targets or heavy simulation performance.
10. **Archived viewport teardown crashed during the live audit.** At 2026-09-17 20:48:42 UTC, after closing windowed playback and inspecting inline full-screen playback, the reference process terminated. The recorded exception is `System.Threading.ThreadStateException: The handle is invalid`, at `Thread.GetNativeHandle → Thread.ThreadState → Thread.IsAlive → NativeViewport.DestroyWindowCore` (compiled source line 89), dispatched through WPF `HwndHost.AsyncDestroyWindow`. This establishes the failing teardown call, not the underlying reason the thread handle became invalid. The final return-to-window/save attempt was not completed. The isolated two-object reference scene was not saved; screenshots were already saved. No user scene was loaded into that archived process. See [preserved crash evidence](D:/projects/Starlight-Engine/migration/old-interface-reference/reference-teardown-crash.json). Do not copy the old thread-lifetime check into a native teardown design.

## 12. Verification ledger and rebuild checklist

### This documentation pass

| Area | Evidence level / result |
|---|---|
| Source/control inventory | 49 authored files hashed; 130 XAML declarations indexed; all 56 extracted event bindings resolve to source definitions |
| Main menu | Live top/bottom traversal and all five RT quality choices; source mapping of actions/ranges |
| Builder | Live all four sections and every visible dock button; both settings popups and material/floor dropdowns |
| Beginner/Advanced | Live both states, exact-values visibility, Build and Body expanders; source validation and conditional fields |
| Nature | Live all eight buttons; actual lightning placement/selection and conditional settings |
| Characters | Live starter placement, scene list, route activation and drawing; source import/card-drop handlers |
| Environment | Live full form; source action and validation mapping |
| More/music | Live catalogue top/bottom, Native Play and AI detail states, experimental availability response, music player; all 20 details extracted from source |
| Projects/presets | Live full modal top/bottom, all actions visible, missing-preset error; load/save/reconcile/launch traced in source |
| Display/playback | Live initial full-screen entry/return, separate windowed Play, Pause, Reset, close, inline full-screen Play/Pause; final teardown crashed as recorded above |
| Full engine regressions | Not rerun; no product source changed by this reference work |

Still not certified by this pass: executing each catalogue selection, every object/effect Apply boundary, deleting user content, every picker acceptance path, launching every project, multi-monitor/DPI combinations, every sculpt brush deformation, all ray tiers, or long-running simulation. Use the control map to build focused parity tests rather than claiming an exhaustive engine regression result.

### Acceptance checklist for subsequent C++ restoration

- [ ] Beginner can place, move, draw and play without entering exact values or seeing irrelevant properties.
- [ ] Advanced is explicit, reversible, and changes disclosure without resetting scene or canvas.
- [ ] Every original task remains in its familiar menu; Characters does not send users to More for routes.
- [ ] Terrain/Shapes/Nature/Sculpt bars preserve button order, visual states, tooltips and Undo/Redo/Close.
- [ ] Terrain/Shape settings open above their bars; names replace numeric material/preset IDs.
- [ ] Menu opening, resizing and switching leave viewport dimensions unchanged.
- [ ] Popups clamp within the current monitor work area and remain readable at supported DPI/window sizes.
- [ ] Selecting a tool terminates incompatible strokes/routes/gizmos; focus loss cannot leave input captured.
- [ ] Pour placement is explicitly armed and follows the next ground click.
- [ ] Save/open/close preserve edits and offer Save/Don't save/Cancel where appropriate.
- [ ] Playback preserves the original separate-window/inline distinction and pause/reset semantics.
- [ ] Native-only/no-FG restrictions remain; capability status is truthful.
- [ ] Screenshot comparisons cover every menu, both complexity modes, both display layouts, and conditional object settings.
- [ ] Native builds, targeted state-transition checks, and existing regression suites pass after implementation.

This reference is the basis for the next interface implementation, not a request for a new design. The user-facing goal remains a familiar, uncluttered editor with the old version's character.
