# Physics publication verification — 27 September 2026

The anonymized publication copy was built and tested separately from the development checkout. This checkpoint contains the current physics/dynamic-environment implementation, retained performance instrumentation and GPU attack fills, the reconciled task list, coverage inventory, and collaborator handoff.

## Passed in the publication copy

- Release build: editor, player, renderer, physics, checks and sample projects.
- 755 native core checks (`publication-physics-core-20260927`).
- 28 standalone physics checks (`publication-physics-session-20260927`).
- Actual-DLL presentation:120 render samples,15 physics ticks,112 presented pose changes.
- 12 fresh-package checks and41 foreground fullscreen2560x1440 gameplay checks (`publication-physics-export-20260927`), overlay enabled and GPU attack fills enabled. Includes dynamic pursuing enemies, fracture/fire/water together, scene cleanup, pause/restart and authored-content preservation.
- Changed source/documentation reviewed with the existing private-name substitutions and credential-pattern scan; no matches. Upstream credits and separate publication history preserved.

The initial copy attempt stopped on a documentation text-encoding mismatch, so its incomplete-copy build failed. The document was normalized, the complete source transfer repeated, and the full build/checks above passed. No incomplete copy is the release candidate.

## Reconciled completion state

Twelve named feature groups have working implementations and bounded evidence. Three acceptance groups remain: one comprehensive all-systems scene, visible authoring/undo/save/export coverage of new controls, and boundary/coverage closure. See PHYSICS-AND-MOTION-TASK-LIST.md for exact criteria. Test totals do not constitute a percentage, an arbitrary-scene guarantee, or full-physics acceptance.

Cloth/soft-body/ragdoll behavior and new electrical/motor/break-force rules were not specified or implemented. Fixed-domain fluids/fog and conservative collision/fracture approximations remain explicit in the coverage document. No new clean-Windows certification or expanded-workload performance acceptance is claimed. Mesh/texture replacement and the dedicated performance pass remain later tasks.

Large captures, generated builds and private recovery archives are not included in Git. Local report identifiers above locate evidence on the development machine; source harnesses are included for reproduction.
