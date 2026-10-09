"""Assemble an internal integrated preview; release approval remains separate."""

import hashlib
import os
import argparse

import json

from pathlib import Path

import shutil

import subprocess

import time

from preview_preflight import inspect, imports, SYSTEM



root = Path(__file__).resolve().parents[2]

config = json.loads((root/'tools/packaging/windows-preview.json').read_text())
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--runtime-directory', type=Path)
parser.add_argument('--toolchain-directory', type=Path)
parser.add_argument('--streamline-sdk', type=Path)
parser.add_argument('--rr-dll', type=Path, default=Path(os.environ.get('STARLIGHT_RR_DLL', 'D:/Upscaler updates/nvngx_dlssd.dll')), help='nvngx_dlssd.dll to ship (env STARLIGHT_RR_DLL)')
parser.add_argument('--rr-license', type=Path, default=Path(os.environ['STARLIGHT_RR_LICENSE']) if os.environ.get('STARLIGHT_RR_LICENSE') else root.parent/'GameProject/starlight-realms/native-dx12/third_party/sdk/streamline/external/ngx-sdk/license.txt', help='NVIDIA RTX SDK license text to ship (env STARLIGHT_RR_LICENSE)')
args = parser.parse_args()
for external in (args.rr_dll, args.rr_license):
    if not external.is_file():
        raise SystemExit('Missing external package input (nothing was staged): ' + str(external))
if args.runtime_directory:
    config['runtimeDirectory'] = args.runtime_directory.resolve().relative_to(root).as_posix()
runtime_source = root/config['runtimeDirectory']

preflight = inspect(root, config)

if preflight['missingImportsOrPrograms'] or preflight['errors']:

    raise RuntimeError('Runtime imports are incomplete')

target = root/'out/native-package-checks'/time.strftime('%Y%m%d-%H%M%S')

target.mkdir(parents=True, exist_ok=False)

entries = {}



def copy(source, relative):

    relative = Path(relative).as_posix()

    if relative in entries:

        return

    dest = target/relative

    dest.parent.mkdir(parents=True, exist_ok=True)

    shutil.copy2(source, dest)

    if source.is_relative_to(target):
        source_key = source.relative_to(target).as_posix().casefold()
        origin = next(entry['origin'] for key, entry in entries.items() if key.casefold() == source_key)
    elif source.is_relative_to(root):
        origin = source.relative_to(root).as_posix()
    else:
        # External SDK paths can contain private machine/account information.
        origin = 'external-sdk/' + source.name
    entries[relative] = dict(path=relative, sha256=hashlib.sha256(dest.read_bytes()).hexdigest(), origin=origin)


copy(root/'out/launcher/Starlight.exe', 'Starlight.exe')

from prepare_graphics_validation import prepare as prepare_graphics_validation
prepare_graphics_validation()
for name in ('D3D12Core.dll', 'd3d12SDKLayers.dll', 'LICENSE.txt', 'distributable files.txt'):
    copy(root/'out/classic/Release/D3D12'/name, 'runtime/Release/D3D12/'+name)
# Internal package: preserve the enabled dynamically loaded feature as well as

# its exact SDK notice. Public redistribution review is still a release gate.

rr=args.rr_dll

rr_license=args.rr_license

copy(rr,'runtime/Release/nvngx_dlssd.dll')

copy(rr_license,'licenses/NVIDIA-RTX-SDK.txt')

copy(rr_license,'runtime/Release/licenses/NVIDIA-RTX-SDK.txt')
if args.streamline_sdk:
    sdk = args.streamline_sdk.resolve(strict=True)
    lookup = {}
    for folder in (root/'out/classic/Release', sdk/'bin/x64'):
        lookup.update({p.name.lower(): p for p in folder.iterdir() if p.is_file()})
    pending = ['sl.interposer.dll', 'sl.common.dll', 'sl.dlss_d.dll']
    seen = set()
    while pending:
        name = pending.pop().lower()
        if name in seen:
            continue
        seen.add(name)
        source = lookup.get(name)
        if source is None:
            raise RuntimeError('Missing Streamline dependency: ' + name)
        copy(source, 'runtime/Release/' + source.name)
        for dependency in imports(source):
            child = dependency['name'].lower()
            if child not in SYSTEM and not child.startswith(('api-ms-win-', 'ext-ms-win-')):
                pending.append(child)
    for source, name in ((sdk/'license.txt', 'NVIDIA-Streamline-MIT.txt'),
                         (sdk/'3rd-party-licenses.md', 'NVIDIA-Streamline-third-party.md'),
                         (sdk/'bin/x64/reflex.license.txt', 'NVIDIA-Reflex.txt')):
        copy(source, 'licenses/' + name)
        copy(source, 'runtime/Release/licenses/' + name)
