import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import zipfile


def digest(stream):
    return hashlib.file_digest(stream, 'sha256').hexdigest()


def archive_preview(package, destination):
    package = package.resolve(strict=True)
    manifest_path = package / 'starlight-package.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    expected = {}
    for row in manifest['files']:
        name = row['path']
        path = PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
            raise ValueError(f'Unsafe package path: {name}')
        if name in expected or name == 'starlight-package.json':
            raise ValueError(f'Duplicate package path: {name}')
        source = package / name
        if not source.resolve(strict=True).is_relative_to(package):
            raise ValueError(f'Package path escapes root: {name}')
        with source.open('rb') as stream:
            if digest(stream) != row['sha256']:
                raise ValueError(f'Package hash mismatch: {name}')
        expected[name] = row['sha256']
    with manifest_path.open('rb') as stream:
        expected['starlight-package.json'] = digest(stream)
    destination = destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(destination, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as output:
        for name in sorted(expected):
            output.write(package / name, name)
    with zipfile.ZipFile(destination) as output:
        if len(output.namelist()) != len(expected) or set(output.namelist()) != set(expected):
            raise ValueError('Archive file inventory differs from package manifest')
        for name, value in expected.items():
            with output.open(name) as stream:
                if digest(stream) != value:
                    raise ValueError(f'Archive hash mismatch: {name}')
    with destination.open('rb') as stream:
        archive_hash = digest(stream)
    result = dict(scope='Manifest-selected internal preview; public release clearance remains separate',
                  releaseReady=False, package=str(package), archive=str(destination),
                  files=len(expected), bytes=destination.stat().st_size, sha256=archive_hash,
                  allEntryHashesVerified=True)
    destination.with_suffix('.verification.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(archive_preview(args.package, args.output), indent=2))
