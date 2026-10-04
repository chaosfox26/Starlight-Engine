# Key-card architecture: scenes, the shared asset library, migration and readiness

Written 2026-10-04 during the key-card integration. This is the one guide to how a scene is stored, where its content lives, how old files are converted, and what "ready" means. It describes what exists in the source tree today and says plainly what is still in progress or not verified.

Status of record for the integration work: `INTEGRATION-CHECKLIST.md` in the recovery work folder (`D:\projects\_recovery\claude-fix-20261002`). If this guide and the checklist disagree, the checklist wins and this guide needs a correction.

## How to read the status words

| Word | Meaning here |
|---|---|
| Implemented and compiled | The code is in the tree and the targets build. |
| Headless-checked | `Starlight.NativeCoreChecks` (and similar console tools) ran without a window, a renderer or a scene being played. |
| In progress | A worker is still on it. The text describes the intended rule, not a finished fact. |
| Runtime-verified | Seen working in a running editor, player, host or renderer. **Nothing in this guide is runtime-verified.** Scene playback, rendering and benchmarks were not run during this work and are not authorized until the owner says so. |

No frame rates, no visual-equivalence claims and no C++23 conformance claims are made anywhere in this guide.

## 1. The key-card contract

A scene file is a **key card**: configuration plus references. It holds object lists, positions, colours, behaviours, settings, rules and quests. It does not hold geometry. Anything large or authored (sculpted meshes, terrain height and paint grids, imported models, textures) is a content-addressed **asset** in one shared engine library, and the card names it by id.

Rules that follow from this:

1. Every entry point (editor Open, Play and Walk, the player, the command line, bundles, portable and game export, the hosts) reads and prepares scenes through the same shared services. No entry point has its own loader.
2. The runtime never reads the old format. An old file produces a plain-English "conversion required" error. Reading old files lives only in the separate migration tool (section 7).
3. A key card never silently continues without content it needs. A missing or damaged required asset is reported (`AssetMissing`), not skipped.
4. Opening a file never changes it on disk.
5. A scene is "ready" only after the CPU preparation and a real GPU fence (section 9), never because a timer ran out.

## 2. Schema 16 (the key-card format)

- Scene JSON carries `SchemaVersion: 16` (`EngineAssets::KeyCardSchema`, `editor/cpp/engine_assets.hpp`). A file below 16, or one that still embeds geometry, throws `ConversionRequired`. A file above 16 is refused.
- A sculpted object stores `"Sculpt": {"Asset": "sculpt-<hash>"}` instead of the position, index and material-coordinate arrays.
- Terrain stores `"Terrain": {"Asset": "terrain-<hash>", ...}`. The asset holds the height grid, the paint grid and the terrain cut volumes. The small settings (lava, water enabled, water level, breakable, destruction health and radius) stay in the card.
- An imported model object stores the model id in `Asset` (`"model-<hash>"`), not a file path. `Character.Asset` is empty or a model id; a path in schema 16 throws `ConversionRequired`.
- Encoding a scene writes the assets it references into the library as a side effect (`to_json` of a sculpt or terrain calls the store function), so a saved card always has its content stored.
- Optional blocks are being added to the schema by another worker for the GameProject club room (scene lighting, moods, view, zones, crowd, cast, and light follow behaviour; declarations are present in `editor/cpp/scene.hpp`). They are additive and optional and the schema number is still 16 at the time of writing. They are **in progress** and not described further here. The design is in `agents\clubroom.md` in the work folder.

Implemented and compiled; headless-checked. Not runtime-verified.

## 3. The one shared engine asset library

One library serves the editor, the player, the hosts, the command line, importers and exporters. There are no per-scene asset folders as a design.

| Kind | Id prefix | Payload | State |
|---|---|---|---|
| Sculpt | `sculpt-` | Sculpt mesh arrays | Implemented and compiled; headless-checked |
| Terrain | `terrain-` | Height grid, paint grid, cut volumes | Implemented and compiled; headless-checked |
| Model | `model-` | The validated GLB bytes exactly as authored | Implemented and compiled; headless-checked (36 model checks) |
| Texture | `texture-` | The PNG or JPG bytes exactly as authored | Implemented and compiled; import runs the strict PNG integrity check (see Image integrity policy) and a header check for JPEG (SOI/SOF/EOI); size 1 to 16384 per side |
| Blob | `blob-` | Opaque typed data, for example GameProject atlas UV streams | **In progress** (another worker). The kind and prefix are declared in `engine_assets.hpp`; this guide makes no claim about its import, validation or checks |
| Cast | (declared in `scene.hpp`) | A per-card description of a skinned character set: models, texture slots, rig names, effects, and per-asset `Requires` / `Wants` | **In progress** (another worker). See section 9 for the rule it implements |

