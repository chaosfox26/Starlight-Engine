# Adventure controls foundation

The current addition provides an opt-in first-person walking preview in the existing editor. Choose **Use adventure controls** in the main menu, then **Play**. Use **W/A/S/D** to walk and **right-drag** to look. Escape pauses; Reset clears the temporary player position. Existing scene objects and camera authoring settings are preserved. **New starter adventure** creates a separate small level with a gold key token, locked door and green exit. The existing save/discard/cancel prompt protects unsaved work; the new level starts unsaved.

Scene schema 8 stores optional Game settings: adventure preset, spawn position, movement speed, look sensitivity, collision radius and collision enabled. Older scenes remain schema 7 until gameplay settings are added. Beginner exposes speed and sensitivity. Advanced adds spawn coordinates and collision settings; applying beginner changes preserves hidden advanced settings. Older editors reject schema 8 rather than silently discarding gameplay.

Movement uses cached static world-space collision boxes, normalized diagonal speed and bounded substeps. This is a first foundation: no jumping, dynamic-body character interaction, stairs, slope constraints, moving platforms or exact concave-mesh character collision yet. Terrain supplies walking height. It is not a complete physics character controller.

Object settings now expose None, Collectible key, Locked door, and Finish / exit interactions, with a shared key name. Press E while looking at an object within 2.5 metres. A matching key opens a door; an exit can require a key. Collected keys and opened doors disappear only in the play copy. Reset restores the authored scene. These are basic interaction rules; richer triggers, objectives and Interactive building remain follow-on work. This addition is not completion of the builder-game experience.

Evidence: adventure-settings-20260923 and adventure-movement-20260923 core checks. Live Play/Pause/Resume/Reset passed in adventure-play-ui-20260923. Beginner/Advanced preservation passed in adventure-settings-ui-20260923. Key/door/exit core checks passed in adventure-rules-fixed-20260923; editor-dispatched E-key collection, door opening, completion and Reset passed in adventure-interactions-ui-20260923 (42 steps). Physical keyboard/mouse usability and the latest rounded HUD styling still require visual review.

## Standalone export foundation

Export playable game creates a new StarlightGame folder beneath the chosen directory. Existing exports are refused. Keep the executable, engine/runtime libraries and content folder together. The player uses the same movement and interaction code; Escape pauses and R restarts. This currently targets Windows x64 / Direct3D 12 only.

Package creation and --validate loading succeeded in playable-export-20260923; no-overwrite and missing-runtime rejection passed in playable-export-checks-20260923. A separate visible play test and clean-machine test are still pending. Desktop automation was stopped by the user with Escape; no further desktop input was attempted.

Hidden standalone startup rendered 60 frames without an error in standalone-player-validation-20260923/render-smoke.json. This does not verify visible input or clean-machine compatibility. The starter adventure's walking, collision, key/door/exit sequence and restart passed in adventure-starter-20260923, alongside 528 total core checks including the geometry reference fixture.

The exported starter level also rendered 60 frames without errors from its own package directory: adventure-starter-20260923/packaged-render.json.

## Precise building controls

The Shapes settings panel keeps simple toggles in Beginner; Advanced exposes grid spacing (0.01–100 metres), rotation snapping and rotation step (0.1–180 degrees). Defaults remain 0.5 metres and rotation snapping off. Rotation snapping applies to the dragged axis on release; other angles stay untouched. Group insertion, asset insertion and paste use the chosen grid spacing while preserving relative offsets. These are editor-session settings, not saved scene properties. Core checks passed for negative positions, attached routes, group spacing, angle snapping and invalid increments in adjustable-snapping-20260923 (532 checks with geometry fixtures). The new fields still need visible review.

## Audit integration and file safety

The registered `native-adventure-export` check creates a fresh starter, packages it, verifies content and refusal safety, renders hidden frames, and records binary identities. Its CTest run passed after exposing and fixing an over-260-character temporary-path failure in the shared file reader/writer. Existing short-path saves, backups and the long-path regression passed in the 533-check core run. The test is bounded; it does not perform visible desktop interaction. Controller geometry bounds are cached at play start and reused for interaction; idle movement returns without collision work. Static-collision limitations remain unchanged.

