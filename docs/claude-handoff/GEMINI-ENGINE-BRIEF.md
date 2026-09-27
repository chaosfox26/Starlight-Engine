# Starlight Engine — comprehensive briefing for Gemini

## A. Review request

Review this snapshot and explain what Starlight can do now, its limits and useful next work. Treat the following as project-supplied evidence, not an audit you personally performed. Do not claim repository access or test execution unless separately provided. Separate observations, hypotheses, missing evidence and recommendations. Do not invent benchmark comparisons, completion percentages or timelines.

Return a numbered review covering capabilities, limitations, diagnostic hypotheses for the two failures, evidence to distinguish those hypotheses, prioritized work packages and acceptance criteria. Explain optional future systems and what gameplay actually requires them. Lead with plain English, then technical detail where useful. This is a request for analysis, not permission to upload files, contact others or change anything.

## B. Executive snapshot and architecture

As of 27 September 2026, twelve documented physics/motion groups have implementations and passing focused checks within stated limits. A saved/exported mixed scene ran the systems together. The latest full Analysis Tool Run both finished with **11 passing stages and two failing stages**. This is not an all-pass engine audit or universal physics completeness.

1. Starlight is a native Windows C++ engine with a DirectX 12 rendering path and Jolt-backed simulation. It has editor authoring/Play workflows, an exported standalone player, scene serialization, diagnostics and an analysis tool.
2. Rendering includes raster and ray-traced paths, native temporal denoising and optional native-resolution Ray Reconstruction. Source availability is not proof of correct behavior on all hardware or workloads.
3. Authored data stores scene/object/environment/gameplay settings. Runtime motion and damage should not alter that source state. Undo/redo, save/reopen, export, pause and reset require separate checks.
4. Gameplay coordinates movement, projectiles, combat, rigid bodies, fracture and interactions. Stable simulation steps and interpolated presentation serve different purposes. High render FPS alone does not prove smooth object motion.
5. Liquids, particles and GPU fog use distinct bounded models; they are not one unlimited general fluid solver. The renderer also needs correct previous-frame motion data for denoising, even when physics itself is behaving correctly.
6. The overlay/logging measures frame times, average FPS, 1%/0.1% lows, camera/object/effect rates, worker activity and stage costs. Prior geometry reuse and GPU-resident attack-fill work reduced repeated preparation, but expanded-world frame pacing is not accepted.
7. Current assets remain visually basic. The owner wants convincing rocks, grass, textures and materials, then performance consistency. No fresh controlled comparison against commercial engines is established.

## C. Detailed capabilities, limitations and verification

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


## D. Opportunities and improvement roadmap

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


## E. Evidence interpretation and questions

1. The latest audit is run-20260927-145737-11584-42661687. Ask for a specific attached report if needed; local paths in this snapshot do not give you access to that computer.
2. Fullscreen telemetry was visible and updating, but the unobstructed-control assertion failed. Real overlap and a hit-test assumption are competing possibilities, not established causes.
3. The denoiser found finite pixels for the moved prop but insufficient motion magnitude. Renderer history/update behavior and fixture/readback timing need investigation. This does not by itself prove all motion vectors fail or physics stopped working.
4. Earlier focused passes and later failures can coexist because conditions differ. Propose discriminating checks rather than dismissing either result.
5. The generated dynamic-world capability warning was a source-index error: a Markdown marker was required in an index of source code. That detector was corrected and regression-tested. The historical report remains unchanged; this was not a runtime fix or new full pass.
6. Avoid interpreting absent cloth, soft bodies or ragdolls as implemented, or assuming every game needs them. Explain dependencies and tradeoffs for concrete gameplay goals.
7. Preserve the owner's order: audit failures, visual quality, then measured performance. Any different order is a proposal, not an agreed plan.
8. This document is a detailed snapshot, not an independent source audit, long-duration test or hardware certification. State confidence based on the evidence actually supplied.
