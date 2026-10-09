import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import audit_engine
import feature_evidence

ROOT=Path(__file__).resolve().parents[2]
PROGRAM=Path(os.environ.get('STARLIGHT_BUILD_DIR') or ROOT/'out/classic/Release')/'Starlight.NativeAuditRecords.exe'

class AuditRecords(unittest.TestCase):
    def test_build_inputs_discovered(self):
        with tempfile.TemporaryDirectory(prefix='starlight-build-inputs-') as temp:
            base=Path(temp);root=base/'source';root.mkdir()
            names=['CMakeLists.txt','editor/cpp/editor.rc','editor/cpp/editor.manifest',
                   'deep/Physics/CMakeLists.txt','deep/third_party/JoltPhysics/Jolt/Body.cpp',
                   'deep/third_party/JoltPhysics/Build/CMakeLists.txt','deep/projects/Empty/project.cpp',
                   'cmake/shaders.cmake','tools/build/renderer-transforms.json','tools/toolchain/toolchain.json']
            for name in names:
                file=root/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_text(name)
            binary=base/'fixture.exe';binary.write_bytes(b'not executed')
            request=base/'request.json';output=base/'result.json'
            request.write_text(json.dumps(dict(root=str(root),executable=str(binary),features=[],outcome='unknown',workload='input-discovery')))
            subprocess.run([str(PROGRAM),'capture',str(request),str(output)],check=True,capture_output=True)
            actual=json.loads(output.read_text())['sourceHashes']
            self.assertEqual(set(names),set(actual))
            self.assertEqual(actual,feature_evidence.capture_record(binary,root,names,[],'unknown','input-discovery')['sourceHashes'])

    def test_capture_and_findings(self):
        with tempfile.TemporaryDirectory(prefix='starlight-records-') as temp:
            base=Path(temp);root=base/'source with spaces';root.mkdir();(root/'editor/cpp').mkdir(parents=True)
            source=root/'editor/cpp/example.cpp';source.write_text('source');binary=root/'fixture.exe';binary.write_bytes(b'not executed')
            request=base/'request.json';output=base/'captured.json';features=['native-application-and-build'];sources=['editor/cpp/example.cpp']
            request.write_text(json.dumps(dict(root=str(root),executable=str(binary),sources=sources,features=features,outcome='unknown',workload='fixture')))
            subprocess.run([str(PROGRAM),'capture',str(request),str(output)],check=True,capture_output=True)
            actual=json.loads(output.read_text(encoding='utf-8'));expected=feature_evidence.capture_record(binary,root,sources,features,'unknown','fixture')
            self.assertEqual(expected,actual)
            source.write_text('changed');self.assertNotEqual(actual['sourceHashes'],feature_evidence.capture_record(binary,root,sources,features,'unknown','fixture')['sourceHashes'])
            output=base/'discovered.json';data=json.loads(request.read_text());del data['sources'];request.write_text(json.dumps(data));subprocess.run([str(PROGRAM),'capture',str(request),str(output)],check=True,capture_output=True)
            self.assertEqual(json.loads(output.read_text())['sourceHashes'],feature_evidence.capture_record(binary,root,sources,features,'unknown','fixture')['sourceHashes'])
            runs=base/'runs';runs.mkdir();(runs/'result.json').write_text(json.dumps({'error':'timeout','checks':[{'pass':False,'name':'failed check'},{'pass':True,'name':'passed'},'ignored'],'violations':['A'], 'passed':False,'case':'fixture'}))
            (runs/'render-matrix.json').write_text(json.dumps([{'passed':False,'path':'fallback'},3,{'passed':True}]))
            request.write_text(json.dumps({'root':str(runs)}));output=base/'findings.json';subprocess.run([str(PROGRAM),'findings',str(request),str(output)],check=True,capture_output=True)
            self.assertEqual(audit_engine.collect_findings(runs),json.loads(output.read_text(encoding='utf-8')))
            (runs/'audit-verdict.json').write_text(json.dumps({'evidenceChecks':[
                {'name':'debug-tier-0','accepted':False,'errors':['Direct3D debug layer is inactive']},
                {'name':'measurement-0','accepted':True,'errors':[]}]}))
            output=base/'debug-findings.json'
            subprocess.run([str(PROGRAM),'findings',str(request),str(output)],check=True,capture_output=True)
            self.assertTrue(any(row['detail']=='debug-tier-0: Direct3D debug layer is inactive' for row in json.loads(output.read_text())))
            (runs/'session-results.json').write_text('{');output=base/'malformed.json';subprocess.run([str(PROGRAM),'findings',str(request),str(output)],check=True,capture_output=True)
            self.assertTrue(any(row['detail'].startswith('Unreadable result:') for row in json.loads(output.read_text())))
            self.assertNotEqual(subprocess.run([str(PROGRAM),'findings',str(request),str(output)],capture_output=True).returncode,0)
            preserved={'file':'removed/capture/result.json','detail':'Retained failure'}
            (runs/'audit-findings.json').write_text(json.dumps([preserved]))
            request.write_text(json.dumps({'root':str(runs),'includeRetained':True}));output=base/'retained.json'
            subprocess.run([str(PROGRAM),'findings',str(request),str(output)],check=True,capture_output=True)
            self.assertIn(preserved,json.loads(output.read_text()))
            nested=runs
            for _ in range(6):nested=nested/('long-path-'+'x'*45)
            nested.mkdir(parents=True);(nested/'result.json').write_text(json.dumps({'passed':False,'case':'long-path finding'}))
            output=base/'long-path.json'
            subprocess.run([str(PROGRAM),'findings',str(request),str(output)],check=True,capture_output=True)
            self.assertTrue(any(row['detail']=='long-path finding' for row in json.loads(output.read_text())))

    def test_oversized_result_is_reported_not_dropped(self):
        with tempfile.TemporaryDirectory(prefix='starlight-oversized-') as temp:
            base=Path(temp);runs=base/'runs';runs.mkdir()
            (runs/'result.json').write_text(json.dumps({'passed':False,'case':'hidden failure','padding':'x'*(2*1024*1024+1)}))
            request=base/'request.json';request.write_text(json.dumps({'root':str(runs)}))
            output=base/'findings.json';subprocess.run([str(PROGRAM),'findings',str(request),str(output)],check=True,capture_output=True)
            actual=json.loads(output.read_text(encoding='utf-8'))
            self.assertEqual(audit_engine.collect_findings(runs),actual)
            self.assertTrue(any(row['detail'].startswith('Too large to read') for row in actual))

if __name__=='__main__':unittest.main()
