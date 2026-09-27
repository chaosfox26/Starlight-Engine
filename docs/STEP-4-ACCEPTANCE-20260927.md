# Step 4/4A requirement sign-off — 27 September 2026

Local gameplay acceptance passed for the current agreed placeholder scope. **The overall milestone is not certified complete:** genuinely clean-Windows dependencies remain unverified. The combined-combat performance fix passed its measured local workload; see [performance evidence](STEP-4-PERFORMANCE-20260927.md). Final rigs/animation libraries and final health/damage balancing remain deferred by the owner. Steps 1–3 remain closed. No 4B work is authorized in this continuation.

Final validation after the performance change: normal editor/player build passed;937 core checks passed; `D:/projects/Starlight-analysis/task4-export-final-20260927/gameplay-report.json` passed all33 foreground checks with captures, zero audio errors and unchanged authored content. The earlier evidence below is retained as history. The normal installed editor uses these final binaries.

## Current evidence

- Normal Release build: `out/classic/Release`, completed 27 September. Shared editor/player/controller and presentation changes built successfully.
- Core: `D:/projects/Starlight-analysis/task4-presentation-core-20260927/result.json`, 935 checks passed. Includes current movement, all CC types, boss phases, input boundaries and new shot-event/audio waveform checks; count is not a feature count.
- Installed editor movement: `D:/projects/Starlight-analysis/task4-installed-movement-20260927/report.json`, eleven checks passed, every check foreground with captured screenshots. This reruns the corrected hook-flight/wall-attachment test.
- Weapons/audio: `D:/projects/Starlight-analysis/task4-weapons-visible-20260927/report.json`, six foreground checks passed, five audio buffers accepted and no audio API errors. First-person screenshot reviewed. Device acceptance and synthesized PCM validation do not establish the listener's speaker volume or subjective sound quality.
- Latest export: `D:/projects/Starlight-analysis/task4-export-20260927c/gameplay-report.json`, 33 foreground checks passed with renderer captures; original authored content unchanged, audio errors zero. Screenshots of first-person weapons, swing and control recovery inspected. Scratch scenes exercise the same packaged executable; these are not claims of every possible authored scene working.
- Previous export from the same gameplay source: `task4-export-20260927b/combat-report.json`, 1,159 rendered frames, live cone and damage exchange passed in foreground. The subsequent build extends acceptance/capture code only.
- Original specialized evidence below remains relevant to unchanged behavior, supplemented by the current shared-runtime core rerun. Those historical visible checks are not described as new runs.

## Requirement-by-requirement disposition

