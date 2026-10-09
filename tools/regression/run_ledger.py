#!/usr/bin/env python3
"""Fix-ledger runner, version 1 (after-build, report-only).

Runs the native core checks, maps each ledger entry to the checks that carry its token, and writes per-entry states keyed by the build identity
of the binaries that actually ran. Nothing is inferred from source text, an exit code alone, or results from a different binary.

States: VERIFIED, REGRESSED, NOT-CHECKED (the suite did not reach or finish the entry's checks), COVERAGE-ERROR (a finished suite has fewer matching
checks than the entry requires), SKIPPED (the runner could not start the suite).
Exit code: 0 all VERIFIED, 1 any REGRESSED or COVERAGE-ERROR, 2 anything else not verified.
Usage: run_ledger.py --build-dir <folder with Starlight.NativeCoreChecks.exe>        |        run_ledger.py --self-test
"""
import argparse, hashlib, json, os, subprocess, sys, time, uuid
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import registry
import verify_all

def dirty_digest(repo):
    """Hash of every uncommitted change to tracked files, so two different dirty trees at one HEAD get different build ids."""
    try:
        run = subprocess.run(['git', '-C', repo, 'diff', 'HEAD'], capture_output=True, timeout=120)
        return hashlib.sha256(run.stdout).hexdigest() if run.returncode == 0 else 'unavailable'
    except Exception:
        return 'unavailable'

def configured_from(build_dir):
    """The source folder the build tree next to the executables folder was configured from (its CMakeCache.txt), or None when there is no cache."""
    cache = os.path.join(os.path.dirname(os.path.abspath(build_dir).rstrip('\\/')), 'CMakeCache.txt')
    try:
        with open(cache, encoding='utf-8', errors='replace') as f:
            for line in f:
                if line.startswith('CMAKE_HOME_DIRECTORY:'):
                    return line.split('=', 1)[1].strip()
    except OSError:
        pass
    return None

def same_folder(a, b):
    return os.path.normcase(os.path.realpath(a)) == os.path.normcase(os.path.realpath(b))

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()

def publish_json(path, data):
    """Write to a temporary file in the same folder, then replace the target, so a reader never sees a half-written result."""
    temp = path + '.tmp'
    with open(temp, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=1)
        f.flush()
        os.fsync(f.fileno())
    os.replace(temp, path)

def archive_report(results_dir, report):
    """Keep every run's report in history/<runId>.json. Never replaces a file: latest.json and <buildId>.json are overwritten by a later run of the same
    build, so without this a weaker run (for example one with the GPU checks skipped) would erase the record of a fuller one."""
    folder = os.path.join(results_dir, 'history')
    os.makedirs(folder, exist_ok=True)
    path = os.path.join(folder, report['runId'] + '.json')
    with open(path, 'x', encoding='utf-8') as f:
        json.dump(report, f, indent=1)
        f.flush()
        os.fsync(f.fileno())
    return path

def git(repo, *args):
    """Output of a git command, or '' when it fails (git can print a word such as HEAD to stdout and still exit with an error)."""
    try:
        run = subprocess.run(['git', '-C', repo] + list(args), capture_output=True, text=True, timeout=60)
    except Exception:
        return ''
    return run.stdout.strip() if run.returncode == 0 else ''

def child_environment(extra):
    return {k: v for k, v in dict(os.environ, **extra).items() if k != 'STARLIGHT_CHECK_GROUPS'}

def apply_result(suite, data):
    suite['completed'] = True
    suite['success'] = data.get('success') is True and not data.get('filtered')
    suite['error'] = data.get('error') or ('filtered run (STARLIGHT_CHECK_GROUPS): not a full pass' if data.get('filtered') else None)
    if suite['success'] and suite.get('exitCode') != 0:
        suite['success'] = False
        suite['error'] = 'the suite wrote success but exited with code %s' % suite.get('exitCode')
    suite['checks'] = data.get('checks', [])
    suite['schema'] = data.get('schema')

def settle_exit(suite):
    """A result.json that says success is not a pass when the process then crashed, hung or was killed at exit."""
    if suite['completed'] and (suite.get('timedOut') or suite.get('exitCode') not in (None, 0)):
        suite['success'] = False
        suite['error'] = suite['error'] or ('timed out after the result was written' if suite.get('timedOut') else 'process exited with code %s after the result was written' % suite['exitCode'])

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

