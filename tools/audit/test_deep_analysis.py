"""Small controlled checks of census, stream hashing, markers and PE inspection."""
import hashlib, json, os, subprocess, sys, tempfile
from pathlib import Path
import deep_analysis as a
import knowledge_report as k
import feature_evidence as fe

def main():
    with tempfile.TemporaryDirectory(prefix='starlight-analyzer-test-') as temp:
        base=Path(temp); root=base/'repo'; root.mkdir(); (root/'editor/cpp').mkdir(parents=True)
        data=b'\x00\x01all bytes\xff\r\n'; (root/'blob.dat').write_bytes(data)
        (root/'blob.dat:provenance').write_bytes(b'named stream')
        (root/'editor/cpp/sample.cpp').write_text('struct Test {};\nAPI void sl_test(void* p) {}\n#include "sample.hpp"\n',encoding='utf-8')
        (root/'Starlight Documents').mkdir(); (root/'Starlight Documents/old.cpp').write_text('old',encoding='utf-8')
        issues=[]; items=a.enumerate_files(root,issues); assert not issues and len(items)==3
        entries=[a.hash_entry(root,x) for x in items]; blob=next(x for x in entries if x['path']=='blob.dat')
        assert blob['sha256']==hashlib.sha256(data).hexdigest() and blob['stable']
        assert blob['streams'][0]['sha256']==hashlib.sha256(b'named stream').hexdigest()
        assert a.category('Starlight Documents/old.cpp').startswith('outdated')
        # Deleting a file after enumeration must surface an explicit failure.
        gone=root/'gone.bin';gone.write_bytes(b'x'); item=(gone,1,gone.stat().st_mtime_ns);gone.unlink()
        assert 'error' in a.hash_entry(root,item)
        out=base/'result';out.mkdir(); sources,index=a.source_analysis(root,entries,out)
        assert list(sources)==['editor/cpp/sample.cpp']
        assert any(s['name']=='sl_test' for s in index[0]['symbols'])
        assert all(c['missing'] for c in a.capability_analysis(sources))
        pe=a.pe_summary(Path(sys.executable));assert pe and not pe.get('error') and pe['imports']
        # Exercise report generation end-to-end on the tiny fixture, not the actual engine.
        result=base/'full-result'
        subprocess.run([sys.executable,str(Path(a.__file__)),'--root',str(root),'--output',str(result)],check=True,capture_output=True)
        state=json.loads((result/'run-state.json').read_text());assert state['bytesRead']==sum(x.stat().st_size for x in root.rglob('*') if x.is_file())
        assert (result/'Starlight-Current-Engine-Summary.md').is_file()
        assert (result/'STARLIGHT-START-HERE.md').is_file()
        knowledge=json.loads((result/'engine-knowledge.json').read_text(encoding='utf-8'))
        assert all(f['implementation_status'].startswith('Unknown') for f in knowledge['features'])
        # A catalogue availability flag must not become a claim of implementation.
        partial=k.knowledge_model([],{'feature_catalog':[{'Name':'Future feature','Available':True}]},[],[],{})
        assert partial['features']==[] and partial['catalogue'][0]['Available'] is True
        # Later failed evidence must supersede an older passing audit in the overview.
        latest=k.latest_validation([
            {'path':'audit-runs/run-20260101-000000/native-classic-ui/result.json','data':{'checks':[{'name':'x','pass':True}]}},
            {'path':'audit-runs/run-20260102-000000/tier-0-new/render.json','data':{'success':False,'error':'device lost'}}])
        assert latest['run']=='run-20260102-000000' and any('device lost' in s for s in latest['summary'])
        assert 'completed with limitations'==state['result']
        # New folder families must be discovered and surfaced, even without old run-* cohorts.
        fresh=root/'audit-runs/new-family';fresh.mkdir(parents=True)
        (fresh/'novel-result.json').write_text('{"unknownSchema":true}')
        (fresh/'broken.json').write_text('{invalid')
        found=a.saved_evidence(root)
        assert any(e['path'].endswith('novel-result.json') for e in found)
        reviewed=k.knowledge_model([],{},found,[],{})
        assert len(reviewed['evidence_review_required'])==2
        visible=k.latest_validation([{'path':'audit-runs/gate0-fresh/visible-editor/result.json',
            'data':{'checks':[{'pass':True}],'pass':True}}])
        assert visible['run'] is None and any('1/1' in x for x in visible['summary'])
        numeric=k.latest_validation([{'path':'audit-runs/editor-menu-comparison-new/summary.json','data':{'checks':76}}])
        assert not any('76/76' in x for x in numeric['summary'])
        failed_path=r'D:\new-audit\visible-editor\result.json'
        declared=k.latest_validation([{'path':failed_path,'reportEvidenceRole':'declared runtime input',
            'data':{'checks':[],'pass':False,'error':'startup failed'}},
            {'path':failed_path+'.progress.json','data':{'pass':True}}])
        assert 'explicitly supplied' in declared['summary'][0]
        assert any('startup failed' in s for s in declared['summary'])
        assert declared['paths']==[failed_path]
        fixture={'features':[{'name':'Test feature','implementation_status':'Partial','evidence':[{'file':'editor/test.cpp'}],'limits':'bounded'}]}
        linked={'path':'new.json','data':{'schema':fe.SCHEMA,'features':['test-feature'],'outcome':'pass','sourceHashes':{'editor/test.cpp':'old'},'build':{'sha256':'a'*64},'issues':[{'id':'leak','status':'open','summary':'unresolved'}]}}
        graph=fe.connect(fixture,[{'path':'editor/test.cpp','sha256':'new'}],[linked])
        assert graph['features'][0]['evidence'][0]['freshness']=='stale or missing source'
        assert graph['features'][0]['issues'][0]['status']=='open'
        linked['data']['sourceHashes']={'editor/test.cpp':'new'}
        graph=fe.connect(fixture,[{'path':'editor/test.cpp','sha256':'new'}],[linked])
        assert graph['features'][0]['evidence'][0]['freshness'].startswith('indexed source matches')
        assert graph['features'][0]['implementation']=='Partial'
        linked['data']['sourceHashes']['CMakePresets.json']='config'
        inventory=[{'path':'CMakePresets.json','sha256':'config','stable':True}]
        assert fe.connect(fixture,[{'path':'editor/test.cpp','sha256':'new'}],[linked],inventory)['features'][0]['evidence'][0]['freshness'].startswith('indexed source matches')
        inventory[0]['stable']=False
        assert fe.connect(fixture,[{'path':'editor/test.cpp','sha256':'new'}],[linked],inventory)['features'][0]['evidence'][0]['freshness']=='stale or missing source'
        linked['data']['features']=['nonexistent']
        assert fe.connect(fixture,[],[linked])['reviewRequired']
        # New user intent may postdate sealed code evidence; preserve it separately.
        direction=b'# Proposed three modes\nNot implemented.\n'
        (root/'STARLIGHT-PRODUCT-DIRECTION.md').write_bytes(direction)
        regenerated=base/'knowledge'
        refreshed=k.regenerate(result,regenerated,root)
        assert refreshed['features']==knowledge['features']
        assert knowledge['product_direction'] is None
        assert refreshed['product_direction']['sha256']==hashlib.sha256(direction).hexdigest()
        assert refreshed['product_direction']['status']=='proposed; not implementation evidence'
        assert (regenerated/'STARLIGHT-PRODUCT-DIRECTION.md').read_bytes()==direction
        assert (regenerated/'STARLIGHT-FEATURE-REFERENCE.md').is_file()
        (root/'editor/cpp/sample.cpp').write_text('changed product source',encoding='utf-8')
        try:
            k.regenerate(result,base/'must-not-generate',root)
            raise AssertionError('Changed product source was accepted as current')
        except ValueError as error:
            assert 'Product source changed' in str(error)
        again=subprocess.run([sys.executable,str(Path(a.__file__)),'--root',str(root),'--output',str(result)],capture_output=True)
        assert again.returncode!=0
    print('PASS: inventory and report checks, knowledge generation, missing-evidence downgrade, catalogue separation, latest-failure selection and overwrite refusal.')

if __name__=='__main__':main()
