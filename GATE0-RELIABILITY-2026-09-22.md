# Gate 0 reliability — investigation checkpoint, 22 September 2026

## Latest: integrated visible audit, 23 September 2026

**Direct editor observation:** Opened the normal `out/classic/Release/Starlight.NativeEditor.exe`, loaded `migration/classic-character-check.starscene` through its Open dialog, and captured the maximized editor using computer-use screenshots. The character visibly rendered; the settled status bar read 66 FPS, GPU 0.35 ms, native resolution. Earlier grid captures read 64 FPS (0.13 ms windowed; 0.34 ms maximized). This corroborates the user's roughly 64 FPS editor observation, independently of the standalone test. The current source render loop has an eight-millisecond frame-start deadline wait, but exact cause in the shipped classic binary remains unproven. No matched old C# editor measurement was taken. No scene edits were saved and no performance settings were changed. Editor remains open for inspection.

**Validated automatic run:** `audit-runs/gate0-auto-fullscreen-20260923-003541` completed both bounded stages with zero failed checks. All 14 editor menu/control steps reported expected behavior, foreground ownership, successful captures and no renderer errors. The render stage reports borderless full-screen 2560×1440, matching internal render dimensions, native contract verified, no upscaling/frame generation, tier 1 native-wavelet, debug disabled. It measured 30.0106 seconds and 10,797 foreground frames out of 10,797, 359.77 FPS throughput and 2.4073 ms average GPU time. This is one frozen diagnostic scene, not a representative game benchmark or long-soak certification. The three-handle lifecycle issue remains unresolved. The pending statements below are superseded for this bounded automatic run only.

**Superseding user correction:** The initial integrated walkthrough timed out before any menu checks because it required manual input. That is an audit startup problem, not an observed engine crash. The replacement in `out/gate0-auto` removes manual start and manual close from the integrated run. Editor checks start after stable foreground verification. The rendering stage uses a borderless full-screen primary-display window at that display's native resolution, requires at least 2560×1440, and measures 30 seconds automatically. Unsupported resolution or unavailable/lost focus is an environment-precondition failure, never a pass. No 720p substitute or upscaled output is accepted. The previous 720p interactive configuration described below is historical. The prior 5.612 ms measurement was 4K offscreen, not 720p; it remains distinct from this visible test. Replacement end-to-end validation is pending.

The isolated `out/gate0-audit` build now includes a paced walkthrough in the real editor and a separate interactive visible rendering stage. Both are built into the audit program, including its focused `--gate0-visible` mode. The editor requires a click or Space before exercising 14 menu/panel/control checks in a scratch scene. It uses real editor command handlers and native controls; this is not hardware mouse-input coverage of every feature. Each step records foreground state, renderer status and a screenshot. The project-selection step captures its own window. Visible rendering then requires a separate click or Space, measures 30 seconds, and stays open for inspection until closed.

The final verdict separates bounded passes, failures and diagnostic controls, and explicitly leaves Gate 0 uncertified. Render evidence is checked for dimensions, native 1:1 contract, no frame generation, selected tier, presentation mode, active debug state, actual frames and GPU timing. All 13 evidence-validator regression checks passed after the build. End-to-end validation of the new integrated walkthrough is pending; compilation does not establish a visible audit pass.

Further isolation: physics-only remained at 124 handles for 45 seconds; renderer-only increased 397 to 400. Servicing the lifecycle host's Windows messages did not eliminate the increase: the pumped run increased 426 to 429. The unchanged assertion still fails. These results narrow the investigation but do not identify the exact owner or demonstrate a leak-free long soak.

The interactive rendering failure was a valid focus-loss rejection: ChaosFox confirmed clicking another window or using Alt-Tab. It is not evidence of a device-loss crash. Historical automatic foreground checks and offscreen timing remain separate from user-observed visible testing.

Remaining work: review integrated visible-run evidence, attribute the net three handles, establish memory/handle plateau over long soak, and obtain controlled comparable renderer measurements under the validated contract. The normal editor launcher remains unchanged. No broader engine capability is certified by this milestone.

## Update: completed first Gate 0 run and visible presentation check, 23 September

The generic audit/profiling window title was misleading. The completed run's `scope.json` and child list establish that only Gate 0 ran. Its initial hidden launch was corrected by showing the existing window. Future Gate 0 windows now have a specific reliability-diagnostics title.

- All five 4K native-wavelet tiers passed the offscreen debug run without recorded debug-layer errors. These were hidden GPU workloads, not visible presentation tests.
- Two matched 4K tier-1 offscreen measurements, debug disabled, completed at 175.31 and 177.11 FPS. The first reported 5.612 ms GPU time and saved a nonblank rendered scene, visually inspected. These are comparable repetitions of this current-renderer workload, not a legacy speedup, universal performance claim or full reliability certification.
- Both traced lifecycle and matched idle control failed the unchanged assertion: 395 → 398 handles. The control performs no scene replacements after warm-up, so replacements are not necessary to trigger the increase. Ownership remains unresolved.
- A standalone control with 36 `std::async` tasks, without engine/renderer/Jolt, remained at 71 handles throughout 45 seconds after those tasks completed. It does not reproduce the increase on its own.
- Following ChaosFox's concern about invisible rendering, a separate **visible native 1920×1080 tier-1 presentation check** completed 30.002 seconds and 12,767 measured frames, all foreground, exit 0, debug enabled, no device loss. GPU time averaged 1.369 ms. The saved report is `audit-runs/gate0-20260922/visible-render.json`; frame and debug log are adjacent. This bounded debug run is not a long-soak result or a comparison with the 4K offscreen workload.

