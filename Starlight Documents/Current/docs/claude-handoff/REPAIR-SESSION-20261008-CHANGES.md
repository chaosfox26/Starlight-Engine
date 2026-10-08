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
