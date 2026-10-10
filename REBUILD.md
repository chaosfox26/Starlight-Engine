# Rebuilding Starlight Engine from a fresh clone (Windows 11)

These steps were run end to end on a fresh clone of this repository on Windows 11 with Visual Studio 18 (Community, MSVC 19.51), CMake, Ninja and Python 3. Visual Studio 2022 or newer should work; only the version above has been tried.

## 1. What you need installed

| Item | Notes |
| --- | --- |
| Windows 11 x64 | The renderer needs Windows and DirectX 12. |
| Visual Studio 2022 or newer | The "Desktop development with C++" workload. It includes CMake and Ninja. |
| Windows SDK with `dxc.exe` | CMake looks in `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64` and then on `PATH`, and stops if it finds none. |
| CMake 3.25 or newer | |
| Ninja | The "Ninja Multi-Config" generator is used. |
| Python 3 | On `PATH` as `python`. Used by `verify_all.py` and the `py-` tests. |
| git | With submodule support. |

Every build command below runs inside the Visual Studio developer shell. Open "x64 Native Tools Command Prompt for VS", or run this once per shell:

```
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64
```

Adjust the path to your Visual Studio edition and version.

## 2. Clone, with LF line endings kept

```
git clone -c core.autocrlf=false --recurse-submodules <repository url> Starlight-Engine
cd Starlight-Engine
```

Use `core.autocrlf=false`. Some files in this repository are stored with CRLF and some with LF, and a few checks compare source text and starter scenes byte for byte, so a clone that rewrites line endings fails them.