if args.toolchain_directory:
    bundle = args.toolchain_directory.resolve(strict=True)
    inventory = json.loads((bundle/'inventory.json').read_text(encoding='utf-8'))
    for item in inventory['files']:
        relative = Path(item['path'])
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Invalid toolchain inventory path')
        source = bundle/relative
        if not source.resolve().is_relative_to(bundle) or hashlib.sha256(source.read_bytes()).hexdigest() != item['sha256']:
            raise ValueError('Toolchain inventory mismatch: ' + item['path'])
        copy(source, Path('tools/toolchain')/relative)
    for name in ('toolchain.json', 'inventory.json'):
        copy(bundle/name, 'tools/toolchain/'+name)



for item in preflight['files']:

    source = root/item['path']

    copy(source, 'runtime/Release/'+source.name)

copy(root/'out/native-analysis-gui/Starlight.DeepAnalysis.exe', 'runtime/Release/Starlight.DeepAnalysis.exe')

copy(root/'out/native-analysis-gui/Starlight.DeepAnalysis.exe', 'Starlight Analysis.exe')

copy(runtime_source/'audit/starlight_audit_tool.exe', 'runtime/Release/audit/starlight_audit_tool.exe')

copy(runtime_source/'projects/Empty/Empty.dll', 'runtime/Release/projects/Empty/Empty.dll')

copy(root/'deep/projects/Empty/Empty.star', 'runtime/Release/projects/Empty/Empty.star')
copy(runtime_source/'projects/Empty/Empty.starscene', 'runtime/Release/projects/Empty/Empty.starscene')

for name in ('starlight-editor.ico','starlight-analysis.ico','starlight-glass-icon-v1.png',

             'starlight-alpha-one-cursive-v3.png','starlight-alpha-one-audio.wav'):

    copy(root/'design/branding'/name, 'design/branding/'+name)

for name in ('LICENSE','THIRD-PARTY-NOTICES.md'):

    copy(root/name,name)

    copy(root/name,'runtime/Release/licenses/'+('Starlight-LICENSE.txt' if name=='LICENSE' else name))

for source in (root/'licenses').iterdir():

    if source.is_file():

        copy(source,'licenses/'+source.name)

        copy(source,'runtime/Release/licenses/'+source.name)

for source in (root/'tools/audit').glob('*.json'):

    copy(source,source.relative_to(root))

for relative in ('tools/checks/historical-managed-references.json','migration/geometry-reference.json','tools/build/renderer-transforms.json',
                 'audit-runs/run-20260917-192850/comparison.bin',

                 'editor/checks/denoise/regression/chaos-rt.packet'):

    copy(root/relative,relative)

for source in (root/'migration/fixtures').rglob('*'):

    if source.is_file(): copy(source,source.relative_to(root))



# Source inspection remains available in this private test package. No build

# caches, external game assets, Git history, or old reports are copied.

from source_payload import source_files
for source in source_files(root):
    copy(source, source.relative_to(root))

copy(root/'CMakeLists.txt','CMakeLists.txt')

# Existing repository starter asset, for internal verification only.

# Verify the owner's authorization against the exact packaged asset hashes.
import re


def default_character_bundle():
    data_root = Path(os.environ['STARLIGHT_DATA_ROOT']) if os.environ.get('STARLIGHT_DATA_ROOT') else Path(os.environ['LOCALAPPDATA'])/'Starlight'
    settings_file = data_root/'default-character.json'
    if not settings_file.is_file():
        raise RuntimeError('No default character is configured: '+str(settings_file)+' does not exist. Open the editor, choose Import character once, then stage again.')
    settings = json.loads(settings_file.read_text(encoding='utf-8'))
    if settings.get('format') != 'starlight-default-character' or settings.get('version') != 1:
        raise RuntimeError('Unsupported default character setting: '+str(settings_file))
    model = settings.get('model', '')
    if not re.fullmatch(r'model-[0-9a-f]{64}', model):
        raise RuntimeError('The default character setting does not hold a model asset id: '+str(settings_file))
    roots = [root/'library']
    registry = data_root/'asset-roots.json'
    if registry.is_file():
        roots += [Path(entry['path']) for entry in json.loads(registry.read_text(encoding='utf-8')).get('roots', [])]
    for folder in roots:
        candidate = folder/(model+'.slasset')
        if candidate.is_file():
            payload = candidate.read_bytes()
            if len(payload) < 20 or payload[:4] != b'SLAS' or int.from_bytes(payload[8:12], 'little') != 3 or len(payload) != 20 + int.from_bytes(payload[12:20], 'little'):
                raise RuntimeError('Default character asset is damaged: '+str(candidate))
            return model, candidate, settings.get('name', 'Character')
    raise RuntimeError('The default character '+model+' is not in any registered asset library ('+', '.join(str(r) for r in roots)+'). Register the library that holds it with Starlight.NativeCommands --asset-root-add <folder>, then stage again.')


character_model, character_file, character_name = default_character_bundle()
copy(root/'library/library.json', 'library/library.json')
copy(character_file, 'library/'+character_model+'.slasset')
entries['library/'+character_model+'.slasset']['origin'] = 'asset-library/'+character_model
target.joinpath('default-character.json').write_text(json.dumps({'format': 'starlight-default-character', 'version': 1, 'model': character_model, 'name': character_name}, indent=2), encoding='utf-8')
entries['default-character.json'] = dict(path='default-character.json', sha256=hashlib.sha256((target/'default-character.json').read_bytes()).hexdigest(), origin='generated from the default character setting')
copy(root/'Starlight Documents/Current/docs/licensing/ALPHA-ONE-ASSET-PERMISSIONS.json',
     'licenses/ALPHA-ONE-ASSET-PERMISSIONS.json')
