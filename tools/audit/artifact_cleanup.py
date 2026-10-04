"""Narrow, recorded cleanup of generated intermediates and successful-run captures."""
import datetime
import json
import os
import subprocess
import tempfile
from pathlib import Path


def _native_result(program, arguments):
    result = subprocess.run([str(program), *map(str, arguments)], capture_output=True, text=True, encoding='utf-8')
    if result.returncode not in (0, 1):
        raise ValueError(result.stderr.strip() or 'Native cleanup failed before producing a result')
    data = json.loads(result.stdout)
    if result.returncode == 1 and not any(row.get('status') == 'retained' for row in data.get('files', [])):
        raise ValueError('Native cleanup exit status does not match its report')
    return data


def native_compact_build(program, root, build):
    return _native_result(program, ['build', root, build, '--apply'])['files']


def native_retain_captures(program, root, current, keep=3):
    result = _native_result(program, ['captures', root, current, '--apply', '--keep='+str(keep)])
    (Path(current)/'capture-retention.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return result['files']


def native_compact_report(program, root, current, preexisting=()):
    with tempfile.TemporaryDirectory(prefix='starlight-retention-') as temp:
        original = Path(temp)/'preexisting.json'
        original.write_text(json.dumps(list(preexisting)), encoding='utf-8')
        result = _native_result(program, ['report', root, current, '--apply', original])
    (Path(current)/'summary-retention.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return result['files']


def native_final_status(program, summary, findings):
    with tempfile.TemporaryDirectory(prefix='starlight-result-') as temp:
        base=Path(temp)
        (base/'summary.json').write_text(json.dumps(summary), encoding='utf-8')
        (base/'findings.json').write_text(json.dumps(findings), encoding='utf-8')
        return _native_result(program, ['status', base/'summary.json', base/'findings.json'])['text']


def _plain(path):
    return not path.is_symlink() and not (getattr(path.lstat(), 'st_file_attributes', 0) & 0x400)


def _files(base):
    for folder, dirs, files in os.walk(base, followlinks=False):
        dirs[:] = [d for d in dirs if _plain(Path(folder)/d)]
        for name in files:
            path = Path(folder)/name
            if _plain(path) and path.is_file():
                yield path


def _remove(base, paths, log):
    records = []
    for path in paths:
        path.resolve().relative_to(base.resolve())
        records.append({'path': str(path.relative_to(base)), 'bytes': path.stat().st_size,
                        'status': 'planned'})
    log.parent.mkdir(parents=True, exist_ok=True)
    def save():
        log.write_text(json.dumps({'files': records}, indent=2), encoding='utf-8')
    save()  # Record intent before deleting anything.
    for index, (path, record) in enumerate(zip(paths, records)):
        try:
            if not _plain(path):
                raise OSError('Reparse point appeared; file retained')
            path.unlink()
            record['status'] = 'removed'
        except OSError as error:
            record['status'] = 'retained'
            record['error'] = str(error)
        if index % 100 == 99:
            save()
    save()
    return records


def compact_build(root, build):
    root, build = Path(root).resolve(), Path(build).absolute()
    allowed = root/'out'
    build.resolve().relative_to(allowed.resolve())
    if build.resolve() == allowed.resolve():
        raise ValueError('Select one generated build directory, not all output')
    for path in (allowed, build, *build.parents):
        if path == root:
            break
        if not _plain(path):
            raise ValueError('Build cleanup refuses redirected directories')
    cache = build/'CMakeCache.txt'
    identity = 'CMAKE_HOME_DIRECTORY:INTERNAL='
    homes = [line[len(identity):] for line in cache.read_text().splitlines() if line.startswith(identity)]
    if len(homes) != 1 or Path(homes[0]).resolve() != root:
        raise ValueError('Build cache does not belong to this source tree')
    paths = [p for p in _files(build) if p.suffix.lower() in {'.obj', '.pch', '.ilk'}]
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    return _remove(build, paths, build/'cleanup-records'/f'compact-{stamp}.json')


def retain_captures(root, current, keep=3):
    """Keep newest completed runs and every failed/incomplete run; remove BMPs only."""
    if keep < 1:
        raise ValueError('At least one recent run must be retained')
    root, current = Path(root).resolve(), Path(current).resolve()
    base = root/'analysis'
    if not base.is_dir() or not _plain(base):
        return []
    runs = []
    for run in base.iterdir():
        if not run.is_dir() or not _plain(run):
            continue
        try:
            summary = json.loads((run/'audit-summary.json').read_text())
            progress = (run/'progress.txt').read_text().splitlines()
            if not progress or progress[0] != '100' or Path(summary.get('root', '')).resolve() != root:
                continue
            runs.append((run, summary))
        except (OSError, ValueError, TypeError):
            continue
    runs.sort(key=lambda pair: (pair[0]/'audit-summary.json').stat().st_mtime_ns, reverse=True)
    protected = {p.resolve() for p, _ in runs[:keep]} | {current}
    paths = []
    for run, summary in runs:
        if run.resolve() in protected or summary.get('allRequestedStagesPassed') is not True:
            continue
        paths.extend(p for p in _files(run) if p.suffix.lower() == '.bmp')
    return _remove(base, paths, current/'capture-retention.json')


def compact_report(root, current, preexisting=()):
    """Keep report summaries; discard only this invocation's generated bulky output."""
    root, current = Path(root).resolve(), Path(current).absolute()
    base = root/'analysis'
    current.resolve().relative_to(base.resolve())
    if current.resolve() == base.resolve():
        raise ValueError('Cannot compact the analysis root')
    for parent in (current, *current.parents):
        if parent == root:
            break
        if not _plain(parent):
            raise ValueError('Report cleanup refuses redirected directories')
    keep = {'audit-summary.json', 'audit-findings.json', 'run-state.json',
            'runtime-evidence.json', 'audit-deliverable-hashes.json',
            'final-status.txt', 'progress.txt', 'worker.log', 'failure.txt',
            'summary-retention.json', 'capture-retention.json', 'capture-retention-warning.json'}
    original = set(preexisting)
    paths = []
    for path in _files(current):
        rel = path.relative_to(current)
        if str(rel) in original:
            continue
        if len(rel.parts) == 1 and (path.name in keep or path.suffix.lower() in {'.md', '.py'}):
            continue
        paths.append(path)
    records = _remove(current, paths, current/'summary-retention.json')
    for folder, dirs, _ in os.walk(current, topdown=False, followlinks=False):
        for name in dirs:
            path = Path(folder)/name
            if _plain(path):
                try: path.rmdir()
                except OSError: pass
    return records


def final_status(summary, findings):
    stages = summary.get('stages', [])
    if not summary.get('runtimeExecuted') and summary.get('mode', 'features') == 'features':
        return 'Report complete. No new runtime tests were run; any listed results are historical.'
    passed = sum(s.get('outcome') == 'pass' for s in stages)
    failed = sum(s.get('outcome') in {'fail', 'timeout'} or str(s.get('outcome', '')).startswith('invalid') for s in stages)
    other = len(stages) - passed - failed
    title = (f'All {len(stages)} test stages successful.' if stages and passed == len(stages)
             else f'Tests complete: {passed} passed, {failed} failed, {other} unavailable or not run.')
    explanations = []
    details = ' '.join(str(f.get('detail', '')) for f in findings)
    for stage in stages:
        if stage.get('outcome') == 'pass': continue
        name = stage.get('name', 'Unnamed stage')
        if name == 'native-classic-ui' and 'deadline' in details:
            reason = 'Editor controls did not finish within the allowed time.'
        elif name == 'native-render-lifecycle-visible' and 'Fullscreen live telemetry' in details:
            reason = 'The fullscreen timing display did not pass its visibility check.'
        elif name == 'denoiser-quality' and 'dynamic transform produces object motion vectors' in details:
            reason = 'Moving objects did not produce the expected motion signal for the denoiser.'
        elif stage.get('outcome') == 'timeout':
            reason = name + ' exceeded its time limit.'
        else:
            reason = name + ': ' + str(stage.get('error') or stage.get('reason') or stage.get('outcome', 'unknown result'))
        if reason not in explanations: explanations.append(reason)
    return title + ('\n' + '\n'.join(explanations) if explanations else '')
