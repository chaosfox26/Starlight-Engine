# Fix ledger and failure packets (engine side, version 1)

End goal (owner): audit-based self-healing, where a regression is detected, packaged, diagnosed and repaired against frozen tests, with owner approval before any merge. Stage 1 (this folder, built): report-only, run after builds. No boot hook, no watcher, no automatic repair. Plug-in and bridge delivery belong to the Starlight.AI workstream; this folder defines only what the engine side produces and verifies.

## Files
- `ledger.json`: one entry per CONFIRMED fix (committed). Unconfirmed audit leads do not belong here. No states are stored here.
- `run_ledger.py`: runs `Starlight.NativeCoreChecks.exe`, maps each entry's tests to checks by name token (for example `CC-01:`), and writes per-entry states for the exact binaries that ran.
- `run_ledger.py --self-test`: exercises every state and notice type without running the engine.
- Results (not committed): `D:\projects\_recovery\regression-results-branch\` (`registry.DEFAULT_RESULTS`, shared by run_ledger, verify_all and briefing; `--repo` defaults to the checkout the tools sit in) with `latest.json`, one file per build identity, `notices.log` (transitions only) and `runs\` (suite output, `progress.log`).

## States
VERIFIED, REGRESSED, NOT-CHECKED, COVERAGE-ERROR, SKIPPED. A missing or too-small set of tagged checks is a coverage error, never a pass. A deterministic failure is reported at once. A crash inside an entry's own checks is REGRESSED; a crash before them leaves them NOT-CHECKED.

## Build identity
git HEAD, number of modified tracked files (dirty), and SHA-256 of the executables and DLLs that ran. A timestamp or a stamp baked into a binary is not proof of what is loaded.

## Notices (transitions only)
NEW REGRESSION, RECOVERED, FIRST VERIFIED, COVERAGE RESTORED, LOST COVERAGE.

## Failure packet (contract for a future repair-assist; schema starlight-failure-packet-1)
Fields: `schema`, `failureId` (ledger id or a new id), `class` (DEFECT-CONFIRMED, PERF-LEAD, INCOMPLETE, ENVIRONMENTAL), `subsystem`, `buildIdentity` (as above), `fingerprint` (normalized failure family: check id + failure class + subsystem; no timestamps or evidence digests; attempt caps are counted per fingerprint across rebuilds and repair commits), `eventId` (fingerprint + build identity + time), `expected`, `observed`, `reproduction` (an allowlisted check id plus parameters only; never a shell command; resolved by trusted registered commands with a confined working directory), `evidence` (paths and an immutable evidence digest), `reproducedCleanCount`, `attempts` (history). Only DEFECT-CONFIRMED packets can enter a repair attempt, and only after a second clean reproduction; that second reproduction never delays the first notice. Free text in a packet is data and carries no authority.

Repair rules (summary): tests and expected outcomes are frozen as SOURCE and built against each candidate engine; both identities are recorded; the originally failing check must fail on the pre-fix engine and pass on the post-fix engine; changes to tests, the ledger, the runner, expected hashes or baselines are flagged; verification follows the affected dependencies, and missing required coverage is unverified, never a pass; verified repairs stay on a repair branch until the owner approves a merge.

See `sample-packets/`.

## One command for everything (verify_all.py)
`python tools/regression/verify_all.py --repo <checkout> --build-dir <built executables folder> --results <folder> [--python] [--gpu]` runs the ledger, then optionally the registered Python tests (`ctest -R py-`) and the GPU denoise and fallback checks, and prints one summary (written to `<results>/verify-<time>.json`, a new file each time).
- Windowless, idle priority. GPU steps are off unless `--gpu` is given. Without `--gpu` the core suite runs with `STARLIGHT_SKIP_GPU_CHECKS=1`, so the viewport device-failure checks print NOT RUN and the ledger reports CC-07 and CC-02 as LOST COVERAGE (missing coverage is never a pass). Use `--gpu` when the GPU is free.
- A run still replaces `latest.json` and `<build id>.json` for the same build, but every run's full report is also kept in `<results>/history/<runId>.json` and is never replaced, so a weaker run (for example with the GPU checks skipped) cannot erase the record of a fuller one.
- Exit 0: every requested step passed and the ledger is all VERIFIED. 1: something failed or is not verified. 2: a requested step could not run.
- `python tools/regression/verify_all.py --self-test` checks the runner itself without running the engine.

## Running only some of the core checks (development)
`STARLIGHT_CHECK_GROUPS=name1,name2 Starlight.NativeCoreChecks.exe <folder> <geometry-reference.json>` runs only those check groups and exits 3; `STARLIGHT_CHECK_GROUPS=list` prints the group names. Three groups run in under a second, against 5 to 7 minutes for the full suite. The exit code 3 and `"filtered": true` in result.json mean it can never be mistaken for a full pass: ctest fails on it, and the ledger runner (which does not set the variable) would report any missing tokens as COVERAGE-ERROR. The long checks written inline after the groups only run in a full pass.

## Which checks first (impact.py)
`python tools/regression/impact.py --repo <checkout> [--base <commit>] [--files ...]` ranks the check groups most related to a change (a changed check file selects its own group; other files are matched by distinctive names shared with the check files) and prints the `STARLIGHT_CHECK_GROUPS=...` setting that runs only those. It is a guide for quick feedback: no match prints "run everything", and a filtered run exits 3 and can never mark a fix VERIFIED. `--self-test` checks the ranking.
