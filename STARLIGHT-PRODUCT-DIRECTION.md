# Starlight product direction — three creation modes

Recorded from ChaosFox's proposal on 18 September 2026; implementation sequence authorized on 23 September below. **Status: future product direction; the complete three-mode experience is not implemented or runtime-validated.** This direction is separate from the outdated material in `Starlight Documents`. Authorization to implement does not establish completion.

## Implementation checkpoint — 23 September

Latest verification: the fresh `full-assessment-20260923-174415` run passed 551 core checks, 92 editor checks and all 63 visible walkthrough steps, including Help, asset browsing and mode round trips. This supersedes older pending-walkthrough statements below. Human usability and full physical-input acceptance remain open. The overall audit FAILED its lifecycle private-memory allowance (17.58 MiB increase against 16 MiB); handle counts and the second batch were flat. Do not call the full audit clean. The next work is memory attribution, dock readability and Help search clarity, followed by reusable events, conditions and actions shared by authoring, Play and export.

Adventure controls, a starter key/door/exit level, Windows standalone packaging, and adjustable snapping are implemented with bounded checks. A walk-and-build preview now connects first-person exploration to the existing authoring tools and a separate gameplay test copy. Its build and data/viewpoint checks pass; live input and visual acceptance remain open. This is a foundation for Interactive mode, not delivery of objectives, XP, unlocks or the complete three-mode product. See `docs/ADVENTURE-CONTROLS.md` for exact scope and evidence.

## Approved next work — 23 September 2026

ChaosFox explicitly authorized the following sequence: finish the audit/verification fixes first, then proceed with the shared game-creation foundations. This supersedes the earlier planning-only status. Routine implementation within this scope does not need another go-ahead. Provide concise, concrete updates; do not silently wait between small additions. Preserve unrelated work and record verification honestly.

### First: repair and verify

1. Fix the visible editor audit's startup/foreground handoff. Record the exact failed prerequisite; preserve real foreground and screenshot requirements. Rerun the failed stage rather than all previously passing workloads without a new reason.
2. Fix report freshness: make the current declared run prominent, normalize evidence paths, separate dated historical failures, and avoid claims of closure or completeness unsupported by evidence. Refresh documents using preserved results where valid.
3. Visually verify the new asset browser/type filter and Copy/Paste controls, including shared dark styling, rounded edges, windowed/fullscreen layout, and folder/file-dialog behavior. Command tests do not replace visual acceptance. If capture is unavailable, record that limitation rather than claim success.

### Then: one complete creation-to-export path

1. Implement one adventure game-type preset with player movement, camera, collision, and interaction. This is a new creator-facing game configuration, distinct from existing project-host benchmark presets.
2. Add reusable, saved gameplay rules for collecting a key, unlocking a door, triggering an action, and reaching an exit. Reopening a project must retain behavior; editor Play must execute it correctly.
3. Export a standalone playable build and validate it without relying on the editor/development environment. Existing portable scene export is content portability, not this milestone.
4. Build an Interactive-mode creation sequence using those same systems: construct a room, place a key, connect a door, test it, and export a playable result. Do not hard-code the example in place of reusable engine features or narrow the engine to GameProject.
5. Add creation objectives, experience, and unlocks tied to meaningful building milestones. ChaosFox explicitly identified these as part of the desired fun. Details of progression/bypass remain design work; do not make advanced creation depend on an invented grind requirement.

### Building-tool expansion — after the bug fixes

ChaosFox confirmed on 23 September that the bug fixes above remain first. Expand the existing building tools afterward, alongside the creation workflow; this addition does not displace repair and verification work.

- Original foundation: fixed 0.5 m grid snapping, ground snapping, and snapping near other objects. The 23 September implementation now adds adjustable grid spacing and rotation-on-release snapping; defaults stay unchanged.
- Implemented in code: adjustable spacing and rotation increments in Advanced, simple toggles in Beginner, shared placement across insertion/paste/groups. Core placement checks pass; new control appearance and Interactive-mode access are not verified or complete.
- Beginner should offer a simple snapping toggle and understandable defaults. Advanced should expose precise increments. Both use the same underlying placement rules and preserve project settings across modes.
- Verify predictable placement and rotation, Undo/Redo, and consistent behavior between editor controls and Interactive tools before marking the expansion complete.

Interactive mode is intended as Starlight's headlining creation experience: making meaningful things should itself be enjoyable. Building, exploring, and testing should flow together. Objectives, experience, and unlocks support that experience; they do not substitute for making the basic building loop enjoyable. The resulting world remains an editable project that can become a playable exported game. This is approved direction, not a claim that the experience is already delivered.

