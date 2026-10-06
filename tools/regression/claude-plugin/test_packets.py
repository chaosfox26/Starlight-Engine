#!/usr/bin/env python3
"""Fixture tests for the failure-packet section of scripts/read_results.py. Builds complete result folders in a temporary directory; touches nothing else."""
import copy, hashlib, json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
READER = os.path.join(HERE, 'scripts', 'read_results.py')
CORE = 'Starlight.NativeCoreChecks.exe'
IDENTITY = {'gitHead': 'a' * 40, 'dirtyTrackedFiles': 0, 'binaries': {CORE: {'sha256': 'b' * 64, 'bytes': 100}, 'Starlight.Physics.dll': {'sha256': 'c' * 64, 'bytes': 50}}}
CREATED = '2026-01-01T00:00:00Z'
BUILD = 'abc-def'
FAMILY = 'core-check:E1|E1:|check-failed|physics'
RUN = 'r1'


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        h.update(f.read())
    return h.hexdigest()


def build(folder, packet_change=None, report_change=None, file_change=None, evidence_change=None, refs=1):
    run = os.path.join(folder, 'runs', 'r1')
    os.makedirs(run)
    log = os.path.join(run, 'progress.log')
    with open(log, 'w') as f:
        f.write('E1: step\n')
    record = {'path': 'runs/r1/progress.log', 'sha256': sha(log), 'bytes': os.path.getsize(log)}
    digest = hashlib.sha256(record['path'].encode() + record['sha256'].encode()).hexdigest()
    packet = {
        'schema': 'starlight-failure-packet-1', 'failureId': 'E1', 'class': 'DEFECT-CONFIRMED', 'failureKind': 'check-failed', 'subsystem': 'physics',
        'buildIdentity': copy.deepcopy(IDENTITY), 'fingerprint': FAMILY, 'eventId': '%s|%s|%s|%s' % (FAMILY, BUILD, RUN, CREATED), 'runId': RUN,
        'failingChecks': ['E1:'], 'expected': 'passes', 'observed': 'REGRESSED: boom',
        'reproduction': {'checkIds': ['E1:'], 'attribution': 'known', 'runner': 'tools/regression/run_ledger.py', 'args': ['--build-dir', '<x>']},
        'evidence': {'files': [record], 'evidenceDigest': digest}, 'reproducedCleanCount': 0,
        'reproductionStatus': 'PENDING: 0 of 2 clean reproductions', 'repairEligible': False, 'attempts': [], 'note': 'n',
    }
    if packet_change:
        packet_change(packet)
    os.makedirs(os.path.join(folder, 'packets', 'r1'))
    path = os.path.join(folder, 'packets', 'r1', 'E1.json')
    with open(path, 'w', encoding='utf-8') as f:
        f.write(json.dumps(packet))
    ref = {'failureId': 'E1', 'path': 'packets/r1/E1.json', 'eventId': packet.get('eventId') if isinstance(packet.get('eventId'), str) else 'x', 'sha256': sha(path)}
    if file_change:
        file_change(folder, ref)
    report = {
        'schema': 'starlight-ledger-results-1', 'createdUtc': CREATED, 'buildId': BUILD, 'runId': RUN, 'identity': copy.deepcopy(IDENTITY),
        'suite': {'started': True, 'exitCode': 0, 'timedOut': False, 'completed': True, 'success': True, 'error': None}, 'suiteCheckCount': 3,
        'entries': [{'id': 'E1', 'subsystem': 'physics', 'state': 'REGRESSED', 'tests': [{'token': 'E1:', 'state': 'REGRESSED', 'reason': 'boom', 'matched': 0}], 'testStrength': 'x'}],
        'packets': [copy.deepcopy(ref) for _ in range(refs)] if refs else [],
    }
    if report_change:
        report_change(report)
    if evidence_change:
        evidence_change(folder)
    with open(os.path.join(folder, 'latest.json'), 'w', encoding='utf-8') as f:
        f.write(json.dumps(report))


def run(**kwargs):
    with tempfile.TemporaryDirectory() as folder:
        build(folder, **kwargs)
        p = subprocess.run([sys.executable, READER, '--results-dir', folder, '--notices', '0'], capture_output=True, text=True)
        return p.returncode, p.stdout + p.stderr


