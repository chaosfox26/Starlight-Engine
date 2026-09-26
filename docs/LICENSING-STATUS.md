# Licensing status

This is a private-backup preparation snapshot. No project-wide MIT license has been granted by this document. Public-release licensing remains undecided.

Original engine and Analysis Tool code may be offered under MIT once ownership and dependency review are complete. Third-party code retains its existing terms and copyright notices; Chaos identifies this project's author, not the authors of dependencies.

Verified examples: Jolt Physics, cgltf, and JSON for Modern C++ carry MIT notices. NVIDIA NGX/DLSS SDK components have separate NVIDIA terms and must not be relabeled MIT. Install the SDK separately and set STARLIGHT_NGX_SDK when configuring a build; no private sibling-project path is required by the main CMake configuration.

The tracked-file inventory and third-party notices are now supplied in LICENSING-INVENTORY.json and ../THIRD-PARTY-NOTICES.md. Source files retain their existing dependency notices.

Release blocker: a historical continuation note records possible mixing with a separate server project and unverified removal of third-party copyleft code. No such dependency was found in the current CMake build graph or the reviewed sample networking source, but absence of a name or header does not establish independent authorship. The owner must resolve the historical origin question or provide the relevant earlier source history for a targeted comparison before a blanket MIT grant can be finalized. This is an unresolved provenance concern, not a finding that Starlight contains copyleft code.

Branding artwork is outside the proposed code license. Vendor SDKs and runtime binaries retain separate terms. No root MIT license is installed while the provenance question remains open.

The anonymized sample identifiers differ from the private development project. A fresh native build including the renamed sample succeeded on 2026-09-26. Verification exposed missing geometric test fixtures; both fixtures were restored, and the fresh core run then passed 924 checks. The full foreground audit is recorded separately; do not infer its result from this build or the core count.

## Owner clarification — 26 September 2026

The owner states that, to their knowledge, the engine has no connection to the separate server project referenced in the historical note. Record this as an owner statement, not an independently verified source-history finding. The earlier note alone does not establish copied code. This clarification should inform the remaining provenance review; do not repeat the same origin question without new evidence. No license grant or upload is performed by this update.

## Starter model restored — 26 September 2026
The owner confirmed ownership of the starter fox model and authorized restoring it to the private backup candidate. projects/SampleGame/build/models/fox-lead.glb is restored byte-for-byte from the original; SHA-256 1128346b5489e2eff42547e1694557cda1dd0b18cbbe282a247c143296c5b65f. Its GLB JSON metadata passed the known-private-name check. This confirms backup inclusion, not a separate public asset license grant. Earlier statements that the candidate excludes this model are superseded.
