# Starlight continuation — 2026-09-18

## Session stopping point — 18 September 2026

### Future organization task — Chaos's closing note

### File origins and possible project mixing — Chaos's additional closing context

Chaos reports that earlier sorting across successive projects involved moving, retaining and deleting files, and she is concerned some projects may have been mixed accidentally. This is a concern to investigate, not a confirmed finding. Folder placement and filenames alone must not be treated as proof of project ownership.


When this deferred organization task is explicitly resumed, begin with a read-only inventory and provenance map: repository history/remotes, file hashes and backup versions, license/copyright headers and notices, dependency/build references, and relevant saved records. Distinguish project-specific code, shared components, third-party material, historical copies and unresolved origins. Mark uncertain files for review in the inventory rather than guessing, moving or deleting them. Preserve evidence and backups before proposing any reorganization. Any licensing conclusions need separate source-grounded review of the actual material; this note makes no legal determination.

This remains future work. No broad scan, cleanup, deletion, license change or project relocation was requested for this session close.



Project history spans multiple chats, breakdowns, documents, folders and backed-up copies of Starlight Engine. Do not assume the current repository or this conversation contains the entire history, or that every discovered engine copy is active.

Chaos wants a future consolidation into one distinct home for **Starlight Engine notes** and another for **SampleGame notes and project material**. Record this as deferred organization work, not an instruction to start moving files during the session close.

Before consolidation, inventory the relevant locations and existing indexes; distinguish current authoritative material, historical/superseded notes, duplicate copies, source trees and intentional backups. Preserve provenance and rollback copies. Use a manifest/index and preserve or repair references so the handoffs, tools and project paths continue to work. Do not merge engine and game scope, delete backups as mere duplicates, or relocate working code/assets just because documentation is being organized. Choose destination folders and migration scope when the task is resumed.



Chaos asked to save context and is ending this session to conserve usage. She plans to return on **22 September 2026**, when she expects her limits to refresh; this is her stated plan, not a verified account reset time. No reminder, scheduled task or automatic resumption was requested.

**Resume by reading the latest handoff and asking/following what Chaos wants to do next.** Do not restart audits, optimization, engine implementation or asset generation automatically. Current engine readiness still comes from dated saved evidence; documentation does not resolve prior runtime failures.

Preserve the central direction: **Starlight is the broader general-purpose engine; SampleGame is a separate game powered by it; the opening slice showcases the engine without defining its limits.**

Saved results:
- Engine knowledge and research entry point: D:/projects/Starlight-Engine/STARLIGHT-KNOWLEDGE.md
- Portable project context: D:/projects/Starlight-Engine/STARLIGHT-SAMPLEGAME-CHAT-HANDOFF.md
- Full SampleGame opening plan, original clarifications and Codex connections: D:/SampleGame/Design/first-scene-brainstorm-notes.md
- Shareable second-person opening: D:/SampleGame/Design/SampleGame-The-Shattered-World-Opening-Readthrough.txt

The latest narrative ends with the reality-fracturing clash, **SampleGame, the Shattered World**, then a fade to black and **Powered by the Starlight Engine**. Chaos approved how the read-through expressed her vision. The intended first environment is selected; detailed gameplay, branching, assets, physics and VR presentation still contain explicitly marked open decisions. The light-faction name heard as “LightOrder” is not resolved against the Codex's LightFaction. No production implementation was performed during this scene-planning portion.



## Latest direction — engine scope and SampleGame planning, 18 September 2026

**Starlight is a general-purpose engine. SampleGame is a separate game project powered by it. The SampleGame nightclub slice is a showcase and a useful development target, not the engine's entire purpose, roadmap or completion criterion.** Chaos explicitly requires this distinction to survive future chats. Building reusable engine features that help the slice is appropriate; silently narrowing the engine to the slice or claiming scene-specific work completes a general engine system is not.

This update records documentation and planning work. It does not resume paused optimization, authorize implementation, or assert that the complete engine must precede every game milestone. Keep engine and game milestone scope/estimates distinct. The older implementation/audit details below are dated evidence, not freshly reverified state.

Current entry points from this conversation:

