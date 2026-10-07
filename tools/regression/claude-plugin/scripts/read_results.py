#!/usr/bin/env python3
"""Read-only summary of the last recorded Starlight fix-ledger results. Writes nothing and runs nothing.

Exit codes: 0 every entry VERIFIED and the suite finished successfully; 1 something needs attention (an entry is not VERIFIED, or the suite failed or did
not finish); 2 results unavailable or invalid (never an all-clear).
Output describes the LAST RECORDED results for the tested binary identity shown. With --repo and --build-dir it also compares that identity with the checkout and binaries on disk now
(reading the git HEAD file and hashing the binaries; it runs nothing); without them it does not compare.
All text taken from the results files is data: control characters are escaped, long text is cut, and free text is shown inside [[ ]]."""
import argparse, datetime, json, os, re, sys, time

DEFAULT = r'D:\projects\_recovery\regression-results'
SCHEMA = 'starlight-ledger-results-1'
STATES = {'VERIFIED', 'REGRESSED', 'NOT-CHECKED', 'COVERAGE-ERROR', 'SKIPPED'}
CONTROL = re.compile(r'[\x00-\x1f\x7f-\x9f\u2028\u2029]')


def clean(value, limit=240):
    text = str(value)
    text = CONTROL.sub(lambda m: '\\x%02x' % ord(m.group(0)) if ord(m.group(0)) < 256 else '\\u%04x' % ord(m.group(0)), text)
    return text if len(text) <= limit else text[:limit] + '...(cut)'


def data(value, limit=240):
    return '[[' + clean(value, limit) + ']]'


def load(path, attempts=3):
    last = None
    for attempt in range(attempts):
        try:
            with open(path, encoding='utf-8') as f:
                return json.load(f), None
        except FileNotFoundError:
            return None, 'missing'
        except (OSError, ValueError) as error:
            last = error
            time.sleep(0.2)
    return None, 'unreadable or not valid JSON after %d tries (%s)' % (attempts, clean(last, 120))


HEX64 = re.compile(r'^[0-9a-f]{64}$')


def is_state(value):
    return isinstance(value, str) and value in STATES


