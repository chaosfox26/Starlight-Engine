# Clean Windows verification — 27 September 2026

PASS for the final exported movement/combat acceptance scope. This closes the previously blocked clean-Windows dependency gate.

A fresh Windows Sandbox session ran Windows 11 Enterprise x64 build 26100 after the owner restarted the host. The game was copied into the guest and all 21 exported files matched the saved SHA-256 inventory. Only the read-only test input and a dedicated writable results folder were mapped. Network was disabled. No SDK, development installation, or additional runtime was installed by the runner; the guest uninstall inventory was empty. Required redistributable DLLs traveled with the export.

Content validation exited successfully. All 33 gameplay checks passed in foreground, with 33 renderer captures, 9 submitted audio buffers, zero audio errors, and authored content unchanged. The separate combat exchange passed with 88 frames, live cone rendering, damage and foreground verified. All 33 captures were visually reviewed through three labeled contact sheets; the running guest window was also observed rendering the game. Shared virtual graphics worked without dedicated GPU passthrough. Guest display inventory exposed AMD Radeon Graphics, NVIDIA GeForce RTX 5090, and Microsoft Remote Display Adapter.

Evidence directory: `D:/projects/Starlight-analysis/clean-windows-kit-20260927/results/20260927-031246`. Files: environment.json, verified-export-hashes.json, transcript.txt, gameplay-check.json, combat-check.json, verdict.json, gameplay-0.bmp through gameplay-32.bmp, and review-0.png/review-12.png/review-24.png. The runner's intermediate verdict intentionally requires evidence review; this report records the completed review. Raw outputs are preserved.

This verifies the final exported fixture and its packaged dependencies in an actual clean guest. It does not certify every hardware configuration, subjective audible output, or every authored scene. The existing local renderer/performance evidence remains separate. No 4B implementation or publication changes were made.
