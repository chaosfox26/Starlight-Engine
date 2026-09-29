# Reusable native objects

The scene menu now offers **Save selected shape / sculpt** and **Add saved object**.
Select a built-in shape or sculpt, save a `.starobject` file, then add that file to another scene.
Each insertion gets its own identity. Appearance, physics settings, sculpt geometry and routes
are preserved. Placement moves the route with the object; normal snapping still applies.
Undo removes the inserted copy without changing the saved file. Redo restores the copy.

This implementation supports native shapes and sculpts individually or in groups. Imported GLB
object-template packaging remains future work; the searchable browser supports GLB files directly. Unsupported
objects and malformed files produce errors rather than silently losing external geometry.
Files are versioned and limited to 32 MiB; saves use the existing atomic-write/backup path.

## Validation — 23 September 2026

The normal native editor and isolated audit editor builds succeeded. All 81 core checks
passed, including settings and sculpt round trips, distinct identities, translated routes,
Undo/Redo, unsupported versions and external-dependency rejection. Results are preserved
in `D:/projects/Starlight-Engine/analysis/reusable-objects-20260923/result.json`.
The new menu/file-dialog interaction has not yet been exercised live. These checks do not
certify the larger asset pipeline or visual acceptance.

## Automated scene building

The existing local command protocol now supports two additional operations:

```json
{"op":"insert-object","file":"stone.starobject","expectedSha256":"SHA256_OF_OBJECT_FILE","name":"Garden stone","x":5,"z":1}
```

`file` can be absolute or relative to the command-request file. Its hash is required;
the exact bytes checked are also the bytes decoded. `name`, `x` and `z` are optional;
omitting them preserves the saved object's values. Normal floor protection applies.

```json
{"op":"duplicate","id":"EXISTING_OBJECT_ID","name":"Second stone","x":7,"z":2}
```

Duplication assigns a fresh ID, preserves settings and moves routes with the copy.
Without a name override, it appends ` copy`. Imported objects can be duplicated inside
their scene, but that retains their existing asset reference and does not package it.

Both operations run inside the existing scene transaction and require the outer request's
current scene hash. An invalid operation or stale object hash prevents the entire batch
from saving, including earlier operations. Successful batches retain the scene backup.

All 84 core checks passed after this extension, including relative object paths, route
translation, independent duplication and stale-object rollback. Evidence:
`D:/projects/Starlight-Engine/analysis/object-commands-20260923/result.json`.

## Reusable groups

Select several native shapes or sculpts and choose **Save selected objects as group**.
**Add saved group** inserts a `.stargroup` file and selects the inserted objects together.
Their spacing, heights, appearance, physics and routes are retained. The first object in
scene order anchors placement at the camera focus; grid/object snapping applies to that
anchor. Floor protection lifts the arrangement as a unit rather than distorting it.
Undo/Redo applies to the entire insertion. Group files support 1–4096 objects within 32 MiB.

Duplicate and Delete now act on the complete current selection. Each is one Undo
action, and duplication translates the copies' routes with their objects.

The command protocol also accepts `insert-group`, with the same `file`, required
`expectedSha256`, and optional `x`/`z` fields as `insert-object`. Member names are preserved.

## Replacing and repairing models

Select an imported model and choose **Replace selected imported model**. Its identity,
name, transforms and route stay intact, and the change can be undone. This does not
convert shapes or sculpts into imported objects.

For a scene that cannot open because a model moved, use `--inspect` to list required
model paths and missing files, then use the existing guarded command protocol:

```json
{"op":"replace-model","id":"EXISTING_OBJECT_ID","asset":"replacement.glb"}
```

Relative replacement paths resolve against the request file. The replacement GLB is
validated before the scene saves; invalid replacements leave the scene unchanged.
Inspection checks file presence only, not geometry or runtime correctness.