- [Portable high-level handoff](STARLIGHT-SAMPLEGAME-CHAT-HANDOFF.md): self-contained engine/game distinction, broader proposed direction, slice summary and evidence limits; attach this companion when moving to a chat without local file access.
- [Engine knowledge entry point](STARLIGHT-KNOWLEDGE.md): reusable feature/workflow understanding from the completed analysis, with current direction and research links. The latest pack is D:/projects/Starlight-analysis/knowledge-20260918-v3; read its freshness limits rather than repeating a full source dive.
- [Landscape reality check](D:/projects/Starlight-analysis/research-20260918/REALITY-CHECK.md): 58 comparison areas and 63 primary sources. Strong precedents exist; no uniqueness or performance-superiority claim was established.
- [SampleGame opening plan](D:/SampleGame/Design/first-scene-brainstorm-notes.md): consolidated sequence, lore connections, future asset needs, open decisions and original spoken clarifications.
- [Shareable opening read-through](D:/SampleGame/Design/SampleGame-The-Shattered-World-Opening-Readthrough.txt): second-person narrative ending in the SampleGame title, then a fade to black and “Powered by the Starlight Engine.”

The opening's stand-and-fight path protects remaining patrons against an overwhelming commander, buying time for the light army. A reality-fracturing sword clash echoes the Codex's creation account. Fleeing has a proposed later path toward darkness; detailed branching rules remain open. SampleGame's Codex and game design remain separate from Starlight's engine documentation. Historical browser/Godot plans do not override the current Starlight-powered direction.

The current work is documentation only. Preserve existing code and known audit failures; do not interpret the scene's desired effects, VR ambitions or narrative as shipped functionality. Chaos's next explicit request determines what work resumes.

---

Companion conversation history: `CHAT-SUMMARY-2026-09-18.md` in this directory. Read it for the chronological requests, scope changes and distinction between earlier requested capabilities and verified implementation.

## Immediate purpose and user intent

The user requested a new chat with as much context as possible, including all optimization audits. This is a context handoff, not authorization to start an additional long benchmark or silently implement the entire optimization roadmap. Read the artifacts, establish the current state, briefly acknowledge readiness, and wait for the user's next instruction.

Workspace: `D:\projects\Starlight-Engine`. Windows / PowerShell; local Codex project is labeled **SampleGame**, but its saved root is `D:\projects`. Do not confuse the Starlight repository with sibling SampleGame projects. Preserve existing uncommitted changes.

Applicable parent instructions: `D:\projects\AGENTS.md` (also supplied in conversation), `_recovery\Chaos-HANDOFF.md`, `_recovery\ORIENTATION.md`, `_recovery\RECONNECT.md`, and the CURRENT portion of `STARBUS-HANDOFF.md`. The old bus is retired. Do not invoke `bus inbox` or revive old automation. Do not send peer messages merely to announce this handoff. Chaos, Starbus and other sibling projects are outside this engine task.

## User preferences and operating rules

- The user wants native C++, a simple classic interface and familiar controls. Avoid complexity or unsolicited redesign.
- Native-resolution rendering; no super resolution and no frame generation. Optional Ray Reconstruction at 1:1 was separately authorized; it is not the native wavelet denoiser.
- Cost matters. Use hardware for bulk scans and tests; keep conversation output concise. Do not repeat audits/planning as a substitute for implementation when implementation is requested.
- For long audits: compile the GUI audit tool, launch detached with a visible progress window, immediately yield. **Do not poll, wait for completion, or stream incremental audit logs.** The user will say “Completed”; then review files. Compilation before launching can be awaited normally.
- Never claim successful performance validation from failed, throttled or unmatched benchmarks. Do not promise zero leaks/races from bounded tests.
- Do not kill an existing editor or completed audit GUI to replace its binary. Use isolated build/output locations when necessary.
- No subagents unless the user or applicable instructions explicitly request delegation. No new goal or scheduler/automation was requested.

## Source control and current build

Local-only Git repository, no remote was configured in the earlier task.

- `43ae574793f31e53da1a0fb3d98857d9494ae06e`: source baseline.
- `a1cfe8bb4aac32d88225ec439f76fb379488c198`: classic native UI restoration; current HEAD as checked for this handoff.
- Jolt submodule: `deep/third_party/JoltPhysics`, commit `0373ec0dd762e4bc2f6acdb08371ee84fa23c6db`, upstream jrouwe/JoltPhysics.
- **Audit and diagnostic changes after the UI commit are uncommitted and contain known failures. Do not represent them as a validated release.**