def exit_code(states, suite):
    """0 only when every entry is VERIFIED and the suite itself passed: a suite that timed out or crashed after its checks is not a pass."""
    if 'REGRESSED' in states or 'COVERAGE-ERROR' in states:
        return 1
    return 0 if states == {'VERIFIED'} and suite.get('success') is True else 2

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

PACKET_SCHEMA = 'starlight-failure-packet-1'
CLEAN_NEEDED = 2

def failure_kind(entry, suite):
    """What kind of failure this is, from facts the runner recorded. Timer or kill outcomes are never treated as a deterministic defect."""
    state = entry['state']
    if state == 'SKIPPED':
        return 'missing-executable', 'ENVIRONMENTAL'
    if state == 'NOT-CHECKED':
        return 'not-reached', 'INCOMPLETE'
    if state == 'COVERAGE-ERROR':
        return 'coverage', 'INCOMPLETE'
    if state == 'REGRESSED':
        if suite.get('completed'):
            return 'check-failed', 'DEFECT-CONFIRMED'
        if suite.get('timedOut'):
            return 'timeout-or-kill', 'INCOMPLETE'
        return 'crash', 'DEFECT-CONFIRMED'
    return 'unknown', 'INCOMPLETE'

def evidence_records(run_folder, results_dir, names):
    records = []
    for name in names:
        path = os.path.join(run_folder, name)
        if os.path.isfile(path):
            records.append({'path': os.path.relpath(path, results_dir).replace(os.sep, '/'), 'sha256': sha256(path), 'bytes': os.path.getsize(path)})
    return records

def evidence_digest(records):
    h = hashlib.sha256()
    for r in sorted(records, key=lambda r: r['path']):
        h.update(r['path'].encode('utf-8'))
        h.update(r['sha256'].encode())
    return h.hexdigest()

def make_packets(entries, report, run_folder, results_dir, evidence_names=('result.json', 'progress.log')):
    """One packet per entry that is not VERIFIED, one immutable file per run. The packet is data: the reproduction names allowlisted check tokens, never a
    shell command. Nothing here repairs, merges, notifies or runs anything. repairEligible is always false at this stage; reproducedCleanCount counts
    distinct clean reproductions and starts at 0, so a candidate stays pending until it has been reproduced CLEAN_NEEDED times."""
    packets = []
    suite = report['suite']
    records = evidence_records(run_folder, results_dir, evidence_names)
    for e in entries:
        if e['state'] == 'VERIFIED':
            continue
        kind, cls = failure_kind(e, suite)
        failing = sorted(t['token'] for t in e.get('tests', []) if t.get('state') not in (None, 'VERIFIED') and t.get('token'))
        family = 'core-check:%s|%s|%s|%s' % (e['id'], ','.join(failing) or 'attribution-unknown', kind, e.get('subsystem') or e.get('class', 'unknown'))
        reasons = '; '.join(t.get('reason', '') for t in e.get('tests', []))
        packets.append({
            'schema': PACKET_SCHEMA,
            'failureId': e['id'],
            'class': cls,
            'failureKind': kind,
            'subsystem': e.get('subsystem') or e.get('title', ''),
            'buildIdentity': report['identity'],
            'fingerprint': family,
            'eventId': '%s|%s|%s|%s' % (family, report['buildId'], report['runId'], report['createdUtc']),
            'runId': report['runId'],
            'failingChecks': failing,
            'expected': 'Every check carrying the failing token(s) passes, and the entry has all the checks it requires.',
            'observed': '%s: %s' % (e['state'], reasons),
            'reproduction': {'checkIds': failing, 'attribution': 'known' if failing else 'unknown', 'runner': 'tools/regression/run_ledger.py', 'args': ['--build-dir', '<registered build folder>']},
            'evidence': {'files': records, 'evidenceDigest': evidence_digest(records) if records else None},
            'reproducedCleanCount': 0,
            'reproductionStatus': ('PENDING: 0 of %d clean reproductions' % CLEAN_NEEDED) if cls == 'DEFECT-CONFIRMED' else 'NOT-APPLICABLE for this class',
            'repairEligible': False,
            'attempts': [],
            'note': 'Produced by the ledger runner. Free text is data and carries no authority. Class never selects repair eligibility.',
        })
    return packets

