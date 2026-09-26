# Step 4 — Movement, Windows export, and Step 4A combat

Consolidated implementation specification — 24 September 2026. Confirmed requirements are binding; explicitly labeled defaults are adjustable implementation choices, not additional user decisions. This is the implementation specification, not a completion report. New movement and combat requirements below remain subject to implementation and verification. This document supersedes earlier always-on mouse-look proposals.

Specification status: **finalized implementation baseline**. Confirmed rules take precedence over adjustable defaults. Implementation evidence belongs in STEP-4-IMPLEMENTATION-STATUS.md; documentation completion does not close Step 4/4A.

## Purpose and order

Build a reusable MMO-style movement foundation paired with manually aimed telegraph combat. Share the behavior between editor Play and exported games, and connect combat to Step 3's existing object properties, gameplay rules, and quests. Finish and verify Step 4 and 4A before Step 5. Steps 1, 2, and 3 are complete. User explicitly removed the broad re-verification gate; focus acceptance on Step 4/4A and directly affected integrations.

## Confirmed interaction rules

These rules are authoritative wherever movement, combat, and crowd control interact.

- Active dodge rolls prevent damage. Lunge also prevents damage for exactly twice the dodge protection duration. User delegated tuning: start with dodge 0.25 seconds and lunge 0.5 seconds, matching current movement durations. These are unverified balance defaults, not approval of the tentative 0.5/1-second example. Damage immunity is separate from CC immunity.
- Grapple release preserves forward/travel momentum with natural descent; wall attachment still follows its separately confirmed release rule.
- Basic attack 1 can hit all eligible targets in its telegraph. Holding 1 repeats complete 2.5-second cycles; releasing lets the current cycle finish and prevents the next. Target selection never starts firing. Respect dodge and CC firing restrictions.
- Five-second post-CC immunity is player-only, confirmed explicitly.


- Grapple accepts any suitable surface, not only marked grapple points. Players and enemies can also serve as attachment targets, including a large boss's shoulder. Attachment follows the moving target and permits continued aiming and firing. This is traversal attachment, not permission to pull, immobilize, or otherwise CC a friendly player. Character attachment, moving anchors, and their presentation still require implementation and verification.
- Spend one grapple charge when launching, including a miss. Holding G must not repeatedly launch or repeatedly spend charges for one attempt. Capacity remains two; recover one every ten seconds.
- Player taunt/enrage hides other enemies from the affected player's view, leaving the taunter as the only eligible attack target. This is separate from Blind's halved sight distance and hit probability; preserve world visibility and collision.
- Early cleansing of CC immediately starts the five-second immunity window.
- Stun and fear detach an active grapple, including wall attachment and swinging.
- Fear blocks firing; being hit by an attack breaks fear. Ending fear starts the applicable immunity window.
- Confirmed: the five-second post-CC immunity is player-only; NPC enemies do not receive it. Friendly/self-CC remains prohibited as specified. Do not infer NPC overlap/reapplication policy from this immunity exclusion.

Implementation and verification are recorded separately.


### Interruption and recovery

- Sleep: normal waking completes before the five-second player CC immunity begins; control returns at that boundary. Early cleansing still grants immunity immediately as separately confirmed.
- Starting a dodge cancels an attack still winding up. It must not resolve the cancelled shot afterward.
- Knockback, pull, launch, and knockdown block firing throughout their active loss-of-control/recovery sequence. Firing becomes eligible again when that sequence ends.
- Releasing G during a wall pull, before attachment, detaches and lets the character fall from their current position with collision and gravity, including over a chasm. No automatic rescue or teleport. This does not change releasing G after established wall attachment, which keeps the character attached.
- Dodging while grapple-attached detaches the character. Apply this to wall and swing attachment; rejected dodge attempts must not be treated as a successful dodge.
- Lunge damage protection lasts twice dodge protection. Initial delegated tuning: 0.25-second dodge and 0.5-second lunge. Lunge still costs both dodge charges. Implement and verify damage rejection and exact start/end boundaries; these protections are not yet verified merely because movement-duration tests passed.



## Step 4: confirmed controls

| Input | Required behavior |
| --- | --- |
| W / S | Move forward / backward. |
| A / D | Strafe left / right, rather than keyboard turning. |
| Combined movement keys | Allow diagonal movement and movement along changing headings. |
| Hold right mouse button | Turn the camera and character facing together. No always-on mouse-look. |
| Hold left mouse button and drag | Orbit the camera independently of character facing. |
| Release camera drag | Return ordinary cursor use for interface interaction. |
| Mouse wheel | Zoom from third-person follow view all the way into first person. |
| Space | Jump; a second distinct press in the air performs a double jump. |
| Hold Shift while running | Sprint, consuming the separate sprint stamina resource. |
| Double-tap W, A, S, or D | Dodge in that key's direction. |
| V | Dodge in the direction of the currently held movement keys, including diagonals. |

V is the dodge/lunge key. Control is the confirmed crouch/prone/slide key. Movement, facing, and independent camera orbit must remain distinct. Camera motion must not prevent movement.

## Continuous ground directions (confirmed)
Players, friendly NPCs, enemies, and bosses must move at arbitrary headings throughout 360 degrees, including angles such as 12, 15, or 18 degrees. No four-way or eight-way world-direction snapping. Keyboard combinations remain digital input, rotated by continuous facing; analog input and AI steering retain their continuous direction vectors. Diagonal input must not grant extra maximum speed. Enemy steering must follow this rule when integrated; authored routes already use continuous positions.


## Smooth pursuit and engagement (confirmed)
Players, friendly NPCs, enemies, and bosses must not teleport or jolt unintentionally during movement, engagement, target changes, or close-range repositioning. Pursuit updates a desired destination, never assigns the target position directly to the pursuer. Actual displacement must remain bounded by movement speed and elapsed simulation time, with collision respected. Use bounded acceleration, braking and turning; maintain attack spacing without oscillating around the stopping distance. A moving target may cause continuous lateral repositioning at any heading. If blocked or unable to reach a target, stop or choose a valid route rather than snapping through geometry. Explicit spawn/reset and intentionally designed teleport abilities are separate operations, never fallback pursuit behavior.
Acceptance: move and reverse the target repeatedly, stop at arbitrary offsets, cross attack-range thresholds, switch targets and obstruct pursuit. Check frame-to-frame displacement, smooth starts/stops and turns, stable spacing, and collision at different frame rates and during frame stalls. Include small enemies and large bosses. These are required behavior and verification gates, not completed automated checks; enemy pursuit is not yet verified by the player-heading tests.

## Sprint: exact confirmed values

- Maximum and starting stamina: **500**.
- Drain while sprinting with Shift held: **15 stamina per second**.
- Regeneration after releasing Shift: **25 stamina per second**, capped at 500. This replaces the earlier tentative value of 20.
- Sprint is hold-to-use, not an automatic or latched toggle. No automatic stamina depletion when Shift is released.
- **Exhaustion clarification:** reaching zero starts regeneration even while Shift remains held. Sprint stays disabled until stamina reaches the full 500, then automatically resumes if Shift is still held and the character is running. This drain/recover cycle can repeat without releasing Shift. Releasing Shift at any partial balance starts regeneration from that balance. Releasing/repressing must not bypass an exhaustion recovery already in progress.
- Display a small **semicircular meter just left of screen center**.
- Derived timing: approximately 33.33 seconds of continuous sprint from full; 20 seconds to refill from empty after release.