### Startup and mode selection — 23 September 2026

ChaosFox requested a polished, game-like Starlight Engine logo splash followed by a mode-selection screen rather than entering the editor immediately. Smooth animation and transitions are part of the intended experience. This is approved product direction, not implemented or visually verified behavior.

- Arrange three cards left to right: Interactive, Beginner, Advanced. Each has a visual preview and a short description beneath it.
- Interactive previews building through gameplay; Beginner previews approachable creation tools; Advanced previews deeper menus and controls.
- ChaosFox suggested GIFs showing these experiences. Use short animated demonstrations of the actual supported experience; the final media format remains an implementation choice. Do not present planned capabilities as existing footage.
- Fade from the logo into the cards, with a brief explanation and a clear “Change modes anytime” note. Selecting a card chooses the preferred starting experience; it does not create a separate project or lock the creator into a workflow.
- Allow mode switching from within each mode while preserving the same project, authored work, and hidden advanced settings.
- Keep startup responsive: animation must not impose an artificial loading delay. Provide reduced-motion/static previews, avoid distracting simultaneous playback, and preserve the Starlight dark palette and smooth edges.

### Future menu simplification — 23 September 2026

ChaosFox identified the hamburger menu as cluttered and requested an eventual cleanup, potentially moving groups of options into new submenus in the top-right corner. Keep the interface simple as capabilities grow. This is recorded direction, not an implemented menu redesign.

- Proposed organization: keep project/file actions in the hamburger menu and group creation, assets, and gameplay actions into clearly named top-right menus. These categories are a starting proposal, not a settled layout.
- Use shallow groups and recognizable labels; avoid replacing one long menu with excessive icons or deeply nested menus. Beginner shows common actions first; Advanced reveals additional controls without losing settings.
- Preserve access to existing actions, shortcuts, Undo/Redo, and authored content. Check discoverability and layout in both windowed and fullscreen modes, with consistent Starlight dark styling and smooth rounded edges.
- Update menu documentation and walkthrough checks when actions actually move. Keep this future cleanup separate from outstanding live verification of the current controls.

### Game-type presets and mode continuity

Game-type selection should configure sensible supported defaults. Beginner exposes a small understandable set of adjustments; Advanced reveals deeper configuration and flexibility. Interactive makes creation itself a playable experience. All three modes operate on the same project and gameplay definitions. Switching modes must preserve hidden/advanced settings and authored content, not reset or fork it.

Prefer selecting the game type at project creation so Play and export agree. Later preset changes should preview their effects and preserve user work. Export validates the selected configuration and dependencies; selecting a genre does not automatically implement missing gameplay. Additional genres follow the proven first path.

### Non-negotiable baseline: confidence, clarity, and control

ChaosFox explicitly added these three principles to the no-compromise baseline, alongside native performance, responsiveness, and visual polish. They are acceptance requirements for future work, not claims that all current interfaces already meet them.

- **Confidence:** creators can understand what an action will do, see its result, and recover from mistakes where applicable. Help is available without embarrassment or an assumed level of expertise; Advanced users retain access to basic explanations.
- **Clarity:** controls and lesson titles use understandable language. A clearly labeled Help/Tutorial entry point remains visible and consistently located in Interactive, Beginner, and Advanced modes. Restarting or re-enabling tutorials must not be buried in nested menus.
- **Control:** tutorials can be skipped, stopped, resumed, or re-enabled easily. Provide a catalog of clearly named sections so creators can jump directly to what they need rather than replay everything. Preserve work and learning progress; help must not force an unwanted workflow.

Tutorial access must remain available in every mode, with relevant lessons for that mode and access back to fundamentals. Accessibility must not become interruption: keep guidance optional and avoid repeated unsolicited prompts. Verify discoverability by checking that someone who skipped the tutorial can find help, select a particular lesson, and return to creating without losing work in each mode.

### Three practical design rules

Implementation update: the native editor now includes a persistent labeled Help / Tutorials button and F1 access, eight searchable text lessons, Back/Next/Restart, Close/Escape, and per-user saved lesson positions. It uses shared dark panel controls and is independent of Beginner/Advanced/walk-and-build state. This is optional reference guidance, not an interactive tutorial overlay or automatic task completion tracker. Core checks pass (551 checks in tutorial-progress-20260923-171745), including saved-progress recovery and stable lesson IDs. The editor builds; eleven new visible walkthrough checks are prepared but have not run, and live appearance/input verification remains open. Saving preferences failing leaves guidance usable for the current session and reports that limitation when navigating.

