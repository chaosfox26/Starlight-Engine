# Licensing inventory review — 29 September 2026

## Current result

The former inventory covered 256 files before later additions. This refreshed inventory covers 323 publication entries, including the pinned Jolt submodule and pending current documentation. Classifications are navigation aids, not proof of authorship. Exact hashes and a separate installed-runtime DLL list are in CURRENT-LICENSING-INVENTORY.json.

- project-content: 200
- documentation-or-data: 105
- vendored-third-party: 3
- pinned-third-party-submodule: 1
- asset: 8
- dependency-license-evidence: 6

## What is documented

1. Jolt Physics: MIT. The current submodule revision matches the recorded pin. Include the Jolt notice with a binary containing it.
2. cgltf and embedded jsmn: MIT. Preserve both notices, not only the loader's top-level credit.
3. JSON for Modern C++: MIT, with embedded credits retained. Do not replace upstream author identities with the project publisher.
4. The three source-recorded license-file hashes checked for Jolt, cgltf and JSON still match the retained copies.
5. The installed Visual C++ DLL list was compared by hash with the installed release redistributable directory. See the machine-readable results. Matching bytes establishes file origin, not proof of the owner's license entitlement.

## What remains a decision or verification

1. The owner selected proprietary licensing for Starlight's own code on 29 September 2026. The root LICENSE records all rights reserved, with explicit third-party exceptions. Preview distribution terms still need to accompany the finished installer. Publisher metadata 'ChaosFox' does not grant software rights or create a trusted digital signature.
2. NVIDIA NGX code is linked by the viewport build, even if optional reconstruction is not selected during a demo. The installed RTX SDK license contains redistribution conditions and must be assessed for the actual binary package. Do not label NVIDIA SDK material MIT. Raw SDK developer tools should not be bundled simply because they exist locally.
3. Microsoft runtime redistribution is governed by the installed Visual Studio license and current distributable-code list. Include permitted unmodified release runtime files; exclude debug_nonredist. Do not ship compiler/SDK installations as ordinary engine runtime.
4. Optional AMD FidelityFX, Intel XeSS, NVIDIA Streamline and Anti-Lag integration files do not mean all vendor DLLs are required. The current main CMake viewport uses off/stub upscaler sources. [Update 2026-10-04: that remains true of the editor viewport, but the Empty and GameProject host libraries now build the vendor backends by default, and their build output contains the vendor DLLs and notices (see THIRD-PARTY-NOTICES.md), so the installer dependency list must account for them.] Establish the exact installer dependency list before carrying large optional DLLs from deep/bin.
5. On 30 September 2026, the owner authorized the current starter model and supplied Suno splash audio for the shareable Alpha One preview and reported paid-tier commercial audio rights. See ALPHA-ONE-ASSET-PERMISSIONS.json for exact hashes and scope. Future replacement assets need their own review; this is not a blanket grant for downloaded models.
6. Python and an installer framework, if bundled or used for the installer, add their own notices/terms. No installer framework has been downloaded or installed during this review.
7. The older provenance note was qualified by the owner's statement that the engine has no connection to the referenced server project. That statement is preserved. No copied copyleft code was established by the prior review; neither does a name scan prove independent authorship. This inventory does not repeat the same ownership question or treat that old note as a confirmed violation.

## Concrete packaging gap

Updated 30 September: editor/cpp/game_export.cpp copies the required engine/Jolt/cgltf/jsmn/JSON notices, the NVIDIA RTX notice when reconstruction is present, and the Streamline/Reflex runtime files and notices when that backend is present. The latest immutable internal stage, out/native-package-checks/20260930-221238, passes the export regression including notice equality and manifest hashes. Package notice inventory covers 10,451 files with no missing referenced notices. These checks establish notice preservation, not distribution entitlement or complete legal clearance.

The existing THIRD-PARTY-NOTICES.md also contains a stale claim that no character model is tracked; the later licensing-status appendix records the restored starter model. Use the current asset entries rather than that superseded exclusion claim.

## Next steps

1. Owner-code policy is selected: proprietary. Finalize preview-use terms and asset distribution permissions separately.
2. Fix notice packaging for exports and installer staging.
3. Resolve the exact NVIDIA and Microsoft binary distribution requirements for the chosen package.
4. Create a package manifest of every shipped file, origin, hash and applicable license. Do not treat this source inventory as that final manifest.
5. Test the installer in Sandbox after the package exists. No installer or clean-install result is claimed by this review.

## Authoritative references

- [MIT license](https://opensource.org/license/mit)
- [NVIDIA RTX SDK license](https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt) — the installed SDK license was also read; use its exact version for the package.
- [Microsoft runtime redistribution](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files)
- [Visual Studio 2026 distributable code](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution)

This is a technical inventory and review of supplied terms, not a legal opinion or universal release certification.