## Dodge: exact confirmed values

- Two available dodge charges.
- Each dodge consumes one charge.
- Restore **one charge every five seconds**, up to two. Use sequential recharge for this specification: two missing charges take ten seconds to restore; this is the interpretation of “one dodge every five seconds.”
- Show **two separate stacked bars immediately left of the sprint semicircle**.
- Dodge charges are separate from sprint stamina.
- **Dodge camera, user clarification:** in first person, the camera must visibly roll with the dodge, conveying the character rolling to the side. In third person, the camera follows the character's dodge movement without rolling itself. Rotation and timing follow the adjustable camera defaults below; implementation evidence is recorded separately.

## Step 4: grapple traversal

User addition, 24 September 2026. **Hold G** to launch and maintain a hook-and-chain-style grapple toward an aimed attachment point. This is not a press-again-to-detach toggle.

- Wall mode: holding G pulls the character directly toward the attachment point and attaches the character to the wall. Once wall attachment is established, releasing G does **not** drop the character. The character can remain attached and climb up the wall. **Space jumps off the wall.** Wall climbing and wall jumping are part of this requirement.
- Swing mode: attaching overhead, to a ceiling or a suspended/dangling target, supports swinging across gaps or objectives. **Keep G held to stay attached and swing back and forth. Release G to detach and drop.** On swing release, preserve forward/travel momentum and descend naturally toward the ground; do not stop immediately or add an unrequested launch boost. Established wall attachment still persists after releasing G.
- These are two actual traversal behaviors, not merely different grapple visuals. Represent the visible hook and tether and keep the character's movement collision-safe.
- **Grapple resource: two charges, regenerating one charge every ten seconds**, as specified by ChaosFox. Interpret this as sequential recharge: restoring both from empty takes twenty seconds. Keep this resource distinct from sprint stamina and dodge charges. Spend one charge on launch even on a miss. HUD placement follows the implementation defaults below.
- Grappling belongs to the shared movement foundation in Step 4, including editor Play and standalone export. It is not implemented yet.
- Acceptance: wall attachment pulls toward the selected point without crossing solid obstacles; overhead/suspended attachment permits a real tethered swing; missed/invalid attachment does not move the player; detachment and removed targets cannot leave the controller stuck. Record first- and third-person behavior and verify export parity.
- Acceptance: releasing G after wall attachment keeps the character attached; climbing works from that state and Space jumps off. Swing attachment persists while G is held and detaches on release. Do not apply swing-release behavior to an established wall attachment.

## Step 4: lunge, crouch, prone, and slide

Additional user direction, 24 September 2026:

- **Tap V:** directional dodge as already specified. **Hold V while running:** leap/lunge forward with the arms reaching forward and the whole body extending into the movement.
- **Lunge costs both dodge charges and travels farther than a normal dodge.** Require two available charges before starting; consume them together, with no partial spend if unavailable. Existing dodge recharge remains one charge every five seconds. Exact lunge distance remains a tuning choice; do not equate twice the cost with an implicitly approved double distance.
- Directional W/A/S/D double-tap dodging remains specified. The new held-V lunge must be distinguished from a short V press; it must not first consume a dodge and then trigger a lunge accidentally. Use the tap/hold arbitration defined in the defaults below.
- **Control:** crouch. A short press while stationary is the working interpretation for crouch; use the toggle/exit defaults below.
- **Hold Control while stationary:** enter prone; the character can then crawl forward.
- **Press Control while running:** slide. Running is the latest explicit trigger, not sprinting exclusively.
- These require actual posture/movement and collision changes, not camera-only lowering. Standing up must respect overhead clearance; prone/crouch/slide transitions must not permit clipping through geometry.
- Acceptance: short V gives exactly one dodge; held V while running gives a lunge without also firing the tap action; Control selects the intended posture/slide by movement state; crawl and safe stand-up work; authoring text inputs/shortcuts do not trigger movement actions. Verify collision, camera, resources, and Play/export parity.

Feature implementation status is tracked separately in STEP-4-IMPLEMENTATION-STATUS.md; this document defines the required finished behavior.

## Existing Step 4 obligations remain

- Explicitly scoped character-controller support for jumping, stairs, and moving platforms. Do not substitute the old static-AABB collision approach for the controller work.
- Preserve collision safety during the new movements and camera modes.
- Independently validate Windows standalone dependencies on a genuinely clean machine or clean Windows environment. A local export smoke test alone is insufficient.
- Keep authored scenes separate from temporary Play state.

## Step 4A: confirmed combat foundation

Weapon direction confirmed by ChaosFox: **dual magical pistols**. Create original Starlight weapon designs, effects, animations, sounds, names, and lore. 

1. An object marked **Breakable** through Step 3's authoring system is eligible for targeting and combat destruction.
2. **Tab** selects and visibly highlights a breakable target.
3. Selection does **not** auto-aim, fire, or automatically engage combat.
4. The **top-row 1 key** explicitly initiates the attack; holding it repeats attacks at the defined firing rate, and release stops repetition. Left mouse button is not the requested firing binding.
5. Display an attack **telegraph in the direction the player is aiming**, showing the attack area.
6. Movement and aiming remain available during the attack. The telegraph follows the changing aim and player movement during its active aiming phase.
7. **All eligible targets inside the telegraph can be hit by the basic number-1 attack**, not only the selected target. A target must actually be **inside the telegraph when the hit resolves**. A selected target outside it must not be hit merely because it is selected.
8. A successful hit visibly **shatters the breakable block** and updates its linked quest objective through the existing gameplay rules.
9. Use the same rules in editor Play and the exported game. Destruction during Play must not delete the authored object.

### Firing during movement

User clarification: top-row **1 can fire while moving, sprinting, jumping, double jumping, falling, grappling/pulling, swinging, attached to or climbing a wall, and standing on or riding platforms**. No grounded or stationary requirement may be added. Movement and aiming continue during attacks.

**An active dodge roll is the sole movement state that blocks firing.** This movement restriction ends when the roll ends; no extra post-roll lockout is specified. Menu/text input handling and any separately defined attack cooldown are distinct from movement restrictions. Starting a dodge cancels a shot still winding up. Do not queue blocked presses for delayed automatic firing; held-1 repeat handling must respect the dodge restriction.

Current Step 3 breaking removes the object; it is not already a projectile, telegraph, or visual shatter system. This document does not claim those features exist yet.

Damage numbers and their presentation are explicitly deferred by ChaosFox. They are not part of the initial Step 4A delivery.

## Basic attack cycle correction — authoritative latest input
A press of top-row1 commits one complete firing cycle. Releasing1 does not cancel the active cycle; it only prevents automatic repetition after that cycle. Holding1 repeats full cycles. Dodge and action-blocking CC still interrupt; focus loss, pause and reset clear firing. User described roughly1.5–2.5 seconds; selected adjustable starting duration is2.5 seconds, not a verified reference-game value. This supersedes earlier release-to-cancel and0.5-second repeat/0.15-second wind-up defaults. Initial implementation resolves one hit at cycle end; multi-hit firing within a cycle is not yet specified or implemented.


