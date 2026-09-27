# Dynamic environment implementation checkpoint

This is an implementation checkpoint for Task 1, **not full physics acceptance**. The owner authorized implementation after completing the task list. Keep the GPU attack-fill path enabled. Mesh/material replacement and the dedicated performance pass follow the physics work.

## Implemented paths

| System | Current implementation | Verification / remaining boundary |
| --- | --- | --- |
| Debris and impact particles | Gravity, relative-wind drag, restitution, terrain response, swept sphere versus conservative boxes, overlap recovery and bounded lifetime. Gameplay fragments now collide instead of falling through the scene. | Thin-wall sweep, engulfed-particle recovery, ground settling and lifetime checks. Cube, rectangle, trapezoid and octagon breakables now split into sixteen capped pieces from their source mesh; gameplay collision uses independent rotating Jolt bodies with terrain and character contacts. Other shapes still use generic debris; arbitrary concave and multi-material fracture remain open. |
| Lightning | Runtime discharge progression, changing branch seeds, transformed moving origins, nearest main-path object/terrain contact and independently moving impact sparks. Runtime geometry replaces the authored bolt during playback. | Core contact/reset/cache checks and foreground environment captures. Branch paths are not individually collision-resolved; electrical damage, chain rules, synchronized audio and electrical target reactions remain open. |
| Water and lava | Authored terrain water and lava patches initialize finite volumes in the existing terrain liquid solver. Playback updates obstacles, including translated routes. Simulated surfaces replace the original static surfaces. | Volume conservation and lava redistribution tests; visible integrated run. This is a bounded height-field transport model, not a full pressure/velocity fluid solver; mixing, waves, splashes and detailed two-way coupling remain open. Small lava footprints below the grid sampling scale need further treatment. |
| Fog and smoke | Two GPU density volumes retain history. Semi-Lagrangian transport, prescribed wind/turbulence, sources, decay, obstacle exclusion and tangential deflection feed the existing volumetric lighting pass. | Shader compilation and foreground raster execution passed. The simulated domain is 32×16×32 world units with 0.5-unit cells, bounded to 32 conservative obstacle boxes. Outside it, analytic density remains. Numerical GPU readback tests, pressure projection, terrain boundaries, larger scenes and full moving-emitter coverage remain open. |
| Grass and bolt deformation | Time-dependent vertex deformation. Grass roots and bolt endpoints are anchored; prior/current deformation times feed raster motion vectors. | Runtime shader compiled and captures reviewed. Normals, ray-traced deformation equivalence, and grass contact response remain open. |
| Rigid-body presentation | A separate presentation query interpolates position and normalized rotation between completed Jolt steps. The collision solver and existing raw pose query remain authoritative at 60 Hz. | Real-DLL check: 120 render samples, 15 physics ticks, 15 raw pose changes and 112 presented pose changes. This adds a one-tick presentation delay; it does not raise the physics frequency. |
| Monitoring | Object transform rate and effect-parameter update rate are distinct in the overlay and JSONL recording. | The effect reading measures the time parameter submitted for rendering, not GPU readback of individual vertices or solver convergence. |

## Evidence

- Release editor, player, physics and renderer build passed.
- 593 native core checks passed, including particle collision, lightning, finite liquid initialization, shared-world buoyancy, moving-surface momentum, moving lightning targets and separate effect telemetry.
- `tools/checks/physics_presentation_check.py` passed against the built physics DLL.
- The battle regression passed all 44 foreground checks at full-screen 2560×1440 with the overlay enabled.
- The first combined environment run passed eight foreground checks at 2560×1440; impact sparks were observed and liquid volume remained 46.725 units³. Its captures show the new lightning and spark geometry, grass, liquids and moving obstacle. These checks establish execution and selected invariants, not finished visual quality or complete fluid correctness.

Private raw evidence remains outside the repository in the local analysis workspace. No new clean-Windows certification or final exported-player acceptance is claimed for these changes.

## Open integration work

The original representative environment exposed substantial CPU liquid/scene preparation cost (approximately 52 FPS in one captured sample). Static geometry caching and liquid mesh reuse improved the comparable final rolling sample to approximately 163 FPS; this is not a complete-run pacing guarantee. Shared player/prop falling and pushing passed five fullscreen checks. The earlier battle throughput does not describe this workload. Preserve that evidence for Task 3 instead of claiming the new environment is optimized.

