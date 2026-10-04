# Portable editor scenes

> **Update 2026-10-04 (key-card integration).** Parts of this page describe the format before schema 16. Those statements are marked "Superseded" below and the original wording is kept as history. Current behavior: a portable export is a folder holding `scene.starscene`, `manifest.json` and `assets/<id>.slasset` for every engine asset the scene references (sculpts, terrain and imported models, each a content-addressed id). The old `assets/<sha>.glb` copies no longer exist. The manifest lists each asset's SHA-256 and size plus the scene hash, and verification also requires every referenced asset to be listed, to match its id, and to be read from disk. Scenes no longer hold model file paths or embedded geometry, and scene files from before schema 16 are refused with a plain-English "conversion required" message until they are converted with Starlight.SceneMigration. All of this is compiled and headless-checked only: no export was transferred to another machine and no player was run on an export. See [KEY-CARD-ARCHITECTURE.md](architecture/KEY-CARD-ARCHITECTURE.md).

Choose **Export portable scene** in the scene menu and choose a new `.starbundle`
folder name. The exporter creates a folder, not an archive. Copy the entire folder
to another location and open its `scene.starscene` in the native editor.

The export includes the current scene, embedded GLB model files, and a manifest
recording file hashes. Identical models are stored once. Shapes, sculpts, terrain,
routes and other scene settings remain in the scene file. [Superseded 2026-10-04: the export carries every engine asset the scene references (sculpts, terrain and models) as `assets/<id>.slasset`; sculpt and terrain geometry is no longer stored in the scene file.] The original scene and
models are not modified. Existing destinations are refused. Failed exports never
publish a partial destination.

Relative model paths are resolved against the scene file's folder on opening.
Saving models that live inside that folder retains relative paths. Saving elsewhere
does not automatically copy models; export again when making a portable copy. [Superseded 2026-10-04: scenes no longer hold model file paths. A model is a library asset named by id. Saving a scene copies a verified copy of each model it needs into the primary library, and a portable export bundles it.]

Models requiring external GLB URIs are rejected; use models with embedded resources. [Still true; since 2026-10-04 the check happens when the model is imported into the library.]
This exports what the native editor currently supports. It does not add animation,
new material support, a game executable, or platform packaging.

## Automation and integrity checks

```text
Starlight.NativeEditor.exe --export-portable scene.starscene new-folder --result export.json
Starlight.NativeEditor.exe --verify-portable new-folder --result integrity.json
```

Verification checks the export-time hashes, asset sizes, folder containment and
manifest coverage. Editing an exported scene invalidates its export-time scene hash;
export a fresh copy to establish a new manifest. Hash verification detects changes,
not trusted authorship or runtime correctness.

## Reusing arrangements containing imported models

**Export selected objects** creates a portable scene containing only the selected
objects and their required models. It excludes the source terrain. Unlike `.stargroup`
files, this workflow supports imported GLB models as well as native shapes and sculpts.

Use **Add objects from another scene** to bring those objects into your current scene.
The destination terrain, lighting and world settings are preserved. Imported objects
get fresh identities and are selected together; their spacing and routes move together.
Undo removes the entire insertion. This also works with ordinary saved scenes whose
model files are still available. [Update 2026-10-04: a source scene must be a schema 16 key card, and the models it names must be in the library or in a bundle beside it.]

Automation supports a guarded operation within the existing scene command request:

```json
{"op":"merge-scene","file":"source/scene.starscene","expectedSha256":"SOURCE_SCENE_HASH","x":4,"z":2}
```

Relative source paths resolve against the request file; model paths resolve against
the source scene. [Superseded 2026-10-04: model paths no longer exist; models come from the library by id.] The source hash must match. Without `x`/`z`, placement starts from
the source arrangement's first object. The existing destination scene hash and backup
protection still apply.

## Verification

The focused core suite passed 487 checks with the existing geometry reference loaded.
New coverage includes shared-model deduplication, relocation and render-data creation,
relative paths after saving, refusal to overwrite existing folders, failed-export
cleanup, and rejection of corrupt assets or absolute manifest paths.
Evidence: `D:/projects/Starlight-Engine/analysis/portable-scenes-verified-20260923/result.json`.
Live export-menu interaction and actual transfer to another computer remain unverified.

The later integrated run passed 500 total core checks, including selection-only export
and guarded composition of a portable model scene. Evidence:
`D:/projects/Starlight-Engine/analysis/asset-workflow-integrated-20260923/result.json`.
