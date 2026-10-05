#!/usr/bin/env python3
"""Fix-ledger runner, version 1 (after-build, report-only).

Runs the native core checks, maps each ledger entry to the checks that carry its token, and writes per-entry states keyed by the build identity
of the binaries that actually ran. Nothing is inferred from source text, an exit code alone, or results from a different binary.

States: VERIFIED, REGRESSED, NOT-CHECKED (the suite did not reach or finish the entry's checks), COVERAGE-ERROR (a finished suite has fewer matching
checks than the entry requires), SKIPPED (the runner could not start the suite).
Exit code: 0 all VERIFIED, 1 any REGRESSED or COVERAGE-ERROR, 2 anything else not verified.
Usage: run_ledger.py --build-dir <folder with Starlight.NativeCoreChecks.exe>        |        run_ledger.py --self-test
"""
import argparse, hashlib, json, os, subprocess, sys, time

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()

def git(repo, *args):
    try:
        return subprocess.run(['git', '-C', repo] + list(args), capture_output=True, text=True, timeout=60).stdout.strip()
    except Exception:
        return ''

def evaluate_test(test, suite):
    """State of one ledger test given what the suite did. suite keys: started, completed, success, error, checks (names of passed checks), skippedReason."""
    token = test['token']
    need = test.get('minChecks', 1)
    matched = [c for c in suite['checks'] if c.startswith(token)]
    failing = suite['error'] is not None and str(suite['error']).startswith(token)
    if not suite['started']:
        return 'SKIPPED', suite.get('skippedReason'), len(matched)
    if failing:
        return 'REGRESSED', 'check failed: %s' % suite['error'], len(matched)
    if not suite['completed']:
        # no result file: crash, hang or kill; the progress log names every check that passed before the stop
        if 0 < len(matched) < need and suite['checks'] and suite['checks'][-1].startswith(token):
            return 'REGRESSED', 'the suite stopped (crash, hang or kill) inside the checks of this entry, after %d of %d: %s' % (len(matched), need, suite['error']), len(matched)
        if len(matched) >= need:
            return 'VERIFIED', '%d matching checks passed before the suite stopped (%s)' % (len(matched), suite['error']), len(matched)
        return 'NOT-CHECKED', 'the suite stopped before the checks of this entry ran: %d of %d matching checks (%s)' % (len(matched), need, suite['error']), len(matched)
    if suite['success']:
        if len(matched) >= need:
            return 'VERIFIED', '%d matching checks passed' % len(matched), len(matched)
        return 'COVERAGE-ERROR', 'only %d checks carry token %s, %d required' % (len(matched), token, need), len(matched)
    if len(matched) >= need:
        return 'VERIFIED', '%d matching checks passed (the suite failed on a different check: %s)' % (len(matched), suite['error']), len(matched)
    return 'NOT-CHECKED', 'the suite stopped on another check (%s) before these ran: %d of %d matching checks' % (suite['error'], len(matched), need), len(matched)

ORDER = ['REGRESSED', 'COVERAGE-ERROR', 'NOT-CHECKED', 'SKIPPED', 'VERIFIED']

def evaluate_entry(entry, suite):
    states = []
    for test in entry['tests']:
        state, why, matched = evaluate_test(test, suite)
        states.append({'token': test['token'], 'state': state, 'reason': why, 'matched': matched})
    return sorted(states, key=lambda s: ORDER.index(s['state']))[0]['state'], states

def transitions_for(entries, previous):
    out = []
    for e in entries:
        before = previous.get(e['id'])
        if e['state'] == 'REGRESSED' and before != 'REGRESSED':
            out.append('NEW REGRESSION  %s (%s)' % (e['id'], e['tests'][0]['reason']))
        elif e['state'] == 'VERIFIED' and before == 'REGRESSED':
            out.append('RECOVERED       %s' % e['id'])
        elif e['state'] == 'VERIFIED' and before in ('NOT-CHECKED', 'COVERAGE-ERROR', 'SKIPPED'):
            out.append('COVERAGE RESTORED %s' % e['id'])
        elif e['state'] == 'VERIFIED' and before is None:
            out.append('FIRST VERIFIED  %s' % e['id'])
        elif e['state'] in ('NOT-CHECKED', 'COVERAGE-ERROR', 'SKIPPED') and before == 'VERIFIED':
            out.append('LOST COVERAGE   %s (%s)' % (e['id'], e['tests'][0]['reason']))
    return out

