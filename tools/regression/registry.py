#!/usr/bin/env python3
"""Committed check registry: resolves a check id to a fixed command, and never launches anything.

resolve(check_id, repo, build_dir, run_folder) returns {'argv', 'cwd', 'env', 'timeoutSeconds', 'evidenceRoot', 'evidenceFiles', 'binaries', 'token', 'command'}
or raises RegistryError. It is the single source of the command the ledger runner executes, so the template cannot drift from what really runs.
Free text from a packet (runner, args, paths) is never an input. Unknown ids, unknown placeholders, executables with path parts, and build roots that do
not resolve inside the build directory all fail closed.
Usage: registry.py --self-test | registry.py --check-ledger"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REGISTRY_PATH = os.path.join(HERE, 'check_registry.json')
SCHEMA = 'starlight-check-registry-1'
PLACEHOLDER = re.compile(r'\{([A-Za-z]+)\}')
ID = re.compile(r'^[A-Za-z0-9._:+-]{1,80}$')


class RegistryError(Exception):
    pass


def load(path=REGISTRY_PATH):
    with open(path, encoding='utf-8') as f:
        registry = json.load(f)
    problems = validate(registry)
    if problems:
        raise RegistryError('registry is invalid: ' + '; '.join(problems))
    return registry


def validate(registry):
    found = []
    if not isinstance(registry, dict) or registry.get('schema') != SCHEMA:
        return ['wrong or missing schema']
    allowed = registry.get('placeholders')
    if not isinstance(allowed, list) or not all(isinstance(p, str) for p in allowed):
        return ['placeholders must be a list of names']
    commands, checks = registry.get('commands'), registry.get('checks')
    if not isinstance(commands, dict) or not commands:
        found.append('commands is missing or empty')
        commands = {}
    if not isinstance(checks, list) or not checks:
        found.append('checks is missing or empty')
        checks = []

    def template_ok(text):
        return isinstance(text, str) and all(name in allowed for name in PLACEHOLDER.findall(text))

    for name, c in commands.items():
        if not isinstance(c, dict):
            found.append('command %s is malformed' % name)
            continue
        exe = c.get('executable')
        if not isinstance(exe, str) or not exe or any(ch in exe for ch in '/\\:') or exe in ('.', '..'):
            found.append('command %s: executable must be a bare file name' % name)
        if not isinstance(c.get('argsTemplate'), list) or not all(template_ok(a) for a in c['argsTemplate']):
            found.append('command %s: argsTemplate uses an unknown placeholder or is not text' % name)
        for key in ('cwd', 'buildRoot', 'evidenceRoot'):
            if not template_ok(c.get(key)):
                found.append('command %s: %s is missing or uses an unknown placeholder' % (name, key))
        env = c.get('environment', {})
        if not isinstance(env, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in env.items()):
            found.append('command %s: environment must be text pairs' % name)
        timeout = c.get('timeoutSeconds')
        if not isinstance(timeout, int) or isinstance(timeout, bool) or not 1 <= timeout <= 7200:
            found.append('command %s: timeoutSeconds must be 1..7200' % name)
        for key in ('evidenceFiles', 'binaries'):
            if not isinstance(c.get(key), list) or not all(isinstance(x, str) and x and not any(ch in x for ch in '/\\:') for x in c[key]):
                found.append('command %s: %s must be bare file names' % (name, key))
    seen = set()
    for check in checks:
        if not isinstance(check, dict) or not isinstance(check.get('checkId'), str) or not ID.match(check['checkId']):
            found.append('a check has a missing or unsafe id')
            continue
        if check['checkId'] in seen:
            found.append('duplicate check id %s' % check['checkId'])
        seen.add(check['checkId'])
        if check.get('command') not in commands:
            found.append('check %s names an unknown command' % check['checkId'])
        if not isinstance(check.get('token'), str) or not check['token']:
            found.append('check %s has no token' % check['checkId'])
        if not isinstance(check.get('ledgerEntries'), list) or not check['ledgerEntries']:
            found.append('check %s lists no ledger entries' % check['checkId'])
    return found


def fill(text, values):
    return PLACEHOLDER.sub(lambda m: values[m.group(1)], text)


def resolve(check_id, repo, build_dir, run_folder, registry=None):
    registry = registry or load()
    if not isinstance(check_id, str):
        raise RegistryError('check id must be text')
    check = next((c for c in registry['checks'] if c['checkId'] == check_id), None)
    if check is None:
        raise RegistryError('unknown check id (failing closed): %r' % check_id[:80])
    command = registry['commands'][check['command']]
    values = {'repo': os.path.abspath(repo), 'buildDir': os.path.abspath(build_dir), 'runFolder': os.path.abspath(run_folder)}
    given_build = os.path.realpath(values['buildDir'])
    build_root = os.path.realpath(fill(command['buildRoot'], values))
    exe = os.path.realpath(os.path.join(build_root, command['executable']))
    try:
        inside = os.path.commonpath([exe, build_root]) == build_root and os.path.commonpath([build_root, given_build]) == given_build
    except ValueError:
        inside = False
    if not inside:
        raise RegistryError('the executable does not resolve inside the registered build root within the given build directory')
    return {
        'checkId': check_id, 'token': check['token'], 'command': check['command'],
        'argv': [exe] + [fill(a, values) for a in command['argsTemplate']],
        'cwd': fill(command['cwd'], values), 'env': dict(command.get('environment', {})), 'timeoutSeconds': command['timeoutSeconds'],
        'evidenceRoot': fill(command['evidenceRoot'], values), 'evidenceFiles': list(command['evidenceFiles']), 'binaries': list(command['binaries']),
    }


def check_ledger(ledger_path=None, registry=None):
    """Every ledger test token must be a registered check for that ledger entry, and every registered check must serve a ledger entry."""
    registry = registry or load()
    ledger = json.load(open(ledger_path or os.path.join(HERE, 'ledger.json'), encoding='utf-8'))
    problems, used = [], set()
    by_id = {c['checkId']: c for c in registry['checks']}
    for entry in ledger['entries']:
        for test in entry['tests']:
            check = by_id.get(test['token'])
            if check is None:
                problems.append('ledger entry %s uses unregistered check %s' % (entry['id'], test['token']))
            elif entry['id'] not in check['ledgerEntries'] or check['token'] != test['token']:
                problems.append('registry check %s does not serve ledger entry %s' % (test['token'], entry['id']))
            elif check['command'] and registry['commands'][check['command']]['executable'] != test.get('executable'):
                problems.append('ledger entry %s names a different executable than the registry' % entry['id'])
            used.add(test['token'])
    for check in registry['checks']:
        if check['checkId'] not in used:
            problems.append('registered check %s is used by no ledger entry' % check['checkId'])
    return problems


def self_test():
    import copy, tempfile
    bad = 0

    def expect(name, condition):
        nonlocal bad
        print('  %s %s' % ('ok ' if condition else 'BAD', name))
        bad += not condition

    registry = load()
    with tempfile.TemporaryDirectory() as build, tempfile.TemporaryDirectory() as run:
        got = resolve('CC-01:', HERE, build, run, registry)
        expect('a registered id resolves to a fixed command', got['argv'][0].endswith('Starlight.NativeCoreChecks.exe') and got['argv'][1] == os.path.abspath(run) and got['timeoutSeconds'] == 1800)
        expect('resolution did not create or run anything', not os.listdir(build) and not os.listdir(run))
        for bad_id in ('NOPE:', '', 'CC-01:; calc', '../x', None, 5):
            try:
                resolve(bad_id, HERE, build, run, registry)
                expect('unknown id fails closed: %r' % (bad_id,), False)
            except RegistryError:
                expect('unknown id fails closed: %r' % (bad_id,), True)
        broken = copy.deepcopy(registry)
        broken['commands']['core-suite']['executable'] = '..\\evil.exe'
        expect('executable with path parts is rejected', bool(validate(broken)))
        broken = copy.deepcopy(registry)
        broken['commands']['core-suite']['argsTemplate'].append('{packetArgs}')
        expect('unknown placeholder is rejected', bool(validate(broken)))
        broken = copy.deepcopy(registry)
        broken['checks'].append(copy.deepcopy(broken['checks'][0]))
        expect('duplicate check id is rejected', bool(validate(broken)))
        broken = copy.deepcopy(registry)
        broken['commands']['core-suite']['timeoutSeconds'] = 0
        expect('bad timeout is rejected', bool(validate(broken)))
        broken = copy.deepcopy(registry)
        broken['commands']['core-suite']['environment'] = {'A': 1}
        expect('non-text environment is rejected', bool(validate(broken)))
        broken = copy.deepcopy(registry)
        broken['commands']['core-suite']['buildRoot'] = '{buildDir}/../..'
        try:
            resolve('CC-01:', HERE, build, run, broken)
            expect('a build root that escapes the build directory is rejected', False)
        except RegistryError:
            expect('a build root that escapes the build directory is rejected', True)
    problems = check_ledger(registry=registry)
    expect('registry and ledger agree: %s' % (problems or 'no differences'), not problems)
    ledger = json.load(open(os.path.join(HERE, 'ledger.json'), encoding='utf-8'))
    fake = copy.deepcopy(ledger)
    fake['entries'][0]['tests'][0]['token'] = 'UNREGISTERED:'
    with tempfile.NamedTemporaryFile('w', suffix='.json', delete=False, encoding='utf-8') as f:
        json.dump(fake, f)
        path = f.name
    try:
        expect('a ledger test with an unregistered token is caught', bool(check_ledger(path, registry)))
    finally:
        os.remove(path)
    print('REGISTRY SELF-TEST', 'PASSED' if not bad else 'FAILED (%d)' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    if '--self-test' in sys.argv:
        sys.exit(self_test())
    if '--check-ledger' in sys.argv:
        found = check_ledger()
        print('\n'.join(found) if found else 'registry and ledger agree')
        sys.exit(1 if found else 0)
    print(__doc__)