Opening a scene in the editor now offers a **Locate missing model** file picker when
a required model has moved. The same repair flow applies when adding objects from a
saved scene. Shared missing references are repaired once. Cancellation leaves the
existing scene intact, including when another missing model was already located.
Successful repairs are unsaved edits; the source scene file is untouched until Save.
Existing but invalid GLB files still produce a validation error rather than being
silently substituted. Command tools remain noninteractive.

## Integrated validation

The integrated core run passed 500 checks with the geometry fixtures loaded. This
includes reusable groups, guarded insertion, model relinking and failure rollback,
missing-file reporting, and composition of portable scenes with relative model paths.
Evidence: `D:/projects/Starlight-Engine/analysis/asset-workflow-integrated-20260923/result.json`.
This count includes existing checks; it is not a count of newly implemented features.

The isolated editor run passed all 81 checks, including full-selection Duplicate/Delete
and Undo/Redo through actual editor commands. All 80 menu cycles completed with flat
tooltip/GDI counts and no growing USER-resource high-water mark. An earlier hidden-start
run failed two visibility checks; the harness now explicitly shows its test window.
Both results are preserved. Passing these checks does not certify the appearance of
the new controls or their interactive file-dialog workflow.

## Searchable asset browser

Choose **Browse assets…** in the scene menu, then choose your asset folder. The editor
remembers that folder. A background scan finds GLB models, saved objects, groups, and
scene files; search matches names and relative folders. The type selector narrows results
to models, saved objects, groups, or scene objects. Refresh picks up files added externally.

**Add selected asset to scene** creates independent objects with fresh identities and
normal placement/snapping. Scene files contribute objects without replacing the current
world settings. Each insertion is one Undo action. Files are validated when inserted;
being listed does not certify an asset's contents. Missing scene models use the repair flow.

The browser is currently a text list, without thumbnails. Scans exclude junctions and
symbolic links, limit depth and entries, and report partial/unreadable results. Only the
first 500 search matches are shown; refine the search to reach a smaller set. The scan's
time budget is checked between filesystem operations, not a timeout on an individual
blocked filesystem call.

The browser's insertion, search, and Undo command checks passed in the isolated editor,
with all 85 checks passing and resource counts flat across 80 menu cycles. An intentional
progress-file lock generated recorded reporting errors without hanging or losing the
final result: `D:/projects/Starlight-Engine/analysis/asset-browser-ui-final-20260923.json`.
This result predates the type selector addition. Appearance and interactive folder-picker
verification remain outstanding; the desktop capture tool could not inspect the window.

The type-filter data checks passed in the subsequent 505-check core run:
`D:/projects/Starlight-Engine/analysis/asset-type-filter-20260923/result.json`.

## Copy objects between scenes

Select one or more objects and choose **Copy selected objects** from the scene menu.
Open or create the destination scene, then choose **Paste copied objects**. The group
is placed at the camera focus with independent identities, preserved settings and
spacing, and translated routes. One Undo removes the whole paste; Redo restores it.

This is an editor-session object clipboard, separate from the Windows text clipboard.
It stays available across scene changes until the editor closes or another selection
is copied. Imported models retain absolute references to their original files, which
are revalidated before pasting. A missing/invalid model cancels the entire insertion.
Use portable export when copying to another computer or preserving external models.

The combined type-filter and Copy/Paste editor checks passed all 91 assertions and 80
resource cycles: `D:/projects/Starlight-Engine/analysis/asset-copy-paste-ui-20260923.json`.
GDI, USER, and tooltip counts stayed unchanged. This verifies command behavior,
including missing-model rollback, rather than visual polish or mouse interaction.

**Focus** now centers the camera on the combined bounds of the selected group instead
of only its first object. Other preset views retain their existing single-object behavior.
The final integrated editor run passed 92 checks and all 80 resource cycles with flat
GDI/USER/tooltip counts: `D:/projects/Starlight-Engine/analysis/group-focus-ui-20260923.json`.
Both normal and isolated editor builds include this change.
