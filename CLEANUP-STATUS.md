# Cleanup status — 29 September 2026

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
