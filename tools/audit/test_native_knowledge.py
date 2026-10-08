"""Preserve evidence interpretation while replacing the Python implementation."""
import os
from pathlib import Path
import unittest
import tempfile
import json
import knowledge_report as baseline
import feature_evidence

ROOT=Path(__file__).resolve().parents[2]
PROGRAM=Path(os.environ.get('STARLIGHT_BUILD_DIR') or ROOT/'out/classic/Release')/'Starlight.NativeKnowledge.exe'


class NativeKnowledgeTests(unittest.TestCase):
    def compare(self, evidence, capabilities=()):
        contract={'feature_catalog':None}; index=[{'path':'uncited.cpp'}]; provenance={'mode':'regression fixture'}
        expected=baseline.knowledge_model(list(capabilities),contract,evidence,index,provenance)
        actual=baseline.native_knowledge_model(ROOT,PROGRAM,list(capabilities),contract,evidence,index,provenance)
        self.assertEqual(expected,actual)
        return actual

    def test_missing_capability_support_stays_unknown(self):
        model=self.compare([], [{'title':'Unmapped example','interpretation':'Unproven behavior','limits':'fixture',
                                  'evidence':[],'missing':[{'file':'missing.cpp','pattern':'feature'}]}])
        self.assertFalse(model['features'][0]['source_supported'])
        self.assertIn('Unknown',model['features'][0]['implementation_status'])

    def test_empty_evidence(self):
        self.assertIsNone(self.compare([])['validation']['run'])

    def test_history_and_explicit_results_preserve_provenance(self):
        evidence=[
            {'path':r'audit-runs\run-001\native-session\session-results.json','data':{'success':True,'checks':[{}]}},
            {'path':r'audit-runs\run-002\native-session\session-results.json','data':{'success':False,'checks':[],'error':'failure'}},
            {'path':'audit-runs/run-002/native-classic-ui/result.json','data':{'checks':[None,{'name':'open','pass':True},{'name':'save','pass':False}]}},
            {'path':'audit-runs/run-999/live.progress.json','data':{'success':True}},
            {'path':'other/new.json','data':{'success':True}},
            {'path':'current/stage.json','reportEvidenceRole':'declared runtime input','data':{'outcome':'timeout','error':'still failed'}},
            {'path':'bad.json','error':'unreadable'}]
        model=self.compare(evidence)
        self.assertEqual(model['validation']['run'],'run-002')
        self.assertTrue(any(row['path']=='other/new.json' for row in model['evidence_review_required']))

    def test_mixed_render_and_visible_evidence(self):
        self.compare([
            {'path':'audit-runs/run-001/tier-new/render.json','data':{'success':True}},
            {'path':'audit-runs/run-001/tier-old/render.json','data':{'success':False,'error':'failed'}},
            {'path':'audit-runs/run-001/native-session/lifecycle-memory.json','data':{'cycles':3,'resourcesRetired':False,'baseline':{'handles':10},'final':{'handles':11}}},
            {'path':'audit-runs/gate0-001/visible-editor/result.json','data':{'pass':False,'error':'timeout','checks':None}},
            {'path':'audit-runs/gate0-001/visible-presentation/render.json','data':{'success':False}},
            {'path':'audit-runs/gate0-001/audit-verdict.json','data':{}},
            {'path':'array.json','data':[]}])

    def test_feature_evidence_freshness_and_issues(self):
        capabilities=[{'title':'Test feature','interpretation':'Fixture','limits':'Bounded test',
                       'missing':[],'evidence':[{'file':'a.cpp','line':1},{'file':'b.cpp','line':1}]}]
        index=[{'path':'a.cpp','sha256':'a'},{'path':'b.cpp','sha256':'b'}]
        inventory=[{'path':'vendor.h','sha256':'v','stable':True}]
        evidence=[]
        for name,hashes in [('unknown',{}),('stale',{'a.cpp':'old'}),('partial',{'a.cpp':'a'}),
                            ('matched',{'a.cpp':'a','b.cpp':'b','vendor.h':'v'})]:
            evidence.append({'path':name+'.json','data':{'schema':feature_evidence.SCHEMA,
                'features':['test-feature'],'sourceHashes':hashes,'outcome':'fail',
                'build':{'sha256':'f'*64},'issues':[{'id':'still-open','status':'open','summary':'Preserved'}]}})
        evidence.extend([{'path':'invalid.json','data':{'schema':feature_evidence.SCHEMA,'features':'bad'}},
                         {'path':'unknown.json','data':{'schema':feature_evidence.SCHEMA,'features':['unrecognized']}}])
        with tempfile.TemporaryDirectory() as temp:
            out=Path(temp);provenance={'mode':'fixture'};contract={'feature_catalog':None}
            model=baseline.native_knowledge_model(ROOT,PROGRAM,capabilities,contract,evidence,index,provenance,
                                                  inventory=inventory,graph_output=out)
            expected=feature_evidence.connect(model,index,evidence,inventory)
            self.assertEqual(expected,json.loads((out/'feature-status.json').read_text(encoding='utf-8')))
            self.assertEqual([row['freshness'] for row in expected['features'][0]['evidence']],
                             ['unknown: no tested source hashes','stale or missing source','partial source coverage',
                              'indexed source matches; transitive build inputs not established'])


if __name__=='__main__': unittest.main()
