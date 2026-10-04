"""Hash-bound technical license inventory for one staged payload, not legal clearance."""
import argparse
import hashlib
import json
from pathlib import Path


def inventory(package):
    manifest_path = package / 'starlight-package.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    notices = ['licenses/Jolt-MIT.txt', 'licenses/cgltf-MIT.txt',
               'licenses/jsmn-MIT.txt', 'licenses/nlohmann-json-MIT.txt',
               'licenses/json-embedded-credits.txt', 'licenses/NVIDIA-RTX-SDK.txt']
    records, issues = [], []
    for item in manifest['files']:
        relative = Path(item['path'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Manifest path escapes package')
        path = package / relative
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != item['sha256']:
            raise ValueError('Package content differs from manifest: ' + item['path'])
        name, lower = relative.name.lower(), relative.as_posix().lower()
        component, terms, evidence = 'Starlight project material', 'Proprietary; embedded third-party material retains its terms', ['LICENSE', 'THIRD-PARTY-NOTICES.md']
        review = 'classification-only'
        if lower.startswith('tools/toolchain/'):
            prefix = 'tools/toolchain/'
            group = relative.parts[2] if len(relative.parts) > 2 else ''
            bundled = {
                'llvm': ('LLVM-MinGW x64 toolchain', 'Apache-2.0 with LLVM exceptions and retained MinGW component terms',
                         ['llvm/LICENSE.TXT', 'llvm/x86_64-w64-mingw32/share/mingw32/COPYING',
                          'llvm/x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64-runtime.txt',
                          'llvm/x86_64-w64-mingw32/share/mingw32/COPYING.MinGW-w64.txt']),
                'cmake': ('CMake build tools', 'BSD-style CMake license and retained component notices', ['cmake/doc/cmake/LICENSE.rst']),
                'ninja': ('Ninja build tool', 'Apache-2.0', ['ninja/COPYING']),
                'dxc': ('DirectX Shader Compiler', 'Supplied Microsoft, LLVM and MIT terms; binary redistribution review required',
                        ['dxc/LICENSE-MS.txt', 'dxc/LICENSE-LLVM.txt', 'dxc/LICENCE-MIT.txt']),
                'math': ('DirectXMath headers', 'MIT', ['math/LICENSE']),
                'streamline': ('Streamline interface headers', 'MIT and retained component notices',
                               ['streamline/license.txt', 'streamline/3rd-party-licenses.md']),
            }
            if group in bundled:
                component, terms, paths = bundled[group]
                evidence = [prefix + entry for entry in paths]
                review = 'bundled-component-notices-retained; distribution-conditions-review'
            else:
                component, terms, evidence = 'Starlight toolchain manifest', 'Proprietary project configuration and third-party inventory', ['LICENSE']
        elif name.startswith(('msvcp140', 'vcruntime140', 'concrt140')) and path.suffix.lower() == '.dll':
            component, terms, evidence = 'Microsoft Visual C++ runtime', 'Microsoft redistribution terms', ['https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files']
            review = 'exact-redist-origin-and-license-entitlement-review'
        elif name in ('d3d12core.dll', 'd3d12sdklayers.dll'):
            component, terms = 'Microsoft DirectX Agility SDK 1.619.6', 'Microsoft DirectX license'
            evidence = [str(relative.parent/'LICENSE.txt').replace('\\', '/'), str(relative.parent/'distributable files.txt').replace('\\', '/')]
        elif name == 'nvngx_dlssd.dll':
            component, terms, evidence = 'NVIDIA Ray Reconstruction', 'NVIDIA RTX SDK license', ['licenses/NVIDIA-RTX-SDK.txt']
            review = 'redistribution-conditions-review'
        elif name in ('sl.interposer.dll', 'sl.common.dll', 'sl.dlss_d.dll'):
            component, terms = 'NVIDIA Streamline runtime integration', 'Streamline and linked NVIDIA SDK conditions'
            evidence = ['licenses/NVIDIA-Streamline-MIT.txt', 'licenses/NVIDIA-Streamline-third-party.md', 'licenses/NVIDIA-RTX-SDK.txt']
            review = 'redistribution-conditions-review'
        elif name == 'nvlowlatencyvk.dll':
            component, terms, evidence = 'NVIDIA Vulkan latency dependency', 'Supplied NVIDIA Reflex license', ['licenses/NVIDIA-Reflex.txt']
            review = 'delay-import-dependency; redistribution-conditions-review'
        elif lower.startswith('deep/third_party/joltphysics/'):
            component, terms, evidence = 'Jolt Physics source', 'MIT', ['licenses/Jolt-MIT.txt', 'deep/third_party/JoltPhysics/LICENSE']
        elif name == 'cgltf.h':
            component, terms, evidence = 'cgltf and embedded jsmn', 'MIT', ['licenses/cgltf-MIT.txt', 'licenses/jsmn-MIT.txt']
        elif name == 'json.hpp':
            component, terms, evidence = 'JSON for Modern C++', 'MIT and embedded notices', ['licenses/nlohmann-json-MIT.txt', 'licenses/json-embedded-credits.txt']
        elif name == 'starlight.physics.dll':
            component, evidence = 'Starlight physics with Jolt', ['LICENSE', 'licenses/Jolt-MIT.txt']
        elif path.suffix.lower() in ('.exe', '.dll'):
            evidence += notices
            review = 'linked-component-list-conservative-not-an-exact-link-map'
        elif lower.startswith('design/branding/') or lower.startswith('library/model-'):
            component, evidence = 'Starlight branding or authorized asset', ['LICENSE', 'licenses/ALPHA-ONE-ASSET-PERMISSIONS.json']
            review = 'owner-authorization-record'
        elif '/licenses/' in '/' + lower or name in ('license', 'third-party-notices.md'):
            component, terms, evidence = 'License and provenance evidence', 'Preserve original notices', [relative.as_posix()]
        missing = [entry for entry in evidence if not entry.startswith('https://') and not (package/entry).is_file()]
        if missing:
            issues.append({'path': relative.as_posix(), 'missingNotices': missing})
        records.append(dict(path=relative.as_posix(), sha256=digest, bytes=path.stat().st_size,
                            origin=item.get('origin'), component=component, terms=terms,
                            evidence=evidence, review=review))
    return dict(format='starlight-package-license-inventory-v1',
                manifestSha256=hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
                publisher=manifest['publisher'], files=records, missingNoticeFiles=issues,
                clearance=False, scope='Exact staged file hashes and notice presence. Classifications do not prove ownership, license entitlement or distribution clearance.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = inventory(args.package.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(files=len(result['files']), missingNotices=len(result['missingNoticeFiles']), clearance=False)))
    raise SystemExit(1 if result['missingNoticeFiles'] else 0)
