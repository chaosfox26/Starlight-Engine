# Full analysis result — 27 September 2026

User ran **Run both** in the Starlight Analysis Tool. Evidence directory: `run-20260927-145737-11584-42661687` under the local Starlight-analysis folder.

**11 of 13 stages passed; two failed.** Completion is not an all-pass result.

- Native render/lifecycle/visible stage: `Fullscreen live telemetry` failed its behavior assertion. Foreground remained true, screenshots succeeded, 459 new frames rendered and no renderer error was reported. This was not recorded as focus loss.
- Denoiser quality: `dynamic transform produces object motion vectors` failed.

The build, audit contract, core, diagnostics, managed-code checks/policy, textures, classic UI, export, session lifecycle and renderer fallbacks passed. These results do not erase the separately passing bounded physics closeout fixtures, but overall full analysis acceptance is open. Investigate these two failures before claiming the complete standard audit passes.

The feature report also falsely marked dynamic-world source support incomplete because a required documentation marker was searched only in the source index. The detector now relies on the actual source markers; the historical report is preserved unchanged. This reporting fix does not change either runtime failure and has not been substituted for a rerun.

No new performance or clean-Windows certification is claimed. Full local evidence and captures remain outside Git; this summary is the portable collaborator handoff.
