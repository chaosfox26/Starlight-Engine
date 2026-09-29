# Prompt for Claude Code — Starlight continuation

## A. Owner's request and start condition

Take over Starlight Engine development in coordination with my existing assistant. Read this document and workspace instructions before acting. I want steady, efficient implementation, concise plain-English communication, accurate verification and no redundant work. Do not restart completed systems or label plans as implementations.

If I provide this for orientation only, read and report readiness without starting tests. If I ask you to resume using this prompt, begin with the two known full-audit failures. Respect my newest direct instruction and explicit pauses. This file does not schedule unattended work.

## B. First work package when authorized

1. Read applicable AGENTS.md/CLAUDE.md and recovery instructions. Check actual checkout, branch, HEAD, working changes and active processes. Reconcile newer evidence against this snapshot and preserve the other assistant's edits.
2. Read the exact failing reports. The latest full audit is 11/13 passing, not all green. Focused physics fixtures passed separately.
3. Investigate fullscreen telemetry: the status control had live FPS text and positive dimensions, but its unobstructed test returned false. Inspect actual layout, z-order and the hit-test assumption. Determine whether the problem is the UI or the test.
4. Investigate denoiser object motion: moved prop guide pixels were finite and present, but mean motion magnitude failed the 0.1 threshold in native mode. Check packet identity/transforms, previous-frame state, frame advance and readback timing. Earlier focused passes do not invalidate this failure.
5. Make the smallest justified correction. Preserve failed evidence. Do not remove assertions, reduce thresholds or substitute fixtures merely to obtain a pass. Explain justified test corrections separately from engine fixes.
6. Finish the build before running its binaries. Run relevant focused checks, then required broader verification. Visible tests must stay foreground; stop on focus loss. Do not run heavy background builds during visible performance checks.
7. Continue the authorized package through verification and a coherent checkpoint. A progress summary is not completion. If blocked, record the precise blocker, attempted approaches and next action; do not loop indefinitely or expand scope.

## C. Completion criteria

1. Each failure has an evidence-backed explanation and verified correction, or a clearly documented unresolved blocker.
2. Reports identify exact source/build revisions and pass/fail/not-run outcomes. No failure is erased by a different test passing.
3. No user scene, authored setting, unrelated change or private history is lost.
4. Handoff records changed files, evidence, limitations, active processes and the next concrete step.
5. When publication is authorized, sanitize deltas into the separate publication checkout, validate, push normally and verify the remote revision. Never push private development history or recovery archives.

## D. Source navigation

1. Native Windows C++ editor/player, DirectX 12 renderer and Jolt-backed physics DLL. Editor Play and exported gameplay share systems but are distinct execution paths.
2. editor/cpp/adventure.hpp: gameplay, projectiles, combat, fracture. adventure_controller.cpp: character/shared-body integration. physics.cpp and deep/Physics/PhysicsWorld.cpp: simulation wrappers/native world.
3. editor/cpp/body_constraints.hpp: joints and rope presentation. flow.hpp/flow.cpp: surface liquids. native_session.cpp: runtime presentation, effects and fog routing. deep/engine/atmosphere.hlsl: GPU fog.
4. tools/checks/physics_boundary_checks.inl: boundary tests. visible_ui_checks.inl: editor/fog checks. editor/cpp/player_gameplay_checks.inl: exported gameplay/mixed-world checks. tools/checks/mixed_physics_fixture.py: saved fixture.
5. tools/render/check_denoise.py: failing object-motion assertion. tools/audit/deep_analysis.py, knowledge_report.py, audit_engine.py and deep_analysis_gui.cpp: analysis interpretation, execution and GUI.

## E. Build and evidence references

Use the correct checkout and absolute build paths to avoid selecting another CMake cache. Development build: D:/projects/Starlight-Engine/out/classic. Publication build: D:/projects/Starlight-Engine/publication/out/verification.

