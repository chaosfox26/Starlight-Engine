"""Guard against editing the wrong declaration or using stale locations."""
import hashlib
import json
from pathlib import Path
import tempfile
from source_lookup import lookup


def main():
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        root, run = base / 'repo', base / 'run'
        root.mkdir(); run.mkdir()
        source = b'void Update() {}\nvoid Update(int x) {}\n'
        target = root / 'menu.cpp'
        target.write_bytes(source)
        index = [{'path': 'menu.cpp', 'sha256': hashlib.sha256(source).hexdigest(),
                  'matches_inventory': True, 'symbols': [
                      {'name': 'Update', 'kind': 'function_candidates', 'line': 1},
                      {'name': 'Update', 'kind': 'function_candidates', 'line': 2}]}]
        def seal():
            raw = json.dumps(index).encode()
            (run / 'source-index.json').write_bytes(raw)
            (run / 'artifact-hashes.json').write_text(json.dumps({
                'source-index.json': hashlib.sha256(raw).hexdigest()}))
        seal()
        assert lookup(root, run, symbol='Update')['status'] == 'ambiguous'
        result = lookup(root, run, file='menu.cpp', symbol='Update',
                        kind='function_candidates', text='int x', line=2,
                        expect='void Update(int x) {}')
        assert result['status'] == 'unique'
        assert result['matches'][0]['context'][0]['line'] == 1
        assert lookup(root, run, symbol='Missing')['status'] == 'not-found'
        assert lookup(root, run, line=2, expect='wrong')['status'] == 'not-found'
        target.write_bytes(b'// shifted\n' + source)
        assert lookup(root, run, symbol='Update')['status'] == 'blocked'
        target.write_bytes(source)
        target.unlink()
        assert lookup(root, run, symbol='Update')['status'] == 'blocked'
        target.write_bytes(source)
        (run / 'source-index.json').write_text('[]')
        assert lookup(root, run, symbol='Update')['status'] == 'blocked'
        seal()
        (run / 'artifact-hashes.json').unlink()
        assert lookup(root, run, symbol='Update')['status'] == 'blocked'
        index[0]['path'] = '../outside.cpp'
        seal()
        assert lookup(root, run, symbol='Update')['status'] == 'blocked'
        assert target.read_bytes() == source
    print('Source lookup guards passed: ambiguity, combined filters, context, stale/missing source, seal, path boundary, read-only behavior.')


if __name__ == '__main__':
    main()
