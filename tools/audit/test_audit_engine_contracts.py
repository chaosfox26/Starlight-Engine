"""Contracts for the audit coordinator's failure handling (audit item A5); no engine, GPU or user scenes involved."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import audit_engine as engine


def alive(pid):
    out = subprocess.run(['tasklist', '/FI', f'PID eq {pid}', '/NH'], capture_output=True, text=True).stdout
    return str(pid) in out


def main():
    with tempfile.TemporaryDirectory(prefix='starlight-audit-a5-') as name:
        base = Path(name)
        big = base/'result.json'
        big.write_text(json.dumps({'pad': 'x'*(2*1024*1024+10)}), encoding='utf-8')
        findings = engine.collect_findings(base)
        assert any('Too large' in f['detail'] and f['file'] == str(big) for f in findings), findings
        small = base/'sub'
        small.mkdir()
        (small/'result.json').write_text(json.dumps({'error': 'boom'}), encoding='utf-8')
        assert any(f['detail'] == 'boom' for f in engine.collect_findings(base))
        pid_file = base/'child.pid'
        code = f'import os,time;open(r"{pid_file}","w").write(str(os.getpid()));time.sleep(60)'
        calls = []

        def failing_progress(_):
            calls.append(1)
            if pid_file.exists():
                raise RuntimeError('progress callback failed')
        try:
            engine.execute([sys.executable, '-c', code], base, base/'child.log', 30, failing_progress)
            raise AssertionError('the progress failure should have propagated')
        except RuntimeError as error:
            assert 'progress callback failed' in str(error)
        time.sleep(1)
        child = int(pid_file.read_text())
        assert not alive(child), 'the child process was left running after progress() raised'
    print('audit coordinator contracts pass')


if __name__ == '__main__':
    main()
