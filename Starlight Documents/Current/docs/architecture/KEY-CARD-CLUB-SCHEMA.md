# Key-card schema 16: club-room additions (field reference and comments)

Written 2026-10-04. These fields extend schema 16 additively. Nothing was bumped to 17. Every addition is optional: a card that does not use them has the same bytes as before, and an editor save of such a card does not write any of the new keys.

Source: `editor/cpp/scene.hpp` (types), `scene.cpp` (validation), `scene_json.cpp` (reading and writing), `commands.cpp` (command line). Checks: `tools/checks/club_schema_checks.inl`. Status: implemented, compiled, headless-checked. Not runtime-verified (nothing was rendered or played).

## Coordinate convention

The card stores **editor space**: right-handed, Y up. The GameProject host is left-handed and mirrors z: `host_z = -card_z` (and the matching sign flips of yaw and rotation). The conversion happens once, in the host adapter. Nothing in the card or the engine core flips z. Names such as "(host)" in object names only describe the direction as seen in the host.

## Object fields (on a Character)

| Field | Type, range, default | Written when | Notes |
|---|---|---|---|
| Shape `cylinder` | Width and Depth are the radius (unit radius 1 scaled), Height is the height, standing on its base, 48 segments | always (Shape) | Solid body; may be Dynamic |
| Shape `ring` | Width and Depth are the outer radius, Height the thickness, `RingInner` the inner radius as a fraction | always (Shape) | Not allowed as a Dynamic body |
| `Glow` | 0..1, default 0 | not 0 | Above zero the cube, rectangle, trapezoid, octagon, cylinder, ring or sculpt is drawn emissive. The editor draws a fixed emissive strength; the value is the host's to use for strength. Rejected on lights, fire, models and other effect shapes |
| `Group` | 1..64 of letters, digits `- _ .`, default empty | not empty | Names a group that `Lighting.Modulators` can target |
| `RingInner` | 0..0.99, default 0.82 | not 0.82 | Only on a ring. 0 gives a full disc |
| `LightInner` | 0.1..1, default 0.8 | not 0.8 | Only on a light. Inner cone as a fraction of the outer half-angle. 0.8 is what the editor used before this field existed |
| `Follow` | object, absent by default | present | Only on a light. See below |

`Follow`: `Target` (name, default `focus`), `OffsetX`, `OffsetZ` (within 1000), `Lag` 0..60, `Wobble` 0..20, `PoolRadius` 0.05..50, optional `Housing` {`Width`, `Height`, `Depth` 0.01..1000, `Color` hex}, optional `Beam` {`Haze`, `Pool`, `InnerPool` 0..2}. The editor shows the light at its rest pose; the runtime that animates it is the host's.

## Scene blocks

| Block | Fields (ranges) | Notes |
|---|---|---|
| `Lighting` (optional) | `Mode` `authored` or `sun`; `KeyDirection` [3] not zero; `KeyIntensity` 0..100; `KeyIntensityFlat` optional 0..100; `Ambient` [3] 0..4; `AmbientGround` 0..4; `Clear` [3] 0..1; `FogMix` 0..1; `Modulators` | Absent means the Hour-driven sun applies as before. `Modulators` is `{name: {target: coefficient}}`, up to 16 modulators of up to 32 targets; a target is `Clear`, `Key`, `Ambient`, `FogMix`, `Spots` or `group:<name>`; coefficient -4..4. A `group:` target must name a Group that at least one object has |
| `Moods` (list, up to 32) | `Id`, `Clear` [3] 0..1, optional `Ambient` [3], `AmbientGround`, `KeyIntensity`, `FogMix` | Names are unique. The mapping from a game phase to a mood name stays in the host |
| `ActiveMood` | text, default empty | Must be one of the mood names. Used by the editor preview |
| `View` (optional) | `FovDegrees` 10..140; `Near` 0.001..100; `Far` above Near, up to 1000000; `OrbitRateRadiansPerSecond` 0..50; optional `FarWhenDistanceAbove` {`Distance`, `FarPerDistance`, `FarBase`}; optional `Follow` {`TargetHeight`, `TargetHeightPerY`, `DefaultDistance`, `DefaultHeight`}; optional `IdleOrbit` {`DelaySeconds`, `PeriodSeconds`} | The design file spelled the orbit rate `OrbitRateRadiansPerSecond`; that spelling is kept |
| `Zones` (list, up to 64) | `Id`, `Kind` (`raised-floor`, `anchor`, `bounds`, `spawn`, `crowd`), `Shape` (`circle` needs `Radius`; `box` needs `Width` and `Depth`), `X`, `Z`, `Height` (raised floor only), `Note` | Names are unique. `Shape` defaults to `circle` when absent |
| `Crowd` (optional) | `Lod` {`Grids`, `EnterPx`, `ExitPx`} (1 to 4 levels, equal lengths, grid 2..64, exit not below enter, pixels 0..4096); `Variants` 1..8; `CullSphere` {`CentreHeight`, `Radius`} | Defaults are the club values |
| `Cast.Models` (list, up to 32) | see below | Written only when not empty |