def self_test():
    entry = {'id': 'X', 'tests': [{'token': 'X:', 'minChecks': 3}]}
    base = {'started': True, 'completed': True, 'success': True, 'error': None, 'checks': ['a', 'X: 1', 'X: 2', 'X: 3', 'b']}
    cases = [
        ('finished and passed', dict(base), 'VERIFIED'),
        ('finished, too few tagged checks', dict(base, checks=['a', 'X: 1']), 'COVERAGE-ERROR'),
        ('finished, no tagged checks', dict(base, checks=['a', 'b']), 'COVERAGE-ERROR'),
        ('failed on its own check', dict(base, success=False, error='X: 2 broke', checks=['a', 'X: 1']), 'REGRESSED'),
        ('crash inside its checks', dict(base, completed=False, success=None, error='suite did not finish; last passed check: X: 2', checks=['a', 'X: 1', 'X: 2']), 'REGRESSED'),
        ('crash before its checks', dict(base, completed=False, success=None, error='suite did not finish; last passed check: a', checks=['a']), 'NOT-CHECKED'),
        ('crash after its checks', dict(base, completed=False, success=None, error='suite did not finish; last passed check: b', checks=['a', 'X: 1', 'X: 2', 'X: 3', 'b']), 'VERIFIED'),
        ('suite stopped on another check', dict(base, success=False, error='Y: other', checks=['a']), 'NOT-CHECKED'),
        ('suite failed elsewhere after its checks', dict(base, success=False, error='Y: other', checks=['a', 'X: 1', 'X: 2', 'X: 3']), 'VERIFIED'),
        ('suite could not start', dict(base, started=False, completed=False, success=None, checks=[], skippedReason='no exe'), 'SKIPPED'),
    ]
    bad = 0
    for name, suite, want in cases:
        got, _ = evaluate_entry(entry, suite)
        flag = 'ok ' if got == want else 'BAD'
        bad += got != want
        print('  %s %-42s expected %-14s got %s' % (flag, name, want, got))
    notes = transitions_for([{'id': 'X', 'state': 'VERIFIED', 'tests': [{'reason': 'r'}]}], {'X': 'NOT-CHECKED'})
    print('  %s coverage restored notice: %s' % ('ok ' if notes and notes[0].startswith('COVERAGE RESTORED') else 'BAD', notes))
    bad += not (notes and notes[0].startswith('COVERAGE RESTORED'))
    notes = transitions_for([{'id': 'X', 'state': 'REGRESSED', 'tests': [{'reason': 'r'}]}], {'X': 'VERIFIED'})
    print('  %s new regression notice: %s' % ('ok ' if notes and notes[0].startswith('NEW REGRESSION') else 'BAD', notes))
    bad += not (notes and notes[0].startswith('NEW REGRESSION'))
    print('SELF-TEST', 'PASSED' if not bad else 'FAILED (%d)' % bad)
    return 1 if bad else 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=r'D:\projects\Starlight-Engine')
    ap.add_argument('--build-dir')
    ap.add_argument('--ledger', default=None)
    ap.add_argument('--results', default=r'D:\projects\_recovery\regression-results')
    ap.add_argument('--timeout', type=int, default=1800)
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.build_dir:
        ap.error('--build-dir is required')

    ledger_path = args.ledger or os.path.join(args.repo, 'tools', 'regression', 'ledger.json')
    ledger = json.load(open(ledger_path, encoding='utf-8'))
    os.makedirs(args.results, exist_ok=True)

    exe = os.path.join(args.build_dir, 'Starlight.NativeCoreChecks.exe')
    identity = {
        'gitHead': git(args.repo, 'rev-parse', 'HEAD'),
        'dirtyTrackedFiles': len([l for l in git(args.repo, 'status', '--porcelain', '--untracked-files=no').splitlines() if l.strip()]),
        'binaries': {},
    }
    for name in ('Starlight.NativeCoreChecks.exe', 'Starlight.Viewport.dll', 'Starlight.Physics.dll'):
        p = os.path.join(args.build_dir, name)
        if os.path.exists(p):
            identity['binaries'][name] = {'sha256': sha256(p), 'bytes': os.path.getsize(p)}
    build_id = (identity['gitHead'][:12] or 'nogit') + ('+dirty' if identity['dirtyTrackedFiles'] else '') + '-' + identity['binaries'].get('Starlight.NativeCoreChecks.exe', {}).get('sha256', 'nobinary')[:10]

    run_folder = os.path.join(args.results, 'runs', build_id + '-' + time.strftime('%Y%m%d-%H%M%S'))
    os.makedirs(run_folder, exist_ok=True)
    result_json = os.path.join(run_folder, 'result.json')
    progress_path = os.path.join(run_folder, 'progress.log')
    suite = {'started': False, 'exitCode': None, 'timedOut': False, 'completed': False, 'success': None, 'error': None, 'checks': []}
    if not os.path.exists(exe):
        suite['skippedReason'] = 'Starlight.NativeCoreChecks.exe not found in the build directory'
    else:
        suite['started'] = True
        try:
            proc = subprocess.run([exe, run_folder, os.path.join(args.repo, 'migration', 'geometry-reference.json')], cwd=args.repo,
                                  capture_output=True, text=True, timeout=args.timeout, env=dict(os.environ, STARLIGHT_SUPPRESS_DIALOGS='1'))
            suite['exitCode'] = proc.returncode
            suite['stdoutTail'] = proc.stdout[-400:]
            suite['stderrTail'] = proc.stderr[-400:]
        except subprocess.TimeoutExpired:
            suite['timedOut'] = True
        if os.path.exists(result_json):
            try:
                data = json.load(open(result_json, encoding='utf-8'))
                suite['completed'] = True
                suite['success'] = bool(data.get('success'))
                suite['error'] = data.get('error')
                suite['checks'] = data.get('checks', [])
                suite['schema'] = data.get('schema')
            except Exception as e:
                suite['error'] = 'result.json unreadable: %s' % e
        elif os.path.exists(progress_path):
            suite['checks'] = [l.strip() for l in open(progress_path, encoding='utf-8', errors='replace') if l.strip()]
            suite['error'] = 'suite did not finish; last passed check: %s' % (suite['checks'][-1] if suite['checks'] else 'none')
        else:
            suite['error'] = 'suite did not finish and left no progress log'

    entries = []
    for entry in ledger['entries']:
        state, states = evaluate_entry(entry, suite)
        entries.append({'id': entry['id'], 'title': entry['title'], 'class': entry['class'], 'state': state, 'tests': states,
                        'testStrength': entry.get('testStrength'), 'investigate': entry.get('investigate', [])})

    report = {'schema': 'starlight-ledger-results-1', 'createdUtc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'buildId': build_id,
              'identity': identity, 'suite': {k: v for k, v in suite.items() if k != 'checks'}, 'suiteCheckCount': len(suite['checks']), 'entries': entries}

    latest_path = os.path.join(args.results, 'latest.json')
    previous = {}
    if os.path.exists(latest_path):
        try:
            previous = {e['id']: e['state'] for e in json.load(open(latest_path, encoding='utf-8')).get('entries', [])}
        except Exception:
            previous = {}
    report['transitions'] = transitions_for(entries, previous)
    json.dump(report, open(os.path.join(args.results, build_id + '.json'), 'w', encoding='utf-8'), indent=1)
    json.dump(report, open(latest_path, 'w', encoding='utf-8'), indent=1)
    if report['transitions']:
        with open(os.path.join(args.results, 'notices.log'), 'a', encoding='utf-8') as f:
            for t in report['transitions']:
                f.write('%s  build %s  %s\n' % (report['createdUtc'], build_id, t))

    print('build identity:', build_id, '| suite checks recorded:', len(suite['checks']), '| suite success:', suite['success'], '| exit', suite['exitCode'])
    for e in entries:
        print('  %-14s %-14s %s' % (e['id'], e['state'], e['tests'][0]['reason']))
    for t in report['transitions']:
        print('NOTICE:', t)
    states = {e['state'] for e in entries}
    if 'REGRESSED' in states or 'COVERAGE-ERROR' in states:
        return 1
    return 0 if states == {'VERIFIED'} else 2

if __name__ == '__main__':
    sys.exit(main())
