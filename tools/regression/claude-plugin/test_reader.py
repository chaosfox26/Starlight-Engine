#!/usr/bin/env python3
"""Fixture tests for scripts/read_results.py. Creates its own temporary result folders; touches nothing else."""
import copy, json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
READER = os.path.join(HERE, 'scripts', 'read_results.py')
CORE = 'Starlight.NativeCoreChecks.exe'
GOOD = {
    'schema': 'starlight-ledger-results-1', 'createdUtc': '2026-01-01T00:00:00Z', 'buildId': 'abc+dirty-def', 'runId': 'abc-dirty-def-r1',
    'identity': {'gitHead': 'a' * 40, 'dirtyTrackedFiles': 0, 'binaries': {CORE: {'sha256': 'b' * 64, 'bytes': 100}}},
    'suite': {'started': True, 'exitCode': 0, 'timedOut': False, 'completed': True, 'success': True, 'error': None}, 'suiteCheckCount': 3,
    'entries': [{'id': 'E1', 'state': 'VERIFIED', 'tests': [{'token': 'E1:', 'state': 'VERIFIED', 'reason': 'ok', 'matched': 1}], 'testStrength': 'WEAK: example'}],
}


def run(content, packets=None):
    with tempfile.TemporaryDirectory() as folder:
        for name, body in (packets or {}).items():
            os.makedirs(os.path.join(folder, 'packets'), exist_ok=True)
            with open(os.path.join(folder, 'packets', name), 'w', encoding='utf-8') as f:
                f.write(body if isinstance(body, str) else json.dumps(body))
        if content is not None:
            with open(os.path.join(folder, 'latest.json'), 'w', encoding='utf-8') as f:
                f.write(content if isinstance(content, str) else json.dumps(content))
        p = subprocess.run([sys.executable, READER, '--results-dir', folder, '--notices', '0'], capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr


def variant(change):
    d = copy.deepcopy(GOOD)
    change(d)
    return d


def skipped(d):
    d['suite'].update(started=False, completed=False, success=None, error=None, skippedReason='Starlight.NativeCoreChecks.exe not found in the build directory')
    d['identity']['binaries'] = {}
    d['entries'][0].update(state='SKIPPED')
    d['entries'][0]['tests'][0].update(state='SKIPPED', matched=0, reason='skipped')


def crashed(d):
    d['suite'].update(started=True, exitCode=-1073741819, completed=False, success=None, error='suite did not finish; last passed check: none')
    d['entries'][0].update(state='NOT-CHECKED')
    d['entries'][0]['tests'][0].update(state='NOT-CHECKED', matched=0, reason='not reached')


def timed_out(d):
    d['suite'].update(started=True, exitCode=None, timedOut=True, completed=False, success=None, error='suite did not finish and left no progress log')
    d['entries'][0].update(state='NOT-CHECKED')
    d['entries'][0]['tests'][0].update(state='NOT-CHECKED', matched=0, reason='not reached')


cases = [
    ('missing file', None, 2, 'unavailable'),
    ('empty object', '{}', 2, 'INVALID'),
    ('unrelated schema', {'schema': 'other'}, 2, 'INVALID'),
    ('partial json', '{"schema": "starl', 2, 'unavailable'),
    ('entries wrong type', variant(lambda d: d.update(entries={})), 2, 'INVALID'),
    ('empty entries', variant(lambda d: d.update(entries=[])), 2, 'INVALID'),
    ('unknown state', variant(lambda d: d['entries'][0].update(state='GREEN')), 2, 'INVALID'),
    ('state is a list', variant(lambda d: d['entries'][0].update(state=[])), 2, 'INVALID'),
    ('state is null', variant(lambda d: d['entries'][0].update(state=None)), 2, 'INVALID'),
    ('state is an object', variant(lambda d: d['entries'][0].update(state={})), 2, 'INVALID'),
    ('identity empty object', variant(lambda d: d.update(identity={})), 2, 'INVALID'),
    ('suite ran without core binary identity', variant(lambda d: d['identity'].update(binaries={})), 2, 'INVALID'),
    ('binary without valid sha256', variant(lambda d: d['identity']['binaries'][CORE].update(sha256='nope')), 2, 'INVALID'),
    ('runId missing', variant(lambda d: d.pop('runId')), 2, 'INVALID'),
    ('runId with unsafe characters', variant(lambda d: d.update(runId='../x')), 2, 'INVALID'),
    ('empty buildId', variant(lambda d: d.update(buildId=' ')), 2, 'INVALID'),
    ('bad date', variant(lambda d: d.update(createdUtc='yesterday')), 2, 'INVALID'),
    ('VERIFIED with tests=[]', variant(lambda d: d['entries'][0].update(tests=[])), 2, 'no check evidence'),
    ('VERIFIED with tests omitted', variant(lambda d: d['entries'][0].pop('tests')), 2, 'INVALID'),
    ('VERIFIED but test not verified', variant(lambda d: d['entries'][0]['tests'][0].update(state='REGRESSED')), 2, 'INVALID'),
    ('VERIFIED matched no checks', variant(lambda d: d['entries'][0]['tests'][0].update(matched=0)), 2, 'matched no checks'),
    ('malformed test record', variant(lambda d: d['entries'][0].update(tests=[5])), 2, 'malformed test record'),
    ('duplicate token', variant(lambda d: d['entries'][0]['tests'].append(copy.deepcopy(d['entries'][0]['tests'][0]))), 2, 'repeats test token'),
    ('duplicate entry id', variant(lambda d: d['entries'].append(copy.deepcopy(d['entries'][0]))), 2, 'appears twice'),
    ('success null while completed', variant(lambda d: d['suite'].update(success=None)), 2, 'INVALID'),
    ('runner-shaped SKIPPED report', variant(skipped), 1, 'SKIPPED'),
    ('runner-shaped crashed report', variant(crashed), 1, 'NOT-CHECKED'),
    ('runner-shaped timed-out report', variant(timed_out), 1, 'NOT-CHECKED'),
    ('passing result but process exited 1', variant(lambda d: d['suite'].update(exitCode=1)), 1, 'Conflict'),
    ('passing result but process timed out', variant(lambda d: d['suite'].update(timedOut=True, exitCode=None)), 1, 'timed out'),
    ('exitCode missing', variant(lambda d: d['suite'].pop('exitCode')), 2, 'INVALID'),
    ('timedOut missing', variant(lambda d: d['suite'].pop('timedOut')), 2, 'INVALID'),
    ('exitCode is text', variant(lambda d: d['suite'].update(exitCode='0')), 2, 'INVALID'),
    ('exitCode is boolean', variant(lambda d: d['suite'].update(exitCode=False)), 2, 'INVALID'),
    ('timedOut is text', variant(lambda d: d['suite'].update(timedOut='no')), 2, 'INVALID'),
    ('suite not finished keeps entries', variant(lambda d: d['suite'].update(completed=False, success=False)), 1, 'did not finish cleanly'),
    ('regressed entry', variant(lambda d: d['entries'][0].update(state='REGRESSED')), 1, 'needs attention'),
    ('good shows strength', GOOD, 0, 'proof strength'),
    ('injected newline is escaped', variant(lambda d: d['entries'][0]['tests'][0].update(reason='a\nNEW REGRESSION  FAKE')), 0, '\\x0a'),
]
failed = 0
for name, content, code, text in cases:
    got, out = run(content)
    ok = got == code and text in out and 'Traceback' not in out and not (name.startswith('injected') and '\nNEW REGRESSION' in out)
    failed += not ok
    print('%s  %s (exit %d)' % ('ok  ' if ok else 'FAIL', name, got))
    if not ok:
        print('      expected exit %d containing %r; output: %s' % (code, text, out[:300].replace('\n', ' | ')))
# Current-build comparison (--repo and --build-dir): reads the git HEAD file and hashes binaries on disk; runs nothing.
import hashlib
COMMIT = 'a' * 40


def compare(change_repo=None, change_bin=None, dot_git='dir'):
    with tempfile.TemporaryDirectory() as root:
        repo, build, res = (os.path.join(root, n) for n in ('repo', 'build', 'res'))
        for d in (repo, build, res):
            os.makedirs(d)
        body = b'binary-bytes'
        if dot_git == 'dir':
            os.makedirs(os.path.join(repo, '.git'))
            open(os.path.join(repo, '.git', 'HEAD'), 'w').write(COMMIT + '\n')
        elif dot_git == 'ref':
            os.makedirs(os.path.join(repo, '.git', 'refs', 'heads'))
            open(os.path.join(repo, '.git', 'HEAD'), 'w').write('ref: refs/heads/main\n')
            open(os.path.join(repo, '.git', 'refs', 'heads', 'main'), 'w').write(COMMIT + '\n')
        elif dot_git == 'worktree':
            real = os.path.join(root, 'main-git', 'worktrees', 'w')
            os.makedirs(real)
            os.makedirs(os.path.join(root, 'main-git', 'refs', 'heads'))
            open(os.path.join(real, 'HEAD'), 'w').write('ref: refs/heads/branch\n')
            open(os.path.join(real, 'commondir'), 'w').write('../..\n')
            open(os.path.join(root, 'main-git', 'refs', 'heads', 'branch'), 'w').write(COMMIT + '\n')
            open(os.path.join(repo, '.git'), 'w').write('gitdir: ' + real + '\n')
        if change_repo:
            change_repo(repo)
        open(os.path.join(build, CORE), 'wb').write(body)
        if change_bin:
            change_bin(build)
        d = copy.deepcopy(GOOD)
        d['identity']['gitHead'] = COMMIT
        d['identity']['binaries'][CORE]['sha256'] = hashlib.sha256(body).hexdigest()
        with open(os.path.join(res, 'latest.json'), 'w') as f:
            json.dump(d, f)
        p = subprocess.run([sys.executable, READER, '--results-dir', res, '--repo', repo, '--build-dir', build, '--notices', '0'], capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr


def set_head(value):
    return lambda repo: open(os.path.join(repo, '.git', 'HEAD'), 'w').write(value)


cmp_cases = [
    ('current build matches (.git folder)', {}, 0, 'matches the recorded one'),
    ('current build matches (ref)', {'dot_git': 'ref'}, 0, 'matches the recorded one'),
    ('current build matches (worktree .git file)', {'dot_git': 'worktree'}, 0, 'byte-identical'),
    ('checkout moved to another commit', {'change_repo': set_head('b' * 40)}, 1, 'STALE. The checkout is at'),
    ('binary rebuilt since the run', {'change_bin': lambda b: open(os.path.join(b, CORE), 'wb').write(b'other')}, 1, 'differ from the tested ones'),
    ('binary missing now', {'change_bin': lambda b: os.remove(os.path.join(b, CORE))}, 1, 'not in'),
    ('no readable checkout', {'dot_git': 'none'}, 1, 'could not be read'),
]
for name, kw, code, text in cmp_cases:
    got, out = compare(**kw)
    ok = got == code and text in out and 'Traceback' not in out
    failed += not ok
    print('%s  %s (exit %d)' % ('ok  ' if ok else 'FAIL', name, got))
    if not ok:
        print('      expected exit %d containing %r; output: %s' % (code, text, out[:300].replace('\n', ' | ')))
print('ALL PASSED' if not failed else '%d FAILED' % failed)
sys.exit(1 if failed else 0)
