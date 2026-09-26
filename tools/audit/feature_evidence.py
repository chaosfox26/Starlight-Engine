"""Join declared features to evidence without inferring completion from filenames."""
import hashlib
import json
import re
from pathlib import Path

SCHEMA = 'starlight.feature-evidence.v1'


def feature_id(title):
    return re.sub(r'[^a-z0-9]+', '-', title.lower()).strip('-')


def connect(model, index, evidence, inventory=()):
    # Dependency provenance includes files deliberately omitted from the lexical
    # source atlas (vendored headers, build configuration and generated sources).
    # Compare against their stable byte hashes, never invent missing coverage.
    hashes = {s['path']: s.get('sha256') for s in inventory if s.get('stable') is True}
    hashes.update({s['path']: s.get('sha256') for s in index})
    features = {}
    for f in model['features']:
        key = feature_id(f['name'])
        features[key] = {
            'id': key, 'name': f['name'], 'implementation': f['implementation_status'],
            'sources': sorted({e['file'] for e in f['evidence']}),
            'limits': f['limits'], 'evidence': [], 'issues': [],
            'verification': 'No explicitly linked runtime evidence',
        }
    warnings = []
    for e in evidence:
        data = e.get('data')
        if not isinstance(data, dict) or data.get('schema') != SCHEMA:
            warnings.append({'path': e['path'], 'reason': e.get('error', 'Legacy/unrecognized schema; no inferred feature verdict')})
            continue
        refs = data.get('features')
        if not isinstance(refs, list) or not refs or not all(isinstance(x, str) for x in refs):
            warnings.append({'path': e['path'], 'reason': 'Missing or invalid feature IDs'})
            continue
        tested = data.get('sourceHashes', {})
        build = data.get('build', {})
        valid_build = isinstance(build, dict) and isinstance(build.get('sha256'), str) and bool(re.fullmatch('[0-9a-fA-F]{64}', build['sha256']))
        for ref in refs:
            if ref not in features:
                warnings.append({'path': e['path'], 'reason': 'Unknown feature ID: ' + ref})
                continue
            f = features[ref]
            if not isinstance(tested, dict) or not tested:
                freshness = 'unknown: no tested source hashes'
            elif any(hashes.get(p) != h or not h for p, h in tested.items()):
                freshness = 'stale or missing source'
            elif not all(p in tested for p in f['sources']):
                freshness = 'partial source coverage'
            else:
                freshness = 'indexed source matches; transitive build inputs not established'
            record = {'path': e['path'], 'sha256': e.get('sha256'),
                      'outcome': data.get('outcome', 'unknown'), 'workload': data.get('workload', 'unspecified'),
                      'build': build if isinstance(build, dict) else {},
                      'buildIdentityRecorded': valid_build, 'freshness': freshness}
            f['evidence'].append(record)
            for issue in data.get('issues', []) if isinstance(data.get('issues', []), list) else []:
                if isinstance(issue, dict):
                    f['issues'].append({'id': issue.get('id'), 'status': issue.get('status', 'unknown'),
                                        'summary': issue.get('summary', ''), 'evidence': e['path']})
            f['verification'] = 'Linked evidence exists; inspect workload, freshness and unresolved/conflicting issues'
    return {'schema': 'starlight.feature-status.v1', 'features': list(features.values()),
            'reviewRequired': warnings,
            'limits': 'Declared records are not trusted proof. No last-write-wins issue closure, whole-feature completion, or binary/source equivalence is inferred.'}


def write_report(out, graph):
    (out / 'feature-status.json').write_text(json.dumps(graph, indent=2), encoding='utf-8')
    lines = ['# Generated feature and evidence briefing', '', graph['limits'], '',
             'This report is generated from the current feature model and discovered evidence. Unlinked legacy tests remain review work, not silently converted passes.', '']
    for f in graph['features']:
        lines += ['## ' + f['name'], '', '**Feature ID:** `' + f['id'] + '`', '',
                  '**Implementation:** ' + f['implementation'], '', '**Verification:** ' + f['verification'], '',
                  '**Limits:** ' + f['limits'], '', '**Source:** ' + ', '.join(f['sources']), '']
        for e in f['evidence']:
            lines += [f"- {e['path']}: {e['outcome']}; {e['freshness']}; build identity recorded={e['buildIdentityRecorded']}; workload={e['workload']}"]
        for issue in f['issues']:
            lines += [f"- Issue {issue['id']}: {issue['status']} — {issue['summary']} ({issue['evidence']})"]
    lines += ['', '## Evidence requiring review', '']
    lines += [f"- {w['path']}: {w['reason']}" for w in graph['reviewRequired']]
    (out / 'GENERATED-FEATURE-BRIEFING.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def capture_record(executable, source_root, sources, features, outcome, workload, issues=()):
    """Call at test execution; never attach current hashes to historical results."""
    executable = Path(executable)
    return {'schema': SCHEMA, 'features': features, 'outcome': outcome, 'workload': workload,
            'build': {'path': str(executable), 'sha256': hashlib.sha256(executable.read_bytes()).hexdigest()},
            'sourceHashes': {p: hashlib.sha256((Path(source_root) / p).read_bytes()).hexdigest() for p in sources},
            'issues': list(issues)}