The broader Task 1 checklist remains open: complete movement/contact coverage, projectile and combat coupling, fracture beyond cube primitives, authoring of complete body/constraint/material settings, fluid fidelity, particle/effect lifecycle coverage, complete temporal rendering, and editor/export acceptance. Do not mark all physics implemented based on this checkpoint. Tasks 2 and 3 have not been completed.

## Fracture checkpoint

Cube fracture passes closed-volume conservation, source-piece preservation, shared-world creation, rotation/terrain response, expiry and restart checks. The initial cap winding failure is retained in the analysis workspace; source winding is normalized before cutting. Non-collision playback retains swept-particle fallback. Fracture currently covers unsculpted cube, rectangle, trapezoid and octagon geometry only, with no textured interior authoring or general concave decomposition.

The dedicated fracture run passed six fullscreen 2560x1440 foreground checks, including cut pieces, rigid rotation and individual debris monitor readings. Its capture was reviewed. Fragments preserve source color, inherit physical friction/restitution, expire after eight seconds and remain capped at 256 bodies. Shapes outside the supported primitive set retain generic debris; no arbitrary fracture completion is claimed.

Physical materials now expose friction and bounce in the editor, persist through scene serialization, validate their ranges, and apply in both standalone and shared character physics worlds. Older scenes retain the previous defaults. A real shared-world drop comparison verifies higher restitution produces a higher rebound. Damping, collision filters and constraint authoring remain open.

## Shared liquids and moving interactions

The renderer publishes an immutable sample of the simulated water/lava height and density. The gameplay collision world uses the latest sample to apply buoyancy and drag to dynamic props and registered fracture bodies on each physics substep. No simulation world is accessed concurrently. The transfer has up to one rendered-frame delay; this is not a full two-way pressure or fluid-momentum solver. Tests cover floating versus sinking, floating fracture bodies, removal, liquid density and rejection outside the simulation domain.

The eight-check fullscreen1440p liquid run passes with a light prop floating and a dense prop on the floor. The capture exposed cell-edge surface spikes: geometry now interpolates cell surface heights rather than combining interpolated depth with a different cell's obstacle height. A level-water-over-slope regression passes. The repeated visible run passes; coarse displacement mounds and final liquid visual quality remain open.

Moving-platform takeoff now retains horizontal carry across airborne frames. Wall jumps retain moving-anchor velocity; newly attached grapples clear the previous anchor velocity. Actual shared-world platform carrying/jump tests and grapple checks pass. Rotating-platform authoring, general constraints and comprehensive movement coverage remain open.

Lightning has an editor target-object selector backed by a saved stable object reference. During playback its target point follows that object's transform, with nearest obstruction/contact handling retained. Removing the target stops the discharge. Tracking, persistence and removal checks pass. Reusable group copies and scene merges remap internal lightning targets, and placement retains target-local offsets. Saving an emitter without its required target is rejected. Static editor-preview targeting, electrical damage/chaining and audio synchronization remain open.

Latest verification: Release build and 593 native core checks pass. The integrated moving-target environment passes eight fullscreen checks and the battle regression passes all 44 foreground checks at 2560x1440. The final follow-up build adds target-reference remapping and removes destroyed bodies from the contact cache; its core suite passes. No new export or clean-Windows acceptance is claimed.

## Traveling shots and moving fire checkpoint

The player basic attack now has an authored physical-shot mode: positive Shot speed emits a traveling projectile; zero preserves the existing instant sector attack. Shot gravity is independently configurable. Runtime shots use swept-radius collision against terrain and conservative moving object boxes, resolve the nearest impact, and retire on impact, twenty-metre range, ten-second lifetime or reset. Relative sweeps cover linearly moving targets; rotating and deforming targets still use conservative bounds. There is a 128-shot cap.

Damage and destruction occur on arrival using current target hostility, with wall obstruction, independently moving impact sparks, visible tracers and per-projectile monitor/log poses. Existing shot/impact event counters feed the existing audio path. This implements the player mode, not every enemy weapon, ricochet, penetration, homing behavior or all combat physics. Editor settings persist through scene save/load; old scenes keep instant attacks.

Fire playback replaces static cone geometry with continuously emitted flame particles using updraft, wind/drag, terrain/box collision, lifetime and inherited emitter motion. Removal stops emission while existing particles expire; reset clears all state. The existing volumetric/light emitter remains. This is a bounded procedural fire effect, not a combustion/heat-transfer solver, and visual material quality remains Task 2.

