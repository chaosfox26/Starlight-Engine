## Owner update — 30 September 2026: delivery order and hardware targets

### Visual-overhaul scope clarified after the reboot

The owner explicitly requests a full visual overhaul of everything currently present in the engine: higher-quality meshes, textures, materials, lighting and effects. Named requirements include realistic fire, water, lightning, lava and rocks; these examples are not the limit of the scope. The reported problem is a flat, two-dimensional appearance reminiscent of early/mid-2000s games. Higher resolution alone does not establish acceptance: the intended result is visibly better depth, form, motion and scene interaction across existing visual categories. Preserve existing gameplay, atmosphere/physics behavior and asset-editing capabilities. Inventory all current visual categories before declaring the overhaul complete; selected showcase effects cannot stand in for the full request. Maintain scalable quality and the recorded hardware targets. Exact visual references and per-category acceptance still need elaboration as the owner explains; do not invent approval for deleting or replacing existing capabilities.

This update supersedes conflicting older ordering and pause statements below. The current packaging status and exact evidence remain in [Alpha One continuation](docs/packaging/ALPHA-ONE-CONTINUATION.md). Preserve the broader numbered roadmap and its scoped acceptance; do not reopen completed work or mark broad tasks complete from a narrow package test.

1. Finish Alpha One self-contained portable/optional-install packaging and functional verification in Windows Sandbox.
2. Complete the agreed texture, shader, mesh and environment-quality work. Specific asset changes still need definition; preserve existing atmosphere/physics capabilities.
3. Optimize and verify performance with that representative content and the low-end hardware targets below.
4. ARM/Steam Frame feasibility is now urgent: the owner expects a headset on October 1 and wants day-one use. Investigate compatibility now without treating an unverified native port as complete or silently replacing the Windows packaging goal. A full native ARM port and immersive VR remain distinct implementation scopes.

| Area | Owner target | Verification boundary |
| --- | --- | --- |
| NVIDIA discrete graphics | RTX 2000-series and newer as the current priority, approximately 4 GB VRAM | Earlier GTX 900-series exploration remains a stretch target; no blanket family guarantee |
| AMD discrete graphics | Radeon RX 5000-series and newer as the current priority, approximately 4 GB VRAM | Earlier R9 300-series exploration remains a stretch target; exact models need verification |
| Intel graphics | Include Intel discrete GPUs | Minimum model remains unset; rendering, upscaling and frame generation are separate acceptance items |
| Integrated graphics | Intel and AMD from approximately 2021 onward | Use shared-memory budgets; do not impose the discrete 4 GB VRAM rule |
| CPUs | Intel and AMD x64 from around 2015 onward | Verify instruction requirements and workload behavior; age alone is insufficient |
| System RAM | 4 GB minimum target | Unverified; budget Windows, engine and integrated graphics together |
| Rendering focus | Prioritize rasterized performance; retain optional advanced features with capability checks | No universal FPS target or cross-hardware certification inferred |

Latest voice steering returns the immediate discrete-GPU priority to RTX 2000/RX 5000 while retaining Intel integration. Hardware names describe development targets, not measured minimum requirements. Windows Sandbox covers clean-machine behavior; it cannot certify all physical GPUs/CPUs. Current upscaler stubs do not establish implemented XeSS or frame-generation support.

Steam Frame requirement clarified by owner: run the editor on a virtual screen with actual native ARM64 integration. FEX/CPU emulation and a translation-layer substitute do not meet acceptance. Immersive VR is not the requested first target. Target native SteamOS/Linux ARM64 with a native graphics backend and editor platform support. The current Starlight CMake explicitly rejects non-Windows builds and the renderer uses D3D12; no OpenXR/Vulkan implementation was found in the inspected renderer/editor/build source. This requires platform/windowing/input, graphics, dependency and toolchain work, not just a compiler architecture switch. Verify native executable/dependency architecture, editor launch, representative scene rendering, input, save/reopen and exported-player behavior on the actual headset. A complete verified port by October 1 is not a credible commitment from the current state. Preserve the full Windows goal and engine capabilities; no reduced demo or compatibility-layer build counts as completion of native editor support.

