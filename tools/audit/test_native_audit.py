import hashlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
RUNTIME=ROOT/'out/classic/Release'

class NativeAudit(unittest.TestCase):
    def test_prebuilt_runtime_outcomes(self):
        with tempfile.TemporaryDirectory(prefix='starlight-runtime-coordinator-') as temp:
            root=Path(temp)/'root';root.mkdir();out=root/'analysis/runtime'
            def stage(name,args,exe='Starlight.NativeProcessChecks.exe'):
                return dict(name=name,executable=exe,arguments=args,timeoutSeconds=1,features=['diagnostics-and-existing-audits'])
            plan=Path(temp)/'plan.json'
            plan.write_text(json.dumps(dict(format='starlight-runtime-checks-v1',profile='prebuilt-runtime',stages=[
                stage('success',['--child','space and \\"quote','trailing\\','']),
                stage('failure',['--child','fail']),
                stage('deadline',['--child','wait']),
                stage('missing',[],'missing.exe'),
            ])))
            args=[str(RUNTIME/'Starlight.NativeAudit.exe'),'--root',str(root),'--output',str(out),'--rules',str(ROOT/'tools/audit'),'--runtime-dir',str(RUNTIME),'--audit-mode','errors','--test-plan',str(plan),'--keep-artifacts']
            env=os.environ.copy();env['PATH']=''
            result=subprocess.run(args,env=env,capture_output=True)
            self.assertEqual(result.returncode,4,result.stderr)
            summary=json.loads((out/'audit-summary.json').read_text(encoding='utf-8'))
            self.assertTrue(summary['runtimeExecuted']);self.assertFalse(summary['sourceBuildExecuted'])
            self.assertEqual([s['outcome'] for s in summary['stages']],['pass','fail','timeout','unavailable'])
            self.assertEqual(summary['stages'][1]['exitCode'],7)
            self.assertIn('1 passed, 2 failed, 1 unavailable',(out/'final-status.txt').read_text())
            evidence=json.loads((out/'runtime-evidence.json').read_text())
            self.assertEqual([e['data']['outcome'] for e in evidence],['pass','fail','timeout'])
            self.assertFalse((out/'run-state.json').exists())

    def test_missing_dll_reports_without_blocking_dialog(self):
        with tempfile.TemporaryDirectory(prefix='starlight-missing-dll-') as temp:
            root=Path(temp);runtime=root/'runtime';runtime.mkdir()
            shutil.copy2(RUNTIME/'Starlight.NativeRenderChecks.exe',runtime/'Starlight.NativeRenderChecks.exe')
            plan=root/'plan.json'
            plan.write_text(json.dumps(dict(format='starlight-runtime-checks-v1',profile='prebuilt-runtime',stages=[dict(name='missing-library',executable='Starlight.NativeRenderChecks.exe',arguments=[],timeoutSeconds=5,features=['diagnostics-and-existing-audits'])])))
            out=root/'analysis/result';env=os.environ.copy();env['PATH']=''
            result=subprocess.run([str(RUNTIME/'Starlight.NativeAudit.exe'),'--root',str(root),'--output',str(out),'--runtime-dir',str(runtime),'--rules',str(ROOT/'tools/audit'),'--audit-mode','errors','--test-plan',str(plan)],env=env,capture_output=True,timeout=15)
            self.assertEqual(result.returncode,4,result.stderr)
            summary=json.loads((out/'audit-summary.json').read_text())
            self.assertEqual(summary['stages'][0]['exitCode'],0xc0000135)
            self.assertIn('required runtime DLL',(out/'final-status.txt').read_text())

    def test_automatic_capture_retention(self):
        with tempfile.TemporaryDirectory(prefix='starlight-retention-coordinator-') as temp:
            root=Path(temp)/'root';root.mkdir();runs=[]
            for number in range(4):
                old=root/'analysis'/f'old-{number}';old.mkdir(parents=True);runs.append(old)
                summary=old/'audit-summary.json'
                summary.write_text(json.dumps(dict(root=str(root),runtimeExecuted=True,stages=[dict(name='fixture',outcome='pass')],allRequestedStagesPassed=True)))
                os.utime(summary,(1000+number,1000+number))
                (old/'runtime-evidence.json').write_text('[]')
                (old/'progress.txt').write_text('100\nFinished')
                (old/'capture.bmp').write_bytes(b'fixture screenshot')
                (old/'summary.md').write_text('Keep written evidence')
            out=root/'analysis/new'
            result=subprocess.run([str(RUNTIME/'Starlight.NativeAudit.exe'),'--root',str(root),'--output',str(out),'--rules',str(ROOT/'tools/audit'),'--runtime-dir',str(RUNTIME)],capture_output=True)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertFalse((runs[0]/'capture.bmp').exists())
            self.assertFalse((runs[1]/'capture.bmp').exists())
            self.assertTrue((runs[2]/'capture.bmp').exists())
            self.assertTrue((runs[3]/'capture.bmp').exists())
            self.assertTrue(all((old/'summary.md').is_file() for old in runs))
            receipt=json.loads((out/'capture-retention.json').read_text())
            self.assertEqual(len(receipt['files']),2)
            self.assertTrue(all(item['status']=='removed' for item in receipt['files']))

    def test_feature_run_reuse_and_compaction(self):
        with tempfile.TemporaryDirectory(prefix='starlight-coordinator-') as temp:
            root=Path(temp)/'root with spaces';root.mkdir();(root/'asset.txt').write_text('preserved')
            previous=root/'analysis/previous';previous.mkdir(parents=True)
            (previous/'audit-summary.json').write_text(json.dumps(dict(root=str(root),runtimeExecuted=True,stages=[dict(name='example',outcome='fail')],allRequestedStagesPassed=False)))
            (previous/'runtime-evidence.json').write_text('[]')
            findings=[dict(file='removed/result.json',detail='Retained diagnosis')]
            (previous/'audit-findings.json').write_text(json.dumps(findings))
            out=root/'analysis/new'
            env=os.environ.copy();env['PATH']=''
            args=[str(RUNTIME/'Starlight.NativeAudit.exe'),'--root',str(root),'--output',str(out),'--rules',str(ROOT/'tools/audit'),'--runtime-dir',str(RUNTIME)]
            result=subprocess.run(args,env=env,capture_output=True)
            self.assertEqual(result.returncode,4,result.stderr)
            summary=json.loads((out/'audit-summary.json').read_text(encoding='utf-8'))
            self.assertFalse(summary['runtimeExecuted']);self.assertFalse(summary['allRequestedStagesPassed'])
            self.assertEqual(summary['reusedRuntime'],str(previous))
            self.assertEqual(json.loads((out/'audit-findings.json').read_text(encoding='utf-8')),findings)
            self.assertTrue((out/'STARLIGHT-START-HERE.md').is_file())
            self.assertFalse((out/'byte-manifest.jsonl').exists())
            self.assertTrue((out/'cleanup-records').is_dir())
            self.assertIn('historical',(out/'final-status.txt').read_text(encoding='utf-8'))
            for name,digest in json.loads((out/'audit-deliverable-hashes.json').read_text()).items():
                self.assertEqual(hashlib.sha256((out/name).read_bytes()).hexdigest(),digest)
            self.assertNotEqual(subprocess.run(args,env=env,capture_output=True).returncode,0)
            self.assertEqual((root/'asset.txt').read_text(),'preserved')

if __name__=='__main__':unittest.main()