## Basic attack cone correction — latest requirement
The basic attack telegraph is a widening cone/ground sector, with its point at the character feet, extending outward along aim. It replaces the old rectangular box. Adjustable initial dimensions:20m radius,30degree full spread,2m hit height. Grounded aiming uses character attack-facing for the ground sector; airborne aiming retains attack pitch. Independent camera orbit alone does not turn the character or cone. Ground slope conformity and final airborne presentation still require verification. The2.5s committed-cycle behavior remains unchanged.

All cone telegraphs, for players, friendly NPCs, enemies and bosses, including damage and healing, remain movable throughout casting. The tip stays at a fixed local point on the caster (feet for these ground cones), translating with the caster and rotating with its live attack-facing through 360 degrees. Moving or turning never restarts channel progress. Outline, fill and hit resolution share the same live transform. No cast-start position or direction lock.

Channel presentation: keep the cone outline visible throughout the active firing cycle. Fill the cone progressively from the character feet toward its outer edge, driven by actual channel progress rather than a separate timer. The fill reaches full range when the2.5second channel completes. Each repeated cycle starts its own fill; releasing1 lets the current fill finish, while dodge or blocking CC interrupts it.

Telegraph color baseline: enemy attack telegraphs are red; friendly attack telegraphs are blue; friendly healing telegraphs are green; enemy healing telegraphs are orange. Apply the category color consistently to the cone outline and its progressing fill. The local player basic attack uses friendly blue. Enemy and healing attack producers must supply their category when connected.

Telegraph affiliation applies equally to players and NPCs: friendly damage is blue, friendly healing is green, enemy damage is red, enemy healing is orange. Friendly does not mean player-only; classify by relationship to the viewer and by damage versus healing.


## Enemy basic attack: Swipe (confirmed)
Enemy and boss basic melee attacks use a repeatable 1.5-second Swipe channel. The player number-one ranged cycle remains 2.5 seconds. Swipe displays a red cone with channel-progress fill. Adjustable initial melee footprint: 3 metres, 90-degree full spread. The cone follows the caster's actual position and current attack aim continuously through all 360 degrees, including during pursuit and channeling. The visible outline, progressing fill and resolved hit area use the same origin and direction. Do not lock the cone to world-forward or an old approach/channel-start heading. Stepping out of the current hit area or dodging can avoid the impact. Normal action-blocking CC and boss stagger interrupt it. Hostile actors may enable nearby attacks in object properties; disabled by default. Damage is a temporary 10 for integration tests, pending user damage tuning.

## Step 4A: crowd-control target safety

**User requirement: crowd-control abilities cannot be used on friendly players. This includes taunt and every other CC effect.** Do not add a friendly-fire option that overrides this requirement without a new explicit user decision.

Enforce this in shared effect application and resolution, not just targeting UI: direct casts, projectiles, area effects, chained/secondary effects, and forced displacement must all reject friendly-player CC. Check the current relationship when the effect would apply, so a previously valid hostile target that becomes friendly cannot be affected by a delayed hit. For future multiplayer, the authoritative game state must enforce the same rule; client presentation alone is insufficient.

Acceptance: friendly players receive no CC state, forced movement, taunt/enrage, or CC-specific camera/visibility changes through any supported application path. Mixed friendly/hostile area tests affect only eligible hostile targets. Define team/faction and PvP eligibility explicitly; lack of a friendly label must not automatically authorize attacking any other player. This is a documented requirement, not a claim of implemented networking or faction systems.

## Step 4A: crowd control — stun

### Post-effect CC immunity

ChaosFox requested CC immunity after an applied crowd-control effect. Confirmed timing using stun: **the immunity begins when the stun ends and lasts five seconds**, not when the stun initially lands. The example of a 1.5-second stun illustrates the sequence: 1.5 seconds stunned, then five seconds immune; it does not by itself replace the delegated default stun duration of two seconds.

**User clarification: this applies after any CC effect ends, and the five-second immunity blocks all CC types**, not just the type that ended. Momentary effects use the end of their control/recovery sequence; persistent effects use the end of their active duration. During the immunity window, incoming CC is rejected rather than queued for later. This protection does not itself grant damage immunity.

**Activation is automatic:** ending the CC triggers immunity without a button press, skill activation, or player action.

**Hostile application required:** ChaosFox explicitly excludes self-applied CC as well as friendly-player CC. A character cannot trigger this immunity as a free pre-engagement buff. Grant it only after an actual accepted enemy-applied CC ends; rejected, missed, self-applied, or friendly applications must not grant immunity. Environmental effects require separate eligibility rules rather than silently qualifying as an enemy application.

**For affected players, no CC chaining or overlap:** only one CC may be active. Do not stack, replace, refresh, extend, or queue additional CC. When the active effect ends, automatically begin the five-second universal immunity window; reject new CC throughout that window as well. Attempts during immunity must not reset or extend its timer. Tether's own boundary-yank reaction is part of the accepted tether, not a second independently applied CC.

**Same-attacker early reapplication exception, latest user instruction:** if the player who applied the active CC to another player attempts to apply another CC to that same target before the first ends, cancel the original CC immediately and start the target's five-second universal immunity immediately. The incoming CC does not take effect. This includes a repeat of the same type or a different CC type. Track the active effect's source identity to enforce the rule. Other attackers' CC attempts remain rejected without changing the active effect or its timer. Evaluate only an otherwise eligible hostile application reaching the target; a mere button press, miss, or invalid/friendly application is not a landed CC attempt. Cancellation clears the original control restrictions and presentation safely; it must not teleport the character or freeze airborne physics.

Player-only scope and immediate immunity after early cleansing are confirmed. For ordinary sleep recovery, immunity begins after waking finishes and control returns. Explicit early cleanse follows the confirmed immediate-immunity rule. This requirement is saved, not implemented.

### Duration table and effect categories

User clarification: distinguish **persistent status durations** from **momentary displacement/reaction timing**. The lasting statuses to tune are stun, root/bind, slow, silence, fear, taunt/enrage, sleep, blind, disorient, and tether. Each has a duration below. Knockback, pull, launch, and knockdown instead have motion/recovery windows; their listed timings must not add a separate lingering debuff after that reaction finishes. Sleep's waking phase is a separately displayed recovery status, as explicitly requested. A tether boundary yank is a brief pull reaction inside the longer tether status.

ChaosFox explicitly delegated the starting duration of every CC effect to implementation judgment. The following are **initial editable tuning defaults**, not measured balance results or copied timings from another game. Duration alone no longer requires a user decision. Previously listed duration questions below are superseded by this table; other unresolved interactions remain open.

