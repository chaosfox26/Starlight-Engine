# Physics and motion task list

Reconciled 27 September 2026 against source, current test reports and Step 4 acceptance. This replaces the duplicate incremental lists and stale unchecked entries. A checked item means implemented and tested within the coverage document's stated limits, not unlimited physical realism or every possible scene.

**Position:** the twelve supported feature groups and the bounded closeout fixtures now pass. See [closeout results](PHYSICS-CLOSEOUT-CHECKS-20260927.md) for tested scope and retained limitations. This is not a claim of arbitrary-scene correctness or every possible editor setting combination.

[Coverage and evidence](PHYSICS-COVERAGE-20260927.md) · [Collaborator handoff](PHYSICS-HANDOFF.md) · [Implementation history](DYNAMIC-ENVIRONMENT-IMPLEMENTATION-20260927.md)

## 1. Physics and dynamic behavior

### Implemented feature groups

- [x] Player movement: walking/running, posture, jump/landing, slope/stair/platform contact, dodge/lunge, control effects and time-based movement. Existing Step 4 acceptance plus current core/export checks.
- [x] Grapple: hook travel, wall attachment/climbing/mantling, moving anchors, swing/release momentum, obstruction and slow-frame handling.
- [x] Combat: existing melee/area/hitscan rules plus authored player/enemy projectiles, swept impacts, team/control/damage rules, physical impulses and impact effects. Dynamic pursuing bodies retain gravity.
- [x] Objects: static, kinematic and dynamic collision; imported/sculpted geometry, gravity, velocity/spin, friction/bounce, pushing, stacking and fast-body collision.
- [x] Constraints: fixed, hinge, ball, slider, spring and segmented physical ropes; limits, collision, connected-body removal and reset.
- [x] Destruction: supported primitive/convex/concave/panel/hollow-shell fracture, inherited momentum, tumbling fragments, world/liquid interactions and cleanup.
- [x] Water/lava: transport around obstacles, waves, wind response, splashes, buoyancy/drag, swimming and currents that carry objects and players.
- [x] Fog/smoke: transported density, moving emitters, wind/turbulence, buoyant smoke, obstacle exclusion, wakes, decay and pause/reset.
- [x] Fire/sparks/droplets: independent emission/motion/lifetime, gravity/drag/wind, contact response, water quenching and scene cleanup.
- [x] Lightning: evolving/branching paths, moving targets, traced contacts, chains, damage, sparks and spatial delayed thunder.
- [x] Grass: rooted wind/contact deformation and recovery. The better-looking meshes belong to Task 2.
- [x] Motion/presentation: continuous presentation between physics ticks, object/effect monitoring and raster/ray-traced motion data, including the tested Ray Reconstruction consumer.

### Integration already verified

- [x] Serialization and runtime/authored-state separation have core coverage; packaged scene replacement, pause and restart pass.
- [x] Current release build and 772 core checks pass (17 new boundary checks).
- [x] Standalone physics: 28 checks; fullscreen liquid: 8; fullscreen battle: 44.
- [x] Current exported game: 12 packaging and 41 fullscreen 2560x1440 gameplay checks. Includes dynamic-enemy pursuit/falling and simultaneous fracture/fire/water with cleanup.
- [x] Actual GPU fog fields: terrain, liquid, fire, dense moving sources, obstacle exclusion, pause/reset. Actual raster, ray tracing and reconstruction motion checks are retained.
- [x] Fixed-tick presentation: 120 render samples, 15 physics ticks, 112 presented pose changes.
- [x] Failed runs and bounded solver/collision assumptions are documented; no blanket clean-Windows or performance claim for this new build.

### Closeout verification results

- [x] **Comprehensive mixed-world acceptance fixture:** six saved/exported fullscreen checks pass with all named systems present, observed motion/effects, GPU fog sampling, pause/restart and scene replacement.
- [x] **Focused editor authoring fixture:** thirteen visible checks pass across new material, velocity/spin, rope, particle, lightning and projectile control groups, including undo/redo and save/reopen. Export preserves the scene exactly. Representative values were exercised; this does not certify every field value/combination.
- [x] **Targeted boundary closure:** 17 new core checks and seven fullscreen GPU checks cover the identified fracture, liquid, projectile, emitter, droplet, rope and fog cases. Unsupported fracture's generic-debris fallback is confirmed and documented.