Release build and 607 native core checks pass, covering fast walls, crossing targets, gravity, delayed gameplay impact, scenery obstruction, serialization/reset and fire lifecycle. The seven-check fullscreen1440p projectile run passes with flight, monitoring and impact fracture observed. Added large liquid test fixtures now live on the heap to avoid the Windows test-thread stack limit; prior failures remain preserved. The eight-check fire/environment foreground run passes, and the unchanged instant-attack battle regression passes all 44 checks.

### Effect clock correction

The visible environment recording exposed shader effect readings around 64 updates/s despite substantially faster rendering. Viewport visual time still used GetTickCount64, whose coarse increments held shader poses across multiple rendered frames. It now uses the high-resolution steady clock. The final fullscreen environment run passes all eight checks and explicitly requires effect update rate at least 85% of measured render rate: final rolling sample 157.5 effect updates/s versus 157.21 rendered FPS. This reading validates submitted effect time, not GPU vertex readback or complete motion-vector correctness. Visual quality and full-run pacing remain open.

Latest evidence: physics-projectile-visible-20260927 (7 checks), physics-fire-visible-20260927 (8), physics-projectile-fire-battle-20260927 (44), physics-effects-clock-visible-20260927 (8). The final clock build passed; 607 core checks were passed before the renderer-clock-only change.


## Continued physics integration: characters, impacts, constraints and temporal data

Verified checkpoints, with failed attempts retained in the analysis folder:

- Player body bounds now participate in lightning interception/damage, particle collision, liquid-entry wave/splash emission and fog obstacle/wake inputs. Friendly lightning does not damage the player. Projectile momentum is applied at its contact point, producing both translation and rotation. Core PASS640: `physics-player-impulse-core-final-20260927`.
- Character buoyancy uses submerged capsule height and liquid density, with drag and upward swim input. Leaving liquid restores ordinary gravity. Equivalent 30/240 Hz inputs, flotation, swim input and exit checks pass. Core PASS644: `physics-swimming-core-20260927`. This adds swimming response, not a drowning/thermal-health system.
- Authored starting linear velocity and spin, plus hinge angle limits, are editable and saved. Both shared gameplay and standalone worlds initialize momentum from authored values. Core PASS648: `physics-authoring-core-20260927`.
- Optional segmented ropes use 2-64 capsule bodies connected by point constraints in the same Jolt collision world. Adjacent links ignore one another; nonadjacent links and world bodies collide. Load support, path publication, save boundaries and anchor removal pass. Standalone paths interpolate between fixed ticks; shared gameplay paths use current solved poses. Reset removes rope links/constraints. Core PASS652: `physics-segmented-rope-core-20260927`; standalone `physics-rope-reset-session-20260927`; fullscreen PASS6 `physics-rope-links-visible-20260927`. Zero segments preserves the prior endpoint-distance behavior. Segment count/radius are authored settings; runtime link coordinates are not serialized.
- Actor attacks may opt into traveling shots with speed, gravity and mass. Shots retain owner/team/control/accuracy data, skip their owner, physically collide, and apply damage only at impact. Player projectile hits preserve existing team and dodge rules. Core PASS656: `physics-enemy-projectiles-core-20260927`. Actor speed zero preserves existing contact attacks.
- SLVA scene packets carry stable per-vertex identities in addition to rigid part identities. Particle/debris/liquid/cable/player/weapon geometry retains prior rendered positions; resident GPU attack fills retain prior transforms. Actual GPU motion-texture readback verifies moving, stationary, reordered, particle and expanding resident-fill cases. Fullscreen PASS9: `physics-attack-motion-fixed-visible-20260927`. The initial test failed because the editor cleared manually published overlays every idle frame; cleanup now happens when gameplay presentation ends, clearing stale player collision state as well. This is temporal-data correctness, not the deferred performance pass or full temporal-consumer certification.
- Fullscreen battle regression after player/liquid/temporal integration passed all44 checks: `physics-integrated-battle-20260927`. Later rope/actor/audio additions still require their final integrated/export checks.

The broad physics checklist remains open. General concave/open fracture, fuller fluid/fog interactions, branch/contact fidelity, remaining movement/combat coverage and final lifecycle/export acceptance are not silently treated as complete. Spatial thunder has been implemented but is still undergoing the current checks.


## Continued integration: exported gameplay, physical bolts, terrain fog