Next: component-isolation diagnostics in `out/gate0-handles`, separating physics-only initialization from renderer lifecycle without the initial physics test. These modes are explicitly labelled diagnostic controls, not substitutes for full lifecycle acceptance. Original lifecycle assertions remain unchanged. Long-soak stability and no-handle-leak acceptance are still unproven.

The earlier checkpoint below is preserved as investigation history; its pending first-run statements are superseded by this update.

**Status: implementation and small reproduction complete for the benchmark defect; lifecycle attribution and broader validation pending. Gate 0 is not complete.** No general optimization roadmap was resumed. The regular editor launcher still points to the pre-existing classic build; changes are built in `out/gate0` for isolated validation.

## Confirmed benchmark defect

The original 17 September binary reproduced exit 7 with the saved `comparison.bin` at only 320×180, tier 0, native wavelet selected, with `STARLIGHT_D3D_DEBUG=1`. The debug log explicitly reports an attempted write to a swapchain buffer other than its current backbuffer, followed by device removal with `DXGI_ERROR_ACCESS_DENIED`. This is direct evidence against manual swapchain advancement without Present; it is not an inferred GPU-memory exhaustion diagnosis.

Evidence: `audit-runs/gate0-20260922/repro-before/debug.log` and `result.json`.

The fix allocates an independently owned same-size/same-format output texture for offscreen benchmark copies, preserves frame-slot fences, avoids presentation callbacks in offscreen mode, and obtains any swapchain index from DXGI rather than synthesizing it. Native ABI entry points remain compatible; optional `sl_benchmark_contract` reports version 2 so the harness does not silently trust the earlier broken bypass. No super resolution or frame generation was enabled.

With the fix, the same small workload completed five seconds of measurement and 14,018 measured frames with exit 0 and no debug-layer errors recorded. This is a small diagnostic reproduction, with validation instrumentation enabled, **not a 4K product performance claim or a legacy speedup**. Evidence: `repro-fixed.json` and `repro-fixed.log`. Later harness metadata additions were compiled afterward and require the queued validation.

DRED breadcrumbs and page-fault collection are now enabled when the existing debug environment switch is set. Device-removal reporting drains debug messages and records the removed HRESULT and available DRED summary. The confirmed pre-fix cause above came from debug-layer evidence; do not misdescribe it as a DRED page-fault finding.

## Lifecycle failure remains open

The unchanged lifecycle assertion reproduced the same net three-handle increase in isolated runs:

- `lifecycle-traced`: 438 → 441 handles; private memory +20,480 bytes; scene resources retired; assertion failed.
- `lifecycle-origins`: 418 → 421 handles; private memory +3,989,504 bytes; scene resources retired; assertion failed. This more heavily instrumented run includes handle-type queries and is not a clean performance measurement.

Final new handles include two events, two timers, two semaphores and a thread, while other earlier handles closed. The newly observed thread starts in `ntdll.dll`; a recorded creator thread starts in `ucrtbase.dll`. These observations suggest runtime/background ownership needs investigation; they do not establish a leak-free engine or prove the exact owner. Own-process handle tracing succeeded but returned empty stacks on this system, so stack attribution remains unavailable from that mechanism.

Added diagnostics preserve raw handle tables, batch tables, type names, thread entry modules, timestamps, and optional creation/closure records. No lifecycle thresholds or existing assertions were weakened. An explicitly labelled idle-control mode waits through equivalent frame intervals after the same warm-up without replacing scenes; it must never be counted as a scene-soak pass.

Handle-trace structure reference: https://github.com/winsiderss/phnt/blob/master/ntpsapi.h . Only the current process is queried; no foreign handle names or global verifier settings are collected or changed.

## Next evidence run

The existing native progress tool now accepts `--gate0` as its third argument after engine-root and output-directory arguments. This scoped mode runs traced 80-cycle lifecycle checks, a matched idle control, 4K debug checks for tiers 0–4, and two matched current-renderer tier-1 measurements with debug instrumentation disabled. It does not scan unrelated projects, run the full optimization audit, or assert parity with the legacy renderer's different presentation path. Child processes retain isolated job ownership and timeouts.

Output will be under `audit-runs/gate0-validation-20260922`. Review after ChaosFox reports completion, following her saved detached-audit workflow. Investigate lifecycle ownership before any warm-up or test-contract adjustment; a repeated failure remains a failure. Compare renderer contracts and debug logs, not just exit codes. Review whether further targeted fixes or longer soak validation are justified.

**Valid comparable legacy/new renderer measurements do not yet exist. Long-soak stability and no-handle-leak acceptance have not been demonstrated.** Repeated current-renderer measurements are pending and are a different claim.

## Preservation and continuation

Initial modified/untracked files and a binary Git diff were copied to `audit-runs/gate0-20260922/before`. Unrelated tracked modifications and `editor/cpp/audit_window_trace.hpp` were hash-checked against that copy and preserved. Production changes in this investigation are confined to the renderer benchmark/diagnostic path, standalone harness, session diagnostic instrumentation, and scoped audit runner. Builds succeeded in the isolated directory. `git diff --check` was run; no source assertions were removed.

Use this checkpoint alongside the dated knowledge pack and Gemini briefing. The 18 September feature catalogue and 22 September briefing are historical snapshots; they do not automatically incorporate this implementation. Starlight remains the general-purpose engine and GameProject a separate game/showcase.