These rules turn `STARLIGHT-CORE-PILLARS.md` into implementation and review criteria. They describe required behavior, not completed verification.

1. **Help is always one clear action away.** Keep a visibly labeled Help/Tutorial control in a consistent place across all three modes. It opens a lesson catalog with understandable titles, direct section selection, and Resume. Skipping guidance must never hide the way back. Verify with a fresh-user journey: skip the tutorial, find a specific lesson, and return to the project without losing work. Guidance stays optional.
2. **Changing the experience preserves the creation.** Switching modes preserves the project, selection where applicable, authored content, and hidden Advanced settings. Editing actions provide predictable Undo where applicable; actions that could discard work explain the consequence before it happens. Verify mode round trips with actual edits and Advanced settings, plus Undo/Redo and save/reopen. Future mode controls must not imply capabilities that are not implemented.
3. **Every interaction must feel immediate and look finished.** Menus use clear labels and shallow groups; controls keep consistent dark styling, readable text, and smooth edges in both window modes. Switching menus and scrolling must not flash unfinished backgrounds or introduce avoidable waits. Animation explains a transition without delaying input or startup and supports reduced motion. Measure responsiveness on a stated workload and inspect the visible result; a successful build or check count alone does not establish polish. Preserve the intentional 500 FPS ceiling and the existing native-performance requirements.

### Acceptance requirements

- A nonprogrammer can build the small example through the editor, save/reopen it, play it, export it, and give the result to someone else to launch.
- Native performance, responsiveness, visual polish and Starlight dark colors remain mandatory. Preserve the intentional 500 FPS ceiling; no artificial active-work delays, upscaling or generated frames used to disguise poor native performance.
- Maintain scene integrity, predictable Undo where applicable, and compatibility with existing content. Validate focused changes; preserve failed evidence.
- User-facing goals, implemented code, verified behavior, visual acceptance, known defects and future plans remain separately reported.
- Complete Interactive/Beginner/Advanced modes are not yet delivered. This is the approved implementation direction and completion bar, not a feature-complete claim.

## The idea

### Engine scope and GameProject — ChaosFox's clarification, 18 September 2026

**Starlight is intended as a general-purpose engine, not an engine built only for GameProject or its showcase slice.** GameProject is a separate game project powered by Starlight. Its opening can demonstrate engine capabilities and inform development priorities, but must not redefine or replace the broader engine product direction.

Future assistants must preserve this distinction when reading slice plans or estimating delivery. Scene-specific content and choreography belong to GameProject; reusable engine capabilities belong to Starlight. Do not present a special-case scene implementation as completion of a general engine system, or silently discard wider requirements to meet a showcase deadline. Earlier suggestions to bound the slice were planning proposals, not authorization to narrow Starlight's purpose.

Building the engine out remains the broader objective. This clarification does not assert that the engine is complete, define a final feature-completeness checklist, or require every future capability to be finished before any GameProject milestone. Engine milestones and game-slice milestones need separate scope and evidence.

Starlight should offer three ways to create. Interactive mode is a building game made with Starlight's own capabilities: the user builds inside a playable experience, and Starlight reconciles that work into the environment or project being developed. In ChaosFox's words, it is “a game where you build games.” The aspiration is to build freely and bring the result into the project at any point.

| Mode | User's intended experience | Status |
|---|---|---|
| Interactive | Create through a playable building experience; reconcile the construction into the current environment/project. | Proposed. Exact interaction, reconciliation and gameplay-authoring scope remain to be designed. |
| Beginner | Tutorials, basic controls and guided creation, building on the existing approachable interface. | Proposed complete mode. Current simple controls are a foundation; a complete tutorial curriculum is not established by the analysis. |
| Advanced | A development experience for experienced developers. | Proposed complete mode. Existing advanced controls are a foundation, not proof of a delivered separate mode. |

The original spoken proposal did not specify progression locks, VR, multiplayer or AI generation. ChaosFox's subsequently uploaded sculpting proposal explicitly adds a terraforming tech tree, procedural/AI tools, VR and collaboration as ideas for assessment. See the [complete feasibility and sequencing review](D:/projects/Starlight-Engine/STARLIGHT-SCULPTING-FEASIBILITY-2026-09-18.md). These are proposed extensions, not delivered functionality or a finalized requirement to gate ordinary developer tools behind progression.

## What reconciliation needs to mean

The confirmed intent is that construction in Interactive mode becomes useful project/environment content. Merely walking through a preview would not fulfill that intent.

The following is a **proposed interpretation for later design**, not a settled specification: all three modes work on the same underlying authored project. A user could construct a bridge interactively, refine its materials or behavior in Advanced, and return to Interactive without losing those refinements. Beginner would teach the same concepts through simpler controls.

