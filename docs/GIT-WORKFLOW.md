# Git checkpoints

## Development versus publication — 27 September 2026

This is the original private development history. Never push it to the separate anonymized publication repository. The latter lives at D:/projects/Starlight-Publish and was published privately to https://github.com/ChaosFox/Starlight-Engine with ChaosFox first-party identity and preserved upstream credits. Review/anonymize future deltas before publication; do not copy private lore, identities, recovery archives or original history into it. No blanket MIT grant was made; the external NVIDIA SDK retains its separate license/prerequisite status. Detailed private recovery context is D:/projects/_recovery/STARLIGHT-RESUMED-20260927.md. Google Drive upload was cancelled and its uploaded folder deleted at the owner's request; do not treat Drive as a backup or restart uploads.

This repository records the current engine source, tools, requirements and selected regression inputs. A checkpoint records working state; it does not certify completion. Step 4/4A remains open, performance work follows its completion, and 4B (abilities, stats, traits) follows performance verification.

## Working together

- Before editing, read the current handoff and inspect `git status` and recent commits. Codex and Opus share the same working files; avoid concurrent edits to the same files.
- Make small, descriptive commits after a coherent change and its relevant verification. State unfinished work and test limitations.
- Do not reset, clean, force-checkout or discard another session's changes to obtain a clean tree.
- Review `git diff` and `git diff --cached` before committing. Keep credentials, generated builds and temporary reports out of Git.
- Use a separate branch/worktree for independent experimental work once its starting checkpoint is committed.

## Restore and build

Clone with `git clone --recurse-submodules <repository>` or initialize dependencies with `git submodule update --init --recursive`. Jolt is pinned as a submodule; it is not copied into engine commits.

The native build requires the external Windows/Visual Studio/DXC/Python toolchain and the NGX SDK described in README.md and CMakeLists.txt. Git is not a replacement for those dependencies or for large local assets and audit captures. The normal installed editor lives under `out/classic/Release`; that generated output is not tracked. Rebuild it from the checkpoint before claiming installed binaries match source.

## Initial checkpoint evidence

The 24 September full GUI audit passed all 13 stages, including 924 core checks. A 26 September file-integrity review confirmed all 173 indexed source files unchanged. Two environment-related failures in the 25 September audit were rerun successfully on 26 September. This Git setup changes tracking and documentation, not gameplay, and does not rerun or close all gameplay acceptance gates.

Detailed local evidence: `D:/projects/Starlight-analysis/file-integrity-20260926/REVIEW.md`. Current continuation: `D:/projects/_recovery/STARLIGHT-TUESDAY-HANDOFF-20260929.md`. These external locations are local references, not files restored by cloning this repository.

No remote is configured at this checkpoint. Local commits and local backups protect against editing mistakes, but not loss of the entire drive. Choose a private remote destination separately before uploading code.