def problems(d):
    found = []
    if not isinstance(d, dict):
        return ['the results file is not a JSON object']
    if d.get('schema') != SCHEMA:
        found.append('schema is %s, expected %s' % (data(d.get('schema'), 60), SCHEMA))
    for key, kind in (('buildId', str), ('runId', str), ('createdUtc', str), ('identity', dict), ('suite', dict), ('entries', list)):
        if not isinstance(d.get(key), kind):
            found.append('field %s is missing or has the wrong type' % key)
    if found:
        return found
    if not d['buildId'].strip():
        found.append('buildId is empty')
    if not re.match(r'^[A-Za-z0-9+._-]+$', d['runId']):
        found.append('runId is empty or has unsafe characters')
    try:
        datetime.datetime.strptime(d['createdUtc'], '%Y-%m-%dT%H:%M:%SZ')
    except ValueError:
        found.append('createdUtc is not a valid UTC time')
    suite = d['suite']
    for key in ('started', 'completed'):
        if not isinstance(suite.get(key), bool):
            found.append('suite field %s is missing or not true/false' % key)
    if not isinstance(suite.get('timedOut'), bool):
        found.append('suite field timedOut is missing or not true/false')
    exit_code = suite.get('exitCode', 'missing')
    if exit_code == 'missing' or not (exit_code is None or (isinstance(exit_code, int) and not isinstance(exit_code, bool))):
        found.append('suite field exitCode is missing or not a whole number or null')
    if not (isinstance(suite.get('success'), bool) or (suite.get('success') is None and suite.get('completed') is False)):
        found.append('suite field success must be true/false once the suite completed (null only when it did not complete)')
    identity = d['identity']
    binaries = identity.get('binaries')
    if not isinstance(binaries, dict):
        found.append('identity.binaries is missing (no proof of which binaries were tested)')
    else:
        for name, info in binaries.items():
            if not isinstance(info, dict) or not isinstance(info.get('sha256'), str) or not HEX64.match(info['sha256']):
                found.append('binary %s has no valid sha256' % data(name, 60))
        if suite.get('started') is True:
            core = binaries.get('Starlight.NativeCoreChecks.exe')
            if not isinstance(core, dict) or not isinstance(core.get('bytes'), int) or core['bytes'] <= 0:
                found.append('the suite ran but identity has no sha256 and size for Starlight.NativeCoreChecks.exe')
    if 'gitHead' in identity and not isinstance(identity['gitHead'], str):
        found.append('identity.gitHead has the wrong type')
    if 'dirtyTrackedFiles' in identity and (not isinstance(identity['dirtyTrackedFiles'], int) or isinstance(identity['dirtyTrackedFiles'], bool)):
        found.append('identity.dirtyTrackedFiles has the wrong type')
    if not d['entries']:
        found.append('there are no ledger entries (an empty ledger proves nothing)')
    seen = set()
    for index, e in enumerate(d['entries']):
        if not isinstance(e, dict) or not isinstance(e.get('id'), str) or not e['id']:
            found.append('entry %d is malformed' % index)
            continue
        name = clean(e['id'], 40)
        if e['id'] in seen:
            found.append('entry id %s appears twice' % name)
        seen.add(e['id'])
        if not is_state(e.get('state')):
            found.append('entry %s has an unknown or non-text state %s' % (name, data(e.get('state'), 40)))
            continue
        tests = e.get('tests')
        if not isinstance(tests, list):
            found.append('entry %s has no tests list' % name)
            continue
        if e['state'] == 'VERIFIED' and not tests:
            found.append('entry %s is VERIFIED with no check evidence' % name)
        tokens = set()
        for t in tests:
            if (not isinstance(t, dict) or not isinstance(t.get('token'), str) or not t['token'] or not is_state(t.get('state'))
                    or not isinstance(t.get('reason'), str) or not isinstance(t.get('matched'), int) or isinstance(t.get('matched'), bool) or t['matched'] < 0):
                found.append('entry %s has a malformed test record' % name)
                continue
            if t['token'] in tokens:
                found.append('entry %s repeats test token %s' % (name, data(t['token'], 40)))
            tokens.add(t['token'])
            if e['state'] == 'VERIFIED' and t['state'] != 'VERIFIED':
                found.append('entry %s is VERIFIED but test %s is %s' % (name, data(t['token'], 40), t['state']))
            if e['state'] == 'VERIFIED' and t['matched'] < 1:
                found.append('entry %s is VERIFIED but test %s matched no checks' % (name, data(t['token'], 40)))
    return found