Modified tracked files at handoff:

```text
.gitignore
CMakeLists.txt
deep/engine/dxr.h
deep/engine/render_device.h
editor/cpp/classic_ui_checks.inl
editor/cpp/editor_main.cpp
editor/cpp/native_session.cpp
editor/cpp/native_session.hpp
editor/cpp/session_checks.cpp
editor/native/standalone.cpp
editor/native/viewport.cpp
```

Untracked additions include `editor/cpp/audit_window_trace.hpp` and `tools/` (audit sources/scripts). The root `.gitignore` is restrictive: reports, including the optimization report, may exist locally without showing up in `git status`. This handoff is also a local file; do not rely only on tracked files for context.

Build command:

```powershell
python Build-Native.py --config Release --build-dir out/classic
```

Python orchestrates CMake/Ninja and Visual Studio's native compiler; it is not a managed engine dependency. Current native target is C++20, with shared renderer/project portions using C++17 where configured.

- Editor: `out/classic/Release/Starlight.NativeEditor.exe`.
- Current audit: `out/classic/Release/audit/starlight_audit_tool.exe`.
- Previous audit binary also remains at `out/classic/Release/starlight_audit_tool.exe`; **do not accidentally launch this stale version**.
- CMake now places the audit target under the `audit` subdirectory to avoid replacing an older running audit GUI. The tool finds native check binaries in the parent directory.
- Latest compilation log: `out/audit-patch-build.log`; Release built successfully, but runtime verification failed as detailed below.
- Native launcher: `Launch-Starlight-Editor.cmd`, normally `--show-terrain`.
- VS installation previously used: `C:\Program Files\Microsoft Visual Studio\18\Community`.

## Primary optimization deliverable — read this first

`optimization_audit_report.md` (about 47.8 KB, 5,627 words at completion).

It contains:

1. Ten prioritized bottlenecks.
2. Immediate bloat-removal candidates and boundaries that must not be blindly deleted.
3. Native layout evidence, GUI/hidden work, SIMD and ownership analysis.
4. A low-risk implementation checklist and acceptance criteria.
5. A dedicated 16-core / 32-thread concurrency section: scheduler limits, task graphs, CCD/cache/NUMA awareness, false-sharing candidates and multi-threaded command recording.

The automated scan covered 2,930 source/build/script files and 3,878,157 lines, with 35,630 lexical review sites. Only 89 files belong directly to the current engine/editor/physics/project source families; the remainder includes historical optimization source snapshots and toolchain files. This is **not** 2,930 live runtime files. Vendored/build/output directories were excluded by the scanner. Exact exclusions and hashes are in the manifest.

Supporting artifacts:

```text
tools/audit/optimization_scan.py
out/optimization-audit/manifest.json
out/optimization-audit/review-sites.json
out/optimization-audit/layouts.json
out/optimization-audit/layout_probe.cpp
out/optimization-audit/layout_probe.exe
out/optimization-audit/layout-build.log
out/optimization-audit/report-references.json
out/optimization-audit/concurrency-references.json
```

The native layout probe compiled and ran with MSVC x64 C++20 `/O2 /MD`. Measured shallow sizes: Vec3 24, Vertex 80, Mesh 48, MeshPart 80, Character 464, SceneData 240, TerrainData 64, TerrainFlow 92,432, FlowObstacleMask 120, RenderStatus 128, CameraState 56, SceneHistory 296, std::string 32 bytes; alignment 8 for these types. Vector/string heap data is additional.

### Most important static findings

