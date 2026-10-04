# Self-contained Windows preview: portable or installed

## Agreed product

1. Publisher and copyright holder: **ChaosFox**. The personal-information replacement/display identity is **ChaosFox**.
2. Deliver an unfinished but working Starlight Engine preview. Existing limitations must be visible; installation must not imply a finished engine.
3. One extracted portable folder must provide every included engine feature. No separate Python, compiler, CMake, SDK or runtime installation may be required from a user. Do not remove features or relabel skipped checks as passed to meet this condition.
4. Include editor, standalone player/export capability, Analysis Tool, runtime resources, required notices and getting-started instructions. A shared launcher offers portable operation or optional local installation on first launch. Local installation provides shortcuts and an uninstaller; portable operation needs neither registration nor administrator access.
5. Replace the Python Analysis Tool implementation with native functionality. Freezing or bundling Python does not satisfy the requested removal of that dependency.
6. Preserve functional test coverage using prebuilt test programs and fixtures. Build-from-source verification is a distinct capability requiring a distributable toolchain; it must be resolved explicitly before claiming that every existing developer tool is self-contained.
7. Use application-relative paths for packaged resources and a writable portable data directory in portable mode, or per-user application data in installed mode, for settings, projects and reports; clearly report a read-only location. No dependency on this development machine or sibling projects.
8. Test in fresh Windows Sandbox: extraction, launch, scene editing/saving/loading, models/textures, movement/combat/physics/effects, project workflow, game export and exported play, all analysis actions, compact reports, restart, moving the folder and removing the portable copy. Run from a path containing spaces. Judge functionality, not FPS.
9. Preserve dependencies' copyrights and license terms. Original Starlight code is proprietary. Asset and SDK permissions must match the actual payload.
10. Target a portable archive below 1 GB, measured after packaging. Keep the working engine folder below 4 GiB where practical; build intermediates may grow temporarily and should use the existing controlled cleanup.

## Current checkpoint

The editor/player are native. The Analysis GUI launches Python; its runner uses Python, CMake/CTest and the compiler. Export already copies player/renderer/physics and release Visual C++ runtime DLLs. Export does not yet copy required notices. The editor discovers its root using a fixed four-parent layout and refers to a starter model in a project-specific directory.

Known unresolved full-run failures: fullscreen telemetry visibility, denoiser object motion, and the latest editor-controls timeout. Recheck against fresh evidence; do not claim they are fixed by packaging.

## Delivery order

1. Establish a deterministic payload and inspect executable imports; report missing dependencies without modifying the engine.
2. Correct resource discovery and asset/license packaging, including exported-game notices.
3. Implement the native analysis runner and native equivalents of Python checks, reports and cleanup; prove parity with the existing checks.
4. Resolve build-tool capability without requiring separate installs. Confirm redistribution rights for any bundled tools.
5. Assemble the shared package and validate portable extraction/relocation as well as local installation, shortcuts, every included workflow and uninstall in Sandbox.

The packaging preflight is developer tooling; it is not shipped and does not add a Python requirement to the installed product. A successful import inventory alone is not a self-contained release certification: dynamic loading, data files, subprocesses and clean-machine behavior must also be verified.

## Portable requirement clarification

Portable is a folder containing executables and required resources, not necessarily one executable. In portable mode keep settings and generated reports in that folder rather than leaving application data on the host. Do not silently omit the Analysis Tool or test coverage to obtain portability. The owner clarified on 29 September 2026 that portable distribution must ALSO offer optional local installation on first launch.

## First-launch behavior (latest clarification)

1. Opening the shared launcher first offers **Run portable** or **Install on this PC**, with a cancel option.
2. Run portable launches directly from the extracted folder and stores user data alongside it. Remember that choice; provide Install on this PC later through the launcher/settings.
3. Install on this PC copies the same complete payload into a per-user local application folder, creates shortcuts and an uninstall entry, then launches the installed copy. Publisher: ChaosFox. Do not require administrator access for the default per-user installation.
4. Do not delete or alter the original portable package during installation. Any transfer of existing portable projects/settings must be explicit and preserve originals.
5. Uninstall removes installed program files and shortcuts while preserving user projects and data by default.
6. Both modes have the same features and the same no-separate-dependency-install requirement. Test both modes in Sandbox before release.
