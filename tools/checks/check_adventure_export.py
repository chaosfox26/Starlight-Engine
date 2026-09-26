"""Bounded standalone export check. Uses a fresh fixture; never desktop input."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve() / ('run-' + uuid.uuid4().hex)
    out.mkdir(parents=True)
    runtime = args.runtime.resolve()
    checks = []
    binaries = {}

    def run(command, expect=0):
        result = subprocess.run([str(x) for x in command], cwd=out, capture_output=True,
                                timeout=40, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        with (out/'commands.log').open('ab') as log:
            log.write(result.stdout + result.stderr)
        if result.returncode != expect:
            raise RuntimeError(f'{Path(command[0]).name} returned {result.returncode}; expected {expect}')

    def check(condition, name):
        if not condition:
            raise RuntimeError(name)
        checks.append(name)

    try:
        for name in ('Starlight.NativeCommands.exe', 'Starlight.NativePlayer.exe', 'Starlight.Viewport.dll', 'Starlight.Physics.dll'):
            binaries[name] = hashlib.sha256((runtime/name).read_bytes()).hexdigest()
        cli = runtime/'Starlight.NativeCommands.exe'
        scene = out/'starter.starscene'
        package = out/'game'
        run([cli, '--new-adventure', scene])
        scene_hash = hashlib.sha256(scene.read_bytes()).hexdigest()
        run([cli, '--new-adventure', scene], expect=1)
        check(hashlib.sha256(scene.read_bytes()).hexdigest() == scene_hash, 'Existing starter scene preserved')
        fixture = json.loads(scene.read_text(encoding='utf-8'))
        door = next(c for c in fixture['Characters'] if c.get('GameplayRole') == 'door')
        game = fixture['Game']
        fixture['SchemaVersion'] = 11
        fixture['Rules'] = [{'Id': 'export-rule-check', 'Name': 'Exported rule', 'Event': 'enter-area',
                             'Subject': '', 'X': game['SpawnX'], 'Y': game['SpawnY'], 'Z': game['SpawnZ'],
                             'Radius': 1, 'Once': True, 'Enabled': True, 'Conditions': [],
                             'Actions': [{'Type': 'complete-objective', 'Target': 'Export rule'},
                                         {'Type': 'open-door', 'Target': door['Id']}]}]
        fixture['Quests'] = [{'Id': 'export-quest', 'Title': 'Restore the outpost', 'Description': 'Enter the marked area to open the door.', 'RuleIds': ['export-rule-check'], 'StepSizes': [1], 'Tracked': True}]
        object_links = []
        for index, (role, event) in enumerate((('door', 'object-opened'), ('breakable', 'object-broken'), ('key', 'item-collected'))):
            obj = dict(door, Id='preset-object-' + role, Name=role + ' block', X=8 + index, GameplayRole=role, GameplayKey=('preset-object-' + role if role == 'key' else ''))
            fixture['Characters'].append(obj)
            rule_id = 'preset-rule-' + role
            fixture['Rules'].append(dict(fixture['Rules'][0], Id=rule_id, Name=role + ' objective', Event=event, Subject=obj['Id'], Actions=[{'Type': 'complete-objective', 'Target': rule_id}]))
            object_links.append(rule_id)
        fixture['Quests'].append({'Id': 'preset-quest', 'Title': 'Object interactions', 'Description': 'Use your authored objects.', 'RuleIds': object_links, 'StepSizes': [3], 'Tracked': False})
        scene.write_text(json.dumps(fixture, indent=2), encoding='utf-8')
        run([cli, '--export-game', scene, package, runtime])
        check((package/'content/scene.starscene').is_file(), 'Starter scene packaged')
        packaged = json.loads((package/'content/scene.starscene').read_text(encoding='utf-8'))
        check(packaged['Quests'] == fixture['Quests'], 'Export preserves quest title description and stable objective links')
        check(packaged['Rules'] == fixture['Rules'], 'Export preserves saved events conditions actions and target identities')
        check(json.loads((package/'gameplay-check.json').read_text(encoding='utf-8')).get('findings') == [],
              'Starter export includes a clean basic gameplay setup report')
        player = package/'Starlight.NativePlayer.exe'
        run([player, '--validate'])
        checks.append('Packaged content validates')
        before = {str(p.relative_to(package)): hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in package.rglob('*') if p.is_file()}
        run([cli, '--export-game', scene, package, runtime], expect=1)
        after = {str(p.relative_to(package)): hashlib.sha256(p.read_bytes()).hexdigest()
                 for p in package.rglob('*') if p.is_file()}
        check(before == after, 'Existing package refused without modification')
        missing = out/'missing-runtime'; missing.mkdir()
        run([cli, '--export-game', scene, out/'must-not-exist', missing], expect=1)
        check(not (out/'must-not-exist').exists(), 'Missing runtime rejected before creating output')
        smoke = out/'render.json'
        run([player, '--smoke', smoke])
        rendered = json.loads(smoke.read_text(encoding='utf-8'))
        check(rendered.get('pass') is True and rendered.get('frames', 0) >= 60 and not rendered.get('error'),
              'Packaged starter renders 60 frames without error')
        check(rendered.get('questCount') == 2 and rendered.get('firstQuestComplete') is True, 'Standalone quest tracker reads real runtime completion')
        check(rendered.get('completedObjectives') == ['Export rule'] and
              rendered.get('remainingCharacters') == len(fixture['Characters']) - 1,
              'Packaged standalone executes saved area objective and linked-door actions')
        check(all(hashlib.sha256((runtime/name).read_bytes()).hexdigest() == digest and
                  hashlib.sha256((package/name).read_bytes()).hexdigest() == digest
                  for name, digest in binaries.items() if name != 'Starlight.NativeCommands.exe') and
              hashlib.sha256(cli.read_bytes()).hexdigest() == binaries['Starlight.NativeCommands.exe'],
              'Tested binaries match the package and stayed unchanged during the check')
        report = {'success': True, 'checks': checks}
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        report = {'success': False, 'checks': checks, 'error': str(error)}
    report.update(binaries=binaries, scope='Fresh starter export, content validation, rejection safety and hidden rendering; not human input, visible polish or clean-machine certification')
    (out/'result.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
