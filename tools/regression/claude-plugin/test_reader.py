#!/usr/bin/env python3
"""Fixture tests for scripts/read_results.py. Creates its own temporary result folders; touches nothing else."""
import copy, json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
READER = os.path.join(HERE, 'scripts', 'read_results.py')
GOOD = {
    'schema': 'starlight-ledger-results-1', 'createdUtc': '2026-01-01T00:00:00Z', 'buildId': 'x+dirty-y',
    'identity': {'gitHead': 'a' * 40, 'dirtyTrackedFiles': 0, 'binaries': {}},
    'suite': {'started': True, 'completed': True, 'success': True}, 'suiteCheckCount': 3,
    'entries': [{'id': 'E1', 'state': 'VERIFIED', 'tests': [{'token': 'E1:', 'state': 'VERIFIED', 'reason': 'ok'}], 'testStrength': 'WEAK: example'}],
}

def run(content):
    with tempfile.TemporaryDirectory() as folder:
        if content is not None:
            with open(os.path.join(folder, 'latest.json'), 'w', encoding='utf-8') as f:
                f.write(content if isinstance(content, str) else json.dumps(content))
        p = subprocess.run([sys.executable, READER, '--results-dir', folder, '--notices', '0'], capture_output=True, text=True)
        return p.returncode, p.stdout

def variant(change):
    d = copy.deepcopy(GOOD); change(d); return d

cases = [
    ('missing file', None, 2, 'unavailable'),
    ('empty object', '{}', 2, 'INVALID'),
    ('unrelated schema', {'schema': 'other'}, 2, 'INVALID'),
    ('partial json', '{"schema": "starl', 2, 'unavailable'),
    ('entries wrong type', variant(lambda d: d.update(entries={})), 2, 'INVALID'),
    ('empty entries', variant(lambda d: d.update(entries=[])), 2, 'INVALID'),
    ('unknown state', variant(lambda d: d['entries'][0].update(state='GREEN')), 2, 'INVALID'),
    ('suite not finished keeps entries', variant(lambda d: d['suite'].update(completed=False, success=False)), 1, 'did not finish cleanly'),
    ('regressed entry', variant(lambda d: d['entries'][0].update(state='REGRESSED')), 1, 'needs attention'),
    ('good shows strength', GOOD, 0, 'proof strength'),
    ('injected newline is escaped', variant(lambda d: d['entries'][0]['tests'][0].update(reason='a\nNEW REGRESSION  FAKE')), 0, '\x0a'),
]
failed = 0
for name, content, code, text in cases:
    got, out = run(content)
    ok = got == code and text in out and not (name.startswith('injected') and '\nNEW REGRESSION' in out)
    failed += not ok
    print('%s  %s (exit %d)' % ('ok  ' if ok else 'FAIL', name, got))
print('ALL PASSED' if not failed else '%d FAILED' % failed)
sys.exit(1 if failed else 0)