## Cast models

`Id`, `Model` (a `model-` id), `Role` (`lead` or `crowd`), `Variants` 1..8, optional `Rig` {`Clips`, `Joints` (name to name, up to 32 each), `HeightRule` {`AcceptRange` [low, high], `TargetHeight`}}, `Uv` {`Layout` `glb` or `atlas`; `Asset` a `blob-` id, required for `atlas`, forbidden for `glb`}, `Maps` ({`v0`...: {slot: `texture-` id}}, slots `basecolor`, `normal`, `roughness`, `ao`, `furflow`, `sparkle`; variant keys must be below `Variants`), `Effects` ({`fur`: {`Maps` [slots], `Profiles` {name: {`length`, `softness`, `stiffness`, `sheen`, `sway`, `density`}}}}), `Requires`, `Wants`.

Ids are checked by shape only (prefix plus 64 lower-case hex digits). Whether the library holds them is checked by the loader, by `cast_missing_assets`, and by the `set-block` command (which can be told not to).

### Requires and Wants

Items: `model`, `skeleton`, `uv:atlas`, `map:<slot>`, `effect:<name>`.

| Case | Result |
|---|---|
| Item in `Requires` and declared | Mandatory: the host fails loading visibly if the content is missing or damaged |
| Item in `Wants` and declared | Optional: a failure is a visible warning and only that effect is switched off |
| Something declared (a map, an effect, a UV blob) that no list names | Creates no item and no warning |
| Nothing declared (a model with no fur, no sparkle, no atlas) | Passes; nothing is requested |
| Item named in either list but not declared | Card error at validation. `skeleton` needs a rig with joints; `uv:atlas` needs layout `atlas`; `map:<slot>` needs the slot in every variant; `effect:<name>` needs the effect declared with its maps present in every variant |
| Same item twice, or in both lists; unknown item | Card error |

The Requires/Wants evaluation at load time (Ready, Degraded, Failed) belongs to the host readiness code and is not part of the card format.

## Command line

`--commands` operations added (see `--describe`): `set-block` {`block`: `Lighting`, `Moods`, `ActiveMood`, `View`, `Zones`, `Crowd` or `Cast`, `value` (JSON of the block, `null` clears), optional `verifyAssets` (default true, for `Cast`)}; `follow` {`id`, any of `target`, `offsetX`, `offsetZ`, `lag`, `wobble`, `poolRadius`, `housing`, `beam` (null removes it), or `clear`: true}. `shape` and `transform` gained `glow`, `group`, `ringInner`, `lightInner`; `shape` also takes `width`, `height`, `depth`, `color`. `--inspect` returns every block and, for a cast, `castAssets` and `missingCastAssetCount`.

## Not in the card yet

Colourway tables, entity proxy appearances, gear and tattoo placeholders, material tweaks and the join look from the design inventory (R23 to R27, R30) are host data and are not part of this schema step. The editor has no authoring interface for any of the new fields.
