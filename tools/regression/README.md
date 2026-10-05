# Fix ledger and failure packets (engine side, version 1)

Design status: report-only, run after builds. No boot hook, no watcher, no automatic repair. Plug-in and bridge delivery belong to the Starlight.AI workstream; this folder defines only what the engine side produces and verifies.

## Files
- `ledger.json`: one entry per CONFIRMED fix (committed). Unconfirmed audit leads do not belong here. No states are stored here.
- `run_ledger.py`: runs `Starlight.NativeCoreChecks.exe`, maps each entry's tests to checks by name token (for example `CC-01:`), and writes per-entry states for the exact binaries that ran.
- `run_ledger.py --self-test`: exercises every state and notice type without running the engine.
- Results (not committed): `D:\projects\_recovery\regression-results\` with `latest.json`, one file per build identity, `notices.log` (transitions only) and `runs\` (suite output, `progress.log`).

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
