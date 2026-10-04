"""Deletion boundary and retention regression checks, using temporary files only."""
import json
from pathlib import Path
import tempfile
import unittest
from artifact_cleanup import compact_build, retain_captures, compact_report, final_status


class CleanupTests(unittest.TestCase):
    def test_compact_preserves_deliverables(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); build = root/'out/test'; build.mkdir(parents=True)
            (build/'CMakeCache.txt').write_text('CMAKE_HOME_DIRECTORY:INTERNAL='+str(root))
            for name in ('part.obj', 'header.pch', 'link.ilk', 'engine.exe', 'engine.dll', 'engine.pdb', 'engine.lib', 'asset.glb', 'result.json'):
                (build/name).write_text('test')
            rows = compact_build(root, build)
            self.assertEqual(len(rows), 3)
            for name in ('engine.exe', 'engine.dll', 'engine.pdb', 'engine.lib', 'asset.glb', 'result.json'):
                self.assertTrue((build/name).exists())
            self.assertFalse((build/'part.obj').exists())
            (build/'part.obj').write_text('regenerated')
            self.assertEqual(len(compact_build(root, build)), 1)

    def test_wrong_tree_and_wrong_cache_are_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); build = root/'out/test'; build.mkdir(parents=True)
            (build/'part.obj').write_text('keep')
            (build/'CMakeCache.txt').write_text('CMAKE_HOME_DIRECTORY:INTERNAL='+str(root/'other'))
            for path in (build, root, root/'out'):
                with self.assertRaises(ValueError): compact_build(root, path)
            self.assertTrue((build/'part.obj').exists())

    def test_retention_preserves_failed_incomplete_and_recent(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            runs = {}
            for name, passed, complete in [('old', True, True), ('failed', False, True), ('incomplete', True, False), ('current', True, True)]:
                p = root/'analysis'/name; p.mkdir(parents=True); runs[name] = p
                (p/'audit-summary.json').write_text(json.dumps({'root': str(root), 'allRequestedStagesPassed': passed}))
                (p/'progress.txt').write_text('100\ndone' if complete else '60\nrunning')
                (p/'frame.bmp').write_bytes(b'capture')
                (p/'fixture.bin').write_bytes(b'keep')
            rows = retain_captures(root, runs['current'], keep=1)
            self.assertEqual(len(rows), 1)
            self.assertFalse((runs['old']/'frame.bmp').exists())
            for name in ('failed', 'incomplete', 'current'):
                self.assertTrue((runs[name]/'frame.bmp').exists())
            self.assertTrue((runs['old']/'fixture.bin').exists())
            self.assertTrue((runs['old']/'audit-summary.json').exists())



    def test_summary_cleanup_preserves_original_and_reports(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); run = root/'analysis/run'; run.mkdir(parents=True)
            for name in ('audit-summary.json', 'audit-findings.json', 'final-status.txt', 'original.glb', 'frame.bmp', 'game.exe'):
                (run/name).write_text('test')
            outside = root/'asset.glb'; outside.write_text('asset')
            rows = compact_report(root, run, ['original.glb'])
            self.assertEqual({r['path'] for r in rows}, {'frame.bmp', 'game.exe'})
            for name in ('audit-summary.json', 'audit-findings.json', 'final-status.txt', 'original.glb'):
                self.assertTrue((run/name).exists())
            self.assertTrue(outside.exists())
            with self.assertRaises(ValueError): compact_report(root, root/'analysis')

    def test_final_status_distinguishes_failures_and_historical_results(self):
        summary = {'runtimeExecuted': True, 'stages': [{'name':'build','outcome':'pass'}, {'name':'native-classic-ui','outcome':'fail'}]}
        message = final_status(summary, [{'detail':'stage deadline'}])
        self.assertIn('1 passed, 1 failed', message)
        self.assertIn('Editor controls did not finish', message)
        summary['stages'].pop()
        self.assertIn('All 1 test stages successful', final_status(summary, []))
        summary['runtimeExecuted'] = False
        self.assertIn('No new runtime tests', final_status(summary, []))


if __name__ == '__main__':
    unittest.main()