- Physics/fluid activity triggers full scene packet regeneration about every 50 ms; unchanged terrain and imported GLB geometry are rebuilt/reparsed.
- Resize destroys/recreates the entire viewport instead of resizing just dependent resources.
- DXR builds a temporary geometry vector/map and performs matrix inversions and TLAS work even on static scenes.
- Mesh uploads hash full arrays, build temporary caches, use all-pairs removal checks and flush. The newest conservative retirement clears survivor BLAS too.
- Physics steps rebuild ID maps and pose/liquid vectors; obstacle masks reconstruct geometry even when reported body poses have not actually changed.
- World/local transforms repeatedly compute trigonometry per vertex; bounds/snapping reconstruct meshes. Cache transform and mesh revisions.
- Path duration and segment traversal repeat each frame.
- Edit preflight creates/discards a packet that the worker immediately rebuilds; refresh also recreates lists/controls.
- SampleGame project motion-history node maps clear/repopulate each frame. Distinguish this from its crowd SoA arrays, which already exist.
- DXR allocates 26 full-resolution images totaling 212 bytes/pixel (~1.64 GiB at 4K), before other renderer resources. Wavelet-only images 9–22 total ~1,012.5 MiB at 4K and could be deferred during RR-only use with safe fallback.
- Terrain's 97×97 data becomes a **193×193 preview mesh**, 37,249 vertices and 221,184 ground indices. Do not repeat the old assumption that rendered terrain is only 97×97.
- Safe cleanup candidates include unused `Viewport::error`, parser-only retained `Part::baseY` (keep wire bytes and validation), and lazy initialization of the unused bilinear backend in HDR-only editor mode.
- Native DLL exports are active ABI boundaries, not leftover C#/PInvoke/IPC. Do not delete them based on naming.

### Concurrency findings and constraints

- `PersistentWorkers` is a persistent native thread pool with static contiguous chunks, one submitter, caller participation, `notify_all` and a mutex-protected completion count. No first-party work stealing, fibers or task DAG exists.
- The SampleGame project defaults `poolThreads` to **physicalCoreCount**, so auto mode targets 16 participants, not 32. A shared 2,000-item serial cutoff and 512-item chunk minimum further limit participation.
- Gather/culling, pose evaluation and CPU instance-bucket construction already have parallel paths. That is **not** multi-threaded D3D12 command recording.
- One direct command list is recorded with per-frame allocators; CPU command recording and final submission are serial. Existing stage timings are not per-worker command recording profiles.
- Jolt has four workers; up to 36 terrain tile cooks can independently use `std::async`; multiple sessions add pools. Bound total useful concurrency before adding workers.
- No explicit first-party CPU-set/affinity/cache-domain placement was found. Only physical core counting is present. Discover actual topology; do not assume CCD equals NUMA or processor numbering encodes cache placement.
- The hardware brand string reported **AMD Ryzen 9 9950X3D2 16-Core Processor**, 32 logical processors, plus RTX 5090 and roughly 64 GB system RAM. User calls the CPU 9950X3D. Do not hard-code asymmetric cache assumptions based on either string.
- False sharing, cache misses, lock starvation, SMT speedups and affinity benefits remain measurement candidates. A contended shared completion counter is true sharing, not solved by alignment alone.
- Concurrency appendix specifies 64-byte-aligned independent worker records where justified, immutable/double-buffered staging, bounded queues, local-first stealing candidates, and worker/frame-slot-owned command lists with explicit resource transitions. No such roadmap refactor has been implemented yet.

## Audit executable and completed runs

Source: `tools/audit/audit.cpp`, a native Win32 GUI with progress bar/stage, background coordinator, 32 CPU workers for scan/stress, DXGI/CPU inventory and native child test processes. Owned child jobs are cancelled/terminated only on timeout or cancellation. GPU/UI suites run sequentially. Final report is `engine_audit_findings.md` inside a fresh `audit-runs/run-*` directory. The window remains open after completion.

The tool runs native core/diagnostic/texture/session/UI checks, legacy vs native describe/inspect CLI comparisons, and 4K renderer tiers 0–4. Archived renderer comparisons copy the old DLLs beside the current standalone harness in an isolated output directory. It does not edit the archive. The controller itself is native; executing the archived WPF CLI can load that archived managed runtime for comparison.

### Run 1 — `audit-runs/run-20260917-190948`

