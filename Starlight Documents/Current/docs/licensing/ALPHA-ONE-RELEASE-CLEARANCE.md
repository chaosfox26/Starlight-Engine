# Alpha One release clearance — owner review pending

This record describes the internal candidate and outstanding distribution conditions. It grants no new rights and is not a public-release approval.

## Verified technical package

The immutable internal candidate `out/native-package-checks/20260930-224320` includes the editor/player, native analysis tools, source and bundled build tools. Its package manifest verifies. Earlier equivalent renderer/player builds pass export and ray-reconstruction tests; the latest audit changes have separate passing regression tests. Full latest source-build GUI acceptance is still running in Sandbox.

Fresh Sandbox run `out/integrated-sandbox/20260930-221407/results/result.json` completed all 26 installation, relocation, shortcut, uninstall and data-preservation checks against stage 221238. Ten runtime stages passed. Strict graphics-debug validation failed because the Windows optional component is absent; ray reconstruction failed because the virtual adapter reports Streamline feature unsupported. Neither failure is counted as a pass. Available physical NVIDIA hardware passes the bounded ray-reconstruction workload.

## Conditions that remain unresolved

| Item | Evidence or remaining action |
|---|---|
| Permission for recipients to use Alpha One | Root LICENSE reserves all rights and says preview permission is supplied separately. Owner must approve recipient-facing preview terms. The intended permission for publishing exported games also needs an explicit grant; do not infer it from the presence of Export. |
| NVIDIA commercial-release notification | The supplied NVIDIA RTX SDK supplement, section 4, requires notice before commercial release for DLSS/NGX integrations. Required fields include company/publisher/developer, application, SDK, platform, ship date and product/video link. ChaosFox, Starlight Engine and Windows x64 are established; a release date, public link and any existing notification record are not established here. No notification has been sent. |
| NVIDIA trademark placement and approval | Supplement sections 7.1(b) and 7.2(c) address splash/about/credits placement and prior approval of proposed mark use. The currently approved Starlight splash is preserved. No NVIDIA-approved proposed placement or separate agreement is recorded; do not silently replace the approved design or claim compliance from a text license file alone. |
| Microsoft release-runtime entitlement | Package DLL origin was matched to release redistributables, not debug runtimes. Matching hashes does not establish the owner's license entitlement. Owner confirmation remains pending. |
| DXC binary terms | The bundled DXC archive retains all three supplied license files. The Microsoft terms include distribution and recipient-protection conditions. Exact permission for the selected binary set and recipient terms must be resolved before public release; notice presence alone is insufficient. |
| Open-source notices | LLVM/MinGW, CMake, Ninja, DirectXMath, Streamline headers, Jolt, cgltf, jsmn and JSON notices are retained. Exact shipped file hashes and notice checks are recorded by the package-license inventory. This is technical notice verification, not a blanket license certification. |
| Assets | Owner authorization already covers the recorded starter model and supplied paid-tier Suno splash audio for this preview. See ALPHA-ONE-ASSET-PERMISSIONS.json. Do not ask the owner to repeat that authorization or extend it to unrelated assets. |

The outstanding owner question asks whether NVIDIA approval/notification and Microsoft entitlement already exist. Technical verification continues while that question is pending. There has been no publishing, vendor contact, acceptance of terms on the owner's behalf or automatic Windows-component installation.

## References

- Exact NVIDIA terms in the candidate's `licenses/NVIDIA-RTX-SDK.txt` and `licenses/NVIDIA-Reflex.txt`; [NVIDIA SDK license source](https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt).
- [Microsoft runtime redistribution](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files).
- [Visual Studio 2026 distributable code](https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution).
- Exact DXC terms in the candidate's `tools/toolchain/dxc` license files; [pinned compiler release](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2609).
- [Microsoft's Agility runtime specification](https://github.com/microsoft/DirectX-Specs/blob/master/d3d/D3D12Redistributable.md), which requires the OS Graphics Tools feature even with redistributable debug DLLs.
