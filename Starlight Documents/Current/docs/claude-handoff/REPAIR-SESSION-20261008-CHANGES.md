# Starlight repair session — changes, settings and how to verify (8 October 2026)

This file covers the overnight repair commits on branch `repair-session-20261006` (`git log 91fc14d..HEAD`, 56 commits: 48 fixes and checks, 1 revert and 7 merges), every environment variable the engine and tools read, what a person will notice, how to run the verification, and what is still open. It records what was changed. It is not a test result: run section 4 to get the current result for the build in front of you.

Nothing here was pushed or merged anywhere. Commits live on the repair branch only.

## 1. Environment variables

Found by searching `editor`, `deep` and `tools` for `GetEnvironmentVariable`, `getenv` and the repair-switch helper (`editor/cpp/repair_switches.hpp`). `deep/projects/GameProject`, `deep/third_party` and generated output were not searched. A name built at run time would not appear here. "Off" below means "the thing is not active unless the variable is set".

Marks: **NEW** = added in `91fc14d..HEAD`. **CHANGED** = behaviour or default changed in that range. **KEPT** = listed because it was asked about; unchanged in the range.

### 1.1 Settings an owner or tester might use

| Name | Read in | Default | Effect |
| --- | --- | --- | --- |
| `STARLIGHT_BACKUPS_KEPT` **NEW** | `editor/cpp/platform.cpp` | unset: backups are never deleted | Positive number n: after a save, only the newest n `<file>.<32 hex digits>.bak` copies beside the file stay. Names in any other form are never removed. 0, negative or text keeps everything. |
| `STARLIGHT_MESH_DETAIL` **KEPT** | `editor/cpp/mesh_reduce.hpp` | 0.33 | Share of triangles kept for the engine's built-in (procedural) meshes. 1 restores the full meshes; values below 0.05 are held at 0.05; anything that is not a positive number keeps 0.33 (it does not silently restore full detail). Authored files are never reduced. |
| `STARLIGHT_PREFRACTURE_MAX` **CHANGED** | `editor/cpp/adventure.hpp` | 256 (was: every breakable) | Most breakables prepared ahead while a scene loads. In a larger scene the rest are prepared as they are used. 0 prepares none. |
| `STARLIGHT_PREFRACTURE` | `editor/cpp/adventure.hpp` | on | `0` turns break-ahead preparation off. |
| `STARLIGHT_FRACTURE_HELPERS` | `editor/cpp/rock_fracture.hpp` | 2 helper threads, or min(6, cores-8) on 12+ threads | 0 to 64: helper threads for rock fracture. |
| `STARLIGHT_PREPARE_LIMIT_MS` **CHANGED** | `editor/cpp/scene_preparation.hpp` | 60000 | Time limit for scene preparation. A value that is not a number, or is below 1, now keeps 60000 (it used to become 1 ms). |
| `STARLIGHT_EVENT_LOG` **CHANGED** | `deep/engine/event_log.h` | off | Path prefix for the event log written at exit. A path of 400 characters or more is now ignored (logging stays off) instead of being read half-filled. After the log is written the log accepts no more events. |
| `STARLIGHT_CAMERA_LOG`, `STARLIGHT_STALL_LOG` **CHANGED** | `editor/cpp/native_session.cpp`, `editor/native/viewport.cpp` | off | File path to write a camera or stall log (`.viewport` is added for the viewport's own stall log). A path of 512 characters or more is ignored. |
| `STARLIGHT_PASS_LOG` | `editor/native/viewport.cpp` | off | File path for a per-pass viewport log (same 512 character limit). |
| `STARLIGHT_PACING_LOG` | `editor/cpp/native_session.cpp` | off | File path for the frame-pacing log. |
| `STARLIGHT_DATA_ROOT` | `editor/cpp/platform.cpp` and others | `%LOCALAPPDATA%\Starlight` | Absolute folder for application data (portable mode). A relative path is an error. |
| `STARLIGHT_ASSET_LIBRARY` | `editor/cpp/engine_assets.cpp` | built-in search | Absolute folder to use as the asset library. |
| `STARLIGHT_RESOURCE_ROOT` | `editor/cpp/editor_main.cpp`, splash and loading screens | derived from the executable location | Absolute folder to use as the resource root; a relative value is ignored. |
| `STARLIGHT_SUPPRESS_DIALOGS` | `editor/cpp/editor_main.cpp` | off | `1`: automated runs report errors instead of waiting on a message box. |
| `STARLIGHT_FOREGROUND` | `deep/engine/render_device.h` | on | `0`: opening a project does not take the foreground. |
| `STARLIGHT_FRAME_LIMIT` | `deep/engine/render_device.h` | none; must come from launch settings | Frame-rate limit number. Missing or 0 is an error. |
| `STARLIGHT_MSAA`, `STARLIGHT_AF` | `deep/engine/render_device.h` | settings / 16 | Override multisampling (1, 2, 4, 8; reduced to what the adapter supports) and anisotropy (1, 2, 4, 8, 16). |
| `STARLIGHT_HDR_BLOOM`, `STARLIGHT_BLOOM_LEVELS` | `deep/engine/hdr_post.h` | on, 5 | Bloom 0/1; levels 3 to 6. Other values are errors. |
| `STARLIGHT_SUN_SHADOWS`, `STARLIGHT_SHADOW_RESOLUTION` | `deep/engine/sun_shadows.h` | settings | 0/1; 1024, 2048 or 4096. Other values are errors. |
| `STARLIGHT_LOCAL_SHADOWS`, `STARLIGHT_LOCAL_SHADOW_RESOLUTION`, `STARLIGHT_LOCAL_SHADOW_CULLING` | `deep/engine/point_shadows.h` | settings | 0/1; 256, 512 or 1024; 0/1. Other values are errors. |
| `STARLIGHT_SURFACE_RESOLUTION` | `deep/engine/surface_textures.h` | 1024 | 256, 512 or 1024. |
| `STARLIGHT_SCREEN_REFLECTIONS` | `editor/native/viewport.cpp` | settings | 0 or 1; anything else is an error. |
| `STARLIGHT_TEXTURE_WORKERS` | `deep/engine/model/texture.cpp` | automatic | 1 to 64 texture worker threads. |
| `STARLIGHT_DISABLE_DXR`, `STARLIGHT_DISABLE_RR`, `STARLIGHT_RR_DIRECTORY`, `STARLIGHT_SL_VERBOSE` | `deep/engine/dxr.h`, `ray_reconstruction*.h`, `upscale/upscale_dlss.cpp` | off | Force the raster fallback, force the native denoiser, point at the ray-reconstruction DLL folder, turn on Streamline verbose logging. |
| `STARLIGHT_RENDER_PRIORITY` | `editor/cpp/native_session.cpp` | normal | -1, 1 or 2: render thread priority (measurement switch). |

### 1.2 Repair switches (all default ON; set to `0` to get the old behaviour back)

Read by `repair_switch()` in `editor/cpp/repair_switches.hpp`, once per process. They exist so every repair can be measured on and off in the same executable. All were added before `91fc14d`; none changed in this range.

| Name | Effect when on |
| --- | --- |
| `STARLIGHT_FIX_BOUNDS` | Cached object bounds and a ray prefilter in gameplay queries. |
| `STARLIGHT_FIX_SPATIAL` | Spatial index for gameplay ray queries (only active together with `FIX_BOUNDS`). |
| `STARLIGHT_FIX_DELTA` | Combat actors drawn in the scene delta; liquid obstacle mask reused. |
| `STARLIGHT_FIX_CUBEBOUNDS` | Plain cubes get their bounds from their eight corners with no mesh built. |
| `STARLIGHT_FIX_HULLBOUNDS` | Moving pieces are measured from convex-hull vertices, not every vertex. |
| `STARLIGHT_FIX_TURN` | Turning in place tolerates shallow contact with neighbours. |
| `STARLIGHT_FIX_SOLIDS` | Enemies are not blocked by grass, lights, fire or keys. |
| `STARLIGHT_FIX_INSTANCES` | Moving physics bodies are drawn as resident instances. |
| `STARLIGHT_FIX_PREFRACTURE` | Rolling break-ahead fracture. |

Other on-by-default switches (`0` turns off): `STARLIGHT_SCENE_DELTA` (whole-packet publication instead of delta), `STARLIGHT_LIQUID_ASYNC`, `STARLIGHT_GPU_GRASS`, `STARLIGHT_EFFECT_SPLIT`, `STARLIGHT_BATTLE_WORKERS`, `STARLIGHT_GPU_TELEGRAPHS`, `STARLIGHT_SHARE_STATIC_SHAPES`, `STARLIGHT_SORT_SOLIDS`, `STARLIGHT_ZPREPASS`, `STARLIGHT_EXPERIMENT_PERSISTENT_WORKERS`. Off-by-default switches: `STARLIGHT_DISABLE_CUBE_FAST` and `STARLIGHT_DISABLE_PREWARM` (set to turn the obstacle-bounds prewarm at scene start off; the cube fast path is turned off by `STARLIGHT_DISABLE_CUBE_FAST`), `STARLIGHT_EXPERIMENT_GPU_UPLOAD` (`1` turns on), `STARLIGHT_PACE_SMOOTH` (`1`), `STARLIGHT_PACE_SPIN_US`, `STARLIGHT_FLOW_THREADS` (1 to 32), `STARLIGHT_FLOW_SPIN_US` (0 to 100000).

### 1.3 Diagnostics and verification switches (off unless set)

| Name | Effect |
| --- | --- |
| `STARLIGHT_BOUNDS_VERIFY`, `STARLIGHT_CACHE_VERIFY`, `STARLIGHT_OBSTACLE_VERIFY`, `STARLIGHT_OBSTACLE_MASK_VERIFY`, `STARLIGHT_SPATIAL_VERIFY`, `STARLIGHT_FLOW_VERIFY`, `STARLIGHT_SCENE_DELTA_VERIFY`, `STARLIGHT_DUST_VERIFY` | `1`: also compute the slow reference answer and compare it with the fast path. Slower; for finding disagreements. `SCENE_DELTA_VERIFY` also turns off async liquid. |
| `STARLIGHT_DIAG_FRAGMENTS`, `STARLIGHT_DIAG_NO_BATCH`, `STARLIGHT_DIAG_NO_DUST`, `STARLIGHT_DIAG_NO_FOG_OBSTACLES`, `STARLIGHT_DIAG_NO_MVEC`, `STARLIGHT_BYPASS_TRACE` | Remove one piece of work or trace one path to attribute its cost. Diagnostic only; the picture or sim is wrong while they are set. |
| `STARLIGHT_D3D_DEBUG`, `STARLIGHT_NATIVE_LOG` | D3D12 debug layer; path of a native viewport log. |
| `STARLIGHT_ALLOCATION_TRACE`, `STARLIGHT_HANDLE_TRACE`, `STARLIGHT_MENU_TIMING`, `STARLIGHT_MEMORY_MAP` | Allocation, handle, menu timing and memory-map tracing used by the audit tools. |

### 1.4 Test and tool switches (only read by checks and tools)

| Name | Read in | Effect |
| --- | --- | --- |
| `STARLIGHT_CHECK_GROUPS` **KEPT** | `tools/checks/core_checks.cpp` | `name1,name2` runs only those core check groups and exits 3 on purpose (a filtered run is never a pass); `list` prints the group names. |
| `STARLIGHT_SKIP_GPU_CHECKS` **KEPT** | `tools/checks/device_fault_checks.inl` | Set: the viewport device-failure checks print NOT RUN. `verify_all.py` sets it unless `--gpu` is given. |
| `STARLIGHT_PROCESS_TEST` | `tools/checks/native_process_checks.cpp` | Used by the process-environment check only. |
| `STARLIGHT_GATE0_COMPONENT`, `STARLIGHT_LIFECYCLE_IDLE_CONTROL`, `STARLIGHT_LIFECYCLE_BATCH_CYCLES`, `STARLIGHT_LIFECYCLE_RECOVERY`, `STARLIGHT_VISIBLE_LIFECYCLE`, `STARLIGHT_BENCH_SECONDS` | Set by the audit controller (`tools/audit/audit.cpp`) or read by session checks to select Gate 0 and lifecycle modes. |
| `STARLIGHT_BENCH_*` (ANIMATE, CAMERA, CAPTURE_FRAME, EFFECT_TIME, GRASS_BODY, SURFACE_WEATHER, WEATHER_SHELTER) | `tools/checks/render_checks.cpp`, `graphics_preview.hpp` | Shape the render benchmark scene. |
| `STARLIGHT_VALIDATE_*` (FOG, GLOW_SETTINGS, GRASS_CONTACTS, LOCAL_SHADOW_LIFECYCLE, NIGHT_SKY, RESIZE, SHADOW_SETTINGS, SOOT, SURFACE_TEXTURES) | `tools/checks/render_checks.cpp` | Turn on one extra render validation. |
| `STARLIGHT_PREVIEW_GRASS_CONTACT`, `STARLIGHT_GRASS_TEST_WORLD`, `STARLIGHT_MIXED_PHYSICS_ONLY`, `STARLIGHT_SHOWCASE_ONLY` | `tools/checks`, `editor/cpp/player_gameplay_checks.inl` | Narrow a check to one fixture. |
| `STARLIGHT_STRESS_SCENE`, `_PLAN`, `_BAR`, `_DUMP`, `_NOHUD`, `_PROFILE`, `_VISUAL` | `tools/checks/visible_ui_checks.inl` | Stress-scene runs: scene and plan paths, bar scene, bounds dump, no HUD, profiling, loading-screen capture. |
| `STARLIGHT_UI_*_ONLY` and friends | `tools/checks/visible_ui_checks.inl`, `classic_ui_checks.inl` | Run a single visible-UI check: BATTLE, COMBAT, COMBAT_AUTHORING, CONCAVE_FRACTURE, CONTROL, ENEMY_PROJECTILE, ENVIRONMENT, FOG, FOG_BOUNDARY, FRACTURE, GRAPPLE_EDGE, HOLLOW_FRACTURE, INTERACTION, JOINTS, LAYOUT, LIGHTNING, LIQUID_PHYSICS, MOTION, MOVEMENT, MOVING_GRAPPLE, NAVIGATION, NPC_CONTROL, NPC_EXCHANGE, OBJECTS, PANEL_FRACTURE, PERCEPTION, PHYSICS_AUTHORING, PROJECTILE, PROP_PHYSICS, PURSUIT, QUESTS, RT_MOTION, RULES, SCULPT_FRACTURE, STRESS, SWIPE, U_FRACTURE, WEAPONS, WORKSHOP, ZOOM. Modifiers: `STARLIGHT_UI_FOG_DENSE/_FIRE/_WATER`, `STARLIGHT_UI_FRACTURE_WATER`, `STARLIGHT_UI_PACING`, `STARLIGHT_UI_RR_MOTION`, `STARLIGHT_UI_SLOPE_CONE`, `STARLIGHT_UI_SKIP_CAPTURE` (measurement only: no per-step screenshots). |

Other (non-Starlight) variables read: `LOCALAPPDATA`, `USERPROFILE`, `WINDIR`, `PATH` (data folder and the packaging sandbox scripts) and `AUDIT_CONTEXT_CHECK` (`tools/audit/test_audit_engine_contracts.py`).

There are no other `STARLIGHT_FIX_*` or `STARLIGHT_*` policy switches added in this range. None of the fixes below needed a new off-by-default behaviour switch except the opt-in `STARLIGHT_BACKUPS_KEPT`.

## 2. Changelog of the overnight commits

Audit ids are as written in the commit messages ("06" = files/assets, "03" = physics/fracture, "04" = loading, "07" = liquid/terrain, "08" = gameplay, "10" = diagnostics/runtime, "tests-tools" = test and tool audit, "editor" = editor audit). Short hashes are in `git log 91fc14d..HEAD`.

### Saving, files and assets (audit 06)
- `d5734ac` (06#3) Saves pruned old backups to five; replaced in `88ba021` so backups are never removed unless `STARLIGHT_BACKUPS_KEPT` is set (the owner's never-delete rule).
- `b349931` (06#6) A save whose `ReplaceFileW` fails part-way puts the previous file back, and keeps the temporary file when it cannot.
- `5220644` (06#10) A failure to create the write lock reports its Windows error and a briefly held lock is waited for.
- `d47c4cc` (06#4) The asset browser lists files that carry a reparse attribute and only skips reparse folders.
- `22a98ff` (06#5) The player refuses exported content whose `manifest.json` is missing.
- `a9bf6c8` (06#8) Game export hashes each runtime file before copying and refuses a copy that differs.
- `2891954` (06#12) A JPEG ending in a run of 0xFF bytes is reported as truncated instead of read past its end.
- `23e098a` (06#1) Copy a bundle-only asset into the library: applied, then reverted by `ced1099` (see section 5).

### Editor
- `611f285` (editor 2) The error throttle applies only to failures that repeat by themselves; errors from a person's action are always shown.
- `b378166` (editor 6) The scene watcher says once that the open scene file is gone instead of writing a diagnostic every second.
- `9c5ecbd` (editor 7) Open and `--scene` take the disk hash from the bytes that were decoded, so a failed re-read cannot leave the editor half-switched.
- `f92d768` (editor 11) Deleting objects names the gameplay rules left pointing at them, in the status line and the `--commands` result.
- `88ba021` (review fixes) A held key repeating does not re-show an error; export detection needs `.slasset` files in `assets` or a `content` folder beside `game-manifest.json`; the load barrier always ends prewarm mode, even on an exception.

### Physics, fracture and destruction (audit 03)
- `7bdf614` (03#1) Fracture helper threads restore the pool thread's priority when their task ends.
- `7ba9052` (03#2) A break whose rubble cannot get physics bodies still finishes: pieces fall as plain particles and the object is removed.
- `75665bb` (03#4) Break-ahead fracture workers honour a stop request between cuts, so restart or exit does not wait for a whole rock.
- `84cb2bd` (03#3) Break-ahead holds at most 256 prepared breakables by default (`STARLIGHT_PREFRACTURE_MAX`).
- `61ca66b` (03#6) Removing a physics body also drops it from the presentation snapshot so later bodies keep interpolating.

### Gameplay (audit 08)
- `5be1b1d` (08#1) The camera boom ignores non-solid effect objects.
- `043ae49` (08#4) A lightning chain hop tries candidates nearest first and stops at the first reachable one.
- `ebbcc83` (08#5) Tab-targeting measures routed breakables at their current position.
- `80def68` (08#6) Projectile sweep bounds drop ids that were not seen this update.

### Liquid and terrain (audit 07)
- `6558ac9` (07#1) Moving objects join the liquid mask by the drawn liquid surface (displaced by submerged solids, plus waves), not bed plus depth.
- `bbe7764` (07#5) The light obstacle layer is copied before an in-place removal when another holder shares it.
- `e6e93c5` (07#7, report part) Terrain destruction reports once when the 128 hole limit refuses a hit instead of silently stopping.
- `09fbe20` (07#9) A liquid obstacle layer that lost over half its triangles drops the dead records and rebuilds its tables.

### Loading and scene preparation (audit 04)
- `b7e37dd` (04#1) The loading barrier installs the resident scene in play's mode, so starting play no longer rebuilds and reinstalls the base scene.
- `a6418c0` (04#5) Scene preparation stops after a failed stage instead of running the rest.
- `1949900` (04#6) An unparsable `STARLIGHT_PREPARE_LIMIT_MS` keeps the default instead of becoming 1 ms.

### Rendering (RND-A02)
- `0099468` (finding 2) The native draw-slot budget counts scene parts, resident instances and telegraphs together and rejects an update that would overflow it; `88ba021` stops it counting stale instances when a scene is installed.
- `3739fde` (finding 4) Removed transient base parts are kept behind the fence instead of freeing their buffers at once.

### Diagnostics and runtime (audit 10)
- `1f21989` (10#1) The DLSS Streamline interposer loads through the restricted vendor search order like the other upscalers.
- `7d157e9` (10#4) Camera, stall and event log path variables longer than their buffer are ignored.
- `37c72a3` (10#5) The event log stops accepting events once written at exit, and unfinished events are skipped.

### Tests, ledger and tools
- `4836112`, `b6aa1c9` (tests-tools 2) Ledger `minChecks` equals the checks written (EL-05 is 6, HULL-BOUND 12); the runner refuses a minimum below the named checks; the source count ignores `require(false)` branches.
- `81d33e6` (tests-tools 3) Ledger tools default to the checkout they sit in and one shared results folder; `run_ledger` refuses a build configured from another checkout.
- `38077ab` (tests-tools 4) `run_ledger` refuses a stale build itself and records a hash of uncommitted changes in the build identity; `88ba021` makes it check every loaded binary and record `unavailable` when the diff cannot be read.
- `314faec` (tests-tools 5) Regression tool self-tests are registered with ctest; four known-failing Python tests are listed as disabled.
- `153fc0a` (tests-tools 6) Three assertion-free checks now assert.
- `1733acf` (tests-tools 7) The CC-04 ledger note no longer says caps are not done.
- `76053f1` (tests-tools 8) `impact.py` ranks groups whose function sits in a shared `.inl` and lists groups it cannot rank.
- `3cb8fcb` (tests-tools 9, partial) The CC-10 pump check waits for a tick handshake instead of a 300 ms wall-clock budget.
- `7a2f205` (DEP-06) Check that the audit runner applies a CTest test's environment and working directory.
- `6cba57e` (DEP-07) Check that the graphics runtime store sweep leaves another live process's staging alone.
- `fa2f2c0` (DEP-08) Check for migration inventory path confinement; the helper moves to a header.
- `8eb9c5f` (CORE-03, CORE-04) Check for the preset range check and checked write; validate and write move to a header.
- `0e8e4c3` Asset platform checks remove their scratch files so later groups do not scan them.

### Merges and reverts
`d2046fa`, `17a59cd`, `b1595d2`, `3509c5b`, `88abe6a`, `99d52c0`, `6eb4443` merge the seven parallel worktrees (`wf_a1e3befa-2ad-2` to `-8`) into the repair branch. `ced1099` reverts `23e098a`.

## 3. Behaviour changes a person will notice

- **Save backups:** a save still leaves a `.bak` copy beside the file, and old ones are kept. Set `STARLIGHT_BACKUPS_KEPT=<n>` to keep only the newest n.
- **Failed saves:** an interrupted save restores the previous file instead of leaving a half-written one.
- **Exported games:** the player now refuses content with no `manifest.json`; export refuses a copied file whose hash differs.
- **Asset browser:** files with a reparse attribute are listed.
- **Editor messages:** errors from something you did are always shown; a held key no longer repeats the same error; a vanished open scene is announced once; deleting objects tells you which rules now point at nothing.
- **Break-ahead fracture:** large scenes prepare at most 256 breakables at load; the rest are prepared as they are used. A break with no free physics bodies now finishes as falling particles.
- **Water and lava:** moving objects enter the liquid at the drawn surface (including waves). Terrain destruction says once when the 128 hole limit is reached.
- **Gameplay:** the third-person camera boom is no longer pushed by non-solid effects; lightning chains pick the nearest reachable target; tab-targeting uses the breakable's current position.
- **Starting play:** the base scene is no longer rebuilt and reinstalled when play starts.
- **Rendering:** a scene update that would overflow the native draw-slot budget is rejected with an error instead of overflowing.
- **Built-in meshes:** unchanged in this range, still 33% of triangles kept by default (`STARLIGHT_MESH_DETAIL=1` restores full meshes).

## 4. How to run the verification

All commands are PowerShell, from the root of the checkout (the repair checkout is `D:\projects\Starlight-Engine-repair`; agent worktrees are under `D:\projects\Starlight-Engine\.claude\worktrees`). Use absolute paths when scripting.

**Visual Studio environment is required** for every build. In a plain shell run the build through `VsDevCmd`:

```text
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul 2>&1 && cmake --build out/agent --config Release --target Starlight.NativeCoreChecks -- -j 4 2>&1'
```

Other targets: `Starlight.NativeTextureChecks`, `Starlight.NativeSessionChecks`, `Starlight.NativeRenderChecks`, `Starlight.Viewport`. Use `-j 4` at most when several builds run at once.

**Fresh worktree setup.** A new worktree lacks ignored fixtures. `D:\projects\_recovery\claude-fix-20261002\agent-setup.ps1 -Tree <absolute worktree path>` copies `editor\checks`, `migration\classic-advanced.jpg` and the two `migration\fixtures\*.glb` files, links Jolt (`deep\third_party\JoltPhysics` as a junction to `D:\projects\Starlight-Engine\deep\third_party\JoltPhysics`) and configures `out\agent`. Remove the junction without following it: `cmd /c rmdir "<worktree>\deep\third_party\JoltPhysics"`.

**Core suite (about 5 to 7 minutes).** Needs a fresh, empty scratch folder as the first argument and the geometry reference file:

```text
out\agent\Release\Starlight.NativeCoreChecks.exe <fresh scratch dir> migration\geometry-reference.json
```

One group quickly: `$env:STARLIGHT_CHECK_GROUPS='<group>'` before the same command (exit code 3 is expected; it is not a full pass). `STARLIGHT_CHECK_GROUPS=list` prints the names. `python tools/regression/impact.py --repo <checkout> --base 91fc14d` suggests which groups to run first for a change.

**Texture, session and render harnesses.** Run from the worktree root with a new empty scratch folder as the first argument each time (`Starlight.NativeTextureChecks.exe <scratch>`, same for `NativeSessionChecks` and `NativeRenderChecks`). Run the texture harness when GLB or animation code changed, the session harness when `native_session` or `render_scene` changed. Render and session runs use the GPU: only one GPU run at a time, and not while someone is playing.

**Everything in one command (fix ledger):**

```text
python tools/regression/verify_all.py --repo <checkout> --build-dir <folder holding the built executables> --results <results folder> [--python] [--gpu]
```

- Runs the ledger (core suite mapped onto the fix ledger), optionally the registered Python tests (`ctest -R py-`) and the GPU denoise and fallback checks. Windowless and idle priority.
- Without `--gpu` the core suite runs with `STARLIGHT_SKIP_GPU_CHECKS=1`, so CC-07 and CC-02 show as LOST COVERAGE (never a pass).
- Exit 0 means every requested step passed and the ledger is all VERIFIED; 1 means a failure or something not verified; 2 means a step could not run.
- `run_ledger.py` itself refuses a stale build or a build configured from another checkout. The default results folder is `D:\projects\_recovery\regression-results-branch\`; every run is kept under `history\`.
- `python tools/regression/verify_all.py --self-test` and `python tools/regression/run_ledger.py --self-test` check the runners without the engine.

Do not edit `tools/regression/ledger.json` or the baselines to get a pass.

## 5. Known gaps

- **Audit 06#1 is open.** The change that copied an asset found only in a bundle or search folder into the library (`23e098a`) was reverted (`ced1099`); that behaviour is still missing.
- **Partial fixes:** 07#7 only reports the 128 hole limit (the limit itself is unchanged); tests-tools 9 fixed the CC-10 pump check only.
- **Disabled Python tests:** `test_analysis_gui_source_build`, `test_audit_engine`, `test_deep_analysis` and `tools/checks/test_verify_no_managed` fail from a clean checkout (missing native executables or paths) and are registered as disabled; ctest reports them as Not Run (Disabled).
- **Nothing here is a performance claim.** The repair switches in 1.2 exist to compare on and off; this file contains no before/after frame-time numbers.
- **GPU and visible runs are separate from the core suite.** Session, render, texture and visible-UI harnesses need a free GPU and, for visible tests, foreground access. A core-suite pass says nothing about them.
- **Audit findings with no entry in section 2** were not changed in `91fc14d..HEAD`; an earlier round may have handled them (check `git log`) or they remain open.
- **Variable list limits:** found by static search of literal names; `deep/projects/GameProject`, `deep/third_party` and `graphify` output were not searched.
- No push, merge or publication was done. Publication stays a separate, owner-approved step.

## 6. What changed since 91fc14d, by subsystem

This section covers the whole `integration-20261009` branch, not only the repair set in sections 1 to 5. `git log --no-merges 91fc14d..integration-20261009` lists 253 commits, and with the 41 merge commits the range holds 294. `git diff --shortstat 91fc14d integration-20261009` reports 180 files changed, 9043 lines added and 801 removed. The branch `repair-session-20261006` is an ancestor of `integration-20261009`. `git rev-list --count 91fc14d..repair-session-20261006` now gives 58, so the 56 in the opening paragraph is out of date.

Commit references are short hashes from `git log integration-20261009`. `870c0f9` is the commit that wrote sections 1 to 5. Commits that exist only on the fleet and worktree branches are not on the integration branch. They are listed in 6.9 and are not shipped.

No commit in this range records a measured speed-up. Performance-related changes below say what they do, not how much faster they are. Any timing claim needs a fresh measurement on a quiet machine, with the switches in 1.2 and 1.3 set on and off in the same run.

Three groups of files need a note. The vendored header `deep/engine/third_party/cgltf.h` changed in `0163101` and `201b2ff`. The six files under `deep/projects/GameProject/net` changed for the WebSocket client (see 6.8). The build and package files `CMakeLists.txt`, `cmake/Upscalers.cmake` and the packaging scripts changed for the items named below.

### 6.1 Rendering

- **Draw budget (`0099468`, `88ba021`).** The native renderer counts scene parts, resident instances and telegraphs together against one draw-slot budget. An update that would overflow the budget is rejected with an error. Stale instances are no longer counted when a scene is installed.
- **Retired parts (`3739fde`).** Base parts removed by a scene update wait behind their retirement fence before their GPU buffers are freed.
- **Frame failures (`4dbd360`, `74b3129`, `b2abdcf`, `2132a6f`, `158de2f`).** A frame that throws while recording always closes its command list. Ray-tracing geometry is built for resident instances. Fog-obstacle and GPU-wait failures are reported, and obstacles are committed only after every one of them validates. A draw whose variant has no texture set binds a null texture table. Queued grass entries are cleared at the start of each frame, so a failed frame leaves no stale pointers.
- **Device loss and teardown (`9281d5b`, `4db881f`).** A removed device is reported by the first call that fails, and teardown no longer throws out of a catch handler. Viewport destruction frees GPU buffers only when the GPU is confirmed idle. If a flush fails, it is retried without the failure hook. If idle cannot be confirmed, the viewport stays allocated.
- **Resize and rebuilds (`b7f10ec`, `80d9b2e`).** A resize that fails part way keeps the previous targets and size. A glow or resize rebuild that failed part way is redone when the identical call is retried.
- **Upscalers (`4bf1592`, `1f21989`).** If a vendor upscaler fails after its swapchain was released, the renderer makes a plain swapchain instead of using a null one. The DLSS Streamline interposer loads through the same restricted vendor search order as the other upscalers.
- **Viewport calls (`750fca8`, `f6f334d`, `7f0ee00`, `3aad042`, `b691dee`, `cce7f92`).** Viewport calls that used to dereference a null handle, output pointer or path now return defaults. Stall and pass log paths that do not fit their buffers are ignored. A vertex padding self-check that compared a copy with its own source was removed. The benchmark report no longer uses fixed-size buffers and escapes control characters. Frame delta time comes from the performance counter instead of the 15 ms tick count. Snapshot smoothing ignores non-finite values and wraps yaw without a loop.
- **Grass (`cd3fe45`, `48ee4ff`, `a9bb651`, `e968665`, `48d8f2d`).** Grass deformation drops queued entries when a frame aborts, so no dangling pointer reaches the next flush. Resident meshes that use the GPU grass vertex kind are rejected. Duplicate grass identities are rejected before a scene is replaced, and the resident token is reset when a full scene replaces the parts. A grass pool that is wholly unused is freed once its retirement fences complete. Grass shadow reach relaxes after contacts leave, and never drops below what active contacts and settling blades need.
- **Point shadows (`00c4fd0`).** The point-shadow cube array is kept through a short empty spell and freed after 240 empty frames.
- **Foliage and joints (`91894e8`, `392a81f`, `2479975`).** Bark and Leaf foliage bodies in resident instances are tagged -40 and -41, like the rest of the packet. Skinned joints keep the transform of non-joint ancestor nodes. The object cache verifies cached pose bounds against the same pose, not the authored spot.
- **Presentation (`6341186`).** The unit cube used to draw boxes is built once, not once per drawn box.
- **Mesh detail (`9782d07`, `f3440a0`, `a3217c1`).** The effective `STARLIGHT_MESH_DETAIL` is logged once at startup, and submitted triangle totals appear in the event log. The built-in meshes themselves are unchanged. Reduced rocks, boulders, crystals and pebbles get smooth normals from their own faces.

### 6.2 Editor

- **Error messages (`611f285`, `ba1b3a1`).** The error throttle applies only to failures that repeat by themselves. Errors caused by a person's action always show. The editor's error display, its diagnostics and the command-line error report no longer throw on text that is not valid UTF-8. A held key that repeats no longer re-shows the same error (`88ba021`).
- **Scene watcher (`b378166`).** When the open scene file disappears, the editor says so once instead of writing a diagnostic file every second.
- **Opening a scene (`9c5ecbd`, `e85431b`, `f1b93a3`).** Open and `--scene` take the disk hash from the bytes that were decoded, before any editor state changes. A failed re-read therefore cannot leave the editor half-switched. Open ends the creation lesson, hides the lesson tracker and disarms a water or lava pour, the same as New and the file-change reload already did. An external change while there are unsaved edits now says that the edits are kept, and that Save as writes them elsewhere, in the status line and the failed-save message.
- **Autosave (`488382b`, `a30f06e`).** While a scene has unsaved edits, the editor writes `<scene>.autosave` beside it every 2 minutes. It asks to save when Windows ends the session. The autosave file is removed only after a normal Save. The editor interface notes describe the file.
- **Command line (`b171262`, `8a46d54`, `a30f06e`).** The windowed editor runs every documented command flag instead of opening a window for them. CLI batches can opt in to saving over missing models (`allowMissingAssets`) and to add resolving paths and terrain the way the editor does (`correctedAdd`). Both default to off.
- **Deleting objects (`f92d768`, `bef70dd`).** Deleting objects names the gameplay rules that are left pointing at them, in the status line and in the `--commands` result. Deleting a key object also reports item-collected rules that were waiting for that key.
- **Selection and input (`8d22575`, `28d3fba`, `b7abce6`, `e1d6e13`).** A click that picks an object, Undo, Redo, Open, an outside file change and the object list all keep the multi-selection and the active object in step. A double-click on the 3D view is handled as a press, so it no longer runs command 0, which showed "Pause playback before editing" during Play. A second mouse button pressed during a brush stroke, an orbit or a box selection is ignored. Pasting objects and exporting a selection drop joint and lightning links to objects that are not in the destination.
- **Snapping (`7ae6f94`).** With `STARLIGHT_SNAP_ONLY_MOVED=1`, Terrain, Sculpt and Route strokes no longer snap and move the selected object. The default is unchanged: every stroke snaps.

### 6.3 Assets and files

- **Saves and backups (`b349931`, `5220644`, `d5734ac`, `88ba021`).** `d5734ac` first pruned backups to the newest five. The review fix `88ba021` reversed that default, so backups are only pruned when the owner sets `STARLIGHT_BACKUPS_KEPT`. If a save's `ReplaceFileW` fails part way, the previous file is put back, and the temporary file is kept if it cannot be. A failure to create the write lock reports its Windows error, and a briefly held lock is waited for.
- **Restore helper (`fcc9725`).** The rollback after a failed `ReplaceFileW` became a named function, `restore_replaced_file`, so a check can exercise it.
- **Bundle-only assets (`23e098a`, `ced1099`).** `23e098a` copied an asset found only in a bundle or search folder into the library. `ced1099` reverted it. The behaviour is still missing (section 5).
- **Asset library (`d47c4cc`, `5b31199`, `8186f9e`, `fbd7997`, `9c2eb90`, `2749652`).** The asset browser lists files that carry a reparse attribute and skips only reparse folders. A damaged asset copy is skipped when a later root holds an intact one. A malformed asset id is never turned into a file path. An unreadable asset file reports AssetMissing. Asset, scene and model files are checked for size before they are read, and the copies made when an asset is stored are reduced. `ScopedRoot` asserts if it is used from two threads at once.
- **Portable scenes (`4df7921`, `8f1e7f8`, `2241d59`, `f8c8c7e`, `1e79acb`, `f1b5b8d`).** Portable scene manifest paths with a colon are refused. Pours and route points are capped before a scene file is decoded. Grapple, team and control-effect values that are not whole 32-bit numbers are refused rather than wrapped. A reusable group that repeats an object identity is refused, so joints no longer link to the later copy. A scene file with more than 20000 objects is refused before conversion. Adversarial checks confirm that damaged starter scenes are refused cleanly (`dff2afb`).
- **Exports and the player (`22a98ff`, `a9bf6c8`, `7a162ca`, `673c869`, `a9de883`).** The player refuses content whose `manifest.json` is missing. Export hashes each runtime file before copying it and refuses a copy that differs. The player checks runtime executable and DLL hashes from `game-manifest.json` at startup. Export staging uses a fresh folder name and never deletes an unrelated folder. A failed extraction removes its partly extracted folder, even when it throws.
- **Zip files and names (`3e670c9`, `e22735e`, `9edbe7e`, `45efdc3`, `0c36545`).** Zip entries whose names are not valid UTF-8 are refused. Damaged zip central directories are refused cleanly. Names that differ only by non-ASCII letter case count as duplicates. The path guard rejects the CONIN and CONOUT devices, superscript COM and LPT names, and device names with a space before the dot. A set id that ends in a dot or is a device name is refused.
- **Numbers in catalogs and manifests (`8e7a988`, `4aca2d2`).** Manifest integers outside the 32-bit range are refused instead of wrapping. Signed catalogs with a negative, fractional or out-of-range number are refused.
- **Key cards and paths (`c6fbff9`, `6f76fb5`, `5577ddf`, `10164a5`).** Key-card and report paths are carried as UTF-8, so folders outside the ANSI code page work. The key-card lookup reads the host module path with a growing buffer, so long install paths are not cut short.
- **Model and texture readers (`2891954`, `22ec12e`, `e815516`, `cfde111`).** A JPEG that ends in a run of 0xFF bytes is reported as truncated. A baseline JPEG whose header claims more image than its bytes can hold is refused before the pixel buffer is allocated. PNG zlib streams that would rebuild huge Huffman tables are refused, and the fixed-code tables are built once, thread-safely.
- **GLB and glTF (`0f97970`, `52a6337`, `228233a`, `201b2ff`, `b35564a`, `3ac04fa`, `1f85706`, `b7251df`, `bba3d48`, `eb6a72e`, `0163101`, `9fd4384`, `94b0b14`).** GLB JSON nested deeper than 256 levels is refused before tokenising. A data-URI buffer that claims more bytes than its text holds is refused. Accessor and buffer-view sizes that wrap are refused before cgltf validates them. Accessor and sparse counts whose byte size wraps 64 bits are refused. Vertex and index accessors without a buffer view are capped at 4194304 elements. Concurrent first requests for one GLB share a single parse. Triangle strips and fans are expanded, and point and line primitives are named and skipped. A vertex with zero total weight is held to the joint range check. Node transforms cannot make skinned vertices non-finite. Animation samplers must use SCALAR times and VEC3 or VEC4 values, and sparse accessor writes stop at the requested count. Animation channels with no keyframes are dropped at load.
- **Launcher and presets (`93ed935`, `2c6edb0`, `52a7d41`, `599a19b`).** A preset that is nested deeply, larger than 1 MiB or has a name that is not text is refused before it can overflow the launcher's stack. Launch error and preset files are written even when their text is not UTF-8. Torn side files and unwritable reports no longer block a launch after the preset is migrated.
- **Packages and staging (`05290b1`, `1390fbc`, `aec45d8`, `70078a9`, `1e9b477`, `4bd4865`, `70041d1`, `3a2ed9f`, `a94eae8`, `bcda904`).** Package paths that start with a separator are refused. The graphics validation SDK is downloaded under a temporary name and kept only when its hash matches. The source package carries `THIRD-PARTY-NOTICES.md` and `renderer-transforms.json`, and a check fails if `CMakeLists.txt` names a file the package lacks. The package ships `Starlight.SceneMigration.exe`, which the editor tells users to run. Staging takes the ray-reconstruction DLL and its licence from arguments and fails before creating the package folder. `Empty.starscene` is staged beside `Empty.dll`, and a project module is refused without its key card. The DLSS SDK has its own cache variable. The Empty host releases its GPU meshes before the device is deleted. Every inventory output path is validated before `--convert-all` writes anything. Portable executable images whose header is out of reach or invalid are reported instead of skipped (`bcda904`).
- **Saved state and the graphics store (`ac14259`, `46cd002`, `0c36545`, `cfde111`, `379374a`, `e1864a5`, `b5a8f6a`).** Unreadable or damaged state files are retried, kept under a new name, noted as an event, and block a prune. A prune or inventory leaves another live process's staging folder alone. `confirm_good` promotes only a set that was really resolved this launch. Problem reports and state files survive text that is cut mid-character or is not UTF-8. The `--trust` public key file is decoded strictly.

### 6.4 Physics and liquids

- **Break-ahead fracture (`7bdf614`, `7ba9052`, `75665bb`, `84cb2bd`, `b4c066b`, `d629026`).** Fracture helper threads restore the pool thread's priority when their task ends. A break whose rubble cannot get physics bodies still finishes, with the pieces falling as plain particles. Break-ahead workers honour a stop request between cuts, so a restart or exit does not wait for a whole rock. Break-ahead holds at most 256 prepared breakables by default (`STARLIGHT_PREFRACTURE_MAX`), and the cap counts only live entries. The stale-source diagnostic reads the entry state under its mutex.
- **Rubble patterns (`1510281`).** With `STARLIGHT_DETERMINISTIC_RUBBLE=1`, the same break id gives the same pieces in every run. Unset, the rubble still varies between runs, as before.
- **Rolling resistance (`0fa586e`).** `STARLIGHT_ROLLING_RESISTANCE=1` applies rolling resistance in the adventure physics world. The default is off, which keeps the shipped roll-out behaviour.
- **Hinge limits (`691c1d2`).** With `STARLIGHT_STRICT_HINGE_LIMITS=1`, a hinge range that excludes 0 degrees is refused. The default still accepts it, as before.
- **Bodies and presentation (`61ca66b`).** Removing a physics body also drops it from the presentation snapshot, so later bodies keep interpolating correctly.
- **Liquid mask (`6558ac9`, `4aaa880`, `408055a`).** Moving objects join the liquid mask by the drawn liquid surface, which includes displacement by submerged solids and the waves, instead of bed plus depth. The wet-object filter uses the higher of the still level and the drawn surface, so wave troughs do not make membership flicker. A non-finite liquid vertex drops that liquid for the frame instead of throwing into the render loop.
- **Obstacle layers (`f5c0d18`, `bbe7764`, `2e29e2b`, `09fbe20`).** Each triangle is registered only in the cells its projection meets, not in its whole bounding box. The light obstacle layer is copied before an in-place removal when another holder shares it. Objects are removed in place only up to a third of the live ones; beyond that the layer is rebuilt. A layer that lost more than half its triangles drops the dead records and rebuilds its tables.
- **Terrain (`ebb2c32`, `804d766`, `e6e93c5`, `729153d`).** The surface search skips carved volumes that cannot change its answer, with identical results. Terrain volume meshes are validated once per rebuild, and two grounds no longer evict each other's cached mesh. Terrain destruction reports once when the 128 hole limit refuses a hit. A hole centre is kept inside the plus or minus 12 domain, so a hit near the edge cannot stop the game.
- **Flow scratch buffers (`d2efa0b`).** A TerrainFlow can no longer be assigned over a flow's scratch buffers and geometry job.

### 6.5 Gameplay

- **Camera boom (`5be1b1d`, `8dd1fad`, `280c464`).** The third-person boom ignores non-solid effect objects. It is pulled in only by objects that block movement, not by grass, lights, fire, lava, lightning or smoke. That filter has the repair switch `STARLIGHT_FIX_CAMERABOOM`, on by default. `STARLIGHT_CAMERA_BOOM_SMOOTHING=1` closes the boom at once when geometry is in the way, opens it again over about 0.15 s, and never shrinks it below 0.25 m. The default is off.
- **Lightning (`043ae49`).** A lightning chain hop tries candidates nearest first and stops at the first reachable one.
- **Targeting (`ebbcc83`, `091795e`).** Tab-targeting measures routed breakables at their current position. Line of sight for attacks and healing ignores objects that never block movement.
- **Projectiles (`80def68`).** Projectile sweep bounds drop ids that were not seen in the update.
- **Movement (`5e8bf92`, `614edb7`, `ac0327b`, `e41340e`, `25e2bf1`, `33dac79`).** An actor's vertical step on heightmap terrain follows the horizontal move that was allowed. Routed combat actors are not stranded by the twenty-metre leash when they lag their route point. Objects that never block movement are not start-position solids. A character step with non-finite feet or input is refused, so the controller cannot be poisoned. Forced movement moves the player when collision is off. A launch on the player ends when collision is off.
- **Rules and messages (`93e0b97`, `d9767c6`, `6d59ccf`).** The message from an enter-area rule survives standing on an object. BossBreakBar forgets resolved event ids when the bar is Ready again, so the set stops growing. Combat and gameplay-key settings are kept when health is 0 or the role is None.
- **Entities (`e6aa063`).** The entity smoothing map is swept, so the ids of departed entities no longer build up.
- **Quest and audio (`91bbd0c`, `ad89334`).** Quest tracker wheel scrolling accepts wheel reports smaller than one notch. Gameplay audio stop also covers a request the audio thread has already taken off the queue.

### 6.6 Loading and play start

- **Resident scene at play start (`b7e37dd`).** The loading barrier installs the resident scene in play's mode, so starting play no longer rebuilds and reinstalls the whole base scene. The session check counts base installs across play start.
- **Loading barrier (`16e1147`, `dff0df5`).** The barrier decides the resting set and prepares the physics session before the scene is revealed. Key-card objects that the host cannot draw become a degraded readiness item with a count.
- **Scene preparation (`a6418c0`, `1949900`, `88ba021`).** Scene preparation stops after a failed stage instead of running the rest. An unparsable `STARLIGHT_PREPARE_LIMIT_MS` keeps the default (60000) instead of becoming 1 ms. Prewarm mode always ends, even on an exception.
- **Queued full loads (`2dfba45`).** A queued full scene load stays a full load when a runtime update replaces it.
- **Start (`292e028`).** `start()` validates the new scene before it resets the running session.

### 6.7 Tools and verification

- **Ledger (`4836112`, `b6aa1c9`, `135012a`, `1733acf`, `81d33e6`, `38077ab`, `b1626f5`, `e2f084d`, `7fdac62`).** The ledger's `minChecks` equals the checks written: EL-05 is 6, HULL-BOUND 12, CC-07 9. The runner refuses a minimum below the named checks. The ledger tools default to the checkout they sit in and one shared results folder. `run_ledger` refuses a build configured from another checkout, refuses a stale build itself, and records a hash of uncommitted changes in the build identity. The filtered-run guard is run by the ledger self-test. A malformed ledger or unsafe entry id is refused. A result file that holds a list, text or null is reported, not crashed on. A source payload test reports SKIPPED (exit 77) when a linked folder points outside the tree.
- **verify_all (`1d80184`, `70913fe`, `2553a31`, `c8c6f22`, `a30dc0f`).** The self-test checks that the Python step fails when no `py-` test is registered. With `--gpu`, `nvngx_dlssd.dll` and the D3D12 runtime folder are placed next to the executables, or the run stops with a clear message.
- **Test registration (`314faec`, `7a2f205`, `b21eeb4`, `9342a81`, `1c055aa`, `1dbec59`).** Regression tool self-tests are registered with ctest. Four known-failing Python tests are listed as disabled, not left out. The audit runner applies a CTest test's environment and working directory, and the audit coordinator runs registered tests through one function. The native audit Python tests run against the build tree under test. ctest clears the run-narrowing switches for the classic UI and session tests. The physics presentation check is registered with ctest and refuses to run under `python -O`.
- **Impact and briefing (`76053f1`, `1d80184`).** `impact.py` ranks groups whose function sits in a shared `.inl`, ignores the `RUN_GROUP` macro line, and lists groups it cannot rank. The briefing and verify_all report a malformed result file without crashing.
- **Checks made to fail when they should (`153fc0a`, `3cb8fcb`, `bc941bf`, `e617ae0`, `6156cc1`, `23c5075`, `46c383b`, `692a44a`, `bc941bf`, `0a2a33c`, `763da87`, `9ee3983`, `33f2403`).** Three assertion-free checks now assert. The CC-10 pump check waits for a tick handshake instead of a 300 ms wall-clock budget. A stop requested during a running fracture is checked to end it early. Two key-card checks can now fail. The Ctrl+Z check pins the open stroke ending before Undo or Redo. The rules-naming check covers the `--commands` result. The open-hash check races a file swap. The crash-dump check accepts only a dump written after the child started. The symlink listing check reports NOT RUN when symlinks cannot be created. The battle walkthrough exit code follows its own verdict. The render harness reports failed validation assertions as exit 15. The error throttle check pins the editor classifying person-driven messages (`bc941bf`).
- **Red-first proofs (`26703e8`, `3d6ca81`, `624920b`, `72237ce`, `fcc9725`, `e784819`, `376fbb3`).** These commits add checks that fail on the old behaviour: a sparse view check that faults on a read past the buffer, a STEP channel held when sampled, a matrix joint with rotation and scale, non-finite keyframes, the rollback helper, the instance bucket table shape, and the paste, duplicate, group and asset-browser id renewal.
- **Fuzz and adversarial groups, which add checks (`1f116dc`, `8186f9e`, `fbd7997`, `3e670c9`, `dedf51e`, `22ec12e`, `010ef3a`, `135c638`, `295281c`, `ebd0d27`, `641e44b`, `dff2afb`, `e22735e`).** Deterministic asset store, portable scene, export containment, manifest and scene JSON fuzz groups were added. A deterministic mutation fuzz group covers the PNG, JPEG and DDS readers. A GLB mutation fuzzer runs on the editor loader and on the skinned model loader in the texture checks. Adversarial checks cover damaged command requests, starter scenes, very deeply nested JSON, damaged zip central directories and a loopback GameProject WebSocket client.
- **Environment and fresh clone (`c36ed30`, `00c1a30`, `5e9d040`, `0e8e4c3`, `aec45d8`).** Starter cards are checked out as LF so canonical-form checks pass from a fresh clone. The PNG and JPEG fixtures the texture harness decodes are tracked. `5e9d040` documents what a fresh clone needs beyond the repository. Asset platform checks remove their scratch files so later groups do not scan them.
- **Diagnostics (`37c72a3`, `660aa24`, `7d157e9`, `8f77e62`).** The event log stops accepting events once it is written at exit, keeps its buffer, and skips unfinished events (`37c72a3`). A closed log leaves an unfilled slot unwritten and stays off (`660aa24`). Camera, stall and event log path variables longer than their buffer are ignored (`7d157e9`). `8f77e62` lets `STARLIGHT_EVENT_LOG_CAPACITY` raise the capacity so long runs are recorded to the end.
- **Further checks (`1f2062e`, `27721d8`, `2986571`, `29de06d`, `3980e82`, `82be122`, `ad93df0`, `b33a2db`, `d621b35`, `de32211`, `6cba57e`, `8eb9c5f`, `fa2f2c0`).** Checks were added for: the lightning hop order when two reachable bodies sit at different distances (`1f2062e`); terrain assets against the terrain rules at readiness (`27721d8`); finding the saved default test settings from the build tree, the package and the old layout (`2986571`); the physics ABI error matching (`29de06d`); the symlink check creating a directory junction when symlinks are not allowed (`3980e82`); the backup retention check, which uses eight backups as its default and counts only exact backup names (`82be122`); the `STARLIGHT_PREPARE_LIMIT_MS` wiring (`ad93df0`); the 33% and 10% mesh reduction defaults (`b33a2db`); break-ahead pieces reported for preload after loading (`d621b35`); the stop after a failed scene preparation stage (`de32211`); the graphics runtime store sweep leaving another live process staging alone (`6cba57e`); the preset range check and checked write, moved into a header (`8eb9c5f`); and the migration inventory path confinement, moved into a header (`fa2f2c0`).
- **Device checks (`d886de4`, `f07503c`, `e9749ae`, `9da4358`).** Device-failure checks take a machine-wide lock, so only one runs at a time on a GPU (`d886de4`). Each injected failure runs on a fresh thread (`f07503c`). The night-sky capture deletes its previous file first and requires a non-empty new one. The ray-tracing shader-model claim is checked against the shader build tool that the build really uses.
- **Build repairs after merges (`36e87f1`, `afb4fc2`, `8adc818`, `f379a56`).** These commits fix build breaks left by the merges: two UTF-8 path edits in the key-card view, braces in the project module header, one `SmoothXf` definition, and checks that two branches changed differently.
- **Benchmarks (`aa6d6f5`).** `Starlight.NativeBench` is an optional CPU-only micro-benchmark target that is not in CTest. It measures the overnight performance changes; it records no results here.

### 6.8 Launcher, packaging and networking

- **Project and runtime files (`bb642e6`, `55794d6`).** A project module must be a plain file in the project folder, and its arguments use the command-line allow-list (`bb642e6`). Rooted and drive-relative runtime entries are rejected instead of hanging the runtime child (`55794d6`).
- **WebSocket client, GameProject (`3587135`, `a82a791`, `e36c042`, `641e44b`, `96c95b8`, `154387b`).** Exceptions are caught at the WebSocket reader boundary, and the message type is validated before decode. Frame, reassembly and buffer sizes are bounded, and high-bit lengths are rejected. The client requires the matching `Sec-WebSocket-Accept` and sends CLOSE while the socket can still be written. The welcome identity is carried in the synchronised state store, not a shared string. The mock feeder thread is joined on every exit from its scope. These changes are in `deep/projects/GameProject/net`, the consumer network code. This section describes them and does not touch them.

### 6.9 Branch-only commits (not on integration-20261009)

The fleet and worktree branches hold 62 commits that are not reachable from `integration-20261009` (69 branch and commit pairs, because some commits sit on more than one branch). They are listed here, not in the sections above. Some add a switch that the integration branch does not have; those are marked.

- `7077855`, `db8e731`, `f8ee19a` (`fleet10-h-build`): launcher and self-contained build message for a missing branding file, a configure-time warning when Python 3 is missing, and copying the ray-reconstruction DLL and Agility D3D12 runtime beside the GPU executables.
- `6027fcd` (`fleet10-h-kc`): keeps the reason a render loop failed in the Empty host report.
- `1235527`, `3a9c817`, `3fc571e`, `4121747`, `5401322`, `561257f`, `b27d9b2`, `eb12d02` (`fleet10-h-ws`): WebSocket frame and close validation, rejection of non-minimal lengths, invalid UTF-8 text, a fix for reconnect after server close, and a reader wake-up so close returns on a silent peer.
- `d0aac7d` (`fleet11-h-core`): a filtered core-suite run reports success false in result.json.
- `a03ad45`, `ac2bf1a`, `cb0cc16` (`fleet13-h-chk2`): physics slider and bullet checks require the body to move, the native playback stop check asserts the stopped state, and the crash-diagnostic check records "not verified" when no dump matches.
- `4263d1f` (`fleet13-h-data`): accepts the engine's ultraperf quality name in presets.
- `8530ae5`, `bc9f668` (`fleet13-h-inl`): stun chirp check compares against a snapshot taken before the stun; invalid effect and day-duration input shows the intended message instead of a `std::stod` exception.
- `02825fc`, `fae484f` (`fleet13-merge-1`, `fleet9-h-em3`): the build page's Apply saves the bark and leaf values it shows, and clears the foliage settings when the surface leaves Bark or Leaf.
- `6358284`, `a5388e3` (`fleet13-merge-1`, `fleet8-fin-mutation-b`, `worktree-wf_313bc5c6-62a-4`): the ledger gate fails when the core suite crashed or timed out after writing result.json.
- `685dedf`, `7804578`, `e3ec39a` (`fleet13-merge-1`, `fleet9-h-ns`): the gameplay overlay check includes the motion identity, an emptied runtime object list reaches the render thread, and the destructor sets the stop request under the session mutex.
- `37968e9`, `a01a96c` (`fleet8-fin-fuzz-assets`): asset root registry and library marker fuzzing, and refusal of game manifest entries that name a data stream or contain a colon.
- `d45368e` (`fleet8-fin-fuzz-images`): decoder fuzz seeded with a BC7 sRGB DDS.
- `250ecf7` (`fleet8-fin-fuzz-misc`): the key-card adapter refuses objects placed beyond the float range.
- `b52551b` (`fleet8-fin-fuzz-net`): the closed WebSocket gaps become hard regression checks.
- `899a16a` (`fleet8-fin-geometry-budgets`): model, atlas and texture files are read whole only up to per-kind limits.
- `b30b73f` (`fleet9-h-ast`): copies a bundle-only asset into the primary library when stored. It has the same intent as `23e098a`, which integration reverted in `ced1099`.
- `507240d`, `d772a2f` (`fleet9-h-em2`): combat affiliation keeps a stored team number above Enemy, and numeric inspector fields report "Enter a valid number".
- `885dcc1`, `9061629` (`fleet9-h-em4`): the missing-file and changed-file warnings stay on the status line, and a failed Save keeps the window open while the window closes or Windows ends the session.
- `76269ff`, `a68d370`, `ba21abd` (`fleet9-h-mdl`): a non-finite pose time on a looping clip holds the first key, animation keyframe accessors are capped, and inverse bind matrices that are not MAT4 are refused.
- `4ecfd24` (`fleet9-h-phy`): a dynamic collision hull with a non-finite vertex is refused.
- `bd9afe9` (`fleet9-h-pytools`): the ledger reader does not confirm a checkout against results that recorded no commit.
- `a6620c6`, `c628048` (`fleet9-h-rd2`): GPU fence waits re-check completion, and applyMode fails loudly when the upscaler refuses the quality mode.
- `d192f2f` (`fleet9-h-rd4`): the multicore proof sizes its pose scratch palette from the measured model.
- `7984bfd`, `bc7fb88`, `e2a0efc` (`fleet9-rev-1-wf27`, `worktree-wf_313bc5c6-62a-27`): armature transforms reach joints only when `STARLIGHT_JOINT_ANCESTOR_TRANSFORMS=1`. That switch is not on the integration branch. The integration branch makes the change without a switch, in `392a81f`.
- `1aa1dda`, `7b5d737`, `cd50980`, `d157035`, `e16a779` (`worktree-wf_313bc5c6-62a-20`): visits resident instances in place, looks up streaming flags in one index, clears rest-position history on environment reset, hashes eight bytes per step in mesh identity, and skips scene solids outside the view behind `STARLIGHT_VIEW_CULL=1`. `STARLIGHT_VIEW_CULL` is not on the integration branch.
- `441ad45`, `648e807` (`worktree-wf_313bc5c6-62a-12`): ctest clears the wave-2 classic UI switch too, and the read-back guard validates only the scene files this run wrote.
- `542459c` (`worktree-wf_313bc5c6-62a-23`): Editor Play of a scene without Game can run scene preparation behind `STARLIGHT_EDITOR_PLAY_PREPARE`, off by default. Not on the integration branch.
- `278f6bc` (`worktree-wf_313bc5c6-62a-24`): lava clock wrap behind `STARLIGHT_LAVA_TIME_PERIOD`, off by default. Not on the integration branch.
- `7684ec2`, `7cf2b2b`, `88fba02`, `ee53a33` (`worktree-wf_313bc5c6-62a-26`): WebSocket text must be valid UTF-8, server CLOSE payloads are checked, frames the client must refuse fail with CLOSE 1002, and close returns on a quiet or write-blocked connection.
- `fe78754` (`worktree-wf_313bc5c6-62a-9`): the sun shadow resolution override survives shadows being turned off and on.

### 6.10 Summary of switches added in this range

Defaults are as stated in the source comments. All of these are on the integration branch.

| Name | Default | Effect |
| --- | --- | --- |
| `STARLIGHT_BACKUPS_KEPT` | unset (keep all) | Newest n backups kept after a save. |
| `STARLIGHT_DETERMINISTIC_RUBBLE` | off | `1`: same rubble for the same break id in every run. |
| `STARLIGHT_ROLLING_RESISTANCE` | off | `1`: rolling resistance in the adventure physics world. |
| `STARLIGHT_CAMERA_BOOM_SMOOTHING` | off | `1`: smoother third-person boom, never shorter than 0.25 m (or the requested distance if that is shorter). |
| `STARLIGHT_FIX_CAMERABOOM` | on (repair switch) | Boom is pulled in only by objects that block movement. |
| `STARLIGHT_SNAP_ONLY_MOVED` | off | `1`: Terrain, Sculpt and Route strokes do not snap the selected object. |
| `STARLIGHT_STRICT_HINGE_LIMITS` | off | `1`: refuse hinge ranges that exclude 0 degrees. |
| `STARLIGHT_EVENT_LOG_CAPACITY` | 2097152 | Raises the event log capacity, between 2097152 and 67108864. Lower values keep the default. |
| `allowMissingAssets`, `correctedAdd` (CLI request keys) | off | Save over missing models; add resolving paths and terrain as the editor does. |
| `<scene>.autosave` | written every 2 minutes while edits are unsaved | Autosave file beside the scene, removed on a normal Save. |

Switches that exist only on the fleet and worktree branches (`STARLIGHT_JOINT_ANCESTOR_TRANSFORMS`, `STARLIGHT_VIEW_CULL`, `STARLIGHT_EDITOR_PLAY_PREPARE`, `STARLIGHT_LAVA_TIME_PERIOD`) are in 6.9 and are not in this table.

### 6.11 Open points for the next reader

- The branch-only commits in 6.9 are not in the integration branch. Merging them is a separate decision; their switches are not in the defaults table above.
- Section 1.2 lists the repair switches. `STARLIGHT_FIX_CAMERABOOM` (`8dd1fad`) is a repair switch added in this range and is not in that list.
- Section 1 does not list `STARLIGHT_ROLLING_RESISTANCE`, `STARLIGHT_DETERMINISTIC_RUBBLE`, `STARLIGHT_CAMERA_BOOM_SMOOTHING`, `STARLIGHT_SNAP_ONLY_MOVED`, `STARLIGHT_STRICT_HINGE_LIMITS`, `STARLIGHT_EVENT_LOG_CAPACITY` or the request keys `allowMissingAssets` and `correctedAdd`. The closing sentence of 1.4 (no other policy switches added in this range) covers only the earlier `91fc14d..repair-session-20261006` set. These switches are listed in 6.10.
