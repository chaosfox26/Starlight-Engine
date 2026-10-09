"""Exercise the native feature pipeline with no tools on PATH."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
PROGRAM=Path(os.environ.get('STARLIGHT_BUILD_DIR') or ROOT/'out/classic/Release')/'Starlight.NativeAnalysis.exe'

class NativeAnalysis(unittest.TestCase):
    def test_standalone_and_output_safety(self):
        with tempfile.TemporaryDirectory(prefix='starlight-native-analysis-') as temp:
            root=Path(temp)/'root';root.mkdir();(root/'asset.txt').write_text('retained')
            output=root/'analysis/run'
            args=[str(PROGRAM),'--root',str(root),'--output',str(output),'--rules',str(ROOT/'tools/audit')]
            env=os.environ.copy();env['PATH']=''
            subprocess.run(args,env=env,check=True,capture_output=True)
            state=json.loads((output/'run-state.json').read_text(encoding='utf-8'))
            self.assertEqual(state['issues'],[]);self.assertEqual(state['treeChanges'],[])
            self.assertIsNone(state['gitStart']['head']['exitCode'])
            self.assertTrue(state['missingCapabilityMarkers'])
            self.assertIn('limitations',state['result'])
            seal=json.loads((output/'artifact-hashes.json').read_text(encoding='utf-8'))
            for name,digest in seal.items():self.assertEqual(hashlib.sha256((output/name).read_bytes()).hexdigest(),digest,name)
            self.assertGreater(len(seal),15)
            snapshot={p.name:p.read_bytes() for p in output.iterdir() if p.is_file()}
            self.assertNotEqual(subprocess.run(args,env=env,capture_output=True).returncode,0)
            self.assertEqual(snapshot,{p.name:p.read_bytes() for p in output.iterdir() if p.is_file()})
            args[4]=str(root/'unsafe-output')
            self.assertNotEqual(subprocess.run(args,env=env,capture_output=True).returncode,0)
            self.assertFalse((root/'unsafe-output').exists())
            bad=Path(temp)/'bad.json';bad.write_text('{}');args[4]=str(root/'analysis/malformed');args+=['--extra-evidence',str(bad)]
            self.assertNotEqual(subprocess.run(args,env=env,capture_output=True).returncode,0)
            self.assertTrue((root/'analysis/malformed/failure.txt').is_file())
            self.assertTrue((root/'analysis/malformed/progress.txt').read_text(encoding='utf-8').startswith('-1\n'))
            self.assertEqual((root/'asset.txt').read_text(),'retained')

    def test_git_is_not_taken_from_the_analysed_tree(self):
        with tempfile.TemporaryDirectory(prefix='starlight-native-git-') as temp:
            root=Path(temp)/'root';root.mkdir();(root/'asset.txt').write_text('retained')
            shutil.copyfile(Path(os.environ.get('SystemRoot','C:\\Windows'))/'System32'/'cmd.exe',root/'git.exe')
            output=root/'analysis/run'
            args=[str(PROGRAM),'--root',str(root),'--output',str(output),'--rules',str(ROOT/'tools/audit')]
            env=os.environ.copy();env['PATH']=''
            subprocess.run(args,env=env,cwd=str(root),check=True,capture_output=True)
            state=json.loads((output/'run-state.json').read_text(encoding='utf-8'))
            self.assertIsNone(state['gitStart']['head']['exitCode'])
            self.assertIsNone(state['gitEnd']['head']['exitCode'])

if __name__=='__main__':unittest.main()
