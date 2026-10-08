# Graphics runtime update manager, milestone 1 (local core)

Static library `starlight_graphics_runtime` (C++23, Windows, bcrypt only; no renderer, editor, network or vendor SDK dependency),
console tool `Starlight.GraphicsRuntime`, and headless checks in `tools/checks/graphics_runtime_checks.inl`
(run as part of `Starlight.NativeCoreChecks`). Test components are small generated fake files; no vendor binary is used.

## What exists
- Manifest (`set.json`): strict parse and validation (id, vendor, family, version, API family and major, SHA-256, signer, licence files,
  matched set, requirements marked `vendor` or `starlight-policy`, conflicts, channel).
- Package formats: a folder with `set.json` plus files, or a ZIP of the same layout (own STORE/DEFLATE reader with an own inflate;
  path, link, duplicate, ratio, size and entry-count guards).
- State store (`state.json`, atomic temp + backup + replace, versioned immutable `sets/<id>` folders, bundled read-only fallback set).
- Per family: active, previous, last known good, lock, pending trial, channel, vendor-update policy flag; global update mode with a
  first-launch choice.
- Trial with automatic revert, restore previous, restore stable, lock/unlock, linked (matched) activation with plain-language explanations.
- Signed catalog (ECDSA P-256 via Windows CNG, local test keys only) behind a provider interface; channel candidates with an honest
  no-candidate result; automatic updates limited to vetted API-compatible stable releases from a local package folder.
- Export dependency resolver (feature flags to component set), inventory and confirmed prune, problem-report builder.
- Fault injection hook (`StoreOptions::fault`) used to crash every step of staging and state changes in the checks.

## Deferred
Real WinVerifyTrust on vendor DLLs, any download or network provider, Streamline OTA wiring (only the policy flag exists), AMD
`ffxOverrideVersion`, host integration into `upscale_paths.h`, editor submenu UI, problem-report upload, loaded-version file written by the host,
read-only marking and ACLs on set folders, ZIP64.
