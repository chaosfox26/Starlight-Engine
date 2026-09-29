"""Open the Analysis Tool chooser without starting a test run."""
from pathlib import Path
import runpy
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
destination = root / 'out' / 'analysis-tool'
exe = destination / 'Starlight.DeepAnalysis.exe'
sources = [root / 'tools/audit/deep_analysis_gui.cpp', root / 'tools/audit/audit_theme.hpp', root / 'tools/build/build_deep_analysis.py']
if not exe.exists() or any(p.stat().st_mtime > exe.stat().st_mtime for p in sources):
    runpy.run_path(str(root / 'tools/build/build_deep_analysis.py'))['build'](destination)
subprocess.Popen([str(exe), sys.executable, str(root / 'tools/audit/deep_analysis.py'), str(root)], cwd=root)
