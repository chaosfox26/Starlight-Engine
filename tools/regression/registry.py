#!/usr/bin/env python3
"""Committed check registry: resolves a check id to a fixed command, and never launches anything.

resolve(check_id, repo, build_dir, run_folder) returns {'argv', 'cwd', 'env', 'timeoutSeconds', 'evidenceRoot', 'evidenceFiles', 'binaries', 'token', 'command'}
or raises RegistryError. It is the single source of the command the ledger runner executes, so the template cannot drift from what really runs.
Free text from a packet (runner, args, paths) is never an input. Unknown ids, unknown or malformed placeholders, executables with path parts, build roots
that do not resolve inside the build directory, and an evidence root or working directory that leaves its scope all fail closed.
Supported at this stage: exactly one command for every ledger test, and the fixed result format RESULT_FILES. Anything else is refused before anything runs.
Usage: registry.py --self-test | registry.py --check-ledger"""
import json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REGISTRY_PATH = os.path.join(HERE, 'check_registry.json')
SCHEMA = 'starlight-check-registry-1'
IMPLEMENTED_PLACEHOLDERS = ('repo', 'buildDir', 'runFolder')
RESULT_FILES = ['result.json', 'progress.log']
BRACE = re.compile(r'\{([A-Za-z]+)\}')
ID = re.compile(r'^[A-Za-z0-9._:+-]{1,80}$')


class RegistryError(Exception):
    pass


def load(path=REGISTRY_PATH):
    try:
        with open(path, encoding='utf-8') as f:
            registry = json.load(f)
    except (OSError, ValueError) as error:
        raise RegistryError('registry could not be read: %s' % error)
    problems = validate(registry)
    if problems:
        raise RegistryError('registry is invalid: ' + '; '.join(problems))
    return registry


def bare_name(value):
    return isinstance(value, str) and bool(value) and value not in ('.', '..') and not any(ch in value for ch in '/\\:{}') and value == value.strip()


def validate(registry):
    found = []
    if not isinstance(registry, dict) or registry.get('schema') != SCHEMA:
        return ['wrong or missing schema']
    declared = registry.get('placeholders')
    if not isinstance(declared, list) or not all(isinstance(p, str) for p in declared) or not set(declared) <= set(IMPLEMENTED_PLACEHOLDERS):
        return ['placeholders must be a list drawn only from the implemented names %s' % (IMPLEMENTED_PLACEHOLDERS,)]
    commands, checks = registry.get('commands'), registry.get('checks')
    if not isinstance(commands, dict) or not commands:
        found.append('commands is missing or empty')
        commands = {}
    if not isinstance(checks, list) or not checks:
        found.append('checks is missing or empty')
        checks = []

    def template_ok(text):
        if not isinstance(text, str):
            return False
        leftover = BRACE.sub('', text)
        return all(name in declared for name in BRACE.findall(text)) and '{' not in leftover and '}' not in leftover

    for name, c in commands.items():
        if not isinstance(c, dict):
            found.append('command %s is malformed' % name)
            continue
        if not bare_name(c.get('executable')):
            found.append('command %s: executable must be a bare file name' % name)
        if not isinstance(c.get('argsTemplate'), list) or not all(template_ok(a) for a in c['argsTemplate']):
            found.append('command %s: argsTemplate has an unknown, malformed or non-text placeholder' % name)
        for key in ('cwd', 'buildRoot', 'evidenceRoot'):
            if not template_ok(c.get(key)):
                found.append('command %s: %s is missing or has an unknown or malformed placeholder' % (name, key))
        env = c.get('environment', {})
        if not isinstance(env, dict) or not all(isinstance(k, str) and isinstance(v, str) for k, v in env.items()):
            found.append('command %s: environment must be text pairs' % name)
        timeout = c.get('timeoutSeconds')
        if not isinstance(timeout, int) or isinstance(timeout, bool) or not 1 <= timeout <= 7200:
            found.append('command %s: timeoutSeconds must be 1..7200' % name)
        for key in ('evidenceFiles', 'binaries'):
            if not isinstance(c.get(key), list) or not c[key] or not all(bare_name(x) for x in c[key]):
                found.append('command %s: %s must be a non-empty list of bare file names' % (name, key))
        if bare_name(c.get('executable')) and isinstance(c.get('binaries'), list) and c['executable'] not in c['binaries']:
            found.append('command %s: the executable itself must be in binaries so its identity is always recorded' % name)
    seen = set()
    for check in checks:
        if not isinstance(check, dict) or not isinstance(check.get('checkId'), str) or not ID.match(check['checkId']):
            found.append('a check has a missing or unsafe id')
            continue
        if check['checkId'] in seen:
            found.append('duplicate check id %s' % check['checkId'])
        seen.add(check['checkId'])
        if not isinstance(check.get('command'), str) or check['command'] not in commands:
            found.append('check %s names an unknown command' % check['checkId'])
        if not isinstance(check.get('token'), str) or not check['token']:
            found.append('check %s has no token' % check['checkId'])
        if not isinstance(check.get('ledgerEntries'), list) or not check['ledgerEntries'] or not all(isinstance(x, str) for x in check['ledgerEntries']):
            found.append('check %s lists no ledger entries' % check['checkId'])
    return found


