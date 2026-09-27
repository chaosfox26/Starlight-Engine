# Starlight — Claude handoff, 27 September 2026

## 1. Read this first

1. The owner requested a handoff and explicitly stopped further testing. Do not resume tests merely because this document describes them. Wait for the owner's next instruction.
2. The supported physics implementation and focused closeout fixtures passed. The subsequent full Analysis Tool **Run both** finished with **11 passing stages and two failing stages**. The whole audit did not pass.
3. Full audit: `run-20260927-145737-11584-42661687`. The owner ran it through the GUI. Preserve that run, its failures and captures; never overwrite evidence.
4. The next priority is resolving those two audit failures when authorized, then Task 2 mesh/textures, then Task 3 performance consistency. Step 4B has not started.
5. This is bounded game physics, not every conceivable physics system. No percentage-complete or universal correctness claim is supported.

## 2. Repository and evidence locations

1. Development source: `D:/projects/Starlight-Engine`. This contains private original history and has no publication remote. **Never push this history.**
2. Separate anonymized publication source: `D:/projects/Starlight-Publish`; private GitHub destination: https://github.com/Chaos/Starlight-Engine. Publish sanitized deltas only, preserving dependency credits and existing licensing boundaries.
3. Local handoff folder: `D:/projects/Starlight-Claude-Handoff-20260927`. Start with `README.md`. Its `latest-analysis-original` folder contains unchanged top-level analysis data and failure details, with a SHA-256 manifest. These local originals are not for GitHub publication.
4. Full original audit and all screenshots/binaries: `D:/projects/Starlight-analysis/run-20260927-145737-11584-42661687`.
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