[Exact assertions, failures retained and evidence](PHYSICS-CLOSEOUT-CHECKS-20260927.md). These bounded acceptance fixtures are complete; the fidelity limits below remain real limitations.

### Explicit fidelity limits and undefined extensions

- Liquids use a fixed 24x24 heightfield region, not overturning three-dimensional fluid.
- Fog uses a fixed 32x16x32 region with a 64x32x64 density grid, prescribed flow and buoyancy, not a pressure-solved fluid.
- Projectile/particle contacts use conservative bounds. Complex fracture has topology/triangle limits and uses thin shells for hollow forms; it is not exact decomposition of every arbitrary mesh.
- Cloth, deformable soft bodies and ragdolls are absent. No detailed authored behavior was established for them; they are separate scope decisions, not hidden completed items or an automatic expansion of this closeout list.
- New electrical-conductivity rules, joint motors and break-force rules were not specified. Existing chain/damage and constraint behaviors are implemented.

The retained GPU attack-fill change remains enabled. Task 2 follows physics acceptance; the dedicated performance pass remains Task 3. Step 4B has not begun.

## 2. Upgrade mesh, texture and environment quality

**Required outcome:** replace the visibly crude environment placeholders with convincing, detailed geometry and actual surface textures. The owner specifically identifies spiky-looking grass and unnatural rock shapes as problems. More polygons alone do not establish visual quality.

- [ ] Inventory placeholder meshes, untextured surfaces and visible quality gaps across supported environments.
- [ ] Improve rock silhouettes, proportions, surface structure and variation so they read as rocks; replace the current unnatural shapes.
- [ ] Replace spike-like grass with convincing blade/clump geometry, appropriate variation and surface treatment; coordinate its motion with Task 1.
- [ ] Upgrade meshes to the requested high-detail/high-poly quality where it improves their visible form, with sound topology, normals and texture coordinates.
- [ ] Provide actual textures and usable material support, including base color, surface normals and roughness, plus other channels appropriate to each material.
- [ ] Improve terrain and environment surface detail, scale, variation and material transitions so environments no longer look uniformly flat and untextured.
- [ ] Verify that asset import, editor preview, scene storage and exported games preserve the geometry, textures and material appearance.
- [ ] Use appropriate detail levels, culling, texture residency and memory budgets to retain quality while avoiding unnecessary rendering and upload costs.
- [ ] Review visible before/after results with the owner. A polygon-count increase or a passing build alone is not visual acceptance.
- [ ] Recheck frame pacing and memory use in representative scenes combining these assets with the moving/reactive systems from Task 1.

Status: task-list scope only. No asset replacement or implementation has begun. Specific art direction and asset sourcing remain to be established.

## 3. Return to performance optimization

Begin after Tasks 1 and 2, using the actual moving/reactive systems and upgraded assets as the workload. Retain the GPU attack-fill improvement.

- [ ] Establish representative fullscreen 2560Ã—1440 baselines with the overlay enabled and complete continuous recordings.
- [ ] Prioritize consistent frame times and bringing 1% and 0.1% lows closer to average FPS; a higher average alone does not satisfy this task.
- [ ] Measure CPU stages, worker activity, GPU work, synchronization, uploads, memory residency and object/effect motion updates to locate the remaining costs.
- [ ] Assess GPU utilization, clocks and power alongside CPU/GPU timing; do not use maximum utilization or allocated memory as success criteria by themselves.
- [ ] Optimize measured bottlenecks through appropriate parallel work, GPU execution, caching, resource reuse and asset/detail management while preserving the agreed behavior and visual quality.
- [ ] Verify with repeated matched comparisons. Keep full-run, startup and steady-play results clearly distinguished; do not hide hitches or substitute a short rolling sample for the complete result.
- [ ] Confirm that motion stays smooth with stable physics, and that improvements hold when movement, combat, dynamic objects, lightning, fog, particles and upgraded environments run together.
- [ ] Record results, regressions and remaining limits for owner review before calling performance optimization complete.

**Execution order: 1 â†’ 2 â†’ 3.** The owner has authorized implementation and verification. Task 1 is active; Tasks 2 and 3 follow it.