```powershell
python tools/build/Build-Native.py --config Release --build-dir D:/projects/Starlight-Engine/out/classic
& ./out/classic/Release/Starlight.NativeCoreChecks.exe <fresh-output-folder>
$env:STARLIGHT_GATE0_COMPONENT='physics-behavior'
& ./out/classic/Release/Starlight.NativeSessionChecks.exe <fresh-output-folder>
python tools/checks/physics_presentation_check.py out/classic/Release/Starlight.Physics.dll
```

Replace placeholders before execution. Commands are references, not instructions to repeat all checks immediately. For full analysis use the current Starlight Analysis Tool GUI and select Run both. The builder's launch with an explicit output directory previously auto-started feature-only mode; that does not equal Run both. Check for existing runs before launching.

## F. Priority and working style

Resolve audit failures, then Task 2 meshes/textures, then Task 3 frame-time consistency. Cloth, ragdolls, soft bodies and full 3D fluids are optional separate projects, not silently added scope. Alternate assistants through committed checkpoints and explicit file ownership. Keep updates brief; do not stop at a milestone while authorized work remains, but honor pauses and real blockers.

## G. Full factual handoff

Included in full so this prompt does not depend on the prior conversation. Snapshot pauses reflect the handoff date; the owner's newest direct instruction controls whether work is resumed.

# Starlight — Claude handoff, 27 September 2026

## 1. Read this first

1. The owner requested a handoff and explicitly stopped further testing. Do not resume tests merely because this document describes them. Wait for the owner's next instruction.
2. The supported physics implementation and focused closeout fixtures passed. The subsequent full Analysis Tool **Run both** finished with **11 passing stages and two failing stages**. The whole audit did not pass.
3. Full audit: `run-20260927-145737-11584-42661687`. The owner ran it through the GUI. Preserve that run, its failures and captures; never overwrite evidence.
4. The next priority is resolving those two audit failures when authorized, then Task 2 mesh/textures, then Task 3 performance consistency. Step 4B has not started.
5. This is bounded game physics, not every conceivable physics system. No percentage-complete or universal correctness claim is supported.

## 2. Repository and evidence locations

1. Development source: `D:/projects/Starlight-Engine`. This contains private original history and has no publication remote. **Never push this history.**
2. Separate anonymized publication source: `D:/projects/Starlight-Engine/publication`; private GitHub destination: https://github.com/ChaosFox/Starlight-Engine. Publish sanitized deltas only, preserving dependency credits and existing licensing boundaries.
3. Local handoff folder: `D:/projects/Starlight-Engine/Starlight Documents/Current/history/handoff-20260927`. Start with `README.md`. Its `latest-analysis-original` folder contains unchanged top-level analysis data and failure details, with a SHA-256 manifest. These local originals are not for GitHub publication.
4. Full original audit and all screenshots/binaries: `D:/projects/Starlight-Engine/analysis/run-20260927-145737-11584-42661687`.
5. Repository handoff: `docs/claude-handoff`. Its `analysis` folder contains selected text/JSON evidence. The publication copy is anonymized; embedded original hashes and paths describe original evidence, not transformed file bytes. The local raw folder is authoritative for exact-byte analysis.
6. Development checkpoints: `184c47d` physics closeout, `4349645` handoff/analyzer descriptions, `f81d1c1` full audit findings/reporting correction. Published checkpoint before this handoff: `a5a334d`. Use Git HEAD for the newer handoff commit; do not confuse development and publication hashes.

## 3. Implemented physics and motion

