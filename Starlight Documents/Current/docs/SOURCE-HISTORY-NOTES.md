# Source history notes

Historical context moved out of active renderer comments. Technical constraints and license notices remain with the code. These notes describe earlier work, not new verification.

## editor/native/render_device_editor.h

- Milestone 5: upscaler / frame-generation backends (upscale\*.cpp). One path is chosen at
- M6: three frames in flight. Every per-frame GPU-read resource (command allocator, instance
- per-character fur profile (the owner #2097: "luscious/soft/silky" vs "tough/rigid"), picked by look.sex
- follow-spotlights are real cone lights here (M6) instead of the old per-instance "glow" add.
- ---- milestone 5: offscreen scene at render size, reconstructed to output size ----
- ---- M6: skinned characters + bakes-v1 textures ----
- full GPU idle: init / mode change / screenshot / teardown only (never per frame, M6)
- ---- M5: pick the adapter first (vendor drives the auto path), then let the chosen backend
- ---- M5: backend init (may REPLACE the swapchain with a frame-generation one), then
- ---- M5: backend selection. --upscaler auto = adapter vendor (NVIDIA dlss, AMD fsr, Intel
- M6 shading: one GGX/Cook-Torrance model for everything. CB colours are sRGB (the room's
- ---- shell fur (the owner #2090/#2097 phase a). The 'fur' submesh is drawn again N times
- ---- M6: load the charfactory GLBs + their bakes-v1 texture sets. Missing files degrade
- to the M5 primitives per model (never a crash); everything is reported in the log/report.
- ---- M6: one skinned character. Evaluates the pose into this frame's palette slot, then
- threads, report the speedup. This is the measured "spread it across all cores" demonstration.
- M6 block: what was actually loaded + drawn, and how the pacing behaved
- ---- M6: acquire this frame's slot. The CPU only waits when the slot's previous
- GPU-bound case, never the sync-every-frame flush of M1..M5.
- ---- M5: reconstruct render-size scene -> output-size upOutput, copy into the backbuffer ----
- ---- M6: retire by slot instead of flushing. The fence value signalled here is what the

## deep/engine/render_device.h

- Milestone 5: upscaler / frame-generation backends (upscale\*.cpp). One path is chosen at
- M6: three frames in flight. Every per-frame GPU-read resource (command allocator, instance
- per-character fur profile (the owner #2097: "luscious/soft/silky" vs "tough/rigid"), picked by look.sex
- follow-spotlights are real cone lights here (M6) instead of the old per-instance "glow" add.
- ---- milestone 5: offscreen scene at render size, reconstructed to output size ----
- ---- M6: skinned characters + bakes-v1 textures ----
- full GPU idle: init / mode change / screenshot / teardown only (never per frame, M6)
- ---- M5: pick the adapter first (vendor drives the auto path), then let the chosen backend
- ---- M5: backend init (may REPLACE the swapchain with a frame-generation one), then
- ---- M5: backend selection. --upscaler auto = adapter vendor (NVIDIA dlss, AMD fsr, Intel
- M6 shading: one GGX/Cook-Torrance model for everything. CB colours are sRGB (the room's
- ---- shell fur (the owner #2090/#2097 phase a). The 'fur' submesh is drawn again N times
- ---- M6: load the charfactory GLBs + their bakes-v1 texture sets. Missing files degrade
- to the M5 primitives per model (never a crash); everything is reported in the log/report.
- ---- M6: one skinned character. Evaluates the pose into this frame's palette slot, then
- threads, report the speedup. This is the measured "spread it across all cores" demonstration.
- M6 block: what was actually loaded + drawn, and how the pacing behaved
- ---- M6: acquire this frame's slot. The CPU only waits when the slot's previous
- GPU-bound case, never the sync-every-frame flush of M1..M5.
- ---- M5: reconstruct render-size scene -> output-size upOutput, copy into the backbuffer ----
- ---- M6: retire by slot instead of flushing. The fence value signalled here is what the

## deep/engine/upscale/upscale_stubs.cpp

Milestone 5 staging: vendor backends land one file at a time (upscale_fsr.cpp, upscale_dlss.cpp,
upscale_xess.cpp). Until a backend exists its factory returns nullptr and the renderer logs
"not built" and falls through to the next candidate. Remove the matching line when the real
backend lands; build.bat compiles upscale\*.cpp so nothing else changes.

## Historical release-note generator

The original continuation text from migration/record_classic_release.py now lives in ../migration/CLASSIC-RELEASE-HISTORICAL-NOTE.md. The script reads the same text without changing its output. This historical helper was not executed.

## Separation rule

Keep algorithms, invariants, units, API contracts, troubleshooting constraints, licenses, and tool directives beside code. Put project history, conversations and release/handoff narratives in documentation. Report generators and test fixtures may contain prose as runtime data; do not mistake that for removable comments. Vendored code, archived report snapshots, and the separate game project are unchanged.
## deep/engine/model/skinned.h

- Starlight DX12 client — skinned character models (M6).

## deep/engine/model/texture.h

- Starlight DX12 client — texture loading (M6): PNG via WIC -> RGBA8 mip chain, with a raw mip

## deep/engine/upscale/upscale.h

- Starlight DX12 client — milestone 5 upscaler / frame-generation abstraction.

## deep/engine/model/texture.cpp

- for the mip distances the club camera uses; a renormalizing/linear-space variant is an M6 gap.

## deep/engine/upscale/upscale_dlss.cpp

- Starlight DX12 client — milestone 5 DLSS backend (NVIDIA Streamline 2.12: DLSS super resolution +

## deep/engine/upscale/upscale_fsr.cpp

- Starlight DX12 client — milestone 5 FSR backend (AMD FidelityFX SDK 2.3.0: upscaler 4.1.1 / 3.1.x

## deep/engine/upscale/upscale_off.cpp

- Starlight DX12 client — milestone 5 "off" backend: bilinear compute upscale, no vendor SDK.

## deep/engine/upscale/upscale_xess.cpp

- Starlight DX12 client — milestone 5 XeSS backend (Intel XeSS SDK: XeSS-SR super resolution, XeSS-FG
