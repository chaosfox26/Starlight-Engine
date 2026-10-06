#!/usr/bin/env python3
"""Read-only summary of the last recorded Starlight fix-ledger results. Writes nothing and runs nothing.

Exit codes: 0 every entry VERIFIED and the suite finished successfully; 1 something needs attention (an entry is not VERIFIED, or the suite failed or did
not finish); 2 results unavailable or invalid (never an all-clear).
Output describes the LAST RECORDED results for the tested binary identity shown; it does not compare them with the build that is running now.
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
    for key, kind in (('buildId', str), ('createdUtc', str), ('identity', dict), ('suite', dict), ('entries', list)):
        if not isinstance(d.get(key), kind):
            found.append('field %s is missing or has the wrong type' % key)
    if found:
        return found
    if not d['buildId'].strip():
        found.append('buildId is empty')
    try:
        datetime.datetime.strptime(d['createdUtc'], '%Y-%m-%dT%H:%M:%SZ')
    except ValueError:
        found.append('createdUtc is not a valid UTC time')
    suite = d['suite']
    for key in ('started', 'completed'):
        if not isinstance(suite.get(key), bool):
            found.append('suite field %s is missing or not true/false' % key)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--results-dir', default=DEFAULT)
    ap.add_argument('--notices', type=int, default=10)
    a = ap.parse_args()
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
    print('LAST RECORDED results (not a check of the build running now; the current build identity has not been compared).')
    print('Tested build %s | git %s | %s modified tracked files | recorded %s (%s)' % (
        data(d['buildId'], 80), data(str(ident.get('gitHead'))[:12], 20), data(ident.get('dirtyTrackedFiles'), 10), data(d['createdUtc'], 30), age(d['createdUtc'])))
    suite = d['suite']
    suite_ok = suite['started'] and suite['completed'] and suite['success'] is True
    print('Whole suite: started=%s completed=%s success=%s checks=%s%s' % (suite['started'], suite['completed'], suite['success'], data(d.get('suiteCheckCount'), 12),
          '' if suite_ok else '   <-- the suite itself did not finish cleanly'))
    attention = not suite_ok
    for e in d['entries']:
        state = e['state']
        flag = '' if state == 'VERIFIED' else '   <-- needs attention'
        attention = attention or state != 'VERIFIED'
        reasons = '; '.join(clean(t.get('reason', ''), 120) for t in e.get('tests', []) if isinstance(t, dict))
        print('  %-14s %-14s %s%s' % (clean(e['id'], 40), state, data(reasons), flag))
        strength = e.get('testStrength')
        if strength:
            print('      proof strength: %s' % data(strength, 400))
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
