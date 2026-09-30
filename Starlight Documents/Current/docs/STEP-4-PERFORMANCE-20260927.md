# Combined combat performance — 27 September 2026

The recorded fullscreen mock battle slowdown is resolved for the measured local workload. The same 44-exchange foreground scenario improved from **7.47 to 398.01 average rendered FPS**. This is a workload-specific result on this desktop, not an engine-wide performance promise or a newly agreed target.

| Measurement | Before | After batching |
|---|---:|---:|
| Weighted rendered FPS over paced checks | 7.47 | 398.01 |
| Median check FPS | 6.40 | 402.39 |
| Lowest check FPS | 3.15 | 353.26 |
| Measured check time | 40.95 s | 40.77 s |
| Scene rebuild/upload wall time | 40.52 s | 5.13 s |
| Scene updates | 246 | 1,599 |
| Mean rebuild/upload time per update | 164.72 ms | 3.21 ms |
| Median reported GPU time | 0.393 ms | 0.404 ms |
| Foreground checks passed | 44/44 | 44/44 |

Check FPS is frame-count delta divided by elapsed wall time for each roughly 0.9-second check. Minimum check FPS is **not** a per-frame latency percentile. Timings include CPU work and synchronization in the existing scene-upload timer; they do not attribute every driver call separately. Startup and screenshot time outside each measured check are excluded consistently. Fullscreen captures are 2560x1440. Native resolution and the existing 500 FPS ceiling are unchanged.

## Cause and change

The placeholder presentation generated a separate mesh for almost every cone edge, fragment and weapon/body piece. A moving multi-cone battle changed hundreds of small meshes. The viewport hashes them, uploads new vertex/index buffers, submits them and waits for completion when the scene changes. Before this fix, measured scene rebuilding/uploading occupied almost the entire check interval despite GPU rendering around 0.4 ms.

`player_presentation.hpp` now combines world-space cosmetic meshes sharing exactly the same color and emissive material. Every vertex, normal, vertex color, triangle and depth offset is retained. Indices are offset when joining meshes. A single cone becomes two material batches instead of a mesh allocation for every edge. No attacks, damage rules, telegraph fill, simulation rates, resolution, raytracing settings or frame caps are removed or throttled.

The controlled change and repeated scenario establish that cosmetic mesh fragmentation was the dominant bottleneck in this workload. Broader workloads, raytracing tiers and arbitrary scene scale are not certified by this result.

## Verification and evidence

- Before: `D:/projects/Starlight-Engine/analysis/task4-battle-before-20260927/report.json`.
- After: `D:/projects/Starlight-Engine/analysis/task4-battle-batched-20260927/report.json`.
- Both runs passed all44 checks, retained foreground, showed simultaneous telegraphs and recorded damage to player and enemy. The unchanged scenario alternates damage and healing while actors move, pursue and dodge.
- After-run screenshot `ui-14.bmp` visually inspected: green and orange healing, hostile attack outline, moving actors, pistols and resource HUD retained.
- Normal editor/player build passed. `task4-batching-core-20260927/result.json`:937 checks passed, including index remapping/material preservation and bounded cone batching plus prior gameplay checks.
- Final export `D:/projects/Starlight-Engine/analysis/task4-export-final-20260927/gameplay-report.json`:33 foreground checks passed with captures, zero audio errors and authored state unchanged. Its separate `combat-report.json` passed1,210 frames, cone and damage checks in foreground. Clean-Windows validation remains a separate unavailable environment gate.

## Analyzer correction

The movement capability rule now looks for actual grapple stepping in `adventure.hpp`, where integration lives. A regression test accepts integration in that file and rejects a mere mention of grapple. All four analyzer contract scripts passed. Original September24 raw reports are preserved and are not rewritten to conceal the prior false negative. The full visible analyzer was not rerun as part of this focused fix.