def setitem(key, value):
    return lambda packet: packet.__setitem__(key, value)


def pop(key):
    return lambda packet: packet.pop(key)


def nested(first, second, value):
    return lambda packet: packet[first].__setitem__(second, value)


def rewrite_log(folder):
    with open(os.path.join(folder, 'runs', 'r1', 'progress.log'), 'w') as f:
        f.write('X1: step' + chr(10))


def delete_log(folder):
    os.remove(os.path.join(folder, 'runs', 'r1', 'progress.log'))


def tamper_packet(folder, ref):
    with open(os.path.join(folder, 'packets', 'r1', 'E1.json'), 'a') as f:
        f.write(' ')


def bad_path(value):
    return lambda folder, ref: ref.__setitem__('path', value)


def outside_evidence(packet):
    packet['evidence']['files'][0]['path'] = 'packets/r1/E1.json'


CASES = [
    ('valid packet', {}, 1, ['repair eligible: False', 'evidence: verified', 'clean reproductions 0 of 2', 'PENDING']),
    ('no references at all', {'refs': 0}, 1, ['UNCOVERED']),
    ('two references for one entry', {'refs': 2}, 1, ['UNCOVERED', '2 packet references']),
    ('missing buildIdentity', {'packet_change': pop('buildIdentity')}, 1, ['PACKET INVALID', 'buildIdentity']),
    ('wrong DLL hash in packet identity', {'packet_change': lambda p: p['buildIdentity']['binaries']['Starlight.Physics.dll'].__setitem__('sha256', 'd' * 64)}, 1, ['PACKET INVALID', 'full identity']),
    ('missing evidence', {'packet_change': pop('evidence')}, 1, ['PACKET INVALID', 'evidence is malformed']),
    ('missing reproduction', {'packet_change': pop('reproduction')}, 1, ['PACKET INVALID', 'reproduction is malformed']),
    ('wrong eventId under the right file name', {'packet_change': setitem('eventId', 'other|' + BUILD + '|' + RUN + '|' + CREATED)}, 1, ['PACKET INVALID', 'eventId']),
    ('eventId without the run id', {'packet_change': setitem('eventId', '%s|%s|%s' % (FAMILY, BUILD, CREATED))}, 1, ['PACKET INVALID', 'eventId']),
    ('fingerprint is an object', {'packet_change': setitem('fingerprint', {'a': 1})}, 1, ['PACKET INVALID']),
    ('eventId is a list', {'packet_change': setitem('eventId', ['a'])}, 1, ['PACKET INVALID']),
    ('observed is an object', {'packet_change': setitem('observed', {'a': 1})}, 1, ['PACKET INVALID', 'observed']),
    ('negative count', {'packet_change': setitem('reproducedCleanCount', -1)}, 1, ['PACKET INVALID', 'non-negative']),
    ('boolean count', {'packet_change': setitem('reproducedCleanCount', True)}, 1, ['PACKET INVALID', 'non-negative']),
    ('count 0 but repairEligible true', {'packet_change': setitem('repairEligible', True)}, 1, ['PACKET INVALID', 'unsupported claim']),
    ('count 2 but repairEligible true is still unsupported', {'packet_change': lambda p: p.update(reproducedCleanCount=2, repairEligible=True)}, 1, ['PACKET INVALID', 'unsupported claim']),
    ('unknown class', {'packet_change': setitem('class', 'REPAIR-NOW')}, 1, ['PACKET INVALID', 'unknown class']),
    ('failingChecks differs from reproduction', {'packet_change': setitem('failingChecks', ['X:'])}, 1, ['PACKET INVALID', 'differ']),
    ('packet runId differs from the report run', {'packet_change': setitem('runId', 'another-run')}, 1, ['PACKET INVALID', 'runId does not match']),
    ('unregistered failing check in packet and reproduction', {'packet_change': lambda p: (p.__setitem__('failingChecks', ['UNREGISTERED:']), p['reproduction'].__setitem__('checkIds', ['UNREGISTERED:']))}, 1, ['PACKET INVALID', 'do not match the non-verified tests']),
    ('DEFECT-CONFIRMED claimed for a timeout-or-kill', {'packet_change': setitem('failureKind', 'timeout-or-kill')}, 1, ['PACKET INVALID', 'contradict']),
    ('class does not follow the kind', {'packet_change': setitem('class', 'ENVIRONMENTAL')}, 1, ['PACKET INVALID', 'contradict']),
    ('attribution claimed unknown but report names a check', {'packet_change': lambda p: p['reproduction'].__setitem__('attribution', 'unknown')}, 1, ['PACKET INVALID', 'attribution']),
    ('fingerprint not derived from the report', {'packet_change': setitem('fingerprint', 'core-check:E1|other|check-failed|physics')}, 1, ['PACKET INVALID']),
    ('subsystem differs from the report entry', {'packet_change': setitem('subsystem', 'rendering')}, 1, ['PACKET INVALID', 'subsystem']),
    ('reference path names another run', {'file_change': bad_path('packets/other/E1.json')}, 1, ['PACKET INVALID', 'reference path']),
    ('evidence byte count wrong', {'packet_change': lambda p: p['evidence']['files'][0].__setitem__('bytes', 999999)}, 1, ['size differs']),
    ('evidence path in another run folder', {'packet_change': lambda p: p['evidence']['files'][0].__setitem__('path', 'runs/other/progress.log')}, 1, ['not confined to this run']),
    ('packet file changed after the report', {'file_change': tamper_packet}, 1, ['PACKET INVALID', 'hash recorded']),
    ('reference path traverses upward', {'file_change': bad_path('packets/../latest.json')}, 1, ['PACKET INVALID', 'reference path']),
    ('reference path is absolute', {'file_change': bad_path('C:/Windows/win.ini')}, 1, ['PACKET INVALID', 'reference path']),
    ('reference path uses backslashes', {'file_change': bad_path('packets\\r1\\E1.json')}, 1, ['PACKET INVALID', 'reference path']),
    ('evidence file changed', {'evidence_change': rewrite_log}, 1, ['evidence: evidence file changed']),
    ('evidence file missing', {'evidence_change': delete_log}, 1, ['evidence: evidence file missing']),
    ('evidence path outside the run folders', {'packet_change': outside_evidence}, 1, ['not confined']),
    ('evidence digest wrong', {'packet_change': nested('evidence', 'evidenceDigest', '0' * 64)}, 1, ['evidence digest does not match']),
    ('observed newline escaped', {'packet_change': setitem('observed', 'REGRESSED: a' + chr(10) + 'NEW REGRESSION FAKE')}, 1, ['\\x0a']),
]