1. Player movement: walking/running, jumps and landing, posture/crouch/prone/slide, dodge/lunge, slopes/stairs, collision and moving-platform contact.
2. Grapple: traveling hook, attachment, climbing/mantling, moving anchors, swing/release momentum and obstruction handling.
3. Combat: melee/area/hitscan rules, traveling player/enemy projectiles, swept impacts, damage/control/team rules, physical impulses and impact sparks. Agreed Step 4A damage/health placeholders remain placeholders.
4. Objects: static, kinematic and dynamic bodies; gravity, fall, bounce, friction, density, stacking, pushing, starting velocity/spin and imported/sculpted collision. Dynamic pursuing actors retain gravity.
5. Constraints: fixed, hinge, ball, slider, spring and physical segmented ropes; connected-body removal and reset.
6. Destruction: supported primitives, convex/concave shapes, panels and hollow-shell fracture; source motion inheritance, tumbling rigid fragments, world/liquid interaction and cleanup.
7. Water/lava: terrain-bound transport, obstacle response, currents, waves, wind response, splashes, buoyancy/drag and swimming. Currents carry bodies and players.
8. Fog/smoke: GPU density transport, moving sources, prescribed wind/turbulence, buoyancy, obstacle exclusion, wakes, decay and pause/reset.
9. Fire/sparks/droplets: emitted particles with motion, lifetime, gravity/lift, drag/wind and conservative collision; water quenching and cleanup.
10. Lightning: changing/branching paths, traced contacts, moving targets, chains, damage, sparks and delayed spatial thunder.
11. Grass: rooted wind/contact deformation and recovery. Current asset quality is still placeholder quality.
12. Motion/presentation: interpolation between fixed physics steps; camera/object/effect monitoring and raster/ray-traced motion data. Prior focused motion checks passed, but the newest denoiser motion assertion failed and needs reconciliation.

## 4. What is missing or limited

1. Unsupported fracture is a real fidelity limitation: the original object is removed and sixteen generic fragments appear instead of matching rigid mesh pieces. Nonmanifold input is rejected by source-shaped fracture.
2. Hollow/open fracture uses thickened shells and conservative collision groups, not exact arbitrary solid-volume decomposition. Shell input is limited to 8000 triangles and at most 64 groups; active gameplay fragments are capped at 256 and expire.
3. Liquids are a 48x48 heightfield grid with 0.5-metre cells over a 24x24-metre region, x/z [-12,12). They are not overturning, general three-dimensional fluids.
4. Fog uses a 64x32x64 grid over a 32x16x32-metre region. Outside-field rendering uses analytic density. It has no pressure projection. Smoke sources are limited to sixteen, obstacle routing to 128 bodies. Full overflow ordering and every field edge were not exhaustively tested.
5. Projectiles/particles use conservative bounds rather than exact arbitrary concave mesh contacts. Projectile capacity is 128, oldest retired first. Droplets are capped at 1024. Rope segments are 0 or 2–64.
6. Cloth, deformable soft bodies and ragdolls are absent. New conductivity rules, joint motors and break-force behavior were not specified or implemented as part of this closeout.
7. The current editor walkthrough covers representative settings in each new control group, undo/redo, save/reopen and export; it does not test every possible field value or combination.
8. Rock/grass mesh replacement, higher visual detail and a complete texture/material quality pass have not started. Loader support is not equivalent to finished textured assets.
9. Expanded-world frame pacing is not accepted. Higher average FPS is insufficient: the owner wants consistent frame times and improved 1%/0.1% lows. Dedicated optimization remains last.
10. No fresh clean-Windows certification, exhaustive hardware coverage or long-duration soak claim accompanies this pass.

## 5. Checks completed this pass

1. Release build and **772 core checks**, including seventeen new boundaries: liquid edges/non-finite inputs/impulses, fracture topology/size/index limits and gameplay fallback, projectile overflow/reset, emitter admission, droplet cap/reset and rope settings.
2. **28 standalone physics checks**; presentation check: 120 render samples, fifteen simulation ticks, 112 displayed pose changes.
3. **Seven fullscreen GPU fog boundary checks**: exterior source cannot wrap into the field, positive-edge interior density is finite, 130 supplied obstacles are bounded to 128, exclusion remains valid and cleared slots disappear.
4. **Thirteen visible editor checks** across physical material, velocity/spin, rope, particles, lightning target/damage/chaining and projectile settings, with undo/redo and scene save/reopen. Exported scene exactly matches saved authored data.
5. **Six mixed-world exported checks** with all named systems present, observing movement/effects and GPU fog; pause/restart and scene replacement preserve authored settings and clear runtime state.
6. **Twelve packaging checks and 41 fullscreen exported gameplay checks** pass. Fullscreen gameplay tests used 2560x1440 and overlay enabled.
7. Anonymized publication copy independently built and passed all **772 core checks** before upload. Four analyzer contract suites passed before the full audit; the source-marker correction subsequently passed the analyzer regression suite.
8. These focused results remain evidence for their tested scope. They do not override the two later full-audit failures.