| Effect | Starting duration / recovery |
| --- | --- |
| Stun | 2 seconds of action lock. |
| Knockback | 0.5 seconds of movement interruption, preserving the requested brief stumble. |
| Pull | Up to 0.5 seconds of forced movement, ending sooner on reaching the stopping point or obstruction; no added recovery lock. |
| Launch | Physics-driven flight until actual landing, followed by 1 second for side-roll/get-up. Flight is not a fixed-duration stun or an automatic timed landing. |
| Knockdown | 1 second for the knee-drop and standing recovery. |
| Immobilize / root / bind | 3 seconds. |
| Slow / snare | 4 seconds at the approved 33% movement and firing-rate reductions. |
| Silence | 3 seconds; basic attack and movement remain eligible. |
| Disarm | Excluded; no duration. |
| Fear | 2 seconds of forced fleeing. |
| Taunt / player enrage | 3 seconds. |
| Sleep | Up to 5 seconds total in the sleep-entry/asleep phase, then 1.5 seconds in the visible waking phase. An earlier qualifying wake trigger starts the waking phase early. Natural expiry also enters waking. |
| Blind | 3 seconds at the approved 50% hit-chance and sight-range reductions. |
| Disorient | 3 seconds at the approved 25% movement reduction and weapon-skill lock. |
| Tether | 4 seconds overall. A boundary yank uses pull's up-to-0.5-second movement; it does not restart the tether lifetime. |

These values do not authorize indefinite refresh, additive duration stacking, or changes to approved restrictions. The post-effect immunity and same-attacker anti-chain rules above govern reapplication. Animations must fit the defined effect/recovery timeline rather than silently extending the lock. Launch still requires ground contact, and removal/reset must clear all effects safely. No separate vertical knock-up is added merely to assign it a duration. These defaults are documented, not implemented.

User-confirmed behavior, 24 September 2026:

- **Stun overrides normal gameplay actions.** While stunned, the character cannot voluntarily move, sprint, jump, dodge, grapple, climb, or fire. This is an explicit exception to the earlier rule that only dodge rolling blocks firing during movement.
- The character remains in place and looks dazed, with a small circular body wobble rather than actual locomotion. The presentation must not move the collision body through nearby geometry.
- Small birds circle above the character's head and chirp. Use original dazed-character presentation; this does not add self-damage.
- Use an original Starlight visual/audio treatment. The requested animation and sound are not implemented yet.
- Stun blocks character actions, not Escape, menus, or application controls. Exact camera-look behavior remains to be specified separately.
- Stun duration, reapplication, player immunity, early cleansing, and grapple detachment are defined above and in the latest clarifications. Stun cancels pending attacks and follows the airborne/forced-motion defaults below; gravity continues. Inability to act must not freeze gravity.
- Acceptance: all voluntary gameplay actions are rejected during stun without consuming their resources; dazed wobble, orbiting birds, and chirps run only for the active effect; expiration/reset clears the effect and restores controls. Held/queued input behavior after expiration must be defined and tested to avoid unintended automatic attacks.

### Crowd control 1: knockback

User-confirmed behavior: the character is pushed back and visibly stumbles backward. Voluntary movement is interrupted briefly, approximately **half a second**, then resumes. This is a short movement interruption, not a prolonged stun or a full gameplay-action lock.

The camera gives a sharp, brief jolt/shake conveying the impact, then settles normally. Keep camera presentation separate from collision movement; actual displacement must respect geometry. Exact displacement distance, force direction relative to source/facing, shake strength, first/third-person differences, and interaction with airborne/grapple states use the adjustable defaults below. Firing interruption was not specified; do not silently add it.

Acceptance: an impact visibly pushes/stumbles the character, interrupts voluntary movement for the configured short interval, applies the brief camera response, then restores movement and clears camera offsets. No residual tilt/shake or permanent movement lock after reset or interruption.

### Crowd control 2: pull

User-confirmed behavior: forcibly drag the character toward an enemy or specified point, with momentary loss of voluntary movement during the pull. The reaction is led by the torso/stomach, with the limbs trailing backward slightly as though surprised. The head tilts backward and the camera briefly jolts backward during the yank. At recovery, the camera returns to its normal forward-facing presentation and movement control returns.

This is forced inward movement with a distinct reaction, not knockback and not the player's voluntary grapple. Keep temporary camera reaction offsets separate from player aim so recovery does not reset the view to an arbitrary world direction. The described camera/head reaction is a presentation requirement; third-person framing still needs tuning.

Pull uses up to 0.5 seconds of collision-safe forced movement; firing is blocked during the pull.

Acceptance: the character moves toward the designated source/point with the torso-led reaction, briefly loses movement control, and regains it at the end; camera reaction fully clears. Blocked or removed destinations and resets must not leave a stuck movement state.

### Crowd control 3: launch

User-confirmed definition: **launch throws the character upward and backward**, lifting them off their feet. It is not simply a vertical knock-up and is not a selectable two-mode effect. A straight upward knock-up is a distinct concept, with its own rules still undefined.

Required presentation sequence:

1. The character is thrown upward and backward and flails during flight.
2. The character lands on their back with an audible thud.
3. At the back-first landing, the described character-view camera faces upward toward the sky.
4. The character rolls onto their side and pushes themselves up.
5. The camera follows the recovery and returns to its normal forward-facing orientation, clearing temporary offsets rather than resetting the player's world heading arbitrarily.

Use the launch defaults below. Landing and recovery require actual ground contact, never a fixed airborne landing timer.

Acceptance: verify the upward/backward trajectory, airborne flail, actual back-first landing and thud, sky-facing character-view reaction, side-roll/get-up sequence, and restored camera orientation. Walls, ceilings, platforms, interrupted sequences, and reset must not leave invalid collision or residual camera state. This requirement is documented, not implemented.

### Crowd control 4: knockdown

User's final correction replaces the initial idea of another backward fall: on impact, the character **drops to one knee**. The weapon still held in their hand hits the ground, then the character **stumbles back to their feet**.

This is distinct from launch's airborne/back-first landing sequence. Weapon contact is part of the reaction animation, not an inventory drop or disarm; no weapon retrieval mechanic was requested. Duration, movement/action restrictions, camera response, grounded versus airborne handling, and interruption/reapplication rules remain to be specified.

Acceptance: show the impact-to-knee reaction, held weapon contacting the ground, and stumbling stand-up recovery without losing the equipped weapon or replaying the launch animation. Reset/interruption must restore a valid pose and controller state. Documented only; animation and effect are not implemented yet.

### Crowd control 5: immobilize / root / bind

ChaosFox authorized conventional MMO behavior without requiring a custom presentation. Use these names for the same initial movement-restriction effect; this is Starlight's chosen default, not a claim that every MMO treats them identically.

- Prevent voluntary translation: walking, strafing, sprinting, jumping, dodging, climbing, and starting a grapple cannot move the character while rooted.
- Keep aiming, camera look, and firing available unless another independent effect blocks them. Root is not stun.
- Rejected movement actions do not spend sprint stamina, dodge charges, or grapple charges.
- Root does not freeze gravity, suspend an airborne character, or stop supporting-platform motion. Forced displacement is distinct from voluntary movement.
- Duration and visuals are configurable implementation choices. Damage does not automatically break the effect in the initial default; expiry or explicit removal clears it.
- Existing grapple attachment/pull/swing transitions and overlapping effects need explicit integration checks; do not resolve them by treating root as a global physics pause.
- Acceptance: ordinary movement and mobility actions are blocked, aim/fire remain usable, resources are not consumed by rejected actions, and expiry/reset restores movement. Verify airborne and moving-platform cases separately.