def junction_case():
    """An evidence path that resolves through a junction to a folder outside the run must not verify. Skipped, and said so, when junctions cannot be made."""
    with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryDirectory() as outside:
        build(folder)
        run_dir = os.path.join(folder, 'runs', 'r1')
        with open(os.path.join(outside, 'progress.log'), 'w') as f:
            f.write('E1: step' + chr(10))
        os.remove(os.path.join(run_dir, 'progress.log'))
        os.rmdir(run_dir)
        made = subprocess.run(['cmd', '/c', 'mklink', '/J', run_dir, outside], capture_output=True, text=True)
        if made.returncode != 0:
            return None, 'junction could not be created'
        p = subprocess.run([sys.executable, READER, '--results-dir', folder, '--notices', '0'], capture_output=True, text=True)
        out = p.stdout + p.stderr
        subprocess.run(['cmd', '/c', 'rmdir', run_dir], capture_output=True)
        return ('not confined to this run' in out and 'evidence: verified' not in out), out[-300:]

failed = 0
for name, kwargs, code, needles in CASES:
    got, out = run(**kwargs)
    ok = got == code and all(n in out for n in needles) and 'Traceback' not in out and (chr(10) + 'NEW REGRESSION FAKE') not in out
    failed += not ok
    print('%s  %s (exit %d)' % ('ok  ' if ok else 'FAIL', name, got))
    if not ok:
        print('      expected exit %d containing %r; output tail: %s' % (code, needles, out[-500:].replace(chr(10), ' | ')))
result, detail = junction_case()
if result is None:
    print('SKIP  evidence path through a junction (%s)' % detail)
else:
    failed += not result
    print('%s  evidence path through a junction is not confined' % ('ok  ' if result else 'FAIL'))
    if not result:
        print('      ' + detail.replace(chr(10), ' | '))
print('ALL PASSED' if not failed else '%d FAILED' % failed)
sys.exit(1 if failed else 0)
