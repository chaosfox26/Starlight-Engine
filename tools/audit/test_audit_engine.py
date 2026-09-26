"""Coordinator contract tests; no engine rendering or user scenes involved."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import audit_engine as engine


def main():
    with tempfile.TemporaryDirectory(prefix='starlight-audit-contract-') as name:
        base = Path(name)
        child = base/'lifecycle-traced'
        child.mkdir()
        progress_file = child/'lifecycle-progress.json'
        progress_file.write_text(json.dumps({'stage':'scene lifecycle','elapsedMs':12000,
                                            'completed':25,'total':80}), encoding='utf-8')
        assert 'cycles 25/80' in engine.native_progress(base, 95)
        progress_file.write_text('{', encoding='utf-8')
        assert '95s' in engine.native_progress(base, 95)
        progress_file.write_text('[]', encoding='utf-8')
        assert '95s' in engine.native_progress(base, 95)
        passed = engine.execute([sys.executable, '-c', 'print("ok")'], base, base/'pass.log', 10, lambda _: None)
        assert passed['outcome'] == 'pass'
        failed = engine.execute([sys.executable, '-c', 'raise SystemExit(7)'], base, base/'fail.log', 10, lambda _: None)
        assert failed['outcome'] == 'fail' and failed['exitCode'] == 7
        missing = engine.execute([str(base/'missing.exe')], base, base/'missing.log', 10, lambda _: None)
        assert missing['outcome'] == 'unavailable'
        timed = engine.execute([sys.executable, '-c', 'import time;time.sleep(30)'], base, base/'timeout.log', .1, lambda _: None)
        assert timed['outcome'] == 'timeout'
        stage = base/'stage'
        assert engine.test_command({'name':'native-core','command':['core.exe','old-output','reference.json']},stage) == ['core.exe',str(stage),'reference.json']
        assert engine.test_command({'name':'native-classic-ui','command':['editor.exe','--ui-check','old.json']},stage)[-1] == str(stage/'result.json')
        assert engine.test_command({'name':'native-no-managed','command':['python','guard.py']},stage)[-2:] == ['--output',str(stage/'result.json')]
        export_test={'name':'native-adventure-export','command':['python','export.py','--runtime','bin','--output','old']}
        assert engine.test_command(export_test,stage)==['python','export.py','--runtime','bin','--output',str(stage)]
        root=base/'repo';root.mkdir();out=base/'report'
        result=subprocess.run([sys.executable,str(Path(engine.__file__)), '--root',str(root),'--output',str(out),'--audit-mode','features'],capture_output=True,text=True)
        assert result.returncode == 0, result.stdout+result.stderr
        summary=json.loads((out/'audit-summary.json').read_text())
        assert summary['runtimeExecuted'] is False and summary['allRequestedStagesPassed'] is False
        assert (out/'AUDIT-SHAREABLE-REPORT.md').is_file()
        assert (out/'feature-status.json').is_file()
        again=subprocess.run([sys.executable,str(Path(engine.__file__)), '--root',str(root),'--output',str(out)],capture_output=True)
        assert again.returncode != 0
        saved=base/'saved';saved.mkdir()
        (saved/'audit-summary.json').write_text(json.dumps({'stages':[{'name':'native-core','outcome':'fail'}]}))
        (saved/'runtime-evidence.json').write_text('[]')
        (saved/'result.json').write_text(json.dumps({'checks':[{'name':'specific failure','pass':False}]}))
        reused=base/'reused'
        result=subprocess.run([sys.executable,str(Path(engine.__file__)), '--root',str(root),'--output',str(reused),'--reuse-runtime',str(saved)],capture_output=True,text=True)
        assert result.returncode == 4, result.stdout+result.stderr
        assert json.loads((reused/'audit-summary.json').read_text())['runtimeExecuted'] is False
        assert 'specific failure' in (reused/'AUDIT-SHAREABLE-REPORT.md').read_text()
    print('PASS: process outcomes, deadline cleanup, output isolation, feature-only report, no invented runtime pass, overwrite refusal')


if __name__ == '__main__':
    main()
