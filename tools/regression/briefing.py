#!/usr/bin/env python3
"""One-page briefing: where Starlight repair work stands, read from the records, nothing run except `git`.

  python tools/regression/briefing.py [--repo <checkout>] [--results <folder>] [--tracker <folder>] [--notes <folder>]

Sections (each is skipped with a reason if its source is missing): branch and commits ahead, last ledger results with currentness against the checkout,
audit tracker counts, the decision queue size, the newest continuation file. Read-only; writes nothing."""
import argparse, glob, json, os, re, subprocess, sys, time

REPO = r'D:\projects\Starlight-Engine-repair'
RESULTS = r'D:\projects\_recovery\regression-results-branch'
TRACKER = r'D:\projects\_recovery\AUDIT-MASTER'
NOTES = r'D:\projects\_recovery\claude-fix-20261002'
BASE = 'ea4adf8'


def git(repo, *args):
    try:
        return subprocess.run(['git', '-C', repo] + list(args), capture_output=True, text=True, timeout=60,
                              creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0).stdout.strip()
    except Exception as error:
        return ''


def age(path):
    seconds = time.time() - os.path.getmtime(path)
    return '%d min ago' % (seconds / 60) if seconds < 7200 else '%.1f h ago' % (seconds / 3600)


def section_branch(repo):
    branch = git(repo, 'branch', '--show-current')
    if not branch:
        return ['Branch: unavailable (is %s a git checkout?)' % repo]
    ahead = git(repo, 'rev-list', '--count', BASE + '..HEAD')
    dirty = len([l for l in git(repo, 'status', '--porcelain', '--untracked-files=no').splitlines() if l.strip()])
    last = git(repo, 'log', '-1', '--format=%h %s')
    return ['Branch %s: %s commits ahead of %s, %d modified tracked files. Latest: %s' % (branch, ahead or '?', BASE, dirty, last[:100])]


def section_ledger(results, repo):
    path = os.path.join(results, 'latest.json')
    try:
        d = json.load(open(path, encoding='utf-8'))
    except (OSError, ValueError) as error:
        return ['Ledger: no readable results in %s (%s)' % (results, type(error).__name__)]
    counts = {}
    for e in d.get('entries', []):
        counts[e.get('state')] = counts.get(e.get('state'), 0) + 1
    head = git(repo, 'rev-parse', 'HEAD')
    recorded = (d.get('identity') or {}).get('gitHead', '')
    same = 'matches the checkout' if head and head == recorded else 'checkout is at %s (STALE or newer)' % (head[:12] or '?')
    suite = d.get('suite', {})
    return ['Ledger (%s, %s): %s | recorded build %s %s | suite %s checks, success=%s' % (
        d.get('createdUtc'), age(path), ', '.join('%s %d' % (k, v) for k, v in sorted(counts.items(), key=lambda kv: str(kv[0]))),
        recorded[:12], same, d.get('suiteCheckCount'), suite.get('success'))]


def section_since_ledger(results, repo):
    """Files changed since the recorded run and the check groups worth running first (guide only; the full run is what verifies)."""
    try:
        recorded = (json.load(open(os.path.join(results, 'latest.json'), encoding='utf-8')).get('identity') or {}).get('gitHead', '')
    except (OSError, ValueError):
        return []
    head = git(repo, 'rev-parse', 'HEAD')
    if not recorded or not head or recorded == head:
        return []
    files = [l for l in git(repo, 'diff', '--name-only', recorded + '..HEAD').splitlines() if l.strip()]
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import impact
        ranked, _ = impact.rank(repo, files, impact.groups_of(os.path.join(repo, 'tools', 'checks', 'core_checks.cpp')))
    except Exception:
        ranked = []
    top = ', '.join(g for g, _ in ranked[:4]) or 'none matched, run everything'
    return ['Since the recorded run: %d files changed. Run first: %s (a full run is still needed to verify)' % (len(files), top)]


def section_tracker(tracker):
    path = os.path.join(tracker, 'AUDIT-TRACKER.json')
    try:
        items = json.load(open(path, encoding='utf-8'))['items']
    except (OSError, ValueError, KeyError) as error:
        return ['Audit tracker: unavailable (%s)' % type(error).__name__]
    counts = {}
    for i in items:
        counts[i.get('state')] = counts.get(i.get('state'), 0) + 1
    return ['Audit tracker (%d items, updated %s): %s' % (len(items), age(path), ', '.join('%s %d' % (k, v) for k, v in sorted(counts.items(), key=lambda kv: str(kv[0]))))]


def section_notes(notes):
    queue = os.path.join(notes, 'DECISION-QUEUE.md')
    lines = []
    try:
        rows = [l for l in open(queue, encoding='utf-8').read().splitlines() if re.match(r'^\| \d+ \|', l)]
        lines.append('Decision queue: %d open items (%s, updated %s)' % (len(rows), queue, age(queue)))
    except OSError:
        lines.append('Decision queue: none found at %s' % queue)
    found = sorted(glob.glob(os.path.join(notes, 'CONTINUATION-*.md')), key=os.path.getmtime)
    lines.append('Newest continuation: %s (%s)' % (found[-1], age(found[-1])) if found else 'Continuation: none found')
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--repo', default=REPO)
    ap.add_argument('--results', default=RESULTS)
    ap.add_argument('--tracker', default=TRACKER)
    ap.add_argument('--notes', default=NOTES)
    a = ap.parse_args()
    for block in (section_branch(a.repo), section_ledger(a.results, a.repo), section_since_ledger(a.results, a.repo), section_tracker(a.tracker), section_notes(a.notes)):
        for line in block:
            print(line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
