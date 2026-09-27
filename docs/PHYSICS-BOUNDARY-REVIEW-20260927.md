# Physics boundary review — 27 September 2026

Focused review complete; exhaustive boundary acceptance remains open. No engine behavior was changed in this pass.

Release build passed. The core suite passed 771 checks, including 16 new boundary checks in tools/checks/physics_boundary_checks.inl. Local evidence: D:/projects/Starlight-analysis/physics-boundary-core-20260927/result.json. No additional visible test was required for these test-only changes.

| Area | Expected boundary behavior | Evidence |
| --- | --- | --- |
| Liquids | Fixed x/z domain [-12,12); exterior and non-finite queries are dry/stationary. Exterior impulses do not change volume or waves. | New executable checks pass. |
| Fracture topology | Nonmanifold input produces no source-shaped pieces; malformed indices fail validation. | New executable checks pass. |
| Shell fracture | At most 8000 source triangles; exact limit retains every face in at most 64 thickened groups; above limit returns no pieces. | New executable checks pass. |
| Unsupported gameplay fracture | Original object is removed and 16 generic fragments are emitted when source-shaped fracture fails. These are not matching physical mesh pieces. | Source review: AdventurePlayer fracture fallback. End-to-end unsupported-mesh regression remains open. |
| Projectiles | Capacity 128, oldest retired first; reset empties the pool. | New executable checks pass. Existing swept-impact checks cover thin walls and moving targets; contacts use conservative bounds. |
| Smoke emitters | First 16 eligible sources in scene order; removing one admits the next. | New executable checks pass. |
| Fog field | 64x32x64 cells over x/z [-16,16], y [-2,14]. Outside-field rendering uses analytic source density, not transported simulated density. | Shader source review; existing GPU tests cover drift, decay, obstacles and reset. Exact domain-edge GPU regression remains open. |
| Fog obstacles | Up to 128 bodies, player first then scene objects/debris; additional bodies are omitted from this field. | Source review; existing GPU evidence covers eight moving obstacles, not capacity overflow. Overflow GPU regression remains open. |
| Splash droplets | Bounded at 1024; reset clears the full pool. | New executable checks pass. |
| Rope authoring | Segment count 0 or 2–64 accepted; negative, 1 and 65 rejected. | New executable checks pass. Existing physical rope tests cover motion/collision/reset. |

## Remaining acceptance

The review identifies explicit limits rather than claiming universal physics. Three focused regression gaps above remain: unsupported-mesh gameplay fallback, fog domain edges, and fog obstacle overflow. Comprehensive mixed-world and visible editor-control acceptance also remain open. Preserve the GPU attack-fill implementation. Mesh/texture upgrades follow physics acceptance; performance comes last.

## Follow-up verification

All three targeted follow-up checks were exercised: unsupported gameplay fracture in core772, and field edges/obstacle overflow in seven fullscreen GPU checks. See PHYSICS-CLOSEOUT-CHECKS-20260927.md for exact assertions and limits; the earlier open-gap statements above describe the initial review checkpoint.
