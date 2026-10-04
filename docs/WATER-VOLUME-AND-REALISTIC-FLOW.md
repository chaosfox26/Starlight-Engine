# Water volume and realistic flow

Saved October 3, 2026, from the owner's Starlight and GameProject discussion. This is a future design and investigation reference, not an implementation or performance claim. Claude's active fracture, flicker and lava repair continues separately; this document does not authorize changing that work.

## The immersion problem

the owner reports that water looks like a surface sheet: at the edge, or when looking beneath it, there is no convincing visible body of water. A splash sound says the player entered water while the underwater view appears empty. Those cues contradict each other.

The requirement is water that reads as a body with depth from above, from the side and while submerged. Its surface, underwater visibility, lighting, sound and movement must describe the same water. Existing waves or caustics alone do not satisfy this requirement.

This is an observed visual deficiency, not proof that the simulation stores no water volume. The inspected flow code already stores water amounts, terrain depth, currents and liquid snapshots. the owner's observation establishes that the current presentation is insufficient.

## Physical behavior we want

Realistic flow means water responds coherently to gravity, pressure, resistance, terrain and moving objects. Water should conserve its amount except at explicit sources, drains or other modeled transfers. It should flow downhill, collect, redistribute when displaced, and respect solid boundaries. Objects entering it should disturb it and receive appropriate fluid forces.

Numerical fluid methods approximate these rules at a finite resolution. Incompressible simulation uses pressure to correct velocity so fluid does not unnaturally compress or expand; solid and free-surface boundary conditions determine its interaction with walls and air. Surface tracking is an additional part of the problem. [Bridson and Müller-Fischer's fluid simulation notes](https://www.cs.ubc.ca/~rbridson/fluidsimulation/fluids_notes.pdf) explain these foundations.

Water and lava may share geometry-building infrastructure without sharing identical behavior. The inspected Starlight implementation already separates their transport rates and currents, with heat, cooling, crust relief and glow for lava. Further optimization must preserve that distinction. Sharing a triangle-generation routine is not itself a physical error, nor evidence that either liquid is a complete realistic model.

## Surface and underwater rendering

Represent where water begins and ends, its depth along a viewing ray, and whether the camera is inside it. Underwater rendering needs depth-dependent light absorption and scattering, appropriate visibility and color, and consistent treatment of the underside of the free surface. Refraction, reflections, waves and caustics must agree with those boundaries and the simulated state.

A closed boundary mesh, depth representation or another volume representation could supply the required information. A water volume need not be filled with triangles. Select the representation after examining the current renderer and the cases it must support; adding thickness to the top mesh alone will not create realistic flow or underwater optics.

A single height per ground position cannot describe every free surface: overturning waves, falling sheets, separated droplets and multiple layers of water require additional representation. Three-dimensional simulation and surface tracking can handle a broader range of motion, at greater cost. [NVIDIA's real-time 3D fluid chapter](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-30-real-time-simulation-and-rendering-3d-fluids) describes a grid-based approach and free-surface treatment; it is technical background, not a current Starlight benchmark.

## Proposed prototype direction

Investigate broad terrain-flow simulation for large bodies and detailed three-dimensional simulation near important interactions, waterfalls and breaking surfaces. This is a proposal, not a selected architecture or an existing capability.

The difficult boundary is exchanging mass and momentum between regions. Moving a detailed region must not duplicate or lose water, introduce seams, or change collision behavior. Simulation timestamps, geometry and interaction inputs should remain compatible, especially with the current asynchronous liquid mesh pipeline.

Compare plausible grid, particle and combined methods against the actual scenarios. Retain promising experimental approaches, but judge them through working prototypes and measured physical and visual behavior. Do not choose a method solely because it is traditional or novel.

## Small demonstrations for a later pass

1. Enter a pool from above and move the camera below its surface. Verify continuous underwater appearance, sound and movement.
2. Inspect a finite pool from its edge and side. Verify believable boundaries and depth instead of a floating sheet.
3. Drop a rock into water. Verify displacement, waves, fluid forces and conserved water amount.
4. Remove a barrier or alter terrain. Verify water redistributes into the opening without leaking through remaining solids.
5. Prototype a waterfall or overturning splash to establish where the height-based model needs three-dimensional support.
6. Exercise interaction, reset and repeated disturbances while checking mesh/simulation generation consistency and stable frame pacing.

Use reference footage and observable physical checks alongside aesthetics. The art direction remains Sculptural Anime Realism: tangible, detailed and stylized rather than an obligation to imitate a photograph.

## Performance and acceptance

Keep the accepted native-resolution baseline, 500 FPS ceiling, 8× MSAA and 16× AF, with no upscaling, frame generation, TAA or intentional motion blur. Preserve existing material detail, water waves and caustics, physics and gameplay. Measure simulation, surface generation, uploads, rendering, memory use and interaction latency separately.

The all-scene target remains 300+ FPS. Water's current proximity to the ceiling means improvements may appear as steadier frame times or additional capacity rather than a higher average. More complete fluid behavior adds real work: this document does not promise fully physically accurate water at 500 FPS.

Open choices include supported liquid scenarios, spatial extent and resolution, representation of underwater boundaries, fluid/solid coupling, exchange between simulation regions and acceptable numerical error. Resolve those through measured prototypes after the current repair pass, then preserve each accepted improvement as part of the new baseline.
