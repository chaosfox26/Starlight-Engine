# Starlight: folder map and working instructions

**Work in D:\projects\Starlight-Engine. This is the current development engine.** Read this file first; do not choose a version from old executables or historical reports.

## Everyday entry points

1. Launch-Starlight-Editor.cmd opens the installed editor from out/classic/Release.
2. Launch-Starlight-Analysis.cmd opens the analysis chooser. It does not start a run until you choose one.
3. Starlight Documents/Current/README.md describes the product and build requirements.
4. Starlight Documents/Current/docs/claude-handoff/START-HERE.md begins the comprehensive collaborator handoff. Its older test results are dated evidence.
5. CLEANUP-STATUS.md records this cleanup's verification and remaining work.

## Active folders

| Folder | Purpose |
| --- | --- |
| editor/cpp | Editor, scene model, gameplay, authoring and serialization |
| editor/native | Native viewport integration and shaders |
| deep/engine | Shared renderer, workers, model/texture handling and effects; live code despite the older folder name |
| deep/Physics | Current physics integration |
| deep/src | Project host and shared JSON dependency |
| deep/third_party/JoltPhysics | Pinned physics dependency, including its source Build folder |
| deep/projects, deep/settings | Native project modules, manifests and test presets |
| tools | Current build, analysis, checks, diagnostics and launch tools |
| cmake | Current build support |
| migration | Baseline geometry and fixtures used by regression checks; do not remove wholesale |
| design/branding | Branding resources |
| projects | Existing project content; GameProject is outside cleanup scope |
| out | Generated current build products: classic for the editor shortcut, native for the default preset, analysis-tool for the analyzer |
| Starlight Documents/Current | Canonical docs, with clearly marked history below history/ |

## Supporting folders, not alternate engines

| Folder | Purpose |
| --- | --- |
| analysis | Reports, raw profiling data and old backup evidence; new runs use analysis/run-* |
| audit-runs | Original regression evidence, including the required comparison.bin fixture |
| Starlight Documents/Current/docs/claude-handoff/analysis | Selected analysis evidence kept beside the current handoff |
| publication | Independent sanitized GitHub checkout, formerly D:\projects\Starlight-Publish. Never use as the primary development tree or push the private parent Git history |
| _quarantine (removed) | Former temporary holding area, manually deleted by ChaosFox after review. It is no longer available for restoration. See CLEANUP-STATUS.md for post-removal verification. |

The analyzer excludes analysis, publication and _quarantine from its engine file census and records that scope explicitly. Existing reports keep their original paths as historical evidence.

## Build and test

Run commands from this workspace root. Build the shortcut's editor with `python Build-Native.py --config Release --build-dir out/classic`. Omitting --build-dir uses out/native. Build the Analysis Tool with `python tools/build/build_deep_analysis.py --build-dir out/analysis-tool`. Root launchers and desktop shortcuts now refer inside this workspace.

The relocated publication checkout has its own Git metadata. Its classic, audit-engine and verification CMake caches were freshly regenerated after relocation and now point to publication inside this workspace. Its CMake requires an explicit STARLIGHT_NGX_SDK setting; the existing external SDK was supplied during regeneration. This verified configuration, not a new publication compile. Do not modify GameProject or copy licensed SDKs during this cleanup.

## Scope boundary

Claude Code and Starbus are separate projects and are untouched. Old automation snapshots were retired with quarantine; the active game project and model assets remain in place. Cleanup does not implement new features or resolve the earlier telemetry and denoiser failures. Compiler intermediates were removed to reduce size and will regenerate on future builds; installed binaries remain available.
