"""stage_native_preview.py refuses a missing external input before it creates a package folder (PROJ-13)."""
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
staging = root / 'out' / 'native-package-checks'
missing = root / 'out' / 'no-such-folder' / 'nvngx_dlssd.dll'
before = set(staging.iterdir()) if staging.is_dir() else set()
result = subprocess.run([sys.executable, str(root / 'tools' / 'packaging' / 'stage_native_preview.py'),
                         '--rr-dll', str(missing), '--rr-license', str(root / 'LICENSE')],
                        capture_output=True, text=True, cwd=str(root))
after = set(staging.iterdir()) if staging.is_dir() else set()
assert result.returncode != 0, 'a missing ray-reconstruction DLL must stop staging'
assert 'Missing external package input (nothing was staged)' in result.stderr and str(missing) in result.stderr, result.stderr
assert after == before, 'staging created a package folder before refusing: ' + ', '.join(sorted(p.name for p in after - before))
print('ok: staging refuses a missing external package input before creating a package folder')
