# Starlight Engine — current comprehensive handoff

Updated 29 September 2026. This is the current orientation document; dated handoffs describe earlier checkpoints. It documents existing work and does not by itself authorize new development.

## 1. What the engine is

Starlight is a native Windows world/scene editor, rendering and simulation system, local command interface, standalone adventure player/export workflow, and native project-module host. The editor supports authoring, saving and previewing scenes with gameplay and dynamic effects. It is not a complete cross-platform commercial-engine replacement. Current CMake explicitly requires Windows and DirectX 12. Linux/iOS support and universal FPS targets were discussed as a practice prompt, not delivered features or authorized platform work.

## 2. Read in this order

1. This document for the current checkpoint, operating boundaries and next steps.
2. Root WORKSPACE-STRUCTURE.md for the physical folder map and task-to-source routing.
3. FEATURE-CATALOG.md beside this document for implemented systems, limitations and evidence.
4. TOOL-CATALOG.md beside this document for every current tools-directory file and recognized CLI switches.
5. Root CLEANUP-STATUS.md for dated cleanup evidence. Its older paragraphs are historical.
6. README.md here for the earlier physics closeout and precise bounds. Its earlier test counts do not supersede the latest run.

## 3. Which checkout is which

The owner workstation development root is D:/projects/Starlight-Engine. editor and deep are parts of one engine, not competing versions. publication is an independent sanitized checkout connected to the private GitHub repository. Never push the private parent development history. A fresh clone of the GitHub repository is itself a complete publication source checkout; it need not contain a nested publication folder.

The current project owner and publisher is ChaosFox. Earlier anonymization used Chaos for project identities. Do not merge old publication history back. The publication checkpoint at this handoff is 179c14109d1f82616d091fc743f66e94b8684b45. Subsequent folder-map and this comprehensive documentation work are local pending publication. Check git status and HEAD before making changes. The local development copy also contains uncommitted work; never reset it to the publication tree.

The external privacy utility is outside the engine workspace. Its identity list, reports and backups are private and not build inputs. A zero-match source scan is not a guarantee that local history, binaries, documents or older backups contain no personal information.

## 4. Architecture and data flow

1. editor/cpp/scene.hpp and scene_json.cpp define authored scene data and persistence.
2. commands, command_runner, interactions, geometry and mesh code edit that data; editor_main.cpp presents the native Windows interface.
3. adventure_controller, gameplay_rules, object_behaviors and related headers implement runtime gameplay. native_session owns the rendering/simulation session and lifecycle.
4. deep/Physics integrates Jolt bodies, contacts and constraints. Rendering interpolates motion between simulation steps; physics correctness must not depend on display FPS.
5. render_scene prepares data for editor/native/viewport.cpp. deep/engine implements renderer resources, workers, model/texture processing, shaders and optional reconstruction paths.
6. game_export.cpp packages scenes and dependencies for Starlight.NativePlayer. deep/src/runtime.cpp hosts the separate native project modules. These are distinct execution paths with distinct tests.
7. tools/audit coordinates builds, runtime stages, feature evidence, comparison reports and retention. The standalone GUI launches the runner; an embedded editor Analysis panel was only proposed and is not implemented.

## 5. Build, launch and verify

Prerequisites: Windows x64, Visual Studio C++ toolchain, CMake 3.25+, Python, Windows SDK/DXC, pinned Jolt sources, and an installed NGX SDK for the renderer. Main editor code uses C++20; some native project targets use C++17. The development tree currently resolves an external SDK from the predecessor project; the publication build requires an explicit SDK location. Consult CMakeLists.txt rather than copying licensed SDK files.

From the checkout root:

```text
python Build-Native.py --config Release --build-dir out/classic
python Build-Native.py --config Release --build-dir out/classic --test
python Build-Native.py --config Release --build-dir out/classic --compact
python tools/build/build_deep_analysis.py --build-dir out/analysis-tool
```

Launch-Starlight-Editor.cmd selects the installed classic Release editor. Launch-Starlight-Analysis.cmd opens the chooser without starting tests. Use its Run both option for the complete standard analysis. Visible tests need foreground access. Other applications can interfere, but that is not evidence of a particular failure's cause. Do not treat background FPS as a performance result.

Build-Native.py at root is a forwarding shim; edit tools/build/Build-Native.py. Omitting build-dir selects out/native, not necessarily the editor shortcut's binaries. Build products are generated and may change directory size.

Useful focused checks:

```text
python tools/audit/test_audit_engine.py
python -m unittest discover -s tools/audit -p test_artifact_cleanup.py
python tools/audit/test_deep_analysis.py
```

Several Python check scripts use a main function rather than unittest discovery. A zero-test discovery result is not a pass. CMakeLists.txt is authoritative for compiled targets and CTest registration. See TOOL-CATALOG.md for the rest of the tools.

## 6. Last full test result and exact open failures

The latest full run is analysis/run-20260929-213020-5404-8804937. Ten of thirteen stages passed. Its audit-summary.json, audit-findings.json and runtime-evidence.json are retained. This document does not claim a subsequent full pass.

