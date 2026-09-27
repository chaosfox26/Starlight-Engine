Full numbered handoff: [Claude handoff](claude-handoff/README.md). Local folder: `D:/projects/Starlight-Claude-Handoff-20260927`. Testing is paused at the owner's request.

# Physics collaborator handoff — current checkpoint

## Current position

The twelve supported physics groups and bounded closeout fixtures pass. Development checkpoint 184c47d contains boundary tests, editor-control verification and the comprehensive saved/exported mixed world. See PHYSICS-CLOSEOUT-CHECKS-20260927.md for exact scope and retained failures. Older notes claiming three untested closeout groups are superseded.

## Next work

1. Mesh, texture and environment quality (Task 2): convincing rocks/grass and actual materials. This has not started.
2. Performance consistency (Task 3) after the visual work: prioritize frame times and 1%/0.1% lows. Keep GPU attack fills enabled, fullscreen 2560x1440 and the overlay on.
3. Step 4B remains untouched. Do not restart completed physics implementations or equate bounded acceptance with universal physics.

## Verified baseline

Release/core772; standalone physics28; fog boundary7; representative editor controls13 and exact exported scene match; combined packaged world6; packaging12; full exported gameplay41. Presentation:120 samples,15 ticks,112 displayed changes. Current physics check runs passed; no new clean-Windows test or expanded-world performance target is claimed.

## Limits and fixes

Unsupported fracture produces generic debris. Liquids are heightfields, fog has a bounded field, and some collision shapes are conservative. Cloth, soft bodies and ragdolls are absent. Authoring checks exercise representative values per new control group, not every possible combination. The test harness message pump was bounded to avoid starving its deadline. An obsolete warning about dynamic bodies lacking player collision was removed. Preserve earlier failed reports.

## Full analysis and publication

The full Run both analysis finished: 11 of 13 stages passed. Fullscreen live telemetry and denoiser dynamic-transform motion vectors failed. Investigate those before claiming full audit acceptance; see FULL-ANALYSIS-20260927.md. The separate physics closeout fixtures remain passing evidence for their stated scope. Publish only anonymized deltas to the separate publication repository. Never push private development history or raw recovery archives. Handoff updates are repository documentation, not a message sent to Claude.

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