def fill(text, values):
    return BRACE.sub(lambda m: values[m.group(1)], text)


def within(path, root):
    try:
        return os.path.commonpath([path, root]) == root
    except ValueError:
        return False


def resolve(check_id, repo, build_dir, run_folder, registry=None):
    registry = registry if registry is not None else load()
    problems = validate(registry)
    if problems:
        raise RegistryError('registry is invalid: ' + '; '.join(problems))
    if not isinstance(check_id, str):
        raise RegistryError('check id must be text')
    check = next((c for c in registry['checks'] if c['checkId'] == check_id), None)
    if check is None:
        raise RegistryError('unknown check id (failing closed): %r' % check_id[:80])
    command = registry['commands'][check['command']]
    values = {'repo': os.path.abspath(repo), 'buildDir': os.path.abspath(build_dir), 'runFolder': os.path.abspath(run_folder)}
    try:
        given_build = os.path.realpath(values['buildDir'])
        build_root = os.path.realpath(fill(command['buildRoot'], values))
        exe = os.path.realpath(os.path.join(build_root, command['executable']))
        evidence_root = os.path.realpath(fill(command['evidenceRoot'], values))
        cwd = os.path.realpath(fill(command['cwd'], values))
        isolated = os.path.realpath(values['runFolder'])
        repo_root = os.path.realpath(values['repo'])
        argv = [exe] + [fill(a, values) for a in command['argsTemplate']]
    except KeyError as error:
        raise RegistryError('unresolved placeholder %s' % error)
    if not (within(exe, build_root) and within(build_root, given_build)):
        raise RegistryError('the executable does not resolve inside the registered build root within the given build directory')
    if not within(evidence_root, isolated):
        raise RegistryError('the evidence root leaves the isolated run folder')
    if not (within(cwd, repo_root) or within(cwd, isolated)):
        raise RegistryError('the working directory leaves the repository and the run folder')
    return {
        'checkId': check_id, 'token': check['token'], 'command': check['command'], 'argv': argv, 'cwd': cwd, 'env': dict(command.get('environment', {})),
        'timeoutSeconds': command['timeoutSeconds'], 'evidenceRoot': evidence_root, 'evidenceFiles': list(command['evidenceFiles']), 'binaries': list(command['binaries']),
    }


def check_ledger(ledger_path=None, registry=None):
    """Ledger and registry must agree exactly, and exactly one command may serve every ledger test (the runner supports no scheduler)."""
    registry = registry if registry is not None else load()
    problems = validate(registry)
    if problems:
        return ['registry is invalid: ' + '; '.join(problems)]
    ledger = json.load(open(ledger_path or os.path.join(HERE, 'ledger.json'), encoding='utf-8'))
    found, used, commands_used = [], set(), set()
    by_id = {c['checkId']: c for c in registry['checks']}
    entry_ids = {e['id'] for e in ledger['entries']}
    tests_by_entry = {e['id']: {t['token'] for t in e['tests']} for e in ledger['entries']}
    for entry in ledger['entries']:
        for test in entry['tests']:
            check = by_id.get(test['token'])
            if check is None:
                found.append('ledger entry %s uses unregistered check %s' % (entry['id'], test['token']))
                continue
            if entry['id'] not in check['ledgerEntries'] or check['token'] != test['token']:
                found.append('registry check %s does not serve ledger entry %s' % (test['token'], entry['id']))
            elif registry['commands'][check['command']]['executable'] != test.get('executable'):
                found.append('ledger entry %s names a different executable than the registry' % entry['id'])
            used.add(test['token'])
            commands_used.add(check['command'])
    for check in registry['checks']:
        if check['checkId'] not in used:
            found.append('registered check %s is used by no ledger entry' % check['checkId'])
        for member in check['ledgerEntries']:
            if member not in entry_ids:
                found.append('registry check %s lists ledger entry %s, which does not exist' % (check['checkId'], member))
            elif check['checkId'] not in tests_by_entry[member]:
                found.append('registry check %s lists ledger entry %s, which does not use it' % (check['checkId'], member))
    if len(commands_used) > 1:
        found.append('unsupported: the ledger uses %d different commands (%s); the runner supports exactly one' % (len(commands_used), ', '.join(sorted(commands_used))))
    for name in commands_used:
        if registry['commands'][name]['evidenceFiles'] != RESULT_FILES:
            found.append('command %s: only the fixed result format %s is supported' % (name, RESULT_FILES))
    return found


