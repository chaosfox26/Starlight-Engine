# Cleanup status — 29 September 2026

## Latest verified state (supersedes historical quarantine notes below)

1. ChaosFox manually deleted quarantine; absence verified. Full post-deletion Run both completed in `analysis/post-quarantine-full-20260929`: 11/13 stages passed, with the same pre-existing fullscreen telemetry and dynamic-object motion-vector failures. Census checked 7,918 stable files with no scan issues or tree changes. This is bounded verification, not universal certification.
2. With explicit approval, removed 3,613 old analysis BMP screenshots (33.17 GiB), preserving written reports and the fresh run. Historical report screenshot links now point to intentionally removed captures. Manifest: `analysis/cleanup-review-20260929/removed-screenshot-manifest.json`.
3. Removed another 2,395 generated files (4.28 GiB): older BMP captures under editor/checks and audit-runs, plus compiler OBJ/PCH/ILK files under out and publication/out. No tracked files in these categories were found. Required scene packets, comparison fixtures, model assets, source, executable binaries, libraries, and written reports remain. Manifest: `analysis/cleanup-review-20260929/generated-files-removal.json`.
4. Entire workspace measured 6.51 GiB immediately after this second removal, including analysis and publication. Fresh checks add small outputs. Future compilation regenerates compiler intermediates; this is not a permanent size ceiling.
5. After the second removal, all 1,184 native core checks and all 12 export checks passed, including packaged rendering and unchanged-binary verification. The full suite above preceded this second generated-file cleanup; it was not rerun afterward.
6. Remaining follow-up: audit active paths versus historical references, and reconcile current handoff status. No fixes to the two known rendering failures or modifications to Starbus were made.

## Historical cleanup sequence

## Latest full verification of cleanup automation and icon

GUI Run both `analysis/run-20260929-213020-5404-8804937` completed: 10/13 stages passed. The prior fullscreen live telemetry and dynamic motion-vector failures persist. One additional failure occurred: native-classic-ui did not complete all warmup/measured cycles within its stage deadline (process returned 1 after 222.13 seconds). Cause is not established; concurrent desktop work was reported, but is not proven responsible. Do not describe this run as all passing or as only the two old failures.

The census covered 6,008 stable files, reading 1,689,937,241 bytes, with no scan issues or tree changes. This excludes analysis/publication and is not the whole workspace size (4.67 GiB measured after the run/build).

Cleanup automation verification: three focused tests passed; additional isolated checks confirmed cleanup skips failed builds and failed requested tests and keeps the default newest three completed runs. A real audit-contract target regenerated after compaction, passed 13 checks, was compacted again, and passed again. The full compact editor build passed, followed by 1,184 core and 12 export checks. GUI snapshot included artifact_cleanup.py. Its final capture-retention.json contained zero candidates and no warning: current evidence stayed intact; automatic deletion behavior was exercised with temporary test data, not inferred from that empty live record.

The new Analysis Tool icon and cleanup automation remain local pending a subsequent publication update. This full result does not certify the editor's missed timing deadline as resolved.

## Earlier sequence (continued)

Further reduction after archive verification: removed 533 byte-identical older binary/texture test captures (1,063.56 MiB), retaining and hash-verifying an identical copy of each. Protected latest full run, recent failure baselines and cleanup checks remain complete. Mapping: analysis/cleanup-review-20260929/duplicate-binary-captures-removal.json. Also removed 116 generated LIB/PDB files (1,022,888,126 bytes) from other build configurations, preserving installed classic/Release products and project output. Older debugging symbols require regeneration; subsequent builds regenerate removed link products. No source/runtime binaries were removed in this pass. All 1,184 core and 12 export checks passed afterward, including packaged rendering. The owner-created Starlight-Engine.7z remains untouched and represents the earlier verified 5.95 GiB checkpoint, not this newer reduced layout.

Active-path review: regenerated all three relocated publication CMake caches (classic, audit-engine, verification) successfully with their current root and the existing external NGX SDK. Corrected the analyzer GUI build command in its README and obsolete quarantine/verification wording in root README and CLAUDE.md. Desktop editor/analyzer shortcut targets exist; launcher and build entry points use the current root. No old Starlight-Publish/Starlight-analysis root references were found in the scanned current Python/C++/header/command/PowerShell/CMake tool and editor source. Historical relocation manifests and sealed reports intentionally retain original paths. Both analyzer test scripts passed. No full runtime rerun was needed for these documentation/cache changes; publication compilation was not rerun.

Publication build cache: removed generated .lib files only beneath publication/out, with a file manifest in analysis/cleanup-review-20260929/publication-generated-libraries-removal.json. These are build/link products, not runtime DLLs or source dependencies; the next publication build regenerates them. Publication Git status stayed clean. Current installed development binaries were untouched.