def age(created):
    try:
        then = datetime.datetime.strptime(created, '%Y-%m-%dT%H:%M:%SZ').replace(tzinfo=datetime.timezone.utc)
        seconds = (datetime.datetime.now(datetime.timezone.utc) - then).total_seconds()
        if seconds < 0:
            return 'recorded in the future (clock mismatch)'
        if seconds < 3600:
            return '%d minutes ago' % (seconds // 60)
        if seconds < 172800:
            return '%d hours ago' % (seconds // 3600)
        return '%d days ago' % (seconds // 86400)
    except (ValueError, TypeError):
        return 'age unknown'


PACKET_SCHEMA = 'starlight-failure-packet-1'
PACKET_CLASSES = ('DEFECT-CONFIRMED', 'PERF-LEAD', 'INCOMPLETE', 'ENVIRONMENTAL')
CLEAN_NEEDED = 2


def is_count(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def confined(results_dir, relative, top, run_id=None):
    """The real path of relative when it is a plain relative path that stays inside results_dir/top[/run_id] after resolving links and junctions, else None."""
    if not isinstance(relative, str) or not relative or os.path.isabs(relative) or '\\' in relative or relative.startswith('/'):
        return None
    parts = relative.split('/')
    if any(part in ('', '.', '..') for part in parts) or parts[0] != top or (run_id is not None and (len(parts) < 3 or parts[1] != run_id)):
        return None
    anchor = os.path.realpath(results_dir)
    full = os.path.realpath(os.path.join(anchor, *parts))
    root = os.path.join(anchor, top) if run_id is None else os.path.join(anchor, top, run_id)
    try:
        return full if os.path.commonpath([full, root]) == root else None
    except ValueError:
        return None


def file_sha256(path):
    import hashlib
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def expected_kind(entry, suite):
    """The producer's mapping, recomputed from the recorded state so a packet cannot contradict the report."""
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


def packet_problems(packet, ref, report, entry):
    found = []
    entry_id = entry['id']
    if not isinstance(packet, dict):
        return ['not a JSON object']
    if packet.get('schema') != PACKET_SCHEMA:
        found.append('wrong schema')
    for key in ('failureId', 'class', 'failureKind', 'subsystem', 'fingerprint', 'eventId', 'runId', 'expected', 'observed', 'reproductionStatus', 'note'):
        if not isinstance(packet.get(key), str) or not packet.get(key):
            found.append('%s is missing or not text' % key)
    if packet.get('failureId') != entry_id:
        found.append('failureId does not match the entry')
    if packet.get('class') not in PACKET_CLASSES:
        found.append('unknown class')
    if packet.get('buildIdentity') != report.get('identity'):
        found.append('buildIdentity does not equal the full identity of the tested build')
    if packet.get('runId') != report.get('runId'):
        found.append('runId does not match the report run')
    if isinstance(packet.get('fingerprint'), str) and packet.get('eventId') != '%s|%s|%s|%s' % (packet['fingerprint'], report.get('buildId'), report.get('runId'), report.get('createdUtc')):
        found.append('eventId is not fingerprint|buildId|runId|recorded time of this report')
    if packet.get('eventId') != ref.get('eventId'):
        found.append('eventId does not match the report reference')
    checks = packet.get('failingChecks')
    relevant = sorted(t['token'] for t in entry.get('tests', []) if t.get('state') != 'VERIFIED' and t.get('token'))
    if not isinstance(checks, list) or not all(isinstance(c, str) and c for c in checks):
        found.append('failingChecks is not a list of text')
    elif checks != relevant:
        found.append('failingChecks do not match the non-verified tests the report recorded for this entry')
    repro = packet.get('reproduction')
    if not isinstance(repro, dict) or not isinstance(repro.get('checkIds'), list) or not all(isinstance(c, str) for c in repro.get('checkIds', [])) \
            or not isinstance(repro.get('runner'), str) or not isinstance(repro.get('args'), list) or not all(isinstance(a, str) for a in repro.get('args', [])) \
            or repro.get('attribution') not in ('known', 'unknown'):
        found.append('reproduction is malformed')
    elif isinstance(checks, list) and repro['checkIds'] != checks:
        found.append('reproduction check ids differ from failingChecks')
    elif repro['attribution'] != ('known' if relevant else 'unknown'):
        found.append('reproduction attribution contradicts the report')
    kind, cls = expected_kind(entry, report.get('suite', {}))
    if packet.get('failureKind') != kind or packet.get('class') != cls:
        found.append('class/failureKind contradict the report (expected %s / %s)' % (cls, kind))
    expected_subsystem = entry.get('subsystem') or entry.get('title', '')
    if packet.get('subsystem') != expected_subsystem:
        found.append('subsystem does not match the report entry')
    family = 'core-check:%s|%s|%s|%s' % (entry_id, ','.join(relevant) or 'attribution-unknown', kind, entry.get('subsystem') or entry.get('class', 'unknown'))
    if packet.get('fingerprint') != family:
        found.append('fingerprint is not the failure family derived from the report')
    evidence = packet.get('evidence')
    if not isinstance(evidence, dict) or not isinstance(evidence.get('files'), list) or not (evidence.get('evidenceDigest') is None or isinstance(evidence.get('evidenceDigest'), str)):
        found.append('evidence is malformed')
    else:
        for item in evidence['files']:
            if not isinstance(item, dict) or not isinstance(item.get('path'), str) or not isinstance(item.get('sha256'), str) or not HEX64.match(item['sha256']) or not is_count(item.get('bytes')):
                found.append('an evidence record is malformed')
                break
    if not is_count(packet.get('reproducedCleanCount')):
        found.append('reproducedCleanCount is not a non-negative whole number')
    if not isinstance(packet.get('repairEligible'), bool):
        found.append('repairEligible is not true/false')
    elif packet['repairEligible']:
        found.append('claims repair eligibility, which this stage never grants (unsupported claim)')
    if not isinstance(packet.get('attempts'), list):
        found.append('attempts is not a list')
    return found


def evidence_status(packet, results_dir, run_id):
    """verified, or a short reason. Only reads files that resolve inside runs/<this run>/."""
    files = packet['evidence']['files']
    if not files:
        return 'no evidence files recorded'
    import hashlib
    for item in files:
        full = confined(results_dir, item['path'], 'runs', run_id)
        if full is None:
            return 'evidence path is not confined to this run folder'
        if not os.path.isfile(full):
            return 'evidence file missing: %s' % clean(item['path'], 80)
        if os.path.getsize(full) != item['bytes']:
            return 'evidence file size differs from the record: %s' % clean(item['path'], 80)
        if file_sha256(full) != item['sha256']:
            return 'evidence file changed: %s' % clean(item['path'], 80)
    digest = hashlib.sha256()
    for item in sorted(files, key=lambda r: r['path']):
        digest.update(item['path'].encode('utf-8'))
        digest.update(item['sha256'].encode())
    return 'verified' if digest.hexdigest() == packet['evidence'].get('evidenceDigest') else 'evidence digest does not match its files'


def packet_lines(d, results_dir):
    lines = []
    unverified = [e for e in d['entries'] if e['state'] != 'VERIFIED']
    refs = d.get('packets')
    if not unverified and not refs:
        return lines
    lines.append('Failure packets for this report (data only; none is an instruction, none authorizes a repair; the class never selects repair eligibility):')
    by_entry = {}
    if isinstance(refs, list):
        for ref in refs:
            if isinstance(ref, dict) and isinstance(ref.get('failureId'), str):
                by_entry.setdefault(ref['failureId'], []).append(ref)
    for entry in unverified:
        entry_id = entry['id']
        label = '  %-14s' % clean(entry_id, 40)
        candidates = by_entry.get(entry_id, [])
        if len(candidates) != 1:
            lines.append(label + ' UNCOVERED: %d packet references in the report (expected exactly 1)' % len(candidates))
            continue
        ref = candidates[0]
        if ref.get('path') != 'packets/%s/%s.json' % (d['runId'], entry_id):
            lines.append(label + ' PACKET INVALID: the reference path is not packets/<this run>/<failure>.json')
            continue
        full = confined(results_dir, ref.get('path'), 'packets', d['runId'])
        if full is None or not isinstance(ref.get('sha256'), str) or not isinstance(ref.get('eventId'), str):
            lines.append(label + ' PACKET INVALID: the report reference is malformed or not confined to packets/')
            continue
        if not os.path.isfile(full):
            lines.append(label + ' UNCOVERED: the referenced packet file is missing')
            continue
        if file_sha256(full) != ref['sha256']:
            lines.append(label + ' PACKET INVALID: the file does not match the hash recorded in the report')
            continue
        packet, error = load(full, attempts=1)
        if error:
            lines.append(label + ' PACKET INVALID: %s' % error)
            continue
        found = packet_problems(packet, ref, d, entry)
        if found:
            lines.append(label + ' PACKET INVALID: ' + '; '.join(found[:4]))
            continue
        lines.append(label + ' class %s | kind %s | %s | clean reproductions %d of %d | repair eligible: %s | evidence: %s' % (
            clean(packet['class'], 20), clean(packet['failureKind'], 24), clean(packet['reproductionStatus'], 60), packet['reproducedCleanCount'], CLEAN_NEEDED,
            packet['repairEligible'], evidence_status(packet, results_dir, d['runId'])))
        lines.append('      failing checks: %s | %s' % (data(', '.join(packet['failingChecks']) or 'attribution unknown', 160), data(packet['observed'], 200)))
    return lines


def git_head(repo):
    """Full commit id of HEAD read from the .git folder or worktree file without running git; (None, reason) when it cannot be read."""
    try:
        dot = os.path.join(repo, '.git')
        if os.path.isfile(dot):
            text = open(dot, encoding='utf-8').read().strip()
            if not text.startswith('gitdir:'):
                return None, '.git file has no gitdir line'
            gitdir = text[7:].strip()
            if not os.path.isabs(gitdir):
                gitdir = os.path.normpath(os.path.join(repo, gitdir))
        else:
            gitdir = dot
        head = open(os.path.join(gitdir, 'HEAD'), encoding='utf-8').read().strip()
        if re.fullmatch(r'[0-9a-f]{40}', head):
            return head, None
        if not head.startswith('ref:'):
            return None, 'HEAD is neither a commit id nor a ref'
        ref = head[4:].strip()
        common = gitdir
        if os.path.isfile(os.path.join(gitdir, 'commondir')):
            common = os.path.normpath(os.path.join(gitdir, open(os.path.join(gitdir, 'commondir'), encoding='utf-8').read().strip()))
        for base in (gitdir, common):
            path = os.path.join(base, *ref.split('/'))
            if os.path.isfile(path):
                value = open(path, encoding='utf-8').read().strip()
                if re.fullmatch(r'[0-9a-f]{40}', value):
                    return value, None
        packed = os.path.join(common, 'packed-refs')
        if os.path.isfile(packed):
            for line in open(packed, encoding='utf-8').read().splitlines():
                if line.endswith(' ' + ref) and re.match(r'^[0-9a-f]{40} ', line):
                    return line[:40], None
        return None, 'ref %s not found' % ref
    except (OSError, ValueError) as error:
        return None, clean(error, 100)


def current_build_lines(d, repo, build_dir):
    """Compare the recorded identity with the checkout and binaries on disk now. Returns (lines, stale)."""
    lines, stale = [], False
    ident = d['identity']
    head, why = git_head(repo)
    if head is None:
        lines.append('CURRENT BUILD: the checkout in %s could not be read (%s), so the recorded git commit is NOT confirmed.' % (data(repo, 120), data(why, 100)))
        stale = True
    elif ident.get('gitHead') and head != ident.get('gitHead'):
        lines.append('CURRENT BUILD: STALE. The checkout is at %s but these results are for %s.' % (data(head[:12], 20), data(str(ident.get('gitHead'))[:12], 20)))
        stale = True
    else:
        lines.append('CURRENT BUILD: git commit %s matches the recorded one (uncommitted changes since then are not detected here).' % data(head[:12], 20))
    differ, missing, same = [], [], 0
    for name, rec in sorted(ident.get('binaries', {}).items()):
        path = os.path.join(build_dir, name)
        if not os.path.isfile(path):
            missing.append(name)
            continue
        try:
            current = file_sha256(path)
        except OSError:
            missing.append(name)
            continue
        if current == rec.get('sha256'):
            same += 1
        else:
            differ.append(name)
    if differ or missing:
        stale = True
        if differ:
            lines.append('CURRENT BUILD: STALE. These binaries on disk differ from the tested ones: %s' % data(', '.join(differ), 200))
        if missing:
            lines.append('CURRENT BUILD: these tested binaries are not in %s: %s' % (data(build_dir, 120), data(', '.join(missing), 200)))
    else:
        lines.append('CURRENT BUILD: all %d tested binaries in %s are byte-identical to the ones tested.' % (same, data(build_dir, 120)))
    return lines, stale


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--repo')
    ap.add_argument('--build-dir')
    ap.add_argument('--results-dir', default=DEFAULT)
    ap.add_argument('--notices', type=int, default=10)
    a = ap.parse_args()
    if bool(a.repo) != bool(a.build_dir):
        ap.error('--repo and --build-dir go together')
    latest = os.path.join(a.results_dir, 'latest.json')
    d, error = load(latest)
    if error == 'missing':
        print('Ledger results are unavailable: no latest.json in %s. Nothing is verified. (Results are produced by tools/regression/run_ledger.py in the engine repo.)' % data(a.results_dir, 120))
        return 2
    if error:
        print('Ledger results are unavailable: latest.json is %s. Nothing is verified.' % error)
        return 2
    bad = problems(d)
    if bad:
        print('Ledger results are INVALID, so nothing is verified:')
        for line in bad:
            print('  - ' + line)
        return 2
    ident = d['identity']
    print('LAST RECORDED results (%s).' % ('compared with the checkout and binaries on disk, see CURRENT BUILD below' if a.repo else 'not a check of the build running now; the current build identity has not been compared'))
    print('Tested build %s | git %s | %s modified tracked files | recorded %s (%s)' % (
        data(d['buildId'], 80), data(str(ident.get('gitHead'))[:12], 20), data(ident.get('dirtyTrackedFiles'), 10), data(d['createdUtc'], 30), age(d['createdUtc'])))
    suite = d['suite']
    suite_ok = suite['started'] and suite['completed'] and suite['success'] is True and suite['exitCode'] == 0 and suite['timedOut'] is False
    print('Whole suite: started=%s completed=%s success=%s exitCode=%s timedOut=%s checks=%s%s' % (suite['started'], suite['completed'], suite['success'], data(suite['exitCode'], 20), suite['timedOut'], data(d.get('suiteCheckCount'), 12),
          '' if suite_ok else '   <-- the suite itself did not finish cleanly'))
    if suite['completed'] and suite['success'] is True and not suite_ok:
        print('Conflict: the suite wrote a passing result but the process %s. The entry states below come from that result; treat the whole run as not clean.' % ('timed out' if suite['timedOut'] else 'exited with code %s' % data(suite['exitCode'], 20)))
    attention = not suite_ok
    if a.repo:
        build_lines, stale = current_build_lines(d, a.repo, a.build_dir)
        for line in build_lines:
            print(line)
        attention = attention or stale
    for e in d['entries']:
        state = e['state']
        flag = '' if state == 'VERIFIED' else '   <-- needs attention'
        attention = attention or state != 'VERIFIED'
        reasons = '; '.join(clean(t.get('reason', ''), 120) for t in e.get('tests', []) if isinstance(t, dict))
        print('  %-14s %-14s %s%s' % (clean(e['id'], 40), state, data(reasons), flag))
        strength = e.get('testStrength')
        if strength:
            print('      proof strength: %s' % data(strength, 400))
    for line in packet_lines(d, a.results_dir):
        print(line)
    print('Note: VERIFIED means the listed checks passed on that binary. It does not mean the fix is proven beyond what the proof strength says, and it is not repair coverage.')
    notices = os.path.join(a.results_dir, 'notices.log')
    if a.notices > 0 and os.path.isfile(notices):
        try:
            lines = open(notices, encoding='utf-8', errors='replace').read().splitlines()[-a.notices:]
        except OSError:
            lines = []
        if lines:
            print('Historical transitions (old events, may not match the entries above; this is not an alert feed):')
            for l in lines:
                print('  ' + data(l, 300))
    return 1 if attention else 0


if __name__ == '__main__':
    sys.exit(main())
