"""--convert-all must refuse an inventory with an unsafe output path before it converts or writes anything."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
PROGRAM=Path(os.environ.get('STARLIGHT_BUILD_DIR') or ROOT/'out/classic/Release')/'Starlight.SceneMigration.exe'

class SceneMigrationPaths(unittest.TestCase):
    def test_bad_entry_aborts_before_any_write(self):
        scene=ROOT/'starters/empty-demo/empty-demo.starscene'
        data=scene.read_bytes()
        with tempfile.TemporaryDirectory(prefix='starlight-migration-paths-') as temp:
            base=Path(temp);stage=base/'stage'
            def entry(relative):
                return dict(path=str(scene),relative=relative,stage_relative=relative,**{'class':'authored'},kind='scene',status='ok',bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
            inventory=base/'inventory.json'
            inventory.write_text(json.dumps(dict(files=[entry('good.starscene'),entry('..\\escape.starscene')])))
            result=subprocess.run([str(PROGRAM),'--convert-all',str(inventory),'--library',str(base/'library'),'--archive',str(base/'archive'),'--stage',str(stage)],capture_output=True,timeout=120)
            self.assertNotEqual(result.returncode,0,result.stdout)
            self.assertEqual([p for p in base.rglob('*') if p.is_file() and p!=inventory],[],'nothing may be written when an inventory path is unsafe')

if __name__=='__main__':unittest.main()