## Walk and build preview

The main menu now offers **Walk and build (preview)**. It uses the same authored scene and existing editing/Undo tools, with W/A/S/D movement and right-drag look at player height. Menu-driven object/asset insertion appears three metres ahead, within the existing terrain limits; mouse-driven terrain and manipulation tools use the current view. Advanced snapping settings continue to apply. **Return to editor view** restores the ordinary editor camera.

Play starts a fresh gameplay test from the current exploration position. Pause returns to authored building data at the tested viewpoint; collected keys and opened doors are restored for editing. Reset returns to the authored starting position. These gameplay changes are never written into the authored project. Core checks cover viewpoint preservation, rebuilding from changed scene data and keeping collected objects in authoring; the full core run passed 536 checks. The preview's physical input and appearance have not been visually verified. Objectives, XP, unlocks, sophisticated collision and the complete Interactive experience remain unfinished.

The source assessment at adventure-assessment-corrected-20260923 is a sealed checkpoint **before** this walk-and-build addition. It must not be presented as proof of the new preview. Its dependency matching was repaired using the sealed byte inventory, without rerunning engine tests.

Additional explorer collision checks passed in walk-build-collision-20260923 (538 core checks): newly placed walls block movement and removed objects stop blocking. This remains a code/data check, not a physical input test.

## Gameplay setup hints

**Check gameplay setup** in Adventure settings reviews the applied scene settings. It lists missing collectible key names required by doors/exits, absent exits, starts overlapping static solids, and moving interaction objects unsupported by this controller. Findings appear in a read-only dark report field and clear when the scene changes. Export also saves `gameplay-check.json`; findings are advisory so an intentionally open-ended world can still be exported. This check cannot establish reachability or puzzle solvability. Core coverage passed in gameplay-setup-check-20260923 (541 checks), and the registered standalone export check passed with the new report.

## Saved gameplay rules (Task 3)

Adventure settings now include **Gameplay rules**. Name a rule, choose its event and action, then choose **Add rule**. Select an existing saved rule and choose **Remove rule** to remove it. Both operations support Undo/Redo; saving preserves the rules and linked object identities. To change a rule, remove it and add its replacement.

Events are collecting an item, entering a spherical area, or interacting with an object. An empty event subject matches any item/object. Conditions require all listed item names and, optionally, a completed objective. Actions complete a named objective, open a linked door, or finish the game. For example, require both red and blue items before completing an objective; another area rule can require that objective before opening its door. The saved model supports multiple conditions and actions per rule; the current form creates one action per rule.

Area coordinates measure the player's view position, normally 1.7 metres above the feet. An area event fires on entry, including an initial start inside it. If its conditions fail, leave and re-enter after meeting them. Once rules fire at most once per Play session; repeatable area rules require another entry. Rules run in saved order, so earlier actions can satisfy later conditions during the same event. Actions do not recursively emit events.

The bounded model permits 64 rules, 16 conditions and 16 actions per rule. Unknown operations, duplicate rule identities, invalid coordinates/radii and oversized definitions are rejected. Scenes containing rules use schema 9; older scenes remain readable. Stable door/object identities survive full-scene saves and Windows exports. Missing links are reported by Check gameplay setup. This does not prove puzzle solvability.

Editor Play and the Windows player use the same rule runtime. Inventory, objectives, fired rules and removed doors belong only to the Play copy; Stop/Reset restores authored content. Walk-and-build does not execute gameplay rules. Existing key/door/exit roles remain compatibility adapters into the shared state/actions. Selection-only object exports are not a rule-composition feature.

Validation: task3-rules-core-20260923 passed 572 core checks; task3-rules-ui-20260923 passed 12 focused editor checks for authoring, save, Undo/Redo, independent Play and reachable controls. task3-rules-export-20260923 passed 10 local packaging/runtime checks, including an exported area rule completing an objective and opening a door. Clean-machine dependency testing remains Task 4.
