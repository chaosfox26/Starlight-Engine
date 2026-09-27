# Physics closeout checks — 27 September 2026

## Results

- Release build and 772 core checks pass (17 new boundary checks).
- Seven fullscreen 2560x1440 GPU fog boundary checks pass: exterior-source isolation, positive-edge interior density, 130-obstacle input bounded to 128, finite/excluded density and clearing obstacle slots.
- Thirteen visible editor checks pass: material, starting velocity/spin, rope, particles, lightning targeting/damage/chaining, projectile speed, undo/redo and save/reopen. The exported scene matches the saved scene exactly. These exercise representative values in each new control group, not every possible value or combination.
- Six fullscreen 2560x1440 mixed-world packaged checks pass with the overlay enabled. The saved/exported scene contains player/enemy combat, traveling shots, destruction, constrained and falling bodies, water/lava, grass, lightning, fire and sixteen smoke emitters. Observations confirm fracture, splash, fire, projectile, rope, falling-body, lightning and grass activity; GPU fog remains finite and excludes bodies. Pause, restart and replacement preserve authored data and clear runtime effects.
- 28 standalone physics checks pass. Presentation check: 120 render samples, 15 simulation ticks, 112 displayed pose changes.
- Twelve standard export checks and 41 foreground fullscreen exported-game checks pass.

## Corrections and retained failures

The first mixed run was stopped after the test runner kept draining frame messages without returning to its deadline. The harness now processes a bounded batch before checking time/focus. The next run observed every required activity except splashes: the initial falling prop could enter water during warm-up before observation began. The fixture now drops that prop from 12 metres, placing the water crossing inside the observation interval; the splash assertion remains required. Both earlier reports are preserved. This is a fixture timing correction, not a relaxed physics assertion.

Removed the obsolete export warning that dynamic bodies cannot collide with the player; current shared-body collision is covered by existing physics checks. A scratch test world intentionally has no adventure finish object, which remains correctly reported.

Unsupported nonmanifold breakables are confirmed to disappear and emit sixteen generic fragments, not source-shaped rigid pieces. This fidelity limit remains explicit. GPU fog tests verify the bounded routing/field behavior above, not arbitrary-domain fluid simulation or every overflow ordering. Liquid heightfields, conservative collision bounds and fracture-size limits remain documented. Cloth, soft bodies and ragdolls remain absent and outside the specified implementation scope.

## Evidence

Reports are retained locally under D:/projects/Starlight-analysis/: physics-closeout-core-final-20260927, physics-fog-boundary-20260927, physics-authoring-closeout-20260927 (including export-controls.json), physics-mixed-closeout-v3-20260927, physics-closeout-session-20260927 and physics-closeout-export-20260927. Failed mixed reports are in physics-mixed-closeout-20260927 and physics-mixed-closeout-v2-20260927. No clean-Windows retest or expanded-world performance acceptance is claimed. Changes are local; this pass has not been uploaded.
