"""Read-only, freshness-checked queries over the existing analysis source index."""
import argparse
import fnmatch
import hashlib
import json
from pathlib import Path


def lookup(root, run, *, file='*', symbol=None, kind=None, text=None,
           line=None, context=3, expect=None):
    root, run = Path(root).resolve(), Path(run).resolve()
    index_path = run / 'source-index.json'
    raw_index = index_path.read_bytes()
    seal_path = run / 'artifact-hashes.json'
    issues, matches = [], []
    if not seal_path.exists():
        issues.append('Missing artifact seal: index provenance unverified')
    else:
        expected = json.loads(seal_path.read_text(encoding='utf-8')).get('source-index.json')
        if expected != hashlib.sha256(raw_index).hexdigest():
            return {'matches': [], 'issues': ['Source index seal mismatch'], 'status': 'blocked'}
    for entry in json.loads(raw_index):
        relative = entry['path']
        if not fnmatch.fnmatchcase(relative, file):
            continue
        path = (root / relative).resolve()
        if root not in path.parents:
            issues.append(f'{relative}: path escapes repository')
            continue
        try:
            raw = path.read_bytes()
            digest = hashlib.sha256(raw).hexdigest()
            if digest != entry.get('sha256') or entry.get('matches_inventory') is False:
                issues.append(f'{relative}: stale or inconsistent index; rescan required')
                continue
            lines = raw.decode('utf-8', errors='strict').splitlines()
        except (OSError, UnicodeError) as error:
            issues.append(f'{relative}: {error}')
            continue
        declarations = entry.get('symbols', [])
        selected = {s['line'] for s in declarations
                    if (symbol is None or s['name'] == symbol)
                    and (kind is None or s['kind'] == kind)}
        for number, value in enumerate(lines, 1):
            if line is not None and number != line:
                continue
            if (symbol is not None or kind is not None) and number not in selected:
                continue
            if text is not None and text not in value:
                continue
            if expect is not None and value != expect:
                continue
            matches.append({'file': relative, 'line': number, 'text': value,
                            'sha256': digest,
                            'context': [{'line': i + 1, 'text': lines[i]}
                                        for i in range(max(0, number-context-1),
                                                       min(len(lines), number+context))]})
    status = 'blocked' if issues else ('unique' if len(matches) == 1 else
                                      'not-found' if not matches else 'ambiguous')
    return {'status': status, 'matches': matches, 'issues': issues,
            'scope': 'Indexed files only; lexical declarations, not compiler-resolved semantics. '
                     'A unique match is not proof an edit is safe. Recheck the hash before editing.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--run', type=Path, required=True)
    parser.add_argument('--file', default='*', help='Repository-relative, case-sensitive glob')
    parser.add_argument('--symbol', help='Exact indexed declaration name')
    parser.add_argument('--kind', choices=['types', 'native_exports', 'function_candidates'])
    parser.add_argument('--text', help='Literal case-sensitive text on the matching line')
    parser.add_argument('--line', type=int)
    parser.add_argument('--expect', help='Exact full line, including indentation')
    parser.add_argument('--context', type=int, default=3)
    args = parser.parse_args()
    if not any((args.symbol, args.kind, args.text, args.line, args.expect)):
        parser.error('Supply a symbol, kind, text, line or expected-line filter')
    if not 0 <= args.context <= 50 or (args.line is not None and args.line < 1):
        parser.error('Context must be 0..50 and line must be positive')
    try:
        result = lookup(**vars(args))
    except (OSError, ValueError, KeyError, TypeError) as error:
        result = {'status': 'blocked', 'matches': [], 'issues': [str(error)]}
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return {'unique': 0, 'not-found': 1, 'ambiguous': 2, 'blocked': 3}[result['status']]


if __name__ == '__main__':
    raise SystemExit(main())