Design questions remain open:

- Do edits update the project immediately, or become a reviewed, undoable commit? What does switching modes do with unsaved work?
- Which data can be created and changed interactively: geometry, terrain, materials, physics, scripts, game rules, UI, audio? Environment construction alone does not establish complete game creation.
- How are intentional construction edits separated from temporary physics, damage or other simulation changes?
- How are advanced properties preserved when a simpler mode cannot display or edit them?
- Does reconciliation preserve editable objects and relationships, or create derived assets? How do save, undo and conflict handling work?

“Build whatever we want” describes the ambition; supported content types and limits must be defined and tested before claiming unrestricted creation.

## Comparison research — 18 September 2026

**Finding: the broad concept has strong precedents. Do not claim that no other engine/platform offers it, or that Starlight invented game-like game creation.** This is a focused official-source comparison, not an exhaustive survey of all engines, plugins and research. Documentation establishes the capabilities described below; no competitor was installed or tested for this review.

| Engine or platform | Relevant documented capability | Relationship to the proposal |
|---|---|---|
| Unreal Engine | Play/Simulate workflows let developers modify actors and retain supported edits with Keep Simulation Changes. [Epic documentation](https://dev.epicgames.com/documentation/unreal-engine/playing-and-simulating-in-unreal-engine) | Existing precedent for bringing some running-world changes back to authoring. This workflow alone does not establish a complete building-game interface. |
| Fortnite Creative + Unreal Editor for Fortnite (UEFN) | Creative supports building/editing/saving inside Fortnite. Epic describes Live Edit as letting Creative changes update a UEFN project while playtesting. [Fortnite developer tools](https://www.fortnite.com/developer/tools) | The closest documented overlap with playable construction feeding a more powerful editor. UEFN targets Fortnite; distinguish it from stock Unreal Engine and from a general-purpose independent game project. |
| Unity | ProBuilder provides geometry construction and level-design tools inside the editor. [Unity ProBuilder manual](https://docs.unity3d.com/Packages/com.unity.probuilder@6.0/manual/index.html) | Relevant authoring tools, but this source does not demonstrate the proposed integrated three-mode building-game experience. It also cannot establish that no Unity plugin or Unity-built product supplies one. |
| Godot | Godot 4.4 introduced embedded game windows and interactive in-game editing, including selecting scene elements and exploring with the camera while running or paused. [Godot release documentation](https://godotengine.org/releases/4.4/) | Real overlap in interactive runtime inspection/editing. The cited feature is not evidence of a complete building game, automatic persistence of every runtime change or the proposed three-mode project workflow. |
| Dreams | Creation tools let people make games and other content, with learning material and sharing inside a game/creation platform. [PlayStation's Dreams page](https://www.playstation.com/en-gb/games/dreams/) | A direct precedent for the broad “game where you build games” concept. It does not establish the same native Starlight project model. |
| Resonite | In-session development tools create worlds and interactive props; ProtoFlux supports game/system logic with immediate feedback and collaboration. [Resonite features](https://resonite.com/features) | A strong precedent for building and programming within the world itself. General independent game export or parity with Starlight's proposed project workflow is not established by this source. |

## Potential distinction and honest positioning

The opportunity is the quality and integration of the experience: enjoyable in-world construction, approachable teaching, and advanced development as first-class ways of working on one editable general-purpose project. Keeping advanced work intact across mode changes would make that promise concrete. This is a differentiation hypothesis to validate, not a verified unique market position.

Suggested wording for the **vision**: “Starlight aims to let you build through play, learn through guided creation, and develop with advanced tools—within the same project.”

Avoid “the first engine where you can build games inside a game,” “no other engine has this,” and “already implemented.” A future comparison should demonstrate actual workflows, preservation of project data, supported game logic and distribution boundaries before making stronger claims.

## Relationship to current Starlight

The current knowledge snapshot describes scene placement, terrain/sculpt tools, simple/advanced control disclosure, persistence and undo, physics/flow, preview sessions, and a separate project host. Those are possible building blocks. Existing preset reconciliation in the project-host workflow is a different feature and must not be mistaken for the proposed Interactive mode. Existing Play preview is likewise not proof of project-authoring round trips from a building game.

Keep this direction separate from the source-supported feature count and saved test outcomes. The analysis tool carries a dated copy of this document into its knowledge pack so future assistants understand both what exists and what ChaosFox wants to create next. Update this document when ChaosFox refines the idea; implementation status needs separate source and test evidence.
