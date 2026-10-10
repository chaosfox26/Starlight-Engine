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
| projects | Existing project content |
| out | Generated current build products: classic for the editor shortcut, native for the default preset, analysis-tool for the analyzer |
| Starlight Documents/Current | Canonical docs, with clearly marked history below history/ |

## Supporting folders, not alternate engines

| Folder | Purpose |
| --- | --- |
| analysis | Compact reports and retained historical evidence; new runs use analysis/run-*. This is not source or an alternate engine |
| audit-runs | Original regression evidence, including the required comparison.bin fixture |
| Starlight Documents/Current/docs/claude-handoff/analysis | Selected analysis evidence kept beside the current handoff |
| publication | Independent sanitized checkout of this repository for publication. Never use it as the primary development tree. |
| _quarantine (removed) | Former temporary holding area, manually deleted by the owner after review. It is no longer available for restoration. See CLEANUP-STATUS.md for post-removal verification. |

The analyzer excludes analysis, publication and _quarantine from its engine file census and records that scope explicitly. Existing reports keep their original paths as historical evidence.

## Build and test

Run commands from this workspace root. Build the shortcut's editor with `python Build-Native.py --config Release --build-dir out/classic`. Omitting --build-dir uses out/native. Build the Analysis Tool with `python tools/build/build_deep_analysis.py --build-dir out/analysis-tool`. Root launchers and desktop shortcuts now refer inside this workspace.

The relocated publication checkout has its own Git metadata. Its classic, audit-engine and verification CMake caches were freshly regenerated after relocation and now point to publication inside this workspace. Its CMake requires an explicit STARLIGHT_NGX_SDK setting; the existing external SDK was supplied during regeneration. This verified configuration, not a new publication compile. Do not copy licensed SDKs during this cleanup.

## Keeping generated files under control

Use `python Build-Native.py --config Release --build-dir out/classic --compact` for a compact build. Add `--test` when the configured tests should run before cleanup. Normal builds retain their caches. Compact cleanup runs only after successful requested commands, accepts one CMake build belonging to this source tree under `out`, and removes only OBJ/PCH/ILK intermediates. It preserves EXE/DLL/LIB/PDB files, assets and reports. Each operation leaves a manifest under that build's `cleanup-records` directory. The next build regenerates intermediates.

Analysis runs now keep compact written reports, structured stage results, failure explanations, and reporting-script snapshots by default. After reports are saved, newly generated screenshots, test exports and other bulky run artifacts are removed even when tests fail. Files present before the invocation, files outside that run, and redirected paths are protected. `summary-retention.json` records removals; cleanup failures are visible in the final status. Use the command-line `--keep-artifacts` option when raw diagnostic captures are needed. Audit builds use compact build cleanup; normal development builds remain unchanged. Historical successful-run BMP retention also remains for older archives.

## Scope boundary

Claude Code and Starbus are separate projects and are untouched. Old automation snapshots were retired with quarantine; the active project and model assets remain in place. Cleanup does not implement new features or resolve the earlier telemetry and denoiser failures. Compiler intermediates were removed to reduce size and will regenerate on future builds; installed binaries remain available.

## Where to make a change

| Task | Starting location |
| --- | --- |
| Editor UI and gameplay | editor/cpp |
| Rendering and motion vectors | editor/native and deep/engine |
| Physics integration | deep/Physics |
| Analysis runner and final results | tools/audit/audit_engine.py and deep_analysis_gui.cpp |
| Report retention and compaction | tools/audit/artifact_cleanup.py |
| Build compaction | tools/build/Build-Native.py |
| Launcher behavior | tools/launch and root Launch-Starlight-*.cmd |
| Editor and Analysis icons | design/branding |
| Historical notes removed from code | Starlight Documents/Current/docs/SOURCE-HISTORY-NOTES.md |
| Historical release-note template | Starlight Documents/Current/migration/CLASSIC-RELEASE-HISTORICAL-NOTE.md |
| Scene format, shared asset library, migration, readiness contract | Starlight Documents/Current/docs/architecture/KEY-CARD-ARCHITECTURE.md |

## Latest verified state

The latest full run is analysis/run-20260929-213020-5404-8804937: 10 of 13 stages passed. Remaining failures are the editor-controls deadline, fullscreen timing-display visibility, and denoiser object motion vectors. Summary and retention changes were checked separately afterward; they do not resolve these runtime failures. Raw captures and temporary packages from this run were compacted; written results remain. Do not interpret missing historical captures as missing engine assets.


Existing archives are dated snapshots, not live mirrors. Folder sizes change with builds and tests; use current measurements rather than old report sizes.