One import function per kind lives in `editor/cpp/assets.cpp` (`import_model`, `import_texture`); every caller uses it: editor asset browser, Replace model, Import character, command line (`--import-model`, `--import-texture`, `add`, `replace-model`), the repair flow and the migration tool. Parsed models are shared in memory by id (`load_model`, 256 entries and 512 MiB, least recently used evicted, an entry someone holds is never evicted).

## 4. Content-addressed ids and integrity rules

- **Identity.** An id is the kind prefix plus the lower-case SHA-256 of the kind (as 4 little-endian bytes) followed by the payload. The same bytes always give the same id wherever the file came from; a different kind with the same bytes gives a different id. A changed or damaged file can never be mistaken for the original.
- **File layout.** `<id>.slasset` is a 20-byte header (magic `SLAS`, file version 1, kind, payload length) followed by the payload.
- **Reads are verified.** Before a stored file is trusted, the engine checks the magic, version, kind, length and that the hash of the payload equals the id. A mismatch is `AssetMissing` with a plain-language message, never a silent fallback.
- **Writes are durable first.** `put` verifies any file already present under that id, writes with the atomic write path, and only then makes the asset available from memory. A failed write throws. The memory copy is bounded (1 GiB, oldest out first).
- **Memory is not disk.** A running process can still serve an asset from memory after its file was deleted. The verification APIs (`verify_on_disk`, portable verify, the migration tool's own check) always read from disk.
- **Duplicates collapse.** Identical content from different scenes or different source paths is one library file. 64 GameProject files imported in the staging run gave 64 distinct ids because no two had identical bytes.
- **Orphans accumulate.** Nothing deletes unreferenced assets yet; there is no collect tool, and backups keep old scenes alive. Never delete library files by hand.
- **Unknown or malformed ids** are refused or reported missing (`valid_id`); a file path where an id is expected is reported missing, not loaded.

Evidence: 22 identity and durability checks in `tools/checks/key_card_checks.inl`, 36 model and texture checks, headless only.

## 5. Root registration (in progress)

**One logical library, made of registered roots.** This section states the intended rules (the Codex decision). Code for it is present in `editor/cpp/engine_assets.hpp/.cpp` and is being completed by another worker; its compile and check status is not recorded in the checklist, so treat everything here as in progress.

Intended rules:

1. There is one logical library made of one or more **registered roots**. Callers never name a root to read an asset; the library finds the id in any usable root.
2. There is an **in-repo starter library** (`library/` in the engine checkout) that ships with the engine. It is read-only for normal use. It does not exist yet: `D:\projects\Starlight-Engine\library` has not been created.
3. There is one **external durable store** for the large content: `D:\projects\Starlight-Assets\library`. It does not exist yet either. The GameProject staging import (64 files, about 490.5 MiB) sits in the work folder, not in either location.
4. **Registration is persisted.** The registered roots are saved in a registry file in the Starlight data folder (`asset-roots.json`, format `starlight-asset-roots`, version 1). Only one root may be writable (the primary root); every registered root is searched.
5. **A root is a library only if it has a marker.** `library.json` in the root folder must read `{"format":"starlight-asset-library","version":1}`. A folder without a valid marker is never treated as a library. A newer marker version is refused, not guessed at.
6. **Resolution order is the same for every tool.** An explicit root (tests, tools, or the `STARLIGHT_ASSET_LIBRARY` override, which then stands alone), then the persisted registry, then the library shipped with the engine, then a bundle's own `assets` folder.
7. **Nothing is skipped silently.** An unusable root (missing, bad marker, damaged registry) is reported with the reason, and an asset that cannot be found anywhere fails loading visibly.
8. Mutable derived data (indexes, decoded caches) never lives inside a library root.

Open points: the repository size question for the starter library (the GameProject set alone would be roughly nine times the current working tree) is a decision for the owner; large content going to the external store is the direction.

## 6. Portable export and the dependency closure

A portable scene export carries the card plus exactly the assets it references, no more and no less.

- Layout of the exported folder: `scene.starscene`, `manifest.json`, and `assets/<id>.slasset` for every referenced asset (sculpts, terrain, models). The older layout `assets/<sha>.glb` no longer exists.
- The closure is computed from the card's own references (`scene_asset_ids`). Each file is read from the library with verification, written to the bundle, and listed in the manifest with its SHA-256 and byte count. The manifest also records the scene hash.
- Verification checks the scene hash, every listed file's hash and size, folder containment, and that every id the card references is listed in the manifest and passes the on-disk identity check. A bundle missing a model, holding a damaged one, or whose manifest omits one fails. Verification reads from disk even if the asset is cached in memory.
- Existing destinations are refused; a failed export never publishes a partial folder.
- Game export reuses the portable export for its content. A player opening an exported folder registers `<folder>/assets` as a search root.
- Saving a reusable object or group that uses a model bundles the model beside the file; saving fails (and writes nothing) if the library does not hold it.

Headless-checked. A real transfer to another computer and the player reading an exported game's models were not run.

## 7. The migration tool

`Starlight.SceneMigration` (source `tools/migration/scene_migration.cpp`, usage in `tools/migration/README-scene-migration.md`) is the only code that reads the old format. It never opens a window and never runs a scene.

Workflow:

1. **Inventory.** List every `.starscene`, `.starobject` and `.stargroup` under one or more roots, with schema, object count, embedded geometry size, model links, a SHA-256 and a class: `generated-check` (disposable scenes the checks regenerate), `audit-evidence`, `authored`, or `user-scene-outside-repo` (listed, never opened or converted). Reports only.
2. **Archive.** Before anything is converted, the original is copied to an archive folder and `{path, archive, sha256, bytes}` is added to a hash manifest. An archive entry is never overwritten; a hash conflict aborts.
3. **Convert.** Embedded sculpt and terrain geometry becomes library assets; imported model files (GLB paths) are imported with the same `import_model` the editor uses; the card keeps only ids. Every other field is kept.
4. **Verify.** Each stored asset is re-read from disk and its identity recomputed; the engine decodes the new text; every decoded geometry equals the old arrays bit for bit; the re-encoded text is stable; no non-geometry field may be dropped or changed. Any failure means the output is not written. `--verify` and `--verify-all` repeat this independently.
5. **Refuse unsafe cases.** A missing or invalid model file fails the whole document with every missing path listed; nothing is silently dropped. A document at a newer version is refused.

Behaviour of the runtime: an old file gives a plain-English conversion-required message that names the file and tells the user to convert it with the tool. **Originals are never modified by opening them**; in-place conversion needs an explicit flag and still archives first.

State: the tool and a staged run are done and headless-checked. Staging over the whole repo (40 documents: 20 generated checks, 5 audit evidence, 15 authored) converted 20 of 20 convertible documents and verified 20 of 20, with the scene text going from about 13.96 MiB to about 1.04 MiB (the Moonseed Glade starter scene from 10.6 MB to 0.7 MB) and 14 library assets. **The conversion has not been applied to the repository.** Every scene file currently in the repo (schema 1 to 15) is still in the old format, so the current editor and player will say conversion is required for them until the conversion is applied with the archive (checklist item 4, not started). The 20 generated check scenes are meant to be regenerated, not converted.

## 8. Reusable objects and groups: format version 2

- `.starobject` and `.stargroup` files are `Version: 2`: geometry by library reference, like a scene. Version 1 files (embedded sculpt) throw `ConversionRequired`; a version above 2 is refused.
- Models are allowed in objects and groups. The earlier "imported model packaging is not available" limitation is gone.
- Saving bundles the referenced assets into an `assets` folder beside the file. Saving fails with `AssetMissing` and writes nothing if the library does not hold a needed asset; opening an object whose model is nowhere raises `AssetMissing`.
- Ids inside an object are normalized on save (the object `Id` is cleared; group members become `member-N`), and links between members are renumbered with them.
- The migration tool converts old v1 files to v2 and bundles their assets beside them.

State: format and tool implemented, compiled and headless-checked. There are no `.starobject` or `.stargroup` files in the repository, so the conversion path has only been proven on synthetic files.

## 9. The readiness contract

Goal: nothing is shown as ready, and no simulation advances, until the scene's content is genuinely prepared on the CPU and uploaded and fenced on the GPU. The same rule is shared by the editor, the player and the hosts, through `editor/cpp/scene_preparation.hpp` (editor, player) and `deep/engine/host_readiness.h` (Empty and GameProject hosts, which cannot include editor code).

**States.** Editor and player: `Preparing` (the default, meaning "not verified"), `Ready`, `Incomplete`, `Failed`, plus a `cancelled` flag. Hosts: `Preparing`, `Ready`, `Incomplete`, `Failed`, `Cancelled`. `Ready` is reachable only after the final fence. A timeout can only produce `Incomplete`, never `Ready`. A renderer error or a lost device is `Failed`. The failure text is plain English and names the reason.

**Generation tags.** Each scene install and each bulk preload of resident geometry returns a generation number. The host asks for the GPU fence only after it has seen its own scene generation installed and its own preload generation submitted. The render thread records the generations that were installed when it handled the fence, and the host checks the recorded values cover its own; if not, the result is `Failed`. This removes the older weak test (a rebuild counter that any rebuild could satisfy).

**GPU fence.** After the CPU stages (geometry, collision, every breakable's prepared fracture) the host does: preload submitted, scene accepted, one real GPU fence confirmed. Progress shown to the user is completed tasks over total tasks, not a timer. The old fixed "30 frames" criterion is gone.

**Simulation gating.** Playback is off during loading and turned on only after `Ready`. While loading, the message pump leaves timer and frame-update messages queued, discards keyboard and mouse input, defers a window close (the close is carried out afterwards) and keeps the quit message. Nothing can start a nested reload or tear down the renderer mid-load.

**Splash and ordering.** The splash or loading screen is the first visible surface and is painted before the file is parsed. The editor no longer treats the end of the 9.2 second splash animation as "ready"; the player no longer parses and starts the scene before its window exists.

**Hosts.** The Empty and GameProject hosts use the std-only `host_readiness.h`: named required content items (`Pending` / `Ready` / `Missing` / `Failed`), upload-submitted, a non-blocking fence, `Ready` only when no item is missing or failed, and a visible failure listing every missing path with exit code 2. GameProject's join message is sent after `Ready`.

**Per-asset content requirements (settled rule; implementation in progress).** Content requirements are per asset and are declared in the shared key-card or asset configuration, not in host code:

- UV maps are required where the asset needs them.
- Fur and sparkle maps are required only when the asset declares them mandatory; otherwise they are optional, and a requested optional effect that cannot load produces a **visible warning** and just that effect is turned off.
- Required content that is missing or damaged fails loading visibly.
- An asset that declares no fur, sparkle or atlas UV must not be refused.
- The card vocabulary is `Requires` (mandatory) and `Wants` (optional but requested), with items such as `model`, `skeleton`, `uv:atlas`, `map:<slot>` and `effect:<name>`; an item named in either list but not declared by the asset is a card error.

Current honest state: the schema declarations for `Cast` with `Requires` / `Wants` are in `scene.hpp`, but `host_readiness.h` has no optional or warning item state yet, and the GameProject host **still requires** atlas UV, fur-flow and sparkle maps for all four models (it will refuse to start where the old code degraded). Changing that is the club-room and content stages (checklist items 12 and 13), not done.

State: editor, player and hosts compiled; logic of `host_readiness.h` checked with a standalone fake-device test; nothing runtime-verified. Not known: whether a hidden window completes the fence wait, whether the server tolerates a connected-but-not-joined socket for the loading period, and how the first physics and environment steps behave when playback now starts after `Ready`.

## 10. The host adapter story

- There is one rendering class used two ways. The editor viewport gets a text-transformed copy of it and draws a serialized scene packet; the Empty and GameProject hosts include it unchanged and draw directly from code. The hosts read no `.starscene` and use no engine asset library today.
- Done (compiled, nothing run): both hosts go through the shared readiness core (section 9); the GameProject MSAA resolve was changed to follow the shared device's resolve protocol (the defect and fix are inferred from reading the code, not seen on screen); the Empty host runs interactively without `--bench`.
- Designed, not built: the GameProject club room as a configuration-only key card, loaded through a small adapter (`keycard_view.h` plus one adapter translation unit that links the editor core library) so host code never includes editor headers. The draft card and the design, with the decisions that need the owner (coordinate handedness, schema bump, which GameProject channels are mandatory), are in `agents\clubroom.md`.
- Staged, not in the repo: the 64 GameProject models and textures imported into a staging library in the work folder (`gameproject-import\library`), verified against their originals. Four atlas UV `.bin` files cannot be imported until the Blob kind is finished. The hosts cannot yet read assets from the library (their loaders take file paths), so nothing uses the imported set.
- Blocked: host content through the library (checklist items 10, 12, 13).

## 11. C++23 status and caveats

- Target: all Starlight-owned code at C++23, C++26 later. CMake variables `STARLIGHT_CXX_STANDARD` and `STARLIGHT_HOST_CXX_STANDARD` default to 23, and the physics wrapper library follows the first.
- **MSVC has no C++23 switch.** The installed MSVC 19.51 offers `/std:c++latest` only, and that is what a request for 23 becomes. `_MSVC_LANG` there is above the C++23 value and the library mixes C++23 and early C++26 features while some C++23 language features are absent. "It compiles with `/std:c++latest`" is not a statement of C++23 conformance, and this guide does not make one.
- What was observed (headless, compile and link plus console checks): a trial build of all default targets at the latest standard produced zero new warnings compared with the earlier standards and the core checks passed with the same count as the older build. One Starlight-owned file needed a fix for `std::filesystem::path::u8string()` returning `char8_t` strings in C++20 and later; the fix is applied in the tree (a helper in `deep/src/runtime.cpp`). The trial predates that fix, and a separate rebuild of the launcher after it is not recorded.
- Third-party code stays at C++17: the Jolt physics source pins itself at 17. Third-party headers can behave differently under a strict compiler (one Streamline header fails under strict clang C++23; MSVC takes a different branch).
- A second compiler is not green: the only clang on the machine (a packaged copy under `out/`) fails to build the engine even at C++20 for unrelated reasons (a template keyword in a check file, a function-pointer cast in a vendor header, link settings). Nothing here shows portability to other compilers.
- A successful compile does not prove runtime equivalence. The `u8string` case compiled and would have silently changed JSON output.
- A C++26 evaluation needs a newer MSVC with an explicit switch, or a repaired clang baseline. Not started.

## 12. Upscaler build integration

- **Build behaviour.** `cmake/Upscalers.cmake` is included by the root `CMakeLists.txt` for the Empty and GameProject host libraries. By default the DLSS (Streamline), FSR (FidelityFX), XeSS and Anti-Lag 2 backends are compiled into both host libraries when their SDKs are present. Each vendor has an option (`STARLIGHT_UPSCALER_DLSS`, `_FSR`, `_XESS`, `_ANTILAG2`, all default ON). With an option ON and the SDK files missing, configuration **fails** and lists every missing file; with it OFF the vendor is compiled out and requests for it fall back to the off backend with a log line. Nothing is skipped silently. The SDKs are not in the engine repository; the default SDK folder points into the GameProject tree.
- **Runtime default is off.** `STARLIGHT_UPSCALER_DEFAULT` defaults to `off`; the shared renderer's default request and the GameProject generated flag default were changed to follow it. `-DSTARLIGHT_UPSCALER_DEFAULT=auto` restores automatic choice.
- **The editor viewport stays upscale-off** by standing rule; the module refuses to configure `Starlight.Viewport`.
- **Vendor libraries load at run time** (`LoadLibrary`); no vendor import library is linked. The module copies the runtime DLLs beside each host library by default (about 250 MB per host), or beside `Starlight.exe` with `STARLIGHT_UPSCALER_RUNTIME_LAYOUT=launcher`. Do not put them in the game-export runtime folder: the exporter treats a Streamline interposer there as a ray-reconstruction runtime and requires further files (by reading the code; not run).
- **Notices the module copies** next to the DLLs, under `licenses/<Vendor>/`, plus a configure-time `upscaler-manifest.txt` with the SHA-256 of every copied file:
  - Streamline folder (DLSS): the Streamline `license.txt`, the engine's `licenses/NVIDIA-RTX-SDK.txt`, `3rd-party-licenses.md`, and the `nvngx_dlss`, `reflex` and `nis` licence files.
  - FidelityFX folder (FSR): the FidelityFX `docs/license.md` and `3rdpartynotice.md`.
  - XeSS folder: `LICENSE.txt` and `third-party-programs.txt`.
  - AntiLag2 folder: `LICENSE.txt` (header-only SDK).
  A missing expected notice file produces a configure warning.
- **State.** Configured, compiled and linked in both host libraries with all four vendors, with some vendors off, with all off, and with an incomplete SDK path (which failed as intended). **Nothing was run.** Not verified: any upscaler at runtime; whether the FidelityFX loader finds its provider DLLs beside the host library rather than beside the executable (the fallback is the `launcher` layout); which `nvngx_dlss.dll` version is intended (a different-version backup sits beside it); Debug builds; non-MSVC toolchains.

## Image integrity policy

Dated 2026-10-04. Decision recorded from Codex: strict PNG integrity is accepted. Headless-checked only; no renderer, GPU or editor run.

**One rule, three places.** The same checks apply when an image is imported (`import_texture`), when the editor judges readiness (`check_asset_ready`) and when a host loads it (`model::loadTextureFromMemory`, `model::probeTextureDecode`). Decoding with the Windows imaging component (WIC) is never the integrity check, because WIC accepts damaged PNGs (stale checksum, cut zlib stream, missing rows) without an error. The strict in-house validator runs first, WIC decodes second, and the size WIC reports must equal the PNG header.

**What is refused, always with a message that names the chunk and byte offset and says why.** A wrong checksum in any chunk, including optional (ancillary) ones; bytes after the IEND end marker; a missing or repeated header, palette or transparency chunk where the format forbids it; an unknown critical chunk; a broken zlib stream, wrong Adler-32 checksum, extra bytes after the zlib stream, wrong amount of pixel data or an invalid row filter byte; any truncation. The PNG specification lets a reader ignore a damaged ancillary chunk and trailing bytes, and the runtime loader used to do that. It no longer does, so a file that the editor calls damaged cannot load in a host. Valid variants are still accepted and fully verified: every colour type and bit depth, interlaced (Adam7) images, palettes with fewer entries than the depth allows, zero-length or split IDAT chunks, unknown ancillary chunks with a correct checksum, and the PNGs written by the Windows encoder. Not detected: palette indices that point past the palette, and damaged JPEG image data that still forms valid codes (see `agents\png-integrity.md`).

**Our own pipeline.** `import_texture` stores the authored bytes unchanged, so the import itself is the compliance gate: a PNG that fails the strict validator is not stored, and the refusal says "fails the Starlight image integrity check". Nothing else in the engine writes library textures. PNGs the editor writes through the Windows encoder were re-encoded in a check (gray 8 and 16 bit, 24 and 32 bit, 64 bit RGBA, interlaced and not) and pass import, readiness and the runtime loader. JPEG import remains header-level (start, frame header, end marker, size); readiness adds a structure walk and a full decode.

**One size limit.** `deep/engine/model/texture_limits.h` is the only definition: at most 16384 pixels per side, a PNG pixel stream of at most 1 GiB, and a decoded-memory budget (default 1 GiB, configurable with `setDecodedBudget`) checked as width x height x channels x bytes with overflow detection. Import, readiness and the runtime loader all use it, so an image can no longer import and then fail readiness on size. An oversize image is refused with a text that names the size, the limit and the rule that Starlight never downsamples silently; reduce or tile the image first. Known consequence: a 16384 x 16384 RGBA PNG is refused because its pixel stream (1 GiB plus filter bytes) is just over the 1 GiB stream cap; gray, RGB and smaller images up to 16384 per side are accepted. Limits of the graphics device (maximum texture size, memory) are not known to this code and are checked later, when the texture is created on the device.

**Prepared mip cache (derived data, never authoritative).** Hosts can ask for a content-keyed cache of the derived mip chain (`sharedCache` in `TextureLoadOptions`). Location: `<data root>\AssetCache\textures`, where the data root is `STARLIGHT_DATA_ROOT` or `%LOCALAPPDATA%\Starlight` (the same data folder and the same `AssetCache` folder the asset library uses for derived data). Key: SHA-256 of cache schema version, decoder version, content id (the asset id, which is the source content hash), maximum resolution, colour role (sRGB or linear) and filter. Each entry has a checksummed header and a checksummed payload; an entry that fails any check is ignored, counted and rebuilt, never trusted. Old `ILMIP001` and `ILMIP002` files are ignored and not deleted. The folder is bounded (default 8 GiB, oldest version-3 entries are removed first, never the one just written). The original asset stays authoritative and the cache holds derived mips only.

**What a cache hit does and does not prove.** An entry is written only after the source was fully decoded and verified (strict PNG validator plus WIC) in the same load; a load that skips PNG verification writes nothing. The decoder version, schema version and a checksum are part of the key and header, so after a rule change (a bump of the decoder or schema version) every old entry misses and the source is verified again before a new entry exists: the proof is one verified decode per content id per cache generation, stored with the entry. A hit skips only the decode, downsampling and mip generation. `probeTextureDecode`, the readiness function, never reads or writes the cache and always decodes. A load can demand a fresh proof with `forceDecode`. A cache hit relies on the content id: a caller that presents other bytes under an id that already has an entry is outside the contract.

**Decode workers.** All decodes pass one gate that allows at most `max(1, min(4, hardware threads / 2))` at a time (configurable with `setTextureWorkerLimit` or `STARLIGHT_TEXTURE_WORKERS`), whoever calls. `prepareTextures` runs a batch of loads on that many threads and can fill the cache without keeping the images, so preparation can happen behind loading in the shared texture service rather than in host code.

## 13. Verified versus not verified

| Area | Implemented and compiled | Headless-checked | Runtime-verified |
|---|---|---|---|
| Schema 16 cards, sculpt and terrain references | Yes | Yes | No |
| Asset library: identity, durable writes, damage detection | Yes | Yes (22 checks) | No |
| Model and texture import, shared parse cache | Yes | Yes (36 checks); PNG integrity, size limits and the prepared mip cache are headless-checked (see Image integrity policy) | No |
| Root registration, `library.json` marker, registry | Code present, in progress | Not recorded | No |
| Blob and Cast | In progress | No claim | No |
| Portable and game export, dependency closure | Yes | Yes (export and verify) | No (no transfer to another machine, no player run on an export) |
| Migration tool, staged conversion of the repo | Yes | Yes (20 of 20 staged scenes verified; synthetic object and group files) | No |
| Conversion applied to the repo's real scenes | No (not started) | n/a | n/a |
| Reusable object and group version 2 | Yes | Yes (synthetic files only) | No |
| Editor entry points (splash, Open, Play, Walk, messages) | Yes | Not exercised | No |
| Player entry point | Yes | Not exercised | No |
| Readiness core, generation tags, GPU fence, gating | Yes | Logic of the host core only (standalone fake-device test) | No |
| Hosts through the readiness core; MSAA resolve change | Yes | No | No |
| Shared-service fixes (geometry sharing, spatial index, fracture states, hull bound, counters) | Yes | Yes (core checks; the history-identity test) | No; GPU behaviour unverified |
| GameProject content import (staging library) | Yes (tool) | Yes (64 of 64 re-read and compared) | No; hosts cannot read it yet |
| GameProject club room as a card | Design and a draft only | No | No |
| C++23 | Builds at `/std:c++latest` | Core checks | No; no conformance claim |
| Upscaler backends | Compile and link | Configure-time checks | No; nothing run |
| Frame rate, visual equivalence, benchmarks | Not measured | Not measured | Not authorized |

Most recent recorded core-check result in the work reports: 2219 checks passing (a headless console run, not a scene playback). The count changes as workers add checks, and some earlier runs were taken while other workers were mid-edit; take the checklist's number as current.

## 14. Where to look for evidence

- Status of record: `INTEGRATION-CHECKLIST.md`; task list: `TASKS.md` section 12.
- Worker reports in `agents\`: `models.md` (assets, models, textures), `migration2.md` (migration tool), `editor.md`, `readiness.md`, `hosts-impl.md`, `hosts.md`, `render.md`, `runtime.md`, `viewport.md`, `counters.md`, `upscale.md`, `cpp23.md`, `gameproject-import.md`, `clubroom.md`.
- Source: `editor/cpp/engine_assets.hpp/.cpp`, `scene.hpp`, `scene.cpp`, `scene_json.cpp`, `portable_scene.cpp`, `object_template.cpp`, `scene_preparation.hpp`, `deep/engine/host_readiness.h`, `cmake/Upscalers.cmake`, `tools/migration/README-scene-migration.md`.

Open defects and limits are tracked in the checklist (remaining full flushes in shared device headers, load-report counters not wired everywhere, a viewport upload and history defect, model links across relocation, legacy path retirement after verified conversion). Runtime, visual and performance verification (checklist item 17) is not authorized and has not started.
