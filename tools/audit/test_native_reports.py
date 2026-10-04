"""Native report parity, malformed input and evidence-preservation checks."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import deep_analysis as reference

ROOT=Path(__file__).resolve().parents[2]
PROGRAM=ROOT/'out/classic/Release/Starlight.NativeReports.exe'

class NativeReports(unittest.TestCase):
    def test_reports_and_rejection(self):
        with tempfile.TemporaryDirectory(prefix='starlight-reports-test-') as temp:
            base=Path(temp);old=base/'python';old.mkdir()
            state=dict(finishedUtc='fixture',bytesRead=1234567,streams=1,streamBytes=4,stableFiles=1,issues=[{'error':'retained'}],treeChanges=[],gitStart={'head':{'stdout':' hash \n'},'status':{'stdout':' M example\n'}},manifestSha256='fixture')
            data=dict(version=reference.VERSION,state=state,files=[dict(category='source',bytes_read=1234567)],index=[dict(path='editor/cpp/example.cpp',lines=3,sha256='fixture',symbols=[dict(kind='class',name='Example',line=1)],includes=['a.h'],lexical_counts={'allocation':2})],capabilities=[dict(title='Example',status='missing',interpretation='not proven',limits='fixture only',missing=[dict(file='example.cpp',pattern='required')],evidence=[dict(file='example.cpp',line=2,matched='`sample`')])],build={'cmake_declarations':[dict(command='option',arguments='  a\n b|c ',file='CMakeLists.txt',line=4)]},contracts=dict(limits='Not runtime proof',feature_catalog={'z':'星','a':True},records=[dict(file='example.cpp',line=1,kind='native_exports',text='code')]),evidence=[dict(path='audit-runs/tier-one.json',data={'success':False,'error':'a|b'}),dict(path='classic-ui.json',data={'pass':False,'checks':[{'name':'missing','pass':False},'ignored'],'window_creation_trace':None}),dict(path='session-results.json',data={'success':False,'error':'timeout'}),dict(path='lifecycle-memory.json',data={'cycles':2,'final':{'b':1,'a':2}})],binaries=[{}])
            reference.contract_report(ROOT,old,data['contracts'])
            reference.report(ROOT,old,*[data[k] for k in ('state','files','index','capabilities','build','evidence','binaries')])
            source=base/'input.json';source.write_text(json.dumps(data,ensure_ascii=False),encoding='utf-8')
            args=[str(PROGRAM),str(ROOT),str(source),str(ROOT/'tools/audit/report-templates.json'),str(base/'native')]
            subprocess.run(args,check=True,capture_output=True)
            for p in old.glob('*.md'):self.assertEqual(p.read_text(encoding='utf-8'),(base/'native'/p.name).read_text(encoding='utf-8'),p.name)
            self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
            source.write_text('{}',encoding='utf-8');args[-1]=str(base/'empty')
            self.assertNotEqual(subprocess.run(args,capture_output=True).returncode,0)
            self.assertFalse((base/'empty').exists())

if __name__=='__main__':unittest.main()
