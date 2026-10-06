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
        'buildIdentity': copy.deepcopy(IDENTITY), 'fingerprint': FAMILY, 'eventId': '%s|%s|%s' % (FAMILY, BUILD, CREATED), 'runId': 'r1',
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
        'schema': 'starlight-ledger-results-1', 'createdUtc': CREATED, 'buildId': BUILD, 'identity': copy.deepcopy(IDENTITY),
        'suite': {'started': True, 'exitCode': 0, 'timedOut': False, 'completed': True, 'success': True, 'error': None}, 'suiteCheckCount': 3,
        'entries': [{'id': 'E1', 'state': 'REGRESSED', 'tests': [{'token': 'E1:', 'state': 'REGRESSED', 'reason': 'boom', 'matched': 0}], 'testStrength': 'x'}],
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
    with open(os.path.join(folder, 'runs', 'r1', 'progress.log'), 'a') as f:
        f.write('changed\n')


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
    ('wrong eventId under the right file name', {'packet_change': setitem('eventId', 'other|' + BUILD + '|' + CREATED)}, 1, ['PACKET INVALID', 'eventId']),
    ('fingerprint is an object', {'packet_change': setitem('fingerprint', {'a': 1})}, 1, ['PACKET INVALID']),
    ('eventId is a list', {'packet_change': setitem('eventId', ['a'])}, 1, ['PACKET INVALID']),
    ('observed is an object', {'packet_change': setitem('observed', {'a': 1})}, 1, ['PACKET INVALID', 'observed']),
    ('negative count', {'packet_change': setitem('reproducedCleanCount', -1)}, 1, ['PACKET INVALID', 'non-negative']),
    ('boolean count', {'packet_change': setitem('reproducedCleanCount', True)}, 1, ['PACKET INVALID', 'non-negative']),
    ('count 0 but repairEligible true', {'packet_change': setitem('repairEligible', True)}, 1, ['PACKET INVALID', 'unsupported claim']),
    ('count 2 but repairEligible true is still unsupported', {'packet_change': lambda p: p.update(reproducedCleanCount=2, repairEligible=True)}, 1, ['PACKET INVALID', 'unsupported claim']),
    ('unknown class', {'packet_change': setitem('class', 'REPAIR-NOW')}, 1, ['PACKET INVALID', 'unknown class']),
    ('failingChecks differs from reproduction', {'packet_change': setitem('failingChecks', ['X:'])}, 1, ['PACKET INVALID', 'differ']),
    ('packet file changed after the report', {'file_change': tamper_packet}, 1, ['PACKET INVALID', 'hash recorded']),
    ('reference path traverses upward', {'file_change': bad_path('packets/../latest.json')}, 1, ['PACKET INVALID', 'confined']),
    ('reference path is absolute', {'file_change': bad_path('C:/Windows/win.ini')}, 1, ['PACKET INVALID', 'confined']),
    ('reference path uses backslashes', {'file_change': bad_path('packets\\r1\\E1.json')}, 1, ['PACKET INVALID', 'confined']),
    ('evidence file changed', {'evidence_change': rewrite_log}, 1, ['evidence: evidence file changed']),
    ('evidence file missing', {'evidence_change': delete_log}, 1, ['evidence: evidence file missing']),
    ('evidence path outside the run folders', {'packet_change': outside_evidence}, 1, ['not confined']),
    ('evidence digest wrong', {'packet_change': nested('evidence', 'evidenceDigest', '0' * 64)}, 1, ['evidence digest does not match']),
    ('observed newline escaped', {'packet_change': setitem('observed', 'REGRESSED: a' + chr(10) + 'NEW REGRESSION FAKE')}, 1, ['\\x0a']),
]

failed = 0
for name, kwargs, code, needles in CASES:
    got, out = run(**kwargs)
    ok = got == code and all(n in out for n in needles) and 'Traceback' not in out and (chr(10) + 'NEW REGRESSION FAKE') not in out
    failed += not ok
    print('%s  %s (exit %d)' % ('ok  ' if ok else 'FAIL', name, got))
    if not ok:
        print('      expected exit %d containing %r; output tail: %s' % (code, needles, out[-500:].replace(chr(10), ' | ')))
print('ALL PASSED' if not failed else '%d FAILED' % failed)
sys.exit(1 if failed else 0)