| Requirement | Disposition and evidence |
|---|---|
| WASD strafing, diagonals, continuous headings | Accepted locally: controller core checks cover heading/speed normalization; export continuous forward movement passed. |
| Independent left orbit / coupled right look / ordinary cursor after release | Accepted locally: shared camera/input core checks; export orbit, look and wheel checks passed. |
| Third-to-first-person zoom, obstruction, roll recovery and reduced motion | Accepted locally: core controller/camera checks; prior camera-boundary and creator evidence; current export first-person and movement screenshots. |
| Double jump, distinct presses, landing and moving platforms | Accepted locally: controller checks cover repeated input, stairs/platform carry/reset; current export first/second jump and landing passed. |
| Sprint 500, drain 15/s, regeneration 25/s and exhausted recovery | Accepted locally: exact-rate/resource boundary core checks; installed and export resource checks. |
| Directional dodge, V tap, double-tap, two charges, sequential 5-second refill | Accepted locally: input/resource/controller core checks; installed/editor and export damage-protected dodge. |
| Lunge arbitration, both charges, longer travel and twice dodge protection | Accepted locally: atomic-charge and duration/interruption core checks; installed/editor and export lunge. |
| Crouch/prone/slide, capsule clearance and standing safety | Accepted locally: controller low-ceiling/crawl/slide checks; current installed movement and exported posture checks. |
| Grapple launch/hook travel, charge-on-miss, held-input spending and 10-second refill | Accepted locally: grapple/resource core checks; corrected installed movement and packaged hook/attachment. |
| Wall pull, release rules, climb, wall jump and mantle clearance | Accepted locally: controller/grapple core, current wall-release/jump checks, historical `task4-visible-mantle-clearance-20260924` four foreground checks. |
| Swing and momentum-preserving release | Accepted locally: current packaged overhead swing, steering and release; component momentum checks. |
| Moving-character anchors, friendly safety, removal and collisions | Accepted locally: controller core and historical `task4-visible-moving-grapple-20260924` five foreground checks. |
| Shared Play/export behavior and separate authored/runtime state | Accepted locally: current 33 exported gameplay checks, authored-state equality, restart; prior creator save/load/Undo/Play-stop acceptance. |
| Original placeholder avatar, dual weapons, meters and sounds | Accepted for placeholders: shared temporary geometry; current editor/export screenshots; resolved-shot-only muzzle flash and sounds, shatter, stun chirps and landing thud. Full rigged choreography remains explicitly deferred. |
| 1 attack, Tab selection without firing, disabled 2–5/R | Accepted locally: action-slot core checks, current export selection/release cases. No additional skills exposed. |
| Committed 2.5-second player cycles / 1.5-second NPC Swipe | Accepted locally: basic-attack timing/release/repeat/cancel core checks, current exported shatter and separate live exchange. |
| Live caster cones, channel fill, four damage/healing colors | Accepted locally: core live-position/facing checks and historical live-cone/healing foreground checks; latest packaged healing and damage cases. |
| Partial target intersections, terrain-following cones, occlusion and all-target hits | Accepted locally: core geometry/LOS checks; historical `task4-visible-partial-cone-20260924` and `task4-visible-slope-cone-20260924`, seven foreground checks each. |
| Firing during permitted movement; dodge/CC cancellation | Accepted locally: controller/action core including grapple/air/platform combinations; current export cancellation/event and protection checks. |
| Shatter, bounded cosmetics and exactly-once linked objectives | Accepted locally: core rule/cosmetic checks; historical combat foreground evidence; current exported shatter/audio and authored preservation. |
| Smooth NPC pursuit, continuous turning, spacing and obstacle navigation | Accepted locally: core bounded displacement/navigation/turning; historical navigation nine foreground checks and turning five foreground checks. Combined battle performance is a separate open gate. |
| Friendly/self/unknown-hostility CC rejection | Accepted locally: shared hostile eligibility core; current export friendly rejection. No claim of live network authority/PvP verification. |
| One player CC, same-source cancellation, cleanse and five-second immunity | Accepted locally: source/expiry/cleanse/overlap core checks; current export stun expiry/immunity. |
| Stun | Accepted locally for control and placeholder presentation: core restrictions, birds/camera; editor/export chirps. |
| Knockback / pull / launch / knockdown | Accepted locally for physical motion, collision, firing lock and recovery: core and historical NPC-control foreground; current export physical launch/landing with one impact sound. Rigged reactions deferred. |
| Root / slow / silence | Accepted locally: core movement/grapple/gravity/resource/cast restrictions; disabled abilities remain scaffolding. |
| Fear / taunt | Accepted locally: core flee, collision, hit-break, grapple detach and target/perception cleanup; historical perception/NPC-control visible cases. |
| Sleep | Accepted locally: core waking versus cleanse timing, damage wake, non-restarting waking and immunity boundary. Placeholder pose only. |
| Blind / disorient / tether | Accepted locally: core configured sight/hit probability, weapon-cast restrictions, displacement bounds, internal yank and detach behavior; historical perception/control visible checks. |
| Disarm / vertical knock-up | Excluded / no approved separate scope; not implemented as extra abilities. |
| Boss all 14 contributions, solo casts and deduplication | Accepted locally: core strongest-once/composite/event dedup and contribution checks; packaged repeated solo stun break. |
| Boss 1,000 bar, 1.25 damage, up to 8s stagger and 90s protected refill | Accepted locally: varied-rate core boundaries; current packaged boss break and real vulnerability damage. |
| Death/reset/fall boundary, pause and focus input cleanup | Accepted locally: lifecycle/controller core; current exported pause/restart, authored preservation. All current foreground tests held focus; no fabricated focus-loss pass. |
| Creator settings save/load/Undo and Play isolation | Accepted locally: current serialization core; historical seven creator foreground checks `task4-visible-combat-authoring-20260924`. |
| Clean-Windows dependencies | **Blocked/unverified.** WindowsSandbox.exe still absent on 27 September. Prior VM query was denied. No bypass and no clean environment established. Local packages are not a substitute. |
| Combined fullscreen battle performance | Accepted for the measured local workload: same44-exchange fullscreen test,7.47 FPS before /398.01 after; all foreground checks passed, damage/healing retained. This is not an arbitrary-scene guarantee or a new target FPS. |

## Preserved failures and limits

The first 27 September export harness stopped because its initial 1.6-second wait finished before renderer startup produced frames. The corrected harness keeps all original behavioral/rendering checks and adds a bounded wait for three new frames, matching the editor's existing mechanism. Original failed report remains at `task4-export-20260927/gameplay-report.json`. This was a test startup assumption; no hidden retry of lost foreground occurred.

The combat fixture intentionally has no exit objective; export reports that fact. This does not certify a complete game or quest. Export validation and snapshots are bounded acceptance, not an exhaustive hardware/scene certification. No publication repository or remote changed during this work.
