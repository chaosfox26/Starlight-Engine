# Git checkpoints

## Development versus publication — 27 September 2026

This is the anonymized publication repository for the private GitHub project. It has separate history from the original development checkout. Publish reviewed source deltas; never merge the original private history, recovery archives or private assets. Chaos is the first-party publication identity. Preserve upstream attribution and external SDK licensing requirements. The current collaborator entry point is PHYSICS-HANDOFF.md.

This repository records the current engine source, tools, requirements and selected regression inputs. A checkpoint records working state; it does not certify completion. Current order is physics acceptance, mesh/texture quality, then performance optimization. See PHYSICS-AND-MOTION-TASK-LIST.md and PHYSICS-HANDOFF.md; older milestone-order notes are historical.

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

Detailed local evidence: `D:/projects/Starlight-analysis/file-integrity-20260926/REVIEW.md`. Current continuation: PHYSICS-HANDOFF.md. These external locations are local references, not files restored by cloning this repository.

This publication repository uses the private GitHub origin remote. The separate development checkout has no remote; its history must not be pushed here.
