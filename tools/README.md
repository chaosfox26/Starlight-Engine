# Starlight Engine tools

Adventure export validation: the CTest `native-adventure-export` runs the C++ `Starlight.NativeExportChecks` (CMakeLists.txt line 172). `tools/checks/check_adventure_export.py` is a separate standalone script that is not registered in CTest or CMake: given `--runtime <folder with the built NativeCommands, NativePlayer, Viewport and Physics>` and `--output <folder>` it creates a fresh starter scene and package, checks refusal safety, and renders 60 frames in a hidden standalone player (it starts the player, so run it deliberately). It records tested binary hashes; this does not certify visible controls, polish or clean-machine deployment. It was corrected to keep the starter's schema 16 (it used to overwrite it with schema 11) but has not been run since.

This folder belongs exclusively to Starlight Engine. `D:/projects/tools` is a separate shared location and was not changed.

| Folder | Purpose | Entry points |
|---|---|---|
| build | Native builds, shader preparation, analysis-window build | Build-Native.py, build_deep_analysis.py, cmake_shaders.py |
| launch | User editor launcher | Launch-Starlight-Editor.cmd |
| checks | Core, diagnostics, texture, session, render and embedded editor tests; native-only guard | CMake/CTest targets |
| render | Specialized denoising and renderer-fallback validators | check_denoise.py, validate_native.py |
| diagnostics | Shared window/handle/menu instrumentation | Headers included by editor and checks, not standalone applications |
| profiling | Optimization investigation tools | optimization_scan.py |
| audit | Audit controller, reporting, evidence linking, progress UI and reporting self-tests | deep_analysis.py, knowledge_report.py, audit.cpp, test_deep_analysis.py |

## What to use

- Build the editor: the root `Build-Native.py` forwards to `tools/build/Build-Native.py`. Existing command lines remain supported.
- Open the editor: the root launcher forwards to `tools/launch/Launch-Starlight-Editor.cmd`; its existing out/classic target is unchanged.
- Generate source/evidence reports: **Feature report** performs inventory and evidence analysis. **Error checks** builds isolated audit binaries and coordinates native checks. **Run both** adds the source/feature briefing to the runtime findings. The current coordinator is `tools/audit/audit_engine.py`; desktop workflow validation is in progress.
- Run native checks: use the canonical build with `--test`, or the configured CTest targets. Runtime audit stages are separate; see the tool map.
- Specialized rendering validators now accept explicit build, scene-packet and fresh output paths, and participate in error/combined mode. They check diagnostic workloads, not comparable full-screen FPS. Missing fixtures are reported as not-run.

## Consolidated implementations

Build compatibility entry points call `tools/build/Build-Native.py` directly; redundant intermediate build wrappers have been removed with originals preserved in the relocation backup. `editor/check-editor.ps1` calls that same implementation with testing enabled. Audit execution and the editor parity runner share the process timeout/logging implementation in `audit_engine.py`. The coordinator delegates the session test to the native harness's traced lifecycle run, retaining its distinct idle-control check; it does not count a second identical session run.

Shared diagnostic headers remain dependencies of the editor and tests. They are not duplicates of the tests that consume them. Likewise, source inventory, runtime error checks and image-quality checks answer different questions and remain separate components behind the common launcher.

### Native guard archive policy

`checks/verify_no_managed.py` still scans all repository files. Two exact historical comparison DLLs are classified in `historicalReferences`, using the paths and SHA-256 values in `checks/historical-managed-references.json`. No directory is exempt: changed archives, additional managed sources, and copies in active build outputs fail. Run `checks/test_verify_no_managed.py` to exercise those safeguards.

### Lifecycle startup and extended checks

Session checks retain separate startup observations before measuring matched scene endpoints. A device-and-queue-only reproduction showed delayed graphics-runtime resource changes at 30 and 60 seconds without engine code. The check therefore observes at least 75 seconds and requires 15 seconds of stable handles, with a 120-second startup deadline. This only affects diagnostics; it adds no engine pacing delay. The original handle and memory plateau limits remain unchanged. `STARLIGHT_LIFECYCLE_BATCH_CYCLES` optionally increases each measured batch from 40 to at most 1,000 cycles; it cannot shorten the default. Startup and measured-stage progress are saved separately. A bounded pass does not certify arbitrary session length.

Guarded source navigation uses `tools/audit/source_lookup.py` over the existing analysis index; it does not introduce a second indexer. See the analysis documentation for filters and freshness limits.

## Navigation and history

[Complete tool map](../TOOLS-MAP-2026-09-23.md), [machine-readable inventory](audit/tool-inventory.json), [old-to-new path map and backup location](tool-relocations-2026-09-23.json), [analysis tool documentation](audit/DEEP-ANALYSIS-README.md).

Compatibility scripts at old locations contain forwarding only; edit canonical implementations here. Test implementations moved here are still compiled into their original targets. Editor runtime code, project host and UI feedback code remain with the engine: they are product components, not standalone tools. Historical migration/document-rewriting utilities remain in their original locations and are not part of automatic audit execution.

Existing dated audit reports are immutable evidence; their historical paths are not rewritten. The relocation manifest resolves those paths. Current build/scanner references and tool inventory use the canonical paths. Preserve unrelated working-tree changes.

The next task is code-alignment review of each tool and completion of the common audit workflow. Consolidating files does not certify engine functionality or complete that integration.