permissions = json.loads((target/'licenses/ALPHA-ONE-ASSET-PERMISSIONS.json').read_text(encoding='utf-8'))
for asset in permissions['assets']:
    if entries[asset['path']]['sha256'] != asset['sha256']:
        raise RuntimeError('Packaged asset differs from the owner-authorized copy: '+asset['path'])
for source in (root/'Starlight Documents/Current').rglob('*.md'):

    copy(source,source.relative_to(root))



copy(runtime_source/'projects/Empty/Empty.dll', 'deep/projects/Empty/Empty.dll')

copy(root/'deep/projects/Empty/Empty.star', 'deep/projects/Empty/Empty.star')
copy(runtime_source/'projects/Empty/Empty.starscene', 'deep/projects/Empty/Empty.starscene')
for dll in [name for name in entries if re.fullmatch(r'(?:runtime/Release|deep)/projects/(\w+)/\1\.dll', name)]:
    if dll[:-4] + '.starscene' not in entries:
        raise RuntimeError('Project module has no key card beside it: ' + dll)

for source in (root/'deep/settings').glob('*.startest'):

    copy(source,source.relative_to(root))



runtime = target/'runtime/Release'

# Windows searches the executable's directory, not its parent. Resolve each

# executable's entire import closure into that directory, including nested tools.

for executable in list(runtime.rglob('*.exe')):

    queue=[executable];seen=set()

    while queue:

        binary=queue.pop()

        if binary.name.lower() in seen: continue

        seen.add(binary.name.lower())

        for dependency in imports(binary):

            name=dependency['name']

            if name.lower() in SYSTEM or name.lower().startswith(('api-ms-win-','ext-ms-win-')): continue

            local=executable.parent/name

            if not local.is_file():

                available=runtime/name

                if not available.is_file(): raise RuntimeError(f'Missing runtime dependency: {name}')

                copy(available,local.relative_to(target))

            queue.append(local)

for binary in runtime.rglob('*'):

    if binary.suffix.lower() not in ('.exe','.dll'): continue

    for dependency in imports(binary):

        name = dependency['name']

        if name.lower() in SYSTEM or name.lower().startswith(('api-ms-win-','ext-ms-win-')): continue

        if not (binary.parent/name).is_file() and not (runtime/name).is_file():

            raise RuntimeError(f'Missing dependency for {binary.name}: {name}')



copy(root/'tools/packaging/GRAPHICS-CHECKS.txt', 'GRAPHICS-CHECKS.txt')
readme=target/'START-HERE.txt'

readme.write_text('Starlight Engine — Alpha One\nChaosFox\n\nOpen Starlight.exe and choose Run portable or Install on this PC.\nKeep the whole folder together when moving a portable copy.\nOpen Starlight Analysis.exe to check features and run diagnostics.\nThe installed version also adds Starlight and Analysis shortcuts to Start.\nFor optional graphics-debug validation, see GRAPHICS-CHECKS.txt.\n\nThis is an unfinished internal preview. Check the analysis report for known failures.\nCopyright © 2026 ChaosFox. All rights reserved. Third-party terms remain in licenses.\n',encoding='utf-8')

entries['START-HERE.txt']=dict(path='START-HERE.txt',sha256=hashlib.sha256(readme.read_bytes()).hexdigest(),origin='generated-by/tools/packaging/stage_native_preview.py')


manifest = dict(format='starlight-portable-v1',publisher='ChaosFox',

    editor='runtime/Release/Starlight.NativeEditor.exe',

    analysis='Starlight Analysis.exe',files=list(entries.values()),

    releaseReady=False,unfinishedReleaseGates=preflight['unfinishedReleaseGates'],

    purpose='Integrated internal verification; redistribution and final runtime acceptance remain open.',
    sourceBuildRuntimeFiles=[name for name in entries if name.startswith('runtime/Release/') and (
        name.startswith('runtime/Release/D3D12/') or
        (name.startswith('runtime/Release/licenses/') and Path(name).name not in ('Starlight-LICENSE.txt', 'THIRD-PARTY-NOTICES.md')) or
        (len(Path(name).parts) == 3 and Path(name).suffix.lower() == '.dll' and not Path(name).name.lower().startswith('starlight.')))],
    bundledToolchain=bool(args.toolchain_directory),
    rayReconstructionBackend='streamline' if args.streamline_sdk else 'ngx')

(target/'starlight-package.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')

subprocess.run([str(target/'Starlight.exe'),'--verify-package'],check=True,timeout=120)

result=dict(package=str(target),files=len(entries),bytes=sum((target/e).stat().st_size for e in entries),

            manifestVerified=True,releaseReady=False)

(target.parent/'latest-check.json').write_text(json.dumps(result,indent=2),encoding='utf-8')

print(json.dumps(result))