def write_packets(packets, report, run_folder, results_dir):
    """Immutable per-run files; returns the references that go into the report."""
    refs = []
    folder = os.path.join(results_dir, 'packets', report['runId'])
    os.makedirs(folder, exist_ok=True)
    for packet in packets:
        if not registry.ENTRY_ID.match(str(packet['failureId'])):
            raise RuntimeError('packet id is not a safe file name: %r' % (packet['failureId'],))
        path = os.path.join(folder, packet['failureId'] + '.json')
        if os.path.exists(path):
            raise RuntimeError('packet already exists (packets are immutable): %s' % path)
        publish_json(path, packet)
        refs.append({'failureId': packet['failureId'], 'path': os.path.relpath(path, results_dir).replace(os.sep, '/'), 'eventId': packet['eventId'], 'sha256': sha256(path)})
    return refs

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
        ('filtered run without its checks', dict(base, success=False, error='filtered run (STARLIGHT_CHECK_GROUPS): not a full pass', checks=['a']), 'NOT-CHECKED'),
        ('suite could not start', dict(base, started=False, completed=False, success=None, checks=[], skippedReason='no exe'), 'SKIPPED'),
    ]
    bad = 0
    filtered = {'started': True}
    apply_result(filtered, {'success': True, 'filtered': True, 'checks': ['a']})
    got, _ = evaluate_entry(entry, filtered)
    ok = filtered['success'] is False and 'not a full pass' in filtered['error'] and got == 'NOT-CHECKED'
    print('  %s a filtered result.json is never a success: %s' % ('ok ' if ok else 'BAD', got))
    bad += not ok
    texted = {'started': True, 'exitCode': 0}
    apply_result(texted, {'success': 'false', 'checks': ['a']})
    ok = texted['success'] is False
    print('  %s a result.json that says "false" as text is not a success' % ('ok ' if ok else 'BAD'))
    bad += not ok
    crashed_after = {'started': True, 'exitCode': 3}
    apply_result(crashed_after, {'success': True, 'checks': ['a']})
    ok = crashed_after['success'] is False and 'exited with code 3' in crashed_after['error']
    print('  %s a suite that wrote success and then exited non-zero is not a success' % ('ok ' if ok else 'BAD'))
    bad += not ok
    ok = (exit_code({'VERIFIED'}, {'success': True}) == 0 and exit_code({'VERIFIED'}, {'success': None}) == 2
          and exit_code({'VERIFIED', 'REGRESSED'}, {'success': True}) == 1 and exit_code({'NOT-CHECKED'}, {'success': True}) == 2
          and exit_code(set(), {'success': True}) == 2)
    print('  %s every entry VERIFIED still exits 2 when the suite did not pass' % ('ok ' if ok else 'BAD'))
    bad += not ok
    crashed = {'started': True, 'completed': True, 'success': True, 'error': None, 'checks': ['a', 'X: 1', 'X: 2', 'X: 3'], 'exitCode': -1073741819, 'timedOut': False}
    settle_exit(crashed)
    ok = crashed['success'] is False and 'exited with code -1073741819' in crashed['error']
    print('  %s a suite that exits nonzero after writing a pass is not a success' % ('ok ' if ok else 'BAD'))
    bad += not ok
    killed = {'started': True, 'completed': True, 'success': True, 'error': None, 'checks': ['X: 1', 'X: 2', 'X: 3'], 'exitCode': None, 'timedOut': True}
    settle_exit(killed)
    ok = killed['success'] is False and 'timed out' in killed['error']
    print('  %s a suite that times out after writing a pass is not a success' % ('ok ' if ok else 'BAD'))
    bad += not ok
    regressed = {'started': True, 'completed': True, 'success': False, 'error': 'X: 2 broke', 'checks': ['X: 1'], 'exitCode': 1, 'timedOut': False}
    settle_exit(regressed)
    ok = regressed['error'] == 'X: 2 broke' and evaluate_entry(entry, regressed)[0] == 'REGRESSED'
    print('  %s a nonzero exit keeps the failing check name, so the entry is still REGRESSED' % ('ok ' if ok else 'BAD'))
    bad += not ok
    clean = {'started': True, 'completed': True, 'success': True, 'error': None, 'checks': ['X: 1', 'X: 2', 'X: 3'], 'exitCode': 0, 'timedOut': False}
    settle_exit(clean)
    ok = clean['success'] is True and clean['error'] is None
    print('  %s a clean exit 0 pass is left as a pass' % ('ok ' if ok else 'BAD'))
    bad += not ok
    os.environ['STARLIGHT_CHECK_GROUPS'] = 'x'
    env = child_environment({'KEEP_ME': '1', 'STARLIGHT_CHECK_GROUPS': 'y'})
    del os.environ['STARLIGHT_CHECK_GROUPS']
    ok = 'STARLIGHT_CHECK_GROUPS' not in env and env.get('KEEP_ME') == '1'
    print('  %s the child suite environment drops STARLIGHT_CHECK_GROUPS' % ('ok ' if ok else 'BAD'))
    bad += not ok
    for name, suite, want in cases:
        got, _ = evaluate_entry(entry, suite)
        flag = 'ok ' if got == want else 'BAD'
        bad += got != want
        print('  %s %-42s expected %-14s got %s' % (flag, name, want, got))
    exited = dict(base, exitCode=0, timedOut=False)
    settle_exit(exited)
    crashed = dict(base, exitCode=-1073741819, timedOut=False)
    settle_exit(crashed)
    hung = dict(base, exitCode=None, timedOut=True)
    settle_exit(hung)
    ok = exited['success'] is True and crashed['success'] is False and 'exited with code' in crashed['error'] and hung['success'] is False and 'timed out' in hung['error']
    print('  %s a result.json written before a crash or timeout is not a pass' % ('ok ' if ok else 'BAD'))
    bad += not ok
    notes = transitions_for([{'id': 'X', 'state': 'VERIFIED', 'tests': [{'reason': 'r'}]}], {'X': 'NOT-CHECKED'})
    print('  %s coverage restored notice: %s' % ('ok ' if notes and notes[0].startswith('COVERAGE RESTORED') else 'BAD', notes))
    bad += not (notes and notes[0].startswith('COVERAGE RESTORED'))
    notes = transitions_for([{'id': 'X', 'state': 'REGRESSED', 'tests': [{'reason': 'r'}]}], {'X': 'VERIFIED'})
    print('  %s new regression notice: %s' % ('ok ' if notes and notes[0].startswith('NEW REGRESSION') else 'BAD', notes))
    bad += not (notes and notes[0].startswith('NEW REGRESSION'))
    fake_report = {'suite': {}, 'identity': {'gitHead': 'a' * 40, 'dirtyTrackedFiles': 0, 'binaries': {}}, 'buildId': 'b-1', 'runId': 'b-1-x', 'createdUtc': '2026-01-01T00:00:00Z'}
    fake_entries = [{'id': 'V', 'title': 't', 'class': 'correctness', 'state': 'VERIFIED', 'tests': [{'token': 'V:', 'reason': 'r'}]},
                    {'id': 'R', 'title': 't', 'class': 'correctness', 'state': 'REGRESSED', 'tests': [{'token': 'R:', 'reason': 'boom'}]},
                    {'id': 'N', 'title': 't', 'class': 'correctness', 'state': 'NOT-CHECKED', 'tests': [{'token': 'N:', 'reason': 'x'}]},
                    {'id': 'S', 'title': 't', 'class': 'correctness', 'state': 'SKIPPED', 'tests': [{'token': 'S:', 'reason': 'y'}]}]
    import tempfile
    with tempfile.TemporaryDirectory() as results:
        run = os.path.join(results, 'runs', 'b-1-x')
        os.makedirs(run)
        open(os.path.join(run, 'progress.log'), 'w').write('a\n')
        fake_entries[1]['tests'] = [{'token': 'R0:', 'state': 'VERIFIED', 'reason': 'ok'}, {'token': 'R:', 'state': 'REGRESSED', 'reason': 'boom'}]
        fake_report['suite'] = {'completed': True, 'timedOut': False}
        packets = make_packets(fake_entries, fake_report, run, results)
        got = {p['failureId']: p['class'] for p in packets}
        want = {'R': 'DEFECT-CONFIRMED', 'N': 'INCOMPLETE', 'S': 'ENVIRONMENTAL'}
        ok = got == want and all(not p['repairEligible'] and p['reproducedCleanCount'] == 0 and p['schema'] == PACKET_SCHEMA for p in packets)
        print('  %s packets only for non-verified entries, never repair-eligible: %s' % ('ok ' if ok else 'BAD', got))
        bad += not ok
        r = [p for p in packets if p['failureId'] == 'R'][0]
        ok = r['failingChecks'] == ['R:'] and r['fingerprint'].startswith('core-check:R|R:|check-failed|')
        print('  %s attribution uses the failing token, not the first test: %s' % ('ok ' if ok else 'BAD', r['failingChecks']))
        bad += not ok
        fake_report['suite'] = {'completed': False, 'timedOut': True}
        killed = [p for p in make_packets(fake_entries, fake_report, run, results) if p['failureId'] == 'R'][0]
        ok = killed['class'] == 'INCOMPLETE' and killed['failureKind'] == 'timeout-or-kill'
        print('  %s a timeout or kill is never a confirmed defect: %s' % ('ok ' if ok else 'BAD', killed['class']))
        bad += not ok
        fake_report['suite'] = {'completed': True, 'timedOut': False}
        refs = write_packets(packets, fake_report, run, results)
        again_blocked = False
        try:
            write_packets(packets, fake_report, run, results)
        except RuntimeError:
            again_blocked = True
        ok = len(refs) == 3 and again_blocked and all(x['path'].startswith('packets/b-1-x/') for x in refs)
        print('  %s packet files are immutable and referenced by path: %s' % ('ok ' if ok else 'BAD', again_blocked))
        bad += not ok
        escaped = False
        try:
            write_packets([dict(packets[0], failureId='..\\..\\outside')], fake_report, run, results)
        except RuntimeError:
            escaped = True
        ok = escaped and not os.path.exists(os.path.join(os.path.dirname(results), 'outside.json')) and not os.path.exists(os.path.join(results, 'outside.json'))
        print('  %s a packet id that is not a plain file name is refused before anything is written' % ('ok ' if ok else 'BAD'))
        bad += not ok
        other = dict(fake_report, runId='b-1-y')
        e1 = [p for p in make_packets(fake_entries, fake_report, run, results) if p['failureId'] == 'R'][0]['eventId']
        e2 = [p for p in make_packets(fake_entries, other, run, results) if p['failureId'] == 'R'][0]['eventId']
        ok = e1 != e2
        print('  %s runs with the same build and time still get distinct event ids' % ('ok ' if ok else 'BAD'))
        bad += not ok
    with tempfile.TemporaryDirectory() as results:
        first = {'runId': 'same-build-run1', 'marker': 'full run'}
        second = {'runId': 'same-build-run2', 'marker': 'weaker run'}
        one, two = archive_report(results, first), archive_report(results, second)
        kept = json.load(open(one))['marker'] == 'full run' and json.load(open(two))['marker'] == 'weaker run' and one != two
        try:
            archive_report(results, {'runId': 'same-build-run1', 'marker': 'replacement'})
            refused = False
        except FileExistsError:
            refused = True
        print('  %s history keeps every run and never replaces one' % ('ok ' if kept and refused and json.load(open(one))['marker'] == 'full run' else 'BAD'))
        bad += not (kept and refused and json.load(open(one))['marker'] == 'full run')
    with tempfile.TemporaryDirectory() as tree:
        build = os.path.join(tree, 'build', 'Release')
        os.makedirs(build)
        open(os.path.join(build, 'Starlight.NativeCoreChecks.exe'), 'w').write('x')
        repo = os.path.join(tree, 'repo')
        os.makedirs(os.path.join(repo, 'tools', 'checks'))
        subprocess.run(['git', '-C', repo, 'init', '-q'], creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        run = subprocess.run([sys.executable, os.path.abspath(__file__), '--repo', repo, '--build-dir', build, '--results', os.path.join(tree, 'results'),
                              '--ledger', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ledger.json')], capture_output=True, text=True, timeout=300)
        ok = run.returncode == 2 and 'REFUSING TO RUN' in run.stdout and not os.path.exists(os.path.join(tree, 'results', 'latest.json'))
        print('  %s a checkout git cannot read the HEAD of is refused, not recorded as a clean build' % ('ok ' if ok else 'BAD'))
        bad += not ok
    with tempfile.TemporaryDirectory() as tree:
        build = os.path.join(tree, 'Release')
        os.makedirs(build)
        before = configured_from(build)
        with open(os.path.join(tree, 'CMakeCache.txt'), 'w') as f:
            f.write('CMAKE_HOME_DIRECTORY:INTERNAL=' + tree.replace('\\', '/') + '\n')
        home = configured_from(build)
        ok = before is None and home is not None and same_folder(home, tree) and not same_folder(home, os.path.dirname(tree))
        print('  %s the repo a build was configured from is read from its CMake cache and compared by real path' % ('ok ' if ok else 'BAD'))
        bad += not ok
    with tempfile.TemporaryDirectory() as repo:
        flags = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
        for args in (['init', '-q'], ['config', 'user.email', 'a@b'], ['config', 'user.name', 'a']):
            subprocess.run(['git', '-C', repo] + args, creationflags=flags)
        path = os.path.join(repo, 'a.cpp')
        open(path, 'w').write('int a;\n')
        subprocess.run(['git', '-C', repo, 'add', '-A'], creationflags=flags)
        subprocess.run(['git', '-C', repo, 'commit', '-q', '-m', 'x'], creationflags=flags)
        clean = dirty_digest(repo)
        open(path, 'w').write('int b;\n')
        first = dirty_digest(repo)
        open(path, 'w').write('int c;\n')
        second = dirty_digest(repo)
        ok = first and second and len({clean, first, second}) == 3
        print('  %s two different uncommitted edits at one HEAD get different dirty digests' % ('ok ' if ok else 'BAD'))
        bad += not ok
    print('SELF-TEST', 'PASSED' if not bad else 'FAILED (%d)' % bad)
    return 1 if bad else 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo', default=registry.REPO_ROOT)
    ap.add_argument('--build-dir')
    ap.add_argument('--ledger', default=None)
    ap.add_argument('--results', default=registry.DEFAULT_RESULTS)
    ap.add_argument('--timeout', type=int, default=1800)
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if not args.build_dir:
        ap.error('--build-dir is required')

    home = configured_from(args.build_dir)
    if home and not same_folder(home, args.repo):
        print('REFUSING TO RUN: the build was configured from %s but --repo is %s' % (home, args.repo))
        return 2
    stale = verify_all.stale_reason(args.repo, args.build_dir, targets=('Starlight.NativeCoreChecks', 'Starlight.Viewport', 'Starlight.Physics'))
    if stale:
        print('REFUSING TO RUN: ' + stale)
        return 2
    ledger_path = args.ledger or os.path.join(args.repo, 'tools', 'regression', 'ledger.json')
    os.makedirs(args.results, exist_ok=True)

    mismatch = registry.check_ledger(ledger_path)
    if not mismatch:
        ledger = registry.read_json_object(ledger_path)
        mismatch = registry.undercounted(ledger, args.repo)
    if mismatch:
        print('REFUSING TO RUN: the ledger and the committed check registry disagree: ' + '; '.join(mismatch))
        return 2
    registered = registry.command_for_ledger(ledger, registry.load())
    identity = {
        'gitHead': git(args.repo, 'rev-parse', 'HEAD'),
        'dirtyTrackedFiles': len([l for l in git(args.repo, 'status', '--porcelain', '--untracked-files=no').splitlines() if l.strip()]),
        'binaries': {},
    }
    if not identity['gitHead']:
        print('REFUSING TO RUN: git cannot read the HEAD of %s, so the build identity cannot say which sources it was built from' % args.repo)
        return 2
    if identity['dirtyTrackedFiles']:
        identity['dirtyDiffSha256'] = dirty_digest(args.repo)
    for name in registered['binaries']:
        p = os.path.join(args.build_dir, name)
        if os.path.exists(p):
            identity['binaries'][name] = {'sha256': sha256(p), 'bytes': os.path.getsize(p)}
    build_id = (identity['gitHead'][:12] or 'nogit') + ('+dirty' + identity['dirtyDiffSha256'][:8] if identity['dirtyTrackedFiles'] else '') + '-' + identity['binaries'].get(registered['executable'], {}).get('sha256', 'nobinary')[:10]

    run_id = build_id + '-' + time.strftime('%Y%m%d-%H%M%S') + '-' + uuid.uuid4().hex[:12]
    run_folder = os.path.join(args.results, 'runs', run_id)
    os.makedirs(run_folder, exist_ok=False)
    command = registry.resolve(ledger['entries'][0]['tests'][0]['token'], args.repo, args.build_dir, run_folder)
    exe = command['argv'][0]
    result_json = os.path.join(run_folder, 'result.json')
    progress_path = os.path.join(run_folder, 'progress.log')
    suite = {'started': False, 'exitCode': None, 'timedOut': False, 'completed': False, 'success': None, 'error': None, 'checks': []}
    if not os.path.exists(exe):
        suite['skippedReason'] = 'Starlight.NativeCoreChecks.exe not found in the build directory'
    else:
        suite['started'] = True
        try:
            proc = subprocess.run(command['argv'], cwd=command['cwd'], capture_output=True, text=True,
                                  timeout=min(args.timeout, command['timeoutSeconds']), env=child_environment(command['env']))
            suite['exitCode'] = proc.returncode
            suite['stdoutTail'] = proc.stdout[-400:]
            suite['stderrTail'] = proc.stderr[-400:]
        except subprocess.TimeoutExpired:
            suite['timedOut'] = True
        if os.path.exists(result_json):
            try:
                data = json.load(open(result_json, encoding='utf-8'))
                apply_result(suite, data)
            except Exception as e:
                suite['error'] = 'result.json unreadable: %s' % e
        elif os.path.exists(progress_path):
            suite['checks'] = [l.strip() for l in open(progress_path, encoding='utf-8', errors='replace') if l.strip()]
            suite['error'] = 'suite did not finish; last passed check: %s' % (suite['checks'][-1] if suite['checks'] else 'none')
        else:
            suite['error'] = 'suite did not finish and left no progress log'
        settle_exit(suite)

    entries = []
    for entry in ledger['entries']:
        state, states = evaluate_entry(entry, suite)
        entries.append({'id': entry['id'], 'title': entry['title'], 'class': entry['class'], 'subsystem': entry.get('subsystem'), 'state': state, 'tests': states,
                        'testStrength': entry.get('testStrength'), 'investigate': entry.get('investigate', [])})

    report = {'schema': 'starlight-ledger-results-1', 'createdUtc': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()), 'buildId': build_id, 'runId': run_id,
              'identity': identity, 'suite': {k: v for k, v in suite.items() if k != 'checks'}, 'suiteCheckCount': len(suite['checks']), 'entries': entries}

    latest_path = os.path.join(args.results, 'latest.json')
    previous = {}
    if os.path.exists(latest_path):
        try:
            previous = {e['id']: e['state'] for e in json.load(open(latest_path, encoding='utf-8')).get('entries', [])}
        except Exception:
            previous = {}
    report['transitions'] = transitions_for(entries, previous)
    packets = make_packets(entries, report, run_folder, args.results, tuple(registered['evidenceFiles']))
    report['packets'] = write_packets(packets, report, run_folder, args.results) if packets else []
    archive_report(args.results, report)
    publish_json(os.path.join(args.results, build_id + '.json'), report)
    publish_json(latest_path, report)
    if report['transitions']:
        with open(os.path.join(args.results, 'notices.log'), 'a', encoding='utf-8') as f:
            for t in report['transitions']:
                f.write('%s  build %s  %s\n' % (report['createdUtc'], build_id, t))

    print('build identity:', build_id, '| suite checks recorded:', len(suite['checks']), '| suite success:', suite['success'], '| exit', suite['exitCode'])
    for e in entries:
        print('  %-14s %-14s %s' % (e['id'], e['state'], e['tests'][0]['reason']))
    for t in report['transitions']:
        print('NOTICE:', t)
    return exit_code({e['state'] for e in entries}, suite)

if __name__ == '__main__':
    sys.exit(main())
