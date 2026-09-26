# Third-party notices and distribution boundary

Chaos is the project identity. It does not replace the authorship or copyright of dependencies. Preserve all upstream notices, including names and URLs, in redistributed copies.

| Component | Included material | Terms and notice |
|---|---|---|
| Jolt Physics | Pinned submodule, commit 0373ec0dd762e4bc2f6acdb08371ee84fa23c6db; built physics library | MIT; licenses/Jolt-MIT.txt and the submodule's LICENSE |
| cgltf (header labels 1.15; upstream commit de9828bc6419064c302546313ce8ff5eac6cd703) | deep/engine/third_party/cgltf.h, exact match to that upstream file | MIT; licenses/cgltf-MIT.txt; original header retained |
| jsmn | Embedded in cgltf.h | MIT; licenses/jsmn-MIT.txt; original embedded notice retained |
| JSON for Modern C++ 3.11.3 | deep/src/json.hpp and deep/projects/SampleGame/net/json.hpp | MIT; licenses/nlohmann-json-MIT.txt plus embedded source notices and licenses/json-embedded-credits.txt |
| NVIDIA NGX/DLSS | External SDK used to compile/link the viewport; SDK files are not tracked here | NVIDIA RTX SDK license, not this project's proposed MIT license. Review the installed SDK's license.txt before redistributing binaries or SDK material. |
| Microsoft Windows SDK, DirectX compiler, Visual C++ runtime | External build tools/system APIs; build/export may copy runtime files | Microsoft terms apply to redistributed components. Source-only backup does not include those binaries. |
| AMD Anti-Lag 2, FidelityFX/FSR, Intel XeSS, NVIDIA Streamline | Optional integration source; vendor SDKs are not tracked. Default CMake uses upscale_off/upscale_stubs. | Obtain the specific SDK separately and comply with its exact version's license/notices before enabling or redistributing it. Anti-Lag 2 upstream states MIT; do not infer that every other SDK or binary is MIT. |
| MinHook | Optional external diagnostics dependency, disabled unless configured | Not included in this repository; preserve its license if enabled or distributed. |

The source snapshot contains no tracked executables, DLLs, fonts, audio, or downloaded character models. Branding PNG/ICO files and their generation notes are present; they are excluded from any proposed code-only MIT grant pending the owner's asset-license decision. Small geometric regression inputs are included for repeatable tests.

License source URLs and hashes are in licenses/sources.json. File classifications are in docs/LICENSING-INVENTORY.json. This inventory is not evidence that all project-origin code was independently authored; see docs/LICENSING-STATUS.md.

Authoritative references reviewed 2026-09-26:

- https://opensource.org/license/mit
- https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt
- https://github.com/GPUOpen-LibrariesAndSDKs/AntiLag2-SDK
- https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/docs/license.md
- https://github.com/intel/xess

No binary release is authorized or certified by this file. Source licensing and binary redistribution are separate reviews.
