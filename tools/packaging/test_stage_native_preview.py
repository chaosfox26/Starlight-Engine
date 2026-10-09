"""Staging refuses a missing ray-reconstruction DLL or licence before it creates anything in out/native-package-checks."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
SCRIPT = HERE / 'stage_native_preview.py'
PACKAGE_CHECKS = HERE.parents[1] / 'out' / 'native-package-checks'
MESSAGE = 'Missing external package input (nothing was staged)'

class RefusesMissingExternalInputs(unittest.TestCase):
    def snapshot(self):
        return set(os.listdir(PACKAGE_CHECKS)) if PACKAGE_CHECKS.is_dir() else set()

    def run_staging(self, *arguments):
        before = self.snapshot()
        run = subprocess.run([sys.executable, str(SCRIPT)] + list(arguments), capture_output=True, text=True, timeout=300)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn(MESSAGE, run.stderr + run.stdout)
        self.assertEqual(self.snapshot(), before)
        return run

    def test_a_missing_ray_reconstruction_dll_stops_before_anything_is_created(self):
        with tempfile.TemporaryDirectory() as temp:
            licence = Path(temp) / 'license.txt'
            licence.write_text('licence')
            run = self.run_staging('--rr-dll', str(Path(temp) / 'nvngx_dlssd.dll'), '--rr-license', str(licence))
            self.assertIn('nvngx_dlssd.dll', run.stderr + run.stdout)

    def test_a_missing_licence_stops_before_anything_is_created(self):
        with tempfile.TemporaryDirectory() as temp:
            dll = Path(temp) / 'nvngx_dlssd.dll'
            dll.write_bytes(b'dll')
            run = self.run_staging('--rr-dll', str(dll), '--rr-license', str(Path(temp) / 'missing-license.txt'))
            self.assertIn('missing-license.txt', run.stderr + run.stdout)

if __name__ == '__main__':
    unittest.main()