The physics library is the git submodule `deep/third_party/JoltPhysics` (https://github.com/jrouwe/JoltPhysics.git). The superproject pins this exact commit:

```
0373ec0dd762e4bc2f6acdb08371ee84fa23c6db
```

Check it with `git -C deep/third_party/JoltPhysics rev-parse HEAD`. If the folder is empty, run `git submodule update --init deep/third_party/JoltPhysics`. The folder must be a real directory. A junction or symbolic link there makes the `native-no-managed` test report "Linked path requires separate verification".

## 3. Files that are not in git

Three outside inputs are needed. They are NVIDIA and Microsoft files. They are not stored in this repository, and their licences must be checked before you redistribute any of them.

1. **NVIDIA NGX SDK folder.** The default build links the NGX static ABI for ray reconstruction. Point `STARLIGHT_NGX_SDK` at the SDK folder; it must contain `include/nvsdk_ngx.h` and the `nvsdk_ngx_d.lib` libraries. There is no default path. Configure stops with a message that says to set the variable. The SDK comes from NVIDIA's developer downloads. Its licence is the NVIDIA RTX SDK licence (see `licenses/NVIDIA-RTX-SDK.txt` for the text kept in this repository).
2. **`nvngx_dlssd.dll`**, the NVIDIA ray reconstruction library. By default CMake looks for it at `<STARLIGHT_NGX_SDK>/../../bin/x64/nvngx_dlssd.dll`. Pass `-DSTARLIGHT_RR_DLL=<full path to the file>` if it lives elsewhere. The build copies it next to every executable that creates the denoiser (`Starlight.NativeRenderChecks`, `Starlight.NativeDenoiseChecks`, `Starlight.NativeEditor`, `Starlight.NativePlayer`). Without it the GPU denoise step fails with Win32 error 126 (module not found).
3. **The Agility D3D12 runtime folder, named `D3D12`**, with `D3D12Core.dll` and `d3d12SDKLayers.dll` (Agility SDK 1.619.6 from Microsoft). `python tools/packaging/prepare_graphics_validation.py` downloads the pinned NuGet package (SHA-256 is checked) and writes the folder to `out/classic/Release/D3D12`, which is the default `STARLIGHT_D3D12_RUNTIME_DIR`. Pass `-DSTARLIGHT_D3D12_RUNTIME_DIR=<folder>` to use another copy. The build copies the folder to a `D3D12` folder next to `Starlight.NativeRenderChecks.exe`. If it is missing the build only warns, and the GPU checks use the system runtime.

## 4. Configure and build

```
cmake -S . -B out/build -G "Ninja Multi-Config" -DCMAKE_CONFIGURATION_TYPES=Release ^
  -DSTARLIGHT_BUILD_RENDERER=ON -DSTARLIGHT_BUILD_PROJECTS=ON ^
  -DSTARLIGHT_NGX_SDK=C:/sdk/ngx-sdk ^
  -DSTARLIGHT_RR_DLL=C:/sdk/bin/x64/nvngx_dlssd.dll ^
  -DSTARLIGHT_D3D12_RUNTIME_DIR=C:/sdk/D3D12
cmake --build out/build --config Release -- -j 8
```

Replace the three `C:/sdk` paths with your own. The build has about 260 steps. On a 32-thread machine it took about 1.5 minutes. The executables are in `out/build/Release`.

The other vendor upscalers (DLSS through Streamline, FSR, XeSS, Anti-Lag 2) are off by default and need their own SDKs; see `THIRD-PARTY-NOTICES.md`.

## 5. Core checks

```
set STARLIGHT_SKIP_GPU_CHECKS=1
mkdir C:\sc\core
out\build\Release\Starlight.NativeCoreChecks.exe C:\sc\core migration\geometry-reference.json
```

Run it from the repository root. Use a short, empty scratch folder; long paths break some checks. With `STARLIGHT_SKIP_GPU_CHECKS=1` the checks that need a graphics device print `NOT RUN` and the last line is `PASS <n> native checks`. On the fresh clone used for this guide it printed `PASS 3676 native checks` with the flag set, and 3775 checks passed when the same suite ran on a free GPU with the flag unset (step `ledger` of `verify_all.py`, section 6). The last two numbers are the totals of this repository. The full suite before the club starter scene was removed from the published copy had 3794 checks; the 19 missing ones are the club starter card checks described below.

Texture harness, also from the repository root, with a fresh empty scratch folder:

```
mkdir C:\sc\tex
out\build\Release\Starlight.NativeTextureChecks.exe C:\sc\tex
```

It prints `365 texture checks passed (2K/4K DDS, BC5/BC7, memory loaders, WARP upload)`.

Some checks read a club starter scene that is not part of this repository. They print `NOT RUN: <reason>` and skip. They are never counted as passes. Nineteen core checks are skipped for that reason (the club starter card checks in `tools/checks/club_schema_checks.inl`, `adversarial_parser_checks.inl` and `decode_fuzz_checks.inl`).

## 6. The whole verification in one command

```
python tools/regression/verify_all.py --repo C:/src/Starlight-Engine --build-dir C:/src/Starlight-Engine/out/build/Release --results C:/src/results --python --gpu
```

Use absolute paths. `--python` runs the registered `py-` tests through `ctest`. `--gpu` also runs the denoise, fallback and render checks on a free GPU; leave it out while something else is using the GPU. A step that did not run is reported `NOT RUN`, never `PASS`. The summary is written to `<results>/verify-<time>.json`. Exit code 0 means every requested step passed.

Result on the fresh clone used for this guide (Release build, one GPU, nothing else using it): `SUMMARY: all requested steps passed`.

| Step | Result | What it covers |
| --- | --- | --- |
| ledger | PASS, 3775 checks, about 550 s | The core suite on the GPU, mapped onto the fix ledger. |
| python | PASS, 25 of 25 `py-` tests | Four tests are disabled on purpose and `py-source-payload` is skipped (exit code 77); `ctest` lists them as not run. |
| denoise | PASS | `Starlight.NativeDenoiseChecks.exe` on the recorded packet. |
| fallback | PASS | `Starlight.NativeFallbackChecks.exe`: tiers 0 to 4, `rr-auto`, `rr-disabled`, `dxr-disabled`, `rr-missing`. |
| render-* | PASS, 9 steps | `render-night-sky`, `render-grass-contacts`, `render-local-shadow-lifecycle`, `render-shadow-settings`, `render-glow-settings`, `render-resize`, `render-surface-textures`, `render-soot`, `render-fog`. |

A plain `ctest --test-dir out/build -C Release` run (with `STARLIGHT_SKIP_GPU_CHECKS=1`) ran 40 of the 46 registered tests. Every one passed except `native-no-managed` when the Jolt folder was a junction instead of a real directory (see section 2). The two tests `native-core` (the core suite, run separately above and inside `verify_all.py`) and `native-session` (`Starlight.NativeSessionChecks.exe`) were left out of that run; the session harness was not run for this guide.

## 7. About the documents

The documents under `Starlight Documents/` are dated working notes and audit snapshots. A few of them mention a separate game project, written `GameProject`, that is not part of this repository. Paths and links into it do not exist here.