### Crowd control 6: slow / snare

User-confirmed: reduce **firing speed by 33%** and **all movement speed by 33%** while active. Both therefore run at **67% of their normal rate**, not one third of normal. Slow reduces rates without preventing aiming, movement, or firing outright.

Firing speed means attack frequency: multiply attacks per second by 0.67, or equivalently divide the normal interval between shots by 0.67 (approximately 1.4925 times the normal interval). This does not specify reducing projectile speed or damage, or automatically firing without an explicit attack input.

The movement requirement includes supported player-controlled traversal, not merely walking. Define and test how the multiplier applies to sprint, dodge, climbing, grapple pull, swing control, and jumping. Changes to gravity, ballistic jump arcs, forced displacement, and moving-platform velocity are not implicitly specified by a movement-speed reduction. Duration, stacking, and detailed traversal interaction remain to be settled; resource drain and recharge rates are unchanged unless separately requested.

Acceptance: verify the 0.67 movement and attack-rate multipliers, correct shot-interval calculation, continued ability to act, restoration at expiration, and no unintended changes to damage, projectile speed, or resource rates. Documented only, not implemented.

### Crowd control 7: silence

User-confirmed: one combined silence effect blocks non-basic bar abilities regardless of whether they are physical or magical. Do not split this restriction into separate physical and magical effects.

- The basic **number-1 weapon attack remains available**.
- Normal movement, dodging, and other movement mechanics remain available; silence alone is not a movement restriction.
- Other assigned abilities on the bar are temporarily disabled until silence expires or is removed. This includes future slots 2–5 and R once those slots are implemented and enabled; they remain disabled scaffolding in the initial delivery.
- Removing silence removes only its own restriction. It must not enable an empty/unreleased slot, clear unrelated cooldowns, or override another active control effect.
- Acceptance: basic attack and movement work under silence, a test non-basic physical ability and magical ability are blocked through the same restriction, and effect removal restores only otherwise eligible abilities.

### Research item 8: disarm — explicitly excluded

ChaosFox chose not to include disarm. The combined physical/magical silence above provides the desired non-basic ability restriction while preserving the basic attack and movement. Do not implement a separate disarm effect or add overlapping disarm-plus-silence restrictions. Preserve the original research numbering for the ongoing discussion; the next item is 9, fear.

### Crowd control 9: fear

ChaosFox approved the described traditional behavior: the character **flees uncontrollably for a short time**, temporarily replacing voluntary movement with forced fleeing. Restore player movement when fear ends.

Fear lasts two seconds by delegated default, blocks firing, detaches grapples, and breaks when hit by an attack. Flee path selection, other ability restrictions, and airborne behavior remain implementation details. Respect collision; fear must not move the character through walls. Do not assume that approval of fleeing also approved a full stun-style action lock or random input inversion.

Acceptance: fear drives fleeing independently of held movement keys for its active duration, respects geometry, and returns movement control on expiration/removal/reset without leaving queued forced movement. Documented only, not implemented.

### Crowd control 10: taunt

User-confirmed design distinguishes the affected target:

- **NPC enemy:** force the enemy to attack the character that applied taunt for the effect's duration.
- **Enemy player:** apply a temporary **enraged** effect. The player is blinded to other surrounding enemies and can attack only the player who applied taunt.
- The player effect is a restriction on awareness and eligible attack targets. Automatic player firing, forced player movement, and automatic aiming were not explicitly requested; preserve the manual attack-input and telegraph-hit requirements unless ChaosFox changes them.
- Confirmed presentation: hide other enemies and their identifying markers while enraged; retain the taunter in view. Do not hide world collision or make the environment disappear by assumption.
- Define duration, area-attack eligibility, existing attacks/projectiles, stacked taunts, and removal when the taunter dies/disappears or becomes invalid. Do not implement the restriction only as a Tab-selection filter; attack resolution must enforce the approved target rule.
- Acceptance: an NPC selects/attacks the taunter; a player can attack only the taunter during enrage, with the approved awareness effect, and recovers ordinary visibility/target eligibility afterward. Other enemies must not become attackable through area effects or alternate attack paths contrary to the restriction.

This is a saved gameplay requirement, not evidence of existing NPC combat AI or multiplayer support. Player-versus-player validation requires those systems and must not be claimed from a local presentation test.

### Crowd control 11: sleep / incapacitate

User-confirmed sleep presentation and recovery:

- On application, the target gradually lowers themselves to the ground, lies on their side, and enters a cute sleeping animation. Support race/character-appropriate animation variants rather than requiring one identical pose for every character.
- Sleep temporarily stops voluntary movement and actions.
- Waking is not an instantaneous return to combat. Transition to a visible **Waking up** status for approximately **one to two seconds**.
- During waking, the character stirs, looks around with a confused expression, and pushes themselves up from the ground. This expression is presentation, not an additional confusion CC effect.
- Once the character is upright, they become aware of the target again and recover ordinary control/behavior. For player characters, this must not automatically fire a weapon or auto-aim.
- In the damage-wakes-sleep discussion, ChaosFox requested gradual waking; damage-triggered waking must therefore enter the waking phase rather than immediately restore actions. Whether every damage source triggers waking, and behavior on natural sleep expiry or cleanse, follow the implementation defaults below.
- The described waking “buff” is a visible status. Damage immunity, CC immunity, or damage reduction were not specified and must not be inferred from the word grace period.
- Acceptance: show lowering/side-sleep animation, transition to the labeled waking phase, one-to-two-second stir/look/get-up recovery, and restored control only after recovery. Damage/reapplication must not accidentally create an endless waking phase; reset/removal must clear the pose and status safely.

### Crowd control 12: blind

User-confirmed for both players and enemy NPCs:

- Reduce chance to successfully hit a target by **50%**. Interpret as halving the otherwise applicable hit probability: 100% becomes 50%, 80% becomes 40%, not subtracting 50 percentage points.
- Reduce **visible range by 50%**, using half the normal sight distance. The final wording concerns distance, not halving the camera's angular field of view.
- Manual aiming and telegraph geometry remain prerequisites for a hit. The accuracy reduction applies only to otherwise valid hits; blind never permits an off-telegraph hit. It does not halve damage on a successful hit.
- Define a normal sight-range baseline and the player visual treatment; for NPCs, implement the corresponding perception-range restriction. Do not claim that merely overlaying darkness changes NPC perception or hit resolution.
- Acceptance: verify the 0.5 hit-probability multiplier with deterministic seeded checks, a 0.5 sight-range multiplier for the appropriate player/NPC systems, preservation of manual targeting and existing eligibility restrictions, and full restoration on expiration/removal/reset. No friendly player may receive the effect.

### Crowd control 13: disorient / confuse

User-confirmed: a mobile, staggered state rather than a full stun. The player retains movement control and can dodge and use movement mechanics, but visually staggers while moving. Reduce movement speed by **25%**, leaving **75% of normal speed**.