1. Editor controls: the internal warmup/measured UI cycles exceeded their stage deadline. Root cause is not established. Inspect tools/checks/classic_ui_checks.inl and the editor test path. Do not increase the deadline merely to manufacture a pass.
2. Fullscreen telemetry visibility: the same named check failed before cleanup. An earlier recorded result showed visible, nonempty FPS text but failed the unobstructed check. Inspect tools/checks/visible_ui_checks.inl, telemetryVisible and fullscreen layout/hit testing. This does not establish a broken timing measurement or a renderer crash.
3. Denoiser object motion: the moving prop did not produce the expected guide-buffer motion magnitude. Inspect tools/render/check_denoise.py, transform identity/history and viewport submission/readback. Do not remove the motion assertion or assume all physics motion is broken.

Earlier post-quarantine results passed 11/13 with only the latter two failures. Later focused reporting/cleanup checks passed, including five current cleanup tests and the analyzer process/report suite. A real audit-contract target rebuilt after compaction and ran before and after cleanup. Earlier core/export runs passed 1,184 and 12 checks respectively; those are dated checks, not a newly executed full run.

## 7. What changed during this cleanup pass

1. Consolidated current docs, analysis tooling/reports and the publication checkout under the engine workspace, with root launchers and corrected active paths.
2. Removed owner-approved obsolete backups, experiments, duplicate captures and generated intermediates. Quarantine was manually deleted. Written summaries remain; some historical raw evidence paths deliberately no longer exist.
3. Added optional compact builds. Only OBJ/PCH/ILK intermediates in a validated build tree are removed after successful requested build/tests. Executables, DLLs, libraries, symbols and assets remain.
4. Added compact analysis completion: passed/failed/unavailable stage counts, short failure explanations, summary retention and cleanup warnings. Default in-workspace runs remove newly generated captures and temporary packages after reporting, even on failures. Preexisting files are protected. External output directories are not compacted automatically; --keep-artifacts preserves detailed raw output.
5. Updated the Analysis Tool icon with the approved large outlined question mark. Editor branding is retained.
6. Moved historical renderer notes and a release-note template into documentation. Technical explanations and licenses remain inline. Non-comment renderer content was compared for equality; the historical template text was preserved exactly.
7. Replaced specified identities in current scanned source, pushed publication changes and rewrote published history. Upstream credits and the external private development history were not rewritten.
8. The measured workspace reached about 3.77 GiB before subsequent Git/documentation activity. The owner-created archive passed 7-Zip integrity and matched 26,125 file paths/sizes at that checkpoint. Newer archives require their own checks; old figures are not current inventory guarantees.

## 8. What remains and suggested order when authorized

1. Reproduce and resolve the three full-audit failures without weakening their assertions. Retain a compact report with exact stage outcomes.
2. Continue the mesh, texture and material quality pass: convincing rocks/grass and real textured assets rather than assuming loader support means finished visuals.
3. Return to measured frame pacing, especially 1% and 0.1% lows, after functional work. Compare fixed scene, resolution, settings and foreground conditions, retain startup and steady-state distinctions, and measure CPU/GPU costs.
4. Broader physics fidelity extensions require explicit scope. Current bounded physics is not cloth, ragdolls, deformable soft bodies, unrestricted fluid simulation or arbitrary mesh fracture.
5. Step 4B abilities, stats and traits remains separate pending scope. Existing fields or UI scaffolding do not prove a complete system.

## 9. Rules that prevent another confusing handoff

Keep one current starting point. Update this document and the folder map when ownership or paths change. Put long history in note files, keep code-relevant constraints beside code, retain third-party licensing and never edit immutable report snapshots to make history look cleaner. Do not modify Starbus, Claude Code or the separate predecessor/model project as part of Starlight work. Preserve the external model assets and SDK dependency until a verified replacement exists.

The owner prefers concise plain-English outcomes and clear measured progress. Do not invent percentages or universal completion claims. Fullscreen gameplay verification has used 2560x1440 with telemetry enabled. A successful build, a focused test and a full analysis are different evidence levels. Report each accurately. A new source change does not automatically update installed binaries or an existing backup.

## 10. Direct source and evidence links

- [Folder map](../../../../WORKSPACE-STRUCTURE.md)
- [Build and test registration](../../../../CMakeLists.txt)
- [Scene schema](../../../../editor/cpp/scene.hpp)
- [Player controller](../../../../editor/cpp/adventure_controller.cpp)
- [Physics world](../../../../deep/Physics/PhysicsWorld.cpp)
- [Viewport bridge](../../../../editor/native/viewport.cpp)
- [Audit coordinator](../../../../tools/audit/audit_engine.py)
- [Compaction implementation](../../../../tools/audit/artifact_cleanup.py)
- [Telemetry visibility check](../../../../tools/checks/visible_ui_checks.inl)
- [Denoiser check](../../../../tools/render/check_denoise.py)