## 6. Full Analysis Tool failures — exact observations

1. **Fullscreen live telemetry:** `tools/checks/visible_ui_checks.inl`, `telemetryVisible()`, requires the status control to be visible, nonempty, unobstructed and contain FPS. The recorded control was visible, 2532x20 pixels, with text `Native engine · 496 FPS · GPU 0.38 ms · native resolution`. Only `unobstructed` was false. Foreground/capture/new frames passed; 459 frames rendered and renderer error was empty. Inspect actual control bounds/z-order and the hit-test assumption. Do not claim a rendering crash or focus loss, and do not remove the assertion just to pass.
2. **Denoiser object motion:** `tools/render/check_denoise.py` moves a prop through six scene packets, preserving identity, then reads guide buffer 5. Prop pixels were found and finite, but mean motion-vector magnitude did not exceed 0.1. This happened on native denoiser mode 0, after static variance, camera motion, history rejection and camera-cut reset checks passed. Later checks were not reached. Inspect packet-transform updates, previous-frame history and readback timing; do not assume all physics or all motion vectors are broken.
3. **Reporting correction, not runtime repair:** the new dynamic-world capability required a Markdown marker, but the capability detector indexes source files. That produced a false incomplete-source label. Removed that documentation marker and retained actual source markers. Regression suite passed; the historical report remains unchanged and has not been replaced by a fresh full run.
4. See `analysis/audit-summary.json`, `analysis/audit-findings.json`, `analysis/visible-editor-result.json` and `analysis/denoiser-result.json`. The full audit passes build, contract, core, diagnostics, managed-code policy, textures, classic UI, export, session and fallback stages.

## 7. Corrections and earlier failed attempts retained

1. Bounded the exported-game test message-pump batch so continuous frame messages cannot indefinitely prevent deadline/focus checks. This is test-harness logic, not a gameplay optimization.
2. The first mixed-world run was stopped after that message-drain issue. The next run missed splashes because the low falling prop could reach water during warm-up. Raised the fixture's falling prop to twelve metres so the water entry occurs during observation; the splash assertion remained required. The corrected mixed run passed.
3. Removed an obsolete export diagnostic claiming dynamic props cannot collide with the player. Existing shared-world contact checks contradict that warning.
4. Test compilation briefly failed on an inaccessible fragment-body member; the public presentation test now checks the exposed fragment mesh. Initial publication build attempts used the wrong cache directory; the existing publication `out/verification` build then passed. These failures were not reported as successful builds.
5. An initial feature-only analysis ran before the owner's full Run both and is not the final audit. Use the folder ending 61687 above.

## 8. Next steps when authorized

1. Diagnose the two exact full-audit assertions using their preserved evidence before editing engine code. Keep test corrections separate from engine fixes and retain failed reports.
2. Run only relevant focused checks after a fix, then rerun required full acceptance. Never relabel a failure as a pass or count not-run checks as passes.
3. Proceed to Task 2 visual quality: convincing rock shapes, grass blades/clumps, texture/material appearance, save/export preservation and visible owner review.
4. Proceed to Task 3 measured frame-time consistency on representative moving, textured scenes; retain GPU attack-fill reuse and continuous timing records.
5. Keep explanations short and plain, avoid duplicate checks and unnecessary background work. Preserve the owner's requested order.

## 9. Operational guardrails

1. Read workspace AGENTS.md and current recovery handoff before edits. Respect private-development/publication separation, shared working files and existing credentials/licenses.
2. Never alter the lifeline or unrelated projects. Never overwrite user scenes, failed evidence or source history.
3. Visible tests must stay foreground. Stop on focus loss. Do not concurrently run heavy builds/tests with visible performance checks.
4. Finish builds before running binaries from that build directory. Use fresh output folders and record exact revisions.
5. This handoff does not schedule work or send a message to another assistant. The owner will point Claude at the folder after the weekly reset.

## 10. What we can build using the existing foundation