- Block weapon skills, including the basic number-1 attack and weapon skills assigned to slots 2–5. Future weapon skills must follow the same restriction regardless of binding; treatment of non-weapon abilities follows the implementation defaults below.
- Unlike silence, disorient blocks the basic attack. Unlike stun/root, it does not disable voluntary movement or dodging.
- Stagger is presentation, not random input reversal or forced wandering. Do not consume attack resources for blocked weapon-skill attempts or automatically fire queued attempts when disorient ends.
- Acceptance: movement and dodging remain usable at the defined movement modifier, stagger is visible, weapon skills including slot 1 are blocked, and expiration/removal/reset clears only this effect's restrictions. No friendly player may receive the effect.

### Crowd control 14: tether

User-confirmed: restrict the character's permitted distance from an attachment point. Allow movement within that range; attempting to move beyond it triggers a **yank back toward the attachment point**, using crowd-control 2, pull, as the reaction reference.

- Reuse the torso-led pull reaction, trailing limbs, brief head/camera jolt, and momentary loss of voluntary movement during the yank. Restore movement afterward while retaining the tether restriction until the tether ends.
- This is a hostile crowd-control tether, distinct from the player's voluntary grapple. It must not consume grapple charges merely because the effects share some presentation or movement code.
- Radius, whether the yank ends at the boundary or nearer the anchor, yank speed, retrigger timing, moving anchors, line-of-sight/obstruction behavior, and airborne/grapple interactions follow the implementation defaults below. Do not silently interpret crossing the boundary as breaking the tether.
- Acceptance: movement within range works; crossing the range triggers the pull response without teleporting through geometry; movement control recovers after the yank; the boundary remains enforced while the effect lasts. Anchor removal, expiry, and reset must clear the restriction safely. Repeated boundary contact must not create uncontrolled camera shaking or a permanently stuck pull. The no-friendly-player-CC rule applies.

This is documented, not implemented. The numbered crowd-control discussion is recorded, with disarm explicitly excluded. Vertical knock-up was distinguished from launch but has not received a separate approved behavior specification. Do not claim implementation from this design record.

## Step 4A: extensible action slots

Additional user requirement, 24 September 2026: build the action scaffolding alongside the initial attack, rather than hard-coding all combat to the 1 key.

- Initial playable action: top-row **1** fires the specified telegraph attack.
- Provide reusable action slots with default bindings **1, 2, 3, 4, 5, and R**. Slots 2–5 and R are reserved for future assigned skills; no additional skills are promised in this step.
- Route input through a slot-to-skill assignment and shared action execution path. Keep skill identity separate from the physical key so future skills can be assigned without duplicating the combat input implementation.
- An unassigned slot does nothing: no attack, resource spend, or automatic combat engagement.
- User clarification: slots **2, 3, 4, 5, and R are scaffolding only for now and disabled in the editor**. Do not expose them as usable skills or enable them through creator assignment yet. Their internal routing can be tested, but public activation waits for future skill work. Only slot 1 is available for the initial combat feature. Disabled combat bindings must not consume keys needed by existing editor tools or text fields.
- Allow future skill definitions to supply their own targeting/telegraph, timing, cooldown, resource requirements, and effect. Their concrete behavior remains future work; this is scaffolding, not a claim that those skills already exist.
- Editor Play and exported games must use the same action-slot model. Preserve authored assignments through scene save/load and export when assignments are introduced; keep runtime cooldowns and resource state separate from authored definitions.
- Acceptance: slot 1 executes the initial attack through this shared path; unused slots are harmless; a test skill can be assigned to another slot without a new hard-coded key-specific attack path. No requirement yet for a skill-bar UI or additional finished attacks.

## Acceptance checklist

- [ ] WASD strafing, diagonals, and changing headings work without diagonal speed advantage.
- [ ] Left-drag orbit does not turn the character; right-drag controls camera and facing; releasing restores cursor interaction.
- [ ] Third-person wheel zoom reaches first person; camera obstruction handling is verified.
- [ ] First-person dodging visibly rolls the camera and returns it to its normal orientation; third-person dodging follows the character without camera roll. Switching views or interrupting a dodge must not leave a residual camera tilt.
- [ ] Double jump needs a second press, does not repeat from a held key, and resets on valid landing, including moving platforms.
- [ ] Sprint starts at 500, drains at 15/second only under its defined held-Shift condition, and recovers at 25/second after release without exceeding 500 or dropping below zero.
- [ ] The semicircle and both stacked dodge bars clearly show their separate resources.
- [ ] Double-tap and V both dodge correctly, including V diagonals; two dodges consume two charges; recovery grants one every five seconds.
- [ ] Stairs, moving platforms, walls, and ceilings still behave correctly during the expanded movement tests.
- [ ] Tab visibly selects without firing or changing aim; pressing 1 initiates the attack.
- [ ] Moving and aiming update the telegraph; selected targets outside the resolved hit area are missed.
- [ ] Pressing 1 can fire while airborne, sprinting, grappling, swinging, wall-attached/climbing, and riding a moving platform. An active dodge blocks firing; ending the dodge restores firing availability without an extra movement lockout.
- [ ] A valid hit visibly shatters a breakable object and completes only its appropriate linked objective; authored scene remains intact.
- [ ] Editor Play and standalone export exercise the same movement/combat behavior.
- [ ] Clean-Windows dependency validation has recorded environment and results.
- [ ] Visible supervised checks are demonstrated; if the test cannot come forward or loses focus, stop and notify ChaosFox instead of retrying silently.
- [ ] Record evidence and update the ten-task checklist; Steps 1–3 remain closed, with verification focused on Step 4/4A and changed integrations.

## Step 4A: boss break bar

User addition: bosses can have a separate break bar. Eligible crowd-control effects reduce this bar by effect-specific amounts. Emptying it triggers a temporary stunned/staggered state and increased damage taken, creating a coordinated damage window. Show bar progress, the break, and vulnerability clearly. Use original presentation and configurable boss data.

This is a boss mechanic, separate from player-only post-CC immunity. Not implemented or tested yet. Do not silently apply the player same-attacker cancellation rule to boss bar contributions.

The recovery cycle below defines refill and repeat-break rules. Contributions must come from valid resolved effects, not merely button presses. Preserve existing target eligibility and authored/runtime separation.

### Boss break-bar capacity and delegated contribution defaults

ChaosFox confirmed **1,000 maximum/starting break strength** and delegated per-CC contribution tuning by severity. The following are adjustable initial design values, not playtested balance. Values are break damage, not health damage and not percentages.

| CC type | Break damage per accepted application |
| --- | ---: |
| Stun | 250 |
| Launch | 250 |
| Sleep | 200 |
| Fear | 200 |
| Knockdown | 200 |
| Knockback | 150 |
| Pull | 150 |
| Immobilize / root / bind | 150 |
| Disorient | 150 |
| Silence | 100 |
| Taunt | 100 |
| Tether | 100 |
| Slow / snare | 75 |
| Blind | 75 |

Disarm remains excluded. Root/bind/immobilize are aliases of one effect, not three contributions. Initial calculation policy: one contribution per accepted CC application, not every frame or status tick; a tether's internal yank does not add pull damage. Composite abilities use the highest included contribution once by default, as defined below, preventing accidental multiple credit. Clamp strength at zero and trigger the break transition once. Capacity and contribution values are creator-configurable. Four standard stuns remove 1,000; stun + launch + sleep + root + pull also remove 1,000. These examples assume distinct eligible applications during the active bar phase, not bypassing cast validation or cooldowns.