def undercounted(ledger, repo=None):
    """Ledger tests whose minChecks is below the number of check-name literals that carry their token in the core suite sources. A run that stops after minChecks
    matching checks reads VERIFIED, so a minimum below what the author wrote leaves the remaining checks unprotected. Names built in a loop make the real count higher
    than the literal count, never lower, so this can only under-report."""
    folder = os.path.join(repo or os.path.dirname(os.path.dirname(HERE)), 'tools', 'checks')
    text = ''
    for name in sorted(os.listdir(folder)):
        if name.endswith('.inl') or name == 'core_checks.cpp':
            with open(os.path.join(folder, name), encoding='utf-8', errors='replace') as f:
                text += f.read()
    found = []
    for entry in ledger['entries']:
        for test in entry['tests']:
            have = len(re.findall('"' + re.escape(test['token']), text))
            if have > test.get('minChecks', 1):
                found.append('ledger entry %s: minChecks is %d but the sources hold %d checks named %s' % (entry['id'], test.get('minChecks', 1), have, test['token']))
    return found


def command_for_ledger(ledger, registry):
    """The one registered command that serves every ledger test. check_ledger must already have passed."""
    by_id = {c['checkId']: c for c in registry['checks']}
    names = {by_id[t['token']]['command'] for e in ledger['entries'] for t in e['tests']}
    if len(names) != 1:
        raise RegistryError('exactly one command must serve every ledger test')
    return registry['commands'][names.pop()]