- Core 468 assertions passed, session 10 passed; diagnostics and textures passed.
- Classic UI 51/52 passed. Inventory gained one `SystemUserAdapterWindowClass`; USER 139→141 then plateaued; GDI 37 and tooltips 22 unchanged. Ownership was not proven.
- Forty scene replacement cycles: handles 397→398; private bytes +1,978,368 (~1.89 MiB), within the earlier broad tolerance. This was not proof of a leak.
- 32 CPU workers: 6,400 valid round trips, 819,200 counted heap allocations; no heap validation failure. 6,364 mutated inputs rejected, 36 accepted; accepted mutations are not necessarily invalid.
- Native CLI median describe ~21.93 ms vs legacy 150.40 ms; inspect ~29.27 ms vs 166.92 ms. Includes process startup/I/O/shutdown, not UI event latency.
- All render tests effectively ran at 20 FPS/50 ms because hidden presentation throttled them. They cannot substantiate maximum throughput.
- Harness denoiser argument `1` selected **RR**, not wavelet. Both RT arms used RR native 1:1, making the original “native denoiser” report description wrong.
- Reviewed report corrected those descriptions and marked unsampled fast-process memory counts unavailable rather than zero. Raw JSON remains intact. Original generated Markdown is saved as `engine_audit_findings.md.generated-original`.

### Changes made for Run 2 — present in dirty worktree

- `RenderDevice::auditOffscreen` skips frame pacing, frame-latency wait and Present; **it also rotates swapchain backbuffer index manually. This implementation failed and needs correction.**
- Optional viewport exports `sl_benchmark`, `sl_benchmark_flush`, `sl_scene_resources` support the harness and resource diagnostics.
- Standalone harness dynamically looks up new benchmark exports, requests denoiser `0`, captures P50/P95/P99 and drains GPU work at measurement completion. Legacy DLL has no offscreen ABI, so the harness tried focused presentation instead; this fallback also failed unattended.
- Keep denoiser ABI stable: **0 = native temporal wavelet, 1 = RR**. The fix is at callers; inverting viewport mapping would break persisted/editor behavior.
- `DxrPass::retireGeometry` flushes and releases BLAS/geometry/TLAS/scratch/history when removed meshes are found. It conservatively clears survivors, a performance issue documented above.
- Native session status gained scene revision, screenshot count, parts/BLAS/upload counts.
- Session checks wait for the screenshot, prime loaded/empty topology, then perform two 40-cycle load/unload batches with matched empty endpoints, memory samples and native own-process handle tables. Later playback tests run only if lifecycle assertions succeed.
- `audit_window_trace.hpp` uses CBT hooks on existing process threads, captures stack addresses/modules for the target helper class and resolves symbols afterward. Hooks do not cover newly spawned threads automatically.
- UI checks perform 24 additional warm-up cycles; retain strict before/after inventories, do not whitelist or destroy system windows.
- Unsampled child memory metrics are now null/unavailable; audit executable moved to `Release/audit`.

### Run 2 — `audit-runs/run-20260917-192850`

**Not a successful closeout.** Reviewed report and original generated Markdown are preserved alongside all raw results.

- Core, diagnostics, textures and all 12 CLI comparison invocations passed.
- Classic UI **52/52 passed**. USER stayed 142, GDI 37, tooltips 22 across the measured 80 cycles.
- The system-helper class did not occur in either measured inventory; its trace had **zero events, three hooked threads**. The earlier cause was not diagnosed by this run. Do not claim the trace established OS ownership.
- Scene lifecycle completed 80 load/unload cycles/160 swaps. `resourcesRetired=true`; empty endpoints consistently had parts 2, BLAS 0, uploads 0. Private bytes rose **20,480 (20 KiB)**.
- Handles **406→409**, exceeding the tightened threshold. Second-batch counts plateaued after iteration 4. Handle-table difference has seven additions and four removals; not three individually identified leaked objects. Numeric object type IDs are insufficient to attribute ownership.
- Session suite failed `Matched unload endpoints plateau across two forty-cycle batches`. Later playback/preset checks were not reached. Do not broaden tolerances to make this green without diagnosis.
- **All ten new renderer runs failed:** `Native graphics device lost`, exit 7, before valid performance results.
- **All ten old renderer runs failed:** `Stable foreground focus unavailable`, exit 9.
- A leading source-level suspect: `deep/engine/render_device.h` manually advances `frame` without Present while still copying output into `targets[frame]` (swapchain buffers). A correct offscreen path should own independent output textures. No exact debug-layer/DRED cause was captured, so label this hypothesis rather than proven driver diagnosis.
- Native wavelet mode 0 was requested, but operation/performance was not validated because rendering failed.
- No valid FPS or denoiser-performance measurements exist from this run. No new run was launched after reviewing it.

