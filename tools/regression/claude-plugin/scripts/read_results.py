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
    if not d['entries']:
        found.append('there are no ledger entries (an empty ledger proves nothing)')
    for index, e in enumerate(d['entries']):
        if not isinstance(e, dict) or not isinstance(e.get('id'), str):
            found.append('entry %d is malformed' % index)
        elif e.get('state') not in STATES:
            found.append('entry %s has an unknown state %s' % (clean(e['id'], 40), data(e.get('state'), 40)))
        elif not isinstance(e.get('tests', []), list):
            found.append('entry %s has a malformed tests list' % clean(e['id'], 40))
    suite = d['suite']
    for key in ('started', 'completed', 'success'):
        if not isinstance(suite.get(key), bool):
            found.append('suite field %s is missing or not true/false' % key)
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
    suite_ok = suite['started'] and suite['completed'] and suite['success']
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
