"""The shader-model claim of the inline ray tracing capability must be checked against the tool that really builds the shaders."""
import json
from pathlib import Path
import re
import unittest

ROOT=Path(__file__).resolve().parents[2]

class SourceRuleMarkers(unittest.TestCase):
    def test_shader_model_claim_is_checked_in_the_shader_build_tool(self):
        rules=json.loads((ROOT/'tools/audit/source-rules.json').read_text(encoding='utf-8'))
        entry=[c for c in rules['capabilities'] if c[0]=='Inline hardware ray tracing'][0]
        self.assertIn(['tools/build/native_shaders.cpp','cs_6_5'],entry[2])
        for path,marker in entry[2]:
            self.assertTrue(re.search(marker,(ROOT/path).read_text(encoding='utf-8',errors='replace'),re.I),f'{path} does not contain {marker}')

if __name__=='__main__':unittest.main()
