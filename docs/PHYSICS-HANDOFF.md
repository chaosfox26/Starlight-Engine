# Physics collaborator handoff

## Current position

The current supported physics feature set is substantially implemented. Read PHYSICS-AND-MOTION-TASK-LIST.md for the reconciled twelve feature groups and three open acceptance items. Do not restart finished implementations or interpret old unchecked/historical progress notes as the current worklist. The detailed coverage table and implementation history retain evidence and failed-run explanations.

## Continue in this order

1. Build the current checkout, then implement the comprehensive mixed-world acceptance fixture described in the checklist. Keep fullscreen 2560x1440, the overlay visible, and GPU attack fills enabled. Stop a visible run if foreground is lost; preserve failed evidence.
2. Complete focused authoring/undo/save/reopen/export verification for newly added controls. Fix actual failures rather than marking serialization tests as equivalent UI evidence.
3. Close the documented boundary matrix and final acceptance. Keep unsupported systems and solver fidelity limits explicit. Mesh/texture quality follows; performance optimization comes last.

## Verified baseline

- Release build and 755 native core checks.
- 28 standalone physics checks, 8 fullscreen liquid checks, 44 fullscreen battle checks.
- 12 packaging checks and 41 fullscreen exported-game checks. Includes dynamic pursuing enemies retaining gravity, fracture/fire/water together, scene replacement, pause/restart and unchanged authored content.
- Physics presentation check: 120 render samples, 15 physics ticks, 112 presented changes.
- Actual GPU fog and raster/ray-traced/reconstruction motion checks are recorded in PHYSICS-COVERAGE-20260927.md.

Test counts are not feature counts or proof of arbitrary-scene correctness. The latest build has not been retested in clean Windows. No current performance target has been accepted for the expanded workload.

## Build and focused checks

```powershell
python tools/build/Build-Native.py --config Release --build-dir out/verification
& .\out\verification\Release\Starlight.NativeCoreChecks.exe <fresh-report-directory>
$env:STARLIGHT_GATE0_COMPONENT='physics-behavior'
& .\out\verification\Release\Starlight.NativeSessionChecks.exe <fresh-report-directory>
python tools/checks/physics_presentation_check.py out/verification/Release/Starlight.Physics.dll
python tools/checks/check_adventure_export.py --runtime out/verification/Release --output <fresh-export-directory>
```

Finish builds before launching checks that load the same binaries. Run visible checks individually. Use fresh evidence directories; do not replace failed reports. See the source harnesses for visible modes and exported gameplay arguments. Source paths and report names in coverage notes refer to the development machine; captures are not stored in GitHub.

## Ownership and publication

Coordinate changes to shared files; do not discard another worker's edits. The publication repository contains anonymized source and separate history. Never merge the original private development history or private archives into it. Preserve dependency credits and licensing boundaries. Use the public sample identifiers already in this checkout.
