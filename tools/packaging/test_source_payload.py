"""Every repository file the root CMake build names (outside add_test lines) must be in the source package."""
import re
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from source_payload import source_files

try:
    payload = {p.relative_to(root).as_posix() for p in source_files(root)}
except ValueError as error:
    if "escapes package root" not in str(error):
        raise
    print("SKIPPED: a linked folder (for example a junctioned Jolt checkout) points outside this tree; run this test from a pristine clone")
    sys.exit(77)
named = set()
for line in (root/'CMakeLists.txt').read_text(encoding='utf-8').splitlines():
    if line.lstrip().startswith('#') or 'add_test(' in line:
        continue
    for token in re.findall(r'[A-Za-z0-9_./-]+\.(?:json|md|txt|hlsl|h|hpp|cpp|inl|cmake|py)\b', line):
        token = token.lstrip('/')
        if (root/token).is_file():
            named.add(token)
missing = sorted(named - payload)
assert named, 'no build inputs found in CMakeLists.txt'
assert not missing, 'named by CMakeLists.txt but missing from the source package: ' + ', '.join(missing)
print('ok: %d build inputs named by CMakeLists.txt are all in the source package' % len(named))
