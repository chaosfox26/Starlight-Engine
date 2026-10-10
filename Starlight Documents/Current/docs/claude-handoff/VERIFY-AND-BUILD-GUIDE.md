# Verify and build guide (native engine, Windows)

How to build and verify the Starlight native engine from a fresh checkout or a new git worktree.

Written from the scripts on branch `integration-20261009` (tip `a30dc0f`, 8 October 2026). It describes what the scripts do. It is not a test result. Run the suites to get a result for the build in front of you. The commands below were read from the scripts and the CMake files. They were not all re-run end to end while writing this guide.

Always use absolute paths in scripted runs. Paths below use `<worktree>` for the checkout root and `<results>` for the results folder.

## 1. Required environment

| Item | Where or value | Notes |
| --- | --- | --- |
| Windows x64 | Windows 11 Pro used on this machine | |
| Visual Studio C++ toolchain | `C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat` | Load it with `-arch=x64` before every build. A plain shell cannot compile the engine. |
| CMake | 3.25 or newer | Configure with the generator `Ninja Multi-Config`. |
| Ninja | Ships with the Visual Studio CMake component | `tools\build\Build-Native.py` adds it to `PATH` when `cl.exe` is not already there. |
| Python 3 | On `PATH` as `python` | Needed by `verify_all.py`, `run_ledger.py`, the impact tool and the `py-` ctest tests. |
| Windows SDK, DXC | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\dxc.exe` | `CMakeLists.txt` calls `find_program(... dxc ... REQUIRED)`, so configure stops without it. |
| Jolt Physics | `deep\third_party\JoltPhysics` | A git submodule (see `.gitmodules`). An empty folder in a worktree. Section 3 links a populated copy. |
| NGX SDK | The folder of the NVIDIA NGX SDK, for example `C:\sdk\ngx-sdk` | Used by the renderer. There is no default path and the SDK is not stored in this repository: pass `-DSTARLIGHT_NGX_SDK=<absolute path>`. Configure stops with `Set STARLIGHT_NGX_SDK to the folder of the NVIDIA NGX SDK` when `include\nvsdk_ngx.h` is missing. |

The configure flags used by the setup script are:

`-G "Ninja Multi-Config" -DCMAKE_CONFIGURATION_TYPES=Debug;Release -DSTARLIGHT_BUILD_RENDERER=ON -DSTARLIGHT_BUILD_PROJECTS=ON -DSTARLIGHT_NGX_SDK=<absolute NGX path>`

`CMakePresets.json` has a `native` preset with the same generator and configurations, but its binary folder is `out/native`. `tools\build\Build-Native.py --build-dir <folder>` configures that folder from the preset, without an NGX override, so the NGX default rule above applies.

## 2. Two files a fresh build folder needs

Both must sit next to the executables in each configuration folder (`Release` and `Debug`):

1. `nvngx_dlssd.dll`. The GPU denoise step loads it. Without it the denoise check fails its runtime denoiser switch, and the process reports `Win32 126` (module not found). This is an environment problem, not a code failure.
2. The `D3D12` folder (the Agility D3D12 runtime, for example `D3D12Core.dll`). The engine loads its D3D12 runtime from this folder. Without it the engine cannot create a device.

Where they come from:

- `agent-setup.ps1` (section 3) copies both from `D:\projects\Starlight-Engine-repair\out\classic\Release` into `out\agent\Release` and `out\agent\Debug`, when that source exists.
- `verify_all.py --gpu` copies the same two items into `--build-dir` when they are missing, from the same source folder. Without `--gpu` it copies nothing. If the source folder is missing, the GPU steps stop with exit code 2 (NOT RUN).
- The source folder is hard-coded in both scripts. If that build tree is removed, copy the two items from any working build, and change the paths in both scripts before relying on them.

## 3. Fresh checkout or new worktree

1. Create the worktree from the base commit, for example:
   `git -C D:\projects\Starlight-Engine-repair worktree add <worktree> -b <new-branch> <base-commit>`
   Never delete a worktree or branch from another session to make room.
2. Run the setup script with an absolute path:
   `& D:\projects\_recovery\claude-fix-20261002\agent-setup.ps1 -Tree <worktree>`
   It does four things:
   - Copies ignored check fixtures from `D:\projects\Starlight-Engine-repair\editor\checks` into the worktree. Robocopy copies only files that are missing from the worktree. About 700 ignored fixture files live in that folder.
   - Copies three ignored migration files if they are missing: `migration\classic-advanced.jpg` and `migration\fixtures\triangle.glb` and `deep-node-graph.glb`. On `integration-20261009` these three are tracked, so the copy does nothing.
   - Links `deep\third_party\JoltPhysics` as a junction to `D:\projects\Starlight-Engine\deep\third_party\JoltPhysics`.
   - Configures `out\agent` (section 1 flags), then copies the two runtime items from section 2.
   It does not build anything.
3. Build the targets you need (section 4).
4. Before the worktree is ever removed, take the junction out without following it:
   `cmd /c rmdir "<worktree>\deep\third_party\JoltPhysics"`
   Do not use a recursive delete on that path. It can reach the real Jolt folder in the main checkout.

## 4. Build

Run from the worktree root, one target at a time:

```
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 >nul 2>&1 && cmake --build out/agent --config Release --target Starlight.NativeCoreChecks -- -j 3 2>&1'
```

- Pass `-j` explicitly. The CMake presets ask for 12 jobs. Use `-j 3` or less when other builds run on the same machine.
- Targets that matter for verification:
  - `Starlight.NativeCoreChecks`: the core suite (`tools\checks\core_checks.cpp`, groups in `tools\checks\*.inl`).
  - `Starlight.Viewport`: the viewport DLL. The device-failure checks load `Starlight.Viewport.dll` at run time. If it is missing, check CC-07 fails with "the viewport module loads".
  - `Starlight.NativeTextureChecks`, `Starlight.NativeSessionChecks`, `Starlight.NativeRenderChecks`.
  - `Starlight.NativeDenoiseChecks`, `Starlight.NativeFallbackChecks` (the GPU steps of `verify_all --gpu`).
  - `Starlight.NativeGuardChecks` (no-managed-code policy).
  - `Starlight.Physics` (the physics presentation check).
  - `Starlight.NativeEditor`, `Starlight.NativePlayer`, `Starlight.NativeExportChecks` (the ctest UI and export tests).
- Stale builds: `verify_all.py` and `run_ledger.py` refuse to run when the build has pending steps (they ask `cmake --build ... -- -n`) or when a source file under `editor/`, `deep/` or `tools/checks/` is newer than `Starlight.NativeCoreChecks.exe`. Rebuild first. A failed build leaves the old executable behind, and that old executable can still pass.

## 5. Suites

### 5.1 Core suite

```
out\agent\Release\Starlight.NativeCoreChecks.exe <fresh scratch folder> migration\geometry-reference.json
```

- Run from the worktree root. The second argument is the geometry reference file.
- Duration: about 5 to 7 minutes. One recorded run under CTest took 314 s.
- Output in the scratch folder: `result.json` (schema `starlight-check-result-2`, with `success`, `total`, `checks` and, on failure, `error`) and `progress.log` (one line per passed check, flushed as each passes).
- Exit codes: 0 all checks passed. 1 a check failed (its name is printed to stderr, and `result.json` holds the error). 3 a filtered run (section 5.1.1), which is never a full pass.
- Pass counts differ by branch. Recorded totals on the repair branches run from 2873 to 2900. Compare with the last recorded run of the same branch, not with a fixed number.
- Without `STARLIGHT_SKIP_GPU_CHECKS=1`, the device-failure checks (CC-07, CC-02) use a real GPU device (section 7).

#### 5.1.1 Filtered runs (development only)

- `$env:STARLIGHT_CHECK_GROUPS='name1,name2'` runs only those groups and exits 3.
- `$env:STARLIGHT_CHECK_GROUPS='list'` prints the group names.
- `tools\regression\impact.py --repo <worktree> --base <commit>` suggests which groups to run for a change.
- A filtered run never marks a ledger entry VERIFIED. Unset the variable before a full run.

### 5.2 Texture harness

```
out\agent\Release\Starlight.NativeTextureChecks.exe <fresh scratch folder> [--cpu-only] [<repo root>]
```

- Run it from the worktree root. The full run includes a GPU section that uses a D3D12 device. `--cpu-only` runs only the DDS and memory loader checks, and says so in its output.
- Run it when GLB or animation code changed. CTest registers it as `native-textures`.

### 5.3 Session harness (GPU)

```
out\agent\Release\Starlight.NativeSessionChecks.exe <fresh scratch folder> <worktree>
```

- The second argument is the checkout root. The harness loads `deep\projects\Empty\Empty.star` from it, and checks that `deep\settings\native-1440p.startest` is unchanged afterwards.
- Run it when `native_session` or `render_scene` changed. CTest registers it as `native-session`.
- Uses the GPU. One GPU run at a time (section 7).

### 5.4 Render harness (GPU)

- `Starlight.NativeRenderChecks.exe` is a windowed (WIN32) program. Neither CTest nor `verify_all` runs it yet. The CMake file says so.
- Arguments, from `tools\checks\render_checks.cpp` lines 133 to 138: packet file, report file, screenshot file, width, height, then optional tier, mode (default `focused`), bounces and denoiser. Example recorded on 7 October: the fixed packet at 2560x1440, one frame.
- Uses the GPU. One GPU run at a time (section 7).

### 5.5 Guard (no managed code)

- `Starlight.NativeGuardChecks.exe --self-test` checks the guard's own rules.
- `Starlight.NativeGuardChecks.exe --root <worktree> --manifest tools\checks\historical-managed-references.json --output <file>` scans every tracked file. The CTest entry writes `migration\no-managed-results.json` inside the checkout, so that file changes on every run.
- The guard reports a junction as "Linked path requires separate verification". In a worktree the Jolt junction triggers this. It is a worktree artifact, not an engine violation. Do not edit the manifest to hide it.

### 5.6 CTest

```
ctest --test-dir <worktree>\out\agent -C Release --output-on-failure
```

- `--test-dir` is the configure folder (`out\agent`), where the CTest files are. It is not the `Release` folder.
- `native-core` runs from the repository root (fixed in `2b43d9c`), so running CTest from the build folder no longer breaks it.
- Four Python tests are registered but disabled. They show as "Not Run (Disabled)", not as passes: `py-test_analysis_gui_source_build`, `py-test_audit_engine`, `py-test_deep_analysis` and `py-test_verify_no_managed`. The first three fail from a clean checkout because the native executables or paths they need are missing.
- Regression tool self-tests are registered as `py-regression-run_ledger`, `py-regression-registry`, `py-regression-verify_all` and `py-regression-impact`.

### 5.7 verify_all (one command, recommended)

```
python <worktree>\tools\regression\verify_all.py --repo <worktree> --build-dir <worktree>\out\agent\Release --results <results folder> [--python] [--gpu]
```

- `--build-dir` is the `Release` folder that holds the executables. Its parent holds the CMake cache.
- Steps, in order:
  - `ledger` (always): the core suite, mapped onto the fix ledger.
  - `python` (`--python`): `ctest -R py-`, with a 600 s limit per test and 1200 s overall.
  - `denoise` and `fallback` (`--gpu` only): the GPU checks on the recorded packet, with a 1800 s limit each.
- Without `--gpu`, the core suite runs with `STARLIGHT_SKIP_GPU_CHECKS=1`. CC-07 and CC-02 are then reported as LOST COVERAGE, which is never a pass.
- Exit codes: 0 every requested step passed and the ledger is all VERIFIED. 1 a step failed or something is not verified. 2 a requested step could not run, including a stale build, a missing GPU runtime file or a missing executable.
- Output:
  - `<results>\verify-<time>.json` is a new file for each run.
  - `<results>\latest.json` and `<results>\<build id>.json` are overwritten by a later run of the same build.
  - `<results>\history\<runId>.json` is never replaced.
  - `<results>\runs\<runId>\` holds `result.json` and `progress.log`.
  - `<results>\packets\` holds failure packets. `<results>\notices.log` holds transitions only.
- The default results folder is `D:\projects\_recovery\regression-results-branch`. Pass `--results` to use another one.
- `verify_all.py --self-test` checks the runner itself without the engine.

### 5.8 run_ledger alone

```
python <worktree>\tools\regression\run_ledger.py --build-dir <worktree>\out\agent\Release [--repo <worktree>] [--results <results folder>] [--timeout 1800]
```

- The default `--repo` is the checkout that the tools sit in.
- Refuses to run (exit 2) when the build was configured from another checkout, when the build is stale, or when `ledger.json` and `check_registry.json` disagree.
- The runner fills in a fixed command from `tools\regression\check_registry.json`. Nothing else is executed.

## 6. Scratch folders

- Give each run a new, empty folder, with a short absolute path, for example `D:\scratch\core-<date>-<n>`. The short path is the fleet convention. No script enforces a length limit.
- The harnesses create the folder but do not refuse one that already exists. The core suite deletes only `result.json` and `progress.log` when it starts, so anything left by an earlier run (for example `engine-assets`) can change what a later run sees. Use a new folder every time.
- `run_ledger` and `verify_all` make their own run folder under `<results>\runs\<runId>`. Do not choose it yourself.
- Never point a scratch folder at a checkout or at `<results>\history`. Never delete anything under `<results>\history`.

## 7. GPU rules

- Steps that use the GPU:
  - The core suite device-failure checks (CC-07, CC-02), unless `STARLIGHT_SKIP_GPU_CHECKS=1` is set.
  - The texture harness full run (not `--cpu-only`).
  - The session and render harnesses.
  - `verify_all --gpu` (denoise and fallback).
- Run one GPU run at a time. Never run several GPU checks at once. Do not run GPU checks while someone is using the machine for a game or other GPU work. Keep a GPU run under about five minutes where you can.
- Device-failure checks take a named mutex, `Local\Starlight.DeviceFailureChecks`. They wait up to 30 minutes for it. If they cannot get it, they print `NOT RUN`. The name is in the `Local` namespace, so the lock covers only processes in the same Windows logon session. It does not stop a second signed-in user session.
- These checks deliberately break the GPU device. Several of them at once have upset the NVIDIA driver (event 153) and left test processes stuck in the kernel.
- No repo script reads a `gpu.lock` folder. The only lock the scripts take is the mutex above.
- Timing numbers taken on a loaded machine are not reliable. Rerun on an idle machine, and report variance.

## 8. Troubleshooting (failures seen 7 to 8 October 2026)

| Symptom | Cause | What to do |
| --- | --- | --- |
| `Win32 126` from the denoise step, or the "runtime denoiser switch" check fails | `nvngx_dlssd.dll` is not next to the executables | Run `agent-setup.ps1`, or `verify_all --gpu`, which copies it. Environment, not code. |
| `verify_all` exits 2 with "the D3D12 runtime folder is missing" | The source `out\classic\Release\D3D12` is not in the repair checkout | Restore it from a working build (section 2). The engine cannot create a device without it. |
| `verify_all` prints `NOT RUN: N build steps are pending` or `source files are newer than the built checks` | The build is stale | Rebuild the target first. Stale builds are refused, so no pass is recorded for them. |
| `run_ledger` prints `REFUSING TO RUN: the build was configured from ...` | The build folder belongs to another checkout | Point `--repo` at the checkout that built it, or rebuild in this worktree. |
| `run_ledger` prints `REFUSING TO RUN: the ledger and the committed check registry disagree` (exit 2) | `ledger.json` and `check_registry.json` differ | Fix the mismatch in the committed files. Do not edit `ledger.json` or any baseline to get a pass. |
| Core suite exits 3 | `STARLIGHT_CHECK_GROUPS` is set (a filtered run) | Unset it for a full run. Exit 3 is never a pass. |
| CC-07 or CC-02 shows as LOST COVERAGE in the ledger | `--gpu` was not given, so `STARLIGHT_SKIP_GPU_CHECKS=1` was set | Expected. Run with `--gpu` on a free GPU to cover them. |
| `device failure checks: NOT RUN (another process held the GPU failure lock for 30 minutes)` | Another process holds `Local\Starlight.DeviceFailureChecks` | Find the holder and let it finish. Do not kill another run to get the lock. |
| CC-07 fails "the viewport module loads" | `Starlight.Viewport.dll` is not built, or not next to the core executable | Build `Starlight.Viewport` in the same configuration. |
| Core check "A prepared destructible breaks into pieces" failed once (check 163) | First baseline run was launched from the wrong folder. The later intermittent failure (resident fragment count capped at 256) is fixed: the check now counts pieces created. | Rerun the single group. Record `progress.log` if it recurs. It was not reproduced in 40 isolated repeats. |
| `native-core` fails when CTest runs from the build folder | Old working-directory bug | Fixed in `2b43d9c`. Run CTest with `--test-dir`, as in section 5.6. |
| `native-no-managed` reports "Linked path requires separate verification" | The Jolt junction in a worktree | Expected in a worktree. Do not edit the manifest. Check the main checkout instead. |
| `native-textures` reports a missing `editor/checks/corner-controls/left-controls.png` | The worktree is older than `integration-20261009` | On that branch these PNGs are tracked. Reset the worktree to the branch tip. |
| `cmake` configure fails with `Set STARLIGHT_NGX_SDK ...` | the NGX SDK folder was not given | Pass `-DSTARLIGHT_NGX_SDK=<absolute path>` (section 1). |
| Configure fails at `dxc` (`REQUIRED`) | Windows SDK 10.0.26100.0 is not installed, and `dxc.exe` is not on `PATH` | Install the Windows SDK, or put `dxc.exe` on `PATH`. |
| Jolt sources not found at configure | The submodule folder is empty | Run `git submodule update --init deep/third_party/JoltPhysics`, or let `agent-setup.ps1` link a populated copy. |
| A crashed run shows exit code 127 in Git Bash | Git Bash reports a Windows fast-fail (0xC0000409) as 127, and `timeout` also shows 127 | Use the PowerShell runner `D:\projects\_recovery\claude-fix-20261002\stress\varrun.ps1` to get the real exit code. |
| Visible or stress runs fail when the editor loses focus to the Windows shell (`ApplicationManager_DesktopShellWindow`) | Not a code bug. Visible checks need foreground access. | Run them when the desktop is free. The crash behind earlier stress failures (threads not joined on the error path) is fixed in `fffe0c4`. |
| GPU stays busy or stuck after device-failure checks (NVIDIA event 153, stuck test processes) | Test processes left GPU contexts behind | From an elevated PowerShell, run `D:\projects\_recovery\claude-fix-20261002\restart-nvidia-service.ps1`. The screen may flicker. A reboot also clears them. |
| `backup rename failed` at `deep/graphics_runtime/gr_util.cpp:144` (`MoveFileExW`) | Another process held the backup file. One-off, and the rerun passed 2900/2900. | Find what holds the file before rerunning. |
| stderr shows `terrain cook failed: injected terrain cook failure` or `prefracture: preparation failed for Character` | Checks inject these failures on purpose (`deep/Physics/PhysicsWorld.cpp:213` and the fracture path) | Not a verdict. Judge the run by `result.json` and the exit code. |
| `py-` tests show "Not Run (Disabled)" | Four Python tests are disabled on purpose (section 5.6) | Not a pass. Do not count them. |

## 9. Quick checklist for a new worktree

1. Create the worktree from the base commit (section 3, step 1).
2. Run `agent-setup.ps1 -Tree <worktree>`.
3. Build `Starlight.NativeCoreChecks` and `Starlight.Viewport` with `-j 3` under VsDevCmd.
4. Run `verify_all.py` with absolute `--repo`, `--build-dir` (the `Release` folder) and `--results`. Add `--python` and `--gpu` only when the GPU is free.
5. Read the summary, the exit code and `result.json`. A run that did not happen is NOT RUN, not PASS.
6. Remove the Jolt junction with `cmd /c rmdir` before the worktree is ever removed.
