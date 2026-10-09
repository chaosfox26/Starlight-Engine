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
        work = base/'work'
        work.mkdir()
        test = {'properties': [{'name': 'ENVIRONMENT', 'value': ['AUDIT_CONTEXT_CHECK=a=b', 'AUDIT_CONTEXT_OTHER=1']},
                               {'name': 'WORKING_DIRECTORY', 'value': str(work)}]}
        directory, env = engine.test_context(test, base)
        assert directory == str(work), directory
        assert env['AUDIT_CONTEXT_CHECK'] == 'a=b' and env['AUDIT_CONTEXT_OTHER'] == '1', 'ENVIRONMENT entries split at the first ='
        assert 'PATH' in env or 'Path' in env, 'the parent environment is kept'
        bare_directory, bare_env = engine.test_context({'command': ['x']}, base)
        assert bare_directory == base and 'AUDIT_CONTEXT_CHECK' not in bare_env, 'a test without properties runs from the source root'
        probe = base/'probe.txt'
        code = f'import os;open(r"{probe}","w").write(os.getcwd()+"|"+os.environ["AUDIT_CONTEXT_CHECK"])'
        result = engine.execute([sys.executable, '-c', code], directory, base/'probe.log', 30, lambda _: None, env)
        assert result['outcome'] == 'pass', result
        assert probe.read_text() == f'{work}|a=b', probe.read_text()
        wired = base/'wired.txt'
        code = f'import os;open(r"{wired}","w").write(os.getcwd()+"|"+os.environ["AUDIT_CONTEXT_CHECK"])'
        result = engine.run_registered_test(dict(test, command=[sys.executable, '-c', code]), [sys.executable, '-c', code], base, base/'wired.log', lambda _: None)
        assert result['outcome'] == 'pass', result
        assert wired.read_text() == f'{work}|a=b', 'the coordinator runs a registered test with its CTest working directory and environment'
    print('audit coordinator contracts pass')


if __name__ == '__main__':
    main()
