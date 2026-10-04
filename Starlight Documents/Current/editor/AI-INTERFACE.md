# Starlight Editor: local AI interface, version 1

This interface belongs to the editor, not to a particular AI provider. A local model, Claude, Codex, Gemini, or another assistant can use it through a tool able to execute local programs. A cloud model needs a local connector with that permission; the model name alone does not grant access. No provider SDK, API key, paid model, network listener, or model runtime is embedded here.

The editor executable accepts JSON commands and writes a JSON result. These commands work without opening a window. Scene edits use the same scene validation and backup writer as the GUI. An open editor refreshes an externally changed saved scene when it has no unsaved changes; otherwise it preserves the human's edits and reports the conflict. Edits address stable character IDs, not list positions.

Executable after building:
`D:\projects\Starlight-Engine\out\native\Release\Starlight.NativeEditor.exe`

## Discover and inspect

Run the executable with `--describe --result <absolute-report.json>` to discover capabilities.
Run with `--inspect <absolute-scene.starscene> --result <absolute-report.json>` to read a scene, its character IDs, and its SHA256 revision fingerprint.
Use a separate result file, never your scene, asset, or request file. Wait for the process to exit before reading the result. Exit 0 means success; exit 1 means failure. Results also contain `success` and an explanatory `error` on failure.

## Change a scene

Run with `--commands <absolute-request.json> --result <absolute-report.json>`.

Example request (replace the path, fingerprint, and ID with values from inspection):

```json
{
  "schemaVersion": 1,
  "scenePath": "D:/my-project/My scene.starscene",
  "expectedSha256": "CURRENT_SHA256_FROM_INSPECT",
  "operations": [
    {"op": "move", "id": "CHARACTER_ID", "x": 2, "z": -1},
    {"op": "route", "id": "CHARACTER_ID", "points": [{"x": 2, "z": -3}, {"x": 4, "z": -3}]}
  ]
}
```

Supported operations:

| Operation | Fields | Behavior |
|---|---|---|
| add | name, asset, x, z | Import an existing local GLB, add a character, assign a stable ID |
| move | id, x, z | Move the character and translate its existing route by the same amount |
| route | id, points | Replace its route; empty points clears it |
| configure | id, x, z, speed, size | Apply the same validated exact values as Advanced mode; translate the route with the position |
| rename | id, name | Change its display name |
| remove | id | Remove it from the scene; the asset file is preserved |

Coordinates are metres on the floor: X and Z. The preview floor spans -12 to +12 on each axis. Character assets remain referenced by absolute path; this is not a portable asset-package exporter. [Superseded 2026-10-04: a model is referenced by a library id, not a path. `add` and `replace-model` import the GLB you name into the library (or accept an existing model id), `merge-scene` no longer rebases model paths, and portable export bundles the referenced assets.]

The whole batch validates before any scene write. A stale fingerprint rejects the batch. A file lock serializes participating editor writers, with another fingerprint check before saving. Existing scene contents are backed up on every replacement. A repeated request carrying the old fingerprint is rejected; inspect again before another edit. This is trusted local tooling, not a security sandbox against processes that already have filesystem access.

## Current scope

The interface handles saved editor scenes and route previews. It does not yet operate all engine or editor features, create game logic, animate a skeleton, or control native runtime playback. Project/preset selection and native launch are currently available through the GUI. Vendor DLL updates and installers remain future work. Existing `.star` projects and `.startest` files remain separate from `.starscene` preview documents.


## 2026-09-16 native builder extensions
Use bin/terrain-builder/Starlight.Editor.exe. Scene schema is 3; command envelope remains 1. Existing expectedSha256 and backup rules apply.
- `shape`: shape/name/x/y/z; supported cube, rectangle, trapezoid, octagon, fire, light, lava. Three fire/light objects max.
- `transform`: id, optional y/width/height/depth/rotationX/rotationY/rotationZ/keepAboveFloor/color/lightIntensity/lightRange.
- `terrain-brush`: tool (raise/lower/smooth/flatten/paint/hill/crater/plateau), x/z within ±12; radius .4–5, strength .2–4, seconds >0 to10, target ±20, paint index0–4. GUI uses the same TerrainData brush. Stamps apply once independent of seconds.
- `terrain-water`: enabled, level ±20, optional lava boolean.
- `terrain-stamp`: id of a procedural shape to copy its upper surface into terrain; original remains.
No commands control music, camera, snapping toggles or live scene playback yet. Do not claim complete GUI/AI parity.