## Earlier classic-interface restoration — preserve

Committed restoration introduced:

- Separate native windowed game preview with independent session/camera; Play/Pause/Reset sync. Hidden close parks/reuses renderer and HWND; destructor joins worker before HWND destruction.
- Full 20-entry More catalogue and dedicated native local music panel.
- Classic project/preset modal and named floor colors.
- Bounded dock overflow/scrolling and starter drag placement with snapping.
- Escape priority: cancel active drag/pour/menu/form/modal before leaving fullscreen.

Main modules: `editor/cpp/game_preview.hpp`, `classic_completion.inl`, `editor_main.cpp`, `editor_style.hpp`, `music.hpp`, `native_session.cpp`, `classic_ui_checks.inl`, `classic_completion_checks.inl`.

Before later audit modifications, Debug and Release both passed six CTest suites and 52 UI checks. Release UI lifetime GDI 37→37, tooltip 22→22, USER 160→160, equal batch peaks. These are historical validations of the committed state, not validation of today's dirty renderer modifications.

Historical supporting files:

```text
migration/OLD-INTERFACE-SPECIFICATION.md
migration/build_interface_reference.py (verify exact location if absent)
migration/classic-completion-validation.json
migration/classic-completion.diff
migration/completion-debug-build.log
migration/completion-release-build.log
migration/completion-debug-final-compile.log
migration/CLASSIC-RESTORATION-EXECUTION.md (older/stale relative to closeout)
migration/COMPETITIVE-GAP-ANALYSIS.md
migration/TEXTURE-PIPELINE-AUDIT.md
migration/NATIVE-MIGRATION.md
editor/AUDIT-2026-09-17.md
out/classic/classic-ui-Debug.json
out/classic/classic-ui-Release.json
```

Archived managed editor and visual references: `D:\projects\_recovery\starlight-managed-retired-20260917\editor`, with runnable old executable under `bin\audit-release\Starlight.Editor.exe`. Read/copy only; do not modify the archive or restore managed runtime dependencies to the active engine.

## Architecture landmarks

- `CMakeLists.txt`, `Build-Native.py`: consolidated native build and checks.
- `editor/cpp/scene.*`, `scene_json.cpp`: schemas, serialization, validation.
- `render_scene.cpp`: authoring geometry → binary native packet.
- `native_session.*`: worker-owned rendering/physics/flow and coalesced UI input.
- `editor/native/viewport.cpp`: D3D12 viewport C ABI and part/scene ingestion.
- `deep/engine/render_device.h`: shared renderer, frame buffers/fences, presentation, crowd machinery.
- `editor/native/prepare_renderer.py`, `cmake_shaders.py`: generate editor renderer/shader headers under build output.
- `deep/engine/dxr.h`, `dxr.hlsl`, `svgf.hlsl`: inline RayQuery `cs_6_5`, tiers 0–4 and native wavelet denoising. No RT PSOs/shader tables.
- `ray_reconstruction.h`: optional native-resolution RR integration; do not conflate with spatial upscaling.
- `deep/Physics/PhysicsWorld.cpp`, `editor/cpp/physics.cpp`: Jolt fixed-step physics/collision cooking and managed-free wrapper.
- `flow.*`: 48×48 fluid grid, obstacle masks; not a fully volumetric fluid solver.
- `deep/engine/persistent_workers.h`: current persistent pool.
- `deep/projects/SampleGame/project.cpp`: project crowd gather/pose/emission; distinguish from native editor scene rendering.

## Suggested next action only when the user asks to implement

Use the optimization report's checklist. Restore trustworthy measurement before claiming gains. Prioritize independent offscreen output resources and actual DRED evidence, lifecycle handle ownership, and scheduler/recording instrumentation. Then remove repeated work incrementally. Preserve the classic UI and native ABI; do not hide failed assertions, silently change denoiser settings, or rebuild the architecture wholesale without a concrete measured benefit.

At handoff, no broad optimization refactors have been implemented from the report. Only the earlier audit/harness/lifetime changes listed above were made. The static and multi-core reports are the latest completed deliverables.
