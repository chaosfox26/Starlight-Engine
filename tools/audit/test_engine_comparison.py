import datetime as dt
import tempfile
from pathlib import Path
import engine_comparison as c

def main():
    with tempfile.TemporaryDirectory() as temp:
        root=Path(temp)
        model={'provenance':{'evidenceTimestamp':'2026-09-23'},'features':[
            {'name':'Adventure gameplay foundation','source_supported':True,'implementation_status':'Partial',
             'workflow':'Key and door','limits':'Not arbitrary gameplay','evidence':[]},
            {'name':'Unmapped feature','source_supported':False}]}
        report=c.generate(root,model,today=dt.date(2026,9,23))
        assert report['performance']['status']=='not compared'
        assert report['unmappedStarlightFeatures']==['Unmapped feature']
        assert all(len(r['competitors'])==3 for r in report['rows'])
        assert any(not f['sourceSupported'] and f['implementation'].startswith('unknown')
                   for row in report['rows'] for f in row['starlight'])
        assert all(p['url'].startswith('https://') and p['reviewed']==c.REVIEWED for row in report['rows'] for p in row['competitors'])
        stale=c.generate(root,model,today=dt.date(2027,1,1))
        assert stale['competitorFreshness']=='review required'
        assert 'Not compared' in (root/'ENGINE-COMPARISON.md').read_text(encoding='utf-8')
    print('PASS: comparison distinguishes unknown coverage, dated references and unmeasured performance')

if __name__=='__main__':main()