- Packaged fullscreen 2560x1440 gameplay passed all 38 checks in `D:/projects/Starlight-analysis/physics-current-export-observed-20260927`, including new segmented-rope load, enemy projectile flight/impact, swimming buoyancy and spatial thunder. Overlay enabled; original authored content preserved; restart passed. This package predates the subsequent branched-lightning, continuous-liquid and terrain-fog changes. It is not a new clean-Windows certification. Earlier exported test failures are retained: a single end-of-wait projectile observation missed the flight interval; the corrected check observes the complete interval and proves visible flight precedes damage.
- Imported closed GLB solids now enter standalone physics as well as gameplay. Authored meshes without a Shape name are no longer skipped. Core667 and standalone imported-body falling checks pass.
- Concave star-shaped solids decompose into closed physical tetrahedra. The L-shaped fixture retains its volume and twenty source-shaped pieces; fullscreen `physics-concave-visible-20260927` passed six checks. General non-star-shaped decomposition is a separate subsequent batch still being checked.
- Lightning rendering follows the same traced 64-segment main/chain paths and 16-segment branch paths used for contacts. Paths vary by discharge, stop at terrain/objects/player and deduplicate damage per target. Three physical branches replace cosmetic-only branches. Core670 and fullscreen `physics-branched-lightning-visible-20260927` passed. Spatial thunder passed core661 and packaged playback/audio checks.
- Liquid buoyancy sampling is continuous across wet grid boundaries, with matching immutable gameplay snapshots; dry cells remain dry and different liquid types are not blended. Fire lift is integrated at particle substep cadence instead of once per rendered frame. Core674 and fullscreen `physics-continuous-liquid-visible-20260927` passed.
- Fog now receives the terrain height field, excludes underground density and deflects near-surface flow along slopes. CPU readback measures density inside both terrain and object boundaries. Fullscreen `physics-terrain-fog-visible-20260927` passed six checks using an elevated sloped terrain, proving source emission, drift, decay, solid exclusion and reset. The fixed 32x16x32 domain and prescribed velocity field remain documented fidelity limits; this is not an incompressible pressure solver.

- General concave fracture now traces cut-boundary loops, triangulates nonconvex caps, orients closed source shells and recursively decomposes them into closed convex collision pieces. U-shaped source produces three pieces with total volume7 and an unchanged opening; core680 and fullscreen `physics-u-fracture-visible-20260927` six checks pass. Cut planes coincident with original faces initially retained the wrong face on one side; that was fixed and failed evidence retained. Limits: 8000 source triangles,128 pieces,eight split levels; nested cut contours and open surfaces are rejected rather than falsely filled. This remains bounded decomposition, not universal mesh fracture.

- Reactive grass now retains rooted geometry and critically damped bend state, responds to wind and player/object contact, and recovers after contact. The same deformed mesh feeds raster and ray-traced rendering, with regenerated normals and stable motion identities. Shader deformation is skipped for already-simulated grass and lightning so visual paths are not offset a second time. Core683, combined environment8, ray-traced motion9 and raster motion11 pass (`physics-rooted-grass-environment-visible-20260927`, `physics-grass-raytraced-motion-visible-20260927`, `physics-grass-raster-motion-visible-20260927`). Grass asset quality remains task2; this is interaction and temporal correctness, not performance acceptance.
- Ray tracing had retained the old40-byte shader vertex layout after native vertices grew to56 bytes. The shader now consumes the matching history layout, and C++ has a layout assertion. Previous rigid transforms use stable object identities. Ray-traced velocity uses matched current/previous projections; the first new test exposed a roughly0.02-pixel stationary residual and the corrected calculation passes the unchanged threshold. Motion readback probes the actual ray-traced guide when that path is active. The native temporal denoiser consumed these guides in the fullscreen run; proprietary ray-reconstruction testing remains separate.
- Extended standalone physics checks pass: four stacked bodies remain settled, a200m/s body stops at a thin wall, hinge limits resist spin, and slider limits reject off-axis gravity. Evidence `physics-contacts-constraints-session-20260927`.