Rationale: total action loss and major displacement rank above movement-only or partial action restrictions; these values are encounter-tuning judgments, not a claim of universal severity. Normal CC durations do not multiply these initial one-time values. Boss resistance and repeated solo contributions are confirmed below; vulnerability duration/multiplier are defined below; recovery is defined below. No combat implementation or test completion is claimed by this table.

### Confirmed boss resistance and solo contributions

While a boss break bar is intact, valid incoming CC removes break strength instead of applying the ordinary CC status, action restriction, or forced displacement. A stun does not stun, a pull does not pull, and a slow does not slow during that phase. Reaching zero triggers the dedicated boss stagger/stun and increased-damage phase already specified. This conversion does not itself cancel any separately valid health-damage component of an attack.

A single player may apply multiple valid CC abilities to deplete the same boss bar, including during open-world boss encounters. Repeated eligible casts from that player contribute normally, subject to each ability's ordinary cooldown and hit requirements. Do not apply the PvP same-attacker cancellation/automatic-immunity penalty to boss bar contributions, and do not require different attackers. Each accepted application contributes once; holding input, duplicate delivery, or internal tether reactions must not multiply contribution.

Boss vulnerability is now defined below; bar recovery is defined in the confirmed recovery cycle below. Documentation only; implementation and tests pending.

### Confirmed boss vulnerability window

Breaking the boss bar triggers the specified stunned/staggered damage window with **25% increased damage taken (1.25 times otherwise applicable damage)** for **a maximum of eight seconds**. Use eight seconds as the initial default and upper limit for this phase. This modifies damage received, not attacker firing speed or player CC immunity. Additional CC must not extend this phase beyond its eight-second maximum. The confirmed 90-second protected recovery cycle below defines refill and repeat-break eligibility. Requirement saved; implementation and verification pending.

### Confirmed boss bar recovery cycle

After the stunned/staggered vulnerability phase ends, refill the boss break bar gradually from zero to its full **1,000 strength over 90 seconds**. This replaces the tentative 60-second suggestion. Begin recovery after the vulnerability phase, not during it. Use linear recovery initially: maximum strength / 90 per second (approximately 11.111 strength per second at the default capacity), clamped to full.

The bar cannot be broken during recovery. Re-enable break-bar depletion only when the bar is completely full; CC during recovery does not reduce, delay, or restart its refill. Keep ordinary boss control resistance during this protected recovery phase so incoming CC cannot bypass the locked break bar. These are the implementation interpretations of waiting for full before another break. Show recovery distinctly from the active, depletable bar.

Cycle: active bar -> zero/break -> up to eight seconds stunned/staggered with 1.25x damage taken -> 90-second protected refill -> active full bar. Remove the damage bonus when the stagger ends. With the default eight-second stagger, the bar becomes available again 98 seconds after the break begins; the next break still requires new valid CC contributions. This is a saved requirement, not an implemented or verified system.

## Final implementation defaults: movement and traversal

These adjustable defaults fill remaining details under delegated tuning authority. Confirmed user rules above take precedence. They are implementation starting values, not measured balance results.

| Setting | Starting default |
| --- | --- |
| Run / sprint | 6m/s / 9m/s; normalized diagonals |
| Jump / double-jump | 5m/s upward each; gravity9.81m/s² |
| Dodge | 3m over0.25s; one charge; protection throughout |
| Lunge | 6m over0.5s; both charges; protection throughout |
| V hold / double-tap threshold | 0.22s / 0.25s |
| Crouch / prone speed | 50% / 25% normal |
| Stand / crouch / prone height | 1.8m / 1m / 0.5m |
| Control hold threshold | 0.35s |
| Slide | 0.6s, speed8m/s decreasing to2m/s, ends crouched |
| Camera | default5m, maximum12m; wheel0.5m; first person at0.15m |
| Camera sensitivity / pitch | 0.15 degrees per pixel / -80 to80 degrees |
| Grapple range / hook / pull | 25m / 40m/s / 12m/s |
| Wall climb | 2m/s |
| Wall jump | 5m/s up,4m/s outward |
| Swing steering | 6m/s² tangent acceleration,15m/s relative speed cap |

- Idle held Shift does not drain; regeneration follows release or exhausted recovery. Only eligible running consumes stamina. Partial refill remains usable unless exhausted.
- Air steering uses normalized movement; a ledge fall leaves one aerial jump. Supported landing resets jumps. Wall attachment/jump does not refill aerial jumps.
- Short V release dodges; held V while grounded and running lunges forward once. Invalid hold does not trigger a fallback dodge. No-direction dodge goes forward; airborne dodge allowed. Protection ends on interruption; no extra recovery lock. Lunge firing remains allowed.
- Control tap toggles crouch/stand; stationary hold enters prone; tap in prone requests standing. Space first requests safe stand, then jumps only if clearance permits. Slide uses crouch collision and follows terrain without uphill boost. All transitions test actual capsule clearance.
- Camera obstruction retracts boom with0.2m clearance from the current posture eye point. First-person dodge uses a full smooth roll; third-person stays upright. Interruption clears tilt. Cursor capture/hiding is confined to held camera drag; focus loss restores it. Reduced-camera-motion setting changes presentation only.
- Grapple ray chooses nearest suitable solid surface, including characters. Ceiling/suspended overhead anchors swing; wall/character-side anchors pull and attach. Creator Auto/Wall/Swing override resolves unusual geometry. Floor hits pull but do not become climbable walls.
- Rope length equals initial attachment distance, capped25m. Gravity and collision-safe rope constraint drive swinging; WASD supplies tangent steering. Release preserves actual travel momentum, including anchor velocity once, then gravity applies.
- W/S climb vertically, A/D traverse wall. Mantle at top only if swept standing capsule fits. Space detaches outward; same press cannot also double-jump. Each new G press launches once and spends one charge; holding never repeatedly launches.
- Anchor death/removal/teleport detaches safely, no teleport-following. Blocked pull detaches. Attached movement follows local anchor coordinates while respecting collision. Friendly-character grappling moves only the grappler, never the target.
- Show two grapple indicators below dodge bars. Require original visible hook/tether, character posture/roll/lunge animation and dual pistols; placeholders do not establish presentation completion.

## Final implementation defaults: combat and CC integration

