"""Losslessly archive explicitly selected, inactive generated preview directories."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import zipfile


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def compact(root, selected, archive, apply, captures=False):
    root = root.resolve()
    archive = archive.resolve()
    if not archive.is_relative_to(root / 'out'):
        raise ValueError('Archive must stay within workspace out')
    latest = json.loads((root / 'out/native-package-checks/latest-check.json').read_text())
    sandbox = json.loads((root / 'out/integrated-sandbox/latest-check.json').read_text())
    protected = [Path(latest['package']).resolve(), Path(sandbox['configuration']).resolve().parent]
    bases = []
    for relative in selected:
        base = (root / relative).resolve()
        if captures:
            allowed = base.parent == root / 'analysis' or (
                base.name == 'results' and base.parent.parent == root / 'out/integrated-sandbox')
        else:
            allowed = base.parent == root / 'out/native-package-checks' or (
                base.name == 'input' and base.parent.parent == root / 'out/integrated-sandbox')
        if not allowed or not base.is_dir() or any(base == p or base.is_relative_to(p) for p in protected):
            raise ValueError('Not an inactive preview input directory: ' + str(base))
        if any(base == p or base.is_relative_to(p) or p.is_relative_to(base) for p in bases):
            raise ValueError('Overlapping selections')
        bases.append(base)
    records = []
    for base in bases:
        for folder, directories, files in os.walk(base, followlinks=False):
            for name in directories + files:
                path = Path(folder) / name
                if path.is_symlink() or path.is_junction():
                    raise ValueError('Reparse point in preview history')
            for name in files:
                path = Path(folder) / name
                if captures and path.suffix.lower() not in ('.bmp', '.bin'):
                    continue
                info = path.stat()
                records.append(dict(path=path.relative_to(root).as_posix(), bytes=info.st_size,
                                    modifiedNs=info.st_mtime_ns, sha256=digest(path)))
    report = dict(format='starlight-preview-history-v1', capturesOnly=captures,
                  restore='For each file, extract blobs/<sha256> to its recorded workspace-relative path. Verify its hash before use. Never overwrite current files.',
                  directories=[p.relative_to(root).as_posix() for p in bases], files=records,
                  originalBytes=sum(r['bytes'] for r in records), applied=False)
    if not apply:
        print(json.dumps(dict(directories=report['directories'], files=len(records), bytes=report['originalBytes'])))
        return
    archive.parent.mkdir(parents=True, exist_ok=True)
    journal = archive.with_suffix('.json')
    if archive.exists() or journal.exists():
        raise FileExistsError('Archive or journal already exists')
    with zipfile.ZipFile(archive, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        included = set()
        for record in records:
            source = root / record['path']
            if digest(source) != record['sha256']:
                raise RuntimeError('Source changed while archiving')
            if record['sha256'] not in included:
                output.write(source, 'blobs/' + record['sha256'])
                included.add(record['sha256'])
        output.writestr('restore-manifest.json', json.dumps(report, indent=2))
    with zipfile.ZipFile(archive) as check:
        for value in included:
            with check.open('blobs/' + value) as source:
                if hashlib.file_digest(source, 'sha256').hexdigest() != value:
                    raise RuntimeError('Archived content failed hash verification')
        if json.loads(check.read('restore-manifest.json')) != report:
            raise RuntimeError('Archived manifest differs')
    report.update(archive=archive.relative_to(root).as_posix(), archiveBytes=archive.stat().st_size,
                  archiveSha256=digest(archive), uniqueBlobs=len(included), verified=True)
    journal.write_text(json.dumps(report, indent=2), encoding='utf-8')
    for record in records:
        path = root / record['path']
        if not any(path.resolve().is_relative_to(base) for base in bases) or digest(path) != record['sha256']:
            raise RuntimeError('Source changed before removal; verified archive and journal retained')
        path.unlink()
    for base in bases:
        for folder, directories, files in os.walk(base, topdown=False):
            if not any(Path(folder).iterdir()):
                Path(folder).rmdir()
    report['applied'] = True
    journal.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('files', 'restore')}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--selection', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--apply', action='store_true')
    parser.add_argument('--captures', action='store_true')
    args = parser.parse_args()
    compact(args.root, json.loads(args.selection.read_text()), args.archive, args.apply, args.captures)