- Open authored panels now fracture into closed thin triangular-prism shards, retaining source surfaces instead of generic debris. Thickness is bounded to5-20mm and spatial grouping caps bodies at64 for up to8000 source triangles; grouped collision uses the existing convex body approximation. Core686 and fullscreen `physics-panel-fracture-visible-20260927` six checks pass.
- Ordinary editor Play without a Game controller now receives timed thunder events from the environment simulation. Scene epochs prevent replay after reset; focus/pause still stop audio. Gameplay continues using its own shared hit/audio stream, avoiding duplicate thunder. Core689 and environment8 pass at `physics-environment-thunder-visible-20260927`, with submitted audio buffers and zero device errors. Combined battle44 passed at `physics-complete-batch-battle-20260927` before the subsequent routed-body fixes.
- Found and repaired standalone routed colliders remaining at their initial positions while their visuals moved. They now use kinematic poses, retain authored routes and push dynamic bodies; `physics-routed-kinematic-session-20260927` passes. Shared gameplay platform orientation follows route heading. A common routed pose for collision/effects/camera/grapple/impacts is now under verification so rotated bounds match visible geometry.


## Debris/environment and liquid/fire integration

Fracture bodies expose transient world-space collision proxies with rotated bounds. The proxies participate in projectile and lightning tracing, flame/spark collisions, liquid contact impulses and fog body wakes. Projectile impulses reach the actual fragment body. Scene replacement clears these proxies. Core729 and fullscreen fracture6 passed, including an explicit renderer-side environment-body count.

Water now extinguishes existing flame particles and suppresses submerged emission without accumulating a deferred burst. Lava retains fire; reset clears particle state. Core checks cover flooding, sustained submersion, lava transition and reset. Fog air boundaries now combine terrain with the actual simulated liquid surface. Core731 and fullscreen liquid-fog6 passed, checking emitted density, advection, decay, zero density inside excluded volume and reset. Evidence: physics-debris-environment-visible-20260927, physics-debris-water-fire-fresh-core-20260927, physics-liquid-fog-core-20260927 and physics-liquid-fog-visible-20260927. These are interaction checks, not task3 performance acceptance.


## Moving smoke, fire plumes and pause synchronization

A separate render-thread source update now follows routed smoke emitters without rebuilding their geometry. Fire emits transported smoke while burning; submerged or disabled fire emits neither flames nor smoke. Warm smoke has a bounded upward velocity derived from transported density, so detached plumes keep rising. Core734 and fullscreen fire-smoke6 passed.

Environment shader time now follows the simulation clock in NativeSession rather than wall-clock time. Fullscreen smoke pause/resume11 passed: two paused GPU field readbacks are identical, resuming continues transport, removing the source preserves drifting/decaying smoke, and reset clears it. Evidence physics-smoke-pause-resume-visible-20260927. Packaged gameplay38 passed at physics-integrated-current-export-20260927 before this smoke/clock batch; final package rerun remains required. Moving light/fire/targeted-lightning source updates are building next.


## Physical fracture momentum and live contacts

Fracture now samples source point velocity and angular velocity from the live collision body. Each piece inherits translation plus rotational velocity at its own center, along with source spin and the existing break impulse. Routed fracture uses the currently posed center. Core736 passes the point-velocity check. Gameplay impact sparks now collide against current moving-object, debris and player bounds instead of the original static-scene bounds. Moving fire and targeted-lightning lights are refreshed from current poses; water suppression applies to fire light as well as flame and smoke emission. Core735 verified the source movement and quenching; combined battle44 passed before the subsequent momentum batch.


## Current integrated verification

Full Release rebuild, core736 and standalone physics-behavior checks pass. Current-world environment8, simulation-clock ray-traced motion9, dense moving-smoke11 and current packaged fullscreen gameplay38 pass. The dense fixture uses16 routed smoke emitters and8 moving obstacles and proves actual density transport, exclusion, pause/resume, source removal/decay and reset. Package creation/rejection/content integrity adds12 passing checks. The environment screenshot was inspected: visible branches and sparks, moving fire particles, fog, water/lava and reactive grass are present; mesh/material quality remains task2, and its roughly100FPS sample is not performance acceptance.

Evidence directories: physics-final-interaction-core-20260927, physics-final-interaction-session-20260927, physics-clock-rt-motion-visible-20260927, physics-current-world-visible-20260927, physics-dense-moving-fog-visible-20260927, physics-final-world-export-20260927. This is current supported-system evidence, not a universal physics or clean-Windows certification.


## Hollow surfaces and between-tick presentation