def self_test():
    import copy, tempfile
    bad = 0

    def expect(name, condition):
        nonlocal bad
        print('  %s %s' % ('ok ' if condition else 'BAD', name))
        bad += not condition

    def refused(registry, check_id='CC-01:', build=None, run=None):
        try:
            resolve(check_id, HERE, build, run, registry)
            return False
        except RegistryError:
            return True
        except Exception:
            return False

    registry = load()
    ledger = json.load(open(os.path.join(HERE, 'ledger.json'), encoding='utf-8'))
    with tempfile.TemporaryDirectory() as build, tempfile.TemporaryDirectory() as run:
        got = resolve('CC-01:', HERE, build, run, registry)
        expect('a registered id resolves to a fixed command', got['argv'][0].endswith('Starlight.NativeCoreChecks.exe') and got['argv'][1] == os.path.abspath(run) and got['timeoutSeconds'] == 1800)
        expect('resolution did not create or run anything', not os.listdir(build) and not os.listdir(run))
        for bad_id in ('NOPE:', '', 'CC-01:; calc', '../x', None, 5, ['CC-01:']):
            expect('unknown id fails closed: %r' % (bad_id,), refused(registry, bad_id, build, run))

        def mutate(label, change):
            broken = copy.deepcopy(registry)
            change(broken)
            expect('%s is refused by validate' % label, bool(validate(broken)))
            expect('%s is refused by resolve (controlled error)' % label, refused(broken, 'CC-01:', build, run))

        cmd = lambda r: r['commands']['core-suite']
        mutate('executable with path parts', lambda r: cmd(r).__setitem__('executable', '..\\evil.exe'))
        mutate('executable named dot-dot', lambda r: cmd(r).__setitem__('executable', '..'))
        mutate('unknown placeholder declared and used', lambda r: (r['placeholders'].append('packetArgs'), cmd(r)['argsTemplate'].append('{packetArgs}')))
        mutate('malformed placeholder {packetArgs1}', lambda r: cmd(r)['argsTemplate'].append('{packetArgs1}'))
        mutate('stray brace in a template', lambda r: cmd(r)['argsTemplate'].append('{repo'))
        mutate('placeholder name not declared', lambda r: r.__setitem__('placeholders', ['repo']))
        mutate('duplicate check id', lambda r: r['checks'].append(copy.deepcopy(r['checks'][0])))
        mutate('bad timeout', lambda r: cmd(r).__setitem__('timeoutSeconds', 0))
        mutate('non-text environment', lambda r: cmd(r).__setitem__('environment', {'A': 1}))
        mutate('command given as a list', lambda r: r['checks'][0].__setitem__('command', []))
        mutate('command given as an object', lambda r: r['checks'][0].__setitem__('command', {}))
        mutate('dot-dot evidence file name', lambda r: cmd(r)['evidenceFiles'].append('..'))
        mutate('dot binary name', lambda r: cmd(r)['binaries'].append('.'))
        mutate('executable missing from binaries', lambda r: cmd(r).__setitem__('binaries', ['Starlight.Physics.dll']))
        mutate('empty binaries', lambda r: cmd(r).__setitem__('binaries', []))
        for label, change in (('a build root that escapes the build directory', lambda r: cmd(r).__setitem__('buildRoot', '{buildDir}/../..')),
                              ('an evidence root outside the run folder', lambda r: cmd(r).__setitem__('evidenceRoot', '{runFolder}/..')),
                              ('a working directory outside the repository and run folder', lambda r: cmd(r).__setitem__('cwd', '{buildDir}'))):
            broken = copy.deepcopy(registry)
            change(broken)
            expect('%s is refused' % label, refused(broken, 'CC-01:', build, run))
    problems = check_ledger(registry=registry)
    expect('registry and ledger agree: %s' % (problems or 'no differences'), not problems)
    fake = copy.deepcopy(ledger)
    fake['entries'][0]['tests'][0]['token'] = 'UNREGISTERED:'
    with tempfile.NamedTemporaryFile('w', suffix='.json', delete=False, encoding='utf-8') as f:
        json.dump(fake, f)
        path = f.name
    try:
        expect('a ledger test with an unregistered token is caught', bool(check_ledger(path, registry)))
    finally:
        os.remove(path)
    two = copy.deepcopy(registry)
    two['commands']['other-suite'] = copy.deepcopy(two['commands']['core-suite'])
    two['commands']['other-suite']['argsTemplate'] = ['{runFolder}', '--other']
    two['checks'][1]['command'] = 'other-suite'
    problems = check_ledger(registry=two)
    expect('a second command serving ledger tests is refused as unsupported', any('unsupported' in p for p in problems))
    ghost = copy.deepcopy(registry)
    ghost['checks'][0]['ledgerEntries'].append('NO-SUCH-ENTRY')
    expect('a listed ledger entry that does not exist is caught', any('does not exist' in p for p in check_ledger(registry=ghost)))
    wrong = copy.deepcopy(registry)
    wrong['checks'][0]['ledgerEntries'].append('CUBE-BOUNDS')
    expect('a listed ledger entry that does not use the check is caught', any('does not use it' in p for p in check_ledger(registry=wrong)))
    fmt = copy.deepcopy(registry)
    fmt['commands']['core-suite']['evidenceFiles'] = ['result.json']
    expect('an unsupported result format is refused', any('fixed result format' in p for p in check_ledger(registry=fmt)))
    expect('every ledger minChecks covers the check names in the sources: %s' % (undercounted(ledger) or 'no differences'), not undercounted(ledger))
    low = copy.deepcopy(ledger)
    low['entries'][0]['tests'][0]['minChecks'] = 1
    expect('a minChecks below the number of named checks is caught', any(low['entries'][0]['id'] in p for p in undercounted(low)))
    print('REGISTRY SELF-TEST', 'PASSED' if not bad else 'FAILED (%d)' % bad)
    return 1 if bad else 0


if __name__ == '__main__':
    if '--self-test' in sys.argv:
        sys.exit(self_test())
    if '--check-ledger' in sys.argv:
        problems = check_ledger() + undercounted(json.load(open(os.path.join(HERE, 'ledger.json'), encoding='utf-8')))
        print('\n'.join(problems) if problems else 'registry and ledger agree')
        sys.exit(1 if problems else 0)
    print(__doc__)
