# Publication verification — 26 September 2026

Status: **not cleared for release; no remote and no upload**. This verification concerns the anonymized source snapshot, not completion of the movement/combat milestone.

## Verified

- Fresh CMake/Ninja Release build succeeded, including the editor, standalone player, renderer, physics, Empty module and renamed SampleGame module.
- The Analysis Tool GUI built from this snapshot and ran its combined audit against this snapshot's source, not the installed development editor.
- Core checks: the first run stopped at a missing triangle fixture; after restoring the required GLB fixtures, the same fresh build passed **924 checks**.
- All four Analysis Tool Python guard scripts passed when executed directly. They are standalone assertion scripts; generic unittest discovery finds zero tests and is not evidence of a pass.
- The renamed sample networking module built and its embedded mock-protocol checks passed with zero failures. No private/live server was contacted.
- Audit contract, diagnostics, no-managed-code checks and policy, textures, adventure export and denoiser-quality stages passed.
- Both recorded lifecycle workloads completed successfully. The enclosing rendering audit then stopped because a required saved scene packet had been omitted from Git.
- Both GLB fixtures and the rendering scene packet are now tracked in the publication snapshot and private development repository. Original development gameplay source was not modified.
- After restoring the scene packet, all **nine foreground rendering fallback cases passed** (five rendering tiers and four reconstruction/fallback cases). Reports confirm foreground=true and headless=false. This targeted rerun does not cover the complete Gate 0 sequence.
- Third-party JSON headers match upstream v3.11.3 exactly. cgltf matches upstream commit de9828bc6419064c302546313ce8ff5eac6cd703 exactly; its header's 1.15 label does not imply a match to the older v1.15 tag. Original license credits are retained.

## Unresolved verification findings

1. The editor's starter-character card expects a private model under projects/SampleGame/build/models. That model is deliberately not distributed here without confirmed rights. The UI test reports its absence. No private character artwork has been copied to conceal this failure.
2. The UI resource-cycle check exceeded its internal 180-second budget. Its deadline was not relaxed. The overall UI run returned failure; no claim of a clean UI pass is made.
3. The original combined audit remains a failed report; successful targeted reruns do not rewrite it. The full Gate 0 rendering/visible sequence has not been rerun after restoring its scene packet.
4. Clean-machine testing and previously deferred gameplay/performance work remain outstanding. The export check is a bounded rendering/content check, not proof of every interactive export behavior. Some existing audit stages are offscreen; they are not described as foreground acceptance.

## Licensing boundary

Third-party notices and a file inventory are supplied. A blanket MIT grant is **not active**: an older project note raised an unresolved code-origin concern, and confirmation or historical provenance is still needed. This is not a finding of copied copyleft code. Branding is excluded from the proposed code-only license. External SDK and runtime distribution requires their respective vendor terms.

## Evidence

Private local evidence, intentionally outside this publication repository:

- Starlight-analysis/run-20260926-162648-27756-174567718: full GUI-triggered audit, including original failures.
- Starlight-analysis/publish-build.log: first clean native build.
- Starlight-analysis/publish-core-rerun.log: 924-check corrected-fixture run.
- Starlight-analysis/publish-net-tests.log: sample protocol tests.
- Starlight-analysis/publish-visible-fallbacks: targeted foreground rendering matrix after fixture restoration.
- Starlight-analysis/publication-upstream-checks.json: dependency byte comparisons.

Reports and build directories may contain local machine paths and are not approved for upload. The original development Git history and private mapping files must not be pushed into this repository.

## Foreground reruns — 26 September 2026
The new rendering run at Starlight-analysis/verification-rerun-20260926/render completed: 0 failed checks, 10 bounded checks passed, 1 lifecycle control completed. Visible editor and focused presentation evidence were accepted. Internal debug/measurement stages retain their recorded offscreen modes. This is a successful bounded diagnostic run, not full Gate 0 certification (gate0Certified remains false).
The first UI rerun completed 112 checks with only the absent starter asset failing; the menu-resource deadline passed without changing its threshold. The owner then confirmed the starter model belongs to the project and authorized its restoration. The model is restored and staged; a second UI rerun, ui-restored-result.json, is now pending. Earlier missing-model and rendering-rerun status above is historical.

## Verified reruns complete — 26 September 2026
The restored-model UI rerun passed all 114 checks with zero failures in 148641 ms. Evidence: D:/projects/Starlight-analysis/verification-rerun-20260926/ui-restored-result.json. The rendering rerun completed with zero failures, ten bounded checks passed and one control completed; visible editor and presentation evidence accepted. Evidence: D:/projects/Starlight-analysis/verification-rerun-20260926/render/audit-verdict.json. These results resolve the requested UI and rendering reruns; they do not complete deferred gameplay, clean-machine certification, licensing, or off-machine backup. No upload performed.