Closed objects with through-holes now use source-shaped physical shell pieces when solid cutting cannot cap their topology. Every original face is retained with a thin closed-prism thickness, grouped into at most64 bodies. Torus checks preserve its visible opening and source-face count; core739 and fullscreen hollow-fracture6 pass, including tumbling and renderer-side environment collision bodies. This fallback approximates material as a shell and collision as a group hull, rather than claiming exact interior volume or arbitrary topology support. Shell input remains bounded at8000 triangles.

The real physics DLL presentation check passes120 render samples at480Hz:15 solver ticks,15 raw-pose changes and112 presented-pose changes, with bounded monotonic falling and normalized rotations. Physical presentation remains independent of solver tick frequency.


## Exported grapple regression and liquid forcing

The hollow/liquid export run failed its15th visible check, Grapple hook and wall attachment, with foreground retained. This failure is preserved at physics-hollow-liquid-export-20260927. A deterministic core reproducer alternating1/480-second and0.1-microsecond updates exposed premature detachment: a single update below collision movement precision was treated as an obstruction. Grapple now requires120ms of sustained blocked motion, clearing the timer on progress. Tests prove near-simultaneous timer/render callbacks still attach and a genuine intervening body still causes safe release. Core743 and the complete physics-grapple-wind-fixed-export-20260927 packaged run38 passed after that correction; failure reports now preserve gameplay clock, position and tether state.

TerrainFlow now accepts horizontal wind forcing. A zero-mean input drives the actual water wave solver without changing volume; removing the wind lets those waves dissipate. Native environment playback uses the same default horizontal drift as fog. Core743 includes wind conservation and decay. Debris collision proxies also obstruct liquid transport, beyond their earlier splash/impulse participation. The strengthened fullscreen water-fracture test now requires actual splash particles rather than wave energy that ambient wind could also produce.

Follow-up corrections currently under verification: grapple pulling uses the full accepted50ms elapsed interval to avoid overshooting its attachment point on slow frames; liquid entry compares the previous body bottom against the previous water surface so a rising/displaced surface cannot hide a falling-body crossing.

## Slow frames, moving water contact and projectile contact verification

Release build and748 core checks pass. Fullscreen water fracture passes six checks with actual splash particles required. Projectile pause updates now leave overlapping shots intact; terrain impacts report the sampled slope normal; trajectories match at480Hz and50ms updates. Packaging passes12; refreshed visible gameplay is running. The separate sustained-obstruction grapple fix already passed38 exported gameplay checks. No performance or universal physics completion claim.

## Liquid-current coupling (verification in progress)

Actual water/lava cell transfers now publish horizontal fluid velocities. Immutable snapshots carry these velocities to gameplay, and both rigid-body paths use them in buoyancy drag. Immersed characters accumulate current drift smoothly, with collision resolved by the existing controller. Dry cells pass a neutral height to the native character API instead of negative infinity. Current grids use heap storage after the first added core run exposed a Windows stack overflow; its failed evidence is retained. Tests cover conservative flow direction, snapshot agreement, prop/player transport and standalone current response. Build/core rerun pending; do not count this batch as verified yet.

The earlier combined-world package passed all40 fullscreen checks, including fracture/fire/water simultaneously and clearing those systems on scene replacement. That package predates current coupling. Particle zero-time steps now preserve overlapped particles and passed749 checks before the current batch.

Current coupling verification: Release build passes,754 core checks pass,28 standalone physics checks pass, and fullscreen liquid8 passes. Core checks now include dry-cell character recovery. The native module uses its existing C++17 map lookup compatibility. Current grids remain heap-backed after the retained failed stack-overflow run. Final battle and refreshed package verification follow.

## Dynamic combat-body drive (verification in progress)

Actor movement previously called the kinematic movement API even for an authored dynamic object, disabling its gravity. AdventureController now drives such a body through linear velocity while retaining dynamic collision and existing vertical velocity unless vertical control explicitly requests displacement. Ordinary kinematic actors keep their existing path. A focused real-controller regression check verifies simultaneous steering and falling. The liquid-current map is cleared with body-world resets. Latest current-enabled package passed40 fullscreen checks before this drive correction; new build/core checks follow.

Dynamic-body drive verification: Release build and755 core checks pass. The fresh package passes12 packaging checks and41 fullscreen gameplay checks, including an actual pursuing dynamic enemy that falls and contacts terrain. Presentation interpolation remains112 visible pose changes across120 render samples and15 physics ticks. No tests remain running at this checkpoint. All failures remain recorded; full-physics acceptance is not inferred from test counts. All changes are local and uncommitted.
