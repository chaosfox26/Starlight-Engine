"""The preview package requires Starlight.SceneMigration.exe, which the editor tells users to run, and CMake builds that executable (PROJ-12)."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
manifest = json.loads((root / 'tools' / 'packaging' / 'windows-preview.json').read_text(encoding='utf-8'))
exe = 'Starlight.SceneMigration.exe'
assert exe in manifest['required'], exe + ' is not in the required package list'
cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
assert 'add_executable(Starlight.SceneMigration ' in cmake, 'the package requires an executable that CMake does not build'
print('ok: the preview package requires ' + exe)
