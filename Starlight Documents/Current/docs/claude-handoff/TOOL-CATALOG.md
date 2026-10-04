# Current tool catalogue

Every file currently under tools is listed below. C++/header/inl entries may be compiled components, not standalone programs. CLI switches are extracted from literal add_argument calls without running the tool; inspect --help for defaults and positional arguments. Historical migration/design scripts are separate from the automatic audit.

| File | Role | Declared switches |
| --- | --- | --- |
| `tools/audit/artifact_cleanup.py` | Validated build compaction, compact reports, final status and capture retention |  |
| `tools/audit/audit.cpp` | Isolated runtime audit and profiling controller; full, Gate 0, component and visible modes |  |
| `tools/audit/audit_engine.py` | Proposed common coordinator for builds, discovered CTest, native diagnostics and shareable reporting | `--audit-mode`, `--keep-artifacts`, `--output`, `--reuse-runtime`, `--root` |
| `tools/audit/audit_theme.hpp` | Analysis/reporting implementation or regression support |  |
| `tools/audit/build_deep_analysis.py` | Compatibility entry point for the canonical Starlight CMake/Ninja or tool workflow. |  |
| `tools/audit/contract_checks.cpp` | Test render-evidence acceptance/rejection rules |  |
| `tools/audit/DEEP-ANALYSIS-README.md` | Analysis/reporting implementation or regression support |  |
| `tools/audit/deep_analysis.py` | File census, source markers, build/contracts/PE inventory, saved JSON evidence discovery | `--extra-evidence`, `--output`, `--root`, `--workers` |
| `tools/audit/deep_analysis_gui.cpp` | Reusable desktop progress window with Start and report/folder actions |  |
| `tools/audit/engine_comparison.py` | Generate a dated, source-backed capability comparison, never an inferred benchmark. | `--from-run`, `--output` |
| `tools/audit/feature_evidence.py` | Join explicit feature IDs to build/source/workload/issue evidence |  |
| `tools/audit/handle_control.cpp` | Observe handles in no-engine idle/asynchronous control process |  |
| `tools/audit/knowledge_report.py` | Compose product knowledge, feature/workflow descriptions and coverage warnings | `--extra-evidence`, `--from-run`, `--output`, `--root` |
| `tools/audit/optimization_scan.py` | Compatibility entry point for the canonical Starlight CMake/Ninja or tool workflow. |  |
| `tools/audit/render_contract.hpp` | Shared render-report contract validation |  |
| `tools/audit/run_editor_parity_checks.py` | Run UI regression checks with deadline and explicit completion; capture feature evidence | `--timeout` |
| `tools/audit/source_lookup.py` | Read-only, freshness-checked queries over the existing analysis source index. | `--context`, `--expect`, `--file`, `--kind`, `--line`, `--root`, `--run`, `--symbol`, `--text` |
| `tools/audit/test_artifact_cleanup.py` | Deletion boundary and retention regression checks, using temporary files only. |  |
| `tools/audit/test_audit_engine.py` | Coordinator contract tests; no engine rendering or user scenes involved. |  |
| `tools/audit/test_deep_analysis.py` | Test the reporting tool on controlled fixtures |  |
| `tools/audit/test_engine_comparison.py` | Analysis/reporting implementation or regression support |  |
| `tools/audit/test_source_lookup.py` | Guard against editing the wrong declaration or using stale locations. |  |
| `tools/audit/tool-inventory.json` | Analysis/reporting implementation or regression support |  |
| `tools/build/Build-Native.py` | Canonical CMake/Ninja configure/build; optional CTest; supports isolated build directory | `--build-dir`, `--compact`, `--config`, `--test` |
| `tools/build/build_deep_analysis.py` | Build native analysis window and optionally launch it | `--build-dir`, `--launch`, `--output` |
| `tools/build/cmake_shaders.py` | Compile DXR/denoiser shaders and generate headers |  |
| `tools/build/prepare_renderer.py` | Prepare editor renderer header from engine source |  |
| `tools/checks/character_controller_checks.inl` | Compiled checks, fixtures or validation support |  |
| `tools/checks/check_adventure_export.py` | Fresh starter creation, standalone export, refusal safety, binary identity and hidden rendering | `--output`, `--runtime` |
| `tools/checks/classic_completion_checks.inl` | Additional editor interaction/feature checks |  |
| `tools/checks/classic_ui_checks.inl` | Built-in native editor UI assertions and resource cycles |  |
| `tools/checks/core_checks.cpp` | Exercise authored scene/core commands and geometry reference behavior |  |
| `tools/checks/diagnostic_checks.cpp` | Exercise crash diagnostics including isolated child crash/minidump |  |
| `tools/checks/gameplay_rule_ui_checks.inl` | Compiled checks, fixtures or validation support |  |
| `tools/checks/gameplay_rules_checks.inl` | Compiled checks, fixtures or validation support |  |
| `tools/checks/historical-managed-references.json` | Compiled checks, fixtures or validation support |  |
| `tools/checks/interface_scale_checks.inl` | Compiled checks, fixtures or validation support |  |
| `tools/checks/mixed_physics_fixture.py` | Create a combined physics acceptance scene from a standard starter file. |  |
| `tools/checks/physics_boundary_checks.inl` | Compiled checks, fixtures or validation support |  |
| `tools/checks/physics_presentation_check.py` | Exercise the real physics DLL: presentation must move between fixed ticks. |  |
| `tools/checks/quest_checks.inl` | Compiled checks, fixtures or validation support |  |
| `tools/checks/render_checks.cpp` | Native render diagnostics/benchmark, offscreen and visible/fullscreen modes |  |
| `tools/checks/scroll_layout_checks.inl` | Compiled checks, fixtures or validation support |  |
| `tools/checks/session_checks.cpp` | Exercise native sessions, scene lifetime and simulation; includes diagnostic modes |  |
| `tools/checks/solid_fixture.hpp` | Compiled checks, fixtures or validation support |  |
| `tools/checks/test_verify_no_managed.py` | The archive policy must never exempt active or changed managed code. |  |
| `tools/checks/texture_checks.cpp` | Check supported compressed texture loading and upload foundation |  |
| `tools/checks/verify_no_managed.py` | Detect managed sources/manifests/PE images under repository policy | `--output` |
| `tools/checks/visible_ui_checks.inl` | Visible editor walkthrough with focus/capture checks |  |
| `tools/diagnostics/analyze_pacing.py` | Summarize recorded frame intervals; never silently discard startup or hitches. | `--output`, `--start-seconds` |
| `tools/diagnostics/audit_input_runtime.hpp` | Runtime measurement or diagnostic support |  |
| `tools/diagnostics/audit_window_trace.hpp` | Record helper-window creation evidence |  |
| `tools/diagnostics/gate0_handle_trace.hpp` | Support lifecycle handle diagnostics |  |
| `tools/diagnostics/memory_allocation_trace.hpp` | Runtime measurement or diagnostic support |  |
| `tools/diagnostics/menu_timing.hpp` | Bounded opt-in menu handler timing; not input-to-display latency |  |
| `tools/launch/Launch-Starlight-Editor.cmd` | Launch normal native editor with terrain |  |
| `tools/launch/launch_analysis.py` | Open the Analysis Tool chooser without starting a test run. |  |
| `tools/profiling/optimization_scan.py` | Lexical optimization investigation sites and compiled structure-layout probe |  |
| `tools/README.md` | Tool documentation or relocation metadata |  |
| `tools/render/check_denoise.py` | GPU denoiser sequence/guide capture and offline image checks | `--build-dir`, `--config`, `--output`, `--packet` |
| `tools/render/validate_native.py` | Renderer tier/RR/fallback matrix and PE inspection | `--build-dir`, `--config`, `--output`, `--packet`, `--pe-only` |
| `tools/tool-relocations-2026-09-23.json` | Tool documentation or relocation metadata |  |

## Operational boundaries

Build tools write generated outputs. Runtime audits may create foreground windows and launch subprocesses. Fixture builders create test scenes; profiling tools consume recordings and are not game runtime. Compatibility wrappers forward to canonical implementations. Cleanup is narrowly scoped and logged; it is not arbitrary deletion. Private external identity tooling is intentionally excluded from this catalogue.

## Supplementary scripts outside tools

Root Build-Native.py and Launch-Starlight launchers are convenience entry points. migration contains retained baseline fixtures and historical transformation/verification utilities; do not bulk-run those scripts. design contains branding and document-generation helpers. deep includes renderer/physics/project-specific build support and third-party build files; CMakeLists.txt determines what the current product actually builds. See WORKSPACE-STRUCTURE.md for their ownership.