Valve-confirmed Steam Frame processor: Snapdragon 8 Gen 3 (ARM64), running SteamOS based on Arch Linux. Verified against Valve hardware listing and Steamworks compatibility documentation on September 30, 2026: https://store.steampowered.com/hardware/steamframe and https://partner.steamgames.com/doc/steamhardware/steamframe/compatibility?l=english . Confirm graphics-driver capabilities and native dependency availability during port bring-up; this establishes the target specification, not Starlight compatibility or performance.

Latest owner sequencing clarification: establish the native Linux platform target first, then implement ARM64 architecture support on that foundation. Do not combine Linux platform bring-up and the ARM64 port into one initial milestone. Windows Alpha One remains the immediate release goal; this clarification orders the portability work and does not discard the agreed content/performance roadmap. Linux bring-up must progressively implement the editor, rendering, input, file handling and required dependencies, with actual feature verification; a compiling shell is only an intermediate step. Native ARM64 Steam Frame editor support remains the eventual acceptance target.

References: [Valve standalone compatibility](https://partner.steamgames.com/doc/steamhardware/steamframe/compatibility?l=english), [Valve custom-engine integration](https://partner.steamgames.com/doc/steamhardware/steamframe/engines/custom?l=english).

Keep the whole development folder below 4 GiB where safely achievable without removing required capabilities, source, assets or evidence; measure it honestly, including Git, analysis, publication and outputs. Keep summaries, failure reasons and cleanup manifests instead of accumulating duplicate packages and bulky captures. Latest measured size is recorded in the packaging checkpoint, not assumed to meet the target.

The owner requests rapid continuous implementation with verification between steps, concise factual updates, privacy guards, historical/commentary material in separate documentation, and technical constraints/licenses retained where needed. Claude Code and Starbus remain outside this task.

## Current owner-defined sequence

The new sequence is: (1) full physics and dynamic behavior; (2) higher-detail meshes, textures and environment quality; (3) return to performance optimization using that realistic workload. See [the detailed task list](docs/PHYSICS-AND-MOTION-TASK-LIST.md). Retain the GPU attack-fill change. The owner subsequently authorized direct implementation and verification. Task 1 is now active. This sequence supersedes conflicting older order notes below.

## Historical planning checkpoint — superseded by implementation authorization

27 September 2026: retain the GPU attack-fill change. Pause implementation and testing while preparing the next task list. Its first item is full movement, combat and object physics, including genuinely dynamic lightning and other intended moving/reactive effects. The detailed checklist is [Physics and motion task list](docs/PHYSICS-AND-MOTION-TASK-LIST.md). This is a planning checkpoint, not authorization to skip the list and begin implementation. Further frame-pacing investigation is deferred;4B remains unstarted. Earlier priority/order notes below are historical where they conflict.

## Latest clean-Windows result — 27 September 2026

PASS: fresh Windows Sandbox guest, final export hashes verified, content validation and all 33 gameplay checks plus combat exchange passed. Zero audio errors; captures reviewed. See docs/STEP-4-CLEAN-WINDOWS-20260927.md (or STEP-4-CLEAN-WINDOWS-20260927.md from docs). This supersedes older clean-Windows blockers below. 4B remains untouched.

# Starlight Engine: ten-task implementation checklist

## Current override — 27 September 2026

Final follow-up: combined fullscreen performance verified after cosmetic batching (7.47 to398.01 average FPS in the same44-exchange workload).937 core checks and33 final-export foreground gameplay checks passed. See docs/STEP-4-PERFORMANCE-20260927.md. Clean-Windows validation remains the unverified environment gate;4B andTask5 have not begun. This supersedes the earlier “performance next” sentence below.

ChaosFox resumed Step4/4A autonomously. Steps1–3 stay closed; earlier instructions below to reopen them are superseded. Local gameplay acceptance for the agreed placeholder scope passed: normal build,935 core checks,11 installed movement checks,6 weapon/audio checks and33 exported foreground gameplay checks. See docs/STEP-4-ACCEPTANCE-20260927.md for individual requirements and evidence. Clean-Windows validation remains unavailable; do not mark the whole milestone complete. Combined combat performance is next and must be verified before4B (abilities, stats, traits). No4B work yet, and noTask5 advancement.

Updated: 23 September 2026. Owner: ChaosFox. This is the durable execution record for the approved Gemini plan.

## Current working mode

User override: Task 4 is now a supervised, step-by-step session with ChaosFox present. Explain each action, show the result, and wait for her observations between interactive checks. Correction: the user explicitly forbids skipping any part of Task 4. Do not advance to Task 5. After completing Task 4, re-verify Tasks 3, 2 and 1, in that order, against every original requirement; resolve gaps before Task 5. Continue showing concrete progress. Automated controller checks passed (583 total); hands-on and clean-machine validation remain outstanding.

## Execution rules

Follow Tasks 1 through 10 in exact numerical order. Before each task, inspect the current source; implement the requested behavior; validate its acceptance criteria; record evidence and limitations here. Do not advance past a critical failure. Mark complete only after the scoped acceptance checks pass. Do not reopen completed tasks for unrelated bug hunting. The user explicitly requires a requirement-by-requirement verification of Tasks 3, 2 and 1 after Task 4; reopen any unmet requirement or demonstrated regression. Preserve existing work and original failure logs. Never represent unavailable clean-machine or physical-device checks as passed.

Work autonomously but visibly: provide meaningful progress updates at least once per minute during active work, covering findings, changes, validation and blockers.

At every handoff or session restart, read this file first for task order and status. Update it when a task changes state, finishes, or is blocked. Keep the next concrete action recorded. Detailed diagnostic checkpoints supplement this checklist; they do not override its ordering.

User clarification: work autonomously, update this checklist whenever a task finishes, and stop each task at its acceptance criteria. Run a final overall integration test after all ten tasks; do not turn each step into an unlimited bug hunt.


## Latest Task 2 extension — Auto-fit and 25–200% slider
Implemented Auto-fit window checkbox and manual slider with 1% increments, live percentage, release-to-apply, saved preference and last manual value. Auto follows window size in stable 5% bands (25–100%). No Windows settings changed. Replaced blank-at-small-size mode dropdown with a checkbox; screenshot ui-14.bmp confirms label visible at 63%.
Validation: task2-checkbox-layout-20260923.json success=true (72 focused checks); task2-checkbox-visible-20260923/result.json pass=true (17 visible checks). Prior full Auto/slider run passed114 checks. Both final test processes exited and their windows closed. Normal editor PID20856 remains open on previous build; new controls are verified in audit build, NOT installed into the running normal editor. Next: save/close regular editor, rebuild normal executable and reopen. Do not mark deployment complete before this.
Evidence: D:/projects/Starlight-Engine/analysis/task2-checkbox-visible-20260923/ui-14.bmp.

## Latest completed extension —24 September2026

Task3 quest extension passed final scoped acceptance; normal editor/player rebuilt. See [final report](TASK-3-QUEST-FINAL-REPORT.md). This supersedes historical paused/incomplete quest-extension entries below. The subsequent F-menu/grouped-objective extension is complete too; see TASK-3-OBJECT-AUTHORING-REPORT.md. Task4 remains next, including camera behavior and clean-machine export. No Task5 work.

## Status at a glance

- [x] 1. Attribute and fix lifecycle memory retention — COMPLETE: elevated attribution, bounded graphics-buffer reuse, final visible80cycles/12checks passed with0-byte growth; GPUdebug regression passed. See final report.
- [x] 2. Close visible usability gaps — COMPLETE for user-requested application-level scaling: full109checks, final68focused and10visible checks passed; Windows settings unchanged. Physical multi-monitor transitions untested.
- [x] 3. Reusable rules, quests and object-first authoring — COMPLETE for current agreed scope: grouped1–5 objectives; distinct-object tutorials; F menu; Carry/place/cancel/Undo; Open/Break/Collect and quest links.220 core,32 authoring,33 grouped-visible,14 object-visible,9 distinct-object visible checks passed. Final regular-build9 visible and12 local export checks passed; screenshots delivered. EXP/unlocks/full lesson chain remain Task6. See TASK-3-OBJECT-AUTHORING-REPORT.md.
- [ ] 4. Strengthen the creation-to-share loop — IN PROGRESS; must be completed before advancing. Controller implemented, 583 core and 10 local export checks pass. Camera correction, hands-on completion and clean-machine validation outstanding.
- [ ] 5. Deliver the startup experience — not started; gated by Task 4 and the required 3 → 2 → 1 verification.
- [ ] 6. Interactive Mode progression and objectives — not started.
- [ ] 7. Animation and navigation — not started.
- [ ] 8. Terrain expansion and streaming — not started.
- [ ] 9. Runtime UI and spatial audio — not started.
- [ ] 10. Multi-platform and VR export — not started.

## Task 1 — Attribute AND fix memory retention

Final acceptance supersedes historical results below: [Final report](../Starlight-analysis/task1-buffer-reuse-final-20260923/TASK-1-FINAL-REPORT.md). All12checks and80cycles passed;0-byte growth, unchanged16MiB limit; graphics debug validation passed. Elevated tracing accounts for the failed active-endpoint growth.

Goal: resolve the original +17.58 MiB lifecycle failure. Capture warmup, matched idle baseline, active-cycle endpoint and recovery; identify the responsible allocation path and implement a product fix. Validate the exact failed workload without changing the 16 MiB allowance or adding artificial sleeps.

Completed: bounded, fence-safe upload-buffer reuse with persistent mappings removes repeated staging allocation/mapping churn after warmup. Final clean 80-cycle workload passed all 12 checks at +14.2734375 MiB; handles remained flat. Core and canonical GPU-debug checks passed.

Evidence: [Task 1 report](../Starlight-analysis/task1-memory-fix-20260923/TASK-1-REPORT.md), [diagnostic checkpoint](../_recovery/STARLIGHT-TASK1-CHECKPOINT.md).

Limits: residual transient graphics retention remains. Shared allocation-region evidence identifies the actionable upload path but does not establish exclusive ownership of every retained byte or a vendor-internal allocation stack. This is a scoped acceptance pass, not a zero-retention claim.

## Task 2 — Close visible usability gaps

Final application-level acceptance supersedes historical limits below: [Final report](../Starlight-analysis/task2-interface-final-visible-20260923/TASK-2-FINAL-REPORT.md). Menu > Interface size scales the editor100–200%; final normal build is available. Full109checks, final68focused and10visible checks passed. Actual DPI-handler events tested; physical monitor transitions not claimed. User explicitly requested no Windows display-setting changes.

Goal: fix bottom clipping in scrollable panels, including Shape; preserve full control access in small windows and varying DPI sizes; verify consistent icons and a persistent Help Search label. Retain dark styling and scene safety.

Completed: corrected child scrolling, panel/dock/asset layout and scrollbar clipping; dark scrollbars remain visible. 100 editor checks and the final five focused visible checks passed. Help Search remains visible while typing; scene-preservation checks passed.

Evidence: [Task 2 report](../Starlight-analysis/task2-layout-fix-20260923/TASK-2-REPORT.md), [checkpoint](../_recovery/STARLIGHT-TASK2-CHECKPOINT.md).

Limits: actual display was 96 DPI. Control-fit checks modeled scrollbar metrics at 96/120/144/192/240 DPI; physical per-monitor transitions and full font scaling were not demonstrated. An interrupted broader foreground-dependent run is retained in the report rather than counted as passing.

## Task 3 — Reusable gameplay rules foundation

Goal: replace hard-coded gameplay with a bounded saved event/condition/action model. Support required item sets completing objectives and entering areas opening linked doors. Share behavior across authoring, independent Play state and Windows export; provide Undo for authoring changes.

Completed: saved schema, bounded shared runtime, editor rule controls, collection/interact/area events, conditions, objective/door/finish actions, compatibility adapters, and Play/export integration implemented. 572 core checks passed. A fresh local Windows export executed its saved area rule, completed its objective and opened the linked door; 10 export checks passed.

Evidence so far: [core results](../Starlight-analysis/task3-rules-core-20260923/result.json), [export evidence directory](../Starlight-analysis/task3-rules-export-20260923/).

Final acceptance: 12 focused editor checks passed, including actual save/reload, Undo/Redo, independent Play and full control access. Normal editor/player/commands built successfully; source-audit checks passed. [Task 3 report](../Starlight-analysis/task3-rules-complete-20260923/TASK-3-REPORT.md). Local export testing does not substitute for Task 4 clean-machine validation.

## Task 4 — Strengthen the short creation-to-share loop

Implement explicitly scoped jumping, stairs and moving-platform behavior using a suitable character controller, beyond existing static AABB collision. Separately validate standalone exported dependencies on a clean machine.

Acceptance: document supported controller behavior and limitations; verify movement/collision/platform cases and scene safety; record the clean-machine environment and export results. Missing clean-machine access is an explicit validation blocker, not a pass.

## Task 5 — Deliver the startup experience

Implement the polished Starlight logo splash screen and three mode selection cards: Interactive, Beginner, Advanced. Include truthful animated previews and reduced-motion alternatives.

Acceptance: switching views preserves scene content and all settings, including hidden advanced settings; previews reflect implemented behavior; reduced-motion behavior works. This records the approved three-card and splash-screen idea explicitly.

## Task 6 — Interactive Mode progression and objectives

Implement creation objectives, experience and unlocks tied to meaningful building milestones for the game-where-you-build-games loop.

Acceptance: routine creation is not locked behind an invented grind; advanced tools never require grinding. Verify milestone/progression behavior and persistence without damaging authored work.

## Task 7 — Animation and navigation implementation

Implement animation import, native scene playback and navigation meshes derived from walkable surfaces.

Acceptance: characters reach destinations without passing through walls; animations transition without broken poses. Record supported formats, navigation cases and verified limitations.

## Task 8 — Terrain expansion and streaming

Expand beyond finite 24 x 24 metre terrain with large-world streaming, stacked surfaces and caves.

Acceptance: the expanded scene loads and movement stays smooth within a recorded memory budget. Record the scene, hardware, budget and measured behavior; verify stacked/cave geometry and streaming transitions.

## Task 9 — Runtime UI and spatial audio systems

Implement input mapping, creator-authored game menus and object-origin spatial audio.

Acceptance: saved sound settings work in exported games; runtime UI stays readable and responsive. Validate authoring, saving/loading and exported behavior.

## Task 10 — Multi-platform and VR export

Scaffold portable editor and renderer support for Linux, macOS and VR headsets.

Acceptance: identify what is scaffolded versus running on each target. Exported VR samples must pass tracking and frame-time checks on named physical devices. Missing target hardware or measurements cannot be recorded as a pass.

## Required checkpoint after Task 4, before Task 5

- [ ] Re-verify Task 3 against every original action and constraint: saved bounded rules, authoring, independent Play, Windows export and Undo.
- [ ] Re-verify Task 2: bottom clipping, full control access, small windows and varying DPI, consistent icons, persistent Help Search, dark styling and scene safety.
- [ ] Re-verify Task 1: allocation evidence at all four phases, attribution, product fix and exact failed workload; unchanged 16 MiB allowance and no artificial sleeps.

For each requirement, record current-source evidence, validation results and any remaining limitation. Prior scoped passes are historical evidence, not a substitute for this verification. Do not assume the outcome will be complete; reopen and resolve gaps, and report any unavailable validation honestly. No Task 5 work until Task 4 and this checkpoint are complete.

## Resume point

Continue Task 4 with ChaosFox using docs/STEP-4-MOVEMENT-AND-COMBAT.md: held-right-button MMO camera, third-person to first-person zoom, double jump, sprint, directional dodge, and Step 4A manual telegraph combat; finish hands-on controller acceptance and clean-machine dependency validation. Then verify Tasks 3, 2 and 1 in order, with no skipped original requirements. Tasks 5–10 have not begun. Update this checklist after each transition; final overall integration testing still follows Task 10.
## Latest supervised evidence

Task 3 fresh authoring/Undo checks pass: ../Starlight-analysis/supervised-reverify-321-20260923/task3-ui.json. Task 2 fresh full editor check reports pass: ../Starlight-analysis/supervised-reverify-321-20260923/task2-full-ui.json; actual high-DPI gap remains. Task 1 five-minute attribution check stopped early on a confirmed profiling-permission blocker: [result](../Starlight-analysis/task1-five-minute-attribution-20260923/ATTRIBUTION-RESULT.md). No full attribution claim. Quest text/icon and mouse-look/cursor behavior requested during supervision remain outstanding.

Latest handoff: [supervised checkpoint](../_recovery/STARLIGHT-SUPERVISED-CHECKPOINT.md). Task 1 allocation growth now attributed at the active endpoint; full traced workload failed original limit by 8 KiB. Additional bounded graphics-buffer reuse implemented and built, not tested. Visible 80-cycle test prepared; awaiting user readiness. No hidden tests or further task advancement.

## Visible rerun result — 23 September 2026
User confirmed prior run was manually stopped, then explicitly authorized rerun. Evidence: D:/projects/Starlight-Engine/analysis/task1-visible-buffer-reuse-rerun-20260923-220334. PID35644; final failure dialog remains open for user. All80 measured cycles completed, idleControl=false. Baseline/final private bytes both2015870976: delta0bytes, unchanged16MiB allowance passed. Handles568flat. Uploadreuse76to876; loadedBLAS2/unloaded0 confirms scene work. Nine checks passed, then overall session FAILED: Path completion stops nonrepeating playback. Do not report entire suite/Task1 complete. New buffer reuse still needs GPUdebug validation. Next bounded investigation: playback check waits12frames for0.1second route; inspect elapsed-time assumption against runtime before modifying. No new test launched after result.

## Application-level settings zoom — 23 September 2026
User explicitly does not want Windows display scaling changed. Implemented hamburger menu > Settings panel size:100%,125%,150%,200%. This scales settings controls, fonts, panel width/content height and scroll range within the current editor session. It does not change Windows settings, scene data, advanced settings, toolbar size, or help-window fonts. This is settings-panel zoom, not complete editor-wide DPI support. Real multi-monitor DPI transitions remain unverified; do not claim them as passed.

Actual regression: short empty-character panel kept unscaled height and clipped its enlarged control; fixed height calculation. Combo fonts now honor their assigned font; dropdown list capacity preserved during resizing. No scene form rebuild when changing zoom, so in-progress fields stay in place.

Evidence: Starlight-analysis/task2-settings-zoom-fixed-20260923.json all103checks passed, including all7settings pages at4sizes and720x540/1380x900windows, actual font heights, last-control reachability, panel containment and scene preservation. Final dropdown-height correction separately validated by all5visible checks in task2-zoom-visible-final-20260923/result.json; screenshotsui0through4. ui4 is200%Shape bottom controls, Apply fully visible. Normal out/classic NativeEditor built with changes (and currentTask1fix/controller). Audit binary predates final dropdown-height correction; full103-check result is prior to that tiny correction, five final visible checks validate normal final binary. Task2 remains open for broader interface/DPI requirements; application-level settings zoom is delivered.

### Slider endpoint verification completed
Visible run task2-slider-endpoints-visible-20260923/result.json passed all19 checks. Screenshots ui-15.bmp (25%) and ui-16.bmp (200%) inspected: Auto-fit label and percentage render at both endpoints; 25% text is correspondingly tiny. Slider range25–200 and1% arrow increment asserted. Scene unchanged. Test PID15444 exited automatically. Normal editor replacement remains pending save/close of PID20856.

Normal Release rebuild succeeded; updated regular editor launched with Auto-fit checkbox and25–200% manual slider. PID 3792. Supersedes earlier pending normal-editor replacement note.


### Preference persistence verified
Five separate editor processes launched and closed successfully using the actual startup loader and settings controls, with an isolated preference file. Confirmed manual25%, manual200%, Auto with remembered63%, and manual63% restored across process restarts. All5 reports pass: D:/projects/Starlight-Engine/analysis/task2-preference-relaunch-20260923/relaunch-0.json.result.json through relaunch-4.json.result.json. User preference untouched. Added --interface-preference-check acceptance harness; normal editor already contains identical preference save/load behavior, new test hook is only in audit build.

Task 3 quest presentation research: docs/QUEST-TRACKER-REFERENCE.md records Starlight interface requirements, verified current source gaps and implementation acceptance. Research only; quest tracker not yet implemented.


## Current Task3 quest extension and startup focus —23 September2026
Implemented schema10 QuestDefinition (stable IDs/title/description/linked rule IDs/tracked), quest_model.hpp, shared native quest_tracker.hpp, creator UI named multi-selection and multiline details, save/edit/remove Undo, New quest adventure template, independent Play, shared standalone export. Clickable right tracker, real collection counts, completion/locked stages, journal/pin/collapse/scroll. Scope limits in docs/QUESTS.md.
Validated: task3-quests-core-final-20260923 (181 checks); task3-quest-authoring-final-20260923.json (20); task3-quests-export-final-20260923 (12); task3-startup-priority-20260923 (8 visible). Screenshots0/1/6 viewed. Earlier visible runs failed foreground precondition with Discord active; not product passes.
User instruction: startup Starlight prioritizes itself above other apps until user clicks away. startup_activation.hpp performs one activation, temporarily attaches input queues with RAII detachment; no topmost loop. Verified foreground true in8-step run. If any future check cannot foreground itself, STOP and notify user; no retry/substitution without direction.
Normal editor PID3792 remains previous scaling build. Asked user to save/close for normal rebuild; no response yet. Do not kill it or claim new quest/focus behavior installed there. Audit builds current. Quest extension remains open pending extra pin/collapse/scaled checks and normal deployment. Task4 remains unfinished camera/clean-machine; no Task5.

## Latest screenshot handoff — quest journal and opacity
Normal out/classic editor/player/commands rebuilt successfully with current quest journal, hints, background opacity and one-shot startup focus. User closed earlier regular editor and authorized replacement. Latest normal standalone player PID15776 opened on task3-journal-final-visible-20260923/Quest-adventure.starscene as a normal visible game; user specifically wants it left open for a screenshot to share. Do not close it or start another foreground test until directed.
Latest tracker: background0–100% in10% steps, text stays opaque, saved user preference quest-tracker.json (audit skips preference writes). Per-pixel layered child positioning fixed by not moving HWND inside UpdateLayeredWindow. Visual fixed-opacity run11/11 passed, screenshots0/1/2/4 inspected. Earlier opacity run incorrectly passed behavior but drift was found visually; superseded.
Full tracker control run task3-quest-final-visible-20260923 passed18 checks (pin/unpin/collapse/25/200/100 plus progression) BEFORE subsequent wide-journal/hints addition. Latest wide journal adds quest list + detailed description/current-step hints, click objective/title opens it. Latest task3-journal-final-visible-20260923 passed first10 steps then LOST FOREGROUND at25% step; behavior=true, capture=false. STOPPED without retry per user. User then explicitly requested normal demo reopened for screenshot, not a rerun. Latest core181 passed at task3-journal-core-20260923. Authoring20 and export12 latest passes predate hints/opacity additions; targeted revalidation still due before declaring Step3 fully closed.
Next after screenshot: await user's direction to resume interrupted visual check, run remaining latest-source authoring/export checks; save final report/checklist. Task4 not complete; do not skip. Current demo may be interacted with; preserve user's work.

## PAUSED —24 September2026 cogwheel tracker update
User requested: compact tracker shows quests and a cogwheel only (no journal/settings heading); clicking quest opens journal. Cogwheel has opacity, unlock/drag/relock, saved position. Implemented in quest_tracker.hpp: vector cog, settings toggle with opacity +/- lock/reset/collapse/journal, captured drag with clamped normalized anchors, version2 preferences preserving opacity/lock/position (version1 compatible); default compact heading removed. Tests adapted to actual hit rectangles and added drag/lock checks. Audit Editor and Player build succeeded. Normal binaries still previous journal/opacity version; no cogwheel deployment yet.
Visible attempt task3-quest-cogwheel-20260924/result.json: activationRequested=true, then LOST FOCUS before checks. Stopped without retry; notified user immediately. NO TESTS PASSED for this cogwheel change. Await explicit direction before any retry or substitute check. Save/relaunch persistence still needs validation. Do not claim Step3 complete. Current standalone screenshot demo PID15776 was left open earlier; inspect before any rebuild, do not close without authorization.
Latest source compile corrected LONG/int mismatch in drag ranges. Remaining source scrutiny: window stays clamped at varying scales, native mouse capture release and preference save; compact cog fixed at top; validate position across journal open/close and app relaunch. Old passing runs do not prove new controls. Need docsQUESTS updated when complete. User clear rule: if foreground difficulty, stop and notify, wait for direction.

24 September2026: user expanded Step3 authoring direction to in-world object-first menus (right-click/F, movement/pickup/break/open categories, optional sequenced quest objectives). Research and exact scope recorded in docs/OBJECT-FIRST-AUTHORING.md. Existing rule/quest foundation passes remain valid; this additional authoring layer is NOT implemented. Do not claim the extension complete.


## Step4 addition —24 September2026
User explicitly requests MMO camera/movement research and double jumping. Research saved in docs/MMO-MOVEMENT-REFERENCE.md. LATEST CORRECTION: user explicitly wants hold-right-mouse camera control, NOT always-on mouse look; this overrides earlier camera requests. Double jump is not implemented yet.

## Current Step 4 specification — 24 September 2026

[Step 4 movement and Step 4A combat](docs/STEP-4-MOVEMENT-AND-COMBAT.md) records the latest user-approved controls, exact stamina/dodge values, HUD layout, manual telegraph combat, open tuning decisions, and acceptance checklist. Supersedes older automatic mouse-look requests. Requirements recorded; new features are not marked implemented or verified.

Latest Step 4/4A design review: confirmed choices consolidated in the linked specification, including dodge damage immunity, grapple moving-character attachment and release momentum, held-1 area attacks, and player-only CC immunity. Implementation and acceptance remain in progress; no task completion change.

Latest interaction decisions saved in Step 4 spec: sleep immunity after waking/control return; dodge cancels winding-up shot; knockback/pull/launch/knockdown block firing; release G before wall attachment falls, dodge detaches grapple; lunge damage immunity twice dodge, delegated starting values 0.25s dodge / 0.5s lunge. Documentation only; implementation/verification pending.

Step 4A addition: boss break bar, effect-specific CC depletion, empty bar triggers stunned/staggered increased-damage phase. Saved in Step 4 specification; tuning and intact-bar CC interaction remain open; implementation pending.

Boss break-bar tuning: user confirmed default capacity 1,000 and delegated CC contribution values. Step 4 specification now records adjustable per-application defaults from75 (slow/blind) to250 (stun/launch), with no tick/yank double-counting. Documentation only; boss resistance and break/recovery timing remain open.

Confirmed boss rule: intact break bar converts CC to bar damage instead of normal control effects. One player can contribute multiple valid CC casts; PvP same-attacker cancellation/immunity does not apply to boss bar contributions. Specification updated; implementation pending.

Boss break window confirmed: 25% increased damage taken (1.25x), maximum eight seconds, initial default eight seconds. Bar recovery/repeat-break behavior still open. Documentation only; implementation pending.

Boss recovery confirmed: after the up-to-eight-second stagger ends, refill 0 to1,000 over90 seconds; cannot break again until full. Specification records linear refill and protected recovery interpretation. No implementation/test claim.

## Step 4/4A specification finalized
The consolidated docs/STEP-4-MOVEMENT-AND-COMBAT.md now contains confirmed rules, adjustable movement/combat/CC defaults, boss/lifecycle behavior and final acceptance gates. Proceed to implementation without another design interview. Older open-question notes are superseded. Documentation complete; code, visible verification, export acceptance and deployment remain unfinished.
