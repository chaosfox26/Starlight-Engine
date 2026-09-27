# Starlight interaction research and implementation requirements

Research pass: September 16, 2026. Requested by Chaos before further interaction changes.

Purpose: make building enjoyable, understandable, and responsive for someone who has never used an engine. This is a research-backed design specification, **not a declaration that every recommendation is implemented**. The current repair build has automated interaction checks; its user-facing rollout and remaining regression checks are still pending.

## What the references establish

These are documented user-facing behaviors. Public manuals do not reveal each game's internal input, camera, or rendering implementation. Engineering proposals below are Starlight-specific inferences, not claims about proprietary source code. Sources were researched through official documentation and developer articles; no hands-on comparison of the games was performed in this pass. Frontier's guide was available in indexed text, but direct retrieval returned an access error.

| Reference | Verified behavior | Lesson for Starlight |
|---|---|---|
| [Spore controls](https://www.spore.com/comm/tutorials/controls) and [creator manual](https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/17390/manuals/manual.pdf?t=1642702281) | General controls include wheel zoom and middle-drag pitch/rotation. Tribal/civilization selection uses left-drag. Creator controls differ: background right-drag rotation, advanced handles, part scaling, undo/redo. | Keep playful direct manipulation, but do not import every context-dependent mapping. Keep camera gestures consistent across Starlight tools. |
| [Total War Academy](https://academy.totalwar.com/battle-keyboard-and-mouse-controls/) | Left-click selection, left-drag rectangle selection, Ctrl-add, empty-click deselection, middle-drag camera rotation, keyboard travel. | Visible selection and reliable deselection matter as much as selecting. Starlight's requested right-drag selection is a deliberate adaptation. |
| [Age of Empires IV camera](https://support.ageofempires.com/hc/en-us/articles/4408479604244-Editor-Camera-Controls), [view modes](https://support.ageofempires.com/hc/en-us/articles/4415431547156-2-Render-View), [AoE II accessibility](https://www.ageofempires.com/age-ii-de-accessibility/) | AoE IV's editor has middle-drag sliding, wheel zoom, Alt pivot, and distinct game/tools/orthographic views. AoE II exposes scroll inertia settings. | No universal RTS mapping exists. Provide clear view recovery and adjustable camera response instead of assuming one sensitivity suits everyone. |
| [Planet Zoo building guide](https://www.planetzoogame.com/help-centre/player-guides/building-your-zoo) | Brush size/intensity, push/pull, flattening, smoothing, roughening, terraces, and additive/subtractive terrain stamps. | Start with a small brush family; put advanced variants behind a secondary panel. Show the brush footprint and explain the result before the user commits. |
| [Halo Forge fundamentals](https://www.halowaypoint.com/news/halo-infinite-forge-fundamentals) and [Forge overview](https://www.halowaypoint.com/halo-infinite/forge) | Object palettes, scaling, undo/redo, terrain pieces, and magnet snapping support in-game construction. | Separate free placement, grid snapping, ground contact, and object attachment. Let users experiment and undo. |
| [Fortnite Creative placement controls](https://dev.epicgames.com/documentation/fortnite/hotkey-and-keybinding-shortcuts-in-fortnite-creative) | Contextual control hints, multi-selection highlighting, individual-axis resizing, optional grid snapping, ground-drop preview, and separate collision choices. | Preview where the object will land. Show active constraints and distinguish snapping from collision. Avoid hidden mode changes. |
| [Cities: Skylines II map editor](https://www.paradoxinteractive.com/games/cities-skylines-ii/modding/dev-diary-2-map-editor) and [road tools](https://www.paradoxinteractive.com/games/cities-skylines-ii/features/road-tools) | Bottom terrain tools, heightmap workflows, object placement, construction guides, snapping options, and cancellation during road construction. | The requested bottom pill fits the builder-game approach. Preview guides and reversible placement support confident experimentation. |
| [Factorio undo/redo development](https://www.factorio.com/blog/post/fff-412) | Undo/redo tooltips describe the pending action; feedback identifies what changed. | Prefer “Undo move cube” over a mysterious back arrow. One meaningful gesture should produce one recoverable history step. |

Terrain sculpting is already present in competing engines: [Unity's terrain tutorial](https://learn.unity.com/course/introduction-to-terrain-editor/tutorial/working-with-the-terrain-editor-1) covers raising/lowering and smoothing; [UEFN Landscape](https://dev.epicgames.com/documentation/fortnite/landscape-mode-in-unreal-editor-for-fortnite) includes sculpting, flattening, and ramps. Starlight's opportunity is accessibility, consistency, and measured responsiveness—not claiming these tools are unprecedented.

## Recommended interaction contract

### Camera: one mapping everywhere

- Wheel zooms; middle-drag tilts and rotates. Preserve Chaos's chosen mapping.
- Edge hover moves across the ground relative to the viewing direction. Corners combine directions without increasing overall speed.
- Reach overhead, side, and low upward-looking views. Prevent accidental camera inversion. Expose Focus selected, Home, Side, and Above as clickable controls.
- Use elapsed-time movement, a configurable edge zone, and bounded zoom-dependent speed. Start with immediate stopping; optional gentle acceleration must not produce drift after release.
- Menus, popovers, dialogs, window resizing, inactive windows, and leaving the application suppress edge travel. An edge-revealed menu takes priority over panning.
- Provide a visible Pan alternative for users who cannot comfortably press the wheel, without replacing the agreed default controls.

### Selection and transforms

- Select mode: left-click selects one; empty left-click clears; right-drag draws a visible selection rectangle. Add/remove modifiers can follow as secondary controls.
- Sculpt mode owns brush gestures, including right-button lowering. An explicit Select button exits sculpting. Never silently interpret the same gesture as both brush and box selection.
- Only one gesture owns input at a time. Release ends it, even outside the viewport. Focus/capture loss clears ownership. Escape cancels and restores the starting state; release commits. Cancellation and completion must not be conflated.
- Selected and hovered objects/handles need distinguishable feedback, including a group count. Handle picking should choose the closest eligible screen-space handle, not the first hit in a list.
- Size handles in screen space so distant objects remain editable. Freeze the chosen axis and drag reference for the entire gesture; do not retarget mid-drag. Near-parallel camera/axis cases need a stable fallback.
- Move/Rotate/Resize stay visible, plainly named, and undoable. Group operations should show a stable shared pivot. Start by retaining the existing primary-object pivot and label it; do not change it invisibly.

### Placement and snapping

- Show a translucent placement preview under the cursor before a new object is committed; expose repeat placement and cancel explicitly.
- Grid, Ground, and Objects are separate toggles. Keep above floor is a separate constraint, not another name for ground snapping.
- Preview the final snapped position during movement rather than surprising the user with a jump on release. Show the target surface or attachment point.
- Object snapping needs a capture distance and a slightly larger release distance so nearby targets do not flicker. Define grid spacing and rotated/local alignment behavior.
- Record one completed placement/drag/scale/rotation as one Undo action. Redo restores the result; a new edit invalidates redo. Selection and camera travel should not pollute object-edit history.

### Terrain: small toolbox, continuous feedback

Primary tools: Raise/Lower, Smooth, Flatten, Paint, Stamp, Water. Plain labels for brush Size and Strength; optional Soft edge control. Add a two-point Slope tool after core stability.

- Show a terrain-conforming brush footprint and the active action. Flatten samples its target once at stroke start, not repeatedly while the terrain changes.
- Interpolate brush samples along the travelled path to prevent gaps when the mouse moves quickly. Integrate brush strength over time so mouse polling and frame rate do not change the result.
- Stamps should show a ghost footprint and height; additive/subtractive choices must be explicit. Preserve source shapes when stamping them into terrain.
- Paint should eventually blend materials; current solid palette painting is not grass geometry or textured terrain.
- Current water/lava is a shared height surface clipped to low ground. Independent lakes, flow, and fluid simulation are distinct future features.
- Current terrain is a heightfield: one height per horizontal location. It can make hills, valleys, and plateaus, but not caves, overhangs, or arbitrary shape fusion. Those require another representation or separate mesh tools.

## Findings in the current Starlight source

Read-only inspection during this research pass found:

1. Edge pan still uses a fixed distance per timer tick. Direction checks pass, but time-based speed and normalized diagonal motion remain improvements.
2. Terrain strength already uses elapsed time, and flatten height is captured at stroke start. Preserve these good behaviors.
3. Terrain strokes use the latest pointer position every 50 ms without path interpolation. Rapid movement can skip terrain between samples.
4. Terrain invalidation rebuilds the full terrain model. Dirty-region/chunk updates are needed before scaling terrain or claiming sustained sculpting performance.
5. Shape creation currently inserts at the camera center; cursor placement previews remain a gap.
6. Handle picking takes the first eligible hit; rotation uses a screen-delta approximation. Passing release tests does not prove every camera angle feels correct.
7. Snapping occurs on gesture completion. Previewed snapping and target hysteresis remain gaps.
8. Current end-interaction handling clears state but does not provide a distinct rollback transaction for Escape. This needs an explicit cancel path.

The staged interaction repair report passes camera tilt, side/low views, all four edge directions, outer-window edge checks, right-drag selection, axis release/capture cancellation, axis scaling, and exact undo/redo. A 150-edit diagnostic reports zero geometry uploads during transforms. These tests do not establish full usability, fluid simulation, smooth terrain painting, or production-wide performance parity. The staged build has not yet completed rollout to the user.

## Implementation order and acceptance gates

1. **Finish the interaction repair delivery:** native terrain regression, visual toolbar check, updated launcher, then the new build visibly open. Preserve existing user windows/scenes.
2. **Camera consistency:** elapsed-time edge travel, diagonal normalization, UI suppression, sensitivity, stable orbit/focus. Verify fullscreen/windowed, multiple DPI scales, all edges/corners, menu transitions, and Alt+Tab. Same travel time should give comparable displacement under different frame rates.
3. **Placement confidence:** preview, stable nearest handle picking, predictable axis math, snapping preview, real cancellation. Verify release outside window, focus loss, group transforms, repeated edits, and exact undo/redo.
4. **Terrain responsiveness:** stroke interpolation, dirty-region updates, material blending later. Verify continuous strokes at slow/fast pointer speeds, no edits after release, one stroke per undo, save/reopen equivalence, and water boundaries.
5. **Beginner trial:** without shortcuts or coaching, place three shapes, adjust one from the side, box-select two, undo a mistake, form a hill and plateau, paint ground, and save/reopen. Record where the user hesitates; simplify that interaction.

Performance evidence must include sustained camera travel, object dragging, group edits, and terrain strokes—not just idle FPS. Capture frame-time distribution, long stalls, input-to-visible latency where measurable, geometry uploads, and scene size/resolution/settings. Compare renderer and standalone only under matched conditions. A 500 FPS ceiling is a limit, not a promise.