- Player basic attack: feet-origin cone, adjustable 20m radius, 30-degree full spread and 2m hit height. One committed 2.5-second channel resolves at completion; holding repeats. Fill follows actual channel progress. Aim follows character attack-facing on ground and includes attack pitch in air. Enemy Swipe uses its separate 1.5-second melee cycle, as specified above.
- Hit every eligible intersecting target once per shot with individual solid-geometry line-of-sight checks. Tab cycles visible eligible targets within25m by aim proximity, distance, stable ID. Lost targets clear selection; selection never fires or aims.
- Release1 preserves the active committed cycle and stops automatic repetition. Dodge/action-blocking CC cancels wind-up; blocked taps are discarded. Still-held1 can begin a fresh attack once eligible, never resume a cancelled one. Focus loss/pause/menu clears held input and requires fresh press.
- Breakable demonstration blocks shatter on one hit. Initial actor test health100 and basic damage10 are temporary, editable placeholders. All health and damage balance values await the user's later calculations; confirmed stamina, CC and boss-phase values remain unchanged. Damage-number display remains deferred. Cosmetic fragments:16 per object,2s lifetime,256 global limit; recycle oldest cosmetics. No damaging debris. Emit linked quest event exactly once; authored objects remain unchanged.
- Original avatar, pistols, effects, sounds and readable animations required. Slots2–5/R stay disabled scaffolding, including in editor; test-only abilities can exercise effect routing. No new public skill roster is implied.
- All hostile effects validate actor identity, explicit hostility and eligibility at resolution. Unknown relationships fail closed. Environment hazards do not grant enemy-CC immunity. Friendly and self-CC remain rejected.
- Players follow confirmed one-active-CC and5s immunity rules. NPC-source repeat attempts on an already controlled player are rejected, rather than invoking the player-attacker penalty. Ordinary NPCs accept one active CC, reject overlaps, have no post-CC immunity and can be affected again after expiry. Bosses use their separate break-bar cycle.
- Sleep enters waking on natural expiry or any damaging attack, including periodic damage. Further hits never restart waking. Normal waking ends before immunity starts. Cleanse or same-attacker cancellation immediately removes control and grants immunity; visual recovery cannot prolong the lock.
- Stun, fear, sleep, knockback, pull, launch and knockdown detach grapples and cancel voluntary special movement. Root cancels voluntary pull/climb/steering but can retain passive wall/rope attachment; gravity, rope constraints and platform motion continue. Tether yank detaches voluntary grapple.
- Knockback:3m away over0.5s. Pull: up to8m over at most0.5s, stopping at collision or1m from destination. Launch:6m/s upward and6m/s away, actual gravity/landing then1s recovery. Knockdown:1s. All block firing until reaction ends; no tunnelling or timer-based fake landing.
- Fear uses normal run speed away from source, slides along obstacles or stops when blocked. It blocks attacks and voluntary mobility abilities; damaging hit ends it. Stun disables gameplay actions while camera/menu remain usable.
- Slow0.67 and disorient0.75 multiply player-generated horizontal travel, climb/pull speed and swing steering. They do not alter gravity, jump vertical impulse, inherited momentum, platform velocity or forced CC displacement. Fixed dodge/lunge duration and protection stay unchanged; slowed travel covers less distance. Resource rates remain unchanged. Player one-CC rule prevents stacking these effects.
- Silence cancels non-basic casts only; basic1 and movement remain available. Disorient cancels weapon casts including1; non-weapon abilities remain eligible if otherwise allowed. Disabled slots stay disabled.
- Player taunt hides other enemies and their markers, not terrain, collision or friends; only taunter can take that player's attack damage, including area attacks. NPC taunt prioritizes/attacks source within navigation/range. Invalid/dead/nonhostile source ends taunt and grants player immunity when applicable.
- Blind test sight baseline60m, reduced30m, editable per actor. Distance fog handles player visibility; NPC perception uses corresponding distance. Angular field of view unchanged. Seeded hit roll per target per shot multiplies valid hit probability by0.5; no damage reduction or off-telegraph hits.
- Tether radius6m; yank to0.5m inside boundary in at most0.5s;0.5s retrigger guard plus boundary hysteresis prevents shake loops. Internal yank is not another CC application or boss contribution. Obstruction stops pull safely; anchor loss ends tether. Total duration4s.
- Preserve specified CC presentation: wobble/chirping birds; torso-led pull; backward stumble; flail/back landing/thud/skyward view/side-roll get-up; knee-drop with held weapon; side-sleep and waking; disorient stagger. Animations fit timers. First-person camera recovers orientation; third-person conveys body reaction without forced full camera tumbling.
- Dodge/lunge damage protection does not reject otherwise eligible CC. CC interruption ends movement protection immediately. Rejected actions never spend movement resources.

## Final implementation defaults: boss, lifecycle and evidence

- Active boss bar does not regenerate before a break. A composite CC ability contributes the largest contained effect value once unless explicitly authored otherwise. Duplicate event delivery never counts twice. Additional CC during stagger/recovery does not prolong or restart either phase.
- Resolve breaking attack's health damage before the break transition; subsequent damage gains1.25 multiplier. Default stagger8s, then90s protected linear refill. At full, accept new contributions. No queued effects survive a phase boundary.
- Death/reset/scene exit clears transient attacks, CC, grapples, poses, camera roll and held input. Restart refills movement resources and boss bar. Authored settings support save/load/Undo; runtime progress/damage/cooldowns remain separate.
- Pause freezes gameplay timers; no catch-up attacks. Focus/menu/mode changes clear held input and restore cursor; traversal exits safely without teleport. Entity references are checked each use.
- Shared local actor identity, health, hostility and effect interfaces support player/enemy/boss verification in Play and exports. This is not live networking or proof of multiplayer PvP. Production race animation libraries and multiplayer authority are separate integration work; no false completion claims.

## Final acceptance gates

Specification status: finalized implementation baseline. Confirmed requirements plus labeled defaults are sufficient to proceed without another design interview. Adjust defaults through testing and record reasons. Step4/4A code remains incomplete until these gates pass.

- [ ] Verify all movement controls, resource rates, tap/hold boundaries, posture clearance, jumps, stairs and moving platforms.
- [ ] Verify dodge/lunge protection boundaries, interruptions, atomic charge spending and no stale inputs.
- [ ] Visibly demonstrate grapple wall/swing states, momentum, charge-on-miss, wall climb/jump, moving-character anchors, removal and collisions in both perspectives.
- [ ] Demonstrate original avatar, weapons, required animations, camera behavior and all movement meters.
- [ ] Verify moving/airborne attacks, geometry, all-target hits, occlusion, repeat/cancel behavior, shatter and exactly-once quest updates.
- [ ] Verify every CC restriction, duration, presentation, cleanse, air/grapple interaction, friendly rejection and player immunity boundary; include same versus other attacker and sleep waking.
- [ ] Verify all14 boss contributions, solo repeated casts,1,000 threshold,1.25 damage,8s maximum stagger,90s protected refill and full-only reactivation at varied frame rates.
- [ ] Verify creator save/load/Undo, Play isolation/reset, shared export behavior and genuinely clean-Windows dependencies; local smoke tests alone do not qualify.
- [ ] Run visible foreground checks, stop/notify/wait on focus failure, close owned completed test windows, save screenshots/results. Core tests do not prove visuals.
- [ ] Complete Step4/4A acceptance and update the durable task list before Step5. Steps1–3 remain complete; no broad retest of those steps is required. Report implementation, core checks, visible checks and deployment separately.

### Adjustable fall recovery boundary
The initial runtime defeat boundary is100 metres below the authored spawn height. Crossing it defeats the player and clears active attacks, healing, crowd control and grapple traversal. Restart restores health and movement charges. This is a provisional safety boundary for chasms, not final encounter tuning.

Standalone restart uses F5, keeping R reserved for the disabled future skill slot.
