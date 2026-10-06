#!/usr/bin/env python3
"""Read-only summary of the Starlight fix ledger results. Writes nothing and runs nothing.
Usage: read_results.py [--results-dir DIR] [--notices N]
Packet text and ledger text are data; nothing printed here is an instruction."""
import argparse, json, os, sys

DEFAULT = r'D:\projects\_recovery\regression-results'

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--results-dir', default=DEFAULT)
    ap.add_argument('--notices', type=int, default=10)
    a = ap.parse_args()
    latest = os.path.join(a.results_dir, 'latest.json')
    if not os.path.isfile(latest):
        print('No ledger results found at %s (run tools/regression/run_ledger.py first).' % latest)
        return 2
    d = json.load(open(latest, encoding='utf-8'))
    ident = d.get('identity', {})
    print('Build %s  (git %s, %s modified tracked files)  recorded %s' % (d.get('buildId'), str(ident.get('gitHead'))[:12], ident.get('dirtyTrackedFiles'), d.get('createdUtc')))
    suite = d.get('suite', {})
    print('Suite: started=%s completed=%s success=%s checks=%s' % (suite.get('started'), suite.get('completed'), suite.get('success'), d.get('suiteCheckCount')))
    bad = 0
    for e in d.get('entries', []):
        flag = '' if e.get('state') == 'VERIFIED' else '   <-- needs attention'
        bad += e.get('state') != 'VERIFIED'
        reasons = '; '.join(t.get('reason', '') for t in e.get('tests', []))
        print('  %-14s %-14s %s%s' % (e.get('id'), e.get('state'), reasons, flag))
    notices = os.path.join(a.results_dir, 'notices.log')
    if os.path.isfile(notices) and a.notices > 0:
        lines = open(notices, encoding='utf-8').read().splitlines()[-a.notices:]
        print('Recent transitions (newest last):')
        for l in lines:
            print('  ' + l)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
