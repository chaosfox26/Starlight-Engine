# Current dependencies inside deep

Verified 29 September 2026 against the root CMakeLists.txt, tracked source references, shader-generation scripts and launch script.

1. `deep/engine`: live renderer, shaders, model/texture code, worker pool, lightning and diagnostics. Used by the viewport, editor, player and checks.
2. `deep/Physics`: live physics library; its CMake file builds PhysicsWorld.cpp and links Jolt.
3. `deep/third_party/JoltPhysics`: required physics dependency. Preserve the submodule and its Build directory; this is source, not disposable build output.
4. `deep/src`: current project-host executable source and shared JSON header used by the editor and audit tools.
5. `deep/projects`: current native project modules and manifests. Empty is also used by checks. GameProject remains outside cleanup scope.
6. `deep/settings`: presets used by current checks, including native-1440p.startest.
7. `deep/automation`: historical experiment runner and results. No reference to this folder or its worker scripts was found in the tracked current build/runtime/tool sources. Its status file says stopped. The Analysis Tool may enumerate these files for its inventory; that is not a runtime dependency. No source rewiring is necessary to retire it. Deletion remains blocked by execution policy; no removal is claimed.
8. `deep/build`: old generated objects and library. Current CMake builds generate their own outputs under the chosen build directory.

## Current entry points

Use the root Launch-Starlight-Editor.cmd, which launches out/classic/Release/Starlight.NativeEditor.exe. Build-Native.py uses out/native by default or an explicit --build-dir. These outputs share this single source tree.

Do not move or delete the entire deep directory. Moving its live sources would require coordinated CMake, include, shader, test, analyzer and project-path updates and would not reclaim the experiment results themselves.

This is a source-reference review, not a new full engine test. No engine behavior or GameProject files were changed.

Cleanup update: the retired automation and generated build folders have now been moved to _quarantine/20260929, with restore manifests. Subsequent build, core and export checks passed. See the root WORKSPACE-STRUCTURE.md for the current layout.