Test-output retention pass: removed 519 byte-identical duplicate generated native-scene.bin packets and exported game EXE/DLL copies from older analysis runs, reclaiming 449.91 MiB. Each removed file maps to a retained identical file and SHA-256 in analysis/cleanup-review-20260929/duplicate-test-output-removal.json; all mappings were verified after deletion. Latest full run, cleanup verification, recent failure baselines, written reports, unique outputs, required fixtures and test source were preserved. Historical export directories with removed binaries are evidence only, not complete runnable packages; use the manifest to locate retained copies or regenerate the export.

1. Current workspace, analysis files, Analysis Tool and sanitized publication checkout are together under Starlight-Engine. Launchers and the desktop Analysis shortcut point to the new layout.
2. Five obsolete locations were moved into _quarantine/20260929: deep/automation, deep/build, deep-incomplete-first, build-native and the old editor-launcher backup. The move manifest records original paths and verified file counts/bytes. No files were deleted.
3. Five more legacy root folders (bin, src, evidence, output, tmp) were moved into _quarantine/20260929/legacy-root. These are not in the current build graph. Their restore map is preserved.
4. The duplicate dated handoff is now Starlight Documents/Current/history/handoff-20260927. Sixteen old documents are grouped under history/previous-documents. Current handoff links were updated.
5. Compact automation history contains 164 records, 565,587 original bytes, each verified by SHA-256. It preserves decisions and summary evidence without another engine source copy.
6. Owner removed the failed 47 GB multipart-backup staging folder; absence was verified. No cloud files were changed.
7. After the first quarantine move: current Release configure/build passed; 1,184 native core checks passed; all 12 export checks passed, including packaged validation and 60 rendered frames; editor UI report has 114 checks and zero failures. Analyzer unit checks and internal-output/exclusion regressions passed. These are cleanup compatibility checks, not performance or clean-machine certification.
8. Additional legacy-root moves require the final verification recorded below. No blanket completion claim for the broader cleanup or the two old runtime failures.
9. Remaining large material: analysis/git-safety-20260926-160622 is an 11.49 GB old backup; two task1 diagnostic runs total roughly 23.6 GB of profiling captures. They remain intact. Quarantine also remains intact pending owner decision after verification.
10. Publication caches and historical helper scripts are not current engine entry points. Regenerate old caches and review historic scripts before using them. All current working entry points are listed in WORKSPACE-STRUCTURE.md.

Final legacy-root verification: 1,184 core checks and all 12 export checks passed again with both quarantine batches absent from their original paths. This supports retiring the listed batches; it is not authorization to remove unrelated folders.

Further cleanup: unused deep/tools experiment scripts, deep/navigation index/toolchain and deep/evidence were preserved under _quarantine/20260929/legacy-deep. Active tools remain in tools/. Selected handoff evidence was moved from root docs/ into the canonical current handoff; the empty old docs layout was quarantined.

Final source-layout configure/build passed, followed by 1,184 passing core checks after the legacy-deep move. Native-policy scan also passed with zero violations (two exact historical C# comparison fixtures remain intentionally identified). No permanent quarantine deletion was performed: the previous automatic approval block remains unresolved.

The final-layout export rerun passed all 12 checks, including packaged rendering. All cleanup changes remain local; no GitHub push was performed during this pass.

Historical evidence review: 70 compact records were copied and hash-verified under analysis/cleanup-review-20260929/retained-summaries. The old backup Git head is an ancestor of current HEAD; its bundle was also retained. The two large trace folders and old git-safety backup are now in _quarantine/20260929/historical-analysis with a restore manifest. Raw traces remain available there pending removal; retaining summaries does not preserve full trace re-analysis capability.
Full Run both launched via the Analysis Tool on 29 September 2026: run-20260929-200940-28068-3964906. Results pending; do not claim full acceptance from earlier bounded checks.

## Full analysis result

Run run-20260929-200940-28068-3964906 completed in both mode: 11 of 13 stages passed. Build, core, editor UI, exports, textures, diagnostics, native policy, session and renderer fallbacks passed. Two stages failed at the same named assertions recorded before cleanup on 27 September: Fullscreen live telemetry and dynamic transform produces object motion vectors. Their recurrence does not prove every possible cleanup regression absent; full acceptance remains open. No new missing-path failure was reported in the stage findings.

The census read 6,543,042,895 bytes and recorded no scan issues. This is the included engine scan, not total folder disk usage: analysis, publication and _quarantine were excluded explicitly. Quarantined data still consumes disk space until removed. Latest evidence is analysis/run-20260929-200940-28068-3964906/audit-summary.json and audit-findings.json.

Owner confirms a separate full pre-cleanup Starlight Engine archive exists on Google Drive (approximately 56–59 GB, exact size not verified). No restore test was performed here; no Drive upload or deletion was performed during this pass.