1. Combat encounters with traveling player/enemy shots, impacts, control effects, moving enemies and reactive scenery. New weapons and encounter rules still need gameplay design and verification; engine support is not a finished game.
2. Traversal challenges using jumps, moving platforms, grappling, swinging and release momentum.
3. Physical puzzles using pushable or buoyant objects, constrained platforms, hinges, springs and ropes. Author each combination and check stability before treating it as supported gameplay.
4. Destructible scenery using supported fracture shapes and rigid debris. Choose compatible assets; arbitrary meshes still have the documented generic-debris fallback.
5. Reactive environments combining liquid currents, floating objects, moving grass, fog, fire, sparks and lightning. Existing interactions do not imply unspecified behavior such as electrical conductivity through water.
6. Saved and exported demonstrations with the timing overlay and recorded statistics, so both collaborators can compare visible behavior and measured results.

## 11. Improvements to the systems already present

1. First resolve the fullscreen telemetry overlap assertion and the denoiser object-motion assertion. Determine whether each problem is in implementation, fixture or measurement; retain evidence and do not weaken assertions to obtain a pass.
2. Improve destruction fidelity: make unsupported inputs discoverable during authoring, expand supported topology deliberately and reduce reliance on generic debris. Accurate arbitrary-mesh fracture remains additional work.
3. Improve collision fidelity where gameplay needs it, especially complex shapes and fast projectile/particle contacts. Keep physical collision and visible geometry aligned; measure costs before expanding expensive shape handling.
4. Improve fluid coverage and interactions within explicit budgets: domain edges, source/obstacle overflow, transitions, splashes and combined workloads. Larger regions and richer coupling need design and validation, not only larger constants.
5. Expand authoring coverage beyond representative values: invalid inputs, joint targets, extreme settings, undo/redo, scene replacement and export combinations. Existing checks are not exhaustive.
6. Improve asset quality in Task 2: convincing rock silhouettes, grass blades/clumps, detail where it helps, and real textures/materials. Higher polygon counts alone are not visual acceptance.
7. Improve frame pacing in Task 3: target spikes and the gap between average FPS and 1%/0.1% lows on representative moving, textured scenes. Preserve stable physics and smooth displayed motion; do not treat high GPU usage or allocated memory as success by itself.
8. Recheck long-running behavior, resource cleanup and clean-machine exports at appropriate milestones. These are future verification needs, not already completed acceptance.

## 12. Optional future systems requiring new implementation

1. Cloth for flags, clothing or hanging fabric: deformation, attachment and collision behavior beyond rigid bodies.
2. Ragdolls: articulated character bodies, animation-to-physics transitions and recovery behavior. Existing joints alone are not a finished ragdoll system.
3. Soft bodies: deformable objects that squash, bend or recover their shape; distinct from rigid-body motion and mesh fracture.
4. Full three-dimensional fluids for overturning waves and liquid filling arbitrary volumes. This is a substantially different solver and rendering problem from the current surface heightfield.
5. Additional electrical/material interaction rules, powered joints and break-force behavior, if the owner specifies how they should work.
6. These are possible development directions, not promises, completed features or automatic additions to the current task. Discuss priorities with the owner before replacing the agreed order. A good game does not require every possible physics system.

## 13. Alternating Codex and Claude work

1. The owner intends to alternate the assistants to maintain momentum. This document supports that workflow; it does not create a schedule or authorize unattended background runs.
2. At the start of each work period, read this handoff, the current Git status and the latest commits. Identify the active checkout and the other collaborator's unfinished changes before editing.
3. Have one writer own a given file/change at a time. If concurrent work is requested, agree on separate files or isolated worktrees and the integration point; do not overwrite another assistant's work.
4. At each handoff, record the exact revision, files changed, what passed, what failed or was not run, evidence locations, active processes, and the single next concrete action. Label assumptions and unresolved causes clearly.
5. Keep development and anonymized publication histories separate. Commit coherent verified changes; publish sanitized deltas through the existing workflow when authorized. Preserve failed reports and avoid repeat whole-engine checks without a reason.
6. Current state remains paused for the owner to resume implementation. The next concrete investigation is the two recorded full-audit failures. No new engine implementation or test run was requested by this documentation update.
