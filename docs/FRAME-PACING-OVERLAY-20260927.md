# Visible motion and frame timing monitor

The editor now enables a translucent, click-through timing overlay by default. F8 or File > Motion & frame timing toggles it. A two-second frame-time graph shows CPU render-completion intervals, with median, 95th/99th percentiles and worst interval. Labels identify each character in the camera view and the player, with observed transform changes per second. Camera changes are measured separately. Unchanged objects are marked STATIC, not treated as failed animation. Labels are projected into the camera frustum; this is not an occlusion query.

These rates are distinct: a renderer can submit 400 frames while an object supplies 40 different transforms. The overlay measures submitted transforms, not monitor scanout, skeletal joint animation, shader-only effects or input-to-photon latency. It does not change gameplay timing or fix stutter. A rolling two-second reading includes idle intervals and should be interpreted in context. Sampling occurs every rendered frame; the overlay display refreshes ten times per second. Instrumentation overhead remains present while enabled.

Verification: normal Release build passed. Core runner passed 531 checks, including six new cases proving independent 400-frame/50-pose rates, percentile calculation, a single 100 ms hitch, stationary classification and expiration. Fullscreen 2560x1440 battle passed all 44 foreground checks with the overlay visible. Saved capture dimensions were independently verified. Evidence: D:/projects/Starlight-analysis/pacing-overlay-battle-20260927/report.json and ui-0.bmp through ui-43.bmp. Final two-second sample: 421.34 rendered FPS; player, enemy and camera 39 updates/s; frame median 2.45 ms, 99th percentile 2.88 ms, worst 3.67 ms. Other live samples showed spikes; this final window does not summarize the whole run.

An initial invocation accidentally selected the general UI check suite; that owned scratch process was stopped and its partial output preserved at pacing-overlay-visible-20260927. It is not counted as a passing suite. The intended battle used --ui-walkthrough. A source text encoding issue caught in the first visible run was repaired and Release rebuilt afterward.

Next work remains: measure normal-interaction cadence independently of the scripted test pump, then address motion continuity and frame pacing. Do not infer that the visual mismatch has been fixed or move to 4B.


## Ongoing pacing and multicore instrumentation

The working tree now contains frame-driven UI updates, transient raster mesh uploads,
continuous asynchronous JSONL timing capture, bounded final-deadline pacing spin,
and caching of unchanged scene packets. These are ongoing performance changes;
they are not a claim that frame consistency is solved. 4B is untouched.

`STARLIGHT_PACING_LOG` chooses the output file; otherwise recordings go under
application data, Diagnostics/FramePacing. Every recorded frame includes interval,
scene build/upload, render call, GPU query, simulation/combat/physics/presentation,
fence/pacing/present waits, input receipt/sample ages, sequence identifiers and events.
Input ages are software proxies, not input-to-photon latency. GPU queries are delayed.
The overlay's rolling graph, object readings and low calculations are also stored.
The 1% and 0.1% lows use 1000 divided by the mean of the slowest ceil(N*fraction)
frame intervals. The analyzer includes startup by default; filtered windows are explicit.

The multicore panel measures the renderer's existing parallelFor dispatcher, including
persistent and legacy workers and serial fallback. It shows the latest rendered frame's
parallel/serial batch counts, peak participants including the caller, batch wall time,
and elapsed time after the caller's chunk until dispatch returns. Per-slot elapsed
callback times are recorded for every frame (slot zero is the caller). These are not
physical-core utilization or thread CPU-time counters; elapsed values include preemption,
and overlapping workers must not be summed as frame time. Zero batches means that
frame did not use this instrumented pool, not that the whole engine is single-threaded.
Collection is disabled with the timing monitor. Schema version 2 adds multicore readings.

The next visible battle test is paused at the owner's request while discussing multicore.
The new cache/multicore changes have not yet had a visible battle validation.

Verification: Release build and all 540 native checks passed, including actual worker dispatch, serial fallback, disabled telemetry, per-worker logging roundtrip and cache packet equivalence. Visible battle remains paused.


### Parallel battle presentation

Player and actor telegraph meshes now build from a read-only presentation snapshot
using the existing PersistentWorkers implementation, with up to four participants
including the caller. Each task owns its output; actor-order merging preserves the
serial packet byte-for-byte. Single-job frames stay serial. This does not pin actors
to CPU cores and does not parallelize shared combat state or the physics world.
`STARLIGHT_BATTLE_WORKERS=0` selects the same build's serial control for comparison.
Schema 3 records battle worker readings separately from renderer-pool readings.
Battle values repeat with updateSequence; the analyzer deduplicates observed updates.
The visible comparison resumed after the owner explained and authorized this change.
Release and all 542 native checks passed, including parallel/serial geometry equivalence.


Same-build fullscreen 2560x1440 comparison (one run each, overlay visible):

| Measurement | Serial control | Parallel battle |
|---|---:|---:|
| Mean visual task preparation | 0.2475 ms | 0.1734 ms |
| Visual preparation p99 | 0.7174 ms | 0.6143 ms |
| Full-run average FPS | 482.26 | 481.46 |
| Full-run 1% low | 322.69 | 325.81 |
| Full-run 0.1% low | 217.80 | 211.87 |
| Foreground battle checks | 44 passed | 44 passed |

Evidence: Starlight-analysis/pacing-battle-serial-control-20260927 and
pacing-battle-parallel-20260927. No dropped or malformed timing records. The task-stage
mean improves about30%, but end-to-end pacing does not materially improve in this pair;
no smoothness victory is claimed. Startup remains in full-run results. Core542 passes.
Parallel run screenshot ui-14.bmp reviewed: readable worker panel and actor labels at1440p.

Physics review: deep/Physics/PhysicsWorld.cpp uses Jolt with four job threads, rigid
bodies, tiled terrain, contact handling, buoyancy and CharacterVirtual collision.
Rigid bodies use60Hz accumulated steps; character motion substeps at most1/120s.
This does not certify a complete physics feature set. Recorded character-physics cost
is ~0.044ms parallel/~0.048ms serial mean, versus scene build/upload ~0.835/~0.873ms.
The simple battle is insufficient evidence about physics scalability in heavy scenes.


Publication checkpoint: the anonymized publication copy also passed its own Release build, all 542 native core checks, and the audit analyzer contract suite. These checks do not certify that remaining frame-pacing issues are resolved. The current source checkpoint includes Step 4/4A and subsequent performance work; the earlier clean-Windows export evidence applies to its earlier tested binary.
